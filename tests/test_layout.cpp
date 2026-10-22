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
#include "renderer/layout/style_values.hpp"
#include "renderer/layout/text_utils.hpp"

#include <catch2/catch_test_macros.hpp>

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
    auto doc = run("<p>test</p>", "p { display: grid }");
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
        .color = layout::Color::black()};
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
