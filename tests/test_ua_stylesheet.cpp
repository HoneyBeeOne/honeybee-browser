// =============================================================
// Tests for the user-agent stylesheet (ADR-014).
// =============================================================

#include "renderer/css/ua_stylesheet.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string_view>

namespace css = honeybee::renderer::css;

namespace {

const css::Rule* find_rule_by_tag(const css::Stylesheet& s,
                                   std::string_view tag) {
    for (const auto& r : s.rules) {
        for (const auto& sel : r.selectors.selectors) {
            for (const auto& part : sel.parts) {
                for (const auto& simp : part.compound.simples) {
                    if (simp.kind == css::SimpleKind::Type &&
                        simp.name == std::string(tag)) {
                        return &r;
                    }
                }
            }
        }
    }
    return nullptr;
}

const css::Declaration* find_decl(const css::Rule& r,
                                   std::string_view prop) {
    for (const auto& d : r.declarations) {
        if (d.property == prop) return &d;
    }
    return nullptr;
}

} // namespace

TEST_CASE("ua_stylesheet: parses to 13 rules", "[ua_stylesheet]") {
    CHECK(css::ua_stylesheet().rules.size() == 14);
}

TEST_CASE("ua_stylesheet: returns cached reference",
          "[ua_stylesheet]") {
    CHECK(&css::ua_stylesheet() == &css::ua_stylesheet());
}

TEST_CASE("ua_stylesheet: body has margin-top 8px", "[ua_stylesheet]") {
    // ADR-012 expands `margin: 8px` to four longhands at parse
    // time. The UA sheet therefore contains `margin-top`, not
    // `margin`, by the time ua_stylesheet() returns.
    const auto& s = css::ua_stylesheet();
    const auto* r = find_rule_by_tag(s, "body");
    REQUIRE(r != nullptr);
    const auto* d = find_decl(*r, "margin-top");
    REQUIRE(d != nullptr);
    CHECK(d->value == "8px");
}

TEST_CASE("ua_stylesheet: h1 has font-size 2em", "[ua_stylesheet]") {
    const auto& s = css::ua_stylesheet();
    const auto* r = find_rule_by_tag(s, "h1");
    REQUIRE(r != nullptr);
    const auto* d = find_decl(*r, "font-size");
    REQUIRE(d != nullptr);
    CHECK(d->value == "2em");
}

TEST_CASE("ua_stylesheet: h2 has font-size 1.5em", "[ua_stylesheet]") {
    const auto& s = css::ua_stylesheet();
    const auto* r = find_rule_by_tag(s, "h2");
    REQUIRE(r != nullptr);
    const auto* d = find_decl(*r, "font-size");
    REQUIRE(d != nullptr);
    CHECK(d->value == "1.5em");
}

TEST_CASE("ua_stylesheet: p has margin-top 1em", "[ua_stylesheet]") {
    // `margin: 1em 0` expands to top=1em, right=0, bottom=1em,
    // left=0. Check the top longhand as the representative.
    const auto& s = css::ua_stylesheet();
    const auto* r = find_rule_by_tag(s, "p");
    REQUIRE(r != nullptr);
    const auto* d = find_decl(*r, "margin-top");
    REQUIRE(d != nullptr);
    CHECK(d->value == "1em");
}

TEST_CASE("ua_stylesheet: a has blue color", "[ua_stylesheet]") {
    const auto& s = css::ua_stylesheet();
    const auto* r = find_rule_by_tag(s, "a");
    REQUIRE(r != nullptr);
    const auto* d = find_decl(*r, "color");
    REQUIRE(d != nullptr);
    CHECK(d->value == "#0000EE");
}
