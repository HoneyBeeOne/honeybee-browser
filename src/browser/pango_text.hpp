// =============================================================
// File: pango_text.hpp
// Purpose: Pango-backed TextMeasurer and LineDrawer.
//
// measure_with_pango: measures a single string at a given font.
// draw_line_with_pango: draws one LineFragment using the
// fragment's own style (ADR-016). Called once per fragment by
// paint.cpp.
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

void draw_line_with_pango(
    cairo_t* cr,
    const renderer::layout::LineFragment& frag);

} // namespace honeybee::browser
