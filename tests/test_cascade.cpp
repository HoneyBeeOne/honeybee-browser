// =============================================================
// Tests for the cascade (Commit 1: matching + specificity
// ordering). Inheritance tests arrive in Commit 2.
// =============================================================

#include "renderer/css/cascade.hpp"
#include "renderer/css/render_state.hpp"
#include "renderer/css/stylesheet.hpp"
#include "renderer/dom/node.hpp"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>

using honeybee::renderer::css::cascade;
using honeybee::renderer::css::Stylesheet;

namespace {

namespace dom = honeybee::renderer::dom;

std::unique_ptr<dom::Document> make_simple_doc(const std::string& p_id = "") {
    auto doc = std::make_unique<dom::Document>();
    auto html = std::make_unique<dom::Element>("html");
    auto body = std::make_unique<dom::Element>("body");
    auto p = std::make_unique<dom::Element>("p");
    if (!p_id.empty()) p->set_attribute("id", p_id);
    p->append_child(std::make_unique<dom::TextNode>("Hi"));
    body->append_child(std::move(p));
    html->append_child(std::move(body));
    doc->append_child(std::move(html));
    return doc;
}

std::unique_ptr<dom::Document> make_nested_doc() {
    auto doc = std::make_unique<dom::Document>();
    auto html = std::make_unique<dom::Element>("html");
    auto body = std::make_unique<dom::Element>("body");
    auto div = std::make_unique<dom::Element>("div");
    auto p = std::make_unique<dom::Element>("p");
    p->append_child(std::make_unique<dom::TextNode>("Hi"));
    div->append_child(std::move(p));
    body->append_child(std::move(div));
    html->append_child(std::move(body));
    doc->append_child(std::move(html));
    return doc;
}

} // namespace

TEST_CASE("cascade: single matching rule", "[css][cascade]") {
    auto doc = make_simple_doc();
    const auto* p = doc->find_element("p");
    REQUIRE(p);

    const auto result = cascade(Stylesheet::parse("p { color: red }"), *doc);
    const auto& style = result.at(p);
    CHECK(style.get("color").value_or("") == "red");
}

TEST_CASE("cascade: later rule wins with same specificity",
          "[css][cascade]") {
    auto doc = make_simple_doc();
    const auto* p = doc->find_element("p");
    REQUIRE(p);

    const auto result = cascade(
        Stylesheet::parse("p { color: red } p { color: blue }"), *doc);
    CHECK(result.at(p).get("color").value_or("") == "blue");
}

TEST_CASE("cascade: !important beats normal at any specificity",
          "[css][cascade]") {
    auto doc = make_simple_doc("x");
    const auto* p = doc->find_element("p");
    REQUIRE(p);

    const auto result = cascade(
        Stylesheet::parse("p { color: red !important } #x { color: blue }"),
        *doc);
    CHECK(result.at(p).get("color").value_or("") == "red");
}

TEST_CASE("cascade: specificity beats source order",
          "[css][cascade]") {
    auto doc = make_simple_doc("x");
    const auto* p = doc->find_element("p");
    REQUIRE(p);

    const auto result = cascade(
        Stylesheet::parse("#x { color: red } p { color: blue }"), *doc);
    CHECK(result.at(p).get("color").value_or("") == "red");
}

TEST_CASE("cascade: non-matching selector contributes nothing",
          "[css][cascade]") {
    auto doc = make_simple_doc();
    const auto* p = doc->find_element("p");
    REQUIRE(p);

    const auto result = cascade(
        Stylesheet::parse("div { color: red }"), *doc);
    CHECK_FALSE(result.at(p).get("color").has_value());
}

TEST_CASE("cascade: empty declaration block yields empty style",
          "[css][cascade]") {
    auto doc = make_simple_doc();
    const auto* p = doc->find_element("p");
    REQUIRE(p);

    const auto result = cascade(Stylesheet::parse("p {}"), *doc);
    CHECK(result.at(p).declarations.empty());
}

TEST_CASE("cascade: multiple declarations in one rule",
          "[css][cascade]") {
    auto doc = make_simple_doc();
    const auto* p = doc->find_element("p");
    REQUIRE(p);

    const auto result = cascade(
        Stylesheet::parse("p { color: red; font-size: 14px }"), *doc);
    const auto& style = result.at(p);
    CHECK(style.get("color").value_or("") == "red");
    CHECK(style.get("font-size").value_or("") == "14px");
}

TEST_CASE("cascade: both important, specificity decides",
          "[css][cascade]") {
    auto doc = make_simple_doc("x");
    const auto* p = doc->find_element("p");
    REQUIRE(p);

    const auto result = cascade(
        Stylesheet::parse(
            "#x { color: red !important } p { color: blue !important }"),
        *doc);
    CHECK(result.at(p).get("color").value_or("") == "red");
}

TEST_CASE("cascade: descendant selector matches",
          "[css][cascade]") {
    auto doc = make_nested_doc();
    const auto* p = doc->find_element("p");
    REQUIRE(p);

    const auto result = cascade(
        Stylesheet::parse("div p { color: red }"), *doc);
    CHECK(result.at(p).get("color").value_or("") == "red");
}

TEST_CASE("cascade: later declaration in same rule wins",
          "[css][cascade]") {
    auto doc = make_simple_doc();
    const auto* p = doc->find_element("p");
    REQUIRE(p);

    const auto result = cascade(
        Stylesheet::parse("p { color: red; color: blue }"), *doc);
    CHECK(result.at(p).get("color").value_or("") == "blue");
}

// =============================================================
// Inheritance tests (Commit 2).
// =============================================================

TEST_CASE("cascade: inherited property flows to descendant",
          "[css][cascade][inheritance]") {
    auto doc = make_simple_doc();
    const auto* p = doc->find_element("p");
    REQUIRE(p);

    const auto result = cascade(Stylesheet::parse("body { color: navy }"), *doc);
    CHECK(result.at(p).get("color").value_or("") == "navy");
}

TEST_CASE("cascade: own declaration beats inherited",
          "[css][cascade][inheritance]") {
    auto doc = make_simple_doc();
    const auto* p = doc->find_element("p");
    REQUIRE(p);

    const auto result = cascade(
        Stylesheet::parse("body { color: navy } p { color: red }"), *doc);
    CHECK(result.at(p).get("color").value_or("") == "red");
}

TEST_CASE("cascade: non-inherited property does not propagate",
          "[css][cascade][inheritance]") {
    auto doc = make_simple_doc();
    const auto* p = doc->find_element("p");
    REQUIRE(p);

    const auto result = cascade(
        Stylesheet::parse("body { display: block }"), *doc);
    CHECK_FALSE(result.at(p).get("display").has_value());
}

// =============================================================
// Origin-aware cascade (ADR-014).
// =============================================================

TEST_CASE("cascade: author * beats UA h1 on origin tie-break",
          "[css][cascade][origin]") {
    // The canonical case from CSS 2.1 section 6.4.1: author `*`
    // (specificity 0,0,0) beats UA `p` (specificity 0,0,1) because
    // author origin wins before specificity is compared. With naive
    // concatenation, UA specificity would win and color would stay
    // navy. Origin-aware cascade makes it red.
    auto doc = make_simple_doc();
    const auto* p = doc->find_element("p");
    REQUIRE(p);

    const auto ua = Stylesheet::parse("p { color: navy }");
    const auto author = Stylesheet::parse("* { color: red }");
    const auto result = cascade(ua, author, *doc);
    CHECK(result.at(p).get("color").value_or("") == "red");
}

TEST_CASE("cascade: author p beats UA p",
          "[css][cascade][origin]") {
    auto doc = make_simple_doc();
    const auto* p = doc->find_element("p");
    REQUIRE(p);

    const auto ua = Stylesheet::parse("p { color: navy }");
    const auto author = Stylesheet::parse("p { color: green }");
    const auto result = cascade(ua, author, *doc);
    CHECK(result.at(p).get("color").value_or("") == "green");
}

TEST_CASE("cascade: UA applies when no author rule matches",
          "[css][cascade][origin]") {
    auto doc = make_simple_doc();
    const auto* p = doc->find_element("p");
    REQUIRE(p);

    const auto ua = Stylesheet::parse("p { color: navy }");
    const auto author = Stylesheet::parse("div { color: red }");
    const auto result = cascade(ua, author, *doc);
    CHECK(result.at(p).get("color").value_or("") == "navy");
}

TEST_CASE("cascade: single-sheet overload still works",
          "[css][cascade][origin]") {
    auto doc = make_simple_doc();
    const auto* p = doc->find_element("p");
    REQUIRE(p);

    const auto result = cascade(
        Stylesheet::parse("p { color: red }"), *doc);
    CHECK(result.at(p).get("color").value_or("") == "red");
}

namespace css = honeybee::renderer::css;

namespace {

std::unique_ptr<dom::Document> make_anchor_doc(const std::string& href) {
    auto doc = std::make_unique<dom::Document>();
    auto html = std::make_unique<dom::Element>("html");
    auto body = std::make_unique<dom::Element>("body");
    auto a = std::make_unique<dom::Element>("a");
    a->set_attribute("href", href);
    a->append_child(std::make_unique<dom::TextNode>("link"));
    body->append_child(std::move(a));
    html->append_child(std::move(body));
    doc->append_child(std::move(html));
    return doc;
}

} // namespace

// =============================================================
// Pseudo-class matching in cascade (ADR-018).
// =============================================================

TEST_CASE("cascade: :link matches unvisited anchor", "[css][cascade][pseudo]") {
    auto doc = make_anchor_doc("https://example.com/page");
    const auto* a = doc->find_element("a");
    REQUIRE(a);

    css::RenderState state;
    state.base_url = "https://example.com";
    // visited_urls empty -> :link matches

    const auto author = Stylesheet::parse("a:link { color: red }");
    const auto result = cascade(Stylesheet{}, author, *doc, state);
    CHECK(result.at(a).get("color").value_or("") == "red");
}

TEST_CASE("cascade: :link does not match visited anchor",
          "[css][cascade][pseudo]") {
    auto doc = make_anchor_doc("https://example.com/page");
    const auto* a = doc->find_element("a");
    REQUIRE(a);

    css::RenderState state;
    state.base_url = "https://example.com";
    state.visited_urls.insert("https://example.com/page");

    const auto author = Stylesheet::parse("a:link { color: red }");
    const auto result = cascade(Stylesheet{}, author, *doc, state);
    CHECK_FALSE(result.at(a).has("color"));
}

TEST_CASE("cascade: :visited matches visited anchor",
          "[css][cascade][pseudo]") {
    auto doc = make_anchor_doc("https://example.com/page");
    const auto* a = doc->find_element("a");
    REQUIRE(a);

    css::RenderState state;
    state.base_url = "https://example.com";
    state.visited_urls.insert("https://example.com/page");

    const auto author = Stylesheet::parse("a:visited { color: purple }");
    const auto result = cascade(Stylesheet{}, author, *doc, state);
    CHECK(result.at(a).get("color").value_or("") == "purple");
}

TEST_CASE("cascade: :visited resolves relative URL against base",
          "[css][cascade][pseudo]") {
    auto doc = make_anchor_doc("/about");
    const auto* a = doc->find_element("a");
    REQUIRE(a);

    css::RenderState state;
    state.base_url = "https://example.com";
    state.visited_urls.insert("https://example.com/about");

    const auto author = Stylesheet::parse("a:visited { color: purple }");
    const auto result = cascade(Stylesheet{}, author, *doc, state);
    CHECK(result.at(a).get("color").value_or("") == "purple");
}
