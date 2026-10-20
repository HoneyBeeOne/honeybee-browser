// =============================================================
// File: style_values.hpp
// Purpose: Parse CSS values used by layout (per ADR-011).
//
// v1 only supports single-value "Npx" for lengths and named/hex
// colors. Multi-value shorthand, rgb(), hsl(), em/rem etc. are
// deferred per ADR-011.
// =============================================================

#pragma once

#include <string_view>

#include "renderer/layout/layout_box.hpp"

namespace honeybee::renderer::layout {

// Parse a pixel length like "10px" or "-5px".
// Returns 0.0f for anything else (multi-value, em, %, malformed).
float parse_px(std::string_view s);

// Parse a CSS color.
// Supports named colors (red, navy, black, white, transparent, ...)
// and hex (#rgb, #rrggbb). Returns transparent for unknown input.
Color parse_color(std::string_view s);

} // namespace honeybee::renderer::layout
