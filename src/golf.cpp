#include "golf.h"
#include <cmath>
#include <algorithm>

// The spin math uses a real ball (radius 21.35 mm), not the 10x one
// that's drawn: spin is stored in rad/s, so this turns it into surface speed.
static const float REAL_BALL_RADIUS = 0.02135f;

const char* surfaceName(Surface s) {
    switch (s) {
        case Surface::Green:   return "Green";
        case Surface::Fairway: return "Fairway";
        case Surface::Rough:   return "Rough";
        case Surface::Sand:    return "Sand";
        default:               return "Out of bounds";
    }
}

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

// a wall/block covering x1..x2, z1..z2 on the ground (the level editor writes
// walls with this, so it's here for holes pasted from it)
[[maybe_unused]] static Box block(float x1, float z1, float x2, float z2) {
    return Box{ glm::vec3(std::min(x1, x2), 0.0f, std::min(z1, z2)),
                glm::vec3(std::max(x1, x2), WALL_HEIGHT, std::max(z1, z2)) };
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

// a round bunker at x, z, sunk into a shallow dip so it reads as a hollow and
// a ball near the lip trickles in (keep it clear of the putting green)
static void addBunker(Hole& h, float x, float z, float radius) {
    h.bunkers.push_back(Bunker{ glm::vec2(x, z), radius });
    h.stamps.push_back(Stamp::bump(x, z, radius * 0.5f, -0.35f));
}

// The course: full-size golf holes. Units are meters
// (gravity is 9.8), so a 150 m par 3 is about 165 yards. No walls: the fairway
// (the rects) has a band of rough around it, and past that is out of bounds
// (back to the last spot, +1 stroke). The ball starts each hole on a tee. The
// ball is 0.4 m wide (about 10x real size so you can still see it), and the tee
// and cup are scaled to match. Green, fairway, rough and sand each bounce and
// roll differently (GolfGame::feel).
static std::vector<Hole> buildRealCourse() {
    std::vector<Hole> course;

    // Hole 1: straight par 3, 150 m (164 yd), bunkers either side of the green
    {
        Hole h;
        h.name = "Par 3 - 164 yd";
        h.par = 3;
        h.tee = 0.15f;
        h.cupRadius = 0.45f;
        h.greenRadius = 15.0f;
        h.roughWidth = 12.0f;
        h.start = glm::vec3(0, 0, 0);
        h.startAim = 0.0f;
        h.cup = glm::vec3(0, 0, -150);
        h.rects.push_back(Rect{ {-20, -175}, {20, 8} });
        addBunker(h, -21, -146, 5.0f);   // left of the green
        addBunker(h, 17, -134, 4.0f);    // short right
        h.stamps.push_back(Stamp::bump(25, -150, 20, 1.0f)); 
        course.push_back(h);
    }

    // Hole 2: par 4 dogleg right, 250 m up then 110 m across (about 394 yd)
    {
        Hole h;
        h.name = "Dogleg Right - 394 yd";
        h.par = 4;
        h.tee = 0.15f;
        h.cupRadius = 0.45f;
        h.greenRadius = 15.0f;
        h.roughWidth = 12.0f;
        h.start = glm::vec3(0, 0, 0);
        h.startAim = 0.0f;
        h.cup = glm::vec3(110, 0, -250);
        h.rects.push_back(Rect{ {-25, -280}, {25, 8} });     // first leg, 50 m wide
        h.rects.push_back(Rect{ {25, -280}, {140, -220} });  // turn to the right
        addBunker(h, 0, -270, 6.0f);     // through the corner: catches a drive that runs too far
        addBunker(h, 33, -212, 5.0f);    // inside the corner: punishes cutting it
        addBunker(h, 128, -232, 4.5f);   // behind the green on the right
        course.push_back(h);
    }

    // Hole 3: straight par 5, 460 m (503 yd), with gentle rolls in the middle
    {
        Hole h;
        h.name = "Par 5 - 503 yd";
        h.par = 5;
        h.tee = 0.15f;
        h.cupRadius = 0.45f;
        h.greenRadius = 15.0f;
        h.roughWidth = 15.0f;
        h.start = glm::vec3(0, 0, 0);
        h.startAim = 0.0f;
        h.cup = glm::vec3(0, 0, -460);
        h.rects.push_back(Rect{ {-30, -490}, {30, 8} });
        h.stamps.push_back(Stamp::rollers('z', 1.0f, 60.0f).limit(-40, -380, 40, -60, 20));
        addBunker(h, 22, -270, 6.0f);    // fairway bunkers in the driving zone
        addBunker(h, -24, -300, 6.0f);
        addBunker(h, -18, -448, 5.0f);   // short left of the green
        course.push_back(h);
    }

    // ---- paste holes saved from the level editor above this line ----

    for (Hole& h : course) finalizeHole(h);
    return course;
}

// ---------- game flow ----------

GolfGame::GolfGame() {
    holes = buildRealCourse();
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
    ballVel = glm::vec3(0.0f);
    lastRestPos = position;
    ballState = BallState::Ready;
    outOfBounds = false;
    spin = glm::vec3(0.0f);
}

void GolfGame::resetHole() {
    ballPos = hole().start + glm::vec3(0, radius + hole().tee, 0);
    ballVel = glm::vec3(0.0f);
    lastRestPos = ballPos;
    ballState = BallState::Ready;
    strokeCount = 0;
    outOfBounds = false;
    accumulator = 0.0f;
    spin = glm::vec3(0.0f);
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
    return glm::length(ballVel);
}

void GolfGame::hit(float aimAngle, float power) {
    if (ballState != BallState::Ready) return;
    power = std::clamp(power, 0.0f, 1.0f);
    glm::vec3 dir(std::sin(aimAngle), 0.0f, -std::cos(aimAngle));
    glm::vec3 vel;
    if (launchAngle > 0.0f) {
        // full shot: tilt the level aim up by the launch angle. Carry grows with
        // speed squared, so sqrt(power) makes half power carry half as far
        glm::vec3 up(0.0f, 1.0f, 0.0f);
        vel = (dir * std::cos(launchAngle) + up * std::sin(launchAngle)) * (std::sqrt(power) * maxHitSpeed);
    } else {
        // putt: aim along the ground, so uphill/downhill shots follow the slope
        glm::vec3 n = groundNormal(ballPos.x, ballPos.z);
        dir = glm::normalize(dir - glm::dot(dir, n) * n);
        // a putt rolls v^2 / 2a, so sqrt(power) makes half power roll half as
        // far (like the full shots above)
        vel = dir * (std::sqrt(power) * maxHitSpeed);
    }
    flying = launchAngle > 0.0f;
    // full shots get their backspin when they land (landLikeARealBall); a putt
    // starts out rolling, so the spin matches the speed and nothing skids
    spin = glm::vec3(0.0f);
    if (!flying) {
        glm::vec3 n = groundNormal(ballPos.x, ballPos.z);
        spin = glm::cross(n, vel) / REAL_BALL_RADIUS;
    }

    lastRestPos = ballPos;
    ballVel = vel;
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

// Putting green first, then sand, then fairway (the rects), then the band of
// rough around them. Anything else is out of bounds.
Surface GolfGame::surfaceAt(float x, float z) const {
    const Hole& h = hole();
    glm::vec2 p(x, z);
    if (h.greenRadius > 0.0f && glm::length(p - glm::vec2(h.cup.x, h.cup.z)) < h.greenRadius)
        return Surface::Green;
    for (const Bunker& b : h.bunkers)
        if (glm::length(p - b.center) < b.radius) return Surface::Sand;
    if (onGreen(x, z)) return Surface::Fairway;
    if (h.roughWidth > 0.0f) {
        // within roughWidth of any fairway rectangle (so the corners are rounded)
        for (const Rect& r : h.greens) {
            float dx = std::max({ r.min.x - x, 0.0f, x - r.max.x });
            float dz = std::max({ r.min.y - z, 0.0f, z - r.max.y });
            if (dx * dx + dz * dz <= h.roughWidth * h.roughWidth) return Surface::Rough;
        }
    }
    return Surface::OutOfBounds;
}

const SurfaceFeel& GolfGame::feelAt(float x, float z) const {
    Surface s = surfaceAt(x, z);
    return feel[s == Surface::OutOfBounds ? (int)Surface::Rough : (int)s];
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

// ---------- ball and turf ----------
//
// The ball is a solid sphere with spin. Where it touches the grass, the bottom
// of the ball moves at vel + spin x (-R n). If that isn't zero the ball is
// slipping, and friction pushes against the slip: it slows the ball and changes
// its spin until the bottom stops sliding, which is a pure roll. For a solid
// ball that takes an impulse of 2/7 of the slip (the other 5/7 is the spin
// catching up). Backspin makes the bottom slide forward faster, so friction
// takes more speed off: that's why a wedge checks up and a driver runs out.

// push against the slip with at most `maxImpulse` (m/s); contact is toward -n
static void applyTurfFriction(glm::vec3& vel, glm::vec3& spin, glm::vec3 n, float maxImpulse) {
    glm::vec3 r = -REAL_BALL_RADIUS * n;                 // ball center -> contact point
    glm::vec3 u = vel + glm::cross(spin, r);             // how the bottom of the ball is moving
    glm::vec3 slip = u - glm::dot(u, n) * n;             // the part sliding along the grass
    float s = glm::length(slip);
    if (s < 1e-5f || maxImpulse <= 0.0f) return;
    glm::vec3 impulse = -slip / s * std::min(2.0f / 7.0f * s, maxImpulse);
    vel += impulse;
    // torque from the push at the contact point; a solid ball's I = 2/5 m R^2
    spin += glm::cross(r, impulse) * (5.0f / (2.0f * REAL_BALL_RADIUS * REAL_BALL_RADIUS));
}

// The flight has no air, so the ball arrives far too fast and flat. Keep its
// heading and swap in a real ball's landing for this club: angle, speed and
// backspin (scaled by how hard it was hit, 1 = a full swing).
void GolfGame::landLikeARealBall(glm::vec3& vel) {
    glm::vec3 flat(vel.x, 0.0f, vel.z);
    float f = glm::length(flat);
    if (f < 1e-4f || maxHitSpeed <= 0.0f) return;
    glm::vec3 dir = flat / f;
    float share = std::clamp(glm::length(vel) / maxHitSpeed, 0.0f, 1.2f);
    float v = landingSpeed * share;
    vel = dir * (v * std::cos(landingAngle)) + glm::vec3(0.0f, -v * std::sin(landingAngle), 0.0f);
    // backspin: the top of the ball turns back toward the golfer
    float w = landingSpin * share * 6.2831853f / 60.0f;  // rpm -> rad/s
    spin = glm::cross(dir, glm::vec3(0.0f, 1.0f, 0.0f)) * w;
}

// One impact with the ground. The turf gives way, so a hard, steep landing
// effectively hits a little ramp tilted back toward the ball (a pitch mark).
// Penner's model of golf ball bounce, with the tilt growing with speed and
// steepness, and the softness of the turf setting how much.
void GolfGame::bounceOnTurf(glm::vec3& vel, glm::vec3 n) {
    float vn = glm::dot(vel, n);
    if (vn > -1.0f) {             // just settling onto the grass while rolling: no bounce
        vel -= vn * n;
        return;
    }
    const SurfaceFeel& f = feelAt(ballPos.x, ballPos.z);
    glm::vec3 along = vel - vn * n;
    float s = glm::length(along);
    glm::vec3 fwd = s > 1e-4f ? along / s : glm::vec3(0.0f);

    // the pitch mark: tilt the ground back toward the ball
    float speed = glm::length(vel);
    float steep = std::atan2(-vn, s);                     // radians below the ground
    float tilt = glm::radians(f.softness) * (speed / 20.0f) * (steep / 0.785398f);
    tilt = std::min(tilt, 0.6f);                          // ~35 degrees: past that it would bounce backward
    glm::vec3 m = glm::normalize(n * std::cos(tilt) - fwd * std::sin(tilt));

    float vm = glm::dot(vel, m);
    if (vm >= 0.0f) { vel -= vn * n; return; }
    // bounce: soft landings come back up with `bounce`, hard ones lose more
    // (turf dents and stays dented), down to about a third of it at 20 m/s
    float e = f.bounce * (1.0f - 0.65f * std::min(-vm / 20.0f, 1.0f));
    float push = -(1.0f + e) * vm;
    vel += push * m;
    // friction during the impact: at most grip * the push
    applyTurfFriction(vel, spin, m, f.grip * push);

    // long grass and sand don't just rub the ball, they wrap around it and
    // swallow part of its forward speed and spin
    float out = glm::dot(vel, n);
    vel -= (vel - out * n) * f.cushion;
    spin *= 1.0f - f.cushion;
    // a deep pitch mark can leave the ball heading back into the real ground
    if (out < 0.0f) vel -= out * n;
}

// While the ball is on the grass (and not leaving it): first the leftover slip
// (backspin after a landing, or picking up speed down a slope) is worn down by
// friction, at most grip * g each second. Then rolling resistance slows the roll.
void GolfGame::rollOnTurf(glm::vec3& vel, glm::vec3 n) {
    if (glm::dot(vel, n) > 0.5f) return;  // bouncing up off the ground
    const SurfaceFeel& f = feelAt(ballPos.x, ballPos.z);
    applyTurfFriction(vel, spin, n, f.grip * -gravity * n.y * dt);

    glm::vec3 along = vel - glm::dot(vel, n) * n;
    float s = glm::length(along);
    float newS = s - (f.rollDecel + f.rollDrag * s * s) * dt;
    if (newS <= 0.0f || s < 1e-6f) {
        vel -= along;                 // stopped: the grass holds it, spin and all
        spin = glm::vec3(0.0f);
        return;
    }
    glm::vec3 change = along * (newS / s) - along;
    vel += change;
    // slow the spin with it, so the ball keeps rolling instead of starting to slide
    spin += glm::cross(n, change) / REAL_BALL_RADIUS;
}

// ---------- physics step ----------

void GolfGame::physicsStep() {
    glm::vec3 oldPos = ballPos;
    glm::vec3 vel = ballVel;   // m/s
    glm::vec3 n = groundNormal(ballPos.x, ballPos.z);
    bool onGround = ballPos.y - radius <= groundHeight(ballPos.x, ballPos.z) + 0.02f;

    // rolling: only while touching the ground (spin, slip and the turf's slowdown)
    if (onGround) rollOnTurf(vel, n);

    // Verlet step (same idea as Engine::performVerlet). Gravity pulls straight
    // down; the ground below removes the part going into it, so what's left
    // makes the ball roll down slopes and slow down going up them.
    // This is the same math as pos + (pos - prevPos) + g dt^2, but with the
    // velocity kept on its own: working it out as (pos - prevPos) / dt subtracts
    // two nearly equal numbers, and hundreds of meters from the origin float
    // rounding then bends slow putts sideways by up to a couple of meters.
    vel += glm::vec3(0, gravity, 0) * dt;
    ballPos += vel * dt;

    // ground contact
    float ground = groundHeight(ballPos.x, ballPos.z);
    n = groundNormal(ballPos.x, ballPos.z);
    if (ballPos.y < ground + radius) {
        // every touchdown is a proper bounce off whatever turf is here
        ballPos.y = ground + radius;
        if (flying) {
            landLikeARealBall(vel);
            flying = false;
        }
        bounceOnTurf(vel, n);
    }
    // (if the ground drops away faster than the ball falls, e.g. over the top of
    //  a hill or off a ramp, there's no contact and the ball flies)

    for (const Box& w : hole().walls) collideBox(w, vel);
    for (const Bumper& b : hole().bumpers) collideBumper(b, vel);
    if (hole().autoWalls) keepInsideCourse(oldPos, vel);
    ballVel = vel;

    float spd = glm::length(vel);
    ground = groundHeight(ballPos.x, ballPos.z);
    bool touching = ballPos.y - radius <= ground + 0.05f;

    // in the cup? only if on the ground and slow enough, otherwise it rolls over
    glm::vec2 toCup(ballPos.x - hole().cup.x, ballPos.z - hole().cup.z);
    if (touching && glm::length(toCup) < hole().cupRadius && spd < maxSinkSpeed) {
        ballState = BallState::Sunk;
        ballPos = hole().cup + glm::vec3(0, 0.05f, 0); // drop into the hole
        ballVel = glm::vec3(0.0f);
        return;
    }

    // escaped the course somehow: put it back with a 1 stroke penalty.
    // Only once it's on the ground, so a shot can fly over the out of bounds
    // (the rough is still in play)
    bool lost = hole().autoWalls ? ballPos.y < ground - 20.0f
                                 : ((touching && surfaceAt(ballPos.x, ballPos.z) == Surface::OutOfBounds) ||
                                    ballPos.y < -5.0f);
    if (lost) {
        ballPos = lastRestPos;
        ballVel = glm::vec3(0.0f);
        spin = glm::vec3(0.0f);
        strokeCount++;
        outOfBounds = true;
        ballState = BallState::Ready;
        return;
    }

    // stopped rolling: nearly still on the ground for a moment -> ready to hit
    rollTime += dt;
    slowTime = (touching && spd < 0.3f) ? slowTime + dt : 0.0f;
    if (slowTime > 0.4f || rollTime > 45.0f) {
        ballVel = glm::vec3(0.0f);
        spin = glm::vec3(0.0f);
        ballState = BallState::Ready;
    }
}
