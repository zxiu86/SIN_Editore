#include "piece_table.h"
#include <algorithm>
#include <stdexcept>

namespace sinide {

// ---- helpers ----------------------------------------------------------------
const std::string& PieceTable::buf_ref(BufKind k) const {
    return (k == BufKind::Original) ? orig_buf_ : add_buf_;
}

size_t PieceTable::count_nl(std::string_view sv) const {
    return static_cast<size_t>(std::count(sv.begin(), sv.end(), '\n'));
}

void PieceTable::push_snap() {
    undo_stack_.push_back({pieces_, add_buf_.size(), total_chars_});
    redo_stack_.clear();
    dirty_ = true;
}

// Returns {piece_index, byte_offset_within_that_piece}
std::pair<size_t, size_t> PieceTable::find_piece(size_t pos) const {
    size_t acc = 0;
    for (size_t i = 0; i < pieces_.size(); ++i) {
        if (pos <= acc + pieces_[i].length)
            return {i, pos - acc};
        acc += pieces_[i].length;
    }
    return {pieces_.size(), 0};
}

// ---- constructor ------------------------------------------------------------
PieceTable::PieceTable(std::string original) : orig_buf_(std::move(original)) {
    if (!orig_buf_.empty()) {
        pieces_.push_back({BufKind::Original, 0, orig_buf_.size(),
                           count_nl(orig_buf_)});
        total_chars_ = orig_buf_.size();
    }
}

// ---- insert -----------------------------------------------------------------
void PieceTable::insert(size_t pos, std::string_view text) {
    if (text.empty()) return;
    push_snap();

    size_t add_start = add_buf_.size();
    add_buf_.append(text);
    Piece np{BufKind::Added, add_start, text.size(), count_nl(text)};
    total_chars_ += text.size();

    if (pieces_.empty()) {
        pieces_.push_back(np);
        return;
    }

    auto [idx, off] = find_piece(pos);

    if (idx == pieces_.size()) {
        pieces_.push_back(np);
        return;
    }

    if (off == 0) {
        pieces_.insert(pieces_.begin() + (long)idx, np);
    } else if (off == pieces_[idx].length) {
        pieces_.insert(pieces_.begin() + (long)idx + 1, np);
    } else {
        // Split pieces_[idx] at byte offset off
        Piece& p = pieces_[idx];
        const std::string& b = buf_ref(p.buf);

        Piece lp{p.buf, p.start,       off,             count_nl({b.data() + p.start, off})};
        Piece rp{p.buf, p.start + off, p.length - off,  p.lines - lp.lines};

        // Replace p with [lp, np, rp]
        pieces_[idx] = lp;
        pieces_.insert(pieces_.begin() + (long)idx + 1, rp);
        pieces_.insert(pieces_.begin() + (long)idx + 1, np);
    }
}

// ---- erase ------------------------------------------------------------------
void PieceTable::erase(size_t pos, size_t count) {
    if (count == 0 || pieces_.empty()) return;
    push_snap();
    total_chars_ -= count;

    size_t end_pos = pos + count;
    std::vector<Piece> result;
    result.reserve(pieces_.size() + 1);

    size_t acc = 0;
    for (const auto& p : pieces_) {
        size_t p_end = acc + p.length;

        if (p_end <= pos || acc >= end_pos) {
            // Piece entirely outside deletion range
            result.push_back(p);
        } else {
            const std::string& b = buf_ref(p.buf);

            // Keep portion before pos
            if (acc < pos) {
                size_t keep = pos - acc;
                result.push_back({p.buf, p.start, keep,
                                  count_nl({b.data() + p.start, keep})});
            }
            // Keep portion after end_pos
            if (p_end > end_pos) {
                size_t skip = end_pos - acc;
                size_t keep = p.length - skip;
                result.push_back({p.buf, p.start + skip, keep,
                                  count_nl({b.data() + p.start + skip, keep})});
            }
        }
        acc += p.length;
    }
    pieces_ = std::move(result);
}

// ---- text -------------------------------------------------------------------
std::string PieceTable::text() const {
    std::string out;
    out.reserve(total_chars_);
    for (const auto& p : pieces_)
        out.append(buf_ref(p.buf), p.start, p.length);
    return out;
}

std::string PieceTable::line(size_t line_idx) const {
    std::string result;
    size_t cur_line = 0;
    for (const auto& p : pieces_) {
        const std::string& b = buf_ref(p.buf);
        for (size_t i = p.start; i < p.start + p.length; ++i) {
            if (cur_line == line_idx) {
                if (b[i] == '\n') return result;
                result += b[i];
            } else if (b[i] == '\n') {
                ++cur_line;
                if (cur_line > line_idx) return result;
            }
        }
    }
    return result;
}

size_t PieceTable::line_count() const {
    size_t n = 0;
    for (const auto& p : pieces_) n += p.lines;
    return n + 1;
}

Point PieceTable::offset_to_point(size_t char_pos) const {
    Point pt{0, 0};
    size_t acc = 0;
    for (const auto& p : pieces_) {
        const std::string& b = buf_ref(p.buf);
        for (size_t i = p.start; i < p.start + p.length; ++i) {
            if (acc == char_pos) return pt;
            if (b[i] == '\n') { ++pt.line; pt.col = 0; }
            else                ++pt.col;
            ++acc;
        }
    }
    return pt;
}

size_t PieceTable::point_to_offset(Point target) const {
    size_t offset = 0, cur_line = 0, cur_col = 0;
    for (const auto& p : pieces_) {
        const std::string& b = buf_ref(p.buf);
        for (size_t i = p.start; i < p.start + p.length; ++i) {
            if (cur_line == target.line && cur_col == target.col) return offset;
            if (b[i] == '\n') { ++cur_line; cur_col = 0; }
            else                ++cur_col;
            ++offset;
        }
    }
    return offset;
}

// ---- undo / redo ------------------------------------------------------------
void PieceTable::undo() {
    if (undo_stack_.empty()) return;
    redo_stack_.push_back({pieces_, add_buf_.size(), total_chars_});
    auto& s     = undo_stack_.back();
    pieces_     = s.pieces;
    total_chars_= s.total_chars;
    undo_stack_.pop_back();
    dirty_ = !undo_stack_.empty();
}

void PieceTable::redo() {
    if (redo_stack_.empty()) return;
    undo_stack_.push_back({pieces_, add_buf_.size(), total_chars_});
    auto& s     = redo_stack_.back();
    pieces_     = s.pieces;
    total_chars_= s.total_chars;
    redo_stack_.pop_back();
    dirty_ = true;
}

} // namespace sinide
