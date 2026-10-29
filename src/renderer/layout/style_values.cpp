#include "renderer/layout/style_values.hpp"

#include <cctype>
#include <string>
#include <string_view>
#include <cstring>
#include <vector>
#include <algorithm>
#include <unordered_map>

namespace honeybee::renderer::layout {

namespace {

bool is_ws(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && is_ws(s.front())) s.remove_prefix(1);
    while (!s.empty() && is_ws(s.back())) s.remove_suffix(1);
    return s;
}

int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool has_internal_ws(std::string_view s) {
    for (char c : s) if (is_ws(c)) return true;
    return false;
}

bool ends_with(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size() &&
           s.substr(s.size() - suffix.size()) == suffix;
}

} // namespace

float parse_px(std::string_view s) {
    s = trim(s);
    if (s.empty()) return 0.0f;
    if (has_internal_ws(s)) return 0.0f;
    if (s.size() < 3) return 0.0f;
    if (!ends_with(s, "px")) return 0.0f;
    try {
        return std::stof(std::string(s.substr(0, s.size() - 2)));
    } catch (...) {
        return 0.0f;
    }
}

// =============================================================
// ADR-037: CSS math expression evaluator.
//
// Supports calc(), min(), max(), clamp() with standard operator
// precedence (* / before + -). Every operand is converted to PX
// immediately (em/rem/%/pt resolved via LengthContext), so
// mixed-unit arithmetic like "2em + 8px" works via common-unit-
// at-parse-time. Unitless operands stay unitless. Final Number
// results are treated as px (documented limitation).
//
// vh/vw/vmin/vmax resolved per ADR-051 against ctx.viewport_h/w.
// in LengthContext); vertical percentages inside calc.
// =============================================================

namespace {

enum class MathUnit : std::uint8_t {
    Number, Px,
};

struct MathVal {
    float value = 0.0f;
    MathUnit unit = MathUnit::Number;
    bool valid = false;
};

// ADR-037: trim leading/trailing whitespace, return substring.
// Used by min/max/clamp comma-splitting in eval_math_expression.
std::string_view trim_ws(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' ||
                          s.front() == '\n' || s.front() == '\r')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' ||
                          s.back() == '\n' || s.back() == '\r')) {
        s.remove_suffix(1);
    }
    return s;
}

void skip_ws(std::string_view s, size_t& pos) {
    while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' ||
                              s[pos] == '\n' || s[pos] == '\r')) ++pos;
}

MathVal parse_operand(std::string_view s, size_t& pos,
                      const LengthContext& ctx) {
    MathVal out;
    skip_ws(s, pos);
    if (pos >= s.size()) return out;

    size_t start = pos;
    if (s[pos] == '-' || s[pos] == '+') ++pos;

    size_t num_start = pos;
    while (pos < s.size() &&
           ((s[pos] >= '0' && s[pos] <= '9') || s[pos] == '.')) ++pos;
    if (pos == num_start) { pos = start; return out; }

    size_t unit_start = pos;
    while (pos < s.size() &&
           ((s[pos] >= 'a' && s[pos] <= 'z') || s[pos] == '%')) ++pos;

    std::string_view num_part = s.substr(start, unit_start - start);
    std::string_view unit_part = s.substr(unit_start, pos - unit_start);

    float n = 0.0f;
    try { n = std::stof(std::string(num_part)); }
    catch (...) { return out; }

    if (unit_part.empty()) {
        out.value = n;
        out.unit = MathUnit::Number;
        out.valid = true;
        return out;
    }
    if (unit_part == "px") {
        out.value = n;
    } else if (unit_part == "em") {
        const float ref = ctx.on_font_size_property
                              ? ctx.parent_font_size
                              : ctx.font_size;
        out.value = n * ref;
    } else if (unit_part == "rem") {
        out.value = n * ctx.root_font_size;
    } else if (unit_part == "%") {
        out.value = (n / 100.0f) * ctx.parent_content_width;
    } else if (unit_part == "pt") {
        out.value = n * (96.0f / 72.0f);
    } else if (unit_part == "vh") {
        // ADR-051: viewport units inside math expressions. Same
        // resolution as parse_length's VH/VW/VMIN/VMAX cases.
        out.value = (n / 100.0f) * ctx.viewport_h;
    } else if (unit_part == "vw") {
        out.value = (n / 100.0f) * ctx.viewport_w;
    } else if (unit_part == "vmin") {
        out.value = (n / 100.0f) * std::min(ctx.viewport_w,
                                             ctx.viewport_h);
    } else if (unit_part == "vmax") {
        out.value = (n / 100.0f) * std::max(ctx.viewport_w,
                                             ctx.viewport_h);
    } else {
        return out;
    }
    out.unit = MathUnit::Px;
    out.valid = true;
    return out;
}

MathVal combine(MathVal l, MathVal r, char op) {
    MathVal out;
    if (!l.valid || !r.valid) return out;

    if (op == '*') {
        if (l.unit == MathUnit::Px && r.unit == MathUnit::Px) return out;
        out.value = l.value * r.value;
        if (l.unit == MathUnit::Px || r.unit == MathUnit::Px)
            out.unit = MathUnit::Px;
        else
            out.unit = MathUnit::Number;
        out.valid = true;
        return out;
    }
    if (op == '/') {
        if (r.value == 0.0f) return out;
        if (l.unit == MathUnit::Px && r.unit == MathUnit::Number) {
            out.value = l.value / r.value;
            out.unit = MathUnit::Px;
            out.valid = true;
            return out;
        }
        if (l.unit == MathUnit::Number && r.unit == MathUnit::Number) {
            out.value = l.value / r.value;
            out.unit = MathUnit::Number;
            out.valid = true;
            return out;
        }
        if (l.unit == MathUnit::Px && r.unit == MathUnit::Px) {
            out.value = l.value / r.value;
            out.unit = MathUnit::Number;
            out.valid = true;
            return out;
        }
        return out;
    }
    if (op == '+' || op == '-') {
        if (l.unit == MathUnit::Px && r.unit == MathUnit::Px) {
            out.value = (op == '+') ? (l.value + r.value)
                                    : (l.value - r.value);
            out.unit = MathUnit::Px;
            out.valid = true;
            return out;
        }
        if (l.unit == MathUnit::Number && r.unit == MathUnit::Number) {
            out.value = (op == '+') ? (l.value + r.value)
                                    : (l.value - r.value);
            out.unit = MathUnit::Number;
            out.valid = true;
            return out;
        }
        return out;
    }
    return out;
}

MathVal eval_expr(std::string_view s, size_t& pos,
                  const LengthContext& ctx);

MathVal eval_primary(std::string_view s, size_t& pos,
                     const LengthContext& ctx) {
    skip_ws(s, pos);
    if (pos >= s.size()) return {};

    // Nested math function: calc(...), min(...), max(...), clamp(...)
    // Detect function name followed by '(' and recursively evaluate.
    {
        static const char* kFuncs[] = {"calc(", "min(", "max(", "clamp("};
        for (const char* fn : kFuncs) {
            const size_t flen = std::strlen(fn);
            if (s.size() - pos >= flen &&
                s.compare(pos, flen, fn) == 0) {
                // Find matching close-paren
                int depth = 1;
                size_t close = pos + flen;
                while (close < s.size() && depth > 0) {
                    if (s[close] == '(') ++depth;
                    else if (s[close] == ')') --depth;
                    ++close;
                }
                if (depth != 0) return {};
                const auto sub = s.substr(pos, close - pos);
                const float v = eval_math_expression(sub, ctx);
                pos = close;
                MathVal out;
                out.value = v;
                out.unit = MathUnit::Px;
                out.valid = true;
                return out;
            }
        }
    }

    // Parenthesized sub-expression
    if (s[pos] == '(') {
        ++pos;
        MathVal inner = eval_expr(s, pos, ctx);
        skip_ws(s, pos);
        if (pos < s.size() && s[pos] == ')') ++pos;
        return inner;
    }
    return parse_operand(s, pos, ctx);
}

MathVal eval_term(std::string_view s, size_t& pos,
                  const LengthContext& ctx) {
    MathVal l = eval_primary(s, pos, ctx);
    if (!l.valid) return l;
    while (pos < s.size()) {
        skip_ws(s, pos);
        if (pos >= s.size()) break;
        char op = s[pos];
        if (op != '*' && op != '/') break;
        ++pos;
        MathVal r = eval_primary(s, pos, ctx);
        l = combine(l, r, op);
        if (!l.valid) return l;
    }
    return l;
}

MathVal eval_expr(std::string_view s, size_t& pos,
                  const LengthContext& ctx) {
    MathVal l = eval_term(s, pos, ctx);
    if (!l.valid) return l;
    while (pos < s.size()) {
        skip_ws(s, pos);
        if (pos >= s.size()) break;
        char op = s[pos];
        if (op != '+' && op != '-') break;
        ++pos;
        MathVal r = eval_term(s, pos, ctx);
        l = combine(l, r, op);
        if (!l.valid) return l;
    }
    return l;
}

float resolve_to_px(const MathVal& v) {
    if (!v.valid) return 0.0f;
    return v.value;
}

std::vector<std::string_view> split_commas(std::string_view inner) {
    std::vector<std::string_view> out;
    size_t start = 0;
    int depth = 0;
    for (size_t i = 0; i < inner.size(); ++i) {
        if (inner[i] == '(') ++depth;
        else if (inner[i] == ')') --depth;
        else if (inner[i] == ',' && depth == 0) {
            out.push_back(inner.substr(start, i - start));
            start = i + 1;
        }
    }
    out.push_back(inner.substr(start));
    return out;
}

}  // namespace

float eval_math_expression(std::string_view s, const LengthContext& ctx) {
    if (s.empty()) return 0.0f;

    // Extract function name + inner content
    auto name_end = s.find('(');
    if (name_end == std::string_view::npos) return 0.0f;
    const std::string_view name = s.substr(0, name_end);
    if (s.back() != ')') return 0.0f;
    std::string_view inner = s.substr(name_end + 1, s.size() - name_end - 2);

    if (name == "calc") {
        size_t pos = 0;
        MathVal v = eval_expr(inner, pos, ctx);
        return resolve_to_px(v);
    }

    if (name == "min" || name == "max") {
        // Split by top-level commas
        std::vector<std::string_view> parts;
        size_t start = 0;
        int depth = 0;
        for (size_t i = 0; i < inner.size(); ++i) {
            if (inner[i] == '(') ++depth;
            else if (inner[i] == ')') --depth;
            else if (inner[i] == ',' && depth == 0) {
                parts.push_back(trim_ws(inner.substr(start, i - start)));
                start = i + 1;
            }
        }
        parts.push_back(trim_ws(inner.substr(start)));
        if (parts.empty()) return 0.0f;

        float result = 0.0f;
        bool first = true;
        for (auto part : parts) {
            size_t p = 0;
            MathVal v = eval_expr(part, p, ctx);
            if (!v.valid) return 0.0f;
            const float px = resolve_to_px(v);
            if (first) { result = px; first = false; }
            else if (name == "min") result = std::min(result, px);
            else result = std::max(result, px);
        }
        return result;
    }

    if (name == "clamp") {
        std::vector<std::string_view> parts;
        size_t start = 0;
        int depth = 0;
        for (size_t i = 0; i < inner.size(); ++i) {
            if (inner[i] == '(') ++depth;
            else if (inner[i] == ')') --depth;
            else if (inner[i] == ',' && depth == 0) {
                parts.push_back(trim_ws(inner.substr(start, i - start)));
                start = i + 1;
            }
        }
        parts.push_back(trim_ws(inner.substr(start)));
        if (parts.size() != 3) return 0.0f;
        float lo = 0, pref = 0, hi = 0;
        size_t p = 0; MathVal v = eval_expr(parts[0], p, ctx);
        lo = resolve_to_px(v);
        p = 0; v = eval_expr(parts[1], p, ctx);
        pref = resolve_to_px(v);
        p = 0; v = eval_expr(parts[2], p, ctx);
        hi = resolve_to_px(v);
        return std::max(lo, std::min(pref, hi));
    }

    return 0.0f;
}

float parse_length(std::string_view s, const LengthContext& ctx) {
    s = trim(s);
    if (s.empty()) return 0.0f;
    if (s == "auto") return 0.0f;

    // ADR-037: math expression dispatch (must precede has_internal_ws
    // because calc() values legitimately contain internal whitespace).
    if (s.rfind("calc(", 0) == 0 || s.rfind("min(", 0) == 0 ||
        s.rfind("max(", 0) == 0 || s.rfind("clamp(", 0) == 0) {
        return eval_math_expression(s, ctx);
    }

    if (has_internal_ws(s)) return 0.0f;

    enum Unit { PX, EM, REM, PCT, VH, VW, VMIN, VMAX } unit;
    size_t suffix_len = 0;

    // ADR-051: viewport units. vmin/vmax (4 chars) checked
    // before vh/vw (2 chars) since "vmin"/"vmax" don't end in
    // "vh"/"vw" -- but ordering longest-first is defensive.
    if (ends_with(s, "vmin")) {
        unit = VMIN; suffix_len = 4;
    } else if (ends_with(s, "vmax")) {
        unit = VMAX; suffix_len = 4;
    } else if (ends_with(s, "px")) {
        unit = PX; suffix_len = 2;
    } else if (ends_with(s, "rem")) {
        unit = REM; suffix_len = 3;
    } else if (ends_with(s, "em")) {
        unit = EM; suffix_len = 2;
    } else if (ends_with(s, "vh")) {
        unit = VH; suffix_len = 2;
    } else if (ends_with(s, "vw")) {
        unit = VW; suffix_len = 2;
    } else if (ends_with(s, "%")) {
        unit = PCT; suffix_len = 1;
    } else {
        return 0.0f;
    }

    if (s.size() <= suffix_len) return 0.0f;

    float n = 0.0f;
    try {
        n = std::stof(std::string(s.substr(0, s.size() - suffix_len)));
    } catch (...) {
        return 0.0f;
    }

    switch (unit) {
        case PX:
            return n;
        case EM: {
            const float ref = ctx.on_font_size_property
                                  ? ctx.parent_font_size
                                  : ctx.font_size;
            return n * ref;
        }
        case REM:
            return n * ctx.root_font_size;
        case PCT:
            return (n / 100.0f) * ctx.parent_content_width;
        case VH:
            return (n / 100.0f) * ctx.viewport_h;
        case VW:
            return (n / 100.0f) * ctx.viewport_w;
        case VMIN:
            return (n / 100.0f) * std::min(ctx.viewport_w,
                                            ctx.viewport_h);
        case VMAX:
            return (n / 100.0f) * std::max(ctx.viewport_w,
                                            ctx.viewport_h);
    }
    return 0.0f;
}

bool is_auto(std::string_view s) {
    return trim(s) == "auto";
}

Color parse_color(std::string_view s) {
    s = trim(s);
    const std::string key(s);

    static const std::unordered_map<std::string, Color> named = {
        {"transparent", Color::transparent()},
        {"black",       {0.0f, 0.0f, 0.0f, 1.0f}},
        {"white",       {1.0f, 1.0f, 1.0f, 1.0f}},
        {"red",         {1.0f, 0.0f, 0.0f, 1.0f}},
        {"green",       {0.0f, 0.5f, 0.0f, 1.0f}},
        {"blue",        {0.0f, 0.0f, 1.0f, 1.0f}},
        {"navy",        {0.0f, 0.0f, 0.502f, 1.0f}},
        {"yellow",      {1.0f, 1.0f, 0.0f, 1.0f}},
        {"orange",      {1.0f, 0.647f, 0.0f, 1.0f}},
        {"purple",      {0.502f, 0.0f, 0.502f, 1.0f}},
        {"gray",        {0.5f, 0.5f, 0.5f, 1.0f}},
        {"grey",        {0.5f, 0.5f, 0.5f, 1.0f}},
        {"silver",      {0.753f, 0.753f, 0.753f, 1.0f}},
        {"maroon",      {0.502f, 0.0f, 0.0f, 1.0f}},
        {"teal",        {0.0f, 0.502f, 0.502f, 1.0f}},
        {"aqua",        {0.0f, 1.0f, 1.0f, 1.0f}},
        {"fuchsia",     {1.0f, 0.0f, 1.0f, 1.0f}},
        {"lime",        {0.0f, 1.0f, 0.0f, 1.0f}},
        {"olive",       {0.502f, 0.502f, 0.0f, 1.0f}},
    };
    auto it = named.find(key);
    if (it != named.end()) return it->second;

    if (!key.empty() && key[0] == '#') {
        const std::string hex = key.substr(1);
        if (hex.size() == 3) {
            const int r = hex_digit(hex[0]);
            const int g = hex_digit(hex[1]);
            const int b = hex_digit(hex[2]);
            if (r >= 0 && g >= 0 && b >= 0) {
                return {r / 15.0f, g / 15.0f, b / 15.0f, 1.0f};
            }
        } else if (hex.size() == 6) {
            const int r = hex_digit(hex[0]) * 16 + hex_digit(hex[1]);
            const int g = hex_digit(hex[2]) * 16 + hex_digit(hex[3]);
            const int b = hex_digit(hex[4]) * 16 + hex_digit(hex[5]);
            if (r >= 0 && g >= 0 && b >= 0) {
                return {r / 255.0f, g / 255.0f, b / 255.0f, 1.0f};
            }
        }
    }

    return Color::transparent();
}

} // namespace honeybee::renderer::layout
