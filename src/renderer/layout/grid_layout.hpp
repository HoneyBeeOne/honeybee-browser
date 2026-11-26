#pragma once

// =============================================================
// File: grid_layout.hpp
// Purpose: CSS grid template parsing + track sizing (ADR-028).
//
// Parsing lives here; actual child placement happens in
// layout.cpp's layout_element (grid branch). Separation lets
// the parsing be tested in isolation without spinning up a
// full layout tree.
// =============================================================

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "renderer/css/cascade.hpp"
#include "renderer/layout/layout_box.hpp"
#include "renderer/layout/style_values.hpp"

namespace honeybee::renderer::layout {

// ADR-028: one track (row or column) in a grid template.
struct TrackSize {
    enum class Kind {
        Fixed,       // 12.25rem -> fixed px
        Fr,          // 1fr -> fill remaining, proportional to fr factor
        MinMaxFr,    // minmax(0, 1fr) -> like Fr but with min floor
        MinMaxFixed, // minmax(100px, 300px) -> fixed, clamped
        Auto         // min-content, max-content, auto, or unknown keyword
                     // -- natural content size (no override)
    };
    Kind kind = Kind::Fixed;
    float value = 0.0f;      // fixed px, or fr factor
    float min_value = 0.0f;  // MinMax min
    float max_value = 0.0f;  // MinMaxFixed max
};

// ADR-028: a named grid area -> cell rectangle (row/col + spans).
// All values are zero-based grid indices.
struct GridArea {
    int row = 0;
    int col = 0;
    int row_span = 1;
    int col_span = 1;
};

// ADR-028: fully parsed grid template from a container's
// ComputedStyle. Missing parts are empty.
struct GridTemplate {
    std::vector<TrackSize> columns;
    std::vector<TrackSize> rows;
    std::unordered_map<std::string, GridArea> areas;
};

// Parse the container's grid template. Returns nullopt if the
// style doesn't declare `grid-template-columns` (grid with no
// template is treated as a single-column block flow, not a grid).
std::optional<GridTemplate> parse_grid_template(
    const css::ComputedStyle& style, const LengthContext& ctx);

// Parse a track list like "15.5rem minmax(0, 1fr)". Unrecognized
// tokens are skipped (so partial parse can still work).
std::vector<TrackSize> parse_track_list(
    std::string_view text, const LengthContext& ctx);

// Parse a `grid-template-areas` value like
// `'siteNotice siteNotice' 'columnStart pageContent' 'footer footer'`
// into name -> cell rectangle. `.` cells are empty (no name).
std::unordered_map<std::string, GridArea> parse_grid_areas(
    std::string_view text);

// ADR-028: compute column widths from a track list. Fixed widths
// resolved directly; fr tracks split remaining space proportionally;
// minmax applies its min floor (fixed-px case clamped).
// Returns one width per track.
std::vector<float> compute_column_widths(
    const std::vector<TrackSize>& tracks,
    float total_width,
    float column_gap);

// ADR-028: shift the subtree rooted at `node` by `dy`. Adds dy to
// node.y AND every descendant's y (recursive). Used by grid layout
// to reposition children from their "layout at y=0" position to
// their row's Y position, once row heights are known.
void shift_subtree_y(LayoutNode& node, float dy);

// ADR-031: horizontal variant of shift_subtree_y, used by
// float layout to reposition floated children at the left or
// right edge of their block parent.
void shift_subtree_x(LayoutNode& node, float dx);

} // namespace honeybee::renderer::layout
