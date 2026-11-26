// =============================================================
// File: convert.hpp
// Purpose: Public interface of the HTML-to-DOM converter.
//
// This is the seam between lexbor and our owned DOM. Callers use
// parse(), which returns a fully-owned DOM tree; lexbor's
// document is destroyed before parse() returns.
//
// Only convert.cpp includes lexbor headers. Per ADR-007, this is
// the sole lexbor-touching file outside of third_party/.
// =============================================================

#pragma once

#include <memory>
#include <string_view>

#include "renderer/dom/node.hpp"

namespace honeybee::renderer::html {

// Parses `html` with lexbor and produces our owned DOM.
// Returns nullptr only if lexbor fails to allocate or parse.
// Malformed HTML is handled gracefully (lexbor normalizes it).
std::unique_ptr<dom::Document> parse(std::string_view html);

} // namespace honeybee::renderer::html
