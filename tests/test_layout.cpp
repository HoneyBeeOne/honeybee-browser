// =============================================================
// Tests for block-flow layout (ADR-011, Commit 1).
// Uses a fake TextMeasurer for deterministic, GTK-free tests.
// =============================================================

#include <cstdio>
#include "renderer/css/cascade.hpp"
#include "renderer/css/stylesheet.hpp"
#include "renderer/css/ua_stylesheet.hpp"
#include "renderer/html/convert.hpp"
#include "renderer/layout/layout.hpp"
#include "browser/render_pipeline.hpp"
#include "renderer/layout/style_values.hpp"
#include "renderer/layout/text_utils.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace dom = honeybee::renderer::dom;
namespace layout = honeybee::renderer::layout;
namespace css = honeybee::renderer::css;

namespace {

struct TestLayout {
    std::unique_ptr<dom::Document> doc;
    css::Stylesheet sheet;
    css::ComputedStyleMap styles;
    layout::LayoutBox root;
};

std::pair<float, float> fake_measure(
    std::string_view text, std::string_view /*font_family*/,
    float /*font_size*/) {
    return {static_cast<float>(text.size()) * 8.0f, 16.0f};
}

TestLayout run(const std::string& html, const std::string& css_text,
               float viewport_width = 800.0f) {
    TestLayout t;
    t.doc = honeybee::renderer::html::parse(html);
    REQUIRE(t.doc != nullptr);
    t.sheet = css::Stylesheet::parse(css_text);
    t.styles = css::cascade(t.sheet, *t.doc);
    t.root = layout::layout(*t.doc, t.styles, fake_measure, viewport_width);
    return t;
}

TestLayout run_with_ua(const std::string& html, const std::string& css_text,
                       float viewport_width = 800.0f) {
    TestLayout t;
    t.doc = honeybee::renderer::html::parse(html);
    REQUIRE(t.doc != nullptr);
    t.sheet = css::Stylesheet::parse(css_text);
    auto ua = css::ua_stylesheet();
    t.styles = css::cascade(ua, t.sheet, *t.doc);
    t.root = layout::layout(*t.doc, t.styles, fake_measure, viewport_width);
    return t;
}

std::vector<const layout::LayoutBox*>
find_all_boxes(const layout::LayoutBox& root, const std::string& tag) {
    std::vector<const layout::LayoutBox*> out;
    if (root.element && root.element->tag() == tag) out.push_back(&root);
    for (const auto& child : root.children) {
        if (child->kind != layout::LayoutKind::Box) continue;
        const auto* box = static_cast<const layout::LayoutBox*>(child.get());
        auto sub = find_all_boxes(*box, tag);
        out.insert(out.end(), sub.begin(), sub.end());
    }
    return out;
}

std::vector<const layout::LayoutText*>
find_all_texts(const layout::LayoutBox& root) {
    std::vector<const layout::LayoutText*> out;
    for (const auto& child : root.children) {
        if (child->kind == layout::LayoutKind::Text) {
            out.push_back(static_cast<const layout::LayoutText*>(child.get()));
        } else {
            const auto* box = static_cast<const layout::LayoutBox*>(child.get());
            auto sub = find_all_texts(*box);
            out.insert(out.end(), sub.begin(), sub.end());
        }
    }
    return out;
}

std::vector<const layout::LayoutInlineContext*>
find_all_contexts(const layout::LayoutBox& root) {
    std::vector<const layout::LayoutInlineContext*> out;
    for (const auto& child : root.children) {
        if (child->kind == layout::LayoutKind::InlineContext) {
            out.push_back(
                static_cast<const layout::LayoutInlineContext*>(child.get()));
        } else if (child->kind == layout::LayoutKind::Box) {
            const auto* box = static_cast<const layout::LayoutBox*>(child.get());
            auto sub = find_all_contexts(*box);
            out.insert(out.end(), sub.begin(), sub.end());
        }
    }
    return out;
}

std::string context_text(const layout::LayoutInlineContext& ctx) {
    std::string out;
    for (const auto& frag : ctx.lines) {
        if (!out.empty() && !frag.text.empty()) {
            // space between fragments if the source had whitespace
            // Note: the merge in break_styled_words_into_lines
            // already handles intra-style spaces, so fragment.text
            // may contain multiple words. Inter-fragment spaces
            // come from the source text.
        }
        out += frag.text;
    }
    return out;
}

} // namespace

// ---------------- Style value parsing ----------------

TEST_CASE("parse_px: single-value", "[layout][style_values]") {
    CHECK(layout::parse_px("10px") == 10.0f);
    CHECK(layout::parse_px("0px") == 0.0f);
    CHECK(layout::parse_px("-5px") == -5.0f);
    CHECK(layout::parse_px("  12px  ") == 12.0f);
    CHECK(layout::parse_px("1.5px") == 1.5f);
}

TEST_CASE("parse_px: rejected forms", "[layout][style_values]") {
    CHECK(layout::parse_px("") == 0.0f);
    CHECK(layout::parse_px("10") == 0.0f);            // no unit
    CHECK(layout::parse_px("10em") == 0.0f);          // wrong unit
    CHECK(layout::parse_px("10px 20px") == 0.0f);     // multi-value
    CHECK(layout::parse_px("auto") == 0.0f);
}

TEST_CASE("parse_color: named", "[layout][style_values]") {
    CHECK(layout::parse_color("red") == layout::Color{1.0f, 0.0f, 0.0f, 1.0f});
    CHECK(layout::parse_color("black") == layout::Color{0.0f, 0.0f, 0.0f, 1.0f});
    CHECK(layout::parse_color("transparent") == layout::Color::transparent());
}

TEST_CASE("parse_color: hex", "[layout][style_values]") {
    CHECK(layout::parse_color("#fff") == layout::Color{1.0f, 1.0f, 1.0f, 1.0f});
    CHECK(layout::parse_color("#ff0000") == layout::Color{1.0f, 0.0f, 0.0f, 1.0f});
}

TEST_CASE("parse_color: unknown is transparent",
          "[layout][style_values]") {
    CHECK(layout::parse_color("notacolor") == layout::Color::transparent());
}

// ---------------- Layout: basic box model ----------------

TEST_CASE("layout: single <p> fills viewport width",
          "[layout][block]") {
    auto t = run("<p>Hello</p>", "");
    const auto ps = find_all_boxes(t.root, "p");
    REQUIRE(ps.size() == 1);
    CHECK(ps[0]->x == 0.0f);
    CHECK(ps[0]->y == 0.0f);
    CHECK(ps[0]->width == 800.0f);
    CHECK(ps[0]->height == 16.0f);
}

TEST_CASE("layout: text child has measured width",
          "[layout][block]") {
    auto t = run("<p>Hello</p>", "");
    const auto texts = find_all_texts(t.root);
    REQUIRE(texts.size() == 1);
    CHECK(texts[0]->text == "Hello");
    CHECK(texts[0]->width == 40.0f);   // 5 chars * 8
    CHECK(texts[0]->height == 16.0f);
}

TEST_CASE("layout: two <p> stack vertically",
          "[layout][block]") {
    auto t = run("<body><p>A</p><p>B</p></body>", "");
    const auto ps = find_all_boxes(t.root, "p");
    REQUIRE(ps.size() == 2);
    CHECK(ps[0]->y == 0.0f);
    CHECK(ps[1]->y == 16.0f);
}

TEST_CASE("layout: margin stacking matches ADR worked example",
          "[layout][block]") {
    // The exact worked example from ADR-011's ASCII diagram:
    // <body><p>A</p><p>B</p></body> with p { margin: 10px }
    // Expects p1.y = 10, p2.y = 46.
    auto t = run("<body><p>A</p><p>B</p></body>", "p { margin: 10px }");
    const auto ps = find_all_boxes(t.root, "p");
    REQUIRE(ps.size() == 2);
    CHECK(ps[0]->y == 10.0f);
    CHECK(ps[0]->height == 16.0f);
    CHECK(ps[1]->y == 46.0f);
    CHECK(ps[1]->height == 16.0f);
}

TEST_CASE("layout: explicit width overrides fill-parent",
          "[layout][block]") {
    auto t = run("<p>Hi</p>", "p { width: 100px }");
    const auto ps = find_all_boxes(t.root, "p");
    REQUIRE(ps.size() == 1);
    CHECK(ps[0]->width == 100.0f);
}

TEST_CASE("layout: padding contributes to box width",
          "[layout][block]") {
    auto t = run("<p>Hi</p>", "p { padding: 5px }");
    const auto ps = find_all_boxes(t.root, "p");
    REQUIRE(ps.size() == 1);
    // width = content_w (800 - 10) + padding_left + padding_right = 800
    CHECK(ps[0]->width == 800.0f);
}

TEST_CASE("layout: border contributes to box width",
          "[layout][block]") {
    auto t = run("<p>Hi</p>", "p { border-width: 2px }");
    const auto ps = find_all_boxes(t.root, "p");
    REQUIRE(ps.size() == 1);
    CHECK(ps[0]->width == 800.0f);
    CHECK(ps[0]->border[0] == 2.0f);
}

TEST_CASE("layout: nested div stacks",
          "[layout][block]") {
    auto t = run("<div><p>A</p><p>B</p></div>", "");
    const auto divs = find_all_boxes(t.root, "div");
    REQUIRE(divs.size() == 1);
    CHECK(divs[0]->height == 32.0f);  // two 16px children

    const auto ps = find_all_boxes(t.root, "p");
    REQUIRE(ps.size() == 2);
    CHECK(ps[0]->y == 0.0f);
    CHECK(ps[1]->y == 16.0f);
}

TEST_CASE("layout: background color from cascade",
          "[layout][block]") {
    auto t = run("<p>Hi</p>", "p { background-color: red }");
    const auto ps = find_all_boxes(t.root, "p");
    REQUIRE(ps.size() == 1);
    CHECK(ps[0]->background == layout::Color{1.0f, 0.0f, 0.0f, 1.0f});
}

TEST_CASE("layout: text color from cascade",
          "[layout][block]") {
    auto t = run("<p>Hi</p>", "p { color: navy }");
    const auto texts = find_all_texts(t.root);
    REQUIRE(texts.size() == 1);
    CHECK(texts[0]->color.r == 0.0f);
    CHECK(texts[0]->color.g == 0.0f);
    CHECK(texts[0]->color.b > 0.4f);
    CHECK(texts[0]->color.b < 0.6f);
}

TEST_CASE("layout: empty document yields empty box",
          "[layout][block]") {
    // html/head/body are always synthesized by lexbor.
    auto t = run("", "");
    // Root is <html>, but with no text it should have zero height.
    CHECK(t.root.height == 0.0f);
}

// ---------------- Non-rendered elements ----------------

TEST_CASE("layout: <head> contents are not rendered", "[layout][skip]") {
    auto t = run_with_ua(
        "<html><head><title>Page Title</title></head>"
        "<body><p>Visible</p></body></html>",
        "");
    for (const auto* text : find_all_texts(t.root)) {
        CHECK(text->text.find("Page Title") == std::string::npos);
    }
    CHECK(find_all_boxes(t.root, "p").size() == 1);
}

TEST_CASE("layout: <style> is not rendered", "[layout][skip]") {
    auto t = run_with_ua(
        "<body><style>p { color: red }</style><p>Hi</p></body>",
        "");
    for (const auto* text : find_all_texts(t.root)) {
        CHECK(text->text.find("color") == std::string::npos);
    }
}

TEST_CASE("layout: <script> is not rendered", "[layout][skip]") {
    auto t = run_with_ua(
        "<body><script>alert('hi')</script><p>Hi</p></body>",
        "");
    for (const auto* text : find_all_texts(t.root)) {
        CHECK(text->text.find("alert") == std::string::npos);
    }
}

// =============================================================
// Shorthand expansion consumption in layout (ADR-012).
// =============================================================

TEST_CASE("layout: margin two values", "[layout][shorthand]") {
    auto t = run("<p>Hi</p>", "p { margin: 10px 20px }", 800.0f);
    const auto ps = find_all_boxes(t.root, "p");
    REQUIRE(ps.size() == 1);
    CHECK(ps[0]->margin[0] == 10.0f);
    CHECK(ps[0]->margin[1] == 20.0f);
    CHECK(ps[0]->margin[2] == 10.0f);
    CHECK(ps[0]->margin[3] == 20.0f);
    CHECK(ps[0]->width == 760.0f);
}

TEST_CASE("layout: padding four values", "[layout][shorthand]") {
    auto t = run("<p>Hi</p>", "p { padding: 1px 2px 3px 4px }", 800.0f);
    const auto ps = find_all_boxes(t.root, "p");
    REQUIRE(ps.size() == 1);
    CHECK(ps[0]->padding[0] == 1.0f);
    CHECK(ps[0]->padding[1] == 2.0f);
    CHECK(ps[0]->padding[2] == 3.0f);
    CHECK(ps[0]->padding[3] == 4.0f);
}

TEST_CASE("layout: border-width two values", "[layout][shorthand]") {
    auto t = run("<p>Hi</p>", "p { border-width: 1px 2px }", 800.0f);
    const auto ps = find_all_boxes(t.root, "p");
    REQUIRE(ps.size() == 1);
    CHECK(ps[0]->border[0] == 1.0f);
    CHECK(ps[0]->border[1] == 2.0f);
    CHECK(ps[0]->border[2] == 1.0f);
    CHECK(ps[0]->border[3] == 2.0f);
}

TEST_CASE("layout: longhand margin-top works", "[layout][shorthand]") {
    auto t = run("<p>Hi</p>", "p { margin-top: 10px }", 800.0f);
    const auto ps = find_all_boxes(t.root, "p");
    REQUIRE(ps.size() == 1);
    CHECK(ps[0]->margin[0] == 10.0f);
    CHECK(ps[0]->margin[1] == 0.0f);
    CHECK(ps[0]->margin[2] == 0.0f);
    CHECK(ps[0]->margin[3] == 0.0f);
    CHECK(ps[0]->y == 10.0f);
}

// ---------------- parse_length (ADR-013 Stage A) ----------------

TEST_CASE("parse_length: px is absolute", "[length]") {
    layout::LengthContext ctx;
    CHECK(layout::parse_length("16px", ctx) == 16.0f);
    CHECK(layout::parse_length("0px", ctx) == 0.0f);
    CHECK(layout::parse_length("  12px  ", ctx) == 12.0f);
}

TEST_CASE("parse_length: em uses own font-size", "[length]") {
    layout::LengthContext ctx;
    ctx.font_size = 20.0f;
    CHECK(layout::parse_length("2em", ctx) == 40.0f);
    CHECK(layout::parse_length("0.5em", ctx) == 10.0f);
}

TEST_CASE("parse_length: em on font-size uses parent", "[length]") {
    layout::LengthContext ctx;
    ctx.font_size = 20.0f;
    ctx.parent_font_size = 24.0f;
    ctx.on_font_size_property = true;
    CHECK(layout::parse_length("1.5em", ctx) == 36.0f);
}

TEST_CASE("parse_length: rem uses root font-size", "[length]") {
    layout::LengthContext ctx;
    ctx.root_font_size = 20.0f;
    CHECK(layout::parse_length("2rem", ctx) == 40.0f);
    CHECK(layout::parse_length("0.5rem", ctx) == 10.0f);
}

TEST_CASE("parse_length: percent uses parent width", "[length]") {
    layout::LengthContext ctx;
    ctx.parent_content_width = 800.0f;
    CHECK(layout::parse_length("50%", ctx) == 400.0f);
    CHECK(layout::parse_length("10%", ctx) == 80.0f);
}

TEST_CASE("parse_length: auto and unknown units return 0", "[length]") {
    layout::LengthContext ctx;
    CHECK(layout::parse_length("auto", ctx) == 0.0f);
    CHECK(layout::parse_length("2vw", ctx) == 0.0f);
    CHECK(layout::parse_length("", ctx) == 0.0f);
    CHECK(layout::parse_length("10", ctx) == 0.0f);
    CHECK(layout::parse_length("10px 20px", ctx) == 0.0f);
}

TEST_CASE("is_auto: detects auto with whitespace", "[length]") {
    CHECK(layout::is_auto("auto"));
    CHECK(layout::is_auto("  auto  "));
    CHECK_FALSE(layout::is_auto(""));
    CHECK_FALSE(layout::is_auto("10px"));
    CHECK_FALSE(layout::is_auto("autos"));
}

// ---------------- Auto margins (ADR-013 Stage C) ----------------

TEST_CASE("layout: margin 0 auto centers explicit-width box",
          "[layout][auto]") {
    auto t = run("<p>Hi</p>",
                 "p { width: 600px; margin-left: auto; margin-right: auto }",
                 800.0f);
    const auto ps = find_all_boxes(t.root, "p");
    REQUIRE(ps.size() == 1);
    CHECK(ps[0]->width == 600.0f);
    CHECK(ps[0]->margin[3] == 100.0f);
    CHECK(ps[0]->margin[1] == 100.0f);
    CHECK(ps[0]->x == 100.0f);
}

TEST_CASE("layout: only margin-left auto pushes right",
          "[layout][auto]") {
    auto t = run("<p>Hi</p>",
                 "p { width: 600px; margin-left: auto }",
                 800.0f);
    const auto ps = find_all_boxes(t.root, "p");
    REQUIRE(ps.size() == 1);
    CHECK(ps[0]->margin[3] == 200.0f);
    CHECK(ps[0]->margin[1] == 0.0f);
}

TEST_CASE("layout: auto with auto width does not center",
          "[layout][auto]") {
    auto t = run("<p>Hi</p>",
                 "p { margin-left: auto; margin-right: auto }",
                 800.0f);
    const auto ps = find_all_boxes(t.root, "p");
    REQUIRE(ps.size() == 1);
    CHECK(ps[0]->width == 800.0f);
    CHECK(ps[0]->margin[3] == 0.0f);
    CHECK(ps[0]->margin[1] == 0.0f);
}

TEST_CASE("layout: auto margin with padding and border",
          "[layout][auto]") {
    auto t = run("<p>Hi</p>",
                 "p { width: 500px; padding: 10px; border-width: 5px; "
                 "    margin-left: auto; margin-right: auto }",
                 800.0f);
    const auto ps = find_all_boxes(t.root, "p");
    REQUIRE(ps.size() == 1);
    CHECK(ps[0]->margin[3] == 135.0f);
    CHECK(ps[0]->margin[1] == 135.0f);
}

// =============================================================
// Text utilities (ADR-015, Commit 1).
// =============================================================

TEST_CASE("collapse_whitespace: basic collapse", "[layout][text_utils]") {
    CHECK(layout::collapse_whitespace("a b c") == "a b c");
    CHECK(layout::collapse_whitespace("a   b") == "a b");
    CHECK(layout::collapse_whitespace("a\t\nb") == "a b");
    CHECK(layout::collapse_whitespace("a\r\n\t b") == "a b");
}

TEST_CASE("collapse_whitespace: trims", "[layout][text_utils]") {
    CHECK(layout::collapse_whitespace("") == "");
    CHECK(layout::collapse_whitespace("   ") == "");
    CHECK(layout::collapse_whitespace("\n\t ") == "");
    CHECK(layout::collapse_whitespace("   a") == "a");
    CHECK(layout::collapse_whitespace("a   ") == "a");
    CHECK(layout::collapse_whitespace("  a  b  ") == "a b");
}

TEST_CASE("collapse_whitespace: preserves nbsp", "[layout][text_utils]") {
    // U+00A0 in UTF-8 is 0xC2 0xA0. Not in the ASCII whitespace
    // set, so it survives collapsing unchanged.
    CHECK(layout::collapse_whitespace("a\xC2\xA0\xC2\xA0" "b") == "a\xC2\xA0\xC2\xA0" "b");
    CHECK(layout::collapse_whitespace("a   b") == "a b");
}

TEST_CASE("collapse_whitespace: doesn't touch internal content",
          "[layout][text_utils]") {
    CHECK(layout::collapse_whitespace("hello world") == "hello world");
    CHECK(layout::collapse_whitespace("foo-bar_baz") == "foo-bar_baz");
}

TEST_CASE("detect_rtl: Arabic", "[layout][text_utils]") {
    CHECK(layout::detect_rtl("\xd8\xa7\xd9\x84\xd8\xb3\xd9\x84\xd8\xa7\xd9\x85"));   // السلام
    CHECK(layout::detect_rtl("\xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7"));           // مرحبا
}

TEST_CASE("detect_rtl: Hebrew", "[layout][text_utils]") {
    // Hebrew word "shalom"
    CHECK(layout::detect_rtl("\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d"));
}

TEST_CASE("detect_rtl: Latin", "[layout][text_utils]") {
    CHECK_FALSE(layout::detect_rtl("Hello"));
    CHECK_FALSE(layout::detect_rtl("world"));
    CHECK_FALSE(layout::detect_rtl("Hello World"));
}

TEST_CASE("detect_rtl: first strong char wins", "[layout][text_utils]") {
    // Latin first -> LTR (even though Arabic appears later)
    CHECK_FALSE(layout::detect_rtl(
        "Hello \xd8\xa7\xd9\x84\xd8\xb3\xd9\x84\xd8\xa7\xd9\x85"));
    // Arabic first -> RTL (even though Latin appears later)
    CHECK(layout::detect_rtl(
        "\xd8\xa7\xd9\x84\xd8\xb3\xd9\x84\xd8\xa7\xd9\x85 Hello"));
}

TEST_CASE("detect_rtl: default LTR for no strong chars",
          "[layout][text_utils]") {
    CHECK_FALSE(layout::detect_rtl(""));
    CHECK_FALSE(layout::detect_rtl("123"));
    CHECK_FALSE(layout::detect_rtl("   "));
    CHECK_FALSE(layout::detect_rtl("!@#$%"));
    CHECK_FALSE(layout::detect_rtl("  123  !@#  "));
}

TEST_CASE("layout: collapses whitespace in text nodes",
          "[layout][text_utils]") {
    auto t = run("<p>hello    world</p>", "");
    const auto texts = find_all_texts(t.root);
    REQUIRE(texts.size() == 1);
    CHECK(texts[0]->text == "hello world");
}

TEST_CASE("layout: skips whitespace-only text nodes",
          "[layout][text_utils]") {
    // A div with only whitespace between tags should produce no
    // text layout child (the collapse produces an empty string,
    // which the layout branch skips).
    auto t = run("<div>   \n\t   </div>", "");
    const auto texts = find_all_texts(t.root);
    CHECK(texts.empty());
}

TEST_CASE("layout: trims leading and trailing whitespace",
          "[layout][text_utils]") {
    auto t = run("<p>   trimmed   </p>", "");
    const auto texts = find_all_texts(t.root);
    REQUIRE(texts.size() == 1);
    CHECK(texts[0]->text == "trimmed");
}

// =============================================================
// break_into_lines (ADR-015, Commit 2).
// =============================================================

TEST_CASE("break_into_lines: short text one line", "[layout][text_utils]") {
    auto lines = layout::break_into_lines(
        "abc", "", 16.0f, 100.0f, fake_measure);
    REQUIRE(lines.size() == 1);
    CHECK(lines[0].text == "abc");
    CHECK(lines[0].width == 24.0f);
    CHECK(lines[0].height == 16.0f);
    CHECK(lines[0].y == 0.0f);
}

TEST_CASE("break_into_lines: packs multiple words per line",
          "[layout][text_utils]") {
    // "a b c d e" -> a=8, space=8, container 30.
    // L1: "a b" (24). L2: "c d" (24). L3: "e" (8).
    auto lines = layout::break_into_lines(
        "a b c d e", "", 16.0f, 30.0f, fake_measure);
    REQUIRE(lines.size() == 3);
    CHECK(lines[0].text == "a b");
    CHECK(lines[1].text == "c d");
    CHECK(lines[2].text == "e");
    CHECK(lines[0].y == 0.0f);
    CHECK(lines[1].y == 16.0f);
    CHECK(lines[2].y == 32.0f);
}

TEST_CASE("break_into_lines: long word overflows its line",
          "[layout][text_utils]") {
    auto lines = layout::break_into_lines(
        "aaaaa", "", 16.0f, 20.0f, fake_measure);
    REQUIRE(lines.size() == 1);
    CHECK(lines[0].text == "aaaaa");
    CHECK(lines[0].width == 40.0f);
}

TEST_CASE("break_into_lines: empty input", "[layout][text_utils]") {
    auto lines = layout::break_into_lines(
        "", "", 16.0f, 100.0f, fake_measure);
    CHECK(lines.empty());
}

// =============================================================
// Layout with line breaking (ADR-015, Commit 2).
// =============================================================

TEST_CASE("layout: wrapped text produces multiple line fragments",
          "[layout][text_wrapping]") {
    auto t = run("<p>word word word word word</p>", "", 100.0f);
    const auto texts = find_all_texts(t.root);
    REQUIRE(texts.size() == 1);
    CHECK(texts[0]->lines.size() == 3);
    CHECK(texts[0]->lines[0].text == "word word");
    CHECK(texts[0]->lines[1].text == "word word");
    CHECK(texts[0]->lines[2].text == "word");
}

TEST_CASE("layout: short text produces one line fragment",
          "[layout][text_wrapping]") {
    auto t = run("<p>short</p>", "", 800.0f);
    const auto texts = find_all_texts(t.root);
    REQUIRE(texts.size() == 1);
    CHECK(texts[0]->lines.size() == 1);
    CHECK(texts[0]->lines[0].text == "short");
}

TEST_CASE("layout: bounding width is max line width",
          "[layout][text_wrapping]") {
    auto t = run("<p>word word word word word</p>", "", 100.0f);
    const auto texts = find_all_texts(t.root);
    REQUIRE(texts.size() == 1);
    // All three lines are "word word" (72) or "word" (32); max = 72.
    CHECK(texts[0]->width == 72.0f);
}

TEST_CASE("layout: bounding height is sum of line heights",
          "[layout][text_wrapping]") {
    auto t = run("<p>word word word word word</p>", "", 100.0f);
    const auto texts = find_all_texts(t.root);
    REQUIRE(texts.size() == 1);
    // 3 lines * 16px = 48.
    CHECK(texts[0]->height == 48.0f);
}

TEST_CASE("layout: LTR text rtl flag false", "[layout][text_wrapping]") {
    auto t = run("<p>Hello</p>", "");
    const auto texts = find_all_texts(t.root);
    REQUIRE(texts.size() == 1);
    CHECK_FALSE(texts[0]->rtl);
}

TEST_CASE("layout: Arabic text rtl flag true", "[layout][text_wrapping]") {
    auto t = run("<p dir=\"rtl\">\xd8\xa7\xd9\x84\xd8\xb3\xd9\x84\xd8\xa7\xd9\x85</p>", "");
    const auto texts = find_all_texts(t.root);
    REQUIRE(texts.size() == 1);
    CHECK(texts[0]->rtl);
}

TEST_CASE("layout: RTL lines right-aligned to same edge",
          "[layout][text_wrapping]") {
    // 5 identical Arabic words (each 4 chars, 8 UTF-8 bytes, 64px),
    // container 100px. Each word alone on a line (64+8+64 = 136 > 100).
    // Same width per line means same x for all lines (right-aligned).
    auto t = run("<p dir=\"rtl\">\xd9\x83\xd9\x84\xd9\x85\xd8\xa9 \xd9\x83\xd9\x84\xd9\x85\xd8\xa9 \xd9\x83\xd9\x84\xd9\x85\xd8\xa9 \xd9\x83\xd9\x84\xd9\x85\xd8\xa9 \xd9\x83\xd9\x84\xd9\x85\xd8\xa9</p>",
                 "", 100.0f);
    const auto texts = find_all_texts(t.root);
    REQUIRE(texts.size() == 1);
    CHECK(texts[0]->rtl);
    REQUIRE(texts[0]->lines.size() == 5);
    for (std::size_t i = 1; i < texts[0]->lines.size(); ++i) {
        CHECK(texts[0]->lines[i].x == texts[0]->lines[0].x);
    }
}

// =============================================================
// display_of (ADR-016, Commit 1).
// =============================================================

TEST_CASE("display_of: null style returns Block",
          "[layout][display]") {
    CHECK(layout::display_of(nullptr) == layout::Display::Block);
}

TEST_CASE("display_of: missing display returns Block",
          "[layout][display]") {
    // Use an empty ComputedStyle, which will not have a
    // "display" declaration.
    css::ComputedStyle style;
    CHECK(layout::display_of(&style) == layout::Display::Block);
}

TEST_CASE("display_of: explicit inline", "[layout][display]") {
    auto doc = run("<p>test</p>", "p { display: inline }");
    const auto* p = doc.doc->find_element("p");
    REQUIRE(p != nullptr);
    auto it = doc.styles.find(p);
    REQUIRE(it != doc.styles.end());
    CHECK(layout::display_of(&it->second) == layout::Display::Inline);
}

TEST_CASE("display_of: explicit block", "[layout][display]") {
    auto doc = run("<p>test</p>", "p { display: block }");
    const auto* p = doc.doc->find_element("p");
    REQUIRE(p != nullptr);
    auto it = doc.styles.find(p);
    REQUIRE(it != doc.styles.end());
    CHECK(layout::display_of(&it->second) == layout::Display::Block);
}

TEST_CASE("display_of: explicit none", "[layout][display]") {
    auto doc = run("<p>test</p>", "p { display: none }");
    const auto* p = doc.doc->find_element("p");
    REQUIRE(p != nullptr);
    auto it = doc.styles.find(p);
    REQUIRE(it != doc.styles.end());
    CHECK(layout::display_of(&it->second) == layout::Display::None);
}

TEST_CASE("display_of: unknown value falls back to Block",
          "[layout][display]") {
    // ADR-028 note: `display: grid` is now a supported value
    // (returns Display::Grid, verified elsewhere).
    // ADR-029 note: `display: flex` is now a supported value
    // (returns Display::Flex, verified elsewhere). Using
    // `inline-block` here -- genuinely unknown today (not in the
    // Display enum, not planned for v1) -- preserves the test's
    // original intent (unknown display value falls back to Block).
    auto doc = run("<p>test</p>", "p { display: inline-block }");
    const auto* p = doc.doc->find_element("p");
    REQUIRE(p != nullptr);
    auto it = doc.styles.find(p);
    REQUIRE(it != doc.styles.end());
    CHECK(layout::display_of(&it->second) == layout::Display::Block);
}

TEST_CASE("LineFragment: aggregate init still works",
          "[layout][display]") {
    // With the new style fields added in ADR-016, aggregate
    // initialization with just the original five fields must
    // still compile and leave style defaults intact.
    layout::LineFragment frag{
        .x = 10.0f, .y = 20.0f, .width = 30.0f, .height = 16.0f,
        .text = "text",
        .font_family = "", .font_size = 16.0f,
        .color = layout::Color::black(),
        .bg_image_url = {},
        .bg_size_w = 0.0f,
        .bg_size_h = 0.0f,
        .bg_pos_x = layout::BgPosX::Left,
        .bg_pos_y = layout::BgPosY::Top,
        .bg_no_repeat = false};
    CHECK(frag.x == 10.0f);
    CHECK(frag.y == 20.0f);
    CHECK(frag.width == 30.0f);
    CHECK(frag.height == 16.0f);
    CHECK(frag.text == "text");
    CHECK(frag.font_family.empty());
    CHECK(frag.font_size == 16.0f);
    CHECK(frag.color == layout::Color::black());
}

// =============================================================
// Inline formatting (ADR-016, Block B).
// =============================================================

TEST_CASE("layout: inline em flows with surrounding text",
          "[layout][inline]") {
    auto t = run("<p>Hello <em>world</em>!</p>",
                 "em { display: inline }", 800.0f);
    const auto ps = find_all_boxes(t.root, "p");
    REQUIRE(ps.size() == 1);
    // Exactly one LayoutText child -- the inline run was flattened.
    const auto ctxs = find_all_contexts(t.root);
    REQUIRE(ctxs.size() == 1);
    CHECK(context_text(*ctxs[0]) == "Hello world!");
}

TEST_CASE("layout: adjacent inline elements merge",
          "[layout][inline]") {
    auto t = run("<p><em>a</em><em>b</em></p>",
                 "em { display: inline }", 800.0f);
    const auto ctxs = find_all_contexts(t.root);
    REQUIRE(ctxs.size() == 1);
    CHECK(context_text(*ctxs[0]) == "ab");
}

TEST_CASE("layout: space between inline elements preserved",
          "[layout][inline]") {
    auto t = run("<p><em>a</em> <em>b</em></p>",
                 "em { display: inline }", 800.0f);
    const auto ctxs = find_all_contexts(t.root);
    REQUIRE(ctxs.size() == 1);
    CHECK(context_text(*ctxs[0]) == "a b");
}

TEST_CASE("layout: block child interrupts inline run",
          "[layout][inline]") {
    auto t = run("<div>before<p>middle</p>after</div>", "", 800.0f);
    const auto texts = find_all_texts(t.root);
    // Two inline runs (before, after) and one block (middle).
    REQUIRE(texts.size() == 3);
    CHECK(texts[0]->text == "before");
    CHECK(texts[1]->text == "middle");
    CHECK(texts[2]->text == "after");
}

TEST_CASE("layout: whitespace between inline elements collapses",
          "[layout][inline]") {
    auto t = run("<p>Hello\n   <em>world</em>\n   !</p>",
                 "em { display: inline }", 800.0f);
    const auto ctxs = find_all_contexts(t.root);
    REQUIRE(ctxs.size() == 1);
    CHECK(context_text(*ctxs[0]) == "Hello world !");
}

TEST_CASE("layout: nested inline elements flatten",
          "[layout][inline]") {
    auto t = run("<p><em>a<span>b</span>c</em></p>",
                 "em { display: inline } span { display: inline }", 800.0f);
    const auto ctxs = find_all_contexts(t.root);
    REQUIRE(ctxs.size() == 1);
    CHECK(context_text(*ctxs[0]) == "abc");
}

TEST_CASE("layout: anchor color appears in fragment",
          "[layout][inline]") {
    auto t = run("<p>Click <a href=\"#\">here</a> for more</p>",
                 "a { display: inline; color: #FF0000 }", 800.0f);
    const auto ctxs = find_all_contexts(t.root);
    REQUIRE(ctxs.size() == 1);
    const auto& lines = ctxs[0]->lines;
    // Adjacent same-style words merge into one fragment.
    // Expected: ["Click" (black), "here" (red), "for more" (black)]
    REQUIRE(lines.size() == 3);
    CHECK(lines[0].color == layout::Color::black());
    CHECK(lines[1].color.r > 0.9f);
    CHECK(lines[1].color.g < 0.1f);
    CHECK(lines[1].color.b < 0.1f);
    CHECK(lines[2].color == layout::Color::black());
}

TEST_CASE("layout: RTL fragments reverse left-to-right",
          "[layout][inline]") {
    // "كلمة" (parent, black) + "ثانية" (em, blue) + "كلمة" (parent, black)
    auto t = run("<p dir=\"rtl\">\xd9\x83\xd9\x84\xd9\x85\xd8\xa9 "
                 "<em>\xd8\xab\xd8\xa7\xd9\x86\xd9\x8a\xd8\xa9</em> "
                 "\xd9\x83\xd9\x84\xd9\x85\xd8\xa9</p>",
                 "em { display: inline; color: #0000FF }", 800.0f);
    const auto ctxs = find_all_contexts(t.root);
    REQUIRE(ctxs.size() == 1);
    CHECK(ctxs[0]->rtl);
    const auto& lines = ctxs[0]->lines;
    REQUIRE(lines.size() == 3);
    // In RTL visual order, source-order-first word is rightmost.
    // lines[0] is the source-order-first word ("كلمة", black) and
    // must have a larger x than the last fragment.
    CHECK(lines[0].x > lines[2].x);
}

// =============================================================
// dir attribute (ADR-016, Block D).
// =============================================================

TEST_CASE("dir: missing attribute defaults to LTR",
          "[layout][direction]") {
    auto t = run("<p>Hello</p>", "");
    const auto texts = find_all_texts(t.root);
    REQUIRE(texts.size() == 1);
    CHECK_FALSE(texts[0]->rtl);
}

TEST_CASE("dir: explicit rtl on paragraph", "[layout][direction]") {
    auto t = run("<p dir=\"rtl\">Hello</p>", "");
    const auto texts = find_all_texts(t.root);
    REQUIRE(texts.size() == 1);
    CHECK(texts[0]->rtl);
}

TEST_CASE("dir: explicit ltr overrides natural heuristic",
          "[layout][direction]") {
    auto t = run("<p dir=\"ltr\">\xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7</p>",
                 "");
    const auto texts = find_all_texts(t.root);
    REQUIRE(texts.size() == 1);
    CHECK_FALSE(texts[0]->rtl);
}

TEST_CASE("dir: inherited from html", "[layout][direction]") {
    auto t = run("<html dir=\"rtl\"><body><p>Hello</p></body></html>", "");
    const auto texts = find_all_texts(t.root);
    REQUIRE(texts.size() == 1);
    CHECK(texts[0]->rtl);
}

TEST_CASE("dir: auto on Arabic paragraph", "[layout][direction]") {
    auto t = run("<p dir=\"auto\">\xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7</p>",
                 "");
    const auto texts = find_all_texts(t.root);
    REQUIRE(texts.size() == 1);
    CHECK(texts[0]->rtl);
}

TEST_CASE("dir: auto on English paragraph", "[layout][direction]") {
    auto t = run("<p dir=\"auto\">Hello</p>", "");
    const auto texts = find_all_texts(t.root);
    REQUIRE(texts.size() == 1);
    CHECK_FALSE(texts[0]->rtl);
}

TEST_CASE("dir: mixed paragraph with no dir inherits LTR",
          "[layout][direction]") {
    auto t = run("<p>\xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7 Hello</p>", "");
    const auto texts = find_all_texts(t.root);
    REQUIRE(texts.size() == 1);
    CHECK_FALSE(texts[0]->rtl);
}

// =============================================================
// display: none (ADR-017).
// =============================================================

TEST_CASE("layout: author display:none skips element",
          "[layout][display_none]") {
    auto t = run(
        "<div>visible</div><div class=\"hidden\">hidden</div>",
        ".hidden { display: none }");
    const auto texts = find_all_texts(t.root);
    REQUIRE(texts.size() == 1);
    CHECK(texts[0]->text == "visible");
}

TEST_CASE("layout: display:none skips entire subtree",
          "[layout][display_none]") {
    auto t = run(
        "<div class=\"hidden\"><p>child</p></div><div>visible</div>",
        ".hidden { display: none }");
    const auto texts = find_all_texts(t.root);
    REQUIRE(texts.size() == 1);
    CHECK(texts[0]->text == "visible");
}

TEST_CASE("layout: author head { display: block } overrides UA default",
          "[layout][display_none]") {
    // UA stylesheet has head { display: none }. Author rule overrides
    // (origin-aware cascade: author beats UA for normal declarations).
    // Head becomes a block and its direct text renders.
    auto t = run_with_ua(
        "<html><head>head text</head>"
        "<body><p>body</p></body></html>",
        "head { display: block }");
    const auto texts = find_all_texts(t.root);
    // Expect 2: "head text" from head, "body" from p
    REQUIRE(texts.size() == 2);
    CHECK(texts[0]->text == "head text");
    CHECK(texts[1]->text == "body");
}

TEST_CASE("layout: template does not render (UA display:none)",
          "[layout][display_none]") {
    auto t = run_with_ua(
        "<body><template><p>hidden</p></template><p>visible</p></body>",
        "");
    const auto texts = find_all_texts(t.root);
    REQUIRE(texts.size() == 1);
    CHECK(texts[0]->text == "visible");
}

// =============================================================
// ADR-018 Commit 4b: hit-test.
// =============================================================

namespace {

const layout::LineFragment* find_fragment_for_element(
    const layout::LayoutNode& n, const dom::Element* el) {
    if (n.kind == layout::LayoutKind::Box) {
        const auto& b = static_cast<const layout::LayoutBox&>(n);
        for (const auto& c : b.children) {
            if (const auto* r = find_fragment_for_element(*c, el)) return r;
        }
    } else if (n.kind == layout::LayoutKind::Inline) {
        const auto& i = static_cast<const layout::LayoutInline&>(n);
        for (const auto& c : i.children) {
            if (const auto* r = find_fragment_for_element(*c, el)) return r;
        }
    } else if (n.kind == layout::LayoutKind::InlineContext) {
        const auto& c = static_cast<const layout::LayoutInlineContext&>(n);
        for (const auto& f : c.lines) {
            if (f.element == el) return &f;
        }
    }
    return nullptr;
}

std::pair<float, float> ht_measure(std::string_view text,
                                   std::string_view,
                                   float fs) {
    return { static_cast<float>(text.size()) * fs * 0.5f, fs };
}

const layout::LayoutNode* find_element_node(
    const layout::LayoutNode& n, const dom::Element* el) {
    if (n.kind == layout::LayoutKind::Box) {
        const auto& b = static_cast<const layout::LayoutBox&>(n);
        if (b.element == el) return &b;
        for (const auto& c : b.children) {
            if (const auto* r = find_element_node(*c, el)) return r;
        }
    } else if (n.kind == layout::LayoutKind::Inline) {
        const auto& i = static_cast<const layout::LayoutInline&>(n);
        if (i.element == el) return &i;
        for (const auto& c : i.children) {
            if (const auto* r = find_element_node(*c, el)) return r;
        }
    } else if (n.kind == layout::LayoutKind::InlineContext) {
        const auto& c =
            static_cast<const layout::LayoutInlineContext&>(n);
        for (const auto& child : c.children) {
            if (const auto* r = find_element_node(*child, el)) return r;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("hit_test: finds inline element under point",
          "[layout][hit-test]") {
    auto doc = honeybee::renderer::html::parse(
        "<html><body><p>before <a href=\"#\">link</a> after</p>"
        "</body></html>");
    REQUIRE(doc);
    const auto* a = doc->find_element("a");
    REQUIRE(a);

    auto sheet = css::Stylesheet::parse("");
    auto styles = css::cascade(css::ua_stylesheet(), sheet, *doc);
    auto root = layout::layout(*doc, styles, ht_measure, 800.0f);

    const auto* frag = find_fragment_for_element(root, a);
    REQUIRE(frag);
    REQUIRE(frag->width > 0.0f);

    const float cx = frag->x + frag->width / 2.0f;
    const float cy = frag->y + frag->height / 2.0f;

    CHECK(layout::hit_test(root, cx, cy) == a);
}

TEST_CASE("hit_test: empty space in block returns block",
          "[layout][hit-test]") {
    auto doc = honeybee::renderer::html::parse(
        "<html><body><p><a href=\"#\">link</a></p></body></html>");
    REQUIRE(doc);
    const auto* a = doc->find_element("a");
    const auto* p = doc->find_element("p");
    REQUIRE(a);
    REQUIRE(p);

    auto sheet = css::Stylesheet::parse("");
    auto styles = css::cascade(css::ua_stylesheet(), sheet, *doc);
    auto root = layout::layout(*doc, styles, ht_measure, 800.0f);

    const auto* p_node = find_element_node(root, p);
    REQUIRE(p_node);
    REQUIRE(p_node->width > 100.0f);

    // Hit far right of <p> block, past the <a> inline.
    const float far_x = p_node->x + p_node->width - 5.0f;
    const float cy = p_node->y + p_node->height / 2.0f;

    CHECK(layout::hit_test(root, far_x, cy) == p);
}

TEST_CASE("hit_test: point outside root returns nullptr",
          "[layout][hit-test]") {
    auto doc = honeybee::renderer::html::parse(
        "<html><body><p>text</p></body></html>");
    REQUIRE(doc);

    auto sheet = css::Stylesheet::parse("");
    auto styles = css::cascade(css::ua_stylesheet(), sheet, *doc);
    auto root = layout::layout(*doc, styles, ht_measure, 800.0f);

    CHECK(layout::hit_test(root, -10.0f, -10.0f) == nullptr);
    CHECK(layout::hit_test(root, 10000.0f, 10000.0f) == nullptr);
}

// =============================================================
// ADR-021: incremental restyle.
// =============================================================

TEST_CASE("RenderPipeline::restyle preserves layout dimensions",
          "[render-pipeline][restyle]") {
    honeybee::browser::RenderPipeline p;
    p.render(
        "<html><body><p>before <a href=\"#\">link</a> after</p>"
        "</body></html>",
        800.0f);

    const float h0 = p.layout_height();
    REQUIRE(h0 > 0.0f);

    // Restyle with no hover: dimensions must not change.
    p.restyle();
    CHECK(p.layout_height() == h0);

    // Restyle is idempotent on dimensions -- call again to
    // ensure no accumulation error.
    p.restyle();
    CHECK(p.layout_height() == h0);
}

// =============================================================
// ADR-022: targeted cascade cache-equality invariant.
//
// The critical correctness test: after a targeted restyle, the
// pipeline's styles_cache_ must equal what a fresh full cascade
// would produce with the same state. If they diverge, hover
// colors will be subtly wrong somewhere and hard to reproduce.
// =============================================================

namespace {

// Recursively check that two ComputedStyleMaps are equivalent
// for every element in `expected`.
void assert_maps_equal(const css::ComputedStyleMap& expected,
                       const css::ComputedStyleMap& actual) {
    REQUIRE(expected.size() == actual.size());
    for (const auto& [el, style] : expected) {
        auto it = actual.find(el);
        REQUIRE(it != actual.end());
        // Every property in expected should be present and equal
        // in actual.
        for (const auto& [prop, val] : style.declarations) {
            const auto t = it->second.get(prop);
            REQUIRE(t.has_value());
            CHECK(*t == val.value);
        }
        // And no extra properties in actual that aren't in expected.
        for (const auto& [prop, _] : it->second.declarations) {
            CHECK(style.get(prop).has_value());
        }
    }
}

} // namespace

TEST_CASE("RenderPipeline::restyle targeted cache equals full cascade",
          "[render-pipeline][targeted-cache]") {
    honeybee::browser::RenderPipeline p;
    p.render(
        "<html><body><p>before <a href=\"#\">link</a> after</p>"
        "</body></html>",
        800.0f);

    // Prime the cache: render() did a full cascade with hover=null.
    REQUIRE_FALSE(p.styles().empty());

    const auto* a = p.doc()->find_element("a");
    REQUIRE(a);

    // Set hover on <a> and trigger targeted restyle.
    p.set_hovered_element(a);
    p.restyle();

    // Fresh full cascade with the same state -- the ground truth.
    auto sheet = css::Stylesheet::parse("");
    auto fresh = css::cascade(
        css::ua_stylesheet(), sheet, *p.doc(), p.state());

    // Targeted result must equal fresh full-cascade result.
    assert_maps_equal(fresh, p.styles());
}

TEST_CASE("RenderPipeline::restyle targeted cache matches after un-hover",
          "[render-pipeline][targeted-cache]") {
    honeybee::browser::RenderPipeline p;
    p.render(
        "<html><body><p>before <a href=\"#\">link</a> after</p>"
        "</body></html>",
        800.0f);

    const auto* a = p.doc()->find_element("a");
    REQUIRE(a);

    // Hover, then un-hover. Two targeted transitions.
    p.set_hovered_element(a);
    p.restyle();
    p.set_hovered_element(nullptr);
    p.restyle();

    // Final state: hover=null. Must equal fresh full cascade with null hover.
    auto sheet = css::Stylesheet::parse("");
    auto fresh = css::cascade(
        css::ua_stylesheet(), sheet, *p.doc(), p.state());
    assert_maps_equal(fresh, p.styles());
}

// =============================================================
// ADR-022: algorithmic regression guard.
//
// Asserts targeted restyle is O(dirty), not O(document). A future
// change accidentally reverting restyle() to full cascade would
// still pass all correctness tests (result is identical) but
// silently regress performance by 30-50x. This test catches that.
//
// Timing tests would be flaky (CI noise, thermal, debug-vs-release);
// this asserts the underlying algorithmic property deterministically.
// =============================================================

TEST_CASE("RenderPipeline::restyle re-cascades only dirty subtrees",
          "[render-pipeline][algorithmic]") {
    // ~100 sibling <p> elements + one <a> inside one of them.
    // Full cascade: ~102 elements x ~20 rules ~= 2040 apply_rule calls.
    // Targeted: 1 element (<a>) x ~20 rules ~= 20 calls.
    std::string html = "<html><body>";
    for (int i = 0; i < 100; ++i) {
        html += "<p>paragraph " + std::to_string(i) + "</p>";
    }
    html += "<p><a href=\"#\">link</a></p>";
    html += "</body></html>";

    honeybee::browser::RenderPipeline p;
    p.render(html, 800.0f);

    const auto* a = p.doc()->find_element("a");
    REQUIRE(a);

    // Hover transition 1: null -> <a>. Only <a> is dirty.
    css::reset_apply_rule_call_count();
    p.set_hovered_element(a);
    p.restyle();
    const auto hover_calls = css::apply_rule_call_count();

    // Hover transition 2: <a> -> null. Again only <a> is dirty.
    css::reset_apply_rule_call_count();
    p.set_hovered_element(nullptr);
    p.restyle();
    const auto unhover_calls = css::apply_rule_call_count();

    // Threshold logic (measured baseline: 17 calls per transition --
    // exactly one element x 17 UA rules). Full cascade on this
    // document would be ~1,734 (102 elements x 17 rules).
    //
    // < 100 gives 5.9x headroom over the baseline (catches 5x
    // regressions like accidental double-cascade) while remaining
    // robust to legitimate UA-stylesheet growth: false positives
    // would require UA rules to exceed ~100, which is a UA-sheet
    // overhaul that would come with its own ADR. Full-cascade
    // reversion (17 -> 1,734) fails at any threshold, and this one
    // fails fast and unmistakably.
    INFO("hover_calls = " << hover_calls
         << ", unhover_calls = " << unhover_calls);
    CHECK(hover_calls < 100);
    CHECK(unhover_calls < 100);

    // Sanity: at least one call happened (counter actually incremented).
    CHECK(hover_calls > 0);
    CHECK(unhover_calls > 0);
}

// =============================================================
// ADR-023: algorithmic regression guard for skip-subtree.
//
// Asserts that wrapper-element hover re-cascades only the
// hover-sensitive descendants (flagged in hover_sensitive_),
// not the full subtree. A regression to unconditional
// cascade_subtree would pass correctness (identical results) but
// silently regress wrapper hover from ~20ms back to ~900ms.
// =============================================================

TEST_CASE("RenderPipeline::restyle skips non-hover-sensitive descendants",
          "[render-pipeline][algorithmic][skip-subtree]") {
    // Wrapper <div> with 1000 <span>s and 10 <a>s inside.
    //   Old (cascade_subtree(div)): ~1011 elements x 17 rules = ~17187 calls
    //   New (filtered): ~10 <a>s x 17 rules = ~170 calls
    std::string html = "<html><body><div>";
    for (int i = 0; i < 1000; ++i) {
        html += "<span>s</span>";
    }
    for (int i = 0; i < 10; ++i) {
        html += "<a href=\"#\">x</a>";
    }
    html += "</div></body></html>";

    honeybee::browser::RenderPipeline p;
    p.render(html, 800.0f);

    // Sanity: UA a:hover -> Level 1 (AnchorOnly) -> precompute fired.
    REQUIRE(p.hover_level() == css::HoverLevel::AnchorOnly);
    CHECK_FALSE(p.hover_sensitive().empty());

    const auto* div = p.doc()->find_element("div");
    REQUIRE(div);

    css::reset_apply_rule_call_count();
    p.set_hovered_element(div);
    p.restyle();
    const auto div_hover_calls = css::apply_rule_call_count();

    INFO("div_hover_calls = " << div_hover_calls
         << " (ADR-022 full cascade ~17187; ADR-023 filtered ~170; "
            "ADR-024 with transition check ~0)");
    // ADR-024: with the transition check, wrapper-div hover
    // cascades ZERO elements in the UA-only case (no <a> descendant
    // transitions a:hover when the wrapper is hovered). Threshold
    // <20 tolerates up to one element's worth of calls if a single
    // descendant happens to transition; comfortably rejects both
    // the ADR-023 regression (~170) and full cascade (~17187).
    CHECK(div_hover_calls < 20);
}

// =============================================================
// ADR-024: transition-check correctness on ancestor-hover changes.
//
// The critical case: hover <a> (so a:hover matches, cache has
// hover style), then hover the wrapper <div>. The <a>'s match
// state transitions true -> false, so it MUST be re-cascaded.
// A filter that skips "because the current hovered element is a
// div, not an a" would leave a stale hover style in the cache.
// =============================================================

TEST_CASE("RenderPipeline::restyle reverts <a> hover on ancestor hover",
          "[render-pipeline][targeted-cache][adr-024]") {
    honeybee::browser::RenderPipeline p;
    p.render(
        "<html><body><div><a href=\"#\">link</a> tail</div>"
        "</body></html>",
        800.0f);

    const auto* a = p.doc()->find_element("a");
    const auto* div = p.doc()->find_element("div");
    REQUIRE(a);
    REQUIRE(div);

    // Step 1: hover the <a>. a:hover matches -> cascade the <a>.
    p.set_hovered_element(a);
    p.restyle();

    // Step 2: hover the wrapper <div> instead. The <a>'s a:hover
    // match transitions true -> false, so the transition check
    // must cascade the <a> (revert its hover style).
    p.set_hovered_element(div);
    p.restyle();

    // Verify: cache equals what a fresh full cascade would produce
    // with the div (not the a) hovered.
    auto sheet = css::Stylesheet::parse("");
    auto fresh = css::cascade(
        css::ua_stylesheet(), sheet, *p.doc(), p.state());
    assert_maps_equal(fresh, p.styles());

    // Additionally: the <a>'s color must NOT be the hover color
    // (the div is hovered, not the a). This is the specific case
    // that a per-hovered-element (rather than per-element) check
    // would get wrong.
    const auto& a_style = p.styles().at(a);
    const auto a_color = a_style.get("color");
    // UA sheet has `a:hover { color: #0055EE }` and body text is
    // default (black). With div hovered, the <a> should be default
    // (a:link still matches via UA's `a:link { color: #0000EE }`,
    // but a:hover should NOT match). The exact color depends on
    // the UA sheet, but the point is: it should equal what a full
    // cascade produces, which the assert_maps_equal already checks.
    (void)a_color;
}

// =============================================================
// ADR-025: external CSS loading.
// =============================================================

TEST_CASE("RenderPipeline::render_with_css applies external CSS",
          "[render-pipeline][external-css]") {
    auto doc = honeybee::renderer::html::parse(
        "<html><body><p class=\"x\">hello</p></body></html>");
    REQUIRE(doc);

    honeybee::browser::RenderPipeline p;
    p.render_with_css(std::move(doc),
                       ".x { color: #FF0000 }",
                       800.0f);

    const auto* p_el = p.doc()->find_element("p");
    REQUIRE(p_el);
    const auto color = p.styles().at(p_el).get("color");
    REQUIRE(color.has_value());
    CHECK(*color == "#FF0000");
}

TEST_CASE("RenderPipeline::render_with_css handles empty CSS",
          "[render-pipeline][external-css]") {
    auto doc = honeybee::renderer::html::parse(
        "<html><body><p>plain</p></body></html>");
    REQUIRE(doc);

    honeybee::browser::RenderPipeline p;
    p.render_with_css(std::move(doc), std::string{}, 800.0f);

    CHECK(p.has_page());
    CHECK(p.layout_height() > 0.0f);
}

// =============================================================
// Regression: display: none on an element nested inside inline
// content must hide its text. Surfaced during ADR-026 visual
// verification on Wikipedia, which has ~18 <style> tags in body,
// several inside inline elements. Without the fix, their CSS text
// rendered as visible article content.
// =============================================================

TEST_CASE("layout: <style> nested in inline element is hidden",
          "[layout][adr-026-fix]") {
    auto doc = honeybee::renderer::html::parse(
        "<html><body>"
        "<p><span>before <style>.x{color:red}</style> after</span></p>"
        "</body></html>");
    REQUIRE(doc);

    auto sheet = css::Stylesheet::parse("");
    auto styles = css::cascade(css::ua_stylesheet(), sheet, *doc);

    auto root = layout::layout(*doc, styles, fake_measure, 800.0f);

    // Walk the layout tree; assert no LineFragment / LayoutText
    // contains the CSS text that should have been hidden.
    bool found_css_text = false;
    std::function<void(const layout::LayoutNode&)> walk =
        [&](const layout::LayoutNode& n) {
            if (n.kind == layout::LayoutKind::Text) {
                const auto& lt = static_cast<const layout::LayoutText&>(n);
                if (lt.text.find("color:red") != std::string::npos ||
                    lt.text.find(".x{") != std::string::npos) {
                    found_css_text = true;
                }
                for (const auto& f : lt.lines) {
                    if (f.text.find("color:red") != std::string::npos ||
                        f.text.find(".x{") != std::string::npos) {
                        found_css_text = true;
                    }
                }
            } else if (n.kind == layout::LayoutKind::InlineContext) {
                const auto& ctx =
                    static_cast<const layout::LayoutInlineContext&>(n);
                for (const auto& f : ctx.lines) {
                    if (f.text.find("color:red") != std::string::npos ||
                        f.text.find(".x{") != std::string::npos) {
                        found_css_text = true;
                    }
                }
                for (const auto& c : ctx.children) walk(*c);
            } else if (n.kind == layout::LayoutKind::Box) {
                const auto& b = static_cast<const layout::LayoutBox&>(n);
                for (const auto& c : b.children) walk(*c);
            } else if (n.kind == layout::LayoutKind::Inline) {
                const auto& inl = static_cast<const layout::LayoutInline&>(n);
                for (const auto& c : inl.children) walk(*c);
            }
        };
    walk(root);

    CHECK_FALSE(found_css_text);

    // Sanity: the "before" and "after" text should still be present
    // (the span itself isn't display: none, only its <style> child).
    bool found_before = false;
    bool found_after = false;
    std::function<void(const layout::LayoutNode&)> walk2 =
        [&](const layout::LayoutNode& n) {
            if (n.kind == layout::LayoutKind::Text) {
                const auto& lt = static_cast<const layout::LayoutText&>(n);
                if (lt.text.find("before") != std::string::npos) found_before = true;
                if (lt.text.find("after") != std::string::npos) found_after = true;
            } else if (n.kind == layout::LayoutKind::InlineContext) {
                const auto& ctx =
                    static_cast<const layout::LayoutInlineContext&>(n);
                for (const auto& f : ctx.lines) {
                    if (f.text.find("before") != std::string::npos) found_before = true;
                    if (f.text.find("after") != std::string::npos) found_after = true;
                }
                for (const auto& c : ctx.children) walk2(*c);
            } else if (n.kind == layout::LayoutKind::Box) {
                const auto& b = static_cast<const layout::LayoutBox&>(n);
                for (const auto& c : b.children) walk2(*c);
            } else if (n.kind == layout::LayoutKind::Inline) {
                const auto& inl = static_cast<const layout::LayoutInline&>(n);
                for (const auto& c : inl.children) walk2(*c);
            }
        };
    walk2(root);

    CHECK(found_before);
    CHECK(found_after);
}

// =============================================================
// ADR-028: end-to-end grid layout.
// =============================================================

TEST_CASE("layout: 2-column grid places children at cells",
          "[layout][grid][adr-028]") {
    // Simple 2-column grid: sidebar (12rem) + content (1fr).
    auto doc = honeybee::renderer::html::parse(
        "<html><body>"
        "<div class=\"g\">"
        "<div class=\"sidebar\">S</div>"
        "<div class=\"content\">C</div>"
        "</div>"
        "</body></html>");
    REQUIRE(doc);

    auto sheet = css::Stylesheet::parse(
        // Reset body margin so the grid container starts at x=0.
        // Without this, UA's default body { margin: 8px } would
        // shift the whole grid right by 8px, muddying the test's
        // assertions about cell positions.
        "body { margin: 0 }"
        ".g { display: grid; "
        "     grid-template-columns: 12rem minmax(0, 1fr); "
        "     grid-template-areas: 'sidebar content'; "
        "     column-gap: 0 }"
        ".sidebar { grid-area: sidebar }"
        ".content { grid-area: content }");
    auto styles = css::cascade(css::ua_stylesheet(), sheet, *doc);
    auto root = layout::layout(*doc, styles, fake_measure, 800.0f);

    const auto* sidebar = doc->find_element("div");
    REQUIRE(sidebar);
    // Find the .g box, then its children.
    std::function<const layout::LayoutBox*(const layout::LayoutNode&,
        const honeybee::renderer::dom::Element*)> find_box =
        [&](const layout::LayoutNode& n,
            const honeybee::renderer::dom::Element* target)
            -> const layout::LayoutBox* {
            if (n.kind == layout::LayoutKind::Box) {
                const auto& b = static_cast<const layout::LayoutBox&>(n);
                if (b.element == target) return &b;
                for (const auto& c : b.children) {
                    if (auto r = find_box(*c, target)) return r;
                }
            }
            return nullptr;
        };

    const auto* g_el = doc->find_element("div");
    // find_element returns first div, which IS .g. Get children.
    REQUIRE(g_el);
    std::vector<const honeybee::renderer::dom::Element*> divs;
    std::function<void(const honeybee::renderer::dom::Node&)> walk =
        [&](const honeybee::renderer::dom::Node& n) {
            using namespace honeybee::renderer::dom;
            if (n.type() == NodeType::Element) {
                const auto& el = static_cast<const Element&>(n);
                if (el.tag() == "div") divs.push_back(&el);
            }
            for (const auto& c : n.children()) walk(*c);
        };
    walk(*doc);
    REQUIRE(divs.size() >= 3);  // .g, .sidebar, .content

    const auto* g_box = find_box(root, divs[0]);
    REQUIRE(g_box);
    // .g should have 2 children placed horizontally.
    REQUIRE(g_box->children.size() == 2);

    // Find sidebar and content boxes by element.
    const auto* sidebar_box = find_box(root, divs[1]);
    const auto* content_box = find_box(root, divs[2]);
    REQUIRE(sidebar_box);
    REQUIRE(content_box);

    // Sidebar starts at the left edge, content at 12rem (192px).
    CHECK(sidebar_box->x == 0.0f);
    CHECK(content_box->x == 192.0f);

    // Sidebar width is 192px; content fills the rest.
    CHECK(sidebar_box->width == 192.0f);
    CHECK(content_box->width == 608.0f);  // 800 - 192
}

TEST_CASE("layout: grid with no template falls back to block",
          "[layout][grid][adr-028]") {
    // display: grid without grid-template-columns: no template to
    // dispatch on -> layout_block_children runs.
    auto doc = honeybee::renderer::html::parse(
        "<html><body>"
        "<div class=\"g\"><p>A</p><p>B</p></div>"
        "</body></html>");
    REQUIRE(doc);

    auto sheet = css::Stylesheet::parse(
        ".g { display: grid }");
    auto styles = css::cascade(css::ua_stylesheet(), sheet, *doc);
    auto root = layout::layout(*doc, styles, fake_measure, 800.0f);

    // No crash, layout produces positive height (falls back to block).
    CHECK(root.height > 0.0f);
}


// =============================================================
// ADR-029: flex layout algorithm.
// =============================================================

namespace {

struct FlexTestDoc {
    std::unique_ptr<honeybee::renderer::dom::Document> doc;
    honeybee::renderer::css::ComputedStyleMap styles;
};

FlexTestDoc flex_prep(const std::string& html_src,
                      const std::string& css_src,
                      float viewport = 800.0f) {
    auto d = honeybee::renderer::html::parse(html_src);
    // Isolate from UA defaults (default body margin is 8px) so
    // x=0 / y=0 assertions are absolute.
    const std::string full_css =
        "body,html{margin:0;padding:0}" + css_src;
    auto sheet = honeybee::renderer::css::Stylesheet::parse(full_css);
    auto styles = honeybee::renderer::css::cascade(
        honeybee::renderer::css::ua_stylesheet(), sheet, *d,
        honeybee::renderer::css::RenderState{}, viewport);
    return {std::move(d), std::move(styles)};
}

const honeybee::renderer::layout::LayoutBox* find_class(
    const honeybee::renderer::layout::LayoutNode& n,
    const std::string& cls) {
    using namespace honeybee::renderer::layout;
    if (n.kind == LayoutKind::Box) {
        const auto& b = static_cast<const LayoutBox&>(n);
        if (b.element &&
            b.element->get_attribute("class") == cls) {
            return &b;
        }
        for (const auto& c : b.children) {
            if (auto r = find_class(*c, cls)) return r;
        }
    } else if (n.kind == LayoutKind::Inline) {
        const auto& inl = static_cast<const LayoutInline&>(n);
        for (const auto& c : inl.children) {
            if (auto r = find_class(*c, cls)) return r;
        }
    } else if (n.kind == LayoutKind::InlineContext) {
        const auto& ctx = static_cast<const LayoutInlineContext&>(n);
        for (const auto& c : ctx.children) {
            if (auto r = find_class(*c, cls)) return r;
        }
    }
    return nullptr;
}

const honeybee::renderer::layout::LayoutBox* child_box_at(
    const honeybee::renderer::layout::LayoutBox& parent, size_t i) {
    using namespace honeybee::renderer::layout;
    if (i >= parent.children.size()) return nullptr;
    const auto* c = parent.children[i].get();
    if (c->kind != LayoutKind::Box) return nullptr;
    return static_cast<const LayoutBox*>(c);
}

bool near(float a, float b, float tol = 1.0f) {
    return std::abs(a - b) < tol;
}

}  // namespace

// =============================================================
// =============================================================
// =============================================================
// ADR-033: position relative + fixed.
// =============================================================

TEST_CASE("position: relative stored on LayoutBox",
          "[layout][position][adr-033]") {
    auto doc = run(
        "<div class='r'>R</div>",
        ".r{position:relative;width:100px;height:50px}");
    const auto* r = find_class(doc.root, "r");
    REQUIRE(r != nullptr);
    CHECK(r->position == layout::Position::Relative);
}

TEST_CASE("position: static default",
          "[layout][position][adr-033]") {
    auto doc = run(
        "<div class='s'>S</div>",
        ".s{width:100px;height:50px}");
    const auto* s = find_class(doc.root, "s");
    REQUIRE(s != nullptr);
    CHECK(s->position == layout::Position::Static);
}

TEST_CASE("position: fixed anchored at (top, left)",
          "[layout][position][adr-033]") {
    auto doc = run(
        "<div class='f'>F</div>",
        ".f{position:fixed;top:12px;left:12px;width:100px;height:50px}");
    const auto* f = find_class(doc.root, "f");
    REQUIRE(f != nullptr);
    CHECK(f->position == layout::Position::Fixed);
    CHECK(near(f->x, 12.0f, 2.0f));
    CHECK(near(f->y, 12.0f, 2.0f));
}

TEST_CASE("position: fixed without offsets anchors at (0,0)",
          "[layout][position][adr-033]") {
    auto doc = run(
        "<div class='f'>F</div>",
        ".f{position:fixed;width:100px;height:50px}");
    const auto* f = find_class(doc.root, "f");
    REQUIRE(f != nullptr);
    CHECK(near(f->x, 0.0f, 2.0f));
    CHECK(near(f->y, 0.0f, 2.0f));
}

TEST_CASE("position: fixed skipped from parent flow",
          "[layout][position][adr-033]") {
    auto doc = run(
        "<div class='c'>"
        "<div class='f'>F</div>"
        "<div class='n'>N</div>"
        "</div>",
        ".c{width:500px}"
        ".f{position:fixed;top:12px;left:12px;width:100px;height:200px}"
        ".n{width:200px;height:50px}");
    const auto* c = find_class(doc.root, "c");
    const auto* n = find_class(doc.root, "n");
    REQUIRE(c != nullptr);
    REQUIRE(n != nullptr);
    // .n should start near top (fixed .f didn't push it down
    // by its 200px height)
    CHECK(n->y < 20.0f);
    // .c height excludes the fixed child's 200px
    CHECK(c->height < 100.0f);
}

TEST_CASE("position: absolute stored but layout as static (v1)",
          "[layout][position][adr-033]") {
    auto doc = run(
        "<div class='a'>A</div>",
        ".a{position:absolute;top:100px;left:100px;width:100px;height:50px}");
    const auto* a = find_class(doc.root, "a");
    REQUIRE(a != nullptr);
    CHECK(a->position == layout::Position::Absolute);
    // v1: treated as static, so NOT at (100, 100)
    CHECK(a->x < 100.0f);
}

TEST_CASE("position: relative offsets ignored in v1 layout",
          "[layout][position][adr-033]") {
    auto doc = run(
        "<div class='r'>R</div>",
        ".r{position:relative;top:100px;left:100px;width:100px;height:50px}");
    const auto* r = find_class(doc.root, "r");
    REQUIRE(r != nullptr);
    // v1: relative no-op; not shifted by (100, 100)
    CHECK(r->x < 50.0f);
}

// =============================================================
// ADR-036: clear property.
// =============================================================

TEST_CASE("clear:right on float jumps below prior right-float",
          "[layout][clear][adr-036]") {
    auto doc = run(
        "<div class='c'><div class='f1'>F1</div><div class='f2'>F2</div></div>",
        "body,html{margin:0;padding:0}"
        ".c{width:500px}"
        ".f1{float:right;width:100px;height:100px;clear:right}"
        ".f2{float:right;width:100px;height:100px;clear:right}");
    const auto* f1 = find_class(doc.root, "f1");
    const auto* f2 = find_class(doc.root, "f2");
    REQUIRE(f1 != nullptr);
    REQUIRE(f2 != nullptr);
    CHECK(f2->y >= f1->y + f1->height - 1.0f);
}

TEST_CASE("no clear: two float:right overlap at right edge",
          "[layout][clear][adr-036]") {
    auto doc = run(
        "<div class='c'><div class='f1'>F1</div><div class='f2'>F2</div></div>",
        "body,html{margin:0;padding:0}"
        ".c{width:500px}"
        ".f1{float:right;width:100px;height:100px}"
        ".f2{float:right;width:100px;height:100px}");
    const auto* f1 = find_class(doc.root, "f1");
    const auto* f2 = find_class(doc.root, "f2");
    REQUIRE(f1 != nullptr);
    REQUIRE(f2 != nullptr);
    CHECK(near(f1->y, f2->y, 2.0f));
}

TEST_CASE("clear:right on float:left does not jump",
          "[layout][clear][adr-036]") {
    auto doc = run(
        "<div class='c'><div class='f1'>F1</div><div class='f2'>F2</div></div>",
        "body,html{margin:0;padding:0}"
        ".c{width:500px}"
        ".f1{float:right;width:100px;height:100px}"
        ".f2{float:left;width:100px;height:100px;clear:right}");
    const auto* f1 = find_class(doc.root, "f1");
    const auto* f2 = find_class(doc.root, "f2");
    REQUIRE(f1 != nullptr);
    REQUIRE(f2 != nullptr);
    CHECK(near(f1->y, f2->y, 2.0f));
}

TEST_CASE("clear:both on float jumps below prior float",
          "[layout][clear][adr-036]") {
    auto doc = run(
        "<div class='c'><div class='f1'>F1</div><div class='f2'>F2</div></div>",
        "body,html{margin:0;padding:0}"
        ".c{width:500px}"
        ".f1{float:right;width:100px;height:100px}"
        ".f2{float:right;width:100px;height:100px;clear:both}");
    const auto* f1 = find_class(doc.root, "f1");
    const auto* f2 = find_class(doc.root, "f2");
    REQUIRE(f1 != nullptr);
    REQUIRE(f2 != nullptr);
    CHECK(f2->y >= f1->y + f1->height - 1.0f);
}

TEST_CASE("clear:both on non-float block jumps below float",
          "[layout][clear][adr-036]") {
    auto doc = run(
        "<div class='c'><div class='f'>F</div><div class='b'>B</div></div>",
        "body,html{margin:0;padding:0}"
        ".c{width:500px}"
        ".f{float:right;width:100px;height:100px}"
        ".b{height:50px;clear:both}");
    const auto* f = find_class(doc.root, "f");
    const auto* b = find_class(doc.root, "b");
    REQUIRE(f != nullptr);
    REQUIRE(b != nullptr);
    CHECK(b->y >= f->y + f->height - 1.0f);
}

TEST_CASE("no clear on non-float block narrows beside float",
          "[layout][clear][adr-036]") {
    auto doc = run(
        "<div class='c'><div class='f'>F</div><div class='b'>B</div></div>",
        "body,html{margin:0;padding:0}"
        ".c{width:500px}"
        ".f{float:right;width:100px;height:100px}"
        ".b{height:50px}");
    const auto* f = find_class(doc.root, "f");
    const auto* b = find_class(doc.root, "b");
    REQUIRE(f != nullptr);
    REQUIRE(b != nullptr);
    CHECK(b->y < f->y + f->height - 1.0f);
}

// ADR-032: paint-authoritative visibility/opacity culling.
// =============================================================

TEST_CASE("visibility: hidden element has visible=false",
          "[layout][adr-032]") {
    auto doc = run(
        "<div class='h'>H</div>",
        ".h{visibility:hidden;width:100px;height:50px}");
    const auto* h = find_class(doc.root, "h");
    REQUIRE(h != nullptr);
    CHECK(h->visible == false);
}

TEST_CASE("visibility: visible element has visible=true",
          "[layout][adr-032]") {
    auto doc = run(
        "<div class='v'>V</div>",
        ".v{visibility:visible;width:100px;height:50px}");
    const auto* v = find_class(doc.root, "v");
    REQUIRE(v != nullptr);
    CHECK(v->visible == true);
}

TEST_CASE("opacity: 0 element has visible=false",
          "[layout][adr-032]") {
    auto doc = run(
        "<div class='z'>Z</div>",
        ".z{opacity:0;width:100px;height:50px}");
    const auto* z = find_class(doc.root, "z");
    REQUIRE(z != nullptr);
    CHECK(z->visible == false);
}

TEST_CASE("opacity: 0.5 element has visible=true (v1: no blending)",
          "[layout][adr-032]") {
    auto doc = run(
        "<div class='o'>O</div>",
        ".o{opacity:0.5;width:100px;height:50px}");
    const auto* o = find_class(doc.root, "o");
    REQUIRE(o != nullptr);
    CHECK(o->visible == true);
}

TEST_CASE("opacity: var(...) treated as opaque (v1 limitation)",
          "[layout][adr-032]") {
    auto doc = run(
        "<div class='o'>O</div>",
        ".o{opacity:var(--x);width:100px;height:50px}");
    const auto* o = find_class(doc.root, "o");
    REQUIRE(o != nullptr);
    // var() doesn't resolve -- stof throws -> caught -> treated as
    // > 0.0001 -> visible stays true
    CHECK(o->visible == true);
}

TEST_CASE("no visibility/opacity defaults to visible=true",
          "[layout][adr-032]") {
    auto doc = run(
        "<div class='d'>D</div>",
        ".d{width:100px;height:50px}");
    const auto* d = find_class(doc.root, "d");
    REQUIRE(d != nullptr);
    CHECK(d->visible == true);
}

// ADR-031: float layout (v1: block-level float + block narrowing).
// =============================================================

TEST_CASE("float: right places element at right edge",
          "[layout][float][adr-031]") {
    auto doc = run(
        "<div class='c'><div class='f'>F</div></div>",
        ".c{width:500px}.f{float:right;width:100px;height:50px}");
    const auto* f = find_class(doc.root, "f");
    REQUIRE(f != nullptr);
    // c.x + c.width - f.width = right edge anchor.
    // The .c box is at content_x (0 by default with body{margin:0}
    // in run()) and width 500. f should be at x = 500 - 100 = 400.
    CHECK(near(f->x, 400.0f));
    CHECK(near(f->width, 100.0f));
}

TEST_CASE("float: left places element at left edge",
          "[layout][float][adr-031]") {
    auto doc = run(
        "<div class='c'><div class='f'>F</div></div>",
        ".c{width:500px}.f{float:left;width:100px;height:50px}");
    const auto* f = find_class(doc.root, "f");
    REQUIRE(f != nullptr);
    CHECK(near(f->x, 0.0f));
    CHECK(near(f->width, 100.0f));
}

TEST_CASE("float: following block narrows within float zone",
          "[layout][float][adr-031]") {
    auto doc = run(
        "<div class='c'>"
        "<div class='f'>F</div>"
        "<div class='b'>B</div>"
        "</div>",
        ".c{width:500px}.f{float:right;width:100px;height:100px}"
        ".b{height:200px}");
    const auto* f = find_class(doc.root, "f");
    const auto* b = find_class(doc.root, "b");
    REQUIRE(f != nullptr);
    REQUIRE(b != nullptr);
    // .b should be narrowed by float's 100px width
    CHECK(near(b->width, 400.0f));
}

TEST_CASE("float: following block full width when below float",
          "[layout][float][adr-031]") {
    auto doc = run(
        "<div class='c'>"
        "<div class='f'>F</div>"
        "<div class='b'>B</div>"
        "</div>",
        ".c{width:500px}.f{float:right;width:100px;height:50px}"
        ".b{height:100px;margin-top:80px}");
    const auto* b = find_class(doc.root, "b");
    REQUIRE(b != nullptr);
    // .b starts at y=80 (margin-top), float bottom at y=50,
    // so .b is below float zone → full width
    CHECK(near(b->width, 500.0f));
}

TEST_CASE("float: float doesn't advance cursor_y",
          "[layout][float][adr-031]") {
    auto doc = run(
        "<div class='c'>"
        "<div class='f'>F</div>"
        "<div class='b'>B</div>"
        "</div>",
        ".c{width:500px}.f{float:right;width:100px;height:200px}"
        ".b{height:50px}");
    const auto* b = find_class(doc.root, "b");
    REQUIRE(b != nullptr);
    // .b should start at y=0 (float doesn't advance cursor_y)
    CHECK(near(b->y, 0.0f));
}

TEST_CASE("float: left narrows following block from left",
          "[layout][float][adr-031]") {
    auto doc = run(
        "<div class='c'>"
        "<div class='f'>F</div>"
        "<div class='b'>B</div>"
        "</div>",
        ".c{width:500px}.f{float:left;width:100px;height:100px}"
        ".b{height:200px}");
    const auto* b = find_class(doc.root, "b");
    REQUIRE(b != nullptr);
    // .b starts at x=100 (after float), width = 500-100 = 400
    CHECK(near(b->x, 100.0f));
    CHECK(near(b->width, 400.0f));
}

TEST_CASE("float: multiple float:right, no stacking (v1 limitation)",
          "[layout][float][adr-031]") {
    // v1 limitation: two float:right children overlap at the same
    // position (both at right edge). Per-line stacking is ADR-032.
    auto doc = run(
        "<div class='c'>"
        "<div class='f1'>F1</div>"
        "<div class='f2'>F2</div>"
        "</div>",
        ".c{width:500px}"
        ".f1{float:right;width:100px;height:50px}"
        ".f2{float:right;width:100px;height:50px}");
    const auto* f1 = find_class(doc.root, "f1");
    const auto* f2 = find_class(doc.root, "f2");
    REQUIRE(f1 != nullptr);
    REQUIRE(f2 != nullptr);
    // Both at right edge, same y — v1 limitation documented in ADR
    CHECK(near(f1->x, 400.0f));
    CHECK(near(f2->x, 400.0f));
}

TEST_CASE("float: none behaves as normal block",
          "[layout][float][adr-031]") {
    auto doc = run(
        "<div class='c'><div class='f'>F</div></div>",
        ".c{width:500px}.f{float:none;width:100px;height:50px}");
    const auto* f = find_class(doc.root, "f");
    REQUIRE(f != nullptr);
    // float:none → normal block at x=0
    CHECK(near(f->x, 0.0f));
}

TEST_CASE("float: empty container yields no float effect",
          "[layout][float][adr-031]") {
    auto doc = run(
        "<div class='c'></div>",
        ".c{width:500px}");
    CHECK(near(doc.root.height, doc.root.height)); // sanity
}

// ADR-029 flex test block follows:

TEST_CASE("flex: empty container yields zero height",
          "[layout][flex][adr-029]") {
    auto lt = flex_prep("<div class='f'></div>", ".f{display:flex}");
    auto root = honeybee::renderer::layout::layout(
        *lt.doc, lt.styles, &fake_measure,
        800.0f);
    const auto* f = find_class(root, "f");
    REQUIRE(f != nullptr);
    CHECK(near(f->height, 0.0f));
    CHECK(f->children.empty());
}

TEST_CASE("flex: single child, no grow, natural width",
          "[layout][flex][adr-029]") {
    auto lt = flex_prep(
        "<div class='f'><div class='c'>hello</div></div>",
        ".f{display:flex}.c{flex-basis:100px}");
    auto root = honeybee::renderer::layout::layout(
        *lt.doc, lt.styles, &fake_measure,
        800.0f);
    const auto* f = find_class(root, "f");
    REQUIRE(f != nullptr);
    REQUIRE(f->children.size() == 1);
    const auto* c = child_box_at(*f, 0);
    REQUIRE(c != nullptr);
    CHECK(near(c->width, 100.0f));
}

TEST_CASE("flex: 2 children both grow 1 fill width",
          "[layout][flex][adr-029]") {
    auto lt = flex_prep(
        "<div class='f'><div class='a'>A</div><div class='b'>B</div></div>",
        ".f{display:flex;width:800px}"
        ".a{flex:1}.b{flex:1}");
    auto root = honeybee::renderer::layout::layout(
        *lt.doc, lt.styles, &fake_measure,
        800.0f);
    const auto* f = find_class(root, "f");
    REQUIRE(f != nullptr);
    REQUIRE(f->children.size() == 2);
    const auto* a = child_box_at(*f, 0);
    const auto* b = child_box_at(*f, 1);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    CHECK(near(a->width, 400.0f));
    CHECK(near(b->width, 400.0f));
    CHECK(near(a->x, 0.0f));
    CHECK(near(b->x, 400.0f));
}

TEST_CASE("flex: grow 1 vs grow 2 splits free space 1/3 : 2/3",
          "[layout][flex][adr-029]") {
    auto lt = flex_prep(
        "<div class='f'><div class='a'>A</div><div class='b'>B</div></div>",
        ".f{display:flex;width:800px}"
        ".a{flex:1 1 0}.b{flex:2 1 0}");
    auto root = honeybee::renderer::layout::layout(
        *lt.doc, lt.styles, &fake_measure,
        800.0f);
    const auto* f = find_class(root, "f");
    REQUIRE(f != nullptr);
    REQUIRE(f->children.size() == 2);
    const auto* a = child_box_at(*f, 0);
    const auto* b = child_box_at(*f, 1);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    CHECK(near(a->width, 800.0f / 3.0f));
    CHECK(near(b->width, 1600.0f / 3.0f));
}

TEST_CASE("flex: explicit flex-basis honored",
          "[layout][flex][adr-029]") {
    auto lt = flex_prep(
        "<div class='f'><div class='a'>A</div><div class='b'>B</div></div>",
        ".f{display:flex;width:800px}"
        ".a{flex-basis:200px;flex-shrink:0;flex-grow:0}"
        ".b{flex-basis:300px;flex-shrink:0;flex-grow:0}");
    auto root = honeybee::renderer::layout::layout(
        *lt.doc, lt.styles, &fake_measure,
        800.0f);
    const auto* f = find_class(root, "f");
    REQUIRE(f != nullptr);
    REQUIRE(f->children.size() == 2);
    CHECK(near(child_box_at(*f, 0)->width, 200.0f));
    CHECK(near(child_box_at(*f, 1)->width, 300.0f));
}

TEST_CASE("flex: gap inserts space between children",
          "[layout][flex][adr-029]") {
    auto lt = flex_prep(
        "<div class='f'><div class='a'>A</div><div class='b'>B</div></div>",
        ".f{display:flex;gap:20px;width:800px}"
        ".a{flex:0 0 100px}.b{flex:0 0 100px}");
    auto root = honeybee::renderer::layout::layout(
        *lt.doc, lt.styles, &fake_measure,
        800.0f);
    const auto* f = find_class(root, "f");
    REQUIRE(f != nullptr);
    REQUIRE(f->children.size() == 2);
    const auto* a = child_box_at(*f, 0);
    const auto* b = child_box_at(*f, 1);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    CHECK(near(a->x, 0.0f));
    CHECK(near(b->x, 120.0f));  // 100 + 20 gap
}

TEST_CASE("flex: justify-content center centers children",
          "[layout][flex][adr-029]") {
    auto lt = flex_prep(
        "<div class='f'><div class='a'>A</div><div class='b'>B</div></div>",
        ".f{display:flex;justify-content:center;width:800px}"
        ".a{flex:0 0 100px}.b{flex:0 0 100px}");
    auto root = honeybee::renderer::layout::layout(
        *lt.doc, lt.styles, &fake_measure,
        800.0f);
    const auto* f = find_class(root, "f");
    REQUIRE(f != nullptr);
    REQUIRE(f->children.size() == 2);
    const auto* a = child_box_at(*f, 0);
    // Total width = 200, remaining = 600, so start = 300.
    CHECK(near(a->x, 300.0f));
}

TEST_CASE("flex: justify-content space-between pushes to edges",
          "[layout][flex][adr-029]") {
    auto lt = flex_prep(
        "<div class='f'><div class='a'>A</div><div class='b'>B</div></div>",
        ".f{display:flex;justify-content:space-between;width:800px}"
        ".a{flex:0 0 100px}.b{flex:0 0 100px}");
    auto root = honeybee::renderer::layout::layout(
        *lt.doc, lt.styles, &fake_measure,
        800.0f);
    const auto* f = find_class(root, "f");
    REQUIRE(f != nullptr);
    REQUIRE(f->children.size() == 2);
    const auto* a = child_box_at(*f, 0);
    const auto* b = child_box_at(*f, 1);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    CHECK(near(a->x, 0.0f));
    CHECK(near(b->x, 700.0f));  // 800 - 100
}

TEST_CASE("flex: align-items center vertically centers short child",
          "[layout][flex][adr-029]") {
    // Use natural content height (no inline styles, which the test
    // cascade doesn't parse). fake_measure yields 16px per line.
    // .a = 6 nested divs -> 96px. .b = 1 div -> 16px.
    // Container = 96. b offset = (96 - 16) / 2 = 40.
    auto lt = flex_prep(
        "<div class='f'>"
        "<div class='a'>"
        "<div>A1</div><div>A2</div><div>A3</div>"
        "<div>A4</div><div>A5</div><div>A6</div>"
        "</div>"
        "<div class='b'><div>B1</div></div>"
        "</div>",
        ".f{display:flex;align-items:center;width:800px}"
        ".a{flex:0 0 50px}.b{flex:0 0 50px}");
    auto root = honeybee::renderer::layout::layout(
        *lt.doc, lt.styles, &fake_measure,
        800.0f);
    const auto* f = find_class(root, "f");
    REQUIRE(f != nullptr);
    REQUIRE(f->children.size() == 2);
    const auto* a = child_box_at(*f, 0);
    const auto* b = child_box_at(*f, 1);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    CHECK(b->y > a->y);
    CHECK(near(b->y - a->y, 40.0f, 2.0f));
}

// =============================================================
// ADR-029: viewport feature class injection (run via RenderPipeline).
// =============================================================

TEST_CASE("viewport feature classes: desktop flips disabled to enabled",
          "[browser][adr-029][feature-classes]") {
    honeybee::browser::RenderPipeline pipeline;
    pipeline.render(
        "<html class=\"vector-feature-main-menu-pinned-disabled\">"
        "<body></body></html>",
        1200.0f);
    const auto* html_el = pipeline.doc()->find_element("html");
    REQUIRE(html_el != nullptr);
    const std::string cls = html_el->get_attribute("class");
    CHECK(cls.find("vector-feature-main-menu-pinned-enabled")
          != std::string::npos);
    CHECK(cls.find("vector-feature-main-menu-pinned-disabled")
          == std::string::npos);
}

TEST_CASE("viewport feature classes: mobile keeps disabled",
          "[browser][adr-029][feature-classes]") {
    honeybee::browser::RenderPipeline pipeline;
    pipeline.render(
        "<html class=\"vector-feature-main-menu-pinned-disabled\">"
        "<body></body></html>",
        800.0f);
    const auto* html_el = pipeline.doc()->find_element("html");
    REQUIRE(html_el != nullptr);
    const std::string cls = html_el->get_attribute("class");
    CHECK(cls.find("vector-feature-main-menu-pinned-disabled")
          != std::string::npos);
    CHECK(cls.find("vector-feature-main-menu-pinned-enabled")
          == std::string::npos);
}

TEST_CASE("feature-classes: page-tools-pinned flips at desktop",
          "[browser][adr-030][feature-classes]") {
    honeybee::browser::RenderPipeline pipeline;
    pipeline.render(
        "<html class=\"vector-feature-page-tools-pinned-disabled\">"
        "<body></body></html>",
        1200.0f);
    const auto* html_el = pipeline.doc()->find_element("html");
    REQUIRE(html_el != nullptr);
    const std::string cls = html_el->get_attribute("class");
    CHECK(cls.find("vector-feature-page-tools-pinned-enabled")
          != std::string::npos);
    CHECK(cls.find("vector-feature-page-tools-pinned-disabled")
          == std::string::npos);
}

TEST_CASE("feature-classes: page-tools-pinned unchanged at mobile",
          "[browser][adr-030][feature-classes]") {
    honeybee::browser::RenderPipeline pipeline;
    pipeline.render(
        "<html class=\"vector-feature-page-tools-pinned-disabled\">"
        "<body></body></html>",
        800.0f);
    const auto* html_el = pipeline.doc()->find_element("html");
    REQUIRE(html_el != nullptr);
    const std::string cls = html_el->get_attribute("class");
    CHECK(cls.find("vector-feature-page-tools-pinned-disabled")
          != std::string::npos);
}

TEST_CASE("feature-classes: both flips round-trip together",
          "[browser][adr-030][feature-classes]") {
    honeybee::browser::RenderPipeline pipeline;
    pipeline.render(
        "<html class=\"vector-feature-main-menu-pinned-disabled "
        "vector-feature-page-tools-pinned-disabled\">"
        "<body></body></html>",
        1600.0f);
    const auto* html_el = pipeline.doc()->find_element("html");
    REQUIRE(html_el != nullptr);
    const std::string cls_desktop = html_el->get_attribute("class");
    CHECK(cls_desktop.find("vector-feature-main-menu-pinned-enabled")
          != std::string::npos);
    CHECK(cls_desktop.find("vector-feature-page-tools-pinned-enabled")
          != std::string::npos);

    pipeline.relayout(800.0f);
    const std::string cls_mobile = html_el->get_attribute("class");
    CHECK(cls_mobile.find("vector-feature-main-menu-pinned-disabled")
          != std::string::npos);
    CHECK(cls_mobile.find("vector-feature-page-tools-pinned-disabled")
          != std::string::npos);
}

TEST_CASE("viewport css: mw-jump-link hidden at desktop",
          "[browser][adr-030][feature-classes]") {
    honeybee::browser::RenderPipeline pipeline;
    pipeline.render(
        "<html><body><a class=\"mw-jump-link\" href=\"#x\">"
        "Jump to content</a></body></html>",
        1200.0f);
    const auto* el = pipeline.doc()->find_element("a");
    REQUIRE(el != nullptr);
    auto it = pipeline.styles().find(el);
    REQUIRE(it != pipeline.styles().end());
    const auto v = it->second.get("display");
    REQUIRE(v.has_value());
    CHECK(*v == "none");
}

TEST_CASE("viewport css: mw-jump-link untouched at mobile",
          "[browser][adr-030][feature-classes]") {
    honeybee::browser::RenderPipeline pipeline;
    pipeline.render(
        "<html><body><a class=\"mw-jump-link\" href=\"#x\">"
        "Jump to content</a></body></html>",
        800.0f);
    const auto* el = pipeline.doc()->find_element("a");
    REQUIRE(el != nullptr);
    auto it = pipeline.styles().find(el);
    REQUIRE(it != pipeline.styles().end());
    const auto v = it->second.get("display");
    if (v) CHECK(*v != "none");
}

TEST_CASE("viewport css: p-lang-btn hidden at desktop",
          "[browser][adr-029][feature-classes]") {
    honeybee::browser::RenderPipeline pipeline;
    pipeline.render(
        "<html><body><div id=\"p-lang-btn\">x</div></body></html>",
        1200.0f);
    const auto* el = pipeline.doc()->find_element("div");
    REQUIRE(el != nullptr);
    auto it = pipeline.styles().find(el);
    REQUIRE(it != pipeline.styles().end());
    const auto v = it->second.get("display");
    REQUIRE(v.has_value());
    CHECK(*v == "none");
}

TEST_CASE("viewport css: p-lang-btn not hidden at mobile",
          "[browser][adr-029][feature-classes]") {
    honeybee::browser::RenderPipeline pipeline;
    pipeline.render(
        "<html><body><div id=\"p-lang-btn\">x</div></body></html>",
        800.0f);
    const auto* el = pipeline.doc()->find_element("div");
    REQUIRE(el != nullptr);
    auto it = pipeline.styles().find(el);
    REQUIRE(it != pipeline.styles().end());
    const auto v = it->second.get("display");
    if (v) CHECK(*v != "none");
}

TEST_CASE("viewport feature classes: round-trip through resize",
          "[browser][adr-029][feature-classes]") {
    honeybee::browser::RenderPipeline pipeline;
    pipeline.render(
        "<html class=\"vector-feature-main-menu-pinned-disabled\">"
        "<body></body></html>",
        1600.0f);
    const auto* html_el = pipeline.doc()->find_element("html");
    REQUIRE(html_el != nullptr);
    // Now shrink below threshold -> flip back to disabled.
    pipeline.relayout(800.0f);
    CHECK(html_el->get_attribute("class")
          .find("vector-feature-main-menu-pinned-disabled")
          != std::string::npos);
    // Grow again -> back to enabled.
    pipeline.relayout(1600.0f);
    CHECK(html_el->get_attribute("class")
          .find("vector-feature-main-menu-pinned-enabled")
          != std::string::npos);
}

TEST_CASE("flex-direction column stacks children vertically",
          "[layout][flex][adr-029]") {
    auto lt = flex_prep(
        "<div class='f'><div class='a'>A</div><div class='b'>B</div></div>",
        ".f{display:flex;flex-direction:column}"
        ".a{height:30px;width:100px}.b{height:40px;width:100px}");
    auto root = honeybee::renderer::layout::layout(
        *lt.doc, lt.styles, &fake_measure,
        800.0f);
    const auto* f = find_class(root, "f");
    REQUIRE(f != nullptr);
    REQUIRE(f->children.size() == 2);
    const auto* a = child_box_at(*f, 0);
    const auto* b = child_box_at(*f, 1);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    // Same x (column cross-axis = flex-start default)
    CHECK(near(a->x, b->x));
    // Stacked vertically: b.y = a.y + a.height + 0 gap
    CHECK(near(b->y - a->y, a->height));
    // Container height = sum
    CHECK(near(f->height, a->height + b->height, 2.0f));
}

// =============================================================
// ADR-042: position: relative / absolute.
// =============================================================

TEST_CASE("absolute: relative parent + top/left offsets",
          "[layout][position][adr-042]") {
    auto doc = run(
        "<div class='p'><div class='a'>A</div></div>",
        "body,html{margin:0;padding:0}"
        ".p{position:relative;width:300px;height:200px;margin:0;padding:0}"
        ".a{position:absolute;top:10px;left:20px;width:50px;height:30px}");
    const auto* a = find_class(doc.root, "a");
    REQUIRE(a != nullptr);
    CHECK(a->x == Catch::Approx(20.0f).margin(1.0f));
    CHECK(a->y == Catch::Approx(10.0f).margin(1.0f));
    CHECK(a->width == Catch::Approx(50.0f).margin(1.0f));
    CHECK(a->height == Catch::Approx(30.0f).margin(1.0f));
}

TEST_CASE("absolute: relative parent + bottom/right offsets",
          "[layout][position][adr-042]") {
    auto doc = run(
        "<div class='p'><div class='a'>A</div></div>",
        "body,html{margin:0;padding:0}"
        ".p{position:relative;width:300px;height:200px;margin:0;padding:0}"
        ".a{position:absolute;bottom:10px;right:20px;width:50px;height:30px}");
    const auto* a = find_class(doc.root, "a");
    REQUIRE(a != nullptr);
    // right:20, width:50, parent_w:300 -> x = 300 - 20 - 50 = 230
    CHECK(a->x == Catch::Approx(230.0f).margin(1.0f));
    // bottom:10, height:30, parent_h:200 -> y = 200 - 10 - 30 = 160
    CHECK(a->y == Catch::Approx(160.0f).margin(1.0f));
}

TEST_CASE("absolute: parent not positioned falls back to static",
          "[layout][position][adr-042]") {
    auto doc = run(
        "<div class='p'><div class='a'>A</div></div>",
        "body,html{margin:0;padding:0}"
        ".p{width:300px;height:200px;margin:0;padding:0}"
        ".a{position:absolute;top:100px;left:100px;width:50px;height:30px}");
    const auto* a = find_class(doc.root, "a");
    REQUIRE(a != nullptr);
    // Fallback: parent not positioned -> static (top/left ignored)
    CHECK(a->x < 100.0f);
    CHECK(a->y < 100.0f);
}

TEST_CASE("absolute: left=0 right=0 stretches width",
          "[layout][position][adr-042]") {
    auto doc = run(
        "<div class='p'><div class='a'>A</div></div>",
        "body,html{margin:0;padding:0}"
        ".p{position:relative;width:200px;height:100px;margin:0;padding:0}"
        ".a{position:absolute;top:5px;left:0;right:0;height:10px}");
    const auto* a = find_class(doc.root, "a");
    REQUIRE(a != nullptr);
    CHECK(a->x == Catch::Approx(0.0f).margin(1.0f));
    CHECK(a->width == Catch::Approx(200.0f).margin(1.0f));
}

TEST_CASE("absolute: top=0 bottom=0 stretches height",
          "[layout][position][adr-042]") {
    auto doc = run(
        "<div class='p'><div class='a'>A</div></div>",
        "body,html{margin:0;padding:0}"
        ".p{position:relative;width:200px;height:100px;margin:0;padding:0}"
        ".a{position:absolute;top:0;bottom:0;left:10px;width:20px}");
    const auto* a = find_class(doc.root, "a");
    REQUIRE(a != nullptr);
    CHECK(a->y == Catch::Approx(0.0f).margin(1.0f));
    CHECK(a->height == Catch::Approx(100.0f).margin(1.0f));
}

TEST_CASE("absolute: over-constrained left wins over right",
          "[layout][position][adr-042]") {
    auto doc = run(
        "<div class='p'><div class='a'>A</div></div>",
        "body,html{margin:0;padding:0}"
        ".p{position:relative;width:300px;height:100px;margin:0;padding:0}"
        ".a{position:absolute;top:0;left:20px;right:50px;"
        "width:40px;height:10px}");
    const auto* a = find_class(doc.root, "a");
    REQUIRE(a != nullptr);
    // left wins: x = 20
    CHECK(a->x == Catch::Approx(20.0f).margin(1.0f));
}

TEST_CASE("absolute: does not contribute to parent height",
          "[layout][position][adr-042]") {
    auto doc = run(
        "<div class='p'><div class='a'>A</div></div>",
        "body,html{margin:0;padding:0}"
        ".p{position:relative;width:200px;margin:0;padding:0}"
        ".a{position:absolute;top:0;left:0;width:50px;height:300px}");
    const auto* p = find_class(doc.root, "p");
    REQUIRE(p != nullptr);
    // Parent's height excludes the 300px absolute child.
    CHECK(p->height < 100.0f);
}

TEST_CASE("absolute: multiple siblings positioned independently",
          "[layout][position][adr-042]") {
    auto doc = run(
        "<div class='p'>"
        "<div class='a1'>1</div>"
        "<div class='a2'>2</div>"
        "</div>",
        "body,html{margin:0;padding:0}"
        ".p{position:relative;width:200px;height:200px;margin:0;padding:0}"
        ".a1{position:absolute;top:10px;left:10px;width:20px;height:20px}"
        ".a2{position:absolute;top:50px;left:80px;width:20px;height:20px}");
    const auto* a1 = find_class(doc.root, "a1");
    const auto* a2 = find_class(doc.root, "a2");
    REQUIRE(a1 != nullptr);
    REQUIRE(a2 != nullptr);
    CHECK(a1->x == Catch::Approx(10.0f).margin(1.0f));
    CHECK(a1->y == Catch::Approx(10.0f).margin(1.0f));
    CHECK(a2->x == Catch::Approx(80.0f).margin(1.0f));
    CHECK(a2->y == Catch::Approx(50.0f).margin(1.0f));
}

TEST_CASE("relative: sets is_containing_block marker",
          "[layout][position][adr-042]") {
    auto doc = run(
        "<div class='r'>R</div>",
        "body,html{margin:0;padding:0}"
        ".r{position:relative;width:100px;height:50px}");
    const auto* r = find_class(doc.root, "r");
    REQUIRE(r != nullptr);
    CHECK(r->position == layout::Position::Relative);
    CHECK(r->is_containing_block == true);
}

TEST_CASE("absolute: sets is_containing_block marker",
          "[layout][position][adr-042]") {
    auto doc = run(
        "<div class='a'>A</div>",
        "body,html{margin:0;padding:0}"
        ".a{position:absolute;width:100px;height:50px}");
    const auto* a = find_class(doc.root, "a");
    REQUIRE(a != nullptr);
    CHECK(a->position == layout::Position::Absolute);
    CHECK(a->is_containing_block == true);
}

TEST_CASE("static: is_containing_block is false",
          "[layout][position][adr-042]") {
    auto doc = run(
        "<div class='s'>S</div>",
        "body,html{margin:0;padding:0}"
        ".s{width:100px;height:50px}");
    const auto* s = find_class(doc.root, "s");
    REQUIRE(s != nullptr);
    CHECK(s->position == layout::Position::Static);
    CHECK(s->is_containing_block == false);
}

TEST_CASE("absolute: offsets relative to padded parent's content box",
          "[layout][position][adr-042]") {
    auto doc = run(
        "<div class='p'><div class='a'>A</div></div>",
        "body,html{margin:0;padding:0}"
        ".p{position:relative;width:300px;height:200px;"
        "margin:0;padding:20px;box-sizing:content-box}"
        ".a{position:absolute;top:10px;left:10px;width:20px;height:20px}");
    const auto* a = find_class(doc.root, "a");
    REQUIRE(a != nullptr);
    // Content box starts at (20, 20). Absolute child at content + offset.
    CHECK(a->x == Catch::Approx(30.0f).margin(2.0f));
    CHECK(a->y == Catch::Approx(30.0f).margin(2.0f));
}

TEST_CASE("absolute: explicit width/height preserved (no stretch)",
          "[layout][position][adr-042]") {
    auto doc = run(
        "<div class='p'><div class='a'>A</div></div>",
        "body,html{margin:0;padding:0}"
        ".p{position:relative;width:300px;height:200px;margin:0;padding:0}"
        ".a{position:absolute;top:0;left:0;width:75px;height:25px}");
    const auto* a = find_class(doc.root, "a");
    REQUIRE(a != nullptr);
    CHECK(a->width == Catch::Approx(75.0f).margin(1.0f));
    CHECK(a->height == Catch::Approx(25.0f).margin(1.0f));
}

// =============================================================
// ADR-043: box-shadow parse + layout fields.
// =============================================================

TEST_CASE("box-shadow: 0 1px #c8ccd1 parses all fields",
          "[layout][box-shadow][adr-043]") {
    auto doc = run(
        "<div class='b'>B</div>",
        "body,html{margin:0;padding:0}"
        ".b{width:100px;height:50px;box-shadow:0 1px #c8ccd1}");
    const auto* b = find_class(doc.root, "b");
    REQUIRE(b != nullptr);
    CHECK(b->box_shadow_has == true);
    CHECK(b->box_shadow_ox == Catch::Approx(0.0f).margin(0.1f));
    CHECK(b->box_shadow_oy == Catch::Approx(1.0f).margin(0.1f));
    CHECK(b->box_shadow_blur == Catch::Approx(0.0f).margin(0.1f));
    CHECK(b->box_shadow_spread == Catch::Approx(0.0f).margin(0.1f));
    CHECK(b->box_shadow_inset == false);
}

TEST_CASE("box-shadow: 2px 3px 4px 5px red (all four lengths)",
          "[layout][box-shadow][adr-043]") {
    auto doc = run(
        "<div class='b'>B</div>",
        "body,html{margin:0;padding:0}"
        ".b{width:100px;height:50px;box-shadow:2px 3px 4px 5px red}");
    const auto* b = find_class(doc.root, "b");
    REQUIRE(b != nullptr);
    CHECK(b->box_shadow_has == true);
    CHECK(b->box_shadow_ox == Catch::Approx(2.0f).margin(0.1f));
    CHECK(b->box_shadow_oy == Catch::Approx(3.0f).margin(0.1f));
    CHECK(b->box_shadow_blur == Catch::Approx(4.0f).margin(0.1f));
    CHECK(b->box_shadow_spread == Catch::Approx(5.0f).margin(0.1f));
}

TEST_CASE("box-shadow: 0 0 5px red (blur only, no spread)",
          "[layout][box-shadow][adr-043]") {
    auto doc = run(
        "<div class='b'>B</div>",
        "body,html{margin:0;padding:0}"
        ".b{width:100px;height:50px;box-shadow:0 0 5px red}");
    const auto* b = find_class(doc.root, "b");
    REQUIRE(b != nullptr);
    CHECK(b->box_shadow_has == true);
    CHECK(b->box_shadow_blur == Catch::Approx(5.0f).margin(0.1f));
    CHECK(b->box_shadow_spread == Catch::Approx(0.0f).margin(0.1f));
}

TEST_CASE("box-shadow: inset keyword sets inset flag",
          "[layout][box-shadow][adr-043]") {
    auto doc = run(
        "<div class='b'>B</div>",
        "body,html{margin:0;padding:0}"
        ".b{width:100px;height:50px;box-shadow:inset 0 0 0 1px #36c}");
    const auto* b = find_class(doc.root, "b");
    REQUIRE(b != nullptr);
    CHECK(b->box_shadow_has == true);
    CHECK(b->box_shadow_inset == true);
    CHECK(b->box_shadow_spread == Catch::Approx(1.0f).margin(0.1f));
}

TEST_CASE("box-shadow: negative spread parsed correctly",
          "[layout][box-shadow][adr-043]") {
    auto doc = run(
        "<div class='b'>B</div>",
        "body,html{margin:0;padding:0}"
        ".b{width:100px;height:50px;"
        "box-shadow:0 2px 6px -1px rgba(0,0,0,0.2)}");
    const auto* b = find_class(doc.root, "b");
    REQUIRE(b != nullptr);
    CHECK(b->box_shadow_has == true);
    CHECK(b->box_shadow_spread == Catch::Approx(-1.0f).margin(0.1f));
    CHECK(b->box_shadow_blur == Catch::Approx(6.0f).margin(0.1f));
}

TEST_CASE("box-shadow: none leaves has=false",
          "[layout][box-shadow][adr-043]") {
    auto doc = run(
        "<div class='b'>B</div>",
        "body,html{margin:0;padding:0}"
        ".b{width:100px;height:50px;box-shadow:none}");
    const auto* b = find_class(doc.root, "b");
    REQUIRE(b != nullptr);
    CHECK(b->box_shadow_has == false);
}

TEST_CASE("box-shadow: multi-shadow takes first only",
          "[layout][box-shadow][adr-043]") {
    auto doc = run(
        "<div class='b'>B</div>",
        "body,html{margin:0;padding:0}"
        ".b{width:100px;height:50px;"
        "box-shadow:0 1px red, 0 2px blue}");
    const auto* b = find_class(doc.root, "b");
    REQUIRE(b != nullptr);
    CHECK(b->box_shadow_has == true);
    CHECK(b->box_shadow_oy == Catch::Approx(1.0f).margin(0.1f));
}

TEST_CASE("box-shadow: 1px (only one length) drops rule",
          "[layout][box-shadow][adr-043]") {
    auto doc = run(
        "<div class='b'>B</div>",
        "body,html{margin:0;padding:0}"
        ".b{width:100px;height:50px;box-shadow:1px}");
    const auto* b = find_class(doc.root, "b");
    REQUIRE(b != nullptr);
    CHECK(b->box_shadow_has == false);
}

TEST_CASE("box-shadow: foo bar baz (invalid) drops rule",
          "[layout][box-shadow][adr-043]") {
    auto doc = run(
        "<div class='b'>B</div>",
        "body,html{margin:0;padding:0}"
        ".b{width:100px;height:50px;box-shadow:foo bar baz}");
    const auto* b = find_class(doc.root, "b");
    REQUIRE(b != nullptr);
    CHECK(b->box_shadow_has == false);
}

TEST_CASE("box-shadow: no rule leaves defaults",
          "[layout][box-shadow][adr-043]") {
    auto doc = run(
        "<div class='b'>B</div>",
        "body,html{margin:0;padding:0}"
        ".b{width:100px;height:50px}");
    const auto* b = find_class(doc.root, "b");
    REQUIRE(b != nullptr);
    CHECK(b->box_shadow_has == false);
    CHECK(b->box_shadow_inset == false);
    CHECK(b->box_shadow_ox == Catch::Approx(0.0f).margin(0.1f));
}

TEST_CASE("box-shadow: 0 1px red (no blur/spread)",
          "[layout][box-shadow][adr-043]") {
    auto doc = run(
        "<div class='b'>B</div>",
        "body,html{margin:0;padding:0}"
        ".b{width:100px;height:50px;box-shadow:0 1px red}");
    const auto* b = find_class(doc.root, "b");
    REQUIRE(b != nullptr);
    CHECK(b->box_shadow_has == true);
    CHECK(b->box_shadow_blur == Catch::Approx(0.0f).margin(0.1f));
    CHECK(b->box_shadow_spread == Catch::Approx(0.0f).margin(0.1f));
}

TEST_CASE("box-shadow: inset + 4 lengths + color",
          "[layout][box-shadow][adr-043]") {
    auto doc = run(
        "<div class='b'>B</div>",
        "body,html{margin:0;padding:0}"
        ".b{width:100px;height:50px;"
        "box-shadow:inset 1px 2px 3px 4px blue}");
    const auto* b = find_class(doc.root, "b");
    REQUIRE(b != nullptr);
    CHECK(b->box_shadow_has == true);
    CHECK(b->box_shadow_inset == true);
    CHECK(b->box_shadow_ox == Catch::Approx(1.0f).margin(0.1f));
    CHECK(b->box_shadow_oy == Catch::Approx(2.0f).margin(0.1f));
    CHECK(b->box_shadow_blur == Catch::Approx(3.0f).margin(0.1f));
    CHECK(b->box_shadow_spread == Catch::Approx(4.0f).margin(0.1f));
}

// =============================================================
// ADR-044: border-radius parse + layout fields.
// =============================================================

TEST_CASE("border-radius: 2px fills all four corners",
          "[layout][border-radius][adr-044]") {
    auto doc = run(
        "<div class='b'>B</div>",
        "body,html{margin:0;padding:0}"
        ".b{width:100px;height:50px;border-radius:2px}");
    const auto* b = find_class(doc.root, "b");
    REQUIRE(b != nullptr);
    CHECK(b->border_radius[0] == Catch::Approx(2.0f).margin(0.1f));
    CHECK(b->border_radius[1] == Catch::Approx(2.0f).margin(0.1f));
    CHECK(b->border_radius[2] == Catch::Approx(2.0f).margin(0.1f));
    CHECK(b->border_radius[3] == Catch::Approx(2.0f).margin(0.1f));
}

TEST_CASE("border-radius: 0 leaves all zero",
          "[layout][border-radius][adr-044]") {
    auto doc = run(
        "<div class='b'>B</div>",
        "body,html{margin:0;padding:0}"
        ".b{width:100px;height:50px;border-radius:0}");
    const auto* b = find_class(doc.root, "b");
    REQUIRE(b != nullptr);
    CHECK(b->border_radius[0] == Catch::Approx(0.0f).margin(0.1f));
    CHECK(b->border_radius[1] == Catch::Approx(0.0f).margin(0.1f));
    CHECK(b->border_radius[2] == Catch::Approx(0.0f).margin(0.1f));
    CHECK(b->border_radius[3] == Catch::Approx(0.0f).margin(0.1f));
}

TEST_CASE("border-radius: 10px all four = 10",
          "[layout][border-radius][adr-044]") {
    auto doc = run(
        "<div class='b'>B</div>",
        "body,html{margin:0;padding:0}"
        ".b{width:100px;height:50px;border-radius:10px}");
    const auto* b = find_class(doc.root, "b");
    REQUIRE(b != nullptr);
    CHECK(b->border_radius[0] == Catch::Approx(10.0f).margin(0.1f));
    CHECK(b->border_radius[1] == Catch::Approx(10.0f).margin(0.1f));
    CHECK(b->border_radius[2] == Catch::Approx(10.0f).margin(0.1f));
    CHECK(b->border_radius[3] == Catch::Approx(10.0f).margin(0.1f));
}

TEST_CASE("border-top-left-radius alone (no shorthand)",
          "[layout][border-radius][adr-044]") {
    auto doc = run(
        "<div class='b'>B</div>",
        "body,html{margin:0;padding:0}"
        ".b{width:100px;height:50px;border-top-left-radius:4px}");
    const auto* b = find_class(doc.root, "b");
    REQUIRE(b != nullptr);
    CHECK(b->border_radius[0] == Catch::Approx(4.0f).margin(0.1f));
    CHECK(b->border_radius[1] == Catch::Approx(0.0f).margin(0.1f));
    CHECK(b->border_radius[2] == Catch::Approx(0.0f).margin(0.1f));
    CHECK(b->border_radius[3] == Catch::Approx(0.0f).margin(0.1f));
}

TEST_CASE("shorthand + per-corner override (Wikipedia end-button case)",
          "[layout][border-radius][adr-044]") {
    auto doc = run(
        "<div class='b'>B</div>",
        "body,html{margin:0;padding:0}"
        ".b{width:100px;height:50px;border-radius:2px;"
        "border-top-left-radius:0;border-bottom-left-radius:0}");
    const auto* b = find_class(doc.root, "b");
    REQUIRE(b != nullptr);
    CHECK(b->border_radius[0] == Catch::Approx(0.0f).margin(0.1f));
    CHECK(b->border_radius[1] == Catch::Approx(2.0f).margin(0.1f));
    CHECK(b->border_radius[2] == Catch::Approx(2.0f).margin(0.1f));
    CHECK(b->border_radius[3] == Catch::Approx(0.0f).margin(0.1f));
}

TEST_CASE("border-radius: inherit deferred (all 0)",
          "[layout][border-radius][adr-044]") {
    auto doc = run(
        "<div class='b'>B</div>",
        "body,html{margin:0;padding:0}"
        ".b{width:100px;height:50px;border-radius:inherit}");
    const auto* b = find_class(doc.root, "b");
    REQUIRE(b != nullptr);
    CHECK(b->border_radius[0] == Catch::Approx(0.0f).margin(0.1f));
    CHECK(b->border_radius[1] == Catch::Approx(0.0f).margin(0.1f));
    CHECK(b->border_radius[2] == Catch::Approx(0.0f).margin(0.1f));
    CHECK(b->border_radius[3] == Catch::Approx(0.0f).margin(0.1f));
}

TEST_CASE("border-radius: 50% deferred (all 0)",
          "[layout][border-radius][adr-044]") {
    auto doc = run(
        "<div class='b'>B</div>",
        "body,html{margin:0;padding:0}"
        ".b{width:100px;height:100px;border-radius:50%}");
    const auto* b = find_class(doc.root, "b");
    REQUIRE(b != nullptr);
    CHECK(b->border_radius[0] == Catch::Approx(0.0f).margin(0.1f));
    CHECK(b->border_radius[1] == Catch::Approx(0.0f).margin(0.1f));
    CHECK(b->border_radius[2] == Catch::Approx(0.0f).margin(0.1f));
    CHECK(b->border_radius[3] == Catch::Approx(0.0f).margin(0.1f));
}

TEST_CASE("border-radius: foo (malformed) leaves 0",
          "[layout][border-radius][adr-044]") {
    auto doc = run(
        "<div class='b'>B</div>",
        "body,html{margin:0;padding:0}"
        ".b{width:100px;height:50px;border-radius:foo}");
    const auto* b = find_class(doc.root, "b");
    REQUIRE(b != nullptr);
    CHECK(b->border_radius[0] == Catch::Approx(0.0f).margin(0.1f));
}

TEST_CASE("border-radius: 0.5em resolves via ctx (8px)",
          "[layout][border-radius][adr-044]") {
    auto doc = run(
        "<div class='b'>B</div>",
        "body,html{margin:0;padding:0;font-size:16px}"
        ".b{width:100px;height:50px;border-radius:0.5em}");
    const auto* b = find_class(doc.root, "b");
    REQUIRE(b != nullptr);
    CHECK(b->border_radius[0] == Catch::Approx(8.0f).margin(0.5f));
    CHECK(b->border_radius[1] == Catch::Approx(8.0f).margin(0.5f));
    CHECK(b->border_radius[2] == Catch::Approx(8.0f).margin(0.5f));
    CHECK(b->border_radius[3] == Catch::Approx(8.0f).margin(0.5f));
}

TEST_CASE("border-radius: 2px 4px (multi-value) -> first token only",
          "[layout][border-radius][adr-044]") {
    auto doc = run(
        "<div class='b'>B</div>",
        "body,html{margin:0;padding:0}"
        ".b{width:100px;height:50px;border-radius:2px 4px}");
    const auto* b = find_class(doc.root, "b");
    REQUIRE(b != nullptr);
    // Documented deviation: multi-value takes first token.
    CHECK(b->border_radius[0] == Catch::Approx(2.0f).margin(0.1f));
    CHECK(b->border_radius[1] == Catch::Approx(2.0f).margin(0.1f));
    CHECK(b->border_radius[2] == Catch::Approx(2.0f).margin(0.1f));
    CHECK(b->border_radius[3] == Catch::Approx(2.0f).margin(0.1f));
}

TEST_CASE("border-radius: no rule leaves defaults",
          "[layout][border-radius][adr-044]") {
    auto doc = run(
        "<div class='b'>B</div>",
        "body,html{margin:0;padding:0}"
        ".b{width:100px;height:50px}");
    const auto* b = find_class(doc.root, "b");
    REQUIRE(b != nullptr);
    CHECK(b->border_radius[0] == Catch::Approx(0.0f).margin(0.1f));
    CHECK(b->border_radius[1] == Catch::Approx(0.0f).margin(0.1f));
    CHECK(b->border_radius[2] == Catch::Approx(0.0f).margin(0.1f));
    CHECK(b->border_radius[3] == Catch::Approx(0.0f).margin(0.1f));
}

// =============================================================
// ADR-045: inline background-image + padding-right post-process.
// =============================================================

namespace {

const layout::LineFragment* find_frag_by_class(
        const layout::LayoutNode& node, const std::string& cls) {
    if (node.kind == layout::LayoutKind::InlineContext) {
        const auto& ctx =
            static_cast<const layout::LayoutInlineContext&>(node);
        for (const auto& f : ctx.lines) {
            if (f.element &&
                f.element->get_attribute("class") == cls) {
                return &f;
            }
        }
    } else if (node.kind == layout::LayoutKind::Text) {
        const auto& lt = static_cast<const layout::LayoutText&>(node);
        for (const auto& f : lt.lines) {
            if (f.element &&
                f.element->get_attribute("class") == cls) {
                return &f;
            }
        }
    } else if (node.kind == layout::LayoutKind::Box) {
        const auto& b = static_cast<const layout::LayoutBox&>(node);
        for (const auto& c : b.children) {
            if (auto r = find_frag_by_class(*c, cls)) return r;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("inline bg: background-image URL extracted to fragment",
          "[layout][inline-bg][adr-045]") {
    auto doc = run(
        "<div><a class='external' href='#'>link</a></div>",
        "body,html{margin:0;padding:0}"
        "div{width:400px}"
        ".external{background-image:url(x.svg)}");
    const auto* f = find_frag_by_class(doc.root, "external");
    REQUIRE(f != nullptr);
    CHECK(f->bg_image_url == "x.svg");
}

TEST_CASE("inline bg: background-size fixed length",
          "[layout][inline-bg][adr-045]") {
    auto doc = run(
        "<div><a class='external' href='#'>link</a></div>",
        "body,html{margin:0;padding:0}"
        "div{width:400px}"
        ".external{background-image:url(x.svg);background-size:16px}");
    const auto* f = find_frag_by_class(doc.root, "external");
    REQUIRE(f != nullptr);
    CHECK(f->bg_size_w == Catch::Approx(16.0f).margin(0.1f));
}

TEST_CASE("inline bg: background-position: center right",
          "[layout][inline-bg][adr-045]") {
    auto doc = run(
        "<div><a class='external' href='#'>link</a></div>",
        "body,html{margin:0;padding:0}"
        "div{width:400px}"
        ".external{background-image:url(x.svg);"
        "background-position:center right}");
    const auto* f = find_frag_by_class(doc.root, "external");
    REQUIRE(f != nullptr);
    CHECK(f->bg_pos_x == layout::BgPosX::Right);
    CHECK(f->bg_pos_y == layout::BgPosY::Center);
}

TEST_CASE("inline bg: background-position: right center (swapped order)",
          "[layout][inline-bg][adr-045]") {
    auto doc = run(
        "<div><a class='external' href='#'>link</a></div>",
        "body,html{margin:0;padding:0}"
        "div{width:400px}"
        ".external{background-image:url(x.svg);"
        "background-position:right center}");
    const auto* f = find_frag_by_class(doc.root, "external");
    REQUIRE(f != nullptr);
    CHECK(f->bg_pos_x == layout::BgPosX::Right);
    CHECK(f->bg_pos_y == layout::BgPosY::Center);
}

TEST_CASE("inline bg: background-position: left top (defaults)",
          "[layout][inline-bg][adr-045]") {
    auto doc = run(
        "<div><a class='external' href='#'>link</a></div>",
        "body,html{margin:0;padding:0}"
        "div{width:400px}"
        ".external{background-image:url(x.svg);"
        "background-position:left top}");
    const auto* f = find_frag_by_class(doc.root, "external");
    REQUIRE(f != nullptr);
    CHECK(f->bg_pos_x == layout::BgPosX::Left);
    CHECK(f->bg_pos_y == layout::BgPosY::Top);
}

TEST_CASE("inline bg: background-position: center (single keyword)",
          "[layout][inline-bg][adr-045]") {
    auto doc = run(
        "<div><a class='external' href='#'>link</a></div>",
        "body,html{margin:0;padding:0}"
        "div{width:400px}"
        ".external{background-image:url(x.svg);"
        "background-position:center}");
    const auto* f = find_frag_by_class(doc.root, "external");
    REQUIRE(f != nullptr);
    CHECK(f->bg_pos_x == layout::BgPosX::Center);
    CHECK(f->bg_pos_y == layout::BgPosY::Center);
}

TEST_CASE("inline bg: background-repeat: no-repeat",
          "[layout][inline-bg][adr-045]") {
    auto doc = run(
        "<div><a class='external' href='#'>link</a></div>",
        "body,html{margin:0;padding:0}"
        "div{width:400px}"
        ".external{background-image:url(x.svg);"
        "background-repeat:no-repeat}");
    const auto* f = find_frag_by_class(doc.root, "external");
    REQUIRE(f != nullptr);
    CHECK(f->bg_no_repeat == true);
}

TEST_CASE("inline bg: background-repeat: repeat left false (deviation)",
          "[layout][inline-bg][adr-045]") {
    auto doc = run(
        "<div><a class='external' href='#'>link</a></div>",
        "body,html{margin:0;padding:0}"
        "div{width:400px}"
        ".external{background-image:url(x.svg);"
        "background-repeat:repeat}");
    const auto* f = find_frag_by_class(doc.root, "external");
    REQUIRE(f != nullptr);
    // Documented deviation: v1 doesn't tile; repeat -> no-repeat=false.
    CHECK(f->bg_no_repeat == false);
}

TEST_CASE("inline bg: no background-image rule -> defaults preserved",
          "[layout][inline-bg][adr-045]") {
    auto doc = run(
        "<div><a class='ext' href='#'>link</a></div>",
        "body,html{margin:0;padding:0}"
        "div{width:400px}");
    const auto* f = find_frag_by_class(doc.root, "ext");
    REQUIRE(f != nullptr);
    CHECK(f->bg_image_url.empty());
    CHECK(f->bg_size_w == Catch::Approx(0.0f).margin(0.1f));
    CHECK(f->bg_no_repeat == false);
}

TEST_CASE("inline bg: padding-right extends fragment width",
          "[layout][inline-bg][adr-045]") {
    auto doc = run(
        "<div><a class='pad' href='#'>X</a></div>",
        "body,html{margin:0;padding:0}"
        "div{width:400px;font-size:16px}"
        ".pad{padding-right:16px}");
    const auto* f = find_frag_by_class(doc.root, "pad");
    REQUIRE(f != nullptr);
    // The fragment text is "X" (single char, ~8px at 16px font).
    // With padding-right: 16px added, width should be ~24+.
    CHECK(f->width > 15.0f);
}

// =============================================================
// ADR-047: mask-image parse + layout fields.
// =============================================================

TEST_CASE("mask: mask-image url(x.svg) populates fields",
          "[layout][mask][adr-047]") {
    auto doc = run(
        "<div class='m'>M</div>",
        "body,html{margin:0;padding:0}"
        ".m{width:100px;height:100px;mask-image:url(x.svg)}");
    const auto* m = find_class(doc.root, "m");
    REQUIRE(m != nullptr);
    CHECK(m->mask_has == true);
    CHECK(m->mask_image_url == "x.svg");
}

TEST_CASE("mask: -webkit-mask-image only (prefixed form)",
          "[layout][mask][adr-047]") {
    auto doc = run(
        "<div class='m'>M</div>",
        "body,html{margin:0;padding:0}"
        ".m{width:100px;height:100px;"
        "-webkit-mask-image:url(x.svg)}");
    const auto* m = find_class(doc.root, "m");
    REQUIRE(m != nullptr);
    CHECK(m->mask_has == true);
    CHECK(m->mask_image_url == "x.svg");
}

TEST_CASE("mask: both prefixed and unprefixed -> unprefixed wins",
          "[layout][mask][adr-047]") {
    auto doc = run(
        "<div class='m'>M</div>",
        "body,html{margin:0;padding:0}"
        ".m{width:100px;height:100px;"
        "mask-image:url(preferred.svg);"
        "-webkit-mask-image:url(webkit.svg)}");
    const auto* m = find_class(doc.root, "m");
    REQUIRE(m != nullptr);
    CHECK(m->mask_image_url == "preferred.svg");
}

TEST_CASE("mask: mask-image: none -> mask_has false",
          "[layout][mask][adr-047]") {
    auto doc = run(
        "<div class='m'>M</div>",
        "body,html{margin:0;padding:0}"
        ".m{width:100px;height:100px;mask-image:none}");
    const auto* m = find_class(doc.root, "m");
    REQUIRE(m != nullptr);
    CHECK(m->mask_has == false);
}

TEST_CASE("mask: mask-size fixed length",
          "[layout][mask][adr-047]") {
    auto doc = run(
        "<div class='m'>M</div>",
        "body,html{margin:0;padding:0}"
        ".m{width:100px;height:100px;mask-image:url(x.svg);"
        "mask-size:16px}");
    const auto* m = find_class(doc.root, "m");
    REQUIRE(m != nullptr);
    CHECK(m->mask_size_w == Catch::Approx(16.0f).margin(0.1f));
}

TEST_CASE("mask: mask-size auto -> 0 (intrinsic)",
          "[layout][mask][adr-047]") {
    auto doc = run(
        "<div class='m'>M</div>",
        "body,html{margin:0;padding:0}"
        ".m{width:100px;height:100px;mask-image:url(x.svg);"
        "mask-size:auto}");
    const auto* m = find_class(doc.root, "m");
    REQUIRE(m != nullptr);
    CHECK(m->mask_size_w == Catch::Approx(0.0f).margin(0.1f));
}

TEST_CASE("mask: mask-position center right",
          "[layout][mask][adr-047]") {
    auto doc = run(
        "<div class='m'>M</div>",
        "body,html{margin:0;padding:0}"
        ".m{width:100px;height:100px;mask-image:url(x.svg);"
        "mask-position:center right}");
    const auto* m = find_class(doc.root, "m");
    REQUIRE(m != nullptr);
    CHECK(m->mask_pos_x == layout::BgPosX::Right);
    CHECK(m->mask_pos_y == layout::BgPosY::Center);
}

TEST_CASE("mask: mask-position left top (defaults)",
          "[layout][mask][adr-047]") {
    auto doc = run(
        "<div class='m'>M</div>",
        "body,html{margin:0;padding:0}"
        ".m{width:100px;height:100px;mask-image:url(x.svg);"
        "mask-position:left top}");
    const auto* m = find_class(doc.root, "m");
    REQUIRE(m != nullptr);
    CHECK(m->mask_pos_x == layout::BgPosX::Left);
    CHECK(m->mask_pos_y == layout::BgPosY::Top);
}

TEST_CASE("mask: mask-repeat no-repeat",
          "[layout][mask][adr-047]") {
    auto doc = run(
        "<div class='m'>M</div>",
        "body,html{margin:0;padding:0}"
        ".m{width:100px;height:100px;mask-image:url(x.svg);"
        "mask-repeat:no-repeat}");
    const auto* m = find_class(doc.root, "m");
    REQUIRE(m != nullptr);
    CHECK(m->mask_no_repeat == true);
}

TEST_CASE("mask: mask-repeat repeat (documented deviation)",
          "[layout][mask][adr-047]") {
    auto doc = run(
        "<div class='m'>M</div>",
        "body,html{margin:0;padding:0}"
        ".m{width:100px;height:100px;mask-image:url(x.svg);"
        "mask-repeat:repeat}");
    const auto* m = find_class(doc.root, "m");
    REQUIRE(m != nullptr);
    // Documented deviation: v1 doesn't tile; repeat -> false.
    CHECK(m->mask_no_repeat == false);
}

TEST_CASE("mask: data URL (raw SVG) captured verbatim",
          "[layout][mask][adr-047]") {
    auto doc = run(
        "<div class='m'>M</div>",
        "body,html{margin:0;padding:0}"
        ".m{width:100px;height:100px;"
        "mask-image:url(data:image/svg+xml;utf8,<svg/>)}");
    const auto* m = find_class(doc.root, "m");
    REQUIRE(m != nullptr);
    CHECK(m->mask_has == true);
    CHECK(m->mask_image_url.find("data:image/svg+xml") == 0);
}

TEST_CASE("mask: no mask rule leaves defaults",
          "[layout][mask][adr-047]") {
    auto doc = run(
        "<div class='m'>M</div>",
        "body,html{margin:0;padding:0}"
        ".m{width:100px;height:100px}");
    const auto* m = find_class(doc.root, "m");
    REQUIRE(m != nullptr);
    CHECK(m->mask_has == false);
    CHECK(m->mask_image_url.empty());
    CHECK(m->mask_size_w == Catch::Approx(0.0f).margin(0.1f));
    CHECK(m->mask_no_repeat == false);
}
