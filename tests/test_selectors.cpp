#include "renderer/css/selectors.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string_view>

using namespace honeybee::renderer::css;

namespace {
const Selector parse_ok(std::string_view text) {
    auto s = Selector::parse(text);
    REQUIRE(s);
    return *s;
}
} // namespace

TEST_CASE("parse: type selector", "[css][selectors]") {
    const auto s = parse_ok("div");
    REQUIRE(s.parts.size() == 1);
    CHECK(s.parts[0].combinator == Combinator::None);
    REQUIRE(s.parts[0].compound.simples.size() == 1);
    CHECK(s.parts[0].compound.simples[0].kind == SimpleKind::Type);
    CHECK(s.parts[0].compound.simples[0].name == "div");
}

TEST_CASE("parse: universal selector", "[css][selectors]") {
    const auto s = parse_ok("*");
    REQUIRE(s.parts.size() == 1);
    REQUIRE(s.parts[0].compound.simples.size() == 1);
    CHECK(s.parts[0].compound.simples[0].kind == SimpleKind::Universal);
}

TEST_CASE("parse: class selector", "[css][selectors]") {
    const auto s = parse_ok(".foo");
    CHECK(s.parts[0].compound.simples[0].kind == SimpleKind::Class);
    CHECK(s.parts[0].compound.simples[0].name == "foo");
}

TEST_CASE("parse: id selector", "[css][selectors]") {
    const auto s = parse_ok("#main");
    CHECK(s.parts[0].compound.simples[0].kind == SimpleKind::Id);
    CHECK(s.parts[0].compound.simples[0].name == "main");
}

TEST_CASE("parse: attribute presence", "[css][selectors]") {
    const auto s = parse_ok("[href]");
    const auto& a = s.parts[0].compound.simples[0];
    CHECK(a.kind == SimpleKind::Attribute);
    CHECK(a.name == "href");
    CHECK(a.attr_match == AttributeMatch::Presence);
    CHECK(a.attr_value.empty());
}

TEST_CASE("parse: attribute match operators", "[css][selectors]") {
    struct Case { std::string_view text; AttributeMatch m; std::string_view v; };
    const Case cases[] = {
        {R"([href="x"])",   AttributeMatch::Exact,    "x"},
        {R"([href^="x"])",  AttributeMatch::Prefix,   "x"},
        {R"([href$="x"])",  AttributeMatch::Suffix,   "x"},
        {R"([href*="x"])",  AttributeMatch::Contains, "x"},
        {R"([class~="x"])", AttributeMatch::Word,     "x"},
        {R"([lang|="en"])", AttributeMatch::Hyphen,   "en"},
        {"[lang|=en]",      AttributeMatch::Hyphen,   "en"},
    };
    for (const auto& c : cases) {
        INFO("input: " << c.text);
        const auto s = parse_ok(c.text);
        const auto& a = s.parts[0].compound.simples[0];
        CHECK(a.kind == SimpleKind::Attribute);
        CHECK(a.attr_match == c.m);
        CHECK(a.attr_value == c.v);
    }
}

TEST_CASE("parse: compound selector", "[css][selectors]") {
    const auto s = parse_ok("div.foo#bar[data-x=\"1\"]");
    REQUIRE(s.parts.size() == 1);
    const auto& simples = s.parts[0].compound.simples;
    REQUIRE(simples.size() == 4);
    CHECK(simples[0].kind == SimpleKind::Type);
    CHECK(simples[0].name == "div");
    CHECK(simples[1].kind == SimpleKind::Class);
    CHECK(simples[1].name == "foo");
    CHECK(simples[2].kind == SimpleKind::Id);
    CHECK(simples[2].name == "bar");
    CHECK(simples[3].kind == SimpleKind::Attribute);
    CHECK(simples[3].name == "data-x");
    CHECK(simples[3].attr_match == AttributeMatch::Exact);
    CHECK(simples[3].attr_value == "1");
}

TEST_CASE("parse: descendant combinator", "[css][selectors]") {
    const auto s = parse_ok("div p");
    REQUIRE(s.parts.size() == 2);
    CHECK(s.parts[0].combinator == Combinator::None);
    CHECK(s.parts[1].combinator == Combinator::Descendant);
}

TEST_CASE("parse: child combinator", "[css][selectors]") {
    for (const auto text : {"div>p", "div > p", "div >p", "div> p"}) {
        INFO("input: " << text);
        const auto s = parse_ok(text);
        REQUIRE(s.parts.size() == 2);
        CHECK(s.parts[1].combinator == Combinator::Child);
    }
}

TEST_CASE("parse: adjacent sibling combinator", "[css][selectors]") {
    const auto s = parse_ok("div + p");
    REQUIRE(s.parts.size() == 2);
    CHECK(s.parts[1].combinator == Combinator::Adjacent);
}

TEST_CASE("parse: general sibling combinator", "[css][selectors]") {
    const auto s = parse_ok("div ~ p");
    REQUIRE(s.parts.size() == 2);
    CHECK(s.parts[1].combinator == Combinator::Sibling);
}

TEST_CASE("parse: selector list", "[css][selectors]") {
    auto l = SelectorList::parse("div, .foo, #bar");
    REQUIRE(l);
    REQUIRE(l->selectors.size() == 3);
    CHECK(l->selectors[0].parts[0].compound.simples[0].kind == SimpleKind::Type);
    CHECK(l->selectors[1].parts[0].compound.simples[0].kind == SimpleKind::Class);
    CHECK(l->selectors[2].parts[0].compound.simples[0].kind == SimpleKind::Id);
}

TEST_CASE("Selector::parse rejects comma", "[css][selectors]") {
    CHECK_FALSE(Selector::parse("div, p").has_value());
}

TEST_CASE("specificity table", "[css][selectors][specificity]") {
    struct Case { std::string_view text; Specificity want; };
    const Case cases[] = {
        {"*",              {0, 0, 0}},
        {"div",            {0, 0, 1}},
        {".foo",           {0, 1, 0}},
        {"#main",          {1, 0, 0}},
        {"div.foo",        {0, 1, 1}},
        {"div > p.foo",    {0, 1, 2}},
        {"div#a.b.c > p",  {1, 2, 2}},
        {"#x.y[z]",        {1, 2, 0}},
        {"[a][b]",         {0, 2, 0}},
    };
    for (const auto& c : cases) {
        INFO("input: " << c.text);
        const auto s = parse_ok(c.text);
        CHECK(s.specificity() == c.want);
    }
}

TEST_CASE("specificity ordering", "[css][selectors][specificity]") {
    CHECK(Specificity{0, 0, 1} < Specificity{0, 1, 0});
    CHECK(Specificity{0, 1, 0} < Specificity{1, 0, 0});
    CHECK(Specificity{1, 0, 0} < Specificity{1, 0, 1});
    CHECK(Specificity{1, 2, 3} < Specificity{1, 3, 0});
}

TEST_CASE("malformed selectors return nullopt", "[css][selectors]") {
    const std::string_view bad[] = {
        "", " ", ",", "div,", "div >", "> div", ".", "#", "[", "]",
        "[]", "[href=", "[href=]", "[href=\"x]", "[href^]", "* > >",
        "div,,p",
    };
    for (const auto t : bad) {
        INFO("input: '" << t << "'");
        CHECK_FALSE(Selector::parse(t).has_value());
    }
}

TEST_CASE("malformed selector list returns nullopt", "[css][selectors]") {
    const std::string_view bad[] = {
        "", ",", "div,", ",div", "div,,p", "div > p >",
    };
    for (const auto t : bad) {
        INFO("input: '" << t << "'");
        CHECK_FALSE(SelectorList::parse(t).has_value());
    }
}
