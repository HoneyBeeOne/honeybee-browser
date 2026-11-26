#pragma once

// =============================================================
// File: css_sources.hpp
// Purpose: Extract CSS sources from a parsed document (ADR-025).
//
// Walks the DOM in document order and returns every external
// stylesheet (<link rel="stylesheet">) and inline stylesheet
// (<style>) it finds. The window fetches the external ones,
// concatenates everything, and hands the merged CSS to
// RenderPipeline::render_with_css.
//
// Skips:
//   - <link rel="alternate stylesheet"> (user-selectable, not
//     active by default; would need a UI to activate)
//   - <link rel="preload"> without "stylesheet" (preload hint,
//     no <link rel="stylesheet"> follow-up in this document)
//   - Any <link> whose rel does NOT contain "stylesheet"
//
// Currently includes <link rel="stylesheet" media="print"> (and
// other media values) as if they were unconditional. Filtering
// by media type is deferred to a future ADR; a print-only
// stylesheet applied to screen rendering is a known v1 behavior
// difference.
// =============================================================

#include <string>
#include <vector>

#include "renderer/dom/node.hpp"

namespace honeybee::renderer::html {

struct CssSource {
    // true: <style>...</style>, content is text_content().
    // false: <link rel="stylesheet">, content is the raw href
    //        attribute value (may be relative; caller resolves).
    bool inline_style;
    std::string content;
};

// Returns every CSS source in `doc`, in document order.
// <link> hrefs are returned as-authored (no entity resolution
// performed on the caller's behalf is required by
// get_attribute("href") -- but see the implementation's
// defensive decode for compatibility with parsers that surface
// raw entities).
std::vector<CssSource> collect_css_sources(const dom::Document& doc);

} // namespace honeybee::renderer::html
