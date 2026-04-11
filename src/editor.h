#pragma once
// ============================================================
//  SIN Editor  --  Document model & Tab manager
//  Namespace: sinide
// ============================================================
#include "piece_table.h"
#include "highlighter.h"
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace sinide {

namespace fs = std::filesystem;

// ---- per-line render cache --------------------------------------------------
struct LineCache {
    std::string        text;
    std::vector<Token> tokens;
    bool               dirty = true;
};

// ---- one open file ----------------------------------------------------------
struct Document {
    std::string title  = "Untitled";
    fs::path    path   = {};
    PieceTable  buffer;          // direct-init: works with explicit constructor
    bool        is_new = true;

    size_t caret_pos = 0;
    int    scroll_y  = 0;
    int    scroll_x  = 0;

    std::vector<LineCache> line_cache;

    void invalidate_from(size_t line_idx) {
        for (size_t i = line_idx; i < line_cache.size(); ++i)
            line_cache[i].dirty = true;
    }

    explicit Document(std::string t = "Untitled") : title(std::move(t)) {}
};

// ---- tab manager ------------------------------------------------------------
struct TabManager {
    std::vector<std::unique_ptr<Document>> docs;
    int active = 0;

    Document* current() {
        if (docs.empty()) return nullptr;
        return docs[active].get();
    }

    int add(std::string title) {
        docs.push_back(std::make_unique<Document>(std::move(title)));
        active = static_cast<int>(docs.size()) - 1;
        return active;
    }

    void close(int idx) {
        if (docs.empty()) return;
        docs.erase(docs.begin() + idx);
        if (active >= static_cast<int>(docs.size()))
            active = static_cast<int>(docs.size()) - 1;
        if (active < 0) active = 0;
    }
};

} // namespace sinide
