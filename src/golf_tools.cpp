#include "golf_tools.h"
#include <sstream>
#include <cstdio>
#include <cmath>
#include <deque>
#include <limits>
#include <algorithm>

// ---------- export to C++ ----------

// number -> short C++ literal: 14, -2.5f, 0.22f
static std::string num(float v) {
    if (std::fabs(v) < 0.0005f) v = 0.0f;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.3f", v);
    std::string s = buf;
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') { s.pop_back(); return s; }
    return s + "f";
}

static std::string quoted(const std::string& text) {
    std::string out = "\"";
    for (char c : text) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + "\"";
}

static std::string stampToCpp(const Stamp& s) {
    std::ostringstream o;
    switch (s.type) {
    case Stamp::Bump:
        o << "Stamp::bump(" << num(s.center.x) << ", " << num(s.center.y) << ", " << num(s.radius) << ", " << num(s.height) << ")";
        break;
    case Stamp::Ramp:
        o << "Stamp::ramp('" << s.axis << "', " << num(s.from) << ", " << num(s.to) << ", " << num(s.height)
          << (s.launch ? ", true" : "") << ")";
        break;
    case Stamp::Rollers:
        o << "Stamp::rollers('" << s.axis << "', " << num(s.height) << ", " << num(s.wavelength) << ")";
        break;
    case Stamp::Tilt:
        o << "Stamp::tilt(" << num(s.slope.x) << ", " << num(s.slope.y) << ")";
        break;
    case Stamp::Pipe:
        o << "Stamp::pipe('" << s.axis << "', " << num(s.line) << ", " << num(s.curve) << ")";
        break;
    }
    if (s.limited)
        o << ".limit(" << num(s.area.min.x) << ", " << num(s.area.min.y) << ", "
          << num(s.area.max.x) << ", " << num(s.area.max.y) << ", " << num(s.edge) << ")";
    return o.str();
}

std::string holeToCpp(const Hole& h) {
    const std::string in = "        ";
    std::ostringstream o;
    o << "    // " << h.name << " (made with the level editor)\n";
    o << "    {\n";
    o << in << "Hole h;\n";
    o << in << "h.name = " << quoted(h.name) << ";\n";
    o << in << "h.par = " << h.par << ";\n";
    o << in << "h.start = glm::vec3(" << num(h.start.x) << ", 0, " << num(h.start.z) << ");\n";
    o << in << "h.startAim = " << num(h.startAim) << ";\n";
    o << in << "h.cup = glm::vec3(" << num(h.cup.x) << ", 0, " << num(h.cup.z) << ");\n";
    o << in << "h.cupRadius = " << num(h.cupRadius) << ";\n";
    if (h.path.size() >= 2) {
        o << in << "h.path = { ";
        for (size_t i = 0; i < h.path.size(); ++i)
            o << (i ? ", " : "") << "{" << num(h.path[i].x) << ", " << num(h.path[i].y) << "}";
        o << " };\n";
        o << in << "h.pathWidth = " << num(h.pathWidth) << ";\n";
    } else if (h.autoWalls) {
        o << in << "h.autoWalls = true;\n";
    }
    for (const Rect& r : h.rects)
        o << in << "h.rects.push_back(Rect{ {" << num(r.min.x) << ", " << num(r.min.y) << "}, {"
          << num(r.max.x) << ", " << num(r.max.y) << "} });\n";
    for (const Box& w : h.walls) {
        if (std::fabs(w.min.y) < 0.001f && std::fabs(w.max.y - 1.0f) < 0.001f)
            o << in << "h.walls.push_back(block(" << num(w.min.x) << ", " << num(w.min.z) << ", "
              << num(w.max.x) << ", " << num(w.max.z) << "));\n";
        else
            o << in << "h.walls.push_back(Box{ glm::vec3(" << num(w.min.x) << ", " << num(w.min.y) << ", " << num(w.min.z)
              << "), glm::vec3(" << num(w.max.x) << ", " << num(w.max.y) << ", " << num(w.max.z) << ") });\n";
    }
    for (const Bumper& b : h.bumpers)
        o << in << "h.bumpers.push_back(Bumper{ glm::vec3(" << num(b.center.x) << ", 0, " << num(b.center.z)
          << "), " << num(b.radius) << " });\n";
    for (const Stamp& s : h.stamps)
        o << in << "h.stamps.push_back(" << stampToCpp(s) << ");\n";
    if (h.height)
        o << in << "// NOTE: the original hole also had a custom height function written in code;\n"
          << in << "// the editor can't export that, so copy it over by hand if you still want it.\n";
    o << in << "course.push_back(h);\n";
    o << "    }\n";
    return o.str();
}

// ---------- bot ----------

namespace {
// Distance from every 1x1 cell of the course to the cup, walking around walls
// and bumpers (a breadth-first search outward from the cup).
struct DistanceMap {
    glm::vec2 lo;
    int w = 0, hgt = 0;
    std::vector<float> dist;
    const float far = 1e6f;

    void build(const GolfGame& g) {
        const Hole& h = g.hole();
        lo = glm::vec2(1e9f);
        glm::vec2 hi(-1e9f);
        for (const Rect& r : h.greens) { lo = glm::min(lo, r.min); hi = glm::max(hi, r.max); }
        lo = glm::floor(lo);
        w = (int)std::ceil(hi.x - lo.x);
        hgt = (int)std::ceil(hi.y - lo.y);
        dist.assign((size_t)w * hgt, far);
        float r = g.ballRadius();

        auto walkable = [&](int ix, int iz) {
            float x = lo.x + ix + 0.5f, z = lo.y + iz + 0.5f;
            if (!g.onGreen(x, z, std::min(r, 0.49f))) return false;
            for (const Box& b : h.walls)
                if (x > b.min.x - r && x < b.max.x + r && z > b.min.z - r && z < b.max.z + r) return false;
            for (const Bumper& b : h.bumpers)
                if (glm::length(glm::vec2(x - b.center.x, z - b.center.z)) < b.radius + r) return false;
            return true;
        };
        std::vector<char> ok((size_t)w * hgt);
        for (int iz = 0; iz < hgt; ++iz)
            for (int ix = 0; ix < w; ++ix) ok[iz * w + ix] = walkable(ix, iz);

        int cx = std::clamp((int)std::floor(h.cup.x - lo.x), 0, w - 1);
        int cz = std::clamp((int)std::floor(h.cup.z - lo.y), 0, hgt - 1);
        std::deque<int> queue;
        dist[cz * w + cx] = 0.0f;
        queue.push_back(cz * w + cx);
        while (!queue.empty()) {
            int c = queue.front(); queue.pop_front();
            int ix = c % w, iz = c / w;
            const int dx[4] = {1, -1, 0, 0}, dz[4] = {0, 0, 1, -1};
            for (int k = 0; k < 4; ++k) {
                int nx = ix + dx[k], nz = iz + dz[k];
                if (nx < 0 || nz < 0 || nx >= w || nz >= hgt) continue;
                int n = nz * w + nx;
                if (!ok[n] || dist[n] <= dist[c] + 1.0f) continue;
                dist[n] = dist[c] + 1.0f;
                queue.push_back(n);
            }
        }
    }

    float at(glm::vec3 p) const {
        int ix = (int)std::floor(p.x - lo.x), iz = (int)std::floor(p.z - lo.y);
        if (ix < 0 || iz < 0 || ix >= w || iz >= hgt) return far;
        return dist[iz * w + ix];
    }
};

// play one shot to the end
void simulateShot(GolfGame& g, float aim, float power) {
    g.hit(aim, power);
    for (int i = 0; i < 60 * 60 && g.state() == BallState::Rolling; ++i) g.update(1.0f / 60.0f);
}
} // namespace

BotResult runBot(const GolfGame& original, int holeIndex, int maxStrokes) {
    GolfGame game = original;
    game.loadHole(holeIndex);
    DistanceMap map;
    map.build(game);

    BotResult result;
    while (game.state() != BallState::Sunk && result.strokes < maxStrokes) {
        float bestScore = std::numeric_limits<float>::max(), bestAim = 0.0f, bestPower = 0.5f;
        bool found = false;
        for (float aim = -3.14f; aim < 3.14f && !found; aim += 0.05f)
            for (float power = 0.1f; power <= 1.001f; power += 0.1f) {
                GolfGame trial = game;
                simulateShot(trial, aim, power);
                if (trial.wasReset()) continue;
                float score = (trial.state() == BallState::Sunk)
                    ? -1.0f
                    : map.at(trial.ballPosition()) + 0.1f * glm::length(trial.ballPosition() - trial.hole().cup);
                if (score < bestScore) { bestScore = score; bestAim = aim; bestPower = power; }
                if (score < 0.0f) { found = true; break; }
            }
        simulateShot(game, bestAim, bestPower);
        result.strokes++;
        result.restPoints.push_back(game.ballPosition());
    }
    result.sunk = game.state() == BallState::Sunk;
    return result;
}
