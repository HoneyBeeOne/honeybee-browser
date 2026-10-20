// =============================================================
// File: pango_text.hpp
// Purpose: Pango-backed TextMeasurer and TextDrawer.
//
// measure_with_pango: measures a single string at a given font
// (called per-word during line breaking, per-line for height).
//
// draw_with_pango: draws a LayoutText's content via Pango.
// Currently draws `lt.text` as one line at `lt.x, lt.y`.
// Commit 3 will change this to iterate `lt.lines`.
// =============================================================

#pragma once

#include <cairo.h>

#include <string_view>
#include <utility>

#include "renderer/layout/layout_box.hpp"

namespace honeybee::browser {

std::pair<float, float> measure_with_pango(
    std::string_view text,
    std::string_view font_family,
    float font_size);

void draw_with_pango(cairo_t* cr,
                     const renderer::layout::LayoutText& t);

} // namespace honeybee::browser
