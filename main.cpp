#include "editor.h"
#include <raylib.h>
#include <string>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

// ── Build with:
//    mkdir build && cd build
//    cmake .. && cmake --build .
//
//    Dependencies: raylib (link with -lraylib -lGL -lm -lpthread -ldl -lrt -lX11)
// ────────────────────────────────────────────────────────────────────────────

namespace fs = std::filesystem;
using namespace sin;

// ── Layout constants ──────────────────────────────────────────────────────────
static constexpr int   WIN_W        = 1280;
static constexpr int   WIN_H        = 800;
static constexpr int   HEADER_H     = 44;
static constexpr int   TAB_BAR_H    = 36;
static constexpr int   GUTTER_W     = 56;
static constexpr int   FONT_SIZE    = 16;
static constexpr int   LINE_H       = 22;
static constexpr float TAB_W        = 140.0f;
static constexpr int   MENU_W       = 200;
static constexpr int   MENU_ITEM_H  = 38;

// ── Forward declarations ──────────────────────────────────────────────────────
static void draw_header(bool& menu_open, TabManager& tabs);
static void draw_tab_bar(TabManager& tabs, float scroll_offset);
static void draw_editor(TabManager& tabs, Font& mono, Highlighter& hl);
static void draw_hamburger_menu(bool& open, TabManager& tabs);
static void handle_keyboard(TabManager& tabs);
static void handle_mouse(TabManager& tabs);
static void new_file(TabManager& tabs);
static bool save_file(Document* doc);
static bool load_file(TabManager& tabs, const fs::path& p);

// ── Global UI state ───────────────────────────────────────────────────────────
static bool   g_menu_open      = false;
static float  g_tab_scroll     = 0.0f;
static bool   g_run_flash      = false;
static float  g_run_flash_t    = 0.0f;
static char   g_status_msg[256] = "SIN Editor — Ready";

// ── Utilities ─────────────────────────────────────────────────────────────────

static Color blend(Color a, Color b, float t) {
    return {
        (uint8_t)(a.r + (b.r - a.r) * t),
        (uint8_t)(a.g + (b.g - a.g) * t),
        (uint8_t)(a.b + (b.b - a.b) * t),
        (uint8_t)(a.a + (b.a - a.a) * t),
    };
}

static void draw_rounded_rect(Rectangle r, float radius, Color c) {
    DrawRectangleRounded(r, radius, 6, c);
}

// ── Process bridge (POSIX) ───────────────────────────────────────────────────

namespace sin {
RunResult run_sino(const fs::path& script_path) {
    RunResult res;
    std::string cmd = "sino \"" + script_path.string() + "\" 2>&1";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) { res.stderr_data = "Failed to launch SINO interpreter"; res.exit_code = -1; return res; }
    char buf[256];
    while (fgets(buf, sizeof(buf), pipe))
        res.stdout_data += buf;
    res.exit_code = pclose(pipe);
    return res;
}
}

// ── File I/O ─────────────────────────────────────────────────────────────────

static bool load_file(TabManager& tabs, const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss; ss << f.rdbuf();
    int idx = tabs.add(p.filename().string());
    tabs.docs[idx]->path   = p;
    tabs.docs[idx]->buffer = PieceTable(ss.str());
    tabs.docs[idx]->is_new = false;
    snprintf(g_status_msg, sizeof(g_status_msg), "Loaded: %s", p.string().c_str());
    return true;
}

static bool save_file(Document* doc) {
    if (!doc) return false;
    if (doc->path.empty()) {
        doc->path = fs::current_path() / (doc->title + ".sino");
    }
    std::ofstream f(doc->path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    auto txt = doc->buffer.text();
    f.write(txt.data(), txt.size());
    doc->buffer.mark_clean();
    doc->is_new = false;
    snprintf(g_status_msg, sizeof(g_status_msg), "Saved: %s", doc->path.string().c_str());
    return true;
}

static void new_file(TabManager& tabs) {
    static int counter = 1;
    tabs.add("untitled-" + std::to_string(counter++));
}

// ── Drawing: Header ──────────────────────────────────────────────────────────

static void draw_header(bool& menu_open, TabManager& tabs) {
    // Background
    DrawRectangle(0, 0, WIN_W, HEADER_H, COLORS.header_bg);
    // Accent line bottom
    DrawRectangle(0, HEADER_H - 1, WIN_W, 1, {0, 200, 160, 60});

    // ── Hamburger icon ────────────────────────────────────────────────────────
    Rectangle ham_rect = {10, 8, 32, 28};
    bool ham_hover = CheckCollisionPointRec(GetMousePosition(), ham_rect);
    if (ham_hover) DrawRectangleRounded(ham_rect, 0.3f, 4, {255,255,255,15});

    float hx = 18, hy = 16;
    for (int i = 0; i < 3; ++i)
        DrawRectangleRec({hx, hy + i * 7.0f, 20, 2}, ham_hover ? COLORS.accent : Color{180,180,180,255});

    if (ham_hover && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
        menu_open = !menu_open;

    // ── Title ─────────────────────────────────────────────────────────────────
    const char* title = "SIN EDITOR";
    int tw = MeasureText(title, 14);
    DrawText(title, (WIN_W - tw) / 2, 15, 14, COLORS.accent);

    // ── RUN button ────────────────────────────────────────────────────────────
    Rectangle run_rect = {(float)WIN_W - 90, 9, 76, 26};
    bool run_hover = CheckCollisionPointRec(GetMousePosition(), run_rect);

    // Flash animation
    if (g_run_flash) {
        g_run_flash_t += GetFrameTime() * 3.0f;
        if (g_run_flash_t >= 1.0f) { g_run_flash = false; g_run_flash_t = 0; }
    }
    Color run_color = {0, 200, 80, 255};
    if (g_run_flash) run_color = blend(run_color, {255,255,100,255}, g_run_flash_t);
    if (run_hover)   run_color = blend(run_color, WHITE, 0.2f);

    DrawRectangleRounded(run_rect, 0.4f, 6, run_color);
    // Subtle glow
    DrawRectangleRoundedLinesEx({run_rect.x-1, run_rect.y-1, run_rect.width+2, run_rect.height+2},
                                0.4f, 6, 1.5f, {0,255,100,40});
    int rw = MeasureText("▶  RUN", 13);
    DrawText("▶  RUN", (int)(run_rect.x + (run_rect.width - rw)/2), (int)(run_rect.y + 6), 13, BLACK);

    if (run_hover && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
        g_run_flash = true;
        Document* doc = tabs.current();
        if (doc) {
            save_file(doc);
            if (!doc->path.empty()) {
                auto result = run_sino(doc->path);
                snprintf(g_status_msg, sizeof(g_status_msg), "Exit %d: %s",
                    result.exit_code,
                    result.stdout_data.empty() ? "(no output)" : result.stdout_data.substr(0,80).c_str());
            }
        }
    }

    // ── Status bar (bottom of window) ─────────────────────────────────────────
    DrawRectangle(0, WIN_H - 22, WIN_W, 22, {10,10,15,255});
    DrawText(g_status_msg, 10, WIN_H - 17, 12, {120, 180, 120, 200});
}

// ── Drawing: Hamburger Menu ───────────────────────────────────────────────────

struct MenuItem { const char* label; const char* icon; };
static const MenuItem MENU_ITEMS[] = {
    {"New File",  "＋"},
    {"Open File", "📂"},
    {"Save",      "💾"},
    {"Rename",    "✎"},
    {"Delete",    "🗑"},
};
static constexpr int MENU_COUNT = 5;

static void draw_hamburger_menu(bool& open, TabManager& tabs) {
    if (!open) return;

    // Dim background
    DrawRectangle(0, 0, WIN_W, WIN_H, {0, 0, 0, 80});

    Rectangle menu_bg = {8, (float)HEADER_H + 4, (float)MENU_W, (float)(MENU_COUNT * MENU_ITEM_H + 12)};
    draw_rounded_rect(menu_bg, 0.08f, {22, 22, 32, 240});
    DrawRectangleRoundedLinesEx(menu_bg, 0.08f, 6, 1.0f, {0, 200, 160, 60});

    Vector2 mouse = GetMousePosition();
    for (int i = 0; i < MENU_COUNT; ++i) {
        Rectangle item_r = {menu_bg.x + 4, menu_bg.y + 6 + i * MENU_ITEM_H, menu_bg.width - 8, (float)MENU_ITEM_H};
        bool hover = CheckCollisionPointRec(mouse, item_r);
        if (hover) DrawRectangleRounded(item_r, 0.2f, 4, {255,255,255, 18});

        DrawText(MENU_ITEMS[i].label, (int)(item_r.x + 38), (int)(item_r.y + 10), 14,
                 hover ? COLORS.accent : Color{210, 210, 210, 255});

        if (hover && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            open = false;
            switch (i) {
                case 0: new_file(tabs); break;
                case 2: save_file(tabs.current()); break;
                case 3: {
                    // Rename: simple — toggle title (real impl: use text input modal)
                    if (auto* d = tabs.current()) { d->title += "_2"; }
                    break;
                }
                case 4: {
                    tabs.close(tabs.active);
                    break;
                }
                default: break;
            }
        }
    }

    // Close on outside click
    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && !CheckCollisionPointRec(mouse, menu_bg))
        open = false;
}

// ── Drawing: Tab Bar ─────────────────────────────────────────────────────────

static void draw_tab_bar(TabManager& tabs, float& scroll) {
    int y = HEADER_H;
    DrawRectangle(0, y, WIN_W, TAB_BAR_H, COLORS.tab_bg);
    DrawRectangle(0, y + TAB_BAR_H - 1, WIN_W, 1, {30, 30, 50, 255});

    if (tabs.docs.empty()) return;

    BeginScissorMode(0, y, WIN_W, TAB_BAR_H);

    float x = 6 - scroll;
    for (int i = 0; i < (int)tabs.docs.size(); ++i) {
        auto& doc = tabs.docs[i];
        bool active = (i == tabs.active);
        bool dirty  = doc->buffer.is_dirty();

        Rectangle tab_r = {x, (float)y + 4, TAB_W - 4, (float)TAB_BAR_H - 8};
        Color bg = active ? COLORS.tab_active : COLORS.tab_bg;
        DrawRectangleRounded(tab_r, 0.25f, 4, bg);
        if (active) {
            // Bottom accent line
            DrawRectangle((int)(tab_r.x + 4), y + TAB_BAR_H - 3, (int)(tab_r.width - 8), 2, COLORS.accent);
        }

        // Title + dirty dot
        std::string label = doc->title;
        if (dirty) label = "● " + label;
        Color tc = active ? WHITE : Color{160,160,160,255};
        int tw = MeasureText(label.c_str(), 13);
        DrawText(label.c_str(),
                 (int)(tab_r.x + (tab_r.width - tw) / 2),
                 (int)(tab_r.y + 9), 13, tc);

        // Close button
        Rectangle close_r = {tab_r.x + tab_r.width - 18, tab_r.y + 7, 14, 14};
        Vector2 mouse = GetMousePosition();
        bool ch = CheckCollisionPointRec(mouse, close_r);
        if (ch) DrawRectangleRounded(close_r, 0.4f, 4, {255,60,60,180});
        DrawText("×", (int)close_r.x + 3, (int)close_r.y, 14, ch ? WHITE : Color{150,150,150,200});

        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            if (ch) {
                tabs.close(i);
            } else if (CheckCollisionPointRec(mouse, tab_r)) {
                tabs.active = i;
            }
        }

        x += TAB_W;
    }
    EndScissorMode();

    // Scroll arrows
    float total_w = tabs.docs.size() * TAB_W;
    if (total_w > WIN_W) {
        if (IsKeyDown(KEY_LEFT_ALT) && IsKeyPressed(KEY_LEFT))  scroll = std::max(0.0f, scroll - TAB_W);
        if (IsKeyDown(KEY_LEFT_ALT) && IsKeyPressed(KEY_RIGHT)) scroll = std::min(total_w - WIN_W, scroll + TAB_W);
    }
}

// ── Drawing: Editor ──────────────────────────────────────────────────────────

static void draw_editor(TabManager& tabs, Font& mono, Highlighter& hl) {
    Document* doc = tabs.current();
    int editor_y  = HEADER_H + TAB_BAR_H;
    int editor_h  = WIN_H - editor_y - 22;  // minus status bar
    int editor_x  = GUTTER_W;
    int editor_w  = WIN_W - GUTTER_W;

    // Background
    DrawRectangle(0, editor_y, WIN_W, editor_h, COLORS.editor_bg);
    DrawRectangle(0, editor_y, GUTTER_W, editor_h, COLORS.gutter_bg);
    // Gutter separator
    DrawRectangle(GUTTER_W - 1, editor_y, 1, editor_h, {30, 30, 45, 255});

    if (!doc) {
        const char* msg = "No file open — use ☰ to create one";
        int mw = MeasureText(msg, 16);
        DrawText(msg, (WIN_W - mw) / 2, editor_y + editor_h / 2, 16, {80, 80, 100, 255});
        return;
    }

    // Scroll with mouse wheel
    float wheel = GetMouseWheelMove();
    if (wheel != 0.0f) doc->scroll_y = std::max(0, doc->scroll_y - (int)(wheel * 3));

    int visible_lines = editor_h / LINE_H;
    int line_count    = (int)doc->buffer.line_count();

    // Clamp scroll
    doc->scroll_y = std::min(doc->scroll_y, std::max(0, line_count - visible_lines));

    // Ensure line cache size
    doc->line_cache.resize(line_count);

    // Get current line for caret
    Point caret_pt = doc->buffer.offset_to_point(doc->caret_pos);
    int   active_line = (int)caret_pt.line;

    BeginScissorMode(0, editor_y, WIN_W, editor_h);

    for (int li = doc->scroll_y; li < std::min(doc->scroll_y + visible_lines + 1, line_count); ++li) {
        int screen_y = editor_y + (li - doc->scroll_y) * LINE_H;

        // Active line highlight
        if (li == active_line) {
            DrawRectangle(0, screen_y, WIN_W, LINE_H, COLORS.active_bg);
            // Horizontal rule
            DrawRectangle(GUTTER_W, screen_y + LINE_H - 1, editor_w, 1, {30, 120, 180, 40});
        }

        // ── Gutter ────────────────────────────────────────────────────────────
        char lnum[12];
        snprintf(lnum, sizeof(lnum), "%4d", li + 1);
        Color gutter_color = (li == active_line) ? COLORS.active_ln : Color{70, 80, 100, 255};
        DrawText(lnum, 6, screen_y + 3, FONT_SIZE - 2, gutter_color);

        // ── Highlight tokenize (cached) ────────────────────────────────────────
        auto& lc = doc->line_cache[li];
        if (lc.dirty) {
            lc.text   = doc->buffer.line(li);
            lc.tokens = hl.tokenize(lc.text);
            lc.dirty  = false;
        }

        // ── Render tokens ────────────────────────────────────────────────────
        int draw_x = editor_x + 6 - doc->scroll_x;
        for (const auto& tok : lc.tokens) {
            std::string_view sv(lc.text.data() + tok.start, tok.length);
            Color tc = hl.color_for(tok.type);
            // Using built-in font for now (replace with mono font when loaded)
            DrawText(std::string(sv).c_str(), draw_x + (int)(tok.start * 9), screen_y + 3,
                     FONT_SIZE, tc);
        }
        // Fallback: raw line if no tokens
        if (lc.tokens.empty()) {
            DrawText(lc.text.c_str(), draw_x, screen_y + 3, FONT_SIZE, COLORS.normal);
        }
    }

    // ── Caret ─────────────────────────────────────────────────────────────────
    static float blink = 0.0f;
    blink += GetFrameTime() * 2.0f;
    if ((int)blink % 2 == 0) {
        int cx = editor_x + 6 + (int)(caret_pt.col * 9) - doc->scroll_x;
        int cy = editor_y + (active_line - doc->scroll_y) * LINE_H;
        DrawRectangle(cx, cy + 2, 2, LINE_H - 4, COLORS.accent);
    }

    EndScissorMode();
}

// ── Keyboard input ────────────────────────────────────────────────────────────

static void handle_keyboard(TabManager& tabs) {
    Document* doc = tabs.current();
    if (!doc) return;

    // Ctrl shortcuts
    bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);

    if (ctrl && IsKeyPressed(KEY_S)) { save_file(doc); return; }
    if (ctrl && IsKeyPressed(KEY_Z)) { doc->buffer.undo(); doc->invalidate_cache_from(0); return; }
    if (ctrl && IsKeyPressed(KEY_Y)) { doc->buffer.redo(); doc->invalidate_cache_from(0); return; }
    if (ctrl && IsKeyPressed(KEY_N)) { new_file(tabs); return; }

    // Arrow keys
    Point pt = doc->buffer.offset_to_point(doc->caret_pos);
    if (IsKeyPressed(KEY_LEFT)  && doc->caret_pos > 0)                   --doc->caret_pos;
    if (IsKeyPressed(KEY_RIGHT) && doc->caret_pos < doc->buffer.char_count()) ++doc->caret_pos;
    if (IsKeyPressed(KEY_UP)    && pt.line > 0) {
        pt.line--;
        doc->caret_pos = doc->buffer.point_to_offset(pt);
    }
    if (IsKeyPressed(KEY_DOWN)) {
        pt.line++;
        doc->caret_pos = std::min(doc->buffer.point_to_offset(pt), doc->buffer.char_count());
    }
    if (IsKeyPressed(KEY_HOME)) doc->caret_pos = doc->buffer.point_to_offset({pt.line, 0});
    if (IsKeyPressed(KEY_END)) {
        std::string line_text = doc->buffer.line(pt.line);
        doc->caret_pos = doc->buffer.point_to_offset({pt.line, line_text.size()});
    }

    // Backspace / Delete
    if (IsKeyPressed(KEY_BACKSPACE) && doc->caret_pos > 0) {
        doc->buffer.erase(doc->caret_pos - 1, 1);
        --doc->caret_pos;
        Point p2 = doc->buffer.offset_to_point(doc->caret_pos);
        doc->invalidate_cache_from(p2.line);
    }
    if (IsKeyPressed(KEY_DELETE) && doc->caret_pos < doc->buffer.char_count()) {
        doc->buffer.erase(doc->caret_pos, 1);
        Point p2 = doc->buffer.offset_to_point(doc->caret_pos);
        doc->invalidate_cache_from(p2.line);
    }

    // Enter
    if (IsKeyPressed(KEY_ENTER)) {
        doc->buffer.insert(doc->caret_pos, "\n");
        ++doc->caret_pos;
        Point p2 = doc->buffer.offset_to_point(doc->caret_pos);
        doc->invalidate_cache_from(p2.line > 0 ? p2.line - 1 : 0);
    }

    // Tab → 4 spaces
    if (IsKeyPressed(KEY_TAB)) {
        doc->buffer.insert(doc->caret_pos, "    ");
        doc->caret_pos += 4;
        Point p2 = doc->buffer.offset_to_point(doc->caret_pos);
        doc->invalidate_cache_from(p2.line);
    }

    // Printable characters
    int ch;
    while ((ch = GetCharPressed()) != 0) {
        if (ch >= 32) {
            char buf[4] = {(char)ch};
            doc->buffer.insert(doc->caret_pos, std::string_view(buf, 1));
            ++doc->caret_pos;
            Point p2 = doc->buffer.offset_to_point(doc->caret_pos);
            doc->invalidate_cache_from(p2.line);
        }
    }

    // Auto-scroll to caret
    Point caret_pt = doc->buffer.offset_to_point(doc->caret_pos);
    int editor_h = WIN_H - HEADER_H - TAB_BAR_H - 22;
    int visible  = editor_h / LINE_H;
    if ((int)caret_pt.line < doc->scroll_y)
        doc->scroll_y = (int)caret_pt.line;
    if ((int)caret_pt.line >= doc->scroll_y + visible)
        doc->scroll_y = (int)caret_pt.line - visible + 1;
}

// ── Main ─────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
    InitWindow(WIN_W, WIN_H, "SIN Editor — SINO IDE");
    SetTargetFPS(60);

    TabManager tabs;
    Highlighter hl;
    Font mono = GetFontDefault();  // Replace with: LoadFontEx("JetBrainsMono.ttf", FONT_SIZE, nullptr, 0)

    // Load file from arg or create default
    if (argc > 1) {
        load_file(tabs, argv[1]);
    } else {
        new_file(tabs);
        if (auto* d = tabs.current()) {
            d->buffer.insert(0,
                "// Welcome to SIN Editor\n"
                "// SINO Language Prototype\n\n"
                "fn main() {\n"
                "    let msg = \"Hello from SINO!\"\n"
                "    println(msg)\n"
                "}\n"
            );
            d->buffer.mark_clean();
        }
    }

    while (!WindowShouldClose()) {
        handle_keyboard(tabs);

        BeginDrawing();
        ClearBackground(COLORS.editor_bg);

        draw_editor(tabs, mono, hl);
        draw_tab_bar(tabs, g_tab_scroll);
        draw_header(g_menu_open, tabs);
        draw_hamburger_menu(g_menu_open, tabs);

        EndDrawing();
    }

    CloseWindow();
    return 0;
}
