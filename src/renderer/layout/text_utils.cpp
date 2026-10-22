#include "renderer/layout/text_utils.hpp"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace honeybee::renderer::layout {

namespace {

bool is_ascii_whitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

// Minimal UTF-8 decoder. Returns (codepoint, byte_length).
// On invalid sequences returns (0, 1) to skip one byte and
// continue -- the collapsed text just won't match, which is fine
// for direction detection (invalid bytes are treated as non-letters).
std::pair<std::uint32_t, std::size_t> decode_utf8(
    std::string_view s, std::size_t i) {
    if (i >= s.size()) return {0, 0};

    const auto b0 = static_cast<unsigned char>(s[i]);

    if (b0 < 0x80) return {b0, 1};
    if ((b0 & 0xE0) == 0xC0) {
        if (i + 1 >= s.size()) return {0, 1};
        const auto b1 = static_cast<unsigned char>(s[i + 1]);
        if ((b1 & 0xC0) != 0x80) return {0, 1};
        return {((b0 & 0x1F) << 6) | (b1 & 0x3F), 2};
    }
    if ((b0 & 0xF0) == 0xE0) {
        if (i + 2 >= s.size()) return {0, 1};
        const auto b1 = static_cast<unsigned char>(s[i + 1]);
        const auto b2 = static_cast<unsigned char>(s[i + 2]);
        if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80) return {0, 1};
        return {((b0 & 0x0F) << 12) | ((b1 & 0x3F) << 6) | (b2 & 0x3F), 3};
    }
    if ((b0 & 0xF8) == 0xF0) {
        if (i + 3 >= s.size()) return {0, 1};
        const auto b1 = static_cast<unsigned char>(s[i + 1]);
        const auto b2 = static_cast<unsigned char>(s[i + 2]);
        const auto b3 = static_cast<unsigned char>(s[i + 3]);
        if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80 ||
            (b3 & 0xC0) != 0x80) {
            return {0, 1};
        }
        return {((b0 & 0x07) << 18) | ((b1 & 0x3F) << 12)
               | ((b2 & 0x3F) << 6) | (b3 & 0x3F), 4};
    }

    return {0, 1};
}

bool is_rtl_script(std::uint32_t cp) {
    return (cp >= 0x0600  && cp <= 0x06FF)   // Arabic
        || (cp >= 0x0750  && cp <= 0x077F)   // Arabic Supplement
        || (cp >= 0x08A0  && cp <= 0x08FF)   // Arabic Extended-A
        || (cp >= 0xFB50  && cp <= 0xFDFF)   // Arabic Presentation Forms-A
        || (cp >= 0xFE70  && cp <= 0xFEFF)   // Arabic Presentation Forms-B
        || (cp >= 0x0590  && cp <= 0x05FF)   // Hebrew
        || (cp >= 0x0700  && cp <= 0x074F)   // Syriac
        || (cp >= 0x0780  && cp <= 0x07BF)   // Thaana
        || (cp >= 0x07C0  && cp <= 0x07FF)   // NKo
        || (cp >= 0x1E900 && cp <= 0x1E95F); // Adlam
}

bool is_ltr_letter(std::uint32_t cp) {
    return (cp >= 0x0041  && cp <= 0x005A)   // ASCII A-Z
        || (cp >= 0x0061  && cp <= 0x007A)   // ASCII a-z
        || (cp >= 0x00C0  && cp <= 0x024F)   // Latin-1 Supplement + Extended
        || (cp >= 0x0370  && cp <= 0x03FF)   // Greek
        || (cp >= 0x0400  && cp <= 0x04FF)   // Cyrillic
        || (cp >= 0x0530  && cp <= 0x058F);  // Armenian
}

} // namespace

std::string collapse_whitespace(std::string_view text) {
    std::string out;
    out.reserve(text.size());

    // last_was_space starts true so leading whitespace is trimmed
    // (the first space encountered has nothing to collapse against
    // and is dropped).
    bool last_was_space = true;
    for (char c : text) {
        if (is_ascii_whitespace(c)) {
            if (!last_was_space) {
                out.push_back(' ');
                last_was_space = true;
            }
        } else {
            out.push_back(c);
            last_was_space = false;
        }
    }

    // Trim trailing space (single, because runs already collapsed).
    if (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }

    return out;
}

bool detect_rtl(std::string_view text) {
    std::size_t i = 0;
    while (i < text.size()) {
        const auto [cp, len] = decode_utf8(text, i);
        if (len == 0) break;

        if (is_rtl_script(cp)) return true;
        if (is_ltr_letter(cp)) return false;

        i += len;
    }

    // No strong character found: default LTR.
    return false;
}

std::vector<LineFragment> break_into_lines(
    std::string_view collapsed_text,
    std::string_view font_family,
    float font_size,
    float max_width,
    const TextMeasurer& measurer) {

    std::vector<LineFragment> lines;
    if (collapsed_text.empty()) return lines;

    // Tokenize on space.
    std::vector<std::string_view> words;
    std::size_t i = 0;
    while (i < collapsed_text.size()) {
        while (i < collapsed_text.size() && collapsed_text[i] == ' ') ++i;
        if (i >= collapsed_text.size()) break;
        const std::size_t start = i;
        while (i < collapsed_text.size() && collapsed_text[i] != ' ') ++i;
        words.emplace_back(collapsed_text.substr(start, i - start));
    }
    if (words.empty()) return lines;

    // Space width and line height come from measuring once.
    const auto [space_w, line_h] =
        measurer(" ", font_family, font_size);

    std::string current_text;
    float current_width = 0.0f;
    float current_y = 0.0f;

    for (const auto& w : words) {
        const float ww = measurer(w, font_family, font_size).first;
        const float sep = current_text.empty() ? 0.0f : space_w;

        if (current_width + sep + ww <= max_width) {
            if (!current_text.empty()) current_text += ' ';
            current_text += w;
            current_width += sep + ww;
        } else {
            if (!current_text.empty()) {
                LineFragment f;
                f.text = std::move(current_text);
                f.width = current_width;
                f.height = line_h;
                f.y = current_y;
                current_y += line_h;
                lines.push_back(std::move(f));
            }
            current_text = std::string(w);
            current_width = ww;
        }
    }

    if (!current_text.empty()) {
        LineFragment f;
        f.text = std::move(current_text);
        f.width = current_width;
        f.height = line_h;
        f.y = current_y;
        lines.push_back(std::move(f));
    }

    return lines;
}

std::vector<StyledLine> break_styled_words_into_lines(
    const std::vector<StyledWord>& words,
    float max_width,
    const TextMeasurer& measurer) {

    std::vector<StyledLine> lines;
    if (words.empty()) return lines;

    StyledLine current;
    float cursor_x = 0.0f;

    for (const auto& w : words) {
        const float ww = measurer(w.text, w.font_family, w.font_size).first;
        const float wh = measurer(w.text, w.font_family, w.font_size).second;

        float sep = 0.0f;
        if (!current.fragments.empty() && w.preceded_by_space) {
            sep = measurer(" ", w.font_family, w.font_size).first;
        }

        if (!current.fragments.empty() && cursor_x + sep + ww > max_width) {
            // Wrap: flush current line, start new
            current.width = cursor_x;
            lines.push_back(std::move(current));
            current = StyledLine{};
            cursor_x = 0.0f;
            sep = 0.0f;
        }

        // Try to merge with the last fragment on this line.
        LineFragment* last = current.fragments.empty()
                                 ? nullptr
                                 : &current.fragments.back();
        const bool same_style = last &&
            last->font_family == w.font_family &&
            last->font_size == w.font_size &&
            last->color == w.color &&
            last->element == w.element;

        if (same_style) {
            if (sep > 0.0f) last->text += ' ';
            last->text += w.text;
            last->width += sep + ww;
        } else {
            // ADR-018 Commit 4b: this branch now fires more often
            // (element barriers prevent merging across inline
            // element boundaries -- e.g. "a</em> <em>b" yields
            // two fragments). When a space separates this new
            // fragment from the previous one, include it in
            // frag.text so text extraction stays correct. Visual
            // position is unchanged: leading space advances by
            // sep pixels, word starts at cursor_x + sep -- same
            // pixel span as the previous f.x = cursor_x + sep
            // convention. Keeps fragment text self-contained
            // (concatenating all fragments reconstructs the
            // original line text verbatim).
            LineFragment f;
            if (sep > 0.0f) {
                f.text = " ";
                f.text += w.text;
                f.width = sep + ww;
            } else {
                f.text = w.text;
                f.width = ww;
            }
            f.x = cursor_x;
            f.font_family = w.font_family;
            f.font_size = w.font_size;
            f.color = w.color;
            f.height = wh;
            f.element = w.element;
            current.fragments.push_back(std::move(f));
        }

        cursor_x += sep + ww;
        if (wh > current.height) current.height = wh;
    }

    if (!current.fragments.empty()) {
        current.width = cursor_x;
        lines.push_back(std::move(current));
    }

    return lines;
}

} // namespace honeybee::renderer::layout
