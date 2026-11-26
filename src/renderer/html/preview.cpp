// =============================================================
// File: preview.cpp
// Purpose: Implementation of make_preview().
//
// Builds on the owned DOM (via html::parse) and preserves the
// original behavior: title from <title>, text from <body> only.
// =============================================================

#include "renderer/html/preview.hpp"

#include <string>

#include "renderer/dom/node.hpp"
#include "renderer/html/convert.hpp"

namespace honeybee::renderer::html {

namespace {

std::string truncate_with_ellipsis(const std::string& text,
                                   std::size_t max_len) {
    if (text.size() <= max_len) {
        return text;
    }
    std::string out = text.substr(0, max_len);
    out += "...";
    return out;
}

} // namespace

Preview make_preview(std::string_view html, std::size_t max_text) {
    auto doc = parse(html);
    if (doc == nullptr) {
        return Preview{"Parse error", ""};
    }

    // Old behavior: text was extracted from <body> only, not the whole
    // document. Preserve that by navigating explicitly.
    const dom::Element* body = doc->find_element("body");
    const std::string text = body ? body->text_content() : std::string{};

    return Preview{
        doc->title(),
        truncate_with_ellipsis(text, max_text)
    };
}

} // namespace honeybee::renderer::html
