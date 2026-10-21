// =============================================================
// File: text_utils.hpp
// Purpose: Pure text helpers for layout (ADR-015, ADR-016).
//
// - collapse_whitespace: CSS white-space: normal
// - detect_rtl: first-strong heuristic
// - break_into_lines: word-level line breaking (plain text, ADR-015)
// - break_styled_words_into_lines: word-level line breaking with
//   per-word style (used by inline formatting context, ADR-016)
// =============================================================

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "renderer/layout/layout_box.hpp"

namespace honeybee::renderer::layout {

std::string collapse_whitespace(std::string_view text);

bool detect_rtl(std::string_view text);

std::vector<LineFragment> break_into_lines(
    std::string_view collapsed_text,
    std::string_view font_family,
    float font_size,
    float max_width,
    const TextMeasurer& measurer);

// A word with its own style, used by the inline formatting context.
// preceded_by_space tracks whether a space existed in the source
// before this word (between inline elements, spaces are preserved).
struct StyledWord {
    std::string text;
    std::string font_family;
    float font_size = 16.0f;
    Color color = Color::black();
    bool preceded_by_space = false;
};

// One visual line of styled fragments. fragments are ordered
// left-to-right visually (source-order for LTR). width is the
// sum of fragment widths including separators.
struct StyledLine {
    std::vector<LineFragment> fragments;
    float width = 0.0f;
    float height = 0.0f;
};

// Greedy line breaking over styled words. Adjacent words with
// matching style on the same line are merged into a single
// fragment (so a plain paragraph produces one fragment per line,
// matching ADR-015 behavior). Fragment x/y are not set; the
// caller positions them.
std::vector<StyledLine> break_styled_words_into_lines(
    const std::vector<StyledWord>& words,
    float max_width,
    const TextMeasurer& measurer);

} // namespace honeybee::renderer::layout
