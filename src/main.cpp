//  SIN Editor — main.cpp
//  Supports: Desktop (Linux/Windows/macOS) + Android (via android_main)
//  Raylib 5.0 | C++20
//
//  Build — Desktop:
//    mkdir build && cd build
//    cmake .. -DCMAKE_BUILD_TYPE=Release
//    cmake --build . --parallel
//
//  Build — Android (handled by GitHub Actions via Gradle + CMake toolchain)

#include "editor.h"
#include "highlighter.h"

#include <raylib.h>
#include <raymath.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

// ── Android-specific includes ─────────────────────────────────────────────────
#if defined(PLATFORM_ANDROID)
#include <android/asset_manager.h>
#include <android/log.h>
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "SINEditor", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "SINEditor", __VA_ARGS__)
#else
#define LOGI(...) (void)0
#define LOGE(...) (void)0
#endif

namespace fs = std::filesystem;
using namespace sin;

// ─────────────────────────────────────────────────────────────────────────────
//  Layout  (recalculated each frame if window is resized)
// ─────────────────────────────────────────────────────────────────────────────
struct Layout {
    int win_w, win_h;
    int header_h  = 48;
    int tab_bar_h = 38;
    int gutter_w  = 60;
    int font_sz   = 16;
    int line_h    = 22;
    int status_h  = 24;
    float tab_w   = 150.0f;
    int menu_w    = 210;
    int menu_item_h = 40;

    int editor_y()  const { return header_h + tab_bar_h; }
    int editor_h()  const { return win_h - editor_y() - status_h; }
    int editor_x()  const { return gutter_w; }
    int editor_w()  const { return win_w - gutter_w; }
    int visible_lines() const { return editor_h() / line_h; }
};

// ─────────────────────────────────────────────────────────────────────────────
//  Global application state (POD-safe, zero-initialised)
// ─────────────────────────────────────────────────────────────────────────────
struct AppState {
    TabManager  tabs;
    Highlighter hl;
    Layout      lay;

    bool   menu_open      = false;
    float  tab_scroll     = 0.0f;
    float  run_flash_t    = 0.0f;
    bool   run_flashing   = false;
    float  caret_blink    = 0.0f;

    char   status[256]    = "SIN Editor  —  Ready";

    // Touch emulation state (Android)
    bool   touch_down      = false;
    Vector2 touch_pos      = {0, 0};
    double  touch_down_t   = 0.0;
    bool   simulated_click = false;   // fires one frame after touch-up
};

static AppState* g_app = nullptr;

// ─────────────────────────────────────────────────────────────────────────────
//  Helpers
// ─────────────────────────────────────────────────────────────────────────────
static Color blend_color(Color a, Color b, float t) {
    t = Clamp(t, 0.0f, 1.0f);
    return Color{
        (unsigned char)(a.r + (b.r - a.r) * t),
        (unsigned char)(a.g + (b.g - a.g) * t),
        (unsigned char)(a.b + (b.b - a.b) * t),
        (unsigned char)(a.a + (b.a - a.a) * t),
    };
}

// Unified mouse/touch position
static Vector2 input_pos() {
#if defined(PLATFORM_ANDROID)
    if (GetTouchPointCount() > 0)
        return GetTouchPosition(0);
    return {-1, -1};
#else
    return GetMousePosition();
#endif
}

// Unified "clicked this frame" test
static bool input_clicked(Rectangle r) {
    AppState& app = *g_app;
    Vector2 pos = input_pos();
    if (!CheckCollisionPointRec(pos, r)) return false;

#if defined(PLATFORM_ANDROID)
    // On Android: fire on touch-up (simulated_click set in update_touch)
    return app.simulated_click;
#else
    return IsMouseButtonPressed(MOUSE_LEFT_BUTTON);
#endif
}

// ── Touch → click simulation ──────────────────────────────────────────────────
static void update_touch() {
#if defined(PLATFORM_ANDROID)
    AppState& app = *g_app;
    app.simulated_click = false;
    int tc = GetTouchPointCount();
    if (tc > 0) {
        if (!app.touch_down) {
            app.touch_down   = true;
            app.touch_pos    = GetTouchPosition(0);
            app.touch_down_t = GetTime();
        }
    } else {
        if (app.touch_down) {
            double dt = GetTime() - app.touch_down_t;
            // Tap = quick touch (< 300ms, < 15px movement)
            Vector2 cur = input_pos();
            float dist  = Vector2Distance(app.touch_pos, cur);
            if (dt < 0.3 && (dist < 15.0f || cur.x < 0))
                app.simulated_click = true;
            app.touch_down = false;
        }
    }
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
//  File I/O
// ─────────────────────────────────────────────────────────────────────────────
static bool save_file(Document* doc) {
    if (!doc) return false;
    if (doc->path.empty())
        doc->path = fs::current_path() / (doc->title + ".sino");
    std::ofstream f(doc->path, std::ios::binary | std::ios::trunc);
    if (!f) { LOGE("save_file: cannot open %s", doc->path.string().c_str()); return false; }
    auto txt = doc->buffer.text();
    f.write(txt.data(), (std::streamsize)txt.size());
    doc->buffer.mark_clean();
    doc->is_new = false;
    snprintf(g_app->status, sizeof(g_app->status), "Saved: %s", doc->path.string().c_str());
    LOGI("Saved: %s", doc->path.string().c_str());
    return true;
}

static bool load_file(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss; ss << f.rdbuf();
    auto& tabs = g_app->tabs;
    int idx = tabs.add(p.filename().string());
    tabs.docs[idx]->path   = p;
    tabs.docs[idx]->buffer = PieceTable(ss.str());
    tabs.docs[idx]->is_new = false;
    snprintf(g_app->status, sizeof(g_app->status), "Loaded: %s", p.string().c_str());
    return true;
}

static void new_file() {
    static int ctr = 1;
    g_app->tabs.add("untitled-" + std::to_string(ctr++));
}

static void run_current() {
    AppState& app = *g_app;
    Document* doc = app.tabs.current();
    if (!doc) return;
    save_file(doc);
    if (doc->path.empty()) return;

#if !defined(PLATFORM_ANDROID)
    std::string cmd = "sino \"" + doc->path.string() + "\" 2>&1";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) { snprintf(app.status, sizeof(app.status), "Error: SINO not found in PATH"); return; }
    char buf[512]; std::string out;
    while (fgets(buf, sizeof(buf), pipe)) out += buf;
    int ec = pclose(pipe);
    snprintf(app.status, sizeof(app.status), "Exit %d | %s",
             ec, out.empty() ? "(no output)" : out.substr(0, 100).c_str());
#else
    snprintf(app.status, sizeof(app.status), "Run: not supported in mobile demo");
#endif

    app.run_flashing = true;
    app.run_flash_t  = 0.0f;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Keyboard input  (desktop only — Android uses on-screen keyboard via Raylib)
// ─────────────────────────────────────────────────────────────────────────────
static void process_keyboard() {
    AppState& app = *g_app;
    Document* doc = app.tabs.current();
    if (!doc) return;

    bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);

    if (ctrl && IsKeyPressed(KEY_S)) { save_file(doc); return; }
    if (ctrl && IsKeyPressed(KEY_Z)) { doc->buffer.undo();  doc->invalidate_cache_from(0); return; }
    if (ctrl && IsKeyPressed(KEY_Y)) { doc->buffer.redo();  doc->invalidate_cache_from(0); return; }
    if (ctrl && IsKeyPressed(KEY_N)) { new_file(); return; }
    if (ctrl && IsKeyPressed(KEY_W)) { app.tabs.close(app.tabs.active); return; }

    auto caret_pt = [&]() { return doc->buffer.offset_to_point(doc->caret_pos); };

    if (IsKeyPressed(KEY_LEFT)  && doc->caret_pos > 0)                       --doc->caret_pos;
    if (IsKeyPressed(KEY_RIGHT) && doc->caret_pos < doc->buffer.char_count()) ++doc->caret_pos;
    if (IsKeyPressed(KEY_UP)) {
        auto pt = caret_pt(); if (pt.line > 0) { pt.line--; doc->caret_pos = doc->buffer.point_to_offset(pt); }
    }
    if (IsKeyPressed(KEY_DOWN)) {
        auto pt = caret_pt(); pt.line++;
        doc->caret_pos = std::min(doc->buffer.point_to_offset(pt), doc->buffer.char_count());
    }
    if (IsKeyPressed(KEY_HOME)) {
        auto pt = caret_pt(); pt.col = 0; doc->caret_pos = doc->buffer.point_to_offset(pt);
    }
    if (IsKeyPressed(KEY_END)) {
        auto pt = caret_pt();
        doc->caret_pos = doc->buffer.point_to_offset({pt.line, doc->buffer.line(pt.line).size()});
    }
    if (IsKeyPressed(KEY_PAGE_UP))   doc->scroll_y = std::max(0, doc->scroll_y - app.lay.visible_lines());
    if (IsKeyPressed(KEY_PAGE_DOWN)) doc->scroll_y += app.lay.visible_lines();

    if (IsKeyPressed(KEY_BACKSPACE) && doc->caret_pos > 0) {
        doc->buffer.erase(doc->caret_pos - 1, 1);
        --doc->caret_pos;
        doc->invalidate_cache_from(doc->buffer.offset_to_point(doc->caret_pos).line);
    }
    if (IsKeyPressed(KEY_DELETE) && doc->caret_pos < doc->buffer.char_count()) {
        doc->buffer.erase(doc->caret_pos, 1);
        doc->invalidate_cache_from(doc->buffer.offset_to_point(doc->caret_pos).line);
    }
    if (IsKeyPressed(KEY_ENTER)) {
        doc->buffer.insert(doc->caret_pos, "\n");
        ++doc->caret_pos;
        auto pt = doc->buffer.offset_to_point(doc->caret_pos);
        doc->invalidate_cache_from(pt.line > 0 ? pt.line - 1 : 0);
    }
    if (IsKeyPressed(KEY_TAB)) {
        doc->buffer.insert(doc->caret_pos, "    ");
        doc->caret_pos += 4;
        doc->invalidate_cache_from(doc->buffer.offset_to_point(doc->caret_pos).line);
    }

    int ch;
    while ((ch = GetCharPressed()) != 0) {
        if (ch >= 32) {
            char buf[2] = {(char)ch, 0};
            doc->buffer.insert(doc->caret_pos, std::string_view(buf, 1));
            ++doc->caret_pos;
            doc->invalidate_cache_from(doc->buffer.offset_to_point(doc->caret_pos).line);
        }
    }

    // Auto-scroll to caret
    auto pt = doc->buffer.offset_to_point(doc->caret_pos);
    int vl  = app.lay.visible_lines();
    if ((int)pt.line < doc->scroll_y)          doc->scroll_y = (int)pt.line;
    if ((int)pt.line >= doc->scroll_y + vl)    doc->scroll_y = (int)pt.line - vl + 1;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Draw: Header
// ─────────────────────────────────────────────────────────────────────────────
static void draw_header() {
    AppState& app = *g_app;
    Layout&   lay = app.lay;

    DrawRectangle(0, 0, lay.win_w, lay.header_h, COLORS.header_bg);
    DrawRectangle(0, lay.header_h - 1, lay.win_w, 1, {0, 200, 160, 50});

    // ── Hamburger ─────────────────────────────────────────────────────────────
    Rectangle ham = {10, 8, 36, 32};
    bool hov = CheckCollisionPointRec(input_pos(), ham);
    if (hov) DrawRectangleRounded(ham, 0.3f, 4, {255,255,255,18});
    for (int i = 0; i < 3; ++i)
        DrawRectangleRec({20, 15.0f + i * 7, 20, 2}, hov ? COLORS.accent : Color{170,170,170,255});
    if (input_clicked(ham)) app.menu_open = !app.menu_open;

    // ── Title ─────────────────────────────────────────────────────────────────
    const char* title = "SIN EDITOR";
    int tw = MeasureText(title, 15);
    DrawText(title, (lay.win_w - tw) / 2, 16, 15, COLORS.accent);

    // ── Run button ────────────────────────────────────────────────────────────
    Rectangle run = {(float)lay.win_w - 95, 11, 80, 26};
    if (app.run_flashing) {
        app.run_flash_t += GetFrameTime() * 3.0f;
        if (app.run_flash_t >= 1.0f) { app.run_flashing = false; app.run_flash_t = 0; }
    }
    Color rc = {0, 210, 80, 255};
    if (app.run_flashing) rc = blend_color(rc, {255, 240, 60, 255}, app.run_flash_t);
    bool rhov = CheckCollisionPointRec(input_pos(), run);
    if (rhov) rc = blend_color(rc, WHITE, 0.18f);

    DrawRectangleRounded(run, 0.35f, 6, rc);
    DrawRectangleRoundedLinesEx({run.x - 1, run.y - 1, run.width + 2, run.height + 2},
                                 0.35f, 6, 1.5f, {0, 255, 100, 35});
    int rw = MeasureText("RUN", 13);
    DrawText("RUN", (int)(run.x + (run.width - rw) / 2), (int)(run.y + 6), 13, BLACK);

    if (input_clicked(run)) run_current();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Draw: Hamburger Menu
// ─────────────────────────────────────────────────────────────────────────────
struct MenuItem { const char* label; };
static const MenuItem MENU_ITEMS[] = {
    {"New File"},
    {"Save"},
    {"Close Tab"},
    {"Rename"},
    {"Delete"},
};
static constexpr int MENU_COUNT = 5;

static void draw_menu() {
    AppState& app = *g_app;
    if (!app.menu_open) return;

    Layout& lay = app.lay;
    DrawRectangle(0, 0, lay.win_w, lay.win_h, {0, 0, 0, 90});

    float mh = (float)(MENU_COUNT * lay.menu_item_h + 14);
    Rectangle bg = {8, (float)lay.header_h + 4, (float)lay.menu_w, mh};
    DrawRectangleRounded(bg, 0.07f, 6, {20, 20, 32, 245});
    DrawRectangleRoundedLinesEx(bg, 0.07f, 6, 1.0f, {0, 200, 160, 55});

    Vector2 mp = input_pos();
    for (int i = 0; i < MENU_COUNT; ++i) {
        Rectangle item = {bg.x + 5, bg.y + 7 + i * lay.menu_item_h,
                          bg.width - 10, (float)lay.menu_item_h};
        bool hov = CheckCollisionPointRec(mp, item);
        if (hov) DrawRectangleRounded(item, 0.2f, 4, {255, 255, 255, 16});

        DrawText(MENU_ITEMS[i].label,
                 (int)(item.x + 16), (int)(item.y + 11), 14,
                 hov ? COLORS.accent : Color{208, 208, 208, 255});

        if (input_clicked(item)) {
            app.menu_open = false;
            switch (i) {
                case 0: new_file(); break;
                case 1: save_file(app.tabs.current()); break;
                case 2: app.tabs.close(app.tabs.active); break;
                case 3: { if (auto* d = app.tabs.current()) d->title += "_r"; break; }
                case 4: app.tabs.close(app.tabs.active); break;
            }
        }
    }

    if (input_clicked({0, 0, (float)lay.win_w, (float)lay.win_h}) &&
        !CheckCollisionPointRec(mp, bg))
        app.menu_open = false;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Draw: Tab Bar
// ─────────────────────────────────────────────────────────────────────────────
static void draw_tabs() {
    AppState& app = *g_app;
    Layout&   lay = app.lay;
    int y = lay.header_h;

    DrawRectangle(0, y, lay.win_w, lay.tab_bar_h, COLORS.tab_bg);
    DrawRectangle(0, y + lay.tab_bar_h - 1, lay.win_w, 1, {28, 28, 46, 255});

    if (app.tabs.docs.empty()) return;

    // Mouse-wheel horizontal scroll on tab bar
    Vector2 mp = input_pos();
    if (CheckCollisionPointRec(mp, {0, (float)y, (float)lay.win_w, (float)lay.tab_bar_h})) {
        float w = GetMouseWheelMove();
        if (w != 0) app.tab_scroll = std::max(0.0f, app.tab_scroll - w * lay.tab_w);
    }

    float total_w = (float)app.tabs.docs.size() * lay.tab_w;
    app.tab_scroll = Clamp(app.tab_scroll, 0.0f, std::max(0.0f, total_w - lay.win_w));

    BeginScissorMode(0, y, lay.win_w, lay.tab_bar_h);

    float x = 6 - app.tab_scroll;
    for (int i = 0; i < (int)app.tabs.docs.size(); ++i) {
        auto& doc     = app.tabs.docs[i];
        bool  active  = (i == app.tabs.active);
        bool  dirty   = doc->buffer.is_dirty();

        Rectangle tr = {x, (float)y + 4, lay.tab_w - 4, (float)lay.tab_bar_h - 8};
        DrawRectangleRounded(tr, 0.22f, 4, active ? COLORS.tab_active : COLORS.tab_bg);
        if (active)
            DrawRectangle((int)(tr.x + 4), y + lay.tab_bar_h - 3, (int)(tr.width - 8), 2, COLORS.accent);

        std::string label = (dirty ? "* " : "") + doc->title;
        int tw = MeasureText(label.c_str(), 13);
        // Clamp label
        while (tw > (int)(tr.width - 32) && !label.empty()) { label.pop_back(); tw = MeasureText(label.c_str(), 13); }
        DrawText(label.c_str(),
                 (int)(tr.x + 8), (int)(tr.y + 9), 13,
                 active ? WHITE : Color{155, 155, 155, 255});

        // × close
        Rectangle cr = {tr.x + tr.width - 20, tr.y + 8, 14, 14};
        bool chov = CheckCollisionPointRec(mp, cr);
        if (chov) DrawRectangleRounded(cr, 0.4f, 4, {220, 50, 50, 180});
        DrawText("x", (int)cr.x + 3, (int)cr.y + 1, 12, chov ? WHITE : Color{140, 140, 140, 200});

        if (input_clicked(cr))        { app.tabs.close(i); break; }
        else if (input_clicked(tr))     app.tabs.active = i;

        x += lay.tab_w;
    }

    EndScissorMode();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Draw: Editor
// ─────────────────────────────────────────────────────────────────────────────
static void draw_editor() {
    AppState& app = *g_app;
    Layout&   lay = app.lay;

    int ey = lay.editor_y();
    int eh = lay.editor_h();
    int ex = lay.editor_x();

    DrawRectangle(0,  ey, lay.win_w, eh, COLORS.editor_bg);
    DrawRectangle(0,  ey, lay.gutter_w, eh, COLORS.gutter_bg);
    DrawRectangle(lay.gutter_w - 1, ey, 1, eh, {28, 28, 44, 255});

    Document* doc = app.tabs.current();
    if (!doc) {
        const char* msg = "No file open — tap  to create one";
        int mw = MeasureText(msg, 16);
        DrawText(msg, (lay.win_w - mw) / 2, ey + eh / 2, 16, {75, 75, 100, 255});
        return;
    }

    // Mouse-wheel vertical scroll
    float wheel = GetMouseWheelMove();
    if (wheel != 0.0f) doc->scroll_y = std::max(0, doc->scroll_y - (int)(wheel * 3));

    int line_count = (int)doc->buffer.line_count();
    int vis        = lay.visible_lines();
    doc->scroll_y  = Clamp(doc->scroll_y, 0, std::max(0, line_count - vis));

    // Sync cache size
    doc->line_cache.resize(line_count);

    Point caret_pt  = doc->buffer.offset_to_point(doc->caret_pos);
    int   active_ln = (int)caret_pt.line;

    BeginScissorMode(0, ey, lay.win_w, eh);

    for (int li = doc->scroll_y; li < std::min(doc->scroll_y + vis + 1, line_count); ++li) {
        int sy = ey + (li - doc->scroll_y) * lay.line_h;

        // Active line bg + rule
        if (li == active_ln) {
            DrawRectangle(0, sy, lay.win_w, lay.line_h, COLORS.active_bg);
            DrawRectangle(ex, sy + lay.line_h - 1, lay.editor_w(), 1, {28, 110, 175, 35});
        }

        // Gutter
        char lnum[12];
        snprintf(lnum, sizeof(lnum), "%4d", li + 1);
        DrawText(lnum, 6, sy + 3, lay.font_sz - 2,
                 (li == active_ln) ? COLORS.active_ln : Color{65, 78, 100, 255});

        // Tokenise (lazy, cached)
        auto& lc = doc->line_cache[li];
        if (lc.dirty) {
            lc.text   = doc->buffer.line(li);
            lc.tokens = app.hl.tokenize(lc.text);
            lc.dirty  = false;
        }

        // Render tokens
        int glyph_w = lay.font_sz / 2 + 2;   // monospace approximation (~9px at 16)
        int base_x  = ex + 6 - doc->scroll_x;
        if (lc.tokens.empty()) {
            DrawText(lc.text.c_str(), base_x, sy + 3, lay.font_sz, COLORS.normal);
        } else {
            for (const auto& tok : lc.tokens) {
                std::string word(lc.text.data() + tok.start, tok.length);
                DrawText(word.c_str(),
                         base_x + (int)(tok.start * glyph_w),
                         sy + 3, lay.font_sz, app.hl.color_for(tok.type));
            }
        }
    }

    // Caret blink
    app.caret_blink += GetFrameTime() * 2.0f;
    if ((int)app.caret_blink % 2 == 0) {
        int glyph_w = lay.font_sz / 2 + 2;
        int cx = ex + 6 + (int)(caret_pt.col * glyph_w) - doc->scroll_x;
        int cy = ey + (active_ln - doc->scroll_y) * lay.line_h;
        DrawRectangle(cx, cy + 2, 2, lay.line_h - 4, COLORS.accent);
    }

    EndScissorMode();

    // Status bar
    DrawRectangle(0, lay.win_h - lay.status_h, lay.win_w, lay.status_h, {10, 10, 16, 255});
    DrawText(app.status, 10, lay.win_h - lay.status_h + 5, 12, {110, 175, 110, 200});

    // Line/col indicator right side
    char pos[32];
    snprintf(pos, sizeof(pos), "Ln %zu  Col %zu", caret_pt.line + 1, caret_pt.col + 1);
    int pw = MeasureText(pos, 12);
    DrawText(pos, lay.win_w - pw - 12, lay.win_h - lay.status_h + 5, 12, {100, 140, 180, 200});
}

// ─────────────────────────────────────────────────────────────────────────────
//  App tick (called every frame from both desktop loop and android_main)
// ─────────────────────────────────────────────────────────────────────────────
static void app_tick() {
    AppState& app = *g_app;

    // Update layout to handle window resize
    app.lay.win_w = GetScreenWidth();
    app.lay.win_h = GetScreenHeight();

    // Scale UI for high-DPI / small screens
#if defined(PLATFORM_ANDROID)
    // Make touch targets larger on mobile
    app.lay.header_h    = 60;
    app.lay.tab_bar_h   = 44;
    app.lay.menu_item_h = 52;
    app.lay.gutter_w    = 50;
    app.lay.font_sz     = 18;
    app.lay.line_h      = 26;
    app.lay.tab_w       = 170.0f;
    app.lay.menu_w      = 230;
    app.lay.status_h    = 28;
#endif

    update_touch();
    process_keyboard();

    BeginDrawing();
    ClearBackground(COLORS.editor_bg);

    draw_editor();
    draw_tabs();
    draw_header();
    draw_menu();

    EndDrawing();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Bootstrap
// ─────────────────────────────────────────────────────────────────────────────
static void app_init(const char* initial_file) {
    g_app = new AppState();

    if (initial_file && initial_file[0] != '\0') {
        load_file(initial_file);
    } else {
        new_file();
        if (auto* d = g_app->tabs.current()) {
            const char* welcome =
                "// SIN Editor — SINO Language IDE\n"
                "// Version 0.2  |  github.com/your-repo/sin-editor\n\n"
                "fn greet(name: str) -> str {\n"
                "    return \"Hello, \" + name + \"!\"\n"
                "}\n\n"
                "fn main() {\n"
                "    let msg = greet(\"SINO\")\n"
                "    println(msg)\n"
                "}\n";
            d->buffer.insert(0, welcome);
            d->buffer.mark_clean();
        }
    }

    LOGI("SIN Editor initialised. Screen: %d x %d", GetScreenWidth(), GetScreenHeight());
}

static void app_shutdown() {
    delete g_app;
    g_app = nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Entry point — Desktop
// ─────────────────────────────────────────────────────────────────────────────
#if !defined(PLATFORM_ANDROID)

int main(int argc, char* argv[]) {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(1280, 800, "SIN Editor — SINO IDE");
    SetTargetFPS(60);

    app_init(argc > 1 ? argv[1] : nullptr);

    while (!WindowShouldClose()) {
        app_tick();
    }

    app_shutdown();
    CloseWindow();
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Entry point — Android (Raylib calls android_main instead of main)
// ─────────────────────────────────────────────────────────────────────────────
#else

#include <android_native_app_glue.h>

void android_main(struct android_app* state) {
    (void)state;

    // Raylib handles the ANativeActivity and window internally
    InitWindow(0, 0, "SIN Editor");   // 0,0 = full screen on Android
    SetTargetFPS(60);

    app_init(nullptr);   // No file path on cold start

    while (!WindowShouldClose()) {
        app_tick();
    }

    app_shutdown();
    CloseWindow();
}

#endif  // PLATFORM_ANDROID
