// =============================================================
// Tests for block-flow layout (ADR-011, Commit 1).
// Uses a fake TextMeasurer for deterministic, GTK-free tests.
// =============================================================

#include "renderer/css/cascade.hpp"
#include "renderer/css/stylesheet.hpp"
#include "renderer/html/convert.hpp"
#include "renderer/layout/layout.hpp"
#include "renderer/layout/style_values.hpp"

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

std::pair<float, float> fake_measure(const layout::LayoutText& t) {
    return {static_cast<float>(t.text.size()) * 8.0f, 16.0f};
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
    auto t = run(
        "<html><head><title>Page Title</title></head>"
        "<body><p>Visible</p></body></html>",
        "");
    for (const auto* text : find_all_texts(t.root)) {
        CHECK(text->text.find("Page Title") == std::string::npos);
    }
    CHECK(find_all_boxes(t.root, "p").size() == 1);
}

TEST_CASE("layout: <style> is not rendered", "[layout][skip]") {
    auto t = run(
        "<body><style>p { color: red }</style><p>Hi</p></body>",
        "");
    for (const auto* text : find_all_texts(t.root)) {
        CHECK(text->text.find("color") == std::string::npos);
    }
}

TEST_CASE("layout: <script> is not rendered", "[layout][skip]") {
    auto t = run(
        "<body><script>alert('hi')</script><p>Hi</p></body>",
        "");
    for (const auto* text : find_all_texts(t.root)) {
        CHECK(text->text.find("alert") == std::string::npos);
    }
}
