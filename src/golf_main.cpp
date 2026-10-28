// Mini golf built on the physics sandbox.
// Controls: A/D or Left/Right = aim, hold Space = charge power, release = hit,
//           hold C = overview camera, R = restart hole, Enter = next hole, Esc = quit
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <cmath>
#include <algorithm>
#include <string>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "render.h"
#include "golf.h"
#include "golf_render.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

// golf names for strokes relative to par
static const char* scoreName(int strokes, int par) {
    if (strokes == 1) return "Hole in one!";
    switch (strokes - par) {
        case -3: return "Albatross!";
        case -2: return "Eagle!";
        case -1: return "Birdie!";
        case  0: return "Par";
        case  1: return "Bogey";
        case  2: return "Double bogey";
        default: return strokes < par ? "Amazing!" : "Keep practicing";
    }
}

// true only on the frame the key goes down
static bool pressedOnce(GLFWwindow* w, int key, bool& wasDown) {
    bool down = glfwGetKey(w, key) == GLFW_PRESS;
    bool pressed = down && !wasDown;
    wasDown = down;
    return pressed;
}

int main()
{
    Render render;
    GLFWwindow* window = render.glfwSetup(1280, 800, "Mini Golf");
    if (!window) return 1;
    render.imguiSetup(window);

    GolfRenderer golfRenderer;
    if (!golfRenderer.init()) return 1;  // sphere2.obj missing: run from the src folder

    GolfGame game;
    float aimAngle = game.hole().startAim;
    float power = 0.0f;
    float powerDir = 1.0f;     // power goes up then back down while Space is held
    bool charging = false;
    bool rDown = false, enterDown = false;
    bool courseDone = false;
    glm::vec3 camTarget = game.ballPosition();
    float camDistance = 9.0f, camHeight = 4.5f;
    float lastFrame = (float)glfwGetTime();

    while (!glfwWindowShouldClose(window))
    {
        float now = (float)glfwGetTime();
        float dt = now - lastFrame;
        lastFrame = now;
        glfwPollEvents();

        // ---------- input ----------
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, true);

        bool fine = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS; // slow aiming
        float turn = (fine ? 0.3f : 1.5f) * dt;
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS)
            aimAngle -= turn;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS)
            aimAngle += turn;

        bool spaceDown = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
        if (game.state() == BallState::Ready) {
            if (spaceDown) {
                if (!charging) { charging = true; power = 0.0f; powerDir = 1.0f; }
                power += powerDir * 0.8f * dt;               // full power in ~1.25 s
                if (power >= 1.0f) { power = 1.0f; powerDir = -1.0f; }
                if (power <= 0.0f) { power = 0.0f; powerDir = 1.0f; }
            } else if (charging) {
                charging = false;
                if (power > 0.02f) game.hit(aimAngle, power);
            }
        } else {
            charging = false;
        }

        auto goToHole = [&](bool next) {
            if (next && !game.nextHole()) { courseDone = true; return; }
            if (!next) game.resetHole();
            aimAngle = game.hole().startAim;
            power = 0.0f;
            camTarget = game.ballPosition();
        };
        if (pressedOnce(window, GLFW_KEY_R, rDown) && !courseDone) goToHole(false);
        if (pressedOnce(window, GLFW_KEY_ENTER, enterDown) && game.state() == BallState::Sunk && !courseDone)
            goToHole(true);

        // ---------- physics ----------
        game.update(dt);

        // ---------- camera ----------
        int fbW, fbH;
        glfwGetFramebufferSize(window, &fbW, &fbH);
        if (fbW == 0 || fbH == 0) continue; // minimized
        glm::vec3 aimDir(std::sin(aimAngle), 0.0f, -std::cos(aimAngle));
        // follow the ball smoothly instead of snapping
        camTarget += (game.ballPosition() - camTarget) * std::min(1.0f, 6.0f * dt);
        glm::mat4 view;
        if (glfwGetKey(window, GLFW_KEY_C) == GLFW_PRESS) {
            // overview: look down at the middle of the hole
            glm::vec2 lo(1e9f), hi(-1e9f);
            for (const Rect& r : game.hole().greens) { lo = glm::min(lo, r.min); hi = glm::max(hi, r.max); }
            glm::vec3 mid((lo.x + hi.x) * 0.5f, 0.0f, (lo.y + hi.y) * 0.5f);
            mid.y = game.groundHeight(mid.x, mid.z);
            float size = std::max(hi.x - lo.x, hi.y - lo.y);
            view = glm::lookAt(mid + glm::vec3(0, size * 1.1f, size * 0.35f), mid, glm::vec3(0, 1, 0));
        } else {
            glm::vec3 eye = camTarget - aimDir * camDistance + glm::vec3(0, camHeight, 0);
            // don't let the camera sink into a hill behind the ball
            eye.y = std::max(eye.y, game.groundHeight(eye.x, eye.z) + 2.0f);
            glm::vec3 lookAt = camTarget + aimDir * 3.0f;
            lookAt.y = std::max(camTarget.y, game.groundHeight(lookAt.x, lookAt.z));
            view = glm::lookAt(eye, lookAt, glm::vec3(0, 1, 0));
        }
        glm::mat4 projection = glm::perspective(glm::radians(60.0f), (float)fbW / fbH, 0.1f, 1500.0f);

        // ---------- draw ----------
        glViewport(0, 0, fbW, fbH);
        glClearColor(0.55f, 0.78f, 0.95f, 1.0f); // sky
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        golfRenderer.draw(game, view, projection, aimAngle, power, !courseDone);

        // ---------- HUD ----------
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
        ImGui::Begin("Mini Golf", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        const Hole& h = game.hole();
        ImGui::Text("Hole %d/%d: %s", game.holeNumber(), game.holeCount(), h.name.c_str());
        ImGui::Text("Par %d   Strokes: %d", h.par, game.strokes());
        {
            // how far to the cup, and how much higher/lower it is
            glm::vec3 b = game.ballPosition(), c = h.cup;
            float drop = c.y - (b.y - game.ballRadius());
            ImGui::Text("Cup: %.0f away, %s %.1f", glm::length(glm::vec2(c.x - b.x, c.z - b.z)),
                        drop >= 0 ? "uphill" : "downhill", std::fabs(drop));
        }
        ImGui::ProgressBar(power, ImVec2(200, 0), "Power");

        if (courseDone) {
            ImGui::Separator();
            ImGui::Text("Course complete!");
            int total = 0;
            for (int i = 0; i < (int)game.scorecard().size(); ++i) {
                ImGui::Text("Hole %d: %d strokes", i + 1, game.scorecard()[i]);
                total += game.scorecard()[i];
            }
            int diff = total - game.totalPar();
            ImGui::Text("Total: %d (par %d, %s%d)", total, game.totalPar(), diff >= 0 ? "+" : "", diff);
            if (ImGui::Button("Play again")) {
                courseDone = false;
                game.restartCourse();
                aimAngle = game.hole().startAim;
                camTarget = game.ballPosition();
            }
        } else if (game.state() == BallState::Sunk) {
            ImGui::TextColored(ImVec4(1, 0.9f, 0.2f, 1), "%s  (%d strokes)", scoreName(game.strokes(), h.par), game.strokes());
            if (ImGui::Button(game.holeNumber() < game.holeCount() ? "Next hole (Enter)" : "Finish (Enter)"))
                goToHole(true);
            ImGui::SameLine();
            if (ImGui::Button("Retry (R)")) goToHole(false);
        } else if (game.state() == BallState::Rolling) {
            ImGui::Text("Rolling... %.1f", game.speed());
        } else {
            ImGui::Text(game.wasReset() ? "Out of bounds! +1 stroke" : "Aim with A/D, hold Space to swing");
        }

        if (ImGui::CollapsingHeader("Controls")) {
            ImGui::BulletText("A / D or arrows: aim (hold Shift for fine aim)");
            ImGui::BulletText("Hold Space: charge, release: hit");
            ImGui::BulletText("Hold C: overview of the hole");
            ImGui::BulletText("R: restart hole   Esc: quit");
        }
        if (ImGui::CollapsingHeader("Physics tuning")) {
            ImGui::SliderFloat("Max hit speed", &game.maxHitSpeed, 5.0f, 60.0f);
            ImGui::SliderFloat("Rolling drag", &game.rollDrag, 0.0f, 2.0f);
            ImGui::SliderFloat("Rolling friction", &game.rollFriction, 0.0f, 10.0f);
            ImGui::SliderFloat("Wall bounce", &game.wallBounce, 0.0f, 1.0f);
            ImGui::SliderFloat("Bumper bounce", &game.bumperBounce, 0.5f, 2.0f);
            ImGui::SliderFloat("Camera distance", &camDistance, 4.0f, 25.0f);
            ImGui::SliderFloat("Camera height", &camHeight, 1.0f, 15.0f);
        }
        ImGui::End();

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    golfRenderer.cleanup();
    render.imguiCleanup();
    glfwTerminate();
    return 0;
}
