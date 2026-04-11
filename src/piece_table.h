#pragma once
// ============================================================
//  SIN Editor  --  Piece Table Text Buffer
//  Namespace: sinide   (avoids conflict with std::sin)
// ============================================================
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sinide {

enum class BufKind : uint8_t { Original, Added };

struct Piece {
    BufKind buf;
    size_t  start;
    size_t  length;
    size_t  lines;   // cached newline count
};

struct Point { size_t line = 0, col = 0; };

class PieceTable {
public:
    explicit PieceTable(std::string original = "");

    void insert(size_t char_pos, std::string_view text);
    void erase (size_t char_pos, size_t count);

    std::string text()                             const;
    std::string line(size_t line_idx)              const;
    size_t      line_count()                       const;
    size_t      char_count()                       const { return total_chars_; }

    Point  offset_to_point(size_t char_pos)        const;
    size_t point_to_offset(Point p)                const;

    void undo();
    void redo();
    bool can_undo() const { return !undo_stack_.empty(); }
    bool can_redo() const { return !redo_stack_.empty(); }

    bool is_dirty()  const { return dirty_; }
    void mark_clean()      { dirty_ = false; }

private:
    std::string          orig_buf_;
    std::string          add_buf_;
    std::vector<Piece>   pieces_;
    size_t               total_chars_ = 0;
    bool                 dirty_       = false;

    struct Snap {
        std::vector<Piece> pieces;
        size_t             add_buf_sz;
        size_t             total_chars;
    };
    std::vector<Snap> undo_stack_;
    std::vector<Snap> redo_stack_;

    void                      push_snap();
    size_t                    count_nl(std::string_view sv) const;
    std::pair<size_t, size_t> find_piece(size_t char_pos)   const;
    const std::string&        buf_ref(BufKind k)             const;
};

} // namespace sinide
