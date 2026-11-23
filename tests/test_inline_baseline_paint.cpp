// =============================================================
// ADR-082: Pixel-based rendering tests for baseline-aware
// inline layout and vertical-align.
//
// These tests exercise the real line breaker (text_utils.cpp)
// and the real paint pipeline, asserting pixel positions with
// a deterministic stub TextMeasurer (0.5em per character,
// baseline at 0.75em). They guard the acceptance criteria that
// unit-level metric tests cannot: that baseline alignment and
// vertical-align offsets land at the correct painted coordinates.
// =============================================================

#include "renderer/layout/layout_box.hpp"
#include "renderer/layout/text_utils.hpp"
#include "renderer/paint/paint.hpp"

#include <cairo.h>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace layout = honeybee::renderer::layout;
namespace paint  = honeybee::renderer::paint;

namespace {

// --- Test canvas + pixel probing (same pattern as test_paint.cpp) ---

struct Canvas {
    cairo_surface_t* surface;
    cairo_t* cr;

    Canvas(int w, int h) {
        surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
        cr = cairo_create(surface);
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_paint(cr);
    }
    ~Canvas() {
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
    }
};

struct Pixel { uint8_t a, r, g, b; };

Pixel pixel_at(cairo_surface_t* surface, int x, int y) {
    cairo_surface_flush(surface);
    auto* data = reinterpret_cast<uint32_t*>(
        cairo_image_surface_get_data(surface));
    const int stride = cairo_image_surface_get_stride(surface);
    const uint32_t px = data[y * (stride / 4) + x];
    return {
        static_cast<uint8_t>((px >> 24) & 0xff),
        static_cast<uint8_t>((px >> 16) & 0xff),
        static_cast<uint8_t>((px >>  8) & 0xff),
        static_cast<uint8_t>(px & 0xff),
    };
}

bool is_red(const Pixel& p)   { return p.r > 200 && p.g < 50 && p.b < 50 && p.a > 200; }
bool is_green(const Pixel& p) { return p.g > 100 && p.r < 50 && p.b < 50 && p.a > 200; }
bool is_blue(const Pixel& p)  { return p.b > 200 && p.r < 50 && p.g < 50 && p.a > 200; }
bool is_white(const Pixel& p) { return p.r > 200 && p.g > 200 && p.b > 200; }

// --- Deterministic stub metrics: 0.5em width/char, baseline 0.75em ---

layout::TextMetrics stub_measure(
    std::string_view text, std::string_view, float font_size) {
    return {
        static_cast<float>(text.size()) * font_size * 0.5f,
        font_size,
        font_size * 0.75f,
    };
}

// Distinct element addresses so same-style words with different
// vertical-align offsets are not merged into one fragment.
const int elem_a = 0, elem_b = 0;
const auto* el_a = reinterpret_cast<const honeybee::renderer::dom::Element*>(&elem_a);
const auto* el_b = reinterpret_cast<const honeybee::renderer::dom::Element*>(&elem_b);

// Position fragments exactly as layout.cpp does: each fragment's
// baseline sits on the line's shared baseline.
void position_lines(std::vector<layout::StyledLine>& lines) {
    float line_top = 0.0f;
    for (auto& line : lines) {
        for (auto& frag : line.fragments) {
            frag.y = line_top + (line.baseline - frag.baseline);
            frag.line_top = line_top;
        }
        line_top += line.height;
    }
}

void paint_lines(Canvas& c, std::vector<layout::StyledLine>& lines,
                 const std::function<void(cairo_t*, const layout::LineFragment&)>& drawer) {
    layout::LayoutBox root;
    root.width  = 200.0f;
    root.height = 200.0f;

    auto ctx = std::make_unique<layout::LayoutInlineContext>();
    for (auto& line : lines) {
        for (auto& frag : line.fragments) {
            ctx->lines.push_back(frag);
        }
        ctx->line_baselines.push_back(line.baseline);
    }
    root.children.push_back(std::move(ctx));

    paint::paint(c.cr, root, drawer);
}

} // namespace

// Acceptance #1, #18: text with different font sizes shares one
// baseline; the larger font expands the line box.
TEST_CASE("ADR-082: mixed font sizes share a painted baseline",
          "[adr082]") {
    std::vector<layout::StyledWord> words;
    words.push_back({"aaa", "", 16.0f, layout::Color::black(), false});
    words.push_back({"bbb", "", 24.0f, layout::Color::black(), true});

    auto lines = layout::break_styled_words_into_lines(
        words, 1000.0f, stub_measure, 12.0f, 4.0f);
    REQUIRE(lines.size() == 1);

    auto& line = lines[0];
    // ascent = max(16px baseline 12, 24px baseline 18) = 18;
    // descent = max(4, 24-18=6) = 6.
    CHECK(line.height == 24.0f);
    CHECK(line.baseline == 18.0f);
    REQUIRE(line.fragments.size() == 2);

    position_lines(lines);

    Canvas c(200, 100);
    paint_lines(c, lines, [](cairo_t* cr, const layout::LineFragment& f) {
        if (f.inline_block_child) return;
        if (f.font_size > 16.0f) cairo_set_source_rgb(cr, 1, 0, 0);
        else                     cairo_set_source_rgb(cr, 0, 0, 1);
        cairo_rectangle(cr, f.x, f.y, f.width, f.height);
        cairo_fill(cr);
    });

    CHECK(is_blue(pixel_at(c.surface, 5, 18)));
    CHECK(is_red (pixel_at(c.surface, 40, 18)));
    CHECK(is_red (pixel_at(c.surface, 40, 2)));
    CHECK(is_white(pixel_at(c.surface, 5, 2)));
    CHECK(is_blue(pixel_at(c.surface, 5, 8)));
}

// Acceptance #5: length offsets shift the fragment relative to
// the parent's baseline; positive values raise it.
TEST_CASE("ADR-082: vertical-align length offset raises fragment",
          "[adr082]") {
    std::vector<layout::StyledWord> words;
    layout::StyledWord raised;
    raised.text           = "up";
    raised.font_size      = 16.0f;
    raised.element        = el_a;
    raised.baseline_offset = 5.0f;
    words.push_back(raised);

    layout::StyledWord base;
    base.text             = "base";
    base.font_size        = 16.0f;
    base.element          = el_b;
    base.preceded_by_space = true;
    words.push_back(base);

    auto lines = layout::break_styled_words_into_lines(
        words, 1000.0f, stub_measure, 12.0f, 4.0f);
    REQUIRE(lines.size() == 1);
    CHECK(lines[0].height == 21.0f);

    position_lines(lines);

    Canvas c(200, 100);
    paint_lines(c, lines, [](cairo_t* cr, const layout::LineFragment& f) {
        if (f.inline_block_child) return;
        if (f.text.rfind("up", 0) == 0) cairo_set_source_rgb(cr, 0, 0.7, 0);
        else                            cairo_set_source_rgb(cr, 0, 0, 1);
        cairo_rectangle(cr, f.x, f.y, f.width, f.height);
        cairo_fill(cr);
    });

    CHECK(is_green(pixel_at(c.surface, 2, 0)));
    CHECK(is_green(pixel_at(c.surface, 2, 15)));
    CHECK(is_white(pixel_at(c.surface, 2, 16)));
    CHECK(is_white(pixel_at(c.surface, 30, 0)));
    CHECK(is_blue (pixel_at(c.surface, 30, 5)));
}

// Acceptance #19/#20: wrapped lines stack with per-line heights;
// fragment paint y reflects the accumulated line tops.
TEST_CASE("ADR-082: wrapped lines paint at successive line tops",
          "[adr082]") {
    std::vector<layout::StyledWord> words;
    for (int i = 0; i < 8; ++i) {
        layout::StyledWord w;
        w.text             = "w" + std::to_string(i);
        w.font_size        = 16.0f;
        w.preceded_by_space = (i > 0);
        words.push_back(w);
    }

    auto lines = layout::break_styled_words_into_lines(
        words, 50.0f, stub_measure, 12.0f, 4.0f);
    REQUIRE(lines.size() == 4);
    for (const auto& l : lines) CHECK(l.height == 16.0f);

    position_lines(lines);

    Canvas c(200, 200);
    paint_lines(c, lines, [](cairo_t* cr, const layout::LineFragment& f) {
        if (f.inline_block_child) return;
        if      (f.y < 16.0f) cairo_set_source_rgb(cr, 1, 0, 0);
        else if (f.y < 32.0f) cairo_set_source_rgb(cr, 0, 0.7, 0);
        else                  cairo_set_source_rgb(cr, 0, 0, 1);
        cairo_rectangle(cr, f.x, f.y, f.width, f.height);
        cairo_fill(cr);
    });

    CHECK(is_red  (pixel_at(c.surface, 2, 2)));
    CHECK(is_green(pixel_at(c.surface, 2, 20)));
    CHECK(is_blue (pixel_at(c.surface, 2, 34)));
    CHECK(is_white(pixel_at(c.surface, 45, 2)));
}

// Acceptance #11/#13: an inline-block contributes its selected
// baseline; the shared baseline passes through both the atomic
// box's baseline and the text baseline.
TEST_CASE("ADR-082: inline-block baseline aligns with text baseline",
          "[adr082]") {
    auto child = std::make_shared<layout::LayoutBox>();
    child->x = 0.0f; child->y = 0.0f;
    child->width  = 40.0f;
    child->height = 20.0f;
    child->background = {1.0f, 0.0f, 0.0f, 1.0f};

    std::vector<layout::StyledWord> words;
    layout::StyledWord atomic;
    atomic.text                     = "";
    atomic.font_size                = 16.0f;
    atomic.inline_block_element     = el_a;
    atomic.inline_block_box         = child;
    atomic.has_inline_block_baseline = true;
    atomic.inline_block_baseline    = 10.0f;
    words.push_back(atomic);

    layout::StyledWord txt;
    txt.text              = "txt";
    txt.font_size         = 16.0f;
    txt.element           = el_b;
    txt.preceded_by_space = true;
    words.push_back(txt);

    auto lines = layout::break_styled_words_into_lines(
        words, 1000.0f, stub_measure, 12.0f, 4.0f);
    REQUIRE(lines.size() == 1);

    auto& line = lines[0];
    // ascent = max(12, 10) = 12; descent = max(4, 20-10) = 10.
    CHECK(line.height == 22.0f);
    CHECK(line.baseline == 12.0f);

    position_lines(lines);

    Canvas c(200, 100);
    paint_lines(c, lines, [](cairo_t* cr, const layout::LineFragment& f) {
        if (f.inline_block_child) return;
        cairo_set_source_rgb(cr, 0, 0, 1);
        cairo_rectangle(cr, f.x, f.y, f.width, f.height);
        cairo_fill(cr);
    });

    CHECK(is_red  (pixel_at(c.surface, 20, 12)));
    CHECK(is_red  (pixel_at(c.surface, 20, 2)));
    CHECK(is_white(pixel_at(c.surface, 20, 0)));
    CHECK(is_white(pixel_at(c.surface, 20, 23)));
    CHECK(is_blue (pixel_at(c.surface, 52, 12)));
    CHECK(is_blue (pixel_at(c.surface, 52, 1)));
}
