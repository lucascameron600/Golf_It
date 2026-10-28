#ifndef GOLF_RENDER_H
#define GOLF_RENDER_H
#include <vector>
#include <glad/gl.h>
#include <glm/glm.hpp>
#include "golf.h"

// A list of triangles stored on the GPU.
struct Mesh {
    GLuint vao = 0, vbo = 0;
    int vertexCount = 0;
    void upload(const std::vector<GLfloat>& positions);  // xyz per vertex
    void draw() const;
    void release();
};

// Draws the golf course, ball and aim line. The ground of each hole is built
// into meshes once when the hole loads (it can have hills, so it's a grid of
// triangles following the hole's height function). The ball, bumpers and cup
// reuse the sphere from sphere2.obj, moved and scaled with the model matrix.
class GolfRenderer {
public:
    bool init();   // needs an OpenGL context and sphere2.obj in the working folder
    void draw(const GolfGame& game, const glm::mat4& view, const glm::mat4& projection,
              float aimAngle, float power, bool showAim);
    void cleanup();

    // extra shapes (used by the level editor for handles); call after draw()
    void drawSphere(glm::vec3 center, glm::vec3 scale, glm::vec4 color);
    void drawBox(glm::vec3 min, glm::vec3 max, glm::vec4 color);

private:
    void buildHole(const GolfGame& game);
    void drawMesh(const Mesh& mesh, glm::vec4 color);

    GLuint shader = 0;
    GLint modelLoc = -1, colorLoc = -1;
    Mesh sphere, cube;
    Mesh greenLight, greenDark, rough, walls;  // rebuilt for each hole
    int builtVersion = -1;  // which version of the hole the meshes were built for
};

#endif
