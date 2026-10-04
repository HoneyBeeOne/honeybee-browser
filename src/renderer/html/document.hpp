// =============================================================
// File: document.hpp
// Purpose: RAII wrapper around lexbor's HTML document.
//
// The rest of the renderer should talk to this class, not to
// lexbor directly. This keeps the lexbor dependency localized:
// only document.cpp includes lexbor headers.
// =============================================================

#pragma once

#include <memory>
#include <string>
#include <string_view>

// Forward-declare the lexbor document type. This keeps lexbor's
// C headers out of every translation unit that includes us.
struct lxb_html_document;

namespace honeybee::renderer::html {

class Document {
public:
    // Parse an HTML string. Returns nullptr only if lexbor fails to
    // allocate. Malformed HTML is handled gracefully by lexbor (per
    // the WHATWG spec) and does not cause a nullptr return.
    static std::unique_ptr<Document> parse(std::string_view html);

    ~Document();

    // Move-only: the underlying lexbor document has a single owner.
    Document(Document&&) noexcept;
    Document& operator=(Document&&) noexcept;
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;

    // Text content of the <title> element. Empty string if no title.
    std::string title() const;

    // Concatenated text of all text nodes in the <body>, in document
    // order. Whitespace is preserved as-is.
    std::string text_content() const;

private:
    explicit Document(lxb_html_document* doc) noexcept;

    lxb_html_document* doc_ = nullptr;
};

} // namespace honeybee::renderer::html
