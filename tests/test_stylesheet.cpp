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
