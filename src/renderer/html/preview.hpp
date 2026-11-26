// =============================================================
// File: preview.hpp
// Purpose: Produce a short, UI-ready view of an HTML document.
//
// This is the seam between the parser (renderer/html/document)
// and the browser window. The window only needs a title and a
// chunk of text; that logic lives here so it can be unit-tested
// without GTK.
// =============================================================

#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace honeybee::renderer::html {

// A short, presentation-ready view of an HTML document.
struct Preview {
    std::string title;  // <title> content, empty if none
    std::string text;   // visible text, truncated to max_text bytes
};

// Parse `html` and produce a Preview.
//
// - If parsing fails (only on allocation failure), returns an
//   empty Preview with the title "Parse error".
// - `max_text` truncates the extracted text. Truncation is
//   byte-based for now (no UTF-8 awareness); a later phase can
//   improve this. When truncation occurs, "..." is appended.
Preview make_preview(std::string_view html,
                     std::size_t max_text = 1000);

} // namespace honeybee::renderer::html
