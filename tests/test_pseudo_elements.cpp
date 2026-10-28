// =============================================================
// Tests for ADR-041: pseudo-elements (::before / ::after).
//
// Parse tests verify selector parsing produces the right
// SimpleKind markers. Cascade tests verify pseudo_styles map
// gets populated correctly and element_styles is unaffected
// (behavior-neutral regression).
// =============================================================

#include "renderer/css/cascade.hpp"
#include "renderer/css/selectors.hpp"
#include "renderer/css/stylesheet.hpp"
#include "renderer/html/convert.hpp"
#include "renderer/dom/node.hpp"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

namespace {

using namespace honeybee::renderer;

const dom::Element* find_first_tag(const dom::Node& node,
                                    const std::string& tag) {
    if (node.type() == dom::NodeType::Element) {
        const auto& el = static_cast<const dom::Element&>(node);
        if (el.tag() == tag) return &el;
    }
    for (const auto& c : node.children()) {
        if (auto r = find_first_tag(*c, tag)) return r;
    }
    return nullptr;
}

std::vector<const dom::Element*> find_all_tags(
        const dom::Node& node, const std::string& tag) {
    std::vector<const dom::Element*> out;
    if (node.type() == dom::NodeType::Element) {
        const auto& el = static_cast<const dom::Element&>(node);
        if (el.tag() == tag) out.push_back(&el);
    }
    for (const auto& c : node.children()) {
        auto sub = find_all_tags(*c, tag);
        out.insert(out.end(), sub.begin(), sub.end());
    }
    return out;
}

} // namespace

// =============================================================
// Parse tests (7)
// =============================================================

TEST_CASE("Selector::parse: p::before -> PseudoElementBefore",
          "[css][selectors][adr-041]") {
    auto sel = css::Selector::parse("p::before");
    REQUIRE(sel.has_value());
    REQUIRE(sel->parts.size() == 1);
    bool has_type = false, has_pseudo = false;
    for (const auto& s : sel->parts[0].compound.simples) {
        if (s.kind == css::SimpleKind::Type && s.name == "p") has_type = true;
        if (s.kind == css::SimpleKind::PseudoElementBefore) has_pseudo = true;
    }
    REQUIRE(has_type);
    REQUIRE(has_pseudo);
}

TEST_CASE("Selector::parse: p::after -> PseudoElementAfter",
          "[css][selectors][adr-041]") {
    auto sel = css::Selector::parse("p::after");
    REQUIRE(sel.has_value());
    bool has_type = false, has_pseudo = false;
    for (const auto& s : sel->parts[0].compound.simples) {
        if (s.kind == css::SimpleKind::Type && s.name == "p") has_type = true;
        if (s.kind == css::SimpleKind::PseudoElementAfter) has_pseudo = true;
    }
    REQUIRE(has_type);
    REQUIRE(has_pseudo);
}

TEST_CASE("Selector::parse: p:before (legacy single-colon)",
          "[css][selectors][adr-041]") {
    auto sel = css::Selector::parse("p:before");
    REQUIRE(sel.has_value());
    bool has_pseudo = false;
    for (const auto& s : sel->parts[0].compound.simples) {
        if (s.kind == css::SimpleKind::PseudoElementBefore) has_pseudo = true;
    }
    REQUIRE(has_pseudo);
}

TEST_CASE("Selector::parse: p:after (legacy single-colon)",
          "[css][selectors][adr-041]") {
    auto sel = css::Selector::parse("p:after");
    REQUIRE(sel.has_value());
    bool has_pseudo = false;
    for (const auto& s : sel->parts[0].compound.simples) {
        if (s.kind == css::SimpleKind::PseudoElementAfter) has_pseudo = true;
    }
    REQUIRE(has_pseudo);
}

TEST_CASE("Selector::parse: p::first-line rejected (unsupported pseudo-element)",
          "[css][selectors][adr-041]") {
    REQUIRE_FALSE(css::Selector::parse("p::first-line").has_value());
}

TEST_CASE("Selector::parse: p::marker rejected (unsupported pseudo-element)",
          "[css][selectors][adr-041]") {
    REQUIRE_FALSE(css::Selector::parse("p::marker").has_value());
}

TEST_CASE("Selector::parse: .foo::after -> compound class + pseudo-element",
          "[css][selectors][adr-041]") {
    auto sel = css::Selector::parse(".foo::after");
    REQUIRE(sel.has_value());
    bool has_class = false, has_pseudo = false;
    for (const auto& s : sel->parts[0].compound.simples) {
        if (s.kind == css::SimpleKind::Class && s.name == "foo") has_class = true;
        if (s.kind == css::SimpleKind::PseudoElementAfter) has_pseudo = true;
    }
    REQUIRE(has_class);
    REQUIRE(has_pseudo);
}

// =============================================================
// Cascade tests (7)
// =============================================================

TEST_CASE("cascade: p::after populates pseudo_styles[p].after",
          "[css][cascade][adr-041]") {
    auto doc = html::parse("<html><body><p>x</p></body></html>");
    REQUIRE(doc != nullptr);
    auto sheet = css::Stylesheet::parse("p::after { content: 'A' }");
    auto result = css::cascade_with_pseudo(css::Stylesheet{}, sheet, *doc);

    const auto* p = find_first_tag(*doc, "p");
    REQUIRE(p != nullptr);
    REQUIRE(result.pseudo_styles.count(p) == 1);
    REQUIRE(result.pseudo_styles.at(p).after.has("content"));
}

TEST_CASE("cascade: p::before populates pseudo_styles[p].before",
          "[css][cascade][adr-041]") {
    auto doc = html::parse("<html><body><p>x</p></body></html>");
    REQUIRE(doc != nullptr);
    auto sheet = css::Stylesheet::parse("p::before { content: 'B' }");
    auto result = css::cascade_with_pseudo(css::Stylesheet{}, sheet, *doc);

    const auto* p = find_first_tag(*doc, "p");
    REQUIRE(p != nullptr);
    REQUIRE(result.pseudo_styles.count(p) == 1);
    REQUIRE(result.pseudo_styles.at(p).before.has("content"));
}

TEST_CASE("cascade: both p::before and p::after populate",
          "[css][cascade][adr-041]") {
    auto doc = html::parse("<html><body><p>x</p></body></html>");
    REQUIRE(doc != nullptr);
    auto sheet = css::Stylesheet::parse(
        "p::before { color: red } p::after { color: blue }");
    auto result = css::cascade_with_pseudo(css::Stylesheet{}, sheet, *doc);

    const auto* p = find_first_tag(*doc, "p");
    REQUIRE(p != nullptr);
    REQUIRE(result.pseudo_styles.count(p) == 1);
    REQUIRE(result.pseudo_styles.at(p).before.has("color"));
    REQUIRE(result.pseudo_styles.at(p).after.has("color"));
}

TEST_CASE("cascade: element rules unaffected by pseudo synthesis",
          "[css][cascade][adr-041]") {
    auto doc = html::parse("<html><body><p>x</p></body></html>");
    REQUIRE(doc != nullptr);
    auto sheet = css::Stylesheet::parse("p { color: red }");
    auto result = css::cascade_with_pseudo(css::Stylesheet{}, sheet, *doc);

    const auto* p = find_first_tag(*doc, "p");
    REQUIRE(p != nullptr);
    REQUIRE(result.element_styles.count(p) == 1);
    REQUIRE(result.element_styles.at(p).has("color"));
    // No pseudo rules -> pseudo_styles empty.
    REQUIRE(result.pseudo_styles.empty());
}

TEST_CASE("cascade: no pseudo rules -> pseudo_styles empty map",
          "[css][cascade][adr-041]") {
    auto doc = html::parse("<html><body><p>x</p></body></html>");
    REQUIRE(doc != nullptr);
    auto result = css::cascade_with_pseudo(
        css::Stylesheet{}, css::Stylesheet{}, *doc);
    REQUIRE(result.pseudo_styles.empty());
}

TEST_CASE("cascade: cascade() wrapper returns same element_styles",
          "[css][cascade][adr-041]") {
    auto doc = html::parse("<html><body><p>x</p><a href=\"y\">l</a></body></html>");
    REQUIRE(doc != nullptr);
    auto sheet = css::Stylesheet::parse(
        "p { color: red } a { color: blue }");
    auto wrapped = css::cascade(css::Stylesheet{}, sheet, *doc);
    auto pseudo  = css::cascade_with_pseudo(css::Stylesheet{}, sheet, *doc);

    REQUIRE(wrapped.size() == pseudo.element_styles.size());
    for (const auto& [ptr, style] : wrapped) {
        auto it = pseudo.element_styles.find(ptr);
        REQUIRE(it != pseudo.element_styles.end());
        REQUIRE(it->second.declarations.size() == style.declarations.size());
    }
}

TEST_CASE("cascade: two <p> elements get independent pseudo entries",
          "[css][cascade][adr-041]") {
    auto doc = html::parse("<html><body><p>a</p><p>b</p></body></html>");
    REQUIRE(doc != nullptr);
    auto sheet = css::Stylesheet::parse("p::after { color: red }");
    auto result = css::cascade_with_pseudo(css::Stylesheet{}, sheet, *doc);

    auto ps = find_all_tags(*doc, "p");
    REQUIRE(ps.size() == 2);
    REQUIRE(ps[0] != ps[1]);  // distinct addresses, independent entries
    REQUIRE(result.pseudo_styles.count(ps[0]) == 1);
    REQUIRE(result.pseudo_styles.count(ps[1]) == 1);
}
