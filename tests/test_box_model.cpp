// =============================================================
// Tests for box model completion (ADR-039 Commit 1).
//
// Covers height application, min/max clamps, and overflow: hidden.
// box-sizing: border-box is Commit 2 and tested separately.
// =============================================================

#include "renderer/css/cascade.hpp"
#include "renderer/css/stylesheet.hpp"
#include "renderer/css/ua_stylesheet.hpp"
#include "renderer/css/render_state.hpp"
#include "renderer/html/convert.hpp"
#include "renderer/layout/layout.hpp"

#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <memory>
#include <string>

namespace dom = honeybee::renderer::dom;
namespace css = honeybee::renderer::css;
namespace layout = honeybee::renderer::layout;

namespace {

struct TestDoc {
    std::unique_ptr<dom::Document> doc;
    css::ComputedStyleMap styles;
    layout::LayoutBox root;
};

std::pair<float, float> fake_measure(
    std::string_view text, std::string_view /*font_family*/,
    float /*font_size*/) {
    return {static_cast<float>(text.size()) * 8.0f, 16.0f};
}

TestDoc run(const std::string& html, const std::string& css_text,
            float viewport = 800.0f) {
    TestDoc t;
    t.doc = honeybee::renderer::html::parse(html);
    auto sheet = css::Stylesheet::parse(css_text);
    t.styles = css::cascade(css::ua_stylesheet(), sheet, *t.doc,
                            css::RenderState{}, viewport);
    t.root = layout::layout(*t.doc, t.styles, fake_measure, viewport);
    return t;
}

const layout::LayoutBox* find_by_class(
    const layout::LayoutNode& n, const std::string& cls) {
    if (n.kind == layout::LayoutKind::Box) {
        const auto& b = static_cast<const layout::LayoutBox&>(n);
        if (b.element &&
            b.element->get_attribute("class").find(cls) != std::string::npos) {
            return &b;
        }
        for (const auto& c : b.children) {
            if (auto r = find_by_class(*c, cls)) return r;
        }
    } else if (n.kind == layout::LayoutKind::Inline) {
        for (const auto& c :
             static_cast<const layout::LayoutInline&>(n).children) {
            if (auto r = find_by_class(*c, cls)) return r;
        }
    } else if (n.kind == layout::LayoutKind::InlineContext) {
        for (const auto& c :
             static_cast<const layout::LayoutInlineContext&>(n).children) {
            if (auto r = find_by_class(*c, cls)) return r;
        }
    }
    return nullptr;
}

bool near(float a, float b, float tol = 1.0f) {
    return std::abs(a - b) < tol;
}

}  // namespace

// =============================================================
// height applied to all boxes
// =============================================================

TEST_CASE("height: applied to plain div", "[layout][box-model][adr-039]") {
    auto t = run(
        "<html><body><div class='x'>A</div></body></html>",
        "body,html{margin:0;padding:0} .x{height:100px}");
    const auto* x = find_by_class(t.root, "x");
    REQUIRE(x != nullptr);
    CHECK(near(x->height, 100.0f));
}

TEST_CASE("height: content taller than explicit height",
          "[layout][box-model][adr-039]") {
    auto t = run(
        "<html><body><div class='x'>"
        "<p>line1</p><p>line2</p><p>line3</p>"
        "</div></body></html>",
        "body,html{margin:0;padding:0} .x{height:20px}");
    const auto* x = find_by_class(t.root, "x");
    REQUIRE(x != nullptr);
    // Explicit height caps the box
    CHECK(near(x->height, 20.0f));
}

TEST_CASE("height: percentage skipped (parent_content_height deferred)",
          "[layout][box-model][adr-039]") {
    // v1: LengthContext has only parent_content_width, no
    // parent_content_height. Percentage heights would resolve
    // against WIDTH which is semantically wrong (and catastrophic
    // for height:100% on <html>/<body>). Skip until threading.
    auto t = run(
        "<html><body><div class='x'>A</div></body></html>",
        "body,html{margin:0;padding:0;height:100%} .x{height:100%}");
    const auto* x = find_by_class(t.root, "x");
    REQUIRE(x != nullptr);
    // Height derives from content (percentage skipped)
    CHECK(x->height > 0.0f);
    CHECK(x->height < 100.0f);
}

TEST_CASE("height: auto is no-op", "[layout][box-model][adr-039]") {
    auto t = run(
        "<html><body><div class='x'>line1</div></body></html>",
        "body,html{margin:0;padding:0} .x{height:auto}");
    const auto* x = find_by_class(t.root, "x");
    REQUIRE(x != nullptr);
    // Height derives from content (line height + margins)
    CHECK(x->height > 0.0f);
    CHECK(x->height < 100.0f);
}

// =============================================================
// min-width / max-width
// =============================================================

TEST_CASE("min-width: raises content width",
          "[layout][box-model][adr-039]") {
    auto t = run(
        "<html><body><div class='x'>A</div></body></html>",
        "body,html{margin:0;padding:0} .x{width:50px;min-width:200px}");
    const auto* x = find_by_class(t.root, "x");
    REQUIRE(x != nullptr);
    CHECK(near(x->width, 200.0f));
}

TEST_CASE("max-width: caps content width",
          "[layout][box-model][adr-039]") {
    auto t = run(
        "<html><body><div class='x'>A</div></body></html>",
        "body,html{margin:0;padding:0} .x{width:500px;max-width:150px}");
    const auto* x = find_by_class(t.root, "x");
    REQUIRE(x != nullptr);
    CHECK(near(x->width, 150.0f));
}

TEST_CASE("max-width: none is no-op", "[layout][box-model][adr-039]") {
    auto t = run(
        "<html><body><div class='x'>A</div></body></html>",
        "body,html{margin:0;padding:0} .x{width:400px;max-width:none}");
    const auto* x = find_by_class(t.root, "x");
    REQUIRE(x != nullptr);
    CHECK(near(x->width, 400.0f));
}

TEST_CASE("max-width: containing vw is skipped (not collapsed)",
          "[layout][box-model][adr-039]") {
    auto t = run(
        "<html><body><div class='x'>A</div></body></html>",
        "body,html{margin:0;padding:0} .x{width:400px;max-width:min(100px, 75vw)}");
    const auto* x = find_by_class(t.root, "x");
    REQUIRE(x != nullptr);
    // vw deferred per ADR-037; clamp skipped
    CHECK(near(x->width, 400.0f));
}

// =============================================================
// min-height / max-height
// =============================================================

TEST_CASE("min-height: raises empty div",
          "[layout][box-model][adr-039]") {
    auto t = run(
        "<html><body><div class='x'></div></body></html>",
        "body,html{margin:0;padding:0} .x{min-height:50px}");
    const auto* x = find_by_class(t.root, "x");
    REQUIRE(x != nullptr);
    CHECK(near(x->height, 50.0f));
}

TEST_CASE("max-height: caps content height",
          "[layout][box-model][adr-039]") {
    auto t = run(
        "<html><body><div class='x'>"
        "<p>a</p><p>b</p><p>c</p><p>d</p><p>e</p>"
        "</div></body></html>",
        "body,html{margin:0;padding:0} .x{max-height:30px}");
    const auto* x = find_by_class(t.root, "x");
    REQUIRE(x != nullptr);
    CHECK(near(x->height, 30.0f));
}

// =============================================================
// overflow: hidden -> clip_children
// =============================================================

TEST_CASE("overflow: hidden sets clip_children",
          "[layout][box-model][adr-039]") {
    auto t = run(
        "<html><body><div class='x'>A</div></body></html>",
        "body,html{margin:0;padding:0} .x{overflow:hidden}");
    const auto* x = find_by_class(t.root, "x");
    REQUIRE(x != nullptr);
    CHECK(x->clip_children == true);
}

TEST_CASE("overflow: visible does not clip",
          "[layout][box-model][adr-039]") {
    auto t = run(
        "<html><body><div class='x'>A</div></body></html>",
        "body,html{margin:0;padding:0} .x{overflow:visible}");
    const auto* x = find_by_class(t.root, "x");
    REQUIRE(x != nullptr);
    CHECK(x->clip_children == false);
}

TEST_CASE("overflow: auto (deferred) does not clip",
          "[layout][box-model][adr-039]") {
    auto t = run(
        "<html><body><div class='x'>A</div></body></html>",
        "body,html{margin:0;padding:0} .x{overflow:auto}");
    const auto* x = find_by_class(t.root, "x");
    REQUIRE(x != nullptr);
    CHECK(x->clip_children == false);
}

TEST_CASE("overflow: hidden auto (two-value) also clips",
          "[layout][box-model][adr-039]") {
    auto t = run(
        "<html><body><div class='x'>A</div></body></html>",
        "body,html{margin:0;padding:0} .x{overflow:hidden auto}");
    const auto* x = find_by_class(t.root, "x");
    REQUIRE(x != nullptr);
    CHECK(x->clip_children == true);
}

// =============================================================
// ADR-039 Commit 2: box-sizing
//
// Note on `border-width: 5px` (not `border: 5px solid ...`):
// ADR-012's shorthand expansion covers `margin`, `padding`, and
// `border-width` as longhand aliases. It does NOT expand the
// 3-value `border` shorthand (width/style/color combined). Tests
// use `border-width: 5px` to set the width component our layout
// engine actually reads. Expanding the `border` shorthand itself
// is a separate ADR (out of ADR-039 scope).
// =============================================================

TEST_CASE("box-sizing: border-box; width includes padding+border",
          "[layout][box-model][box-sizing][adr-039]") {
    auto t = run(
        "<html><body><div class='x'>A</div></body></html>",
        "body,html{margin:0;padding:0}"
        ".x{box-sizing:border-box;width:100px;padding:10px;"
        "border-width:5px}");
    const auto* x = find_by_class(t.root, "x");
    REQUIRE(x != nullptr);
    // border-box: total = 100 (content = 100 - 20 - 10 = 70)
    CHECK(near(x->width, 100.0f));
}

TEST_CASE("box-sizing: content-box (default) width excludes padding",
          "[layout][box-model][box-sizing][adr-039]") {
    auto t = run(
        "<html><body><div class='x'>A</div></body></html>",
        "body,html{margin:0;padding:0}"
        ".x{width:100px;padding:10px;border-width:5px}");
    const auto* x = find_by_class(t.root, "x");
    REQUIRE(x != nullptr);
    // content-box: total = 100 + 20 + 10 = 130
    CHECK(near(x->width, 130.0f));
}

TEST_CASE("box-sizing: border-box; height includes padding+border",
          "[layout][box-model][box-sizing][adr-039]") {
    auto t = run(
        "<html><body><div class='x'>A</div></body></html>",
        "body,html{margin:0;padding:0}"
        ".x{box-sizing:border-box;height:100px;padding:10px;"
        "border-width:5px}");
    const auto* x = find_by_class(t.root, "x");
    REQUIRE(x != nullptr);
    CHECK(near(x->height, 100.0f));
}

TEST_CASE("box-sizing: border-box; width 100% fits parent exactly",
          "[layout][box-model][box-sizing][adr-039]") {
    auto t = run(
        "<html><body><div class='outer'>"
        "<div class='x'>A</div>"
        "</div></body></html>",
        "body,html{margin:0;padding:0}"
        ".outer{width:500px}"
        ".x{box-sizing:border-box;width:100%;padding:20px}");
    const auto* outer = find_by_class(t.root, "outer");
    const auto* x = find_by_class(t.root, "x");
    REQUIRE(outer != nullptr);
    REQUIRE(x != nullptr);
    // x fits exactly inside outer (500), not overflowing
    CHECK(near(x->width, 500.0f));
}

TEST_CASE("box-sizing: border-box; content_w clamps to 0 if "
          "padding exceeds width",
          "[layout][box-model][box-sizing][adr-039]") {
    auto t = run(
        "<html><body><div class='x'>A</div></body></html>",
        "body,html{margin:0;padding:0}"
        ".x{box-sizing:border-box;width:20px;padding:20px}");
    const auto* x = find_by_class(t.root, "x");
    REQUIRE(x != nullptr);
    // content_w = max(0, 20-40) = 0, so total box.width = 40
    CHECK(near(x->width, 40.0f));
}

TEST_CASE("box-sizing: border-box; max-width interpreted as border-box",
          "[layout][box-model][box-sizing][adr-039]") {
    auto t = run(
        "<html><body><div class='x'>A</div></body></html>",
        "body,html{margin:0;padding:0}"
        ".x{box-sizing:border-box;width:500px;padding:10px;"
        "max-width:100px}");
    const auto* x = find_by_class(t.root, "x");
    REQUIRE(x != nullptr);
    // max-width 100 = border-box dimension; content capped at 100-20=80
    CHECK(near(x->width, 100.0f));
}
