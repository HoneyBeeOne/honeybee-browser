// =============================================================
// File: text_utils.hpp
// Purpose: Pure text-processing helpers for layout (ADR-015).
//
// collapse_whitespace: CSS white-space: normal semantics
// detect_rtl:          first-strong-character direction heuristic
// break_into_lines:    word-level greedy line packing
//
// All are pure functions with no dependency on DOM, CSS, or
// GTK beyond the LineFragment struct (which lives in
// layout_box.hpp).
// =============================================================

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "renderer/layout/layout_box.hpp"

namespace honeybee::renderer::layout {

std::string collapse_whitespace(std::string_view text);

bool detect_rtl(std::string_view text);

// Greedy line-breaking. Given already-collapsed text, splits
// into words and packs into lines that fit within max_width.
// Each returned fragment has text, width, and height set;
// y is a cumulative offset from 0 (fragment[0].y == 0).
// x is 0 -- the caller (layout.cpp) assigns x based on rtl.
//
// Words longer than max_width are placed on their own line and
// overflow (no character-level breaking; overflow-wrap deferred).
std::vector<LineFragment> break_into_lines(
    std::string_view collapsed_text,
    std::string_view font_family,
    float font_size,
    float max_width,
    const TextMeasurer& measurer);

} // namespace honeybee::renderer::layout
