// =============================================================
// Tests for the cascade (Commit 1: matching + specificity
// ordering). Inheritance tests arrive in Commit 2.
// =============================================================

#include "renderer/css/cascade.hpp"
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
