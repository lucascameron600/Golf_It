#ifndef GOLF_H
#define GOLF_H
#include <vector>
#include <string>
#include <functional>
#include <glm/glm.hpp>

// Game logic only (no OpenGL here), so it can be tested without a window.
// Coordinates: y is up, the course lies in x/z. Units are meters.

struct Box    { glm::vec3 min, max; };          // walls and block obstacles (only x/z are used for collision)
struct Bumper { glm::vec3 center; float radius; }; // round bouncy posts
struct Rect   { glm::vec2 min, max; };          // a piece of fairway (x/z)
struct Bunker { glm::vec2 center; float radius; }; // a round patch of sand (x/z)

// What the ball is sitting on (or bouncing off). Each one has its own
// SurfaceFeel below.
enum class Surface { Green, Fairway, Rough, Sand, OutOfBounds };
const char* surfaceName(Surface s);   // "Green", "Fairway", ...

// How one kind of turf treats a landing ball and a rolling ball.
// A bounce works like a real one: the turf gives way a little, so the ball hits a
// small ramp tilted back toward it (a pitch mark), pushes back up with `bounce`,
// and its friction grabs the ball's forward speed and backspin. Then it rolls.
struct SurfaceFeel {
    float bounce;     // share of the speed into the ground that comes back up on a soft landing
                      // (hard landings get about 1/3 of this, like real turf)
    float grip;       // friction between ball and grass: how much forward speed and backspin a bounce grabs
    float softness;   // how deep a pitch mark is, as degrees of tilt for a 20 m/s landing at 45 degrees.
                      // More = steep shots dig in and stop, flat ones still skip forward
    float cushion;    // share of the forward speed (and spin) the grass or sand swallows on each bounce
    float rollDecel;  // m/s^2 a rolling ball slows by (set a green with stimpToDecel below)
    float rollDrag;   // extra slowdown that grows with speed squared: it hardly touches a putt, but
                      // a ball running out at 10 m/s is skipping over the blades and slows a lot more
};

// Stimpmeter: a ball let go at 1.83 m/s rolls this many feet. A constant slowdown
// a gives a roll of v^2 / 2a, so the two convert directly.
inline float stimpToDecel(float stimpFeet) { return 1.83f * 1.83f / (2.0f * stimpFeet * 0.3048f); }
inline float decelToStimp(float decel)     { return 1.83f * 1.83f / (2.0f * decel * 0.3048f); }

// One piece of terrain shape. A hole's ground height is all its stamps added
// together, so hills, ramps and tilts can be mixed freely (and edited in the
// level editor). Make them with the helpers, e.g. Stamp::bump(0, -50, 6, -1.5f).
struct Stamp {
    enum Type { Bump, Ramp, Rollers, Tilt, Pipe };
    Type type = Bump;

    glm::vec2 center{0.0f, 0.0f};  // Bump: middle of the hill/bowl (x, z)
    float radius = 5.0f;           // Bump: how wide
    float height = 1.0f;           // Bump: height (negative = bowl), Ramp: rise, Rollers: wave height
    char axis = 'z';               // Ramp/Rollers/Pipe: direction they run, 'x' or 'z'
    float from = 0.0f, to = 10.0f; // Ramp: where it starts and ends along the axis
    bool launch = false;           // Ramp: gets steeper toward the end (a jump lip) instead of an S-curve
    float wavelength = 12.0f;      // Rollers: distance from one crest to the next
    glm::vec2 slope{0.0f, 0.0f};   // Tilt: height change per unit of x and per unit of z
    float line = 0.0f;             // Pipe: position of the low line (x for a pipe along z)
    float curve = 0.07f;           // Pipe: how quickly the sides rise

    // optional: only apply inside a rectangle, fading out over `edge` units
    bool limited = false;
    Rect area{{0.0f, 0.0f}, {0.0f, 0.0f}};
    float edge = 5.0f;

    static Stamp bump(float x, float z, float radius, float height);
    static Stamp ramp(char axis, float from, float to, float rise, bool launch = false);
    static Stamp rollers(char axis, float waveHeight, float wavelength);
    static Stamp tilt(float slopeX, float slopeZ);
    static Stamp pipe(char axis, float line, float curve);
    Stamp limit(float minX, float minZ, float maxX, float maxZ, float edge) const;

    float evaluate(float x, float z) const;
};

struct Hole {
    std::string name;
    int par = 3;
    glm::vec3 start{0.0f};    // where the ball is placed (y is filled in automatically)
    float startAim = 0.0f;    // initial aim angle (radians, 0 = toward -z)
    float tee = 0.0f;         // the first shot sits this high on a tee (0 = on the ground)
    float greenRadius = 0.0f; // size of the putting green around the cup; the rest is fairway
                              // (0 = no putting green: the whole course is drawn as a checkerboard)
    float roughWidth = 0.0f;  // a band of rough this wide around the fairway (the rects) is
                              // still in play; past it is out of bounds (0 = no rough)
    std::vector<Bunker> bunkers; // sand (see addBunker in golf.cpp, which also digs a dip)
    glm::vec3 cup{0.0f};      // center of the hole (y is filled in automatically)
    float cupRadius = 0.85f;

    // Where you can play (the fairway). Use rectangles, a path, or both:
    std::vector<Rect> rects;          // rectangles of fairway
    std::vector<glm::vec2> path;      // a winding course through these points (x, z)...
    float pathWidth = 14.0f;          // ...this wide
    bool autoWalls = false;           // edges of the green act as walls (always on with a path)

    std::vector<Box> walls;
    std::vector<Bumper> bumpers;
    std::vector<Stamp> stamps;        // terrain shape (empty = flat)

    // Optional extra terrain written as code. The level editor can't edit or
    // export this, so prefer stamps.
    std::function<float(float, float)> height;

    // Filled in by finalizeHole(): every rectangle of green, including the path's.
    std::vector<Rect> greens;
};

// Works out the greens from rects + path and puts the start, cup and bumpers on the ground.
void finalizeHole(Hole& h);
float holeGroundHeight(const Hole& h, float x, float z);

enum class BallState { Ready, Rolling, Sunk };

class GolfGame {
public:
    GolfGame();

    // course flow
    void loadHole(int index);
    void resetHole();               // replay the current hole from the start
    bool nextHole();                // false if this was the last hole
    void restartCourse();

    // for the level editor: swap in an edited hole (it gets finalized), or add one
    void replaceHole(int index, const Hole& h);
    int addHole(const Hole& h);     // returns its index
    const std::vector<Hole>& allHoles() const { return holes; }
    int holeVersion() const { return version; }  // changes whenever a hole is edited

    // place the ball anywhere (used by the bot and the editor)
    void placeBall(glm::vec3 position);

    // aimAngle: radians (0 = toward -z), power: 0..1
    void hit(float aimAngle, float power);
    // advance the simulation by real frame time (runs fixed physics steps inside)
    void update(float frameDt);

    // read-only state for drawing / UI
    const Hole& hole() const { return holes[holeIndex]; }
    int holeNumber() const { return holeIndex + 1; }
    int holeCount() const { return (int)holes.size(); }
    glm::vec3 ballPosition() const { return ballPos; }
    float ballRadius() const { return radius; }
    BallState state() const { return ballState; }
    int strokes() const { return strokeCount; }
    const std::vector<int>& scorecard() const { return scores; } // strokes per finished hole
    int totalPar() const;
    float speed() const;            // current ball speed (units per second)

    // terrain queries (also used by the renderer and the camera)
    float groundHeight(float x, float z) const;
    glm::vec3 groundNormal(float x, float z) const;
    bool onGreen(float x, float z, float margin = 0.0f) const;
    Surface surfaceAt(float x, float z) const;    // green, fairway, rough, sand or out of bounds
    bool wasReset() const { return outOfBounds; } // ball left the course last stroke

    // tuning (public so they can be tweaked from ImGui)
    float maxHitSpeed   = 6.0f;   // m/s at full power. The club sets it; this default is the putter's,
                                  // which rolls about 36 m on a stimp 11 green
    float wallBounce    = 0.7f;   // 1 = perfectly bouncy, 0 = dead stop
    float bumperBounce  = 1.15f;  // bumpers add a little energy
    // A ball drops if it falls about its own radius while crossing the cup:
    // 2 * cupRadius * sqrt(g / (2 * ballRadius)). That's ~1.6 m/s for a real ball
    // and cup, and ~4.5 m/s for this 10x ball and 0.45 m cup
    float maxSinkSpeed  = 4.5f;   // faster than this and the ball rolls over the cup
    float launchAngle   = 0.0f;   // radians above level: 0 = putt along the ground, more = the ball flies

    // The flight has no air, so a ball comes down far faster and flatter than a
    // real one (a driver lands at ~60 m/s instead of ~30). On the first touchdown
    // it's given what a real ball has as it comes down with this club, scaled by
    // how hard it was hit. The club sets these (see clubLanding in clubs.h).
    float landingAngle  = 0.79f;  // radians below level (tour 7-iron: about 50 deg)
    float landingSpeed  = 24.0f;  // m/s
    float landingSpin   = 5500.0f;// backspin, rpm
    // Green, Fairway, Rough, Sand (index with (int)Surface). Out of bounds uses Rough.
    // Tuned so tour clubs run out about like they do on TV (Tour Average, full swing):
    //   driver   fairway ~30 yd (first hop ~1.8 m high), rough ~7, sand stays put
    //   7-iron   fairway ~5 yd, green ~2, rough ~1.5
    //   wedge    green: one hop and stops, a full 100-yd wedge spins back ~3 yd
    SurfaceFeel feel[4] = {
        //  bounce  grip   softness cushion  roll slowdown        grass drag
        {   0.40f,  0.40f, 16.0f,   0.05f,   stimpToDecel(11.0f), 0.00f },  // green: soft, holds spin, stimp 11
        {   0.38f,  0.40f, 12.0f,   0.12f,   0.90f,               0.05f },  // fairway: firm, hops forward and runs
        {   0.20f,  0.30f,  8.0f,   0.50f,   2.50f,               0.25f },  // rough: swallows the ball, no spin
        {   0.03f,  0.60f,  4.0f,   0.90f,   6.00f,               1.00f },  // sand: plugs where it lands
    };
    const SurfaceFeel& feelAt(float x, float z) const;

private:
    void physicsStep();
    void rollOnTurf(glm::vec3& vel, glm::vec3 n);    // slip and rolling resistance
    void bounceOnTurf(glm::vec3& vel, glm::vec3 n);  // one impact with the ground
    void landLikeARealBall(glm::vec3& vel);          // swap the no-air arrival for a real one
    void collideBox(const Box& box, glm::vec3& vel);
    void collideBumper(const Bumper& b, glm::vec3& vel);
    void keepInsideCourse(glm::vec3 oldPos, glm::vec3& vel);

    std::vector<Hole> holes;
    int holeIndex = 0;
    int version = 0;

    // Verlet integration like the sandbox, with the velocity stored on its own
    // (see physicsStep) so float rounding far from the origin can't bend the ball
    glm::vec3 ballPos, ballVel{0.0f};
    glm::vec3 lastRestPos;        // where the last stroke was taken from
    float radius = 0.2f;          // a 0.4 m ball: ~10x real size so you can still see it
    BallState ballState = BallState::Ready;
    int strokeCount = 0;
    std::vector<int> scores;
    bool outOfBounds = false;
    bool flying = false;          // hit into the air and hasn't landed yet
    glm::vec3 spin{0.0f};         // the ball's spin (rad/s, axis = direction, for a real-size ball)
    float slowTime = 0.0f;        // how long the ball has been almost still
    float rollTime = 0.0f;        // how long this shot has been rolling

    const float dt = 1.0f / 120.0f;  // small fixed step so fast balls can't skip through walls
    float accumulator = 0.0f;
    const float gravity = -9.8f;
};

#endif
