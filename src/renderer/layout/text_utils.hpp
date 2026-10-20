// =============================================================
// File: text_utils.hpp
// Purpose: Pure text-processing helpers for layout (ADR-015).
//
// collapse_whitespace: CSS white-space: normal semantics
// detect_rtl:          first-strong-character direction heuristic
//
// Both are pure functions with no dependency on DOM, CSS, or
// GTK. Tests exercise them directly.
// =============================================================

#pragma once

#include <string>
#include <string_view>

namespace honeybee::renderer::layout {

// Collapses runs of space/tab/newline/CR/form-feed into a single
// space. Trims leading and trailing whitespace. U+00A0 (nbsp) is
// preserved as a non-collapsing space -- it does not match the
// ASCII whitespace set, so it survives the collapse unchanged.
std::string collapse_whitespace(std::string_view text);

// Returns true if the first strong character in `text` is in an
// RTL script (Arabic, Hebrew, Syriac, Thaana, NKo, Adlam).
// Returns false for LTR scripts (Latin, Greek, Cyrillic, etc.),
// and for text containing no strong characters (digits,
// punctuation, whitespace only) -- defaulting to LTR per the CSS
// first-strong heuristic.
bool detect_rtl(std::string_view text);

} // namespace honeybee::renderer::layout
