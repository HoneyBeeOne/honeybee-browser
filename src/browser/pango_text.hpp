// =============================================================
// File: pango_text.hpp
// Purpose: Pango-backed TextMeasurer and TextDrawer.
//
// Production implementations of the two callbacks defined in
// renderer/layout (TextMeasurer) and renderer/paint
// (TextDrawer). Kept outside honeybee_lib on purpose: only
// the browser executable needs Pango, and honeybee_lib must
// stay GTK/Pango-free for headless test builds.
// =============================================================

#pragma once

#include <cairo.h>

#include <utility>

#include "renderer/layout/layout_box.hpp"

namespace honeybee::browser {

// Measure `t` with Pango. Returns (width, height) in pixels.
// Uses an offscreen 1x1 image surface so no display is needed.
std::pair<float, float> measure_with_pango(
    const renderer::layout::LayoutText& t);

// Draw `t` at its layout position, using its font family,
// size, and color. Correct Arabic shaping via Pango.
void draw_with_pango(cairo_t* cr,
                     const renderer::layout::LayoutText& t);

} // namespace honeybee::browser
