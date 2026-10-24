#include <catch2/catch_test_macros.hpp>

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
