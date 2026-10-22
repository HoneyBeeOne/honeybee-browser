// =============================================================
// File: render_state.hpp
// Purpose: Mutable presentation state for pseudo-class matching
//          (ADR-018).
//
// State is tracked here, NOT on dom::Element. The DOM stays an
// immutable tree after parse (ADR-007). State is presentation,
// and it lives on RenderPipeline.
// =============================================================

#pragma once

#include <string>
#include <unordered_set>

namespace honeybee::renderer::dom { class Element; }

namespace honeybee::renderer::css {

struct RenderState {
    std::unordered_set<std::string> visited_urls;
    const dom::Element* hovered_element = nullptr;
    float pointer_x = 0.0f;
    float pointer_y = 0.0f;

    // URL of the currently-loaded page, used to resolve relative
    // hrefs against visited_urls. Empty when no page is loaded.
    std::string base_url;
};

} // namespace honeybee::renderer::css
