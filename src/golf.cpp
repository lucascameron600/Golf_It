#include "golf.h"
#include <cmath>
#include <algorithm>

// ---------- small math helpers for terrain ----------

// smooth 0 -> 1 as t goes from a to b (flat at both ends, so no sharp kinks).
// Works in either direction (b can be less than a).
static float smoothRamp(float t, float a, float b) {
    float x = std::clamp((t - a) / (b - a), 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}
// 0 outside [a, b], 1 inside, with smooth edges of width `edge`
static float band(float t, float a, float b, float edge) {
    if (edge <= 0.0f) return (t >= a && t <= b) ? 1.0f : 0.0f;
    return smoothRamp(t, a, a + edge) * (1.0f - smoothRamp(t, b - edge, b));
}

// ---------- terrain stamps ----------

Stamp Stamp::bump(float x, float z, float radius, float height) {
    Stamp s; s.type = Bump; s.center = glm::vec2(x, z); s.radius = radius; s.height = height; return s;
}
Stamp Stamp::ramp(char axis, float from, float to, float rise, bool launch) {
    Stamp s; s.type = Ramp; s.axis = axis; s.from = from; s.to = to; s.height = rise; s.launch = launch; return s;
}
Stamp Stamp::rollers(char axis, float waveHeight, float wavelength) {
    Stamp s; s.type = Rollers; s.axis = axis; s.height = waveHeight; s.wavelength = wavelength; return s;
}
Stamp Stamp::tilt(float slopeX, float slopeZ) {
    Stamp s; s.type = Tilt; s.slope = glm::vec2(slopeX, slopeZ); return s;
}
Stamp Stamp::pipe(char axis, float line, float curve) {
    Stamp s; s.type = Pipe; s.axis = axis; s.line = line; s.curve = curve; return s;
}
Stamp Stamp::limit(float minX, float minZ, float maxX, float maxZ, float edgeWidth) const {
    Stamp s = *this;
    s.limited = true;
    s.area = Rect{ {std::min(minX, maxX), std::min(minZ, maxZ)}, {std::max(minX, maxX), std::max(minZ, maxZ)} };
    s.edge = edgeWidth;
    return s;
}

float Stamp::evaluate(float x, float z) const {
    float along = (axis == 'x') ? x : z;   // position along the stamp's direction
    float across = (axis == 'x') ? z : x;  // position across it
    float h = 0.0f;
    switch (type) {
    case Bump: {
        float d2 = (x - center.x) * (x - center.x) + (z - center.y) * (z - center.y);
        h = height * std::exp(-d2 / (2.0f * radius * radius));
        break;
    }
    case Ramp:
        if (launch) {
            float t = std::clamp((along - from) / (to - from), 0.0f, 1.0f);
            h = height * t * t;              // steepest at the end -> launches the ball
        } else {
            h = height * smoothRamp(along, from, to);
        }
        break;
    case Rollers:
        h = height * std::sin(6.2831853f * along / wavelength);
        break;
    case Tilt:
        h = slope.x * x + slope.y * z;
        break;
    case Pipe:
        h = curve * (across - line) * (across - line);
        break;
    }
    if (limited)
        h *= band(x, area.min.x, area.max.x, edge) * band(z, area.min.y, area.max.y, edge);
    return h;
}

float holeGroundHeight(const Hole& h, float x, float z) {
    float y = h.height ? h.height(x, z) : 0.0f;
    for (const Stamp& s : h.stamps) y += s.evaluate(x, z);
    return y;
}

// ---------- course building helpers ----------

static const float WALL_HEIGHT = 1.0f;

// a wall/block covering x1..x2, z1..z2 on the ground
static Box block(float x1, float z1, float x2, float z2) {
    return Box{ glm::vec3(std::min(x1, x2), 0.0f, std::min(z1, z2)),
                glm::vec3(std::max(x1, x2), WALL_HEIGHT, std::max(z1, z2)) };
}

// four walls (1 unit thick) around a rectangle of green
static void addBorder(Hole& h, float minX, float minZ, float maxX, float maxZ) {
    h.walls.push_back(block(minX - 1, minZ - 1, maxX + 1, minZ));     // north
    h.walls.push_back(block(minX - 1, maxZ,     maxX + 1, maxZ + 1)); // south
    h.walls.push_back(block(minX - 1, minZ,     minX,     maxZ));     // west
    h.walls.push_back(block(maxX,     minZ,     maxX + 1, maxZ));     // east
}

void finalizeHole(Hole& h) {
    // greens = the rectangles plus one rectangle per leg of the path.
    // Legs overlap at the corners so turns are filled in. A diagonal leg is
    // turned into two straight legs (along x first, then along z).
    h.greens = h.rects;
    float half = h.pathWidth * 0.5f;
    auto addLeg = [&](glm::vec2 a, glm::vec2 b) {
        h.greens.push_back(Rect{ glm::min(a, b) - glm::vec2(half), glm::max(a, b) + glm::vec2(half) });
    };
    for (size_t i = 0; i + 1 < h.path.size(); ++i) {
        glm::vec2 a = h.path[i], b = h.path[i + 1];
        if (a.x != b.x && a.y != b.y) {
            glm::vec2 corner(b.x, a.y);
            addLeg(a, corner);
            addLeg(corner, b);
        } else {
            addLeg(a, b);
        }
    }
    if (h.path.size() >= 2) h.autoWalls = true;

    // put the start, cup and bumpers on top of the ground
    h.start.y = holeGroundHeight(h, h.start.x, h.start.z);
    h.cup.y = holeGroundHeight(h, h.cup.x, h.cup.z);
    for (Bumper& b : h.bumpers) b.center.y = holeGroundHeight(h, b.center.x, b.center.z);
}

static std::vector<Hole> buildCourse() {
    std::vector<Hole> course;

    // Hole 1: straight, with a block in the middle to go around (or bank past)
    {
        Hole h;
        h.name = "Straight Shot";
        h.par = 2;
        h.start = glm::vec3(0, 0, 25);
        h.startAim = 0.0f;
        h.cup = glm::vec3(0, 0, -24);
        h.cupRadius = 0.85f;
        h.rects.push_back(Rect{ {-6, -30}, {6, 30} });
        addBorder(h, -6, -30, 6, 30);
        h.walls.push_back(block(-3, -1, 3, 1));
        course.push_back(h);
    }

    // Hole 2: L-shaped dogleg to the right, with a block guarding the cup
    {
        Hole h;
        h.name = "Dogleg";
        h.par = 3;
        h.start = glm::vec3(0, 0, 0);
        h.startAim = 0.0f;
        h.cup = glm::vec3(25, 0, -24);
        h.cupRadius = 0.85f;
        h.rects.push_back(Rect{ {-6, -30}, {6, 6} });    // up the left side
        h.rects.push_back(Rect{ {6, -30}, {30, -18} });  // across the top
        h.walls.push_back(block(-7, -31, 31, -30));  // north
        h.walls.push_back(block(-7, 6, 7, 7));       // south end of the first leg
        h.walls.push_back(block(-7, -30, -6, 6));    // west
        h.walls.push_back(block(6, -18, 7, 6));      // inside of the bend
        h.walls.push_back(block(6, -18, 31, -17));   // south side of the second leg
        h.walls.push_back(block(30, -30, 31, -17));  // east end
        h.walls.push_back(block(17, -27, 19, -21));  // block in front of the cup
        course.push_back(h);
    }

    // Hole 3: a field of bouncy bumpers
    {
        Hole h;
        h.name = "Bumper Alley";
        h.par = 3;
        h.start = glm::vec3(0, 0, 30);
        h.startAim = 0.0f;
        h.cup = glm::vec3(0, 0, -30);
        h.cupRadius = 0.85f;
        h.rects.push_back(Rect{ {-10, -35}, {10, 35} });
        addBorder(h, -10, -35, 10, 35);
        float r = 1.5f;
        glm::vec2 posts[] = { {-5, 18}, {5, 18}, {0, 8}, {-6, -2}, {6, -2},
                              {0, -12}, {-4, -21}, {4, -21} };
        for (glm::vec2 p : posts) h.bumpers.push_back(Bumper{ glm::vec3(p.x, 0, p.y), r });
        course.push_back(h);
    }

    // Hole 4: a long downhill run with rollers and a banked (tilted) section
    {
        Hole h;
        h.name = "Downhill Run";
        h.par = 4;
        h.start = glm::vec3(0, 0, 0);
        h.startAim = 0.0f;
        h.cup = glm::vec3(0, 0, -255);
        h.cupRadius = 0.85f;
        h.path = { {0, 4}, {0, -262} };
        h.pathWidth = 14;
        h.stamps.push_back(Stamp::ramp('z', -5, -235, -32));                                  // 32 units downhill
        h.stamps.push_back(Stamp::rollers('z', 1.5f, 37.7f).limit(-50, -110, 50, -30, 15));  // bumpy waves
        h.stamps.push_back(Stamp::tilt(0.22f, 0).limit(-50, -200, 50, -120, 20));           // tilts to one side
        h.stamps.push_back(Stamp::bump(0, -255, 6, -1.2f));                                   // dip around the cup
        h.bumpers.push_back(Bumper{ glm::vec3(-3, 0, -150), 1.2f });
        h.bumpers.push_back(Bumper{ glm::vec3(3, 0, -175), 1.2f });
        h.bumpers.push_back(Bumper{ glm::vec3(-2, 0, -215), 1.2f });
        course.push_back(h);
    }

    // Hole 5: switchbacks down a mountain; the whole course leans downhill to +x,
    // so the long legs are tilted and the ball drifts toward the lower wall
    {
        Hole h;
        h.name = "Switchback Mountain";
        h.par = 7;
        h.start = glm::vec3(0, 0, 0);
        h.startAim = 0.0f;
        h.cup = glm::vec3(100, 0, -100);
        h.cupRadius = 0.9f;
        h.path = { {0, 4}, {0, -90}, {50, -90}, {50, -10}, {100, -10}, {100, -100} };
        h.pathWidth = 14;
        h.stamps.push_back(Stamp::ramp('x', -5, 93, -18));                                   // the mountain
        h.stamps.push_back(Stamp::rollers('z', 0.7f, 25.13f).limit(42, -80, 58, -20, 4));   // ripples
        h.stamps.push_back(Stamp::bump(100, -100, 5, -1.5f));                                // dip around the cup
        h.walls.push_back(block(-2, -45, 2, -41));     // blocks to steer around
        h.walls.push_back(block(96, -55, 104, -52));
        course.push_back(h);
    }

    // Hole 6: a U-shaped halfpipe with a launch ramp; land it and roll into the cup
    {
        Hole h;
        h.name = "Halfpipe Jump";
        h.par = 4;
        h.start = glm::vec3(0, 0, 0);
        h.startAim = 0.0f;
        h.cup = glm::vec3(0, 0, -180);
        h.cupRadius = 0.9f;
        h.path = { {0, 6}, {0, -186} };
        h.pathWidth = 24;
        h.stamps.push_back(Stamp::pipe('z', 0, 0.07f));            // U shape: sides curve up
        h.stamps.push_back(Stamp::ramp('z', -65, -85, 3, true));   // launch ramp, steepest at the lip
        h.stamps.push_back(Stamp::ramp('z', -85, -89, -4));        // drops away after the lip
        h.stamps.push_back(Stamp::bump(0, -180, 6, -1.5f));        // dip around the cup
        course.push_back(h);
    }

    // ---- paste holes saved from the level editor above this line ----

    for (Hole& h : course) finalizeHole(h);
    return course;
}

// ---------- game flow ----------

GolfGame::GolfGame() {
    holes = buildCourse();
    loadHole(0);
}

void GolfGame::loadHole(int index) {
    holeIndex = std::clamp(index, 0, (int)holes.size() - 1);
    version++;
    resetHole();
}

void GolfGame::replaceHole(int index, const Hole& h) {
    if (index < 0 || index >= (int)holes.size()) return;
    holes[index] = h;
    finalizeHole(holes[index]);
    version++;
    if (index == holeIndex) resetHole();
}

int GolfGame::addHole(const Hole& h) {
    holes.push_back(h);
    finalizeHole(holes.back());
    version++;
    return (int)holes.size() - 1;
}

void GolfGame::placeBall(glm::vec3 position) {
    ballPos = position;
    prevPos = position;
    lastRestPos = position;
    ballState = BallState::Ready;
    outOfBounds = false;
}

void GolfGame::resetHole() {
    ballPos = hole().start + glm::vec3(0, radius, 0);
    prevPos = ballPos;
    lastRestPos = ballPos;
    ballState = BallState::Ready;
    strokeCount = 0;
    outOfBounds = false;
    accumulator = 0.0f;
}

bool GolfGame::nextHole() {
    if (ballState == BallState::Sunk && (int)scores.size() < holeNumber())
        scores.push_back(strokeCount);
    if (holeIndex + 1 >= (int)holes.size()) return false;
    loadHole(holeIndex + 1);
    return true;
}

void GolfGame::restartCourse() {
    scores.clear();
    loadHole(0);
}

int GolfGame::totalPar() const {
    int total = 0;
    for (const Hole& h : holes) total += h.par;
    return total;
}

float GolfGame::speed() const {
    return glm::length(ballPos - prevPos) / dt;
}

void GolfGame::hit(float aimAngle, float power) {
    if (ballState != BallState::Ready) return;
    power = std::clamp(power, 0.0f, 1.0f);
    glm::vec3 dir(std::sin(aimAngle), 0.0f, -std::cos(aimAngle));
    // aim along the ground, so uphill/downhill shots follow the slope
    glm::vec3 n = groundNormal(ballPos.x, ballPos.z);
    dir = glm::normalize(dir - glm::dot(dir, n) * n);
    glm::vec3 vel = dir * (power * maxHitSpeed);

    // Verlet has no velocity variable: moving prevPos backwards gives the ball speed
    lastRestPos = ballPos;
    prevPos = ballPos - vel * dt;
    strokeCount++;
    outOfBounds = false;
    slowTime = 0.0f;
    rollTime = 0.0f;
    ballState = BallState::Rolling;
}

void GolfGame::update(float frameDt) {
    accumulator += std::min(frameDt, 0.1f); // avoid a huge catch-up after a pause
    while (accumulator >= dt) {
        if (ballState == BallState::Rolling) physicsStep();
        accumulator -= dt;
    }
}

// ---------- terrain ----------

float GolfGame::groundHeight(float x, float z) const {
    return holeGroundHeight(hole(), x, z);
}

// The surface direction, from how fast the height changes in x and in z
// (finite differences, so any height function works without extra math).
glm::vec3 GolfGame::groundNormal(float x, float z) const {
    if (!hole().height && hole().stamps.empty()) return glm::vec3(0, 1, 0);
    const float e = 0.05f;
    float dx = (groundHeight(x + e, z) - groundHeight(x - e, z)) / (2 * e);
    float dz = (groundHeight(x, z + e) - groundHeight(x, z - e)) / (2 * e);
    return glm::normalize(glm::vec3(-dx, 1.0f, -dz));
}

// margin > 0 shrinks the greens, so the ball's center stays a radius away from the edge
bool GolfGame::onGreen(float x, float z, float margin) const {
    for (const Rect& g : hole().greens)
        if (x >= g.min.x + margin - 0.01f && x <= g.max.x - margin + 0.01f &&
            z >= g.min.y + margin - 0.01f && z <= g.max.y - margin + 0.01f)
            return true;
    return false;
}

// ---------- collisions ----------

// sphere vs box (walls are treated as columns, so only x/z matter):
// find the closest point on the box, push the ball out if it's too close
void GolfGame::collideBox(const Box& box, glm::vec3& vel) {
    glm::vec2 p(ballPos.x, ballPos.z);
    glm::vec2 closest = glm::clamp(p, glm::vec2(box.min.x, box.min.z), glm::vec2(box.max.x, box.max.z));
    glm::vec2 d = p - closest;
    float dist2 = glm::dot(d, d);
    if (dist2 >= radius * radius) return;

    glm::vec3 n;
    if (dist2 > 1e-8f) {
        float dist = std::sqrt(dist2);
        glm::vec2 n2 = d / dist;
        n = glm::vec3(n2.x, 0, n2.y);
        ballPos.x = closest.x + n2.x * radius;
        ballPos.z = closest.y + n2.y * radius;
    } else {
        // center ended up inside the box: push out the shortest way
        float pushes[4] = { ballPos.x - box.min.x, box.max.x - ballPos.x,
                            ballPos.z - box.min.z, box.max.z - ballPos.z };
        int k = (int)(std::min_element(pushes, pushes + 4) - pushes);
        glm::vec3 normals[4] = { {-1, 0, 0}, {1, 0, 0}, {0, 0, -1}, {0, 0, 1} };
        n = normals[k];
        if (k == 0) ballPos.x = box.min.x - radius;
        if (k == 1) ballPos.x = box.max.x + radius;
        if (k == 2) ballPos.z = box.min.z - radius;
        if (k == 3) ballPos.z = box.max.z + radius;
    }
    // bounce: reverse the part of the velocity going into the wall
    float vn = glm::dot(vel, n);
    if (vn < 0.0f) vel -= (1.0f + wallBounce) * vn * n;
}

// bumpers are vertical posts, so only x/z matter
void GolfGame::collideBumper(const Bumper& b, glm::vec3& vel) {
    glm::vec2 d(ballPos.x - b.center.x, ballPos.z - b.center.z);
    float minDist = b.radius + radius;
    float dist = glm::length(d);
    if (dist >= minDist || dist < 1e-6f) return;

    glm::vec2 n2 = d / dist;
    glm::vec3 n(n2.x, 0.0f, n2.y);
    ballPos.x = b.center.x + n2.x * minDist;
    ballPos.z = b.center.z + n2.y * minDist;
    float vn = glm::dot(vel, n);
    if (vn < 0.0f) vel -= (1.0f + bumperBounce) * vn * n;
    // don't let bumpers fling the ball faster than a full-power hit
    float s = glm::length(vel);
    if (s > maxHitSpeed) vel *= maxHitSpeed / s;
}

// For autoWalls holes: if this step would take the ball off the greens,
// work out whether it crossed an edge in x, in z, or both, and bounce off it.
void GolfGame::keepInsideCourse(glm::vec3 oldPos, glm::vec3& vel) {
    if (onGreen(ballPos.x, ballPos.z, radius)) return;
    bool exitX = !onGreen(ballPos.x, oldPos.z, radius);
    bool exitZ = !onGreen(oldPos.x, ballPos.z, radius);
    if (!exitX && !exitZ) exitX = exitZ = true;   // went out exactly through a corner
    if (exitX) { ballPos.x = oldPos.x; vel.x = -vel.x * wallBounce; }
    if (exitZ) { ballPos.z = oldPos.z; vel.z = -vel.z * wallBounce; }
    if (!onGreen(ballPos.x, ballPos.z, radius)) { ballPos.x = oldPos.x; ballPos.z = oldPos.z; }
}

// ---------- physics step ----------

void GolfGame::physicsStep() {
    glm::vec3 oldPos = ballPos;
    // velocity in units/second, recovered from Verlet positions
    glm::vec3 vel = (ballPos - prevPos) / dt;
    glm::vec3 n = groundNormal(ballPos.x, ballPos.z);
    bool onGround = ballPos.y - radius <= groundHeight(ballPos.x, ballPos.z) + 0.02f;

    // rolling friction: only while touching the ground, only on the part of
    // the velocity that runs along the surface
    if (onGround) {
        glm::vec3 along = vel - glm::dot(vel, n) * n;
        float s = glm::length(along);
        float newS = s - (rollDrag * s + rollFriction) * dt;
        glm::vec3 newAlong = (newS <= 0.0f || s < 1e-6f) ? glm::vec3(0.0f) : along * (newS / s);
        vel += newAlong - along;
    }
    prevPos = ballPos - vel * dt;

    // Verlet step (same idea as Engine::performVerlet). Gravity pulls straight
    // down; the ground below removes the part going into it, so what's left
    // makes the ball roll down slopes and slow down going up them.
    glm::vec3 temp = ballPos;
    ballPos = ballPos + (ballPos - prevPos) + glm::vec3(0, gravity, 0) * (dt * dt);
    prevPos = temp;
    vel = (ballPos - prevPos) / dt;

    // ground contact
    float ground = groundHeight(ballPos.x, ballPos.z);
    n = groundNormal(ballPos.x, ballPos.z);
    if (ballPos.y < ground + radius) {
        ballPos.y = ground + radius;
        float vn = glm::dot(vel, n);
        if (vn < 0.0f) {
            float bounce = (vn < -3.0f) ? 0.3f : 0.0f;   // landings bounce a little
            vel -= (1.0f + bounce) * vn * n;
        }
    }
    // (if the ground drops away faster than the ball falls, e.g. over the top of
    //  a hill or off a ramp, there's no contact and the ball flies)

    for (const Box& w : hole().walls) collideBox(w, vel);
    for (const Bumper& b : hole().bumpers) collideBumper(b, vel);
    if (hole().autoWalls) keepInsideCourse(oldPos, vel);
    prevPos = ballPos - vel * dt;

    float spd = glm::length(vel);
    ground = groundHeight(ballPos.x, ballPos.z);
    bool touching = ballPos.y - radius <= ground + 0.05f;

    // in the cup? only if on the ground and slow enough, otherwise it rolls over
    glm::vec2 toCup(ballPos.x - hole().cup.x, ballPos.z - hole().cup.z);
    if (touching && glm::length(toCup) < hole().cupRadius && spd < maxSinkSpeed) {
        ballState = BallState::Sunk;
        ballPos = hole().cup + glm::vec3(0, 0.05f, 0); // drop into the hole
        prevPos = ballPos;
        return;
    }

    // escaped the course somehow: put it back with a 1 stroke penalty
    bool lost = hole().autoWalls ? ballPos.y < ground - 20.0f
                                 : (!onGreen(ballPos.x, ballPos.z) || ballPos.y < -5.0f);
    if (lost) {
        ballPos = lastRestPos;
        prevPos = ballPos;
        strokeCount++;
        outOfBounds = true;
        ballState = BallState::Ready;
        return;
    }

    // stopped rolling: nearly still on the ground for a moment -> ready to hit
    rollTime += dt;
    slowTime = (touching && spd < 0.3f) ? slowTime + dt : 0.0f;
    if (slowTime > 0.4f || rollTime > 45.0f) {
        prevPos = ballPos;
        ballState = BallState::Ready;
    }
}
