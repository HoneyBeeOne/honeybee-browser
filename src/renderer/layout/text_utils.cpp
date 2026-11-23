#include "renderer/layout/text_utils.hpp"

#include "base/log.hpp"
#include "renderer/dom/node.hpp"

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
    const auto space_metrics = measurer(" ", font_family, font_size);
    const float space_w = space_metrics.width;
    const float line_h = space_metrics.height;

    std::string current_text;
    float current_width = 0.0f;
    float current_y = 0.0f;

    for (const auto& w : words) {
        const float ww = measurer(w, font_family, font_size).width;
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
                f.baseline = measurer(f.text, font_family, font_size).baseline;
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
        f.baseline = measurer(f.text, font_family, font_size).baseline;
        f.y = current_y;
        lines.push_back(std::move(f));
    }

    return lines;
}

std::vector<StyledLine> break_styled_words_into_lines(
    const std::vector<StyledWord>& words,
    float max_width,
    const TextMeasurer& measurer,
    float strut_ascent,
    float strut_descent) {

    std::vector<StyledLine> lines;
    if (words.empty()) return lines;

    StyledLine current;
    current.ascent = strut_ascent;
    current.descent = strut_descent;
    float cursor_x = 0.0f;

    auto finalize_line = [](StyledLine& line) {
        line.baseline = line.ascent;
        line.height = line.ascent + line.descent;
    };

    for (const auto& w : words) {
        // ADR-081: inline-block words are atomic units sized by
        // their laid-out box's outer dimensions (content + margins).
        float ww;
        float wh;
        float baseline = 0.0f;

        if (w.inline_block_box) {
            ww = w.inline_block_box->width
                 + w.inline_block_box->margin[1]
                 + w.inline_block_box->margin[3];
            wh = w.inline_block_box->height
                 + w.inline_block_box->margin[0]
                 + w.inline_block_box->margin[2];

            // ADR-082: use the last in-flow line-box baseline
            // when available. Otherwise use the bottom margin edge.
            baseline = w.has_inline_block_baseline
                ? w.inline_block_baseline
                : wh;
        } else {
            const auto metrics = measurer(w.text, w.font_family, w.font_size);
            ww = metrics.width;
            wh = metrics.height;
            baseline = metrics.baseline;
        }

        // ADR-082: positive vertical-align raises the fragment.
        // Baselines are measured downward from the fragment top,
        // so raising the fragment increases its baseline coordinate.
        const float measured_baseline = baseline;
        baseline += w.baseline_offset;

        if (log_enabled(LogCategory::InlineLayout, LogLevel::Trace)) {
            HB_LOG(InlineLayout, Trace,
                "INLINE_MEASURE element=%p text='%.96s' "
                "font='%s' size=%.2f width=%.3f height=%.3f "
                "measured_baseline=%.3f baseline_offset=%.3f "
                "used_baseline=%.3f cursor_x=%.3f",
                static_cast<const void*>(w.element), w.text.c_str(),
                w.font_family.c_str(), w.font_size, ww, wh,
                measured_baseline, w.baseline_offset,
                baseline, cursor_x);
        }

        float sep = 0.0f;
        if (!current.fragments.empty() && w.preceded_by_space) {
            sep = measurer(" ", w.font_family, w.font_size).width;
        }

        if (!current.fragments.empty() && cursor_x + sep + ww > max_width) {
            if (log_enabled(LogCategory::InlineLayout, LogLevel::Trace)) {
                HB_LOG(InlineLayout, Trace,
                    "INLINE_WRAP line=%zu cursor_x=%.3f separator=%.3f "
                    "word_width=%.3f max_width=%.3f word='%.72s'",
                    lines.size(), cursor_x, sep, ww, max_width,
                    w.text.c_str());
            }

            // Wrap: flush current line, start new
            current.width = cursor_x;
            finalize_line(current);
            lines.push_back(std::move(current));
            current = StyledLine{};
            current.ascent = strut_ascent;
            current.descent = strut_descent;
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
            last->element == w.element &&
            last->inline_ancestors == w.inline_ancestors &&
            !last->inline_block_child &&     // ADR-081: no merge across atomic
            !w.inline_block_box;

        if (log_enabled(LogCategory::InlineLayout, LogLevel::Trace)) {
            HB_LOG(InlineLayout, Trace,
                "INLINE_MERGE_DECISION line=%zu previous=%p "
                "next_element=%p merge=%d previous_text='%.64s' "
                "next_text='%.64s' previous_baseline=%.3f "
                "next_baseline=%.3f baseline_delta=%.3f "
                "previous_height=%.3f next_height=%.3f separator=%.3f",
                lines.size(), static_cast<const void*>(last),
                static_cast<const void*>(w.element),
                same_style ? 1 : 0,
                last ? last->text.c_str() : "",
                w.text.c_str(),
                last ? last->baseline : 0.0f,
                baseline,
                last ? baseline - last->baseline : 0.0f,
                last ? last->height : 0.0f, wh, sep);
        }

        if (same_style) {
            if ((baseline != last->baseline || wh != last->height) &&
                log_enabled(LogCategory::InlineLayout, LogLevel::Trace)) {
                HB_LOG(InlineLayout, Trace,
                    "INLINE_MERGE_METRIC_MISMATCH previous='%.64s' "
                    "next='%.64s' previous_baseline=%.3f "
                    "next_baseline=%.3f previous_height=%.3f next_height=%.3f",
                    last->text.c_str(), w.text.c_str(),
                    last->baseline, baseline, last->height, wh);
            }
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
            f.baseline = baseline;
            f.element = w.element;
            f.inline_block_child = w.inline_block_box;  // ADR-081
            f.inline_ancestors = w.inline_ancestors;
            f.top_aligned_ancestors = w.top_aligned_ancestors;
            f.bottom_aligned_ancestors = w.bottom_aligned_ancestors;
            current.fragments.push_back(std::move(f));
        }

        cursor_x += sep + ww;

        // ADR-082: top/bottom are resolved against the completed line
        // box, not against the baseline.
        const bool edge_aligned =
            !w.top_aligned_ancestors.empty() ||
            !w.bottom_aligned_ancestors.empty();
        if (!edge_aligned) {
            current.ascent = std::max(current.ascent, baseline);
            current.descent = std::max(
                current.descent, std::max(0.0f, wh - baseline));
        }
    }

    if (!current.fragments.empty()) {
        current.width = cursor_x;
        finalize_line(current);
        lines.push_back(std::move(current));
    }

    if (log_enabled(LogCategory::InlineLayout, LogLevel::Trace)) {
        for (std::size_t line_index = 0;
             line_index < lines.size(); ++line_index) {
            const auto& line = lines[line_index];

            HB_LOG(InlineLayout, Trace,
                "INLINE_LINE phase=line_break line=%zu fragments=%zu "
                "width=%.3f height=%.3f baseline=%.3f "
                "ascent=%.3f descent=%.3f max_width=%.3f",
                line_index, line.fragments.size(),
                line.width, line.height, line.baseline,
                line.ascent, line.descent, max_width);

            for (std::size_t fragment_index = 0;
                 fragment_index < line.fragments.size(); ++fragment_index) {
                const auto& frag = line.fragments[fragment_index];

                HB_LOG(InlineLayout, Trace,
                    "INLINE_FRAGMENT phase=line_break line=%zu fragment=%zu "
                    "element=%p text='%.96s' font='%s' size=%.2f "
                    "x=%.3f y=%.3f width=%.3f height=%.3f "
                    "baseline=%.3f inline_ancestors=%zu "
                    "top_ancestors=%zu bottom_ancestors=%zu",
                    line_index, fragment_index,
                    static_cast<const void*>(frag.element),
                    frag.text.c_str(), frag.font_family.c_str(),
                    frag.font_size, frag.x, frag.y,
                    frag.width, frag.height, frag.baseline,
                    frag.inline_ancestors.size(),
                    frag.top_aligned_ancestors.size(),
                    frag.bottom_aligned_ancestors.size());
            }
        }
    }

    return lines;
}

} // namespace honeybee::renderer::layout
