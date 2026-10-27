#include "renderer/css/selectors.hpp"
#include "renderer/dom/node.hpp"

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

// =============================================================
// Matcher tests (Commit 2 of ADR-008).
// Build a real DOM tree and exercise matches() on it.
// =============================================================

namespace {

namespace dom = honeybee::renderer::dom;

// Test tree:
//   Document
//     html
//       head > title("Test Page")
//       body
//         div#main.container
//           h1.title ("Heading")
//           p.first  ("First paragraph")
//           p.second > span.highlight ("highlighted")
//         footer
std::unique_ptr<dom::Document> build_test_tree() {
    auto doc  = std::make_unique<dom::Document>();
    auto html = std::make_unique<dom::Element>("html");

    auto head = std::make_unique<dom::Element>("head");
    auto title = std::make_unique<dom::Element>("title");
    title->append_child(std::make_unique<dom::TextNode>("Test Page"));
    head->append_child(std::move(title));
    html->append_child(std::move(head));

    auto body = std::make_unique<dom::Element>("body");

    auto div = std::make_unique<dom::Element>("div");
    div->set_attribute("id", "main");
    div->set_attribute("class", "container");

    auto h1 = std::make_unique<dom::Element>("h1");
    h1->set_attribute("class", "title");
    h1->append_child(std::make_unique<dom::TextNode>("Heading"));
    div->append_child(std::move(h1));

    auto p1 = std::make_unique<dom::Element>("p");
    p1->set_attribute("class", "first");
    p1->append_child(std::make_unique<dom::TextNode>("First paragraph"));
    div->append_child(std::move(p1));

    auto p2 = std::make_unique<dom::Element>("p");
    p2->set_attribute("class", "second");
    auto span = std::make_unique<dom::Element>("span");
    span->set_attribute("class", "highlight");
    span->append_child(std::make_unique<dom::TextNode>("highlighted"));
    p2->append_child(std::move(span));
    div->append_child(std::move(p2));

    body->append_child(std::move(div));

    auto footer = std::make_unique<dom::Element>("footer");
    body->append_child(std::move(footer));

    html->append_child(std::move(body));
    doc->append_child(std::move(html));
    return doc;
}

const dom::Element* nth_element_child(const dom::Node& parent, size_t n) {
    size_t count = 0;
    for (const auto& child : parent.children()) {
        if (child->type() == dom::NodeType::Element) {
            if (count == n)
                return static_cast<const dom::Element*>(child.get());
            ++count;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("matcher: type selector positive and negative",
          "[css][matcher]") {
    const auto doc = build_test_tree();
    const auto* p1 = nth_element_child(*doc->find_element("div"), 1);
    REQUIRE(p1);
    CHECK(parse_ok("p").matches(*p1));
    CHECK_FALSE(parse_ok("div").matches(*p1));
    CHECK(parse_ok("*").matches(*p1));
}

TEST_CASE("matcher: class selector", "[css][matcher]") {
    const auto doc = build_test_tree();
    const auto* div = doc->find_element("div");
    REQUIRE(div);
    CHECK(parse_ok(".container").matches(*div));
    CHECK_FALSE(parse_ok(".missing").matches(*div));
    CHECK(parse_ok(".container.missing").matches(*div) == false);
}

TEST_CASE("matcher: id selector", "[css][matcher]") {
    const auto doc = build_test_tree();
    const auto* div = doc->find_element("div");
    REQUIRE(div);
    CHECK(parse_ok("#main").matches(*div));
    CHECK_FALSE(parse_ok("#other").matches(*div));
}

TEST_CASE("matcher: attribute operators", "[css][matcher]") {
    const auto doc = build_test_tree();
    const auto* div = doc->find_element("div");
    REQUIRE(div);
    CHECK(parse_ok("[id]").matches(*div));
    CHECK_FALSE(parse_ok("[missing]").matches(*div));
    CHECK(parse_ok("[id=\"main\"]").matches(*div));
    CHECK_FALSE(parse_ok("[id=\"other\"]").matches(*div));
    CHECK(parse_ok("[id^=\"ma\"]").matches(*div));
    CHECK(parse_ok("[id$=\"in\"]").matches(*div));
    CHECK(parse_ok("[id*=\"ai\"]").matches(*div));
    CHECK(parse_ok("[class~=\"container\"]").matches(*div));
}

TEST_CASE("matcher: hyphen attribute operator", "[css][matcher]") {
    auto doc = std::make_unique<dom::Document>();
    auto el = std::make_unique<dom::Element>("html");
    el->set_attribute("lang", "en-GB");
    doc->append_child(std::move(el));
    const auto* html = doc->find_element("html");
    REQUIRE(html);
    CHECK(parse_ok("[lang|=\"en\"]").matches(*html));
    CHECK(parse_ok("[lang|=\"en-GB\"]").matches(*html));
    CHECK_FALSE(parse_ok("[lang|=\"e\"]").matches(*html));
}

TEST_CASE("matcher: descendant combinator", "[css][matcher]") {
    const auto doc = build_test_tree();
    const auto* span = doc->find_element("span");
    REQUIRE(span);
    CHECK(parse_ok("div span").matches(*span));
    CHECK(parse_ok("body span").matches(*span));
    CHECK(parse_ok("html span").matches(*span));
    CHECK_FALSE(parse_ok("footer span").matches(*span));
    CHECK_FALSE(parse_ok("head span").matches(*span));
}

TEST_CASE("matcher: child combinator", "[css][matcher]") {
    const auto doc = build_test_tree();
    const auto* div = doc->find_element("div");
    REQUIRE(div);
    CHECK(parse_ok("body > div").matches(*div));
    CHECK_FALSE(parse_ok("html > div").matches(*div));
    CHECK_FALSE(parse_ok("body div").matches(*div) == false);  // descendant also matches
}

TEST_CASE("matcher: adjacent sibling combinator", "[css][matcher]") {
    const auto doc = build_test_tree();
    const auto* div = doc->find_element("div");
    REQUIRE(div);
    const auto* p1 = nth_element_child(*div, 1);
    const auto* p2 = nth_element_child(*div, 2);
    REQUIRE(p1);
    REQUIRE(p2);
    CHECK(parse_ok("h1 + p").matches(*p1));
    CHECK_FALSE(parse_ok("h1 + p").matches(*p2));
    CHECK(parse_ok("p + p").matches(*p2));
    CHECK_FALSE(parse_ok("p + p").matches(*p1));
}

TEST_CASE("matcher: general sibling combinator", "[css][matcher]") {
    const auto doc = build_test_tree();
    const auto* div = doc->find_element("div");
    REQUIRE(div);
    const auto* p1 = nth_element_child(*div, 1);
    const auto* p2 = nth_element_child(*div, 2);
    REQUIRE(p1);
    REQUIRE(p2);
    CHECK(parse_ok("h1 ~ p").matches(*p1));
    CHECK(parse_ok("h1 ~ p").matches(*p2));
    CHECK_FALSE(parse_ok("p ~ h1").matches(*p1));
    CHECK_FALSE(parse_ok("p ~ h1").matches(*p2));
}

TEST_CASE("matcher: compound selector", "[css][matcher]") {
    const auto doc = build_test_tree();
    const auto* div = doc->find_element("div");
    REQUIRE(div);
    CHECK(parse_ok("div#main.container").matches(*div));
    CHECK_FALSE(parse_ok("div#missing").matches(*div));
    CHECK_FALSE(parse_ok("span#main").matches(*div));
}

TEST_CASE("matcher: full chain with multiple combinators",
          "[css][matcher]") {
    const auto doc = build_test_tree();
    const auto* span = doc->find_element("span");
    REQUIRE(span);
    CHECK(parse_ok("div > p > span").matches(*span));
    CHECK(parse_ok("body div span").matches(*span));
    CHECK_FALSE(parse_ok("div > span").matches(*span));  // span's parent is p, not div
}

TEST_CASE("matcher: SelectorList returns highest matching specificity",
          "[css][matcher]") {
    const auto doc = build_test_tree();
    const auto* div = doc->find_element("div");
    REQUIRE(div);
    auto list = SelectorList::parse("div, .container, #main");
    REQUIRE(list);
    const auto spec = list->matches(*div);
    REQUIRE(spec);
    CHECK(*spec == Specificity{1, 0, 0});  // #main wins
}

TEST_CASE("matcher: SelectorList returns nullopt when nothing matches",
          "[css][matcher]") {
    const auto doc = build_test_tree();
    const auto* div = doc->find_element("div");
    REQUIRE(div);
    auto list = SelectorList::parse(".nothing, [data-missing], #nope");
    REQUIRE(list);
    CHECK_FALSE(list->matches(*div).has_value());
}

// =============================================================
// Pseudo-class parsing and specificity (ADR-018).
// =============================================================

TEST_CASE("parse: :link pseudo-class", "[css][selectors]") {
    auto s = Selector::parse("a:link");
    REQUIRE(s);
    REQUIRE(s->parts.size() == 1);
    REQUIRE(s->parts[0].compound.simples.size() == 2);
    CHECK(s->parts[0].compound.simples[0].kind == SimpleKind::Type);
    CHECK(s->parts[0].compound.simples[1].kind == SimpleKind::PseudoLink);
}

TEST_CASE("parse: :visited pseudo-class", "[css][selectors]") {
    auto s = Selector::parse("a:visited");
    REQUIRE(s);
    REQUIRE(s->parts[0].compound.simples.size() == 2);
    CHECK(s->parts[0].compound.simples[1].kind == SimpleKind::PseudoVisited);
}

TEST_CASE("parse: :hover pseudo-class", "[css][selectors]") {
    auto s = Selector::parse("a:hover");
    REQUIRE(s);
    REQUIRE(s->parts[0].compound.simples.size() == 2);
    CHECK(s->parts[0].compound.simples[1].kind == SimpleKind::PseudoHover);
}

TEST_CASE("parse: unknown pseudo-class is a parse error",
          "[css][selectors]") {
    // ADR-038 note: :first-child used to be in this list;
    // it's now supported (structural pseudo-class). :not() also
    // moved out (logical pseudo-class, ADR-038 Commit 2).
    // :focus and :active are still unsupported (form-state
    // category, deferred to their own ADR).
    CHECK_FALSE(Selector::parse("a:focus").has_value());
    CHECK_FALSE(Selector::parse("a:active").has_value());
    CHECK_FALSE(Selector::parse("a:checked").has_value());
}

TEST_CASE("parse: :first-child now supported (ADR-038)",
          "[css][selectors][adr-038]") {
    // ADR-038: structural pseudo-classes parse and dispatch to
    // their SimpleKind.
    auto s = Selector::parse("a:first-child");
    REQUIRE(s);
    REQUIRE(s->parts[0].compound.simples.size() == 2);
    CHECK(s->parts[0].compound.simples[1].kind
          == SimpleKind::PseudoFirstChild);

    auto s2 = Selector::parse("li:nth-child(2n+1)");
    REQUIRE(s2);
    REQUIRE(s2->parts[0].compound.simples.size() == 2);
    const auto& ps = s2->parts[0].compound.simples[1];
    CHECK(ps.kind == SimpleKind::PseudoNthChild);
    CHECK(ps.nth_a == 2);
    CHECK(ps.nth_b == 1);
}

TEST_CASE("specificity: :link adds class weight", "[css][selectors][specificity]") {
    auto s = Selector::parse("a:link");
    REQUIRE(s);
    CHECK(s->specificity() == Specificity{0, 1, 1});
}

TEST_CASE("specificity: :hover combines with class",
          "[css][selectors][specificity]") {
    auto s = Selector::parse(".x:hover");
    REQUIRE(s);
    CHECK(s->specificity() == Specificity{0, 2, 0});
}

TEST_CASE("specificity: :visited combines with id",
          "[css][selectors][specificity]") {
    auto s = Selector::parse("#id:visited");
    REQUIRE(s);
    CHECK(s->specificity() == Specificity{1, 1, 0});
}

// =============================================================
// ADR-026: :root selector.
// =============================================================

TEST_CASE("parse: :root pseudo-class", "[css][selectors][adr-026]") {
    auto s = Selector::parse(":root");
    REQUIRE(s);
    REQUIRE(s->parts.size() == 1);
    REQUIRE(s->parts[0].compound.simples.size() == 1);
    CHECK(s->parts[0].compound.simples[0].kind == SimpleKind::PseudoRoot);
}

TEST_CASE("specificity: :root counts as class weight",
          "[css][selectors][specificity][adr-026]") {
    auto s = Selector::parse(":root");
    REQUIRE(s);
    CHECK(s->specificity() == Specificity{0, 1, 0});

    auto compound = Selector::parse("html:root");
    REQUIRE(compound);
    CHECK(compound->specificity() == Specificity{0, 1, 1});
}
