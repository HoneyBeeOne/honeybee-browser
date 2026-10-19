#include "renderer/css/stylesheet.hpp"

#include <catch2/catch_test_macros.hpp>

using honeybee::renderer::css::Stylesheet;

TEST_CASE("stylesheet: empty input yields no rules", "[css][stylesheet]") {
    CHECK(Stylesheet::parse("").rules.empty());
    CHECK(Stylesheet::parse("   \n\t  ").rules.empty());
}

TEST_CASE("stylesheet: empty declaration block", "[css][stylesheet]") {
    const auto s = Stylesheet::parse("h1 {}");
    REQUIRE(s.rules.size() == 1);
    REQUIRE(s.rules[0].selectors.selectors.size() == 1);
    CHECK(s.rules[0].declarations.empty());
}

TEST_CASE("stylesheet: empty declaration block with whitespace",
          "[css][stylesheet]") {
    const auto s = Stylesheet::parse("h1 {   \n  }");
    REQUIRE(s.rules.size() == 1);
    CHECK(s.rules[0].declarations.empty());
}

TEST_CASE("stylesheet: two rules parsed in order", "[css][stylesheet]") {
    const auto s = Stylesheet::parse("h1 {} h2 {}");
    REQUIRE(s.rules.size() == 2);
}

TEST_CASE("stylesheet: selector with combinator", "[css][stylesheet]") {
    const auto s = Stylesheet::parse("div.foo > p {}");
    REQUIRE(s.rules.size() == 1);
    REQUIRE(s.rules[0].selectors.selectors.size() == 1);
    REQUIRE(s.rules[0].selectors.selectors[0].parts.size() == 2);
}

TEST_CASE("stylesheet: selector list preserved", "[css][stylesheet]") {
    const auto s = Stylesheet::parse("h1, h2, h3 {}");
    REQUIRE(s.rules.size() == 1);
    CHECK(s.rules[0].selectors.selectors.size() == 3);
}

TEST_CASE("stylesheet: empty selector skipped", "[css][stylesheet]") {
    const auto s = Stylesheet::parse("{}");
    CHECK(s.rules.empty());
}

TEST_CASE("stylesheet: malformed selector skipped", "[css][stylesheet]") {
    const auto s = Stylesheet::parse("1nval1d {}");
    CHECK(s.rules.empty());
}

TEST_CASE("stylesheet: stray close brace is skipped",
          "[css][stylesheet]") {
    const auto s = Stylesheet::parse("} h1 {}");
    REQUIRE(s.rules.size() == 1);
}

TEST_CASE("stylesheet: unterminated rule dropped", "[css][stylesheet]") {
    const auto s = Stylesheet::parse("h1 { color: red");
    CHECK(s.rules.empty());
}

TEST_CASE("stylesheet: comments stripped before parsing",
          "[css][stylesheet]") {
    const auto s = Stylesheet::parse("h1 { /* } */ } h2 {}");
    REQUIRE(s.rules.size() == 2);
}

TEST_CASE("stylesheet: comment between rules", "[css][stylesheet]") {
    const auto s = Stylesheet::parse("h1 {} /* between */ h2 {}");
    REQUIRE(s.rules.size() == 2);
}

TEST_CASE("stylesheet: block at-rule skipped", "[css][stylesheet]") {
    const auto s = Stylesheet::parse("@media screen { div {} } h1 {}");
    REQUIRE(s.rules.size() == 1);
    REQUIRE(s.rules[0].selectors.selectors.size() == 1);
    CHECK(s.rules[0].selectors.selectors[0].parts.size() == 1);
}

TEST_CASE("stylesheet: semicolon at-rule skipped", "[css][stylesheet]") {
    const auto s = Stylesheet::parse("@import url(\"x.css\"); h1 {}");
    REQUIRE(s.rules.size() == 1);
}

// =============================================================
// Declaration parsing (Commit 2).
// =============================================================

TEST_CASE("stylesheet: single declaration", "[css][stylesheet]") {
    const auto s = Stylesheet::parse("h1 { color: red; }");
    REQUIRE(s.rules.size() == 1);
    REQUIRE(s.rules[0].declarations.size() == 1);
    CHECK(s.rules[0].declarations[0].property == "color");
    CHECK(s.rules[0].declarations[0].value == "red");
    CHECK_FALSE(s.rules[0].declarations[0].important);
}

TEST_CASE("stylesheet: trailing semicolon optional",
          "[css][stylesheet]") {
    const auto s = Stylesheet::parse("h1 { color: red }");
    REQUIRE(s.rules.size() == 1);
    REQUIRE(s.rules[0].declarations.size() == 1);
    CHECK(s.rules[0].declarations[0].property == "color");
    CHECK(s.rules[0].declarations[0].value == "red");
}

TEST_CASE("stylesheet: multiple declarations", "[css][stylesheet]") {
    const auto s =
        Stylesheet::parse("h1 { color: red; font-size: 24px; }");
    REQUIRE(s.rules.size() == 1);
    REQUIRE(s.rules[0].declarations.size() == 2);
    CHECK(s.rules[0].declarations[0].property == "color");
    CHECK(s.rules[0].declarations[0].value == "red");
    CHECK(s.rules[0].declarations[1].property == "font-size");
    CHECK(s.rules[0].declarations[1].value == "24px");
}

TEST_CASE("stylesheet: malformed declaration skipped, others kept",
          "[css][stylesheet]") {
    const auto s = Stylesheet::parse(
        "h1 { color: red; broken ; font-size: 20px; }");
    REQUIRE(s.rules.size() == 1);
    REQUIRE(s.rules[0].declarations.size() == 2);
    CHECK(s.rules[0].declarations[0].property == "color");
    CHECK(s.rules[0].declarations[1].property == "font-size");
}

TEST_CASE("stylesheet: missing colon yields no declaration",
          "[css][stylesheet]") {
    const auto s = Stylesheet::parse("h1 { color }");
    REQUIRE(s.rules.size() == 1);
    CHECK(s.rules[0].declarations.empty());
}

TEST_CASE("stylesheet: empty property skipped", "[css][stylesheet]") {
    const auto s = Stylesheet::parse("h1 { : red }");
    REQUIRE(s.rules.size() == 1);
    CHECK(s.rules[0].declarations.empty());
}

TEST_CASE("stylesheet: empty value skipped", "[css][stylesheet]") {
    const auto s = Stylesheet::parse("h1 { color: }");
    REQUIRE(s.rules.size() == 1);
    CHECK(s.rules[0].declarations.empty());
}

TEST_CASE("stylesheet: whitespace around colon and semicolon trimmed",
          "[css][stylesheet]") {
    const auto s = Stylesheet::parse("h1 {   color   :   red   ;   }");
    REQUIRE(s.rules.size() == 1);
    REQUIRE(s.rules[0].declarations.size() == 1);
    CHECK(s.rules[0].declarations[0].property == "color");
    CHECK(s.rules[0].declarations[0].value == "red");
}

TEST_CASE("stylesheet: !important flag set", "[css][stylesheet]") {
    const auto s = Stylesheet::parse("h1 { color: red !important; }");
    REQUIRE(s.rules.size() == 1);
    REQUIRE(s.rules[0].declarations.size() == 1);
    CHECK(s.rules[0].declarations[0].value == "red");
    CHECK(s.rules[0].declarations[0].important);
}

TEST_CASE("stylesheet: !IMPORTANT case insensitive",
          "[css][stylesheet]") {
    const auto s = Stylesheet::parse("h1 { color: red !IMPORTANT }");
    REQUIRE(s.rules.size() == 1);
    REQUIRE(s.rules[0].declarations.size() == 1);
    CHECK(s.rules[0].declarations[0].value == "red");
    CHECK(s.rules[0].declarations[0].important);
}

TEST_CASE("stylesheet: !important with no space before bang",
          "[css][stylesheet]") {
    const auto s = Stylesheet::parse("h1 { color: red!important }");
    REQUIRE(s.rules.size() == 1);
    REQUIRE(s.rules[0].declarations.size() == 1);
    CHECK(s.rules[0].declarations[0].value == "red");
    CHECK(s.rules[0].declarations[0].important);
}

TEST_CASE("stylesheet: ! important with space after bang",
          "[css][stylesheet]") {
    const auto s = Stylesheet::parse("h1 { color: red ! important }");
    REQUIRE(s.rules.size() == 1);
    REQUIRE(s.rules[0].declarations.size() == 1);
    CHECK(s.rules[0].declarations[0].value == "red");
    CHECK(s.rules[0].declarations[0].important);
}

TEST_CASE("stylesheet: no !important leaves flag false",
          "[css][stylesheet]") {
    const auto s = Stylesheet::parse("h1 { color: red }");
    REQUIRE(s.rules.size() == 1);
    REQUIRE(s.rules[0].declarations.size() == 1);
    CHECK_FALSE(s.rules[0].declarations[0].important);
}

TEST_CASE("stylesheet: real-world example", "[css][stylesheet]") {
    const auto s = Stylesheet::parse(
        "h1 { color: navy; font-size: 24px; }\n"
        "p { margin: 0 0 1em 0; }\n"
        "a.link { color: blue !important; }\n");
    REQUIRE(s.rules.size() == 3);
    CHECK(s.rules[0].declarations.size() == 2);
    CHECK(s.rules[1].declarations.size() == 1);
    CHECK(s.rules[2].declarations.size() == 1);
    CHECK(s.rules[2].declarations[0].important);
}
