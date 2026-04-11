#pragma once
// ============================================================
//  SIN Editor  --  SINO Language Syntax Highlighter
//  Single-pass tokeniser, header-only, namespace sinide
// ============================================================
#include <cctype>
#include <string>
#include <string_view>
#include <vector>
#include <raylib.h>

namespace sinide {

// ---- token types ------------------------------------------------------------
enum class TokKind : uint8_t {
    Normal, Keyword, TypeName, Number, String, Comment, Operator, Builtin
};

struct Token {
    size_t  start;
    size_t  length;
    TokKind kind;
};

// ---- color palette (cyberpunk dark) -----------------------------------------
struct Palette {
    Color normal    = {220, 220, 200, 255};
    Color keyword   = { 86, 182, 194, 255};
    Color type_name = { 78, 201, 176, 255};
    Color number    = {181, 206, 168, 255};
    Color str_lit   = {206, 145, 120, 255};
    Color comment   = {106, 153,  85, 255};
    Color op_color  = {212, 212, 212, 255};
    Color builtin   = {220, 120, 220, 255};
    Color active_ln = { 30, 160, 200, 255};
    Color gutter_bg = { 20,  20,  28, 255};
    Color editor_bg = { 14,  14,  20, 255};
    Color active_bg = { 25,  35,  50, 120};
    Color tab_act   = { 30,  30,  45, 255};
    Color tab_bg    = { 18,  18,  28, 255};
    Color header_bg = { 12,  12,  18, 255};
    Color accent    = {  0, 200, 160, 255};
};

// Single global palette instance -- accessed as sinide::THEME
inline const Palette THEME;

// ---- SINO language vocabulary -----------------------------------------------
inline constexpr std::string_view KW[] = {
    "fn","let","mut","const","if","else","elif","while","for","in",
    "return","import","export","struct","enum","match","case","break",
    "continue","nil","true","false","and","or","not","is","as","use",
    "pub","priv","self","super",
};
inline constexpr std::string_view TYPES[] = {
    "int","float","str","bool","void","byte","uint","char",
    "int32","int64","float32","float64","list","map","any",
};
inline constexpr std::string_view BUILTINS[] = {
    "print","println","input","len","range","type","open","close",
    "read","write","append","pop","push","keys","values","exit","assert",
};

// ---- tokeniser --------------------------------------------------------------
class Highlighter {
public:
    std::vector<Token> tokenize(std::string_view line) const;
    Color color_for(TokKind k) const;
};

inline std::vector<Token> Highlighter::tokenize(std::string_view line) const {
    std::vector<Token> toks;
    toks.reserve(32);
    const size_t n = line.size();
    size_t i = 0;

    while (i < n) {
        // Whitespace
        if (line[i] == ' ' || line[i] == '\t') { ++i; continue; }

        // Line comment
        if (i + 1 < n && line[i] == '/' && line[i+1] == '/') {
            toks.push_back({i, n - i, TokKind::Comment});
            break;
        }

        // String literal
        if (line[i] == '"' || line[i] == '\'') {
            char d = line[i];
            size_t s = i++;
            while (i < n && line[i] != d) { if (line[i] == '\\') ++i; ++i; }
            if (i < n) ++i;
            toks.push_back({s, i - s, TokKind::String});
            continue;
        }

        // Number  (includes negative literals)
        if (std::isdigit((unsigned char)line[i]) ||
            (line[i] == '-' && i + 1 < n && std::isdigit((unsigned char)line[i+1]))) {
            size_t s = i++;
            while (i < n && (std::isdigit((unsigned char)line[i]) || line[i] == '.' || line[i] == '_'))
                ++i;
            toks.push_back({s, i - s, TokKind::Number});
            continue;
        }

        // Identifier / keyword / type / builtin
        if (std::isalpha((unsigned char)line[i]) || line[i] == '_') {
            size_t s = i;
            while (i < n && (std::isalnum((unsigned char)line[i]) || line[i] == '_')) ++i;
            std::string_view w = line.substr(s, i - s);

            TokKind k = TokKind::Normal;
            for (auto kw : KW)      if (kw == w) { k = TokKind::Keyword;  break; }
            if (k == TokKind::Normal)
                for (auto ty : TYPES)   if (ty == w) { k = TokKind::TypeName; break; }
            if (k == TokKind::Normal)
                for (auto bi : BUILTINS) if (bi == w) { k = TokKind::Builtin;  break; }

            toks.push_back({s, i - s, k});
            continue;
        }

        // Operator / punctuation (single char)
        toks.push_back({i, 1, TokKind::Operator});
        ++i;
    }
    return toks;
}

inline Color Highlighter::color_for(TokKind k) const {
    switch (k) {
        case TokKind::Keyword:   return THEME.keyword;
        case TokKind::TypeName:  return THEME.type_name;
        case TokKind::Number:    return THEME.number;
        case TokKind::String:    return THEME.str_lit;
        case TokKind::Comment:   return THEME.comment;
        case TokKind::Operator:  return THEME.op_color;
        case TokKind::Builtin:   return THEME.builtin;
        default:                 return THEME.normal;
    }
}

} // namespace sinide
