#ifndef GOLF_TOOLS_H
#define GOLF_TOOLS_H
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include "golf.h"

// Tools for designing holes (used by the level editor, not by the game).

// Turns a hole into a C++ block you can paste into buildCourse() in golf.cpp.
std::string holeToCpp(const Hole& h);

// A simple bot that plays a hole: for every stroke it tries lots of
// aim/power combinations and keeps the one that ends closest to the cup,
// measuring distance along the course (so it doesn't aim through walls).
struct BotResult {
    bool sunk = false;
    int strokes = 0;
    std::vector<glm::vec3> restPoints;  // where the ball stopped after each stroke
};
BotResult runBot(const GolfGame& game, int holeIndex, int maxStrokes = 15);

#endif
