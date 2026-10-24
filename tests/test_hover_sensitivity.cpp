#include <catch2/catch_test_macros.hpp>

#include <functional>

#include "renderer/css/hover_sensitivity.hpp"
#include "renderer/css/render_state.hpp"
#include "renderer/css/stylesheet.hpp"
#include "renderer/css/ua_stylesheet.hpp"
#include "renderer/dom/node.hpp"
#include "renderer/html/convert.hpp"

using honeybee::renderer::css::compute_hover_sensitive;
using honeybee::renderer::css::detect_level;
using honeybee::renderer::css::HoverLevel;
using honeybee::renderer::css::RenderState;
using honeybee::renderer::css::Stylesheet;
using honeybee::renderer::css::ua_stylesheet;

TEST_CASE("detect_level: no :hover rules -> None", "[hover-sensitivity]") {
    const auto empty = Stylesheet::parse("");
    const auto no_hover = Stylesheet::parse("a { color: red } p { margin: 0 }");
    CHECK(detect_level(empty, empty) == HoverLevel::None);
    CHECK(detect_level(no_hover, empty) == HoverLevel::None);
    CHECK(detect_level(empty, no_hover) == HoverLevel::None);
}

TEST_CASE("detect_level: a:hover only -> AnchorOnly", "[hover-sensitivity]") {
    const auto sheet = Stylesheet::parse("a:hover { color: red }");
    CHECK(detect_level(sheet, Stylesheet::parse("")) ==
          HoverLevel::AnchorOnly);
    // UA sheet has only `a:hover` -- the current production state.
    CHECK(detect_level(ua_stylesheet(), Stylesheet::parse("")) ==
          HoverLevel::AnchorOnly);
}

TEST_CASE("detect_level: compound :hover -> Compound", "[hover-sensitivity]") {
    const auto descendant = Stylesheet::parse("div:hover a { color: red }");
    CHECK(detect_level(descendant, Stylesheet::parse("")) ==
          HoverLevel::Compound);

    const auto child = Stylesheet::parse("div:hover > a { color: red }");
    CHECK(detect_level(child, Stylesheet::parse("")) ==
          HoverLevel::Compound);

    const auto two_hover = Stylesheet::parse(
        "a:hover b:hover { color: red }");
    CHECK(detect_level(two_hover, Stylesheet::parse("")) ==
          HoverLevel::Compound);
}

TEST_CASE("detect_level: mixed -> Compound (max wins)", "[hover-sensitivity]") {
    const auto sheet = Stylesheet::parse(
        "a:hover { color: red }"
        "div:hover a { color: blue }");
    CHECK(detect_level(sheet, Stylesheet::parse("")) ==
          HoverLevel::Compound);
}

TEST_CASE("compute_hover_sensitive: only <a> elements flagged",
          "[hover-sensitivity]") {
    auto doc = honeybee::renderer::html::parse(
        "<html><body><p>before <a href=\"#\">x</a> after</p>"
        "</body></html>");
    REQUIRE(doc);
    const auto* a = doc->find_element("a");
    const auto* p = doc->find_element("p");
    REQUIRE(a);
    REQUIRE(p);

    const auto ua = ua_stylesheet();
    const auto author = Stylesheet::parse("");
    RenderState state;
    const auto sensitive = compute_hover_sensitive(
        *doc, ua, author, state);

    CHECK(sensitive.count(a) == 1);
    CHECK(sensitive.count(p) == 0);
}

TEST_CASE("compute_hover_sensitive: no <a> in document -> empty set",
          "[hover-sensitivity]") {
    auto doc = honeybee::renderer::html::parse(
        "<html><body><div><span>x</span></div></body></html>");
    REQUIRE(doc);

    const auto ua = ua_stylesheet();
    const auto author = Stylesheet::parse("");
    RenderState state;
    const auto sensitive = compute_hover_sensitive(
        *doc, ua, author, state);

    CHECK(sensitive.empty());
}

// =============================================================
// ADR-024: helper extraction tests.
// =============================================================

TEST_CASE("collect_anchor_hover_selectors: filters by anchor position",
          "[hover-sensitivity]") {
    using honeybee::renderer::css::collect_anchor_hover_selectors;

    const auto empty = Stylesheet::parse("");
    CHECK(collect_anchor_hover_selectors(empty, empty).empty());
    // UA has exactly `a:hover`.
    CHECK(collect_anchor_hover_selectors(ua_stylesheet(), empty).size() == 1);

    // `div:hover > a`: :hover in non-anchor position -- not collected.
    const auto compound = Stylesheet::parse("div:hover > a { color: red }");
    CHECK(collect_anchor_hover_selectors(compound, empty).empty());

    // Multiple anchor-hover selectors: both collected.
    const auto multi = Stylesheet::parse(
        "a:hover { color: red } p:hover { color: blue }");
    CHECK(collect_anchor_hover_selectors(multi, empty).size() == 2);
}

TEST_CASE("matches_any_anchor_hover agrees with compute_hover_sensitive",
          "[hover-sensitivity]") {
    // Behavior-preservation test: helper called in precompute mode
    // (state.hovered_element = &el) must equal membership in the
    // set returned by compute_hover_sensitive.
    using honeybee::renderer::css::collect_anchor_hover_selectors;
    using honeybee::renderer::css::matches_any_anchor_hover;

    auto doc = honeybee::renderer::html::parse(
        "<html><body><p>before <a href=\"#\">x</a> after</p>"
        "</body></html>");
    REQUIRE(doc);

    const auto ua = ua_stylesheet();
    const auto author = Stylesheet::parse("");
    const auto anchor_hover = collect_anchor_hover_selectors(ua, author);
    const auto sensitive = compute_hover_sensitive(
        *doc, ua, author, RenderState{});

    std::function<void(const honeybee::renderer::dom::Node&)> walk =
        [&](const honeybee::renderer::dom::Node& n) {
            using honeybee::renderer::dom::NodeType;
            using honeybee::renderer::dom::Element;
            if (n.type() == NodeType::Element) {
                const auto& el = static_cast<const Element&>(n);
                RenderState self_hovered;
                self_hovered.hovered_element = &el;
                const bool via_helper = matches_any_anchor_hover(
                    el, self_hovered, anchor_hover);
                const bool via_precompute = sensitive.count(&el) > 0;
                CHECK(via_helper == via_precompute);
            }
            for (const auto& child : n.children()) walk(*child);
        };
    walk(*doc);
}

TEST_CASE("matches_any_anchor_hover: <span> in <a> transitions via ancestor",
          "[hover-sensitivity]") {
    // The subtle case from ADR-024: hovering a <span> INSIDE an
    // <a> causes the ancestor <a> to match a:hover (per ADR-018's
    // walk-up matcher). The transition check must catch this --
    // a per-hovered-element check would miss it.
    using honeybee::renderer::css::collect_anchor_hover_selectors;
    using honeybee::renderer::css::matches_any_anchor_hover;

    auto doc = honeybee::renderer::html::parse(
        "<html><body><a href=\"#\"><span>text</span></a></body></html>");
    REQUIRE(doc);
    const auto* a = doc->find_element("a");
    const auto* span = doc->find_element("span");
    REQUIRE(a);
    REQUIRE(span);

    const auto ua = ua_stylesheet();
    const auto author = Stylesheet::parse("");
    const auto anchor_hover = collect_anchor_hover_selectors(ua, author);
    REQUIRE(anchor_hover.size() == 1);  // UA a:hover

    // <span> hovered -> <a> matches a:hover (span is descendant of a).
    RenderState span_hovered;
    span_hovered.hovered_element = span;
    CHECK(matches_any_anchor_hover(*a, span_hovered, anchor_hover));

    // Nothing hovered -> <a> does not match a:hover.
    RenderState nothing_hovered;
    nothing_hovered.hovered_element = nullptr;
    CHECK_FALSE(matches_any_anchor_hover(*a, nothing_hovered, anchor_hover));

    // The transition check fires: true -> false on unhover.
    CHECK(matches_any_anchor_hover(*a, span_hovered, anchor_hover)
       != matches_any_anchor_hover(*a, nothing_hovered, anchor_hover));
}
