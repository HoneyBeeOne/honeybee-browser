#include "renderer/layout/grid_layout.hpp"

#include <cctype>
#include <cstddef>
#include <cstdlib>

namespace honeybee::renderer::layout {

namespace {

bool is_ws(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

std::string trim(std::string_view s) {
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && is_ws(s[b])) ++b;
    while (e > b && is_ws(s[e - 1])) --e;
    return std::string(s.substr(b, e - b));
}

// Parse one token (e.g. "12.25rem", "1fr", "minmax(0, 1fr)").
std::optional<TrackSize> parse_one_track(
    std::string_view token, const LengthContext& ctx) {

    // fr track: "<N>fr"
    if (token.size() >= 2 &&
        token.substr(token.size() - 2) == "fr") {
        try {
            const float f = std::stof(
                std::string(token.substr(0, token.size() - 2)));
            if (f > 0.0f) {
                TrackSize ts;
                ts.kind = TrackSize::Kind::Fr;
                ts.value = f;
                return ts;
            }
        } catch (...) {
            return std::nullopt;
        }
        return std::nullopt;
    }

    // minmax(...): "minmax(<min>, <max>)" where min is 0 or a
    // length, max is <N>fr or a length.
    if (token.rfind("minmax(", 0) == 0 &&
        token.size() > 8 && token.back() == ')') {
        const std::string inner(
            token.substr(7, token.size() - 8));
        // Find top-level comma (no parens in min/max forms we support).
        const std::size_t comma = inner.find(',');
        if (comma == std::string::npos) return std::nullopt;
        const std::string min_str = trim(inner.substr(0, comma));
        const std::string max_str = trim(inner.substr(comma + 1));

        float min_v = 0.0f;
        if (min_str == "0") {
            min_v = 0.0f;
        } else {
            min_v = parse_length(min_str, ctx);
        }

        // max may be <N>fr
        if (max_str.size() >= 2 &&
            max_str.substr(max_str.size() - 2) == "fr") {
            try {
                const float f = std::stof(
                    max_str.substr(0, max_str.size() - 2));
                if (f > 0.0f) {
                    TrackSize ts;
                    ts.kind = TrackSize::Kind::MinMaxFr;
                    ts.value = f;
                    ts.min_value = min_v;
                    return ts;
                }
            } catch (...) {
                return std::nullopt;
            }
            return std::nullopt;
        }

        // max may be a fixed length
        const float max_v = parse_length(max_str, ctx);
        if (max_v > 0.0f) {
            TrackSize ts;
            ts.kind = TrackSize::Kind::MinMaxFixed;
            ts.min_value = min_v;
            ts.max_value = max_v;
            return ts;
        }
        return std::nullopt;
    }

    // Plain fixed length: "12.25rem", "200px", etc.
    const float fixed = parse_length(token, ctx);
    if (fixed > 0.0f) {
        TrackSize ts;
        ts.kind = TrackSize::Kind::Fixed;
        ts.value = fixed;
        return ts;
    }

    // ADR-028: content-sizing keywords. `min-content`, `max-content`,
    // and `auto` all resolve to natural content size. In flow layout,
    // that means "don't override the child's own measurement" --
    // for rows, natural height; for columns, natural width (which
    // our v1 treats as 0 because columns need explicit width; see
    // compute_column_widths). Wikipedia's grid-template-rows uses
    // `min-content 1fr min-content` -- all three tokens must parse,
    // or the row count gets truncated to just the fr track.
    if (token == "auto" || token == "min-content" ||
        token == "max-content") {
        TrackSize ts;
        ts.kind = TrackSize::Kind::Auto;
        return ts;
    }
    return std::nullopt;
}

} // namespace

std::vector<TrackSize> parse_track_list(
    std::string_view text, const LengthContext& ctx) {
    std::vector<TrackSize> result;
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && is_ws(text[i])) ++i;
        if (i >= text.size()) break;

        // Scan one token, tracking parens (for minmax(...)).
        const std::size_t start = i;
        int depth = 0;
        while (i < text.size()) {
            const char c = text[i];
            if (c == '(') {
                ++depth;
            } else if (c == ')' && depth > 0) {
                --depth;
            } else if (is_ws(c) && depth == 0) {
                break;
            }
            ++i;
        }
        const std::string_view token = text.substr(start, i - start);
        if (auto ts = parse_one_track(token, ctx)) {
            result.push_back(*ts);
        }
    }
    return result;
}

std::unordered_map<std::string, GridArea> parse_grid_areas(
    std::string_view text) {
    std::unordered_map<std::string, GridArea> result;
    int row = 0;
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && is_ws(text[i])) ++i;
        if (i >= text.size()) break;

        // Expect a quote character.
        const char quote = text[i];
        if (quote != '\'' && quote != '"') break;
        ++i;

        const std::size_t row_start = i;
        while (i < text.size() && text[i] != quote) ++i;
        if (i >= text.size()) break;
        const std::string_view row_str =
            text.substr(row_start, i - row_start);
        ++i;

        // Split row_str into cells.
        int col = 0;
        std::size_t j = 0;
        while (j < row_str.size()) {
            while (j < row_str.size() && is_ws(row_str[j])) ++j;
            if (j >= row_str.size()) break;
            const std::size_t cell_start = j;
            while (j < row_str.size() && !is_ws(row_str[j])) ++j;
            const std::string name(
                row_str.substr(cell_start, j - cell_start));
            if (name != ".") {
                auto it = result.find(name);
                if (it == result.end()) {
                    result[name] = GridArea{row, col, 1, 1};
                } else {
                    // Expand the bounding rect for spanning areas.
                    auto& a = it->second;
                    if (row < a.row) {
                        a.row_span += a.row - row;
                        a.row = row;
                    }
                    if (row >= a.row + a.row_span) {
                        a.row_span = row - a.row + 1;
                    }
                    if (col < a.col) {
                        a.col_span += a.col - col;
                        a.col = col;
                    }
                    if (col >= a.col + a.col_span) {
                        a.col_span = col - a.col + 1;
                    }
                }
            }
            ++col;
        }
        ++row;
    }
    return result;
}

std::optional<GridTemplate> parse_grid_template(
    const css::ComputedStyle& style, const LengthContext& ctx) {
    GridTemplate tmpl;

    const auto cols = style.get("grid-template-columns");
    if (cols) {
        tmpl.columns = parse_track_list(*cols, ctx);
    }
    if (tmpl.columns.empty()) return std::nullopt;

    const auto rows = style.get("grid-template-rows");
    if (rows) {
        tmpl.rows = parse_track_list(*rows, ctx);
    }

    const auto areas = style.get("grid-template-areas");
    if (areas) {
        tmpl.areas = parse_grid_areas(*areas);
    }

    return tmpl;
}

std::vector<float> compute_column_widths(
    const std::vector<TrackSize>& tracks,
    float total_width,
    float column_gap) {
    std::vector<float> widths;
    if (tracks.empty()) return widths;

    // Reserve space for gaps between columns.
    const float gap_total =
        tracks.size() > 1
            ? static_cast<float>(tracks.size() - 1) * column_gap
            : 0.0f;
    const float available = total_width - gap_total;

    // Pass 1: sum fixed widths and fr factors.
    float fixed_total = 0.0f;
    float fr_sum = 0.0f;
    for (const auto& t : tracks) {
        switch (t.kind) {
            case TrackSize::Kind::Fixed:
                fixed_total += t.value;
                break;
            case TrackSize::Kind::Fr:
            case TrackSize::Kind::MinMaxFr:
                fr_sum += t.value;
                break;
            case TrackSize::Kind::MinMaxFixed:
                fixed_total += t.max_value;
                break;
            case TrackSize::Kind::Auto:
                // ADR-028 v1: auto columns get 0 width (deferred).
                break;
        }
    }

    // Pass 2: allocate per-track width.
    const float remaining = available - fixed_total;
    for (const auto& t : tracks) {
        float w = 0.0f;
        switch (t.kind) {
            case TrackSize::Kind::Fixed:
                w = t.value;
                break;
            case TrackSize::Kind::Fr:
                w = fr_sum > 0.0f ? remaining * t.value / fr_sum : 0.0f;
                break;
            case TrackSize::Kind::MinMaxFr:
                w = fr_sum > 0.0f ? remaining * t.value / fr_sum : 0.0f;
                if (w < t.min_value) w = t.min_value;
                break;
            case TrackSize::Kind::MinMaxFixed:
                w = t.max_value;
                if (w < t.min_value) w = t.min_value;
                break;
            case TrackSize::Kind::Auto:
                // ADR-028 v1: auto columns get 0 width (deferred).
                w = 0.0f;
                break;
        }
        if (w < 0.0f) w = 0.0f;
        widths.push_back(w);
    }
    return widths;
}

void shift_subtree_y(LayoutNode& node, float dy) {
    if (dy == 0.0f) return;
    node.y += dy;
    switch (node.kind) {
        case LayoutKind::Box: {
            auto& b = static_cast<LayoutBox&>(node);
            for (auto& c : b.children) shift_subtree_y(*c, dy);
            break;
        }
        case LayoutKind::Inline: {
            auto& inl = static_cast<LayoutInline&>(node);
            for (auto& c : inl.children) shift_subtree_y(*c, dy);
            break;
        }
        case LayoutKind::InlineContext: {
            auto& ctx = static_cast<LayoutInlineContext&>(node);
            for (auto& c : ctx.children) shift_subtree_y(*c, dy);
            for (auto& f : ctx.lines) f.y += dy;
            break;
        }
        case LayoutKind::Text: {
            auto& t = static_cast<LayoutText&>(node);
            for (auto& f : t.lines) f.y += dy;
            break;
        }
    }
}

} // namespace honeybee::renderer::layout
