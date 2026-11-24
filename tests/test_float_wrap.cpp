// =============================================================
// ADR-079: Line-by-line float wrap — unit tests.
//
// These tests exercise the LineWidthProvider seam on both text
// breakers. They do NOT touch the block layout pipeline — that's
// covered by the browser-level acceptance test (Wikipedia).
// =============================================================

#include "renderer/layout/layout_box.hpp"
#include "renderer/layout/text_utils.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace layout = honeybee::renderer::layout;

namespace {

layout::TextMetrics stub_measure(
    std::string_view text, std::string_view, float font_size) {
    return {
        static_cast<float>(text.size()) * font_size * 0.5f,
        font_size,
        font_size * 0.75f,
    };
}

// Build N identical "word" tokens (32px wide at font_size=16,
// with an 8px space between them).
std::vector<layout::StyledWord> make_words(int n) {
    std::vector<layout::StyledWord> words;
    for (int i = 0; i < n; ++i) {
        layout::StyledWord w;
        w.text = "word";
        w.font_size = 16.0f;
        w.preceded_by_space = (i > 0);
        words.push_back(std::move(w));
    }
    return words;
}

} // namespace

TEST_CASE("ADR-079: all lines above float are narrowed", "[adr079]") {
    auto words = make_words(10);
    auto provider = [](float y) -> layout::LineWidth {
        if (y < 1000.0f) return { 0.0f, 200.0f };
        return { 0.0f, 400.0f };
    };
    auto lines = layout::break_styled_words_into_lines(
        words, 400.0f, stub_measure, 12.0f, 4.0f, 0.0f, provider);
    REQUIRE(lines.size() >= 2);
    for (const auto& line : lines) {
        CHECK(line.width <= 200.0f + 0.01f);
        for (const auto& frag : line.fragments) {
            CHECK(frag.x_offset == 0.0f);
        }
    }
}

TEST_CASE("ADR-079: all lines below float use full width", "[adr079]") {
    auto words = make_words(10);
    auto provider = [](float y) -> layout::LineWidth {
        if (y < 100.0f) return { 0.0f, 200.0f };
        return { 0.0f, 400.0f };
    };
    auto lines = layout::break_styled_words_into_lines(
        words, 400.0f, stub_measure, 12.0f, 4.0f, 500.0f, provider);
    REQUIRE(lines.size() == 1);
    CHECK(lines[0].width > 200.0f);
    CHECK(lines[0].width <= 400.0f + 0.01f);
}

TEST_CASE("ADR-079: 2 narrow + 3 full line transition", "[adr079]") {
    auto words = make_words(35);
    auto provider = [](float y) -> layout::LineWidth {
        if (y < 32.0f) return { 0.0f, 200.0f };
        return { 0.0f, 400.0f };
    };
    auto lines = layout::break_styled_words_into_lines(
        words, 400.0f, stub_measure, 12.0f, 4.0f, 0.0f, provider);
    REQUIRE(lines.size() == 5);
    CHECK(lines[0].width <= 200.0f + 0.01f);
    CHECK(lines[1].width <= 200.0f + 0.01f);
    CHECK(lines[2].width > 200.0f);
    CHECK(lines[3].width > 200.0f);
}

TEST_CASE("ADR-079: left float sets x_offset on narrowed lines", "[adr079]") {
    auto words = make_words(35);
    auto provider = [](float y) -> layout::LineWidth {
        if (y < 32.0f) return { 100.0f, 300.0f };
        return { 0.0f, 400.0f };
    };
    auto lines = layout::break_styled_words_into_lines(
        words, 400.0f, stub_measure, 12.0f, 4.0f, 0.0f, provider);
    REQUIRE(lines.size() >= 2);
    for (const auto& frag : lines[0].fragments) CHECK(frag.x_offset == 100.0f);
    for (const auto& frag : lines[1].fragments) CHECK(frag.x_offset == 100.0f);
    for (const auto& frag : lines[2].fragments) CHECK(frag.x_offset == 0.0f);
}

TEST_CASE("ADR-079: null provider falls back to max_width", "[adr079]") {
    auto words = make_words(10);
    auto lines = layout::break_styled_words_into_lines(
        words, 400.0f, stub_measure, 12.0f, 4.0f);
    // 10 * 32 + 9 * 8 = 392 <= 400: fits on one line.
    REQUIRE(lines.size() == 1);
    CHECK(lines[0].width == 392.0f);
    for (const auto& frag : lines[0].fragments) CHECK(frag.x_offset == 0.0f);
}

TEST_CASE("ADR-079: plain-text path honors provider", "[adr079]") {
    std::string text = "aa bb cc dd ee ff gg hh";
    auto provider = [](float y) -> layout::LineWidth {
        if (y < 16.0f) return { 0.0f, 80.0f };
        return { 0.0f, 400.0f };
    };
    auto frags = layout::break_into_lines(
        text, "", 16.0f, 400.0f, stub_measure, 0.0f, provider);
    REQUIRE(frags.size() >= 2);
    CHECK(frags[0].width <= 80.0f + 0.01f);
    CHECK(frags.back().width <= 400.0f + 0.01f);
    for (const auto& f : frags) CHECK(f.x_offset == 0.0f);
}
