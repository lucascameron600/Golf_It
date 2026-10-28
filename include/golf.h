#ifndef GOLF_H
#define GOLF_H
#include <vector>
#include <string>
#include <functional>
#include <glm/glm.hpp>

// Game logic only (no OpenGL here), so it can be tested without a window.
// Coordinates: y is up, the course lies in x/z.

struct Box    { glm::vec3 min, max; };          // walls and block obstacles (only x/z are used for collision)
struct Bumper { glm::vec3 center; float radius; }; // round bouncy posts
struct Rect   { glm::vec2 min, max; };          // a piece of playable green (x/z)

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
    glm::vec3 cup{0.0f};      // center of the hole (y is filled in automatically)
    float cupRadius = 0.85f;

    // Where you can play. Use rectangles, a path, or both:
    std::vector<Rect> rects;          // rectangles of green
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
    bool wasReset() const { return outOfBounds; } // ball left the course last stroke

    // tuning (public so they can be tweaked from ImGui)
    float maxHitSpeed   = 30.0f;  // units per second at full power
    float rollDrag      = 0.4f;   // slows fast balls (proportional to speed)
    float rollFriction  = 2.5f;   // constant slowdown so balls actually stop
    float wallBounce    = 0.7f;   // 1 = perfectly bouncy, 0 = dead stop
    float bumperBounce  = 1.15f;  // bumpers add a little energy
    float maxSinkSpeed  = 9.0f;   // faster than this and the ball rolls over the cup

private:
    void physicsStep();
    void collideBox(const Box& box, glm::vec3& vel);
    void collideBumper(const Bumper& b, glm::vec3& vel);
    void keepInsideCourse(glm::vec3 oldPos, glm::vec3& vel);

    std::vector<Hole> holes;
    int holeIndex = 0;
    int version = 0;

    // Verlet integration like the sandbox: velocity is (pos - prevPos)
    glm::vec3 ballPos, prevPos;
    glm::vec3 lastRestPos;        // where the last stroke was taken from
    float radius = 0.5f;
    BallState ballState = BallState::Ready;
    int strokeCount = 0;
    std::vector<int> scores;
    bool outOfBounds = false;
    float slowTime = 0.0f;        // how long the ball has been almost still
    float rollTime = 0.0f;        // how long this shot has been rolling

    const float dt = 1.0f / 120.0f;  // small fixed step so fast balls can't skip through walls
    float accumulator = 0.0f;
    const float gravity = -9.8f;
};

#endif
