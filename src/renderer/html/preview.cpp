// =============================================================
// File: preview.cpp
// Purpose: Implementation of make_preview().
// =============================================================

#include "renderer/html/preview.hpp"

#include "renderer/html/document.hpp"

namespace honeybee::renderer::html {

namespace {

// Cut `text` to at most `max_len` bytes. If truncated, append "...".
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
    auto doc = Document::parse(html);
    if (doc == nullptr) {
        return Preview{"Parse error", ""};
    }
    return Preview{
        doc->title(),
        truncate_with_ellipsis(doc->text_content(), max_text)
    };
}

} // namespace honeybee::renderer::html
