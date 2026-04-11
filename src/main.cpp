#include "editor.h"
#include "highlighter.h"
#include <raylib.h>

#if defined(PLATFORM_ANDROID)
    #include <android/log.h>
    #include <android_native_app_glue.h>
    #define ALOG(...) __android_log_print(ANDROID_LOG_INFO, "SINEditor", __VA_ARGS__)
#else
    #define ALOG(...) printf(__VA_ARGS__)
#endif

namespace ide = sinide;

// ---- دالة التشغيل الرئيسية المعدلة ----
void ExecuteApp() {
    // إعدادات النافذة (0,0 تعني ملء الشاشة بالأندرويد)
    InitWindow(0, 0, "SIN Editor -- SINO IDE");
    SetTargetFPS(60);
    
    ALOG("SINO: Window Initialized Successfully!");

    // كود تجريبي بسيط بدلاً من تحميل ملفات خارجية تسبب كراش
    std::string welcome_text = "// SIN Editor is Alive!\n// Version: 0.2\n\nfn main() {\n    out \"Hello SINO\";\n}";
    
    // ملاحظة: هنا نتأكد من تهيئة الـ Editor بدون لمس الذاكرة الخارجية حالياً
    ide::TabManager tabs;
    tabs.add("NewFile.sino");
    if (tabs.current()) {
        tabs.current()->buffer.insert(0, welcome_text);
    }

    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground({30, 30, 30, 255}); // خلفية سينو الداكنة
        
        DrawText("SINO IDE: READY", 20, 20, 20, MAROON);
        DrawText("If you see this, the core is working!", 20, 60, 10, GRAY);
        
        // رسم الكود التجريبي
        DrawText(welcome_text.c_str(), 20, 100, 15, RAYWHITE);
        
        EndDrawing();
    }

    CloseWindow();
}

#if defined(PLATFORM_ANDROID)
void android_main(struct android_app* state) {
    // أهم سطر لمنع الكراش: ربط حالة الأندرويد بـ Raylib
    SetCallbacks(state); 
    ExecuteApp();
}
#else
int main() {
    ExecuteApp();
    return 0;
}
#endif
// which could conflict with std::sin or other math names
namespace ide = sinide;
// Palette shortcut
static const ide::Palette& C = ide::THEME;

// ---- Layout -----------------------------------------------------------------
struct Layout {
    int   W           = 1280;
    int   H           = 800;
    int   header_h    = 48;
    int   tab_h       = 36;
    int   gutter_w    = 58;
    int   font_sz     = 16;
    int   line_h      = 22;
    int   status_h    = 22;
    float tab_w       = 148.0f;
    int   menu_w      = 205;
    int   menu_row_h  = 40;

    int ed_y() const { return header_h + tab_h; }
    int ed_h() const { return H - ed_y() - status_h; }
    int ed_w() const { return W - gutter_w; }
    int vis()  const { return (ed_h() > 0) ? ed_h() / line_h : 1; }
};

// ---- App state --------------------------------------------------------------
struct App {
    ide::TabManager tabs;
    ide::Highlighter hl;
    Layout lay;

    bool  menu_open    = false;
    float tab_scroll   = 0.0f;
    float run_flash    = 0.0f;
    bool  run_anim     = false;
    float blink        = 0.0f;
    char  status[256]  = "SIN Editor  --  Ready";

    // Touch (Android)
    bool    td         = false;
    Vector2 td_pos     = {};
    double  td_time    = 0.0;
    bool    tapped     = false;
};

static App* g = nullptr;

// ---- Utility ----------------------------------------------------------------
static Color lerp_col(Color a, Color b, float t) {
    t = Clamp(t, 0.0f, 1.0f);
    return {
        (unsigned char)(a.r + (int)((b.r - a.r) * t)),
        (unsigned char)(a.g + (int)((b.g - a.g) * t)),
        (unsigned char)(a.b + (int)((b.b - a.b) * t)),
        (unsigned char)(a.a + (int)((b.a - a.a) * t)),
    };
}

static Vector2 cursor_pos() {
#if defined(PLATFORM_ANDROID)
    return GetTouchPointCount() > 0 ? GetTouchPosition(0) : Vector2{-1,-1};
#else
    return GetMousePosition();
#endif
}

static bool clicked(Rectangle r) {
    if (!CheckCollisionPointRec(cursor_pos(), r)) return false;
#if defined(PLATFORM_ANDROID)
    return g->tapped;
#else
    return IsMouseButtonPressed(MOUSE_LEFT_BUTTON);
#endif
}

// ---- Touch emulation --------------------------------------------------------
static void poll_touch() {
#if defined(PLATFORM_ANDROID)
    g->tapped = false;
    int tc = GetTouchPointCount();
    if (tc > 0) {
        if (!g->td) { g->td = true; g->td_pos = GetTouchPosition(0); g->td_time = GetTime(); }
    } else if (g->td) {
        double  dt   = GetTime() - g->td_time;
        float   dist = Vector2Distance(g->td_pos, GetTouchPosition(0));
        if (dt < 0.3 && dist < 20.0f) g->tapped = true;
        g->td = false;
    }
#endif
}

// ---- File I/O ---------------------------------------------------------------
namespace fs = std::filesystem;

static bool save(ide::Document* doc) {
    if (!doc) return false;
    if (doc->path.empty()) doc->path = fs::current_path() / (doc->title + ".sino");
    std::ofstream f(doc->path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    auto txt = doc->buffer.text();
    f.write(txt.data(), (std::streamsize)txt.size());
    doc->buffer.mark_clean();
    doc->is_new = false;
    snprintf(g->status, sizeof(g->status), "Saved: %s", doc->path.string().c_str());
    ALOG("Saved %s", doc->path.string().c_str());
    return true;
}

static bool load(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss; ss << f.rdbuf();
    int idx = g->tabs.add(p.filename().string());
    g->tabs.docs[idx]->path   = p;
    g->tabs.docs[idx]->buffer = ide::PieceTable(ss.str());
    g->tabs.docs[idx]->is_new = false;
    snprintf(g->status, sizeof(g->status), "Loaded: %s", p.string().c_str());
    return true;
}

static void new_doc() {
    static int n = 1;
    g->tabs.add("untitled-" + std::to_string(n++));
}

static void run_script() {
    auto* doc = g->tabs.current();
    if (!doc) return;
    save(doc);
    g->run_anim = true; g->run_flash = 0.0f;
#if !defined(PLATFORM_ANDROID)
    if (doc->path.empty()) return;
    std::string cmd = "sino \"" + doc->path.string() + "\" 2>&1";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) { snprintf(g->status, sizeof(g->status), "Error: sino not found in PATH"); return; }
    char buf[512]; std::string out;
    while (fgets(buf, sizeof(buf), pipe)) out += buf;
    int ec = pclose(pipe);
    snprintf(g->status, sizeof(g->status), "[Exit %d] %s",
             ec, out.empty() ? "(no output)" : out.substr(0, 90).c_str());
#else
    snprintf(g->status, sizeof(g->status), "Run: not available in mobile build");
#endif
}

// ---- Keyboard ---------------------------------------------------------------
static void keyboard() {
    auto* doc = g->tabs.current();
    if (!doc) return;

    bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    if (ctrl && IsKeyPressed(KEY_S)) { save(doc); return; }
    if (ctrl && IsKeyPressed(KEY_Z)) { doc->buffer.undo(); doc->invalidate_from(0); return; }
    if (ctrl && IsKeyPressed(KEY_Y)) { doc->buffer.redo(); doc->invalidate_from(0); return; }
    if (ctrl && IsKeyPressed(KEY_N)) { new_doc(); return; }
    if (ctrl && IsKeyPressed(KEY_W)) { g->tabs.close(g->tabs.active); return; }

    // Caret helpers
    auto gpt = [&]() { return doc->buffer.offset_to_point(doc->caret_pos); };
    auto spt = [&](ide::Point p) { doc->caret_pos = doc->buffer.point_to_offset(p); };

    if (IsKeyPressed(KEY_LEFT)  && doc->caret_pos > 0)                          --doc->caret_pos;
    if (IsKeyPressed(KEY_RIGHT) && doc->caret_pos < doc->buffer.char_count())    ++doc->caret_pos;
    if (IsKeyPressed(KEY_UP))   { auto p = gpt(); if (p.line > 0) { p.line--; spt(p); } }
    if (IsKeyPressed(KEY_DOWN)) {
        auto p = gpt(); p.line++;
        doc->caret_pos = std::min(doc->buffer.point_to_offset(p), doc->buffer.char_count());
    }
    if (IsKeyPressed(KEY_HOME)) { auto p = gpt(); p.col = 0; spt(p); }
    if (IsKeyPressed(KEY_END))  { auto p = gpt(); p.col = doc->buffer.line(p.line).size(); spt(p); }
    if (IsKeyPressed(KEY_PAGE_UP))   doc->scroll_y = std::max(0, doc->scroll_y - g->lay.vis());
    if (IsKeyPressed(KEY_PAGE_DOWN)) doc->scroll_y += g->lay.vis();

    if (IsKeyPressed(KEY_BACKSPACE) && doc->caret_pos > 0) {
        --doc->caret_pos;
        doc->buffer.erase(doc->caret_pos, 1);
        doc->invalidate_from(doc->buffer.offset_to_point(doc->caret_pos).line);
    }
    if (IsKeyPressed(KEY_DELETE) && doc->caret_pos < doc->buffer.char_count()) {
        doc->buffer.erase(doc->caret_pos, 1);
        doc->invalidate_from(doc->buffer.offset_to_point(doc->caret_pos).line);
    }
    if (IsKeyPressed(KEY_ENTER)) {
        doc->buffer.insert(doc->caret_pos, "\n");
        ++doc->caret_pos;
        auto p = doc->buffer.offset_to_point(doc->caret_pos);
        doc->invalidate_from(p.line > 0 ? p.line - 1 : 0);
    }
    if (IsKeyPressed(KEY_TAB)) {
        doc->buffer.insert(doc->caret_pos, "    ");
        doc->caret_pos += 4;
        doc->invalidate_from(doc->buffer.offset_to_point(doc->caret_pos).line);
    }

    int ch;
    while ((ch = GetCharPressed()) != 0) {
        if (ch >= 32) {
            char b[2] = {(char)ch, 0};
            doc->buffer.insert(doc->caret_pos, std::string_view(b, 1));
            ++doc->caret_pos;
            doc->invalidate_from(doc->buffer.offset_to_point(doc->caret_pos).line);
        }
    }

    // Keep caret visible
    auto pt = doc->buffer.offset_to_point(doc->caret_pos);
    int  vl = g->lay.vis();
    if ((int)pt.line < doc->scroll_y)       doc->scroll_y = (int)pt.line;
    if ((int)pt.line >= doc->scroll_y + vl) doc->scroll_y = (int)pt.line - vl + 1;
}

// ============================================================================
//  DRAW
// ============================================================================

// ---- Header -----------------------------------------------------------------
static void draw_header() {
    Layout& L  = g->lay;
    Vector2 mp = cursor_pos();

    DrawRectangle(0, 0, L.W, L.header_h, C.header_bg);
    DrawRectangle(0, L.header_h - 1, L.W, 1, {0, 200, 160, 45});

    // Hamburger button
    Rectangle ham = {10, 8, 36, 32};
    bool hh = CheckCollisionPointRec(mp, ham);
    if (hh) DrawRectangleRounded(ham, 0.3f, 4, {255,255,255,18});
    for (int i = 0; i < 3; ++i)
        DrawRectangleRec({21, 15.0f + i * 7, 19, 2},
                         hh ? C.accent : Color{165,165,165,255});
    if (clicked(ham)) g->menu_open = !g->menu_open;

    // Title
    const char* title = "SIN EDITOR";
    DrawText(title, (L.W - MeasureText(title, 15)) / 2, 17, 15, C.accent);

    // Run button
    Rectangle run = {(float)(L.W - 94), 11, 79, 26};
    bool rh = CheckCollisionPointRec(mp, run);
    if (g->run_anim) {
        g->run_flash += GetFrameTime() * 3.0f;
        if (g->run_flash >= 1.0f) { g->run_anim = false; g->run_flash = 0; }
    }
    Color rc = {0, 210, 80, 255};
    if (g->run_anim) rc = lerp_col(rc, {255, 240, 60, 255}, g->run_flash);
    if (rh)          rc = lerp_col(rc, WHITE, 0.18f);

    DrawRectangleRounded(run, 0.35f, 6, rc);
    // Correct Raylib 5.0 API -- DrawRectangleRoundedLines (no "Ex" suffix)
    DrawRectangleRoundedLines(
        {run.x-1, run.y-1, run.width+2, run.height+2},
        0.35f, 6, 1.5f, Color{0, 255, 100, 35});
    int rw = MeasureText("RUN", 13);
    DrawText("RUN", (int)(run.x + (run.width - rw) / 2), (int)(run.y + 6), 13, BLACK);
    if (clicked(run)) run_script();
}

// ---- Hamburger menu ---------------------------------------------------------
static const char* MENU_LABELS[] = {
    "New File", "Save", "Close Tab", "Rename", "Delete Tab"
};
static constexpr int MENU_N = 5;

static void draw_menu() {
    if (!g->menu_open) return;
    Layout& L  = g->lay;
    Vector2 mp = cursor_pos();

    DrawRectangle(0, 0, L.W, L.H, {0,0,0,88});

    float     mh = (float)(MENU_N * L.menu_row_h + 14);
    Rectangle bg = {8, (float)(L.header_h + 4), (float)L.menu_w, mh};

    DrawRectangleRounded(bg, 0.07f, 6, {20,20,32,245});
    // Correct Raylib 5.0 API
    DrawRectangleRoundedLines(bg, 0.07f, 6, 1.0f, Color{0, 200, 160, 52});

    for (int i = 0; i < MENU_N; ++i) {
        Rectangle row = {bg.x+5, bg.y+7+(float)(i*L.menu_row_h),
                         bg.width-10, (float)L.menu_row_h};
        bool hov = CheckCollisionPointRec(mp, row);
        if (hov) DrawRectangleRounded(row, 0.2f, 4, {255,255,255,16});
        DrawText(MENU_LABELS[i], (int)(row.x+16), (int)(row.y+11), 14,
                 hov ? C.accent : Color{208,208,208,255});

        if (clicked(row)) {
            g->menu_open = false;
            auto* doc = g->tabs.current();
            switch (i) {
                case 0: new_doc();              break;
                case 1: save(doc);              break;
                case 2: g->tabs.close(g->tabs.active); break;
                case 3: if (doc) doc->title += "_r"; break;
                case 4: g->tabs.close(g->tabs.active); break;
            }
        }
    }
    // Click outside closes menu
    if (clicked({0,0,(float)L.W,(float)L.H}) && !CheckCollisionPointRec(mp,bg))
        g->menu_open = false;
}

// ---- Tab bar ----------------------------------------------------------------
static void draw_tabs() {
    Layout& L  = g->lay;
    int     y  = L.header_h;
    Vector2 mp = cursor_pos();

    DrawRectangle(0, y, L.W, L.tab_h, C.tab_bg);
    DrawRectangle(0, y + L.tab_h - 1, L.W, 1, {26,26,44,255});

    if (g->tabs.docs.empty()) return;

    // Horizontal scroll via wheel
    if (CheckCollisionPointRec(mp,{0,(float)y,(float)L.W,(float)L.tab_h})) {
        float w = GetMouseWheelMove();
        if (w != 0) g->tab_scroll = std::max(0.0f, g->tab_scroll - w * L.tab_w);
    }
    float total_w = (float)g->tabs.docs.size() * L.tab_w;
    g->tab_scroll = Clamp(g->tab_scroll, 0.0f, std::max(0.0f, total_w - (float)L.W));

    BeginScissorMode(0, y, L.W, L.tab_h);
    float x = 5 - g->tab_scroll;

    for (int i = 0; i < (int)g->tabs.docs.size(); ++i) {
        auto& doc   = g->tabs.docs[i];
        bool  act   = (i == g->tabs.active);
        bool  dirty = doc->buffer.is_dirty();

        Rectangle tr = {x, (float)(y+3), L.tab_w-3, (float)(L.tab_h-6)};
        DrawRectangleRounded(tr, 0.22f, 4, act ? C.tab_act : C.tab_bg);
        if (act) DrawRectangle((int)(tr.x+4), y+L.tab_h-3, (int)(tr.width-8), 2, C.accent);

        std::string lbl = (dirty ? "* " : "") + doc->title;
        int tw = MeasureText(lbl.c_str(), 12);
        while (tw > (int)(tr.width-30) && lbl.size()>2) { lbl.pop_back(); tw = MeasureText(lbl.c_str(),12); }
        DrawText(lbl.c_str(), (int)(tr.x+7), (int)(tr.y+8), 12,
                 act ? WHITE : Color{150,150,150,255});

        // x close
        Rectangle cr = {tr.x+tr.width-18, tr.y+7, 13, 13};
        bool ch = CheckCollisionPointRec(mp, cr);
        if (ch) DrawRectangleRounded(cr, 0.4f, 4, {210,45,45,180});
        DrawText("x", (int)(cr.x+3), (int)(cr.y+1), 11,
                 ch ? WHITE : Color{130,130,130,200});

        if      (clicked(cr)) { g->tabs.close(i); break; }
        else if (clicked(tr))   g->tabs.active = i;

        x += L.tab_w;
    }
    EndScissorMode();
}

// ---- Editor -----------------------------------------------------------------
// Extracted so we never use goto (jumping over declarations is UB in C++).
static void draw_doc(ide::Document* doc, int ey, int eh, int ex) {
    Layout& L = g->lay;

    float wh = GetMouseWheelMove();
    if (wh != 0) doc->scroll_y = std::max(0, doc->scroll_y - (int)(wh * 3));

    int lc  = (int)doc->buffer.line_count();
    int vis = L.vis();
    doc->scroll_y = Clamp(doc->scroll_y, 0, std::max(0, lc - vis));
    doc->line_cache.resize((size_t)lc);

    ide::Point cpt    = doc->buffer.offset_to_point(doc->caret_pos);
    int        act_ln = (int)cpt.line;
    const int  glyph  = L.font_sz / 2 + 2;

    BeginScissorMode(0, ey, L.W, eh);

    int last = std::min(doc->scroll_y + vis + 1, lc);
    for (int li = doc->scroll_y; li < last; ++li) {
        int sy = ey + (li - doc->scroll_y) * L.line_h;

        if (li == act_ln) {
            DrawRectangle(0, sy, L.W, L.line_h, C.active_bg);
            DrawRectangle(ex, sy + L.line_h - 1, L.ed_w(), 1, {26,106,172,33});
        }

        char num[10]; snprintf(num, sizeof(num), "%4d", li + 1);
        DrawText(num, 5, sy + 3, L.font_sz - 2,
                 (li == act_ln) ? C.active_ln : Color{62, 76, 100, 255});

        auto& lca = doc->line_cache[(size_t)li];
        if (lca.dirty) {
            lca.text   = doc->buffer.line((size_t)li);
            lca.tokens = g->hl.tokenize(lca.text);
            lca.dirty  = false;
        }

        int bx = ex + 5 - doc->scroll_x;
        if (lca.tokens.empty()) {
            DrawText(lca.text.c_str(), bx, sy + 3, L.font_sz, C.normal);
        } else {
            for (const auto& tok : lca.tokens) {
                std::string w(lca.text.data() + tok.start, tok.length);
                DrawText(w.c_str(), bx + (int)tok.start * glyph,
                         sy + 3, L.font_sz, g->hl.color_for(tok.kind));
            }
        }
    }

    // Caret blink
    g->blink += GetFrameTime() * 2.0f;
    if ((int)g->blink % 2 == 0) {
        int cx = ex + 5 + (int)cpt.col * glyph - doc->scroll_x;
        int cy = ey + (act_ln - doc->scroll_y) * L.line_h;
        DrawRectangle(cx, cy + 2, 2, L.line_h - 4, C.accent);
    }

    EndScissorMode();

    // Line / col indicator
    char pos[32];
    snprintf(pos, sizeof(pos), "Ln %zu  Col %zu", cpt.line + 1, cpt.col + 1);
    int pw = MeasureText(pos, 11);
    DrawText(pos, L.W - pw - 10, L.H - L.status_h + 5, 11, {95,135,180,200});
}

static void draw_editor() {
    Layout& L  = g->lay;
    int     ey = L.ed_y(), eh = L.ed_h(), ex = L.gutter_w;

    DrawRectangle(0, ey, L.W, eh, C.editor_bg);
    DrawRectangle(0, ey, L.gutter_w, eh, C.gutter_bg);
    DrawRectangle(L.gutter_w - 1, ey, 1, eh, {26, 26, 42, 255});

    auto* doc = g->tabs.current();
    if (doc) {
        draw_doc(doc, ey, eh, ex);
    } else {
        const char* m = "No file open -- use the hamburger menu";
        DrawText(m, (L.W - MeasureText(m, 14)) / 2, ey + eh / 2, 14, {70, 70, 100, 255});
    }

    // Status bar -- always drawn
    DrawRectangle(0, L.H - L.status_h, L.W, L.status_h, {9, 9, 15, 255});
    DrawText(g->status, 8, L.H - L.status_h + 5, 11, {105, 172, 105, 200});
}

// ---- Frame tick -------------------------------------------------------------
static void tick() {
    Layout& L  = g->lay;
    L.W = GetScreenWidth();
    L.H = GetScreenHeight();

#if defined(PLATFORM_ANDROID)
    L.header_h   = 58;
    L.tab_h      = 42;
    L.menu_row_h = 50;
    L.gutter_w   = 48;
    L.font_sz    = 18;
    L.line_h     = 26;
    L.tab_w      = 165;
    L.menu_w     = 225;
    L.status_h   = 26;
#endif

    poll_touch();
    keyboard();

    BeginDrawing();
    ClearBackground(C.editor_bg);

    draw_editor();
    draw_tabs();
    draw_header();
    draw_menu();

    EndDrawing();
}

// ============================================================================
//  Entry point  (int main for BOTH desktop and Android)
//  Raylib's rcore_android.c provides android_main() which calls main().
// ============================================================================
int main(void) {
#if defined(PLATFORM_ANDROID)
    InitWindow(0, 0, "SIN Editor");
#else
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(1280, 800, "SIN Editor -- SINO IDE");

    // Load app icon (desktop only -- Android uses the APK's built-in icon)
    if (FileExists("res/sinoicon.png")) {
        Image icon = LoadImage("res/sinoicon.png");
        SetWindowIcon(icon);
        UnloadImage(icon);
    }
#endif
    SetTargetFPS(60);

    App app;
    g = &app;

    // Seed default file
    new_doc();
    if (auto* d = g->tabs.current()) {
        d->buffer.insert(0,
            "// SIN Editor -- SINO Language IDE\n"
            "// v0.2  |  Raylib 5.0  |  C++20\n\n"
            "fn greet(name: str) -> str {\n"
            "    return \"Hello, \" + name + \"!\"\n"
            "}\n\n"
            "fn main() {\n"
            "    let msg = greet(\"SINO\")\n"
            "    println(msg)\n"
            "}\n");
        d->buffer.mark_clean();
    }

    ALOG("SIN Editor ready. Screen %d x %d", GetScreenWidth(), GetScreenHeight());

    while (!WindowShouldClose()) tick();

    CloseWindow();
    return 0;
}
