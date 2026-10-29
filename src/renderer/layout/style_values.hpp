// =============================================================
// File: style_values.hpp
// Purpose: Parse CSS values used by layout (per ADR-011, ADR-013).
//
// ADR-013 adds LengthContext and parse_length for px/em/rem/%.
// parse_px is kept for backward compat until Stage B (which
// switches layout over to parse_length and removes parse_px).
// =============================================================

#pragma once

#include <string_view>

#include "renderer/layout/layout_box.hpp"

namespace honeybee::renderer::layout {

// Context for resolving relative length units per ADR-013.
struct LengthContext {
    float font_size = 16.0f;            // own font-size (for em on non-font-size)
    float parent_font_size = 16.0f;     // parent font-size (for em on font-size)
    float root_font_size = 16.0f;       // root font-size (for rem)
    float parent_content_width = 0.0f;  // parent content width (for %)
    bool on_font_size_property = false; // true when resolving the font-size property itself
    // ADR-051: viewport dimensions for vh/vw/vmin/vmax units.
    // Defaults 0 preserve existing test behavior -- no test
    // currently uses viewport units, so vh/vw resolve to 0 which
    // matches the pre-ADR-051 unparsed behavior.
    float viewport_w = 0.0f;
    float viewport_h = 0.0f;
};

// Legacy: parse Npx only. Returns 0 for any other unit.
// Still used by layout until Stage B migrates to parse_length.
float parse_px(std::string_view s);

// Parse a CSS length in the given context.
// Supports px, em, rem, %. Returns 0 for auto or unknown units.
float parse_length(std::string_view s, const LengthContext& ctx);

// ADR-037: CSS math expression evaluator. Handles calc(),
// min(), max(), clamp(). Called from parse_length for values
// starting with those function names.
float eval_math_expression(std::string_view s, const LengthContext& ctx);

// True if the trimmed input is exactly "auto".
bool is_auto(std::string_view s);

// Parse a CSS color (named or hex).
Color parse_color(std::string_view s);

} // namespace honeybee::renderer::layout
