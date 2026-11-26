// =============================================================
// Tests for CSS value function evaluation (ADR-037).
//
// Covers calc(), min(), max(), clamp() at the parser level,
// plus var()-inside-calc resolution at the cascade level.
// =============================================================

#include "renderer/layout/style_values.hpp"
#include "renderer/css/stylesheet.hpp"
#include "renderer/css/cascade.hpp"
#include "renderer/css/ua_stylesheet.hpp"
#include "renderer/html/convert.hpp"
#include "renderer/css/render_state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <functional>

using honeybee::renderer::layout::LengthContext;
using honeybee::renderer::layout::parse_length;

namespace {

LengthContext base_ctx() {
    LengthContext ctx;
    ctx.font_size = 16.0f;
    ctx.parent_font_size = 16.0f;
    ctx.root_font_size = 16.0f;
    ctx.parent_content_width = 800.0f;
    return ctx;
}

bool near(float a, float b, float tol = 0.1f) {
    return std::abs(a - b) < tol;
}

}  // namespace

// =============================================================
// calc() arithmetic — operator precedence + nesting
// =============================================================

TEST_CASE("calc: simple addition", "[css][calc][adr-037]") {
    auto ctx = base_ctx();
    CHECK(near(parse_length("calc(10px + 5px)", ctx), 15.0f));
}

TEST_CASE("calc: subtraction", "[css][calc][adr-037]") {
    auto ctx = base_ctx();
    CHECK(near(parse_length("calc(100px - 20px)", ctx), 80.0f));
}

TEST_CASE("calc: multiplication", "[css][calc][adr-037]") {
    auto ctx = base_ctx();
    CHECK(near(parse_length("calc(10px * 2)", ctx), 20.0f));
}

TEST_CASE("calc: division", "[css][calc][adr-037]") {
    auto ctx = base_ctx();
    CHECK(near(parse_length("calc(100px / 4)", ctx), 25.0f));
}

TEST_CASE("calc: precedence * before +", "[css][calc][adr-037]") {
    auto ctx = base_ctx();
    CHECK(near(parse_length("calc(10px + 5px * 2)", ctx), 20.0f));
}

TEST_CASE("calc: parens override precedence", "[css][calc][adr-037]") {
    auto ctx = base_ctx();
    CHECK(near(parse_length("calc((10px + 5px) * 2)", ctx), 30.0f));
}

TEST_CASE("calc: unary negation", "[css][calc][adr-037]") {
    auto ctx = base_ctx();
    CHECK(near(parse_length("calc(-1 * 12px)", ctx), -12.0f));
}

TEST_CASE("calc: nested calc()", "[css][calc][adr-037]") {
    auto ctx = base_ctx();
    CHECK(near(parse_length("calc(calc(10px + 5px) * 2)", ctx), 30.0f));
}

// =============================================================
// calc() unit handling
// =============================================================

TEST_CASE("calc: mixed em + px", "[css][calc][adr-037]") {
    auto ctx = base_ctx();
    CHECK(near(parse_length("calc(2em + 8px)", ctx), 40.0f));
}

TEST_CASE("calc: mixed % - px", "[css][calc][adr-037]") {
    auto ctx = base_ctx();
    CHECK(near(parse_length("calc(100% - 20px)", ctx), 780.0f));
}

TEST_CASE("calc: rem + px", "[css][calc][adr-037]") {
    auto ctx = base_ctx();
    CHECK(near(parse_length("calc(1rem + 4px)", ctx), 20.0f));
}

TEST_CASE("calc: vh resolves via viewport_h (ADR-051)",
          "[css][calc][adr-037]") {
    auto ctx = base_ctx();
    // ADR-051: vh is now supported. With viewport_h=0 (the
    // LengthContext default), 100vh = 0, so calc() yields
    // -48. Pre-ADR-051 the whole expression was invalid -> 0.
    CHECK(near(parse_length("calc(100vh - 48px)", ctx), -48.0f));
}

// =============================================================
// min / max / clamp
// =============================================================

TEST_CASE("min: returns smallest", "[css][calc][adr-037]") {
    auto ctx = base_ctx();
    CHECK(near(parse_length("min(10px, 20px)", ctx), 10.0f));
    CHECK(near(parse_length("min(30px, 10px, 20px)", ctx), 10.0f));
}

TEST_CASE("max: returns largest", "[css][calc][adr-037]") {
    auto ctx = base_ctx();
    CHECK(near(parse_length("max(10px, 20px)", ctx), 20.0f));
    CHECK(near(parse_length("max(5px, 30px, 20px)", ctx), 30.0f));
}

TEST_CASE("clamp: within bounds", "[css][calc][adr-037]") {
    auto ctx = base_ctx();
    CHECK(near(parse_length("clamp(10px, 15px, 20px)", ctx), 15.0f));
}

TEST_CASE("clamp: clamped to min", "[css][calc][adr-037]") {
    auto ctx = base_ctx();
    CHECK(near(parse_length("clamp(10px, 5px, 20px)", ctx), 10.0f));
}

TEST_CASE("clamp: clamped to max", "[css][calc][adr-037]") {
    auto ctx = base_ctx();
    CHECK(near(parse_length("clamp(10px, 25px, 20px)", ctx), 20.0f));
}

// =============================================================
// Non-calc regression (make sure we didn't break existing paths)
// =============================================================

TEST_CASE("calc: plain px unchanged", "[css][calc][adr-037]") {
    auto ctx = base_ctx();
    CHECK(near(parse_length("10px", ctx), 10.0f));
}

TEST_CASE("calc: em unchanged", "[css][calc][adr-037]") {
    auto ctx = base_ctx();
    CHECK(near(parse_length("2em", ctx), 32.0f));
}

TEST_CASE("calc: auto still 0", "[css][calc][adr-037]") {
    auto ctx = base_ctx();
    CHECK(near(parse_length("auto", ctx), 0.0f));
}

// =============================================================
// var()-inside-calc resolution at cascade level
// =============================================================

namespace {

using honeybee::renderer::css::Stylesheet;
using honeybee::renderer::css::cascade;
using honeybee::renderer::css::ua_stylesheet;
using honeybee::renderer::css::RenderState;

static std::string cascade_width(
    const std::string& html,
    const std::string& css) {
    auto doc = honeybee::renderer::html::parse(html);
    auto sheet = Stylesheet::parse(css);
    auto styles = cascade(ua_stylesheet(), sheet, *doc,
                           RenderState{}, 800.0f);
    using namespace honeybee::renderer::dom;
    const Element* target = nullptr;
    std::function<void(const Node&)> find = [&](const Node& n) {
        if (n.type() == NodeType::Element) {
            const auto& el = static_cast<const Element&>(n);
            if (el.tag() == "div") target = &el;
            for (const auto& c : n.children()) find(*c);
        }
    };
    for (const auto& c : doc->children()) find(*c);
    if (!target) return "(no target)";
    auto it = styles.find(target);
    if (it == styles.end()) return "(no style)";
    auto w = it->second.get("width");
    return w ? *w : "(unset)";
}

}  // namespace

TEST_CASE("var(): whole-value resolution (baseline, ADR-026)",
          "[css][var][adr-037]") {
    CHECK(cascade_width(
        "<html><body><div>x</div></body></html>",
        ":root{--size:16px} div{width:var(--size)}") == "16px");
}

TEST_CASE("var(): fallback resolution (baseline, ADR-026)",
          "[css][var][adr-037]") {
    CHECK(cascade_width(
        "<html><body><div>x</div></body></html>",
        "div{width:var(--missing,20px)}") == "20px");
}

TEST_CASE("var(): embedded in calc() resolved (ADR-037)",
          "[css][var][adr-037]") {
    CHECK(cascade_width(
        "<html><body><div>x</div></body></html>",
        ":root{--size:16px} div{width:calc(var(--size) + 4px)}")
        == "calc(16px + 4px)");
}

TEST_CASE("var(): embedded in calc() with fallback (ADR-037)",
          "[css][var][adr-037]") {
    CHECK(cascade_width(
        "<html><body><div>x</div></body></html>",
        "div{width:calc(var(--missing,16px) + 4px)}")
        == "calc(16px + 4px)");
}

TEST_CASE("var(): embedded in composite value resolved (ADR-037)",
          "[css][var][adr-037]") {
    CHECK(cascade_width(
        "<html><body><div>x</div></body></html>",
        "div{width:1px solid var(--border-color, #ccc)}")
        == "1px solid #ccc");
}
