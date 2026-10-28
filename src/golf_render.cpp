#include "golf_render.h"
#include "parseobj.h"
#include <iostream>
#include <cmath>
#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

// Same MVP vertex shader as the sandbox, but it also passes the world position on.
static const char* golfVertexSrc = R"glsl(
#version 330 core
layout (location = 0) in vec3 aPos;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
out vec3 worldPos;
void main()
{
    vec4 world = model * vec4(aPos, 1.0);
    worldPos = world.xyz;
    gl_Position = projection * view * world;
}
)glsl";

// Simple lighting without needing normals in the mesh: dFdx/dFdy give how the
// world position changes across neighbouring pixels, and their cross product
// is the surface direction (flat shading, one shade per triangle). This is
// also what makes slopes visible: tilted ground gets a different shade.
static const char* golfFragmentSrc = R"glsl(
#version 330 core
in vec3 worldPos;
out vec4 FragColor;
uniform vec4 inputColor;
void main()
{
    vec3 normal = normalize(cross(dFdx(worldPos), dFdy(worldPos)));
    vec3 lightDir = normalize(vec3(0.4, 1.0, 0.3));
    float light = 0.45 + 0.55 * max(dot(normal, lightDir), 0.0);
    FragColor = vec4(inputColor.rgb * light, inputColor.a);
}
)glsl";

// ---------- Mesh ----------

void Mesh::upload(const std::vector<GLfloat>& positions) {
    release();
    vertexCount = (int)positions.size() / 3;
    if (vertexCount == 0) return;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, positions.size() * sizeof(GLfloat), positions.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}

void Mesh::draw() const {
    if (vertexCount == 0) return;
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, vertexCount);
}

void Mesh::release() {
    if (vao) glDeleteVertexArrays(1, &vao);
    if (vbo) glDeleteBuffers(1, &vbo);
    vao = vbo = 0;
    vertexCount = 0;
}

// ---------- geometry helpers ----------

static void pushVertex(std::vector<GLfloat>& v, glm::vec3 p) {
    v.push_back(p.x); v.push_back(p.y); v.push_back(p.z);
}

// two triangles for the quad a-b-c-d
static void pushQuad(std::vector<GLfloat>& v, glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d) {
    pushVertex(v, a); pushVertex(v, b); pushVertex(v, c);
    pushVertex(v, a); pushVertex(v, c); pushVertex(v, d);
}

// an axis-aligned box as 12 triangles
static void pushBox(std::vector<GLfloat>& v, glm::vec3 lo, glm::vec3 hi) {
    glm::vec3 p[8];
    for (int i = 0; i < 8; ++i)
        p[i] = glm::vec3((i & 1) ? hi.x : lo.x, (i & 2) ? hi.y : lo.y, (i & 4) ? hi.z : lo.z);
    pushQuad(v, p[0], p[1], p[3], p[2]);  // -z face
    pushQuad(v, p[4], p[5], p[7], p[6]);  // +z
    pushQuad(v, p[0], p[4], p[6], p[2]);  // -x
    pushQuad(v, p[1], p[5], p[7], p[3]);  // +x
    pushQuad(v, p[2], p[3], p[7], p[6]);  // top
    pushQuad(v, p[0], p[1], p[5], p[4]);  // bottom
}

// A wall piece along the edge a -> b whose top and bottom follow the ground
// (heights ha, hb at each end), `out` is the direction away from the green.
static void pushSlopedWall(std::vector<GLfloat>& v, glm::vec2 a, glm::vec2 b, float ha, float hb, glm::vec2 out) {
    const float up = 1.0f, down = 0.6f;
    glm::vec3 ib(a.x, ha - down, a.y), it(a.x, ha + up, a.y);          // inner side at a
    glm::vec3 jb(b.x, hb - down, b.y), jt(b.x, hb + up, b.y);          // inner side at b
    glm::vec3 o(out.x, 0.0f, out.y);
    pushQuad(v, ib, jb, jt, it);                    // face toward the green
    pushQuad(v, ib + o, jb + o, jt + o, it + o);    // outside face
    pushQuad(v, it, jt, jt + o, it + o);            // top
    pushQuad(v, ib, it, it + o, ib + o);            // end at a
    pushQuad(v, jb, jt, jt + o, jb + o);            // end at b
}

// ---------- setup ----------

bool GolfRenderer::init() {
    auto compile = [](GLenum type, const char* src) {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, NULL);
        glCompileShader(s);
        GLint ok = 0;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[1024];
            glGetShaderInfoLog(s, sizeof(log), NULL, log);
            std::cout << "Golf shader error:\n" << log << std::endl;
        }
        return s;
    };
    GLuint vs = compile(GL_VERTEX_SHADER, golfVertexSrc);
    GLuint fs = compile(GL_FRAGMENT_SHADER, golfFragmentSrc);
    shader = glCreateProgram();
    glAttachShader(shader, vs);
    glAttachShader(shader, fs);
    glLinkProgram(shader);
    glDeleteShader(vs);
    glDeleteShader(fs);
    modelLoc = glGetUniformLocation(shader, "model");
    colorLoc = glGetUniformLocation(shader, "inputColor");

    std::vector<GLfloat> sphereVerts = parseOBJForSphereVerticies();
    if (sphereVerts.empty()) return false;
    sphere.upload(sphereVerts);

    std::vector<GLfloat> cubeVerts;
    pushBox(cubeVerts, glm::vec3(0.0f), glm::vec3(1.0f));  // unit cube, scaled when drawn
    cube.upload(cubeVerts);
    return true;
}

// Build the ground and walls for the current hole (runs once per hole).
void GolfRenderer::buildHole(const GolfGame& game) {
    const Hole& h = game.hole();
    auto H = [&](float x, float z) { return game.groundHeight(x, z); };

    // bounding box of all the greens
    glm::vec2 lo(1e9f), hi(-1e9f);
    for (const Rect& r : h.greens) { lo = glm::min(lo, r.min); hi = glm::max(hi, r.max); }

    // greens: a grid of 1x1 cells that follows the height function.
    // A checkerboard of two shades (like mowing stripes) makes slopes easy to read.
    std::vector<GLfloat> light, dark;
    for (int ix = (int)std::floor(lo.x); ix < (int)std::ceil(hi.x); ++ix)
        for (int iz = (int)std::floor(lo.y); iz < (int)std::ceil(hi.y); ++iz) {
            float x0 = (float)ix, z0 = (float)iz, x1 = x0 + 1, z1 = z0 + 1;
            if (!game.onGreen(x0 + 0.5f, z0 + 0.5f)) continue;
            bool stripe = ((int)std::floor(x0 / 4.0f) + (int)std::floor(z0 / 4.0f)) % 2 == 0;
            pushQuad(stripe ? light : dark,
                     glm::vec3(x0, H(x0, z0), z0), glm::vec3(x1, H(x1, z0), z0),
                     glm::vec3(x1, H(x1, z1), z1), glm::vec3(x0, H(x0, z1), z1));
        }
    greenLight.upload(light);
    greenDark.upload(dark);

    // rough: a coarser grid around the course, a bit below the greens.
    // Outside the course it keeps the height of the nearest edge, so steep
    // shapes like the halfpipe don't shoot off into the sky.
    std::vector<GLfloat> roughVerts;
    auto roughH = [&](float x, float z) {
        return H(std::clamp(x, lo.x, hi.x), std::clamp(z, lo.y, hi.y)) - 0.6f;
    };
    const float margin = 60.0f, cell = 2.0f;
    for (float x0 = lo.x - margin; x0 < hi.x + margin; x0 += cell)
        for (float z0 = lo.y - margin; z0 < hi.y + margin; z0 += cell) {
            float x1 = x0 + cell, z1 = z0 + cell;
            pushQuad(roughVerts,
                     glm::vec3(x0, roughH(x0, z0), z0), glm::vec3(x1, roughH(x1, z0), z0),
                     glm::vec3(x1, roughH(x1, z1), z1), glm::vec3(x0, roughH(x0, z1), z1));
        }
    rough.upload(roughVerts);

    // walls
    std::vector<GLfloat> wallVerts;
    // explicit walls/blocks: stretch them from below the ground to 1 unit above it
    for (const Box& w : h.walls) {
        float minH = 1e9f, maxH = -1e9f;
        for (float x : { w.min.x, w.max.x, (w.min.x + w.max.x) * 0.5f })
            for (float z : { w.min.z, w.max.z, (w.min.z + w.max.z) * 0.5f }) {
                minH = std::min(minH, H(x, z));
                maxH = std::max(maxH, H(x, z));
            }
        pushBox(wallVerts, glm::vec3(w.min.x, minH - 0.6f, w.min.z),
                           glm::vec3(w.max.x, maxH + (w.max.y - w.min.y), w.max.z));
    }
    // automatic walls: a short wall piece on every edge where green meets non-green
    if (h.autoWalls) {
        const float t = 0.5f; // wall thickness
        for (int ix = (int)std::floor(lo.x); ix < (int)std::ceil(hi.x); ++ix)
            for (int iz = (int)std::floor(lo.y); iz < (int)std::ceil(hi.y); ++iz) {
                float x0 = (float)ix, z0 = (float)iz, x1 = x0 + 1, z1 = z0 + 1;
                float cx = x0 + 0.5f, cz = z0 + 0.5f;
                if (!game.onGreen(cx, cz)) continue;
                auto edge = [&](glm::vec2 a, glm::vec2 b, glm::vec2 out) {
                    pushSlopedWall(wallVerts, a, b, H(a.x, a.y), H(b.x, b.y), out * t);
                };
                bool e = !game.onGreen(cx + 1, cz), w = !game.onGreen(cx - 1, cz);
                bool s2 = !game.onGreen(cx, cz + 1), n = !game.onGreen(cx, cz - 1);
                if (e)  edge({x1, z0}, {x1, z1}, { 1, 0});
                if (w)  edge({x0, z0}, {x0, z1}, {-1, 0});
                if (s2) edge({x0, z1}, {x1, z1}, { 0, 1});
                if (n)  edge({x0, z0}, {x1, z0}, { 0, -1});
                // small posts to close the gaps at outside corners
                auto post = [&](float px, float pz, float sx, float sz) {
                    float hc = H(px, pz);
                    glm::vec3 a(px, hc - 0.6f, pz), b(px + sx * t, hc + 1.0f, pz + sz * t);
                    pushBox(wallVerts, glm::min(a, b), glm::max(a, b));
                };
                if (e && s2) post(x1, z1, 1, 1);
                if (e && n)  post(x1, z0, 1, -1);
                if (w && s2) post(x0, z1, -1, 1);
                if (w && n)  post(x0, z0, -1, -1);
            }
    }
    walls.upload(wallVerts);
    builtVersion = game.holeVersion();
}

// ---------- drawing ----------

void GolfRenderer::drawMesh(const Mesh& mesh, glm::vec4 color) {
    glm::mat4 model(1.0f);
    glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
    glUniform4fv(colorLoc, 1, glm::value_ptr(color));
    mesh.draw();
}

void GolfRenderer::drawSphere(glm::vec3 center, glm::vec3 scale, glm::vec4 color) {
    glm::mat4 model = glm::scale(glm::translate(glm::mat4(1.0f), center), scale);
    glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
    glUniform4fv(colorLoc, 1, glm::value_ptr(color));
    sphere.draw();
}

void GolfRenderer::drawBox(glm::vec3 min, glm::vec3 max, glm::vec4 color) {
    glm::mat4 model = glm::scale(glm::translate(glm::mat4(1.0f), min), max - min);
    glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
    glUniform4fv(colorLoc, 1, glm::value_ptr(color));
    cube.draw();
}

void GolfRenderer::draw(const GolfGame& game, const glm::mat4& view, const glm::mat4& projection,
                        float aimAngle, float power, bool showAim) {
    if (builtVersion != game.holeVersion()) buildHole(game);

    glUseProgram(shader);
    glUniformMatrix4fv(glGetUniformLocation(shader, "view"), 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(shader, "projection"), 1, GL_FALSE, glm::value_ptr(projection));

    const Hole& h = game.hole();
    drawMesh(rough,      glm::vec4(0.20f, 0.45f, 0.18f, 1.0f));
    drawMesh(greenLight, glm::vec4(0.38f, 0.78f, 0.32f, 1.0f));
    drawMesh(greenDark,  glm::vec4(0.31f, 0.68f, 0.27f, 1.0f));
    drawMesh(walls,      glm::vec4(0.55f, 0.36f, 0.20f, 1.0f));
    for (const Bumper& b : h.bumpers)
        drawSphere(b.center, glm::vec3(b.radius, 1.0f, b.radius), glm::vec4(0.90f, 0.25f, 0.20f, 1.0f));

    // the cup: a flattened black sphere is a disk on the ground
    drawSphere(h.cup + glm::vec3(0, 0.04f, 0), glm::vec3(h.cupRadius, 0.02f, h.cupRadius),
               glm::vec4(0.05f, 0.05f, 0.05f, 1.0f));
    // flag: white pole and a red flag
    glm::vec3 c = h.cup;
    drawBox(c + glm::vec3(-0.06f, 0, -0.06f), c + glm::vec3(0.06f, 4.0f, 0.06f), glm::vec4(0.95f, 0.95f, 0.95f, 1));
    drawBox(c + glm::vec3(0.06f, 3.2f, -0.03f), c + glm::vec3(1.6f, 4.0f, 0.03f), glm::vec4(0.9f, 0.1f, 0.1f, 1));

    // ball
    float r = game.ballRadius();
    drawSphere(game.ballPosition(), glm::vec3(r), glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));

    // aim line: dots along the ground in the shot direction, longer and redder with more power
    if (showAim && game.state() == BallState::Ready) {
        glm::vec3 dir(std::sin(aimAngle), 0.0f, -std::cos(aimAngle));
        float spacing = 0.6f + power * 1.4f;
        glm::vec4 color(1.0f, 1.0f - 0.8f * power, 0.2f, 1.0f);
        for (int i = 1; i <= 8; ++i) {
            glm::vec3 p = game.ballPosition() + dir * (r + i * spacing);
            drawSphere(glm::vec3(p.x, game.groundHeight(p.x, p.z) + 0.15f, p.z), glm::vec3(0.12f), color);
        }
    }
    glBindVertexArray(0);
}

void GolfRenderer::cleanup() {
    for (Mesh* m : { &sphere, &cube, &greenLight, &greenDark, &rough, &walls }) m->release();
    if (shader) glDeleteProgram(shader);
    shader = 0;
    builtVersion = -1;
}
