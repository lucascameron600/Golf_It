// Mini golf level editor (for designing holes; not part of the game).
//
// Top view: drag the colored handles with the left mouse button, pan with the
// right mouse button (or W/A/S/D), zoom with the scroll wheel.
// 3D view: right-drag to orbit, scroll to zoom.
// V = switch between top and 3D view.
// Tab = play-test the hole, P = play-test from the spot under the mouse.
// Ctrl+S = save the hole as C++ code into saved_holes/, Ctrl+Z = undo, Delete = delete selected.
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>
#include <fstream>
#include <algorithm>
#include <filesystem>
#include <cctype>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "render.h"
#include "golf.h"
#include "golf_render.h"
#include "golf_tools.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

// ---------- what can be selected ----------

enum class Kind { None, Start, Cup, PathPoint, Rect, Wall, Bumper, Stamp };

struct Selection {
    Kind kind = Kind::None;
    int index = 0;
    int part = 0;   // 0 = move the whole thing, 1 = min corner, 2 = max corner, 3 = area center
    bool operator==(const Selection& o) const { return kind == o.kind && index == o.index && part == o.part; }
};

struct Handle {
    Selection sel;
    glm::vec2 pos;     // x, z
    glm::vec4 color;
    float scale;       // relative size
};

static const char* stampNames[] = { "Bump / bowl", "Ramp", "Rollers", "Tilt", "Halfpipe" };

// ---------- the editor ----------

struct Editor {
    GolfGame game;
    GolfRenderer renderer;
    int editIndex = 0;
    Hole work;                      // the hole being edited
    std::vector<Hole> undoStack;
    bool unsaved = false;
    bool changed = false;           // work changed this frame -> push it into the game

    Selection selected;
    bool dragging = false;
    glm::vec2 dragOffset{0.0f};

    bool playing = false;
    bool topView = true;
    glm::vec2 viewCenter{0.0f};
    float viewWidth = 80.0f;        // top view: world units across the screen
    float orbitYaw = 0.6f, orbitPitch = 0.7f, orbitDist = 60.0f;

    bool snap = true;
    float gridSize = 1.0f;
    float aspect = 1500.0f / 900.0f;   // window shape (updated every frame)
    float panelFrac = 410.0f / 1500.0f; // part of the window covered by the panel

    // play test
    float aimAngle = 0.0f, power = 0.0f, powerDir = 1.0f;
    bool charging = false;
    glm::vec3 camTarget{0.0f};

    BotResult bot;
    bool botValid = false;

    std::string status;
    float statusTime = 0.0f;
    char nameBuf[64] = {};

    // ----- hole management -----

    void loadForEditing(int index) {
        editIndex = index;
        work = game.allHoles()[index];
        game.loadHole(index);
        undoStack.clear();
        selected = Selection();
        unsaved = false;
        botValid = false;
        playing = false;
        std::snprintf(nameBuf, sizeof(nameBuf), "%s", work.name.c_str());
        fitView();
    }

    void fitView() {
        glm::vec2 lo(1e9f), hi(-1e9f);
        for (const Rect& r : game.hole().greens) { lo = glm::min(lo, r.min); hi = glm::max(hi, r.max); }
        if (lo.x > hi.x) { lo = glm::vec2(-20); hi = glm::vec2(20); }
        // fit the hole into the part of the window to the right of the panel
        float usable = 1.0f - panelFrac;
        viewWidth = std::max((hi.x - lo.x) / usable, (hi.y - lo.y) * aspect) * 1.15f + 8.0f;
        viewCenter = (lo + hi) * 0.5f;
        viewCenter.x -= panelFrac * 0.5f * viewWidth;
        orbitDist = std::max(hi.x - lo.x, hi.y - lo.y) * 0.9f + 20.0f;
    }

    void pushUndo(const Hole& before) {
        undoStack.push_back(before);
        if (undoStack.size() > 200) undoStack.erase(undoStack.begin());
    }

    void undo() {
        if (undoStack.empty()) return;
        work = undoStack.back();
        undoStack.pop_back();
        std::snprintf(nameBuf, sizeof(nameBuf), "%s", work.name.c_str());
        selected = Selection();
        changed = true;
    }

    void setStatus(const std::string& s) { status = s; statusTime = 4.0f; }

    // middle of the part of the top view you can see (right of the panel); new pieces go here
    glm::vec2 visibleCenter() const { return viewCenter + glm::vec2(panelFrac * 0.5f * viewWidth, 0.0f); }

    float snapValue(float v) const { return snap ? std::round(v / gridSize) * gridSize : v; }
    glm::vec2 snapPoint(glm::vec2 p) const { return glm::vec2(snapValue(p.x), snapValue(p.y)); }

    // ----- handles -----

    std::vector<Handle> handles() const {
        std::vector<Handle> hs;
        hs.push_back({ {Kind::Start, 0, 0}, {work.start.x, work.start.z}, {1, 1, 1, 1}, 1.3f });
        hs.push_back({ {Kind::Cup, 0, 0}, {work.cup.x, work.cup.z}, {0.1f, 0.1f, 0.1f, 1}, 1.3f });
        for (int i = 0; i < (int)work.path.size(); ++i)
            hs.push_back({ {Kind::PathPoint, i, 0}, work.path[i], {1.0f, 0.85f, 0.1f, 1}, 1.0f });
        for (int i = 0; i < (int)work.rects.size(); ++i) {
            const Rect& r = work.rects[i];
            hs.push_back({ {Kind::Rect, i, 0}, (r.min + r.max) * 0.5f, {0.2f, 0.9f, 0.3f, 1}, 1.0f });
            hs.push_back({ {Kind::Rect, i, 1}, r.min, {0.6f, 1.0f, 0.6f, 1}, 0.7f });
            hs.push_back({ {Kind::Rect, i, 2}, r.max, {0.6f, 1.0f, 0.6f, 1}, 0.7f });
        }
        for (int i = 0; i < (int)work.walls.size(); ++i) {
            const Box& b = work.walls[i];
            glm::vec2 mn(b.min.x, b.min.z), mx(b.max.x, b.max.z);
            hs.push_back({ {Kind::Wall, i, 0}, (mn + mx) * 0.5f, {0.75f, 0.45f, 0.2f, 1}, 1.0f });
            hs.push_back({ {Kind::Wall, i, 1}, mn, {0.95f, 0.7f, 0.45f, 1}, 0.7f });
            hs.push_back({ {Kind::Wall, i, 2}, mx, {0.95f, 0.7f, 0.45f, 1}, 0.7f });
        }
        for (int i = 0; i < (int)work.bumpers.size(); ++i)
            hs.push_back({ {Kind::Bumper, i, 0}, {work.bumpers[i].center.x, work.bumpers[i].center.z}, {1.0f, 0.3f, 0.3f, 1}, 1.0f });
        for (int i = 0; i < (int)work.stamps.size(); ++i) {
            const Stamp& s = work.stamps[i];
            if (s.type == Stamp::Bump)
                hs.push_back({ {Kind::Stamp, i, 0}, s.center, {0.3f, 0.5f, 1.0f, 1}, 1.0f });
            if (s.limited) {
                hs.push_back({ {Kind::Stamp, i, 3}, (s.area.min + s.area.max) * 0.5f, {0.5f, 0.8f, 1.0f, 1}, 0.9f });
                hs.push_back({ {Kind::Stamp, i, 1}, s.area.min, {0.7f, 0.85f, 1.0f, 1}, 0.7f });
                hs.push_back({ {Kind::Stamp, i, 2}, s.area.max, {0.7f, 0.85f, 1.0f, 1}, 0.7f });
            }
        }
        return hs;
    }

    static void fixRect(glm::vec2& mn, glm::vec2& mx) {
        glm::vec2 a = glm::min(mn, mx), b = glm::max(mn, mx);
        mn = a; mx = b;
    }

    // move a handle to a new position
    void moveHandle(const Selection& s, glm::vec2 p) {
        switch (s.kind) {
        case Kind::Start: work.start.x = p.x; work.start.z = p.y; break;
        case Kind::Cup:   work.cup.x = p.x;   work.cup.z = p.y;   break;
        case Kind::PathPoint: work.path[s.index] = p; break;
        case Kind::Rect: {
            Rect& r = work.rects[s.index];
            if (s.part == 0) { glm::vec2 d = p - (r.min + r.max) * 0.5f; r.min += d; r.max += d; }
            if (s.part == 1) r.min = p;
            if (s.part == 2) r.max = p;
            break;
        }
        case Kind::Wall: {
            Box& b = work.walls[s.index];
            glm::vec2 mn(b.min.x, b.min.z), mx(b.max.x, b.max.z);
            if (s.part == 0) { glm::vec2 d = p - (mn + mx) * 0.5f; mn += d; mx += d; }
            if (s.part == 1) mn = p;
            if (s.part == 2) mx = p;
            b.min.x = mn.x; b.min.z = mn.y; b.max.x = mx.x; b.max.z = mx.y;
            break;
        }
        case Kind::Bumper: work.bumpers[s.index].center.x = p.x; work.bumpers[s.index].center.z = p.y; break;
        case Kind::Stamp: {
            Stamp& st = work.stamps[s.index];
            if (s.part == 0) st.center = p;
            if (s.part == 1) st.area.min = p;
            if (s.part == 2) st.area.max = p;
            if (s.part == 3) { glm::vec2 d = p - (st.area.min + st.area.max) * 0.5f; st.area.min += d; st.area.max += d; }
            break;
        }
        default: break;
        }
        changed = true;
    }

    // after dragging a corner past the opposite one, swap so min < max
    void tidySelection() {
        if (selected.kind == Kind::Rect) fixRect(work.rects[selected.index].min, work.rects[selected.index].max);
        if (selected.kind == Kind::Wall) {
            Box& b = work.walls[selected.index];
            glm::vec2 mn(b.min.x, b.min.z), mx(b.max.x, b.max.z);
            fixRect(mn, mx);
            b.min.x = mn.x; b.min.z = mn.y; b.max.x = mx.x; b.max.z = mx.y;
        }
        if (selected.kind == Kind::Stamp) fixRect(work.stamps[selected.index].area.min, work.stamps[selected.index].area.max);
    }

    void deleteSelected() {
        if (selected.kind == Kind::None || selected.kind == Kind::Start || selected.kind == Kind::Cup) return;
        int i = selected.index;
        if (selected.kind == Kind::PathPoint) work.path.erase(work.path.begin() + i);
        if (selected.kind == Kind::Rect)      work.rects.erase(work.rects.begin() + i);
        if (selected.kind == Kind::Wall)      work.walls.erase(work.walls.begin() + i);
        if (selected.kind == Kind::Bumper)    work.bumpers.erase(work.bumpers.begin() + i);
        if (selected.kind == Kind::Stamp)     work.stamps.erase(work.stamps.begin() + i);
        selected = Selection();
        changed = true;
    }

    // ----- saving -----

    void save() {
        namespace fs = std::filesystem;
        work.name = nameBuf;
        game.replaceHole(editIndex, work);
        std::string code = holeToCpp(game.allHoles()[editIndex]);

        std::string safeName;
        for (char c : work.name) safeName += std::isalnum((unsigned char)c) ? (char)std::tolower((unsigned char)c) : '_';
        if (safeName.empty()) safeName = "hole";
        std::time_t now = std::time(nullptr);
        char stamp[32];
        std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", std::localtime(&now));
        char nice[64];
        std::strftime(nice, sizeof(nice), "%Y-%m-%d %H:%M", std::localtime(&now));

        std::error_code ec;
        fs::create_directories("saved_holes", ec);
        std::string path = "saved_holes/" + safeName + "_" + stamp + ".txt";
        for (int n = 2; fs::exists(path); ++n)   // never overwrite an earlier save
            path = "saved_holes/" + safeName + "_" + stamp + "_" + std::to_string(n) + ".txt";
        std::ofstream out(path);
        if (!out) { setStatus("Could not write " + path); return; }
        out << "// Saved from the mini golf level editor on " << nice << "\n"
            << "// Paste this block into buildCourse() in golf.cpp, just above the line\n"
            << "//     // ---- paste holes saved from the level editor above this line ----\n"
            << "// If it's a new version of an existing hole, delete that hole's old block.\n"
            << "// Holes are played in the order they appear in buildCourse().\n\n"
            << code;
        out.close();
        ImGui::SetClipboardText(code.c_str());
        unsaved = false;
        setStatus("Saved to src/" + path + " (also copied to the clipboard)");
    }
};

// ---------- small UI helpers ----------

static bool axisPicker(char& axis) {
    int a = (axis == 'x') ? 0 : 1;
    bool c = ImGui::RadioButton("along x", &a, 0); ImGui::SameLine();
    c |= ImGui::RadioButton("along z", &a, 1);
    axis = a == 0 ? 'x' : 'z';
    return c;
}

static Hole newHoleTemplate() {
    Hole h;
    h.name = "New Hole";
    h.par = 3;
    h.start = glm::vec3(0, 0, 0);
    h.cup = glm::vec3(0, 0, -40);
    h.path = { {0, 4}, {0, -46} };
    h.pathWidth = 14;
    return h;
}

// ---------- main ----------

int main()
{
    Render render;
    GLFWwindow* window = render.glfwSetup(1500, 900, "Mini Golf Level Editor");
    if (!window) return 1;
    render.imguiSetup(window);

    Editor ed;
    if (!ed.renderer.init()) return 1;  // sphere2.obj missing: run from the src folder
    ed.loadForEditing(0);

    float lastFrame = (float)glfwGetTime();
    bool wasEditing = false;
    Hole beforeEdit;   // the hole as it was when the current edit started
    int beforeIndex = -1;

    while (!glfwWindowShouldClose(window))
    {
        float now = (float)glfwGetTime();
        float dt = now - lastFrame;
        lastFrame = now;
        glfwPollEvents();

        int fbW, fbH;
        glfwGetFramebufferSize(window, &fbW, &fbH);
        if (fbW == 0 || fbH == 0) continue;
        float aspect = (float)fbW / fbH;
        ed.aspect = aspect;

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGuiIO& io = ImGui::GetIO();
        ed.panelFrac = std::min(0.6f, 410.0f / std::max(1.0f, io.DisplaySize.x));

        Hole frameStart = ed.work;   // for undo of panel edits
        int editIndexAtFrameStart = ed.editIndex;
        ed.changed = false;

        // ---------- mouse position in the world (top view) ----------
        float viewHeight = ed.viewWidth / aspect;
        glm::vec2 mouseWorld(
            ed.viewCenter.x + (io.MousePos.x / io.DisplaySize.x - 0.5f) * ed.viewWidth,
            ed.viewCenter.y + (io.MousePos.y / io.DisplaySize.y - 0.5f) * viewHeight);

        // ---------- keyboard shortcuts ----------
        bool typing = io.WantCaptureKeyboard;
        if (!typing) {
            if (ImGui::IsKeyPressed(ImGuiKey_Tab, false)) {
                ed.playing = !ed.playing;
                ed.game.resetHole();
                ed.aimAngle = ed.work.startAim;
                ed.camTarget = ed.game.ballPosition();
            }
            if (!ed.playing && ImGui::IsKeyPressed(ImGuiKey_P, false) && ed.topView && ImGui::IsMousePosValid()) {
                // play from the spot under the mouse
                if (ed.game.onGreen(mouseWorld.x, mouseWorld.y)) {
                    ed.playing = true;
                    ed.game.resetHole();
                    ed.game.placeBall(glm::vec3(mouseWorld.x, ed.game.groundHeight(mouseWorld.x, mouseWorld.y) + ed.game.ballRadius(), mouseWorld.y));
                    ed.aimAngle = ed.work.startAim;
                    ed.camTarget = ed.game.ballPosition();
                }
            }
            if (!ed.playing) {
                if (ImGui::IsKeyPressed(ImGuiKey_V, false)) ed.topView = !ed.topView;
                if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) { ed.pushUndo(ed.work); ed.deleteSelected(); ed.changed = true; }
                if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z)) ed.undo();
                if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S)) ed.save();
                // pan with W/A/S/D (not while Ctrl is held for shortcuts)
                if (!io.KeyCtrl) {
                    float pan = ed.viewWidth * 0.6f * dt;
                    if (ImGui::IsKeyDown(ImGuiKey_W)) ed.viewCenter.y -= pan;
                    if (ImGui::IsKeyDown(ImGuiKey_S)) ed.viewCenter.y += pan;
                    if (ImGui::IsKeyDown(ImGuiKey_A)) ed.viewCenter.x -= pan;
                    if (ImGui::IsKeyDown(ImGuiKey_D)) ed.viewCenter.x += pan;
                }
            }
        }

        // ---------- mouse in the scene ----------
        if (!ed.playing && !io.WantCaptureMouse) {
            if (io.MouseWheel != 0.0f) {
                float zoom = std::pow(0.88f, io.MouseWheel);
                if (ed.topView) {
                    // zoom toward the mouse
                    ed.viewCenter = mouseWorld + (ed.viewCenter - mouseWorld) * zoom;
                    ed.viewWidth = std::clamp(ed.viewWidth * zoom, 5.0f, 2000.0f);
                } else {
                    ed.orbitDist = std::clamp(ed.orbitDist * zoom, 3.0f, 1500.0f);
                }
            }
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f)) {
                ImVec2 d = io.MouseDelta;
                if (ed.topView) {
                    ed.viewCenter.x -= d.x / io.DisplaySize.x * ed.viewWidth;
                    ed.viewCenter.y -= d.y / io.DisplaySize.y * viewHeight;
                } else {
                    ed.orbitYaw -= d.x * 0.006f;
                    ed.orbitPitch = std::clamp(ed.orbitPitch + d.y * 0.006f, 0.08f, 1.55f);
                }
            }
            if (ed.topView && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                // pick the closest handle under the mouse
                float pickRadius = ed.viewWidth * 0.015f;
                float best = pickRadius;
                Selection hit;
                glm::vec2 hitPos{0.0f};
                for (const Handle& h : ed.handles()) {
                    float d = glm::length(h.pos - mouseWorld) / h.scale;
                    if (d < best) { best = d; hit = h.sel; hitPos = h.pos; }
                }
                ed.selected = hit;
                if (hit.kind != Kind::None) {
                    ed.dragging = true;
                    ed.dragOffset = hitPos - mouseWorld;
                }
            }
        }
        if (ed.dragging) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                glm::vec2 target = ed.snapPoint(mouseWorld + ed.dragOffset);
                ed.moveHandle(ed.selected, target);
            } else {
                ed.dragging = false;
                ed.tidySelection();
                ed.changed = true;
            }
        }

        // ---------- play test input ----------
        if (ed.playing && !typing) {
            bool fine = ImGui::IsKeyDown(ImGuiKey_LeftShift);
            float turn = (fine ? 0.3f : 1.5f) * dt;
            if (ImGui::IsKeyDown(ImGuiKey_A) || ImGui::IsKeyDown(ImGuiKey_LeftArrow))  ed.aimAngle -= turn;
            if (ImGui::IsKeyDown(ImGuiKey_D) || ImGui::IsKeyDown(ImGuiKey_RightArrow)) ed.aimAngle += turn;
            if (ImGui::IsKeyPressed(ImGuiKey_R, false)) { ed.game.resetHole(); ed.aimAngle = ed.work.startAim; }
            bool spaceDown = ImGui::IsKeyDown(ImGuiKey_Space);
            if (ed.game.state() == BallState::Ready) {
                if (spaceDown) {
                    if (!ed.charging) { ed.charging = true; ed.power = 0.0f; ed.powerDir = 1.0f; }
                    ed.power += ed.powerDir * 0.8f * dt;
                    if (ed.power >= 1.0f) { ed.power = 1.0f; ed.powerDir = -1.0f; }
                    if (ed.power <= 0.0f) { ed.power = 0.0f; ed.powerDir = 1.0f; }
                } else if (ed.charging) {
                    ed.charging = false;
                    if (ed.power > 0.02f) ed.game.hit(ed.aimAngle, ed.power);
                }
            } else {
                ed.charging = false;
            }
            ed.game.update(dt);
        }

        // ---------- editor panel ----------
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(390, 860), ImGuiCond_FirstUseEver);
        ImGui::Begin("Level Editor");

        // which hole
        const auto& holes = ed.game.allHoles();
        std::string current = std::to_string(ed.editIndex + 1) + ": " + holes[ed.editIndex].name;
        if (ImGui::BeginCombo("Hole", current.c_str())) {
            for (int i = 0; i < (int)holes.size(); ++i) {
                std::string label = std::to_string(i + 1) + ": " + holes[i].name;
                if (ImGui::Selectable(label.c_str(), i == ed.editIndex)) ed.loadForEditing(i);
            }
            ImGui::EndCombo();
        }
        if (ImGui::Button("New hole")) {
            int i = ed.game.addHole(newHoleTemplate());
            ed.loadForEditing(i);
            ed.unsaved = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Duplicate")) {
            Hole copy = ed.work;
            copy.name = std::string(ed.nameBuf) + " v2";
            int i = ed.game.addHole(copy);
            ed.loadForEditing(i);
            ed.unsaved = true;
        }
        ImGui::SameLine();
        if (ImGui::Button(ed.unsaved ? "Save*  (Ctrl+S)" : "Save  (Ctrl+S)")) ed.save();
        if (ed.statusTime > 0.0f) {
            ImGui::TextWrapped("%s", ed.status.c_str());
            ed.statusTime -= dt;
        }
        ImGui::Separator();

        // view / mode
        if (ImGui::Button(ed.playing ? "Back to editing (Tab)" : "Play test (Tab)")) {
            ed.playing = !ed.playing;
            ed.game.resetHole();
            ed.aimAngle = ed.work.startAim;
            ed.camTarget = ed.game.ballPosition();
        }
        if (!ed.playing) {
            ImGui::SameLine();
            if (ImGui::Button(ed.topView ? "3D view" : "Top view")) ed.topView = !ed.topView;
            ImGui::SameLine();
            if (ImGui::Button("Fit")) ed.fitView();
            ImGui::Checkbox("Snap to grid", &ed.snap);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80);
            ImGui::DragFloat("grid", &ed.gridSize, 0.05f, 0.25f, 10.0f, "%.2f");
            if (ed.topView && !io.WantCaptureMouse && ImGui::IsMousePosValid())
                ImGui::Text("Mouse: x %.1f  z %.1f  height %.2f", mouseWorld.x, mouseWorld.y,
                            ed.game.groundHeight(mouseWorld.x, mouseWorld.y));
            else
                ImGui::TextDisabled("Mouse: -");
        } else {
            ImGui::Text("Strokes: %d   %s", ed.game.strokes(),
                        ed.game.state() == BallState::Sunk ? "In the cup!" :
                        ed.game.state() == BallState::Rolling ? "Rolling..." : "A/D aim, hold Space");
            ImGui::ProgressBar(ed.power, ImVec2(-1, 0), "Power");
            ImGui::TextDisabled("R = restart, Tab = back to editing");
        }
        ImGui::Separator();

        if (!ed.playing) {
            Hole& w = ed.work;
            // ----- hole settings -----
            if (ImGui::CollapsingHeader("Hole settings", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ImGui::InputText("Name", ed.nameBuf, sizeof(ed.nameBuf))) { w.name = ed.nameBuf; ed.changed = true; }
                ed.changed |= ImGui::InputInt("Par", &w.par);
                w.par = std::max(1, w.par);
                float aimDeg = glm::degrees(w.startAim);
                if (ImGui::SliderFloat("Start aim", &aimDeg, -180.0f, 180.0f, "%.0f deg")) { w.startAim = glm::radians(aimDeg); ed.changed = true; }
                ed.changed |= ImGui::DragFloat("Cup size", &w.cupRadius, 0.01f, 0.5f, 3.0f);
                if (w.path.size() >= 2) {
                    ed.changed |= ImGui::DragFloat("Path width", &w.pathWidth, 0.25f, 4.0f, 60.0f);
                    ImGui::TextDisabled("(edges of the green are walls automatically)");
                } else {
                    ed.changed |= ImGui::Checkbox("Edges of the green are walls", &w.autoWalls);
                }
                if (w.height) ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1), "This hole also has terrain written as code;\nthe editor can't change or save that part.");
            }

            // ----- bot -----
            if (ImGui::CollapsingHeader("Bot test", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ImGui::Button("Let the bot play it")) {
                    ed.game.replaceHole(ed.editIndex, ed.work);
                    ed.bot = runBot(ed.game, ed.editIndex);
                    ed.botValid = true;
                }
                if (ed.botValid) {
                    ImGui::SameLine();
                    if (ed.bot.sunk) {
                        ImGui::Text("Sunk in %d stroke%s", ed.bot.strokes, ed.bot.strokes == 1 ? "" : "s");
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Use as par")) { w.par = ed.bot.strokes; ed.changed = true; }
                    } else {
                        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Gave up after %d strokes", ed.bot.strokes);
                    }
                    ImGui::TextDisabled("Cyan dots = where each bot shot stopped");
                }
            }

            // ----- items -----
            auto listItem = [&](Kind kind, int index, const std::string& label) {
                bool isSel = ed.selected.kind == kind && ed.selected.index == index;
                if (ImGui::Selectable(label.c_str(), isSel)) ed.selected = Selection{kind, index, 0};
            };
            if (ImGui::CollapsingHeader("Course pieces", ImGuiTreeNodeFlags_DefaultOpen)) {
                glm::vec2 c = ed.snapPoint(ed.visibleCenter());
                listItem(Kind::Start, 0, "Start (white)");
                listItem(Kind::Cup, 0, "Cup (black)");

                ImGui::SeparatorText("Path (yellow)");
                for (int i = 0; i < (int)w.path.size(); ++i)
                    listItem(Kind::PathPoint, i, "Point " + std::to_string(i + 1) + "##p" + std::to_string(i));
                if (ImGui::SmallButton("+ point at end")) {
                    glm::vec2 p = w.path.empty() ? c : w.path.back() + glm::vec2(0, -20);
                    if (w.path.empty()) w.path.push_back(c + glm::vec2(0, 10));
                    w.path.push_back(p);
                    ed.selected = Selection{Kind::PathPoint, (int)w.path.size() - 1, 0};
                    ed.changed = true;
                }

                ImGui::SeparatorText("Green rectangles (green)");
                for (int i = 0; i < (int)w.rects.size(); ++i)
                    listItem(Kind::Rect, i, "Rectangle " + std::to_string(i + 1) + "##r" + std::to_string(i));
                if (ImGui::SmallButton("+ rectangle")) {
                    w.rects.push_back(Rect{ c - glm::vec2(6, 10), c + glm::vec2(6, 10) });
                    ed.selected = Selection{Kind::Rect, (int)w.rects.size() - 1, 0};
                    ed.changed = true;
                }

                ImGui::SeparatorText("Walls and blocks (brown)");
                for (int i = 0; i < (int)w.walls.size(); ++i)
                    listItem(Kind::Wall, i, "Wall " + std::to_string(i + 1) + "##w" + std::to_string(i));
                if (ImGui::SmallButton("+ wall")) {
                    w.walls.push_back(Box{ glm::vec3(c.x - 2, 0, c.y - 1), glm::vec3(c.x + 2, 1, c.y + 1) });
                    ed.selected = Selection{Kind::Wall, (int)w.walls.size() - 1, 0};
                    ed.changed = true;
                }

                ImGui::SeparatorText("Bumpers (red)");
                for (int i = 0; i < (int)w.bumpers.size(); ++i)
                    listItem(Kind::Bumper, i, "Bumper " + std::to_string(i + 1) + "##b" + std::to_string(i));
                if (ImGui::SmallButton("+ bumper")) {
                    w.bumpers.push_back(Bumper{ glm::vec3(c.x, 0, c.y), 1.5f });
                    ed.selected = Selection{Kind::Bumper, (int)w.bumpers.size() - 1, 0};
                    ed.changed = true;
                }

                ImGui::SeparatorText("Terrain stamps (blue)");
                for (int i = 0; i < (int)w.stamps.size(); ++i)
                    listItem(Kind::Stamp, i, std::to_string(i + 1) + ". " + stampNames[w.stamps[i].type] + "##s" + std::to_string(i));
                if (ImGui::SmallButton("+ terrain stamp")) ImGui::OpenPopup("addStamp");
                if (ImGui::BeginPopup("addStamp")) {
                    Stamp s;
                    bool add = false;
                    if (ImGui::Selectable("Bump / bowl")) { s = Stamp::bump(c.x, c.y, 6, 2); add = true; }
                    if (ImGui::Selectable("Ramp"))        { s = Stamp::ramp('z', c.y + 10, c.y - 10, 3); add = true; }
                    if (ImGui::Selectable("Rollers"))     { s = Stamp::rollers('z', 1, 20).limit(c.x - 20, c.y - 30, c.x + 20, c.y + 30, 6); add = true; }
                    if (ImGui::Selectable("Tilt"))        { s = Stamp::tilt(0.1f, 0); add = true; }
                    if (ImGui::Selectable("Halfpipe"))    { s = Stamp::pipe('z', c.x, 0.05f); add = true; }
                    if (add) {
                        w.stamps.push_back(s);
                        ed.selected = Selection{Kind::Stamp, (int)w.stamps.size() - 1, 0};
                        ed.changed = true;
                    }
                    ImGui::EndPopup();
                }
            }

            // ----- selected item -----
            if (ImGui::CollapsingHeader("Selected", ImGuiTreeNodeFlags_DefaultOpen)) {
                Selection& s = ed.selected;
                int i = s.index;
                auto drag2 = [&](const char* label, glm::vec2& v) { ed.changed |= ImGui::DragFloat2(label, &v.x, 0.1f); };
                switch (s.kind) {
                case Kind::None:
                    ImGui::TextDisabled("Click a handle in the top view,\nor pick a piece from the list above.");
                    break;
                case Kind::Start: {
                    glm::vec2 p(w.start.x, w.start.z); drag2("Start x, z", p); w.start.x = p.x; w.start.z = p.y;
                    break;
                }
                case Kind::Cup: {
                    glm::vec2 p(w.cup.x, w.cup.z); drag2("Cup x, z", p); w.cup.x = p.x; w.cup.z = p.y;
                    ImGui::TextDisabled("Tip: put the cup in a small bowl stamp\nso it sits flat and funnels the ball in.");
                    break;
                }
                case Kind::PathPoint:
                    drag2("Point x, z", w.path[i]);
                    if (ImGui::Button("Insert point after")) {
                        glm::vec2 next = (i + 1 < (int)w.path.size()) ? w.path[i + 1] : w.path[i] + glm::vec2(0, -20);
                        w.path.insert(w.path.begin() + i + 1, ed.snapPoint((w.path[i] + next) * 0.5f));
                        s.index = i + 1;
                        ed.changed = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Delete point")) ed.deleteSelected();
                    ImGui::TextDisabled("Legs that aren't straight become an L:\nalong x first, then along z.");
                    break;
                case Kind::Rect:
                    drag2("Min x, z", w.rects[i].min);
                    drag2("Max x, z", w.rects[i].max);
                    if (ImGui::Button("Delete rectangle")) ed.deleteSelected();
                    break;
                case Kind::Wall: {
                    Box& b = w.walls[i];
                    glm::vec2 mn(b.min.x, b.min.z), mx(b.max.x, b.max.z);
                    drag2("Min x, z", mn);
                    drag2("Max x, z", mx);
                    b.min.x = mn.x; b.min.z = mn.y; b.max.x = mx.x; b.max.z = mx.y;
                    ed.changed |= ImGui::DragFloat("Height", &b.max.y, 0.05f, 0.2f, 10.0f);
                    if (ImGui::Button("Delete wall")) ed.deleteSelected();
                    break;
                }
                case Kind::Bumper: {
                    Bumper& b = w.bumpers[i];
                    glm::vec2 p(b.center.x, b.center.z); drag2("Position x, z", p); b.center.x = p.x; b.center.z = p.y;
                    ed.changed |= ImGui::DragFloat("Radius", &b.radius, 0.05f, 0.3f, 10.0f);
                    if (ImGui::Button("Delete bumper")) ed.deleteSelected();
                    break;
                }
                case Kind::Stamp: {
                    Stamp& st = w.stamps[i];
                    int type = st.type;
                    if (ImGui::Combo("Type", &type, stampNames, 5)) { st.type = (Stamp::Type)type; ed.changed = true; }
                    switch (st.type) {
                    case Stamp::Bump:
                        drag2("Center x, z", st.center);
                        ed.changed |= ImGui::DragFloat("Radius", &st.radius, 0.1f, 0.5f, 200.0f);
                        ed.changed |= ImGui::DragFloat("Height", &st.height, 0.05f, -50.0f, 50.0f);
                        ImGui::TextDisabled("Negative height = bowl");
                        break;
                    case Stamp::Ramp:
                        ed.changed |= axisPicker(st.axis);
                        ed.changed |= ImGui::DragFloat("Starts at", &st.from, 0.25f);
                        ed.changed |= ImGui::DragFloat("Ends at", &st.to, 0.25f);
                        ed.changed |= ImGui::DragFloat("Rise", &st.height, 0.05f, -100.0f, 100.0f);
                        ed.changed |= ImGui::Checkbox("Launch ramp (steepest at the end)", &st.launch);
                        ImGui::TextDisabled("Negative rise = goes downhill.\nThe cyan lines show where it starts/ends.");
                        break;
                    case Stamp::Rollers:
                        ed.changed |= axisPicker(st.axis);
                        ed.changed |= ImGui::DragFloat("Wave height", &st.height, 0.02f, -10.0f, 10.0f);
                        ed.changed |= ImGui::DragFloat("Wavelength", &st.wavelength, 0.1f, 2.0f, 200.0f);
                        break;
                    case Stamp::Tilt:
                        ed.changed |= ImGui::DragFloat("Slope in x", &st.slope.x, 0.002f, -1.0f, 1.0f);
                        ed.changed |= ImGui::DragFloat("Slope in z", &st.slope.y, 0.002f, -1.0f, 1.0f);
                        ImGui::TextDisabled("0.1 = rises 1 unit every 10 units");
                        break;
                    case Stamp::Pipe:
                        ed.changed |= axisPicker(st.axis);
                        ed.changed |= ImGui::DragFloat("Center line", &st.line, 0.1f);
                        ed.changed |= ImGui::DragFloat("Curve", &st.curve, 0.001f, -1.0f, 1.0f, "%.3f");
                        break;
                    }
                    if (ImGui::Checkbox("Only inside an area", &st.limited)) {
                        if (st.limited && st.area.max.x <= st.area.min.x) {
                            glm::vec2 c = (st.type == Stamp::Bump) ? st.center : ed.snapPoint(ed.visibleCenter());
                            st.area = Rect{ c - glm::vec2(15), c + glm::vec2(15) };
                        }
                        ed.changed = true;
                    }
                    if (st.limited) {
                        drag2("Area min x, z", st.area.min);
                        drag2("Area max x, z", st.area.max);
                        ed.changed |= ImGui::DragFloat("Fade width", &st.edge, 0.1f, 0.0f, 50.0f);
                    }
                    if (ImGui::Button("Delete stamp")) ed.deleteSelected();
                    break;
                }
                }
            }

            if (ImGui::CollapsingHeader("C++ code for this hole")) {
                std::string code = holeToCpp(ed.game.allHoles()[ed.editIndex]);
                ImGui::InputTextMultiline("##code", code.data(), code.size() + 1, ImVec2(-1, 300), ImGuiInputTextFlags_ReadOnly);
                if (ImGui::Button("Copy to clipboard")) ImGui::SetClipboardText(code.c_str());
            }
            ImGui::Separator();
            ImGui::TextDisabled("Top view: left-drag handles, right-drag or WASD to pan,\nscroll to zoom. V = top/3D view, right-drag orbits in 3D.\nP = play from the mouse spot. Ctrl+Z = undo,\nDelete = delete selected.");
        }
        ImGui::End();

        // undo: when you grab a widget or a handle, remember the hole as it was;
        // when you let go, keep that as an undo step if anything actually changed
        bool editing = ImGui::IsAnyItemActive() || ed.dragging;
        if (editing && !wasEditing) { beforeEdit = frameStart; beforeIndex = editIndexAtFrameStart; }
        if (!editing && wasEditing && beforeIndex == ed.editIndex) {  // (not if we switched holes)
            ed.work.name = ed.nameBuf;
            if (holeToCpp(beforeEdit) != holeToCpp(ed.work)) ed.pushUndo(beforeEdit);
        }
        wasEditing = editing;

        // push edits into the game so the preview updates
        if (ed.changed) {
            ed.work.name = ed.nameBuf;
            ed.game.replaceHole(ed.editIndex, ed.work);
            ed.unsaved = true;
            ed.botValid = false;
        }

        // ---------- camera ----------
        glm::mat4 view, projection;
        if (ed.playing) {
            glm::vec3 aimDir(std::sin(ed.aimAngle), 0.0f, -std::cos(ed.aimAngle));
            ed.camTarget += (ed.game.ballPosition() - ed.camTarget) * std::min(1.0f, 6.0f * dt);
            glm::vec3 eye = ed.camTarget - aimDir * 9.0f + glm::vec3(0, 4.5f, 0);
            eye.y = std::max(eye.y, ed.game.groundHeight(eye.x, eye.z) + 2.0f);
            glm::vec3 look = ed.camTarget + aimDir * 3.0f;
            look.y = std::max(ed.camTarget.y, ed.game.groundHeight(look.x, look.z));
            view = glm::lookAt(eye, look, glm::vec3(0, 1, 0));
            projection = glm::perspective(glm::radians(60.0f), aspect, 0.1f, 1500.0f);
        } else if (ed.topView) {
            glm::vec3 c(ed.viewCenter.x, 0.0f, ed.viewCenter.y);
            view = glm::lookAt(c + glm::vec3(0, 1000, 0), c, glm::vec3(0, 0, -1));
            projection = glm::ortho(-ed.viewWidth * 0.5f, ed.viewWidth * 0.5f, -viewHeight * 0.5f, viewHeight * 0.5f, 1.0f, 3000.0f);
        } else {
            glm::vec3 target(ed.viewCenter.x, ed.game.groundHeight(ed.viewCenter.x, ed.viewCenter.y), ed.viewCenter.y);
            glm::vec3 offset(std::cos(ed.orbitPitch) * std::sin(ed.orbitYaw), std::sin(ed.orbitPitch),
                             std::cos(ed.orbitPitch) * std::cos(ed.orbitYaw));
            view = glm::lookAt(target + offset * ed.orbitDist, target, glm::vec3(0, 1, 0));
            projection = glm::perspective(glm::radians(55.0f), aspect, 0.5f, 3000.0f);
        }

        // ---------- draw ----------
        glViewport(0, 0, fbW, fbH);
        if (!ed.playing && ed.topView) glClearColor(0.20f, 0.45f, 0.18f, 1.0f);  // grass all around
        else glClearColor(0.55f, 0.78f, 0.95f, 1.0f);                            // sky
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        ed.renderer.draw(ed.game, view, projection, ed.aimAngle, ed.power, ed.playing);

        if (!ed.playing) {
            // overlays are drawn on top of everything
            glDisable(GL_DEPTH_TEST);
            GolfGame& g = ed.game;
            float size = ed.topView ? ed.viewWidth * 0.007f : ed.orbitDist * 0.012f;
            float lineW = ed.topView ? ed.viewWidth * 0.0018f : ed.orbitDist * 0.003f;
            auto Y = [&](glm::vec2 p) { return g.groundHeight(p.x, p.y) + 0.3f; };
            // straight line along x or z (as a thin box)
            auto line = [&](glm::vec2 a, glm::vec2 b, glm::vec4 col) {
                float y = std::max(Y(a), Y(b));
                glm::vec3 lo(std::min(a.x, b.x) - lineW, y, std::min(a.y, b.y) - lineW);
                glm::vec3 hi(std::max(a.x, b.x) + lineW, y + lineW, std::max(a.y, b.y) + lineW);
                ed.renderer.drawBox(lo, hi, col);
            };
            auto outline = [&](glm::vec2 mn, glm::vec2 mx, glm::vec4 col) {
                line(mn, {mx.x, mn.y}, col); line({mx.x, mn.y}, mx, col);
                line(mx, {mn.x, mx.y}, col); line({mn.x, mx.y}, mn, col);
            };
            const Hole& w = ed.work;
            // path center line (diagonal legs drawn as the L they turn into)
            for (size_t k = 0; k + 1 < w.path.size(); ++k) {
                glm::vec2 a = w.path[k], b = w.path[k + 1], corner(b.x, a.y);
                line(a, corner, {1.0f, 0.85f, 0.1f, 1}); line(corner, b, {1.0f, 0.85f, 0.1f, 1});
            }
            // selected piece outline
            glm::vec4 hl(1.0f, 1.0f, 0.2f, 1.0f);
            const Selection& s = ed.selected;
            if (s.kind == Kind::Rect) outline(w.rects[s.index].min, w.rects[s.index].max, hl);
            if (s.kind == Kind::Wall) outline({w.walls[s.index].min.x, w.walls[s.index].min.z}, {w.walls[s.index].max.x, w.walls[s.index].max.z}, hl);
            if (s.kind == Kind::Stamp) {
                const Stamp& st = w.stamps[s.index];
                glm::vec4 cy(0.3f, 0.9f, 1.0f, 1.0f);
                if (st.limited) outline(st.area.min, st.area.max, cy);
                if (st.type == Stamp::Ramp) {
                    // lines across the course where the ramp starts and ends
                    glm::vec2 lo(1e9f), hi(-1e9f);
                    for (const Rect& r : g.hole().greens) { lo = glm::min(lo, r.min); hi = glm::max(hi, r.max); }
                    for (float v : { st.from, st.to }) {
                        if (st.axis == 'z') line({lo.x - 3, v}, {hi.x + 3, v}, cy);
                        else                line({v, lo.y - 3}, {v, hi.y + 3}, cy);
                    }
                }
                if (st.type == Stamp::Bump) {
                    // ring of 16 dots at the bump's radius
                    for (int k = 0; k < 16; ++k) {
                        float a = k * 6.2831853f / 16.0f;
                        glm::vec2 p = st.center + glm::vec2(std::cos(a), std::sin(a)) * st.radius;
                        ed.renderer.drawSphere(glm::vec3(p.x, Y(p), p.y), glm::vec3(size * 0.35f), cy);
                    }
                }
            }
            // bot results
            if (ed.botValid)
                for (const glm::vec3& p : ed.bot.restPoints)
                    ed.renderer.drawSphere(p + glm::vec3(0, 0.5f, 0), glm::vec3(size * 0.6f), {0.2f, 1.0f, 1.0f, 1});
            // handles
            for (const Handle& h : ed.handles()) {
                bool isSel = h.sel == ed.selected;
                float r = size * h.scale * (isSel ? 1.5f : 1.0f);
                glm::vec4 col = isSel ? glm::vec4(1, 1, 0.2f, 1) : h.color;
                ed.renderer.drawSphere(glm::vec3(h.pos.x, Y(h.pos) + 0.2f, h.pos.y), glm::vec3(r), col);
            }
            glEnable(GL_DEPTH_TEST);
        }

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    ed.renderer.cleanup();
    render.imguiCleanup();
    glfwTerminate();
    return 0;
}
