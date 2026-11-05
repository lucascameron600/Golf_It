// Mini golf built on the physics sandbox.
// Controls: A/D or Left/Right = aim,
//           hold right mouse (or both) and swipe down then up = swing
//           (how far you pull down = power),
//           left-drag = aim, scroll = zoom,
//           F = free view (WASD fly, drag to look) and back,
//           Q / E = longer / shorter club (the game suggests one before each shot),
//           hold C = overview camera, R = restart hole, Enter = next hole, Esc = quit
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <cmath>
#include <cstdio>
#include <cctype>
#include <algorithm>
#include <string>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "render.h"
#include "golf.h"
#include "golf_render.h"
#include "clubs.h"
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

// "Rory McIlroy" -> "RORY MCILROY" for the TV-style HUD
static std::string upper(std::string s) {
    for (char& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}

// score against par the way TV shows it: E, -2, +1
static std::string toParText(int d) {
    if (d == 0) return "E";
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%+d", d);
    return buf;
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
    GLFWwindow* window = render.glfwSetup(1280, 800, "Golf");
    if (!window) return 1;
    render.imguiSetup(window);

    GolfRenderer golfRenderer;
    if (!golfRenderer.init()) return 1;  // sphere2.obj missing: run from the src folder

    // HUD font: a bold system font if there is one, else ImGui's built-in font.
    // The built-in one is added first so the Settings panel keeps its usual look.
    ImGui::GetIO().Fonts->AddFontDefault();
    ImFont* hudFont = nullptr;  // nullptr = PushFont keeps the current font, just resized
    for (const char* path : { "C:/Windows/Fonts/arialbd.ttf", "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
                              "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf" }) {
        if (FILE* f = std::fopen(path, "rb")) {
            std::fclose(f);
            hudFont = ImGui::GetIO().Fonts->AddFontFromFileTTF(path, 18.0f);
            break;
        }
    }

    // pro yardages from the spreadsheet; pick the player under Settings
    std::vector<ClubDistance> clubs = loadClubDistances("../data/pro_club_distances.csv");
    printClubDistances(clubs);

    GolfGame game;

    // The chosen club flies a simple arc (gravity only, no air) that lands at the
    // club's yardage and peaks at 30 yards: TrackMan's tour averages have every
    // club topping out within a few yards of that. Row -1 = the putter: roll along the ground.
    const float normalHitSpeed = game.maxHitSpeed;
    auto pickClub = [&](int i) {
        if (i < 0) { game.launchAngle = 0.0f; game.maxHitSpeed = normalHitSpeed; return; }
        float carry = clubs[i].yards * 0.9144f;  // meters
        float peak = 30.0f * 0.9144f;
        // for an arc with no air: peak / carry = tan(launch) / 4, and carry = v^2 sin(2 launch) / g
        game.launchAngle = std::atan(4.0f * peak / carry);
        game.maxHitSpeed = std::sqrt(carry * 9.8f / std::sin(2.0f * game.launchAngle));  // 9.8 = GolfGame's gravity
    };

    // the bag: the player's rows in the CSV (longest first, as in the file) plus
    // the putter (-1) last. Q / E or the HUD arrows step through it.
    std::string proPlayer = clubs.empty() ? "" : clubs[0].player;
    std::vector<int> bag;
    int bagSlot = 0;
    auto selectSlot = [&](int s) {
        bagSlot = std::clamp(s, 0, (int)bag.size() - 1);
        pickClub(bag[bagSlot]);
    };
    auto fillBag = [&]() {
        bag.clear();
        for (int i = 0; i < (int)clubs.size(); i++)
            if (clubs[i].player == proPlayer) bag.push_back(i);
        bag.push_back(-1);
        selectSlot(bagSlot);
    };
    fillBag();
    bool needClub = true;  // hand over the suggested club once the ball is waiting
    bool qDown = false, eDown = false;

    // shot tracer: every ball position since the last hit, and where it first landed
    std::vector<glm::vec3> tracer;
    glm::vec3 shotStart(0.0f), landing(0.0f);
    bool landed = false, wasAirborne = false;

    float aimAngle = game.hole().startAim;
    float power = 0.0f;        // 0..1, set by how far the swing pulls back
    bool rDown = false, enterDown = false;
    // mouse swing: screen points where it started and the bottom of the backswing
    bool swinging = false;
    ImVec2 swingStart, swingTop;
    float lastPathDeg = 0.0f;  // how far off straight the last swing sent the ball
    bool courseDone = false;
    bool realGolf = false;     // which holes: mini golf or the full-size course
    glm::vec3 camTarget = game.ballPosition();
    float camDistance = 9.0f, camHeight = 4.5f;
    float lastFrame = (float)glfwGetTime();

    // after the ball goes back on a tee: face down the hole, forget the old shot
    auto newHole = [&]() {
        aimAngle = game.hole().startAim;
        camTarget = game.ballPosition();
        tracer.clear();
        landed = false;
        needClub = true;
    };

    // the normal view: behind the ball at the usual distance and height. Real golf
    // sits lower and closer behind a smaller ball, so it's small at the bottom of
    // the screen with the hole stretching out ahead
    auto resetCamera = [&]() {
        camTarget = game.ballPosition();
        camDistance = realGolf ? 8.0f : 9.0f;
        camHeight = realGolf ? 2.5f : 4.5f;
    };

    // free view: F lets the camera fly off on its own to look around (WASD, drag
    // to turn your head), and F again snaps back to the normal view
    bool freeView = false, fDown = false;
    glm::vec3 freePos(0.0f);
    float freeYaw = 0.0f, freePitch = 0.0f;  // radians; yaw 0 looks toward -z, like aimAngle
    glm::mat4 lastView(1.0f);                // last frame's camera, to start free view from
    auto freeForward = [&]() {
        return glm::vec3(std::sin(freeYaw) * std::cos(freePitch), std::sin(freePitch),
                         -std::cos(freeYaw) * std::cos(freePitch));
    };
    auto toggleFreeView = [&]() {
        freeView = !freeView;
        if (freeView) {
            // start from wherever the camera is now, so the view doesn't jump
            glm::mat4 cam = glm::inverse(lastView);
            glm::vec3 fwd = -glm::vec3(cam[2]);
            freePos = glm::vec3(cam[3]);
            freeYaw = std::atan2(fwd.x, -fwd.z);
            freePitch = std::asin(std::clamp(fwd.y, -1.0f, 1.0f));
        } else {
            resetCamera();
        }
    };

    while (!glfwWindowShouldClose(window))
    {
        float now = (float)glfwGetTime();
        float dt = now - lastFrame;
        lastFrame = now;
        glfwPollEvents();

        // ---------- input ----------
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, true);

        bool fine = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS; // slow aiming (fast flying in free view)
        float turn = (fine ? 0.3f : 1.5f) * dt;
        // in free view A / D fly the camera, so only the arrow keys aim there
        auto down = [&](int key) { return glfwGetKey(window, key) == GLFW_PRESS; };
        if (down(GLFW_KEY_LEFT) || (!freeView && down(GLFW_KEY_A))) aimAngle -= turn;
        if (down(GLFW_KEY_RIGHT) || (!freeView && down(GLFW_KEY_D))) aimAngle += turn;

        // mouse: ImGui fills these in during its frame, so this reads last frame's movement
        ImGuiIO& io = ImGui::GetIO();
        if (pressedOnce(window, GLFW_KEY_F, fDown)) toggleFreeView();
        if (freeView) {
            // left-drag turns your head; WASD flies where you're looking,
            // Space / Ctrl straight up / down, Shift 3x faster
            if (!io.WantCaptureMouse && io.MouseDown[0]) {
                freeYaw += io.MouseDelta.x * 0.005f;  // radians per pixel
                freePitch = std::clamp(freePitch - io.MouseDelta.y * 0.005f, -1.5f, 1.5f);  // not quite straight up/down
            }
            glm::vec3 fwd = freeForward(), right(std::cos(freeYaw), 0.0f, std::sin(freeYaw));
            float move = (fine ? 90.0f : 30.0f) * dt;  // 30 m/s
            if (down(GLFW_KEY_W)) freePos += fwd * move;
            if (down(GLFW_KEY_S)) freePos -= fwd * move;
            if (down(GLFW_KEY_D)) freePos += right * move;
            if (down(GLFW_KEY_A)) freePos -= right * move;
            if (down(GLFW_KEY_SPACE)) freePos.y += move;
            if (down(GLFW_KEY_LEFT_CONTROL)) freePos.y -= move;
            freePos.y = std::max(freePos.y, game.groundHeight(freePos.x, freePos.z) + 0.5f);  // stay above the grass
        } else if (!io.WantCaptureMouse && !io.MouseDown[1]) {  // not while using the panel or swinging
            // left-drag sideways aims (the camera sits behind the aim line, so it
            // turns with the aim); scroll zooms
            if (io.MouseDown[0]) aimAngle += io.MouseDelta.x * (fine ? 0.001f : 0.005f);  // radians per pixel
            camDistance = std::clamp(camDistance - io.MouseWheel, 4.0f, 25.0f);
        }

        // mouse swing: hold the right button (or both), pull down for the backswing,
        // then push back up past where you started; that's impact. Power is how
        // far you pulled back: 30% of the window height is full power, so it feels
        // the same at any window size. Screen y grows downward, so "down" is a bigger y.
        float fullSwing = io.DisplaySize.y * 0.3f;
        if (!swinging && !freeView && io.MouseDown[1] && !io.WantCaptureMouse && game.state() == BallState::Ready) {
            swinging = true;
            swingStart = swingTop = io.MousePos;
            power = 0.0f;
        }
        if (swinging) {
            ImVec2 m = io.MousePos;
            if (!io.MouseDown[1] || game.state() != BallState::Ready) {
                swinging = false;                    // let go before impact: no shot
                power = 0.0f;
            } else if (m.y > swingTop.y) {
                swingTop = m;                        // still going back
                power = std::min(1.0f, (swingTop.y - swingStart.y) / fullSwing);
            } else if (m.y <= swingStart.y && swingTop.y - swingStart.y > 10.0f) {
                // impact. Club path = how much the downswing leaned sideways, as an
                // angle off straight up; a quarter of it turns the shot so a slightly
                // crooked swipe doesn't send it out of bounds
                float path = std::atan2(m.x - swingTop.x, swingTop.y - m.y);
                lastPathDeg = glm::degrees(path * 0.25f);
                if (power > 0.02f) {
                    tracer.clear();  // a new shot: start a fresh tracer from here
                    shotStart = game.ballPosition();
                    landed = wasAirborne = false;
                    game.hit(aimAngle + path * 0.25f, power);
                }
                swinging = false;
            }
        }

        // Q / E: one club longer / shorter
        if (pressedOnce(window, GLFW_KEY_Q, qDown)) selectSlot(bagSlot - 1);
        if (pressedOnce(window, GLFW_KEY_E, eDown)) selectSlot(bagSlot + 1);

        auto goToHole = [&](bool next) {
            if (next && !game.nextHole()) { courseDone = true; return; }
            if (!next) game.resetHole();
            newHole();
        };
        if (pressedOnce(window, GLFW_KEY_R, rDown) && !courseDone) goToHole(false);
        if (pressedOnce(window, GLFW_KEY_ENTER, enterDown) && game.state() == BallState::Sunk && !courseDone)
            goToHole(true);

        // ---------- physics ----------
        game.update(dt);

        // shot tracer: remember the ball's path, and the first touchdown after
        // it's been properly in the air (so a putt or a hop off the tee doesn't count)
        if (game.state() == BallState::Rolling) {
            glm::vec3 b = game.ballPosition();
            tracer.push_back(b);
            float above = b.y - game.ballRadius() - game.groundHeight(b.x, b.z);
            if (above > 0.5f) wasAirborne = true;
            if (wasAirborne && !landed && above < 0.06f) { landed = true; landing = b; }
        }

        // ball waiting for the next shot: hand over the suggested club once
        // (putter on the green and in mini golf); Q / E can still change it
        if (game.state() != BallState::Ready) {
            needClub = true;
        } else if (needClub) {
            const Hole& h = game.hole();
            glm::vec3 b = game.ballPosition();
            float toPin = glm::length(glm::vec2(h.cup.x - b.x, h.cup.z - b.z));
            bool putting = h.greenRadius == 0.0f || toPin < h.greenRadius;
            selectSlot(suggestClub(clubs, bag, toPin, putting));
            needClub = false;
        }

        // ---------- camera ----------
        int fbW, fbH;
        glfwGetFramebufferSize(window, &fbW, &fbH);
        if (fbW == 0 || fbH == 0) continue; // minimized
        glm::vec3 aimDir(std::sin(aimAngle), 0.0f, -std::cos(aimAngle));
        // follow the ball smoothly instead of snapping
        camTarget += (game.ballPosition() - camTarget) * std::min(1.0f, 6.0f * dt);
        glm::mat4 view;
        if (freeView) {
            view = glm::lookAt(freePos, freePos + freeForward(), glm::vec3(0, 1, 0));
        } else if (glfwGetKey(window, GLFW_KEY_C) == GLFW_PRESS) {
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
            // real golf looks well down the hole (like the TV view from behind the
            // golfer) so a 30-yard-high shot stays on screen; mini golf looks just ahead
            glm::vec3 lookAt = camTarget + aimDir * (realGolf ? 30.0f : 3.0f);
            lookAt.y = std::max(camTarget.y, game.groundHeight(lookAt.x, lookAt.z));
            view = glm::lookAt(eye, lookAt, glm::vec3(0, 1, 0));
        }
        lastView = view;
        glm::mat4 projection = glm::perspective(glm::radians(60.0f), (float)fbW / fbH, 0.1f, 1500.0f);

        // ---------- draw ----------
        glViewport(0, 0, fbW, fbH);
        glClearColor(0.55f, 0.78f, 0.95f, 1.0f); // sky
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        golfRenderer.draw(game, view, projection, aimAngle, power, false);  // the shot preview below replaces its aim dots
        {
            // tee peg: a waiting ball above the ground is sitting on a tee
            glm::vec3 b = game.ballPosition();
            float ground = game.groundHeight(b.x, b.z), bottom = b.y - game.ballRadius();
            if (game.state() == BallState::Ready && bottom > ground + 0.05f)
                golfRenderer.drawBox(glm::vec3(b.x - 0.06f, ground, b.z - 0.06f),
                                     glm::vec3(b.x + 0.06f, bottom, b.z + 0.06f), glm::vec4(1, 1, 1, 1));
        }

        // ---------- HUD ----------
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        const ImVec2 screen = io.DisplaySize;

        // Settings (top right, folded up to start): course, player, controls, tuning.
        // Drawn first, because switching courses replaces the hole list read below.
        ImGui::SetNextWindowPos(ImVec2(screen.x - 16, 16), ImGuiCond_Always, ImVec2(1, 0));  // grows leftward when opened
        ImGui::SetNextWindowCollapsed(true, ImGuiCond_FirstUseEver);
        ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        int course = realGolf ? 1 : 0;
        ImGui::RadioButton("Mini golf", &course, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Real golf", &course, 1);
        if ((course == 1) != realGolf) {
            realGolf = course == 1;
            game.loadCourse(realGolf);
            courseDone = false;
            newHole();
            resetCamera();
        }
        if (!clubs.empty() && ImGui::BeginCombo("Player", proPlayer.c_str())) {
            for (int i = 0; i < (int)clubs.size(); i++) {
                if (i > 0 && clubs[i].player == clubs[i - 1].player) continue;  // each name once
                if (ImGui::Selectable(clubs[i].player.c_str(), clubs[i].player == proPlayer)) {
                    proPlayer = clubs[i].player;
                    fillBag();
                    needClub = true;
                }
            }
            ImGui::EndCombo();
        }
        if (game.launchAngle > 0.0f)
            ImGui::Text("Full power: %.0f m/s at %.0f deg", game.maxHitSpeed, glm::degrees(game.launchAngle));
        else
            ImGui::Text("Putt: full power %.1f m/s", game.maxHitSpeed);
        if (ImGui::CollapsingHeader("Controls")) {
            ImGui::BulletText("A / D or arrows: aim (hold Shift for fine aim)");
            ImGui::BulletText("Left-drag: aim, scroll: zoom");
            ImGui::BulletText("F: free view (WASD fly, drag to look, Space / Ctrl up / down,");
            ImGui::BulletText("   Shift faster, arrows aim); F again: back behind the ball");
            ImGui::BulletText("Hold right mouse (or both): pull down, push up to hit");
            ImGui::BulletText("Pull back farther for more power (red mark = full)");
            ImGui::BulletText("Lean the push left/right to start the ball left/right");
            ImGui::BulletText("Q / E: longer / shorter club");
            ImGui::BulletText("Hold C: overview of the hole");
            ImGui::BulletText("R: restart hole   Esc: quit");
        }
        if (ImGui::CollapsingHeader("Physics tuning")) {
            ImGui::SliderFloat("Max hit speed", &game.maxHitSpeed, 5.0f, 60.0f);
            ImGui::SliderFloat("Rolling drag", &game.rollDrag, 0.0f, 2.0f);
            ImGui::SliderFloat("Rolling friction", &game.rollFriction, 0.0f, 10.0f);
            ImGui::SliderFloat("Landing roll", &game.landingRoll, 0.0f, 1.0f);
            ImGui::SliderFloat("Wall bounce", &game.wallBounce, 0.0f, 1.0f);
            ImGui::SliderFloat("Bumper bounce", &game.bumperBounce, 0.5f, 2.0f);
            ImGui::SliderFloat("Camera distance", &camDistance, 4.0f, 25.0f);
            ImGui::SliderFloat("Camera height", &camHeight, 1.0f, 15.0f);
        }
        ImGui::End();

        // TV-style boxes: navy, no title bars, fixed in place
        const Hole& h = game.hole();
        glm::vec3 ball = game.ballPosition();
        const float yd = 1.0f / 0.9144f;  // meters -> yards
        float toPinYd = glm::length(glm::vec2(h.cup.x - ball.x, h.cup.z - ball.z)) * yd;
        float carryYd = glm::length(glm::vec2(landing.x - shotStart.x, landing.z - shotStart.z)) * yd;
        float totalYd = glm::length(glm::vec2(ball.x - shotStart.x, ball.z - shotStart.z)) * yd;
        const ImGuiWindowFlags hudFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                          ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                          ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.04f, 0.09f, 0.20f, 0.88f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14, 10));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 3.0f);

        // scorebug (top left): player, score to par over the finished holes, holes finished
        int played = (int)game.scorecard().size(), toPar = 0;
        for (int i = 0; i < played; i++) toPar += game.scorecard()[i] - game.allHoles()[i].par;
        std::string playerName = upper(proPlayer.empty() ? "Player" : proPlayer);
        ImGui::SetNextWindowPos(ImVec2(16, 16));
        ImGui::Begin("##scorebug", nullptr, hudFlags | ImGuiWindowFlags_NoInputs);
        ImGui::PushFont(hudFont, 26.0f);
        ImGui::Text("%s", playerName.c_str());
        ImGui::SameLine(0, 28);
        // TV colors: red under par, white even, blue over
        ImVec4 parColor = toPar < 0 ? ImVec4(1.0f, 0.32f, 0.25f, 1) : toPar > 0 ? ImVec4(0.55f, 0.78f, 1.0f, 1) : ImVec4(1, 1, 1, 1);
        ImGui::TextColored(parColor, "%s", toParText(toPar).c_str());
        ImGui::PopFont();
        ImGui::PushFont(hudFont, 15.0f);
        ImGui::TextDisabled("THRU %d", played);
        ImGui::PopFont();
        float bugBottom = ImGui::GetWindowPos().y + ImGui::GetWindowHeight();
        ImGui::End();

        // hole card, under the scorebug: hole, par, name, distance left, shot number
        ImGui::SetNextWindowPos(ImVec2(16, bugBottom + 6));
        ImGui::Begin("##holecard", nullptr, hudFlags | ImGuiWindowFlags_NoInputs);
        ImGui::PushFont(hudFont, 18.0f);
        ImGui::Text("HOLE %d   PAR %d", game.holeNumber(), h.par);
        ImGui::PopFont();
        ImGui::PushFont(hudFont, 15.0f);
        ImGui::TextDisabled("%s", h.name.c_str());
        ImGui::Text("%.0f YDS TO PIN", toPinYd);
        ImGui::SameLine(0, 18);
        ImGui::TextDisabled("SHOT %d", game.strokes() + (game.state() == BallState::Ready ? 1 : 0));
        ImGui::PopFont();
        ImGui::End();

        // club and power (bottom middle), then what the shot is doing
        ImGui::SetNextWindowPos(ImVec2(screen.x * 0.5f, screen.y - 16), ImGuiCond_Always, ImVec2(0.5f, 1.0f));
        ImGui::Begin("##clubbar", nullptr, hudFlags);
        int row = bag[bagSlot];
        ImGui::PushFont(hudFont, 15.0f);
        if (ImGui::ArrowButton("##longer", ImGuiDir_Left)) selectSlot(bagSlot - 1);
        ImGui::SameLine();
        ImGui::PushFont(hudFont, 24.0f);
        ImGui::Text("%s", upper(row < 0 ? "Putter" : clubs[row].club).c_str());
        ImGui::PopFont();
        ImGui::SameLine();
        if (row >= 0) ImGui::TextDisabled("%d YD", clubs[row].yards);
        else ImGui::TextDisabled("ROLLS");
        ImGui::SameLine();
        if (ImGui::ArrowButton("##shorter", ImGuiDir_Right)) selectSlot(bagSlot + 1);
        ImGui::SameLine();
        ImGui::TextDisabled("Q / E");
        ImGui::SameLine(0, 18);
        if (ImGui::Button(freeView ? "BACK TO BALL (F)" : "FREE VIEW (F)")) toggleFreeView();
        ImGui::ProgressBar(power, ImVec2(340, 0), "POWER");  // fills as you pull back
        if (courseDone) {
            ImGui::Text("Course complete!");
        } else if (game.state() == BallState::Sunk) {
            ImGui::PushFont(hudFont, 22.0f);
            ImGui::TextColored(ImVec4(1, 0.85f, 0.2f, 1), "%s", upper(scoreName(game.strokes(), h.par)).c_str());
            ImGui::PopFont();
            if (ImGui::Button(game.holeNumber() < game.holeCount() ? "Next hole (Enter)" : "Finish (Enter)"))
                goToHole(true);
            ImGui::SameLine();
            if (ImGui::Button("Retry (R)")) goToHole(false);
        } else if (game.state() == BallState::Rolling) {
            if (landed) ImGui::Text("CARRY %.0f YD", carryYd);
            else ImGui::Text("%s", wasAirborne ? "IN THE AIR..." : "ROLLING...");
        } else if (freeView) {
            ImGui::Text("FREE VIEW: WASD fly, drag to look, Space / Ctrl up / down, Shift faster, arrows aim");
        } else if (swinging) {
            ImGui::Text("Push up past the line to hit, let go to cancel");
        } else if (game.wasReset()) {
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.3f, 1), "OUT OF BOUNDS  +1 STROKE");
        } else if (!tracer.empty()) {
            // the last shot, like a TV stat line
            if (landed) ImGui::Text("CARRY %.0f YD   TOTAL %.0f YD", carryYd, totalYd);
            else ImGui::Text("ROLLED %.0f YD", totalYd);
            ImGui::SameLine(0, 18);
            ImGui::TextDisabled("%.1f DEG %s", std::fabs(lastPathDeg), lastPathDeg >= 0 ? "RIGHT" : "LEFT");
        } else {
            ImGui::Text("Hold right mouse, pull down, push up to swing");
        }
        ImGui::PopFont();
        ImGui::End();

        // final scorecard in the middle once the last hole is done
        if (courseDone) {
            ImGui::SetNextWindowPos(ImVec2(screen.x * 0.5f, screen.y * 0.4f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            ImGui::Begin("##final", nullptr, hudFlags);
            ImGui::PushFont(hudFont, 24.0f);
            ImGui::Text("FINAL   %s", playerName.c_str());
            ImGui::PopFont();
            ImGui::PushFont(hudFont, 16.0f);
            int total = 0;
            for (int i = 0; i < (int)game.scorecard().size(); ++i) {
                ImGui::Text("HOLE %d   PAR %d   %d", i + 1, game.allHoles()[i].par, game.scorecard()[i]);
                total += game.scorecard()[i];
            }
            ImGui::Text("TOTAL %d   %s", total, toParText(total - game.totalPar()).c_str());
            if (ImGui::Button("Play again")) {
                courseDone = false;
                game.restartCourse();
                newHole();
            }
            ImGui::PopFont();
            ImGui::End();
        }
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();

        // shot tracer, drawn over the 3D view like on TV: project each point
        // through the same camera matrices as the scene to get screen points
        auto toScreen = [&](glm::vec3 p, ImVec2& out) {
            glm::vec4 c = projection * view * glm::vec4(p, 1.0f);
            if (c.w < 0.1f) return false;  // behind the camera
            out = ImVec2((c.x / c.w * 0.5f + 0.5f) * screen.x, (0.5f - c.y / c.w * 0.5f) * screen.y);
            return true;
        };
        ImDrawList* under = ImGui::GetBackgroundDrawList();  // behind the HUD boxes
        ImVec2 p0, p1;
        for (size_t i = 1; i < tracer.size(); i++)
            if (toScreen(tracer[i - 1], p0) && toScreen(tracer[i], p1))
                under->AddLine(p0, p1, IM_COL32(255, 70, 40, 235), 3.0f);
        if (landed && toScreen(landing, p0)) {
            // where it came down: a ring with the carry next to it
            under->AddCircle(p0, 9.0f, IM_COL32(255, 255, 255, 235), 0, 2.5f);
            char label[16];
            std::snprintf(label, sizeof(label), "%.0f yd", carryYd);
            under->AddText(ImVec2(p0.x + 13, p0.y - 8), IM_COL32(255, 255, 255, 235), label);
        }

        // shot preview: play the shot out on a copy of the game (like the editor's
        // bot does), so it uses the exact same physics and ground as the real one.
        // Full power until you swing, then however far you've pulled back.
        // It aims straight: a crooked push still turns the real shot.
        if (game.state() == BallState::Ready && !courseDone) {
            GolfGame sim = game;
            sim.hit(aimAngle, swinging ? std::max(power, 0.02f) : 1.0f);
            std::vector<glm::vec3> path;
            int landAt = -1;  // index of the first touchdown after flying
            bool flew = false;
            for (int f = 0; f < 60 * 12 && sim.state() == BallState::Rolling; f++) {
                sim.update(1.0f / 60.0f);
                if (sim.wasReset()) break;  // it would go out of bounds: show up to there
                glm::vec3 q = sim.ballPosition();
                path.push_back(q);
                float above = q.y - sim.ballRadius() - sim.groundHeight(q.x, q.z);
                if (above > 0.5f) flew = true;
                if (flew && landAt < 0 && above < 0.06f) landAt = (int)path.size() - 1;
            }
            // white dots through the air, smaller ones along the roll, and a faint
            // shadow on the ground under the flight so you can see how far it goes
            int step = std::max(1, (int)path.size() / 45);
            for (int i = 0; i < (int)path.size(); i += step) {
                bool rolling = landAt >= 0 && i > landAt;
                glm::vec3 q = path[i];
                if (!rolling && toScreen(glm::vec3(q.x, sim.groundHeight(q.x, q.z), q.z), p0))
                    under->AddCircleFilled(p0, 1.5f, IM_COL32(20, 40, 20, 110));
                if (toScreen(q, p0))
                    under->AddCircleFilled(p0, rolling ? 2.0f : 3.0f, IM_COL32(255, 255, 255, rolling ? 140 : 210));
            }
            if (landAt >= 0 && toScreen(path[landAt], p0)) {
                under->AddCircle(p0, 8.0f, IM_COL32(255, 255, 255, 210), 0, 2.0f);
                char label[16];
                std::snprintf(label, sizeof(label), "%.0f yd",
                              glm::length(glm::vec2(path[landAt].x - ball.x, path[landAt].z - ball.z)) * yd);
                under->AddText(ImVec2(p0.x + 12, p0.y - 8), IM_COL32(255, 255, 255, 210), label);
            }
        }

        if (swinging) {
            // the swing so far: start line, full-power mark, backswing, and the way back up
            ImDrawList* over = ImGui::GetForegroundDrawList();
            ImU32 white = IM_COL32(255, 255, 255, 220);
            over->AddLine(ImVec2(swingStart.x - 40, swingStart.y), ImVec2(swingStart.x + 40, swingStart.y), white, 2.0f);
            float full = swingStart.y + fullSwing;  // pull back to here for full power
            over->AddLine(ImVec2(swingStart.x - 20, full), ImVec2(swingStart.x + 20, full), IM_COL32(255, 80, 60, 220), 2.0f);
            over->AddLine(swingStart, swingTop, white, 3.0f);
            over->AddLine(swingTop, io.MousePos, IM_COL32(255, 220, 60, 255), 3.0f);
        }

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    golfRenderer.cleanup();
    render.imguiCleanup();
    glfwTerminate();
    return 0;
}
