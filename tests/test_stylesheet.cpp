#include "renderer/css/render_state.hpp"
#include "renderer/html/convert.hpp"
#include "renderer/dom/node.hpp"
#include "renderer/css/stylesheet.hpp"

#include <catch2/catch_test_macros.hpp>

using honeybee::renderer::css::Stylesheet;
using honeybee::renderer::css::Selector;

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

TEST_CASE("stylesheet: block at-rule extracted with MediaQuery",
          "[css][stylesheet]") {
    // ADR-027: @media screen is now supported (always-match
    // query); div is extracted as a Rule with media populated,
    // h1 stays top-level (media nullopt).
    const auto s = Stylesheet::parse("@media screen { div {} } h1 {}");
    REQUIRE(s.rules.size() == 2);
    // Rule 0: div inside @media screen
    REQUIRE(s.rules[0].selectors.selectors.size() == 1);
    CHECK(s.rules[0].media.has_value());
    CHECK_FALSE(s.rules[0].media->min_width_px.has_value());
    CHECK_FALSE(s.rules[0].media->max_width_px.has_value());
    // Rule 1: h1 top-level
    REQUIRE(s.rules[1].selectors.selectors.size() == 1);
    CHECK_FALSE(s.rules[1].media.has_value());
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
    CHECK(s.rules[1].declarations.size() == 4);
    CHECK(s.rules[2].declarations.size() == 1);
    CHECK(s.rules[2].declarations[0].important);
}

// =============================================================
// Shorthand expansion (ADR-012).
// =============================================================

TEST_CASE("stylesheet: margin single value expands to 4 longhands",
          "[css][stylesheet][shorthand]") {
    const auto s = Stylesheet::parse("p { margin: 10px }");
    REQUIRE(s.rules.size() == 1);
    REQUIRE(s.rules[0].declarations.size() == 4);
    CHECK(s.rules[0].declarations[0].property == "margin-top");
    CHECK(s.rules[0].declarations[1].property == "margin-right");
    CHECK(s.rules[0].declarations[2].property == "margin-bottom");
    CHECK(s.rules[0].declarations[3].property == "margin-left");
    for (const auto& d : s.rules[0].declarations) {
        CHECK(d.value == "10px");
    }
}

TEST_CASE("stylesheet: margin two values", "[css][stylesheet][shorthand]") {
    const auto s = Stylesheet::parse("p { margin: 10px 20px }");
    REQUIRE(s.rules.size() == 1);
    REQUIRE(s.rules[0].declarations.size() == 4);
    CHECK(s.rules[0].declarations[0].value == "10px");
    CHECK(s.rules[0].declarations[1].value == "20px");
    CHECK(s.rules[0].declarations[2].value == "10px");
    CHECK(s.rules[0].declarations[3].value == "20px");
}

TEST_CASE("stylesheet: margin three values", "[css][stylesheet][shorthand]") {
    const auto s = Stylesheet::parse("p { margin: 10px 20px 30px }");
    REQUIRE(s.rules.size() == 1);
    REQUIRE(s.rules[0].declarations.size() == 4);
    CHECK(s.rules[0].declarations[0].value == "10px");
    CHECK(s.rules[0].declarations[1].value == "20px");
    CHECK(s.rules[0].declarations[2].value == "30px");
    CHECK(s.rules[0].declarations[3].value == "20px");
}

TEST_CASE("stylesheet: margin four values",
          "[css][stylesheet][shorthand]") {
    const auto s = Stylesheet::parse("p { margin: 1px 2px 3px 4px }");
    REQUIRE(s.rules.size() == 1);
    REQUIRE(s.rules[0].declarations.size() == 4);
    CHECK(s.rules[0].declarations[0].value == "1px");
    CHECK(s.rules[0].declarations[1].value == "2px");
    CHECK(s.rules[0].declarations[2].value == "3px");
    CHECK(s.rules[0].declarations[3].value == "4px");
}

TEST_CASE("stylesheet: margin too many values skipped",
          "[css][stylesheet][shorthand]") {
    const auto s = Stylesheet::parse("p { margin: 1px 2px 3px 4px 5px }");
    REQUIRE(s.rules.size() == 1);
    CHECK(s.rules[0].declarations.empty());
}

TEST_CASE("stylesheet: margin shorthand with important",
          "[css][stylesheet][shorthand]") {
    const auto s = Stylesheet::parse("p { margin: 10px 20px !important }");
    REQUIRE(s.rules.size() == 1);
    REQUIRE(s.rules[0].declarations.size() == 4);
    for (const auto& d : s.rules[0].declarations) {
        CHECK(d.important);
    }
}

TEST_CASE("stylesheet: longhand not expanded",
          "[css][stylesheet][shorthand]") {
    const auto s = Stylesheet::parse("p { margin-top: 10px }");
    REQUIRE(s.rules.size() == 1);
    REQUIRE(s.rules[0].declarations.size() == 1);
    CHECK(s.rules[0].declarations[0].property == "margin-top");
    CHECK(s.rules[0].declarations[0].value == "10px");
}

TEST_CASE("stylesheet: padding expands", "[css][stylesheet][shorthand]") {
    const auto s = Stylesheet::parse("p { padding: 5px }");
    REQUIRE(s.rules.size() == 1);
    REQUIRE(s.rules[0].declarations.size() == 4);
    CHECK(s.rules[0].declarations[0].property == "padding-top");
    CHECK(s.rules[0].declarations[3].property == "padding-left");
}

TEST_CASE("stylesheet: border-width expands",
          "[css][stylesheet][shorthand]") {
    const auto s = Stylesheet::parse("p { border-width: 1px 2px }");
    REQUIRE(s.rules.size() == 1);
    REQUIRE(s.rules[0].declarations.size() == 4);
    CHECK(s.rules[0].declarations[0].property == "border-top-width");
    CHECK(s.rules[0].declarations[0].value == "1px");
    CHECK(s.rules[0].declarations[1].property == "border-right-width");
    CHECK(s.rules[0].declarations[1].value == "2px");
    CHECK(s.rules[0].declarations[2].value == "1px");
    CHECK(s.rules[0].declarations[3].value == "2px");
}

// =============================================================
// ADR-026: Stylesheet::variables extraction from :root rules.
// =============================================================

TEST_CASE("Stylesheet::variables: empty for :root without custom props",
          "[css][stylesheet][adr-026]") {
    auto sheet = Stylesheet::parse(":root { color: red }");
    CHECK(sheet.variables.empty());
    CHECK(sheet.rules.size() == 1);
}

TEST_CASE("Stylesheet::variables: single :root block",
          "[css][stylesheet][adr-026]") {
    auto sheet = Stylesheet::parse(
        ":root { --color-base: #202122; --font-size: 1rem }");
    REQUIRE(sheet.variables.size() == 2);
    CHECK(sheet.variables.at("--color-base") == "#202122");
    CHECK(sheet.variables.at("--font-size") == "1rem");
    // Rule still in the sheet (declaration semantics preserved).
    CHECK(sheet.rules.size() == 1);
}

TEST_CASE("Stylesheet::variables: two :root blocks merge, later wins",
          "[css][stylesheet][adr-026]") {
    auto sheet = Stylesheet::parse(
        ":root { --a: red; --b: blue }"
        ":root { --a: green }");
    REQUIRE(sheet.variables.size() == 2);
    CHECK(sheet.variables.at("--a") == "green");   // later wins
    CHECK(sheet.variables.at("--b") == "blue");
}

TEST_CASE("Stylesheet::variables: multi-selector :root rule NOT extracted",
          "[css][stylesheet][adr-026]") {
    // Conservative: `:root, .foo` doesn't trigger extraction.
    auto sheet = Stylesheet::parse(":root, .foo { --x: red }");
    CHECK(sheet.variables.empty());
    CHECK(sheet.rules.size() == 1);
}

TEST_CASE("Stylesheet::variables: non-:root rule NOT extracted",
          "[css][stylesheet][adr-026]") {
    auto sheet = Stylesheet::parse(
        "html { --x: red }"               // Type selector, not :root
        ".foo { --y: blue }"              // Class selector
        "html:root { --z: green }");      // Compound, not sole :root
    CHECK(sheet.variables.empty());
    CHECK(sheet.rules.size() == 3);
}

TEST_CASE("matches_simple: :root matches <html> not <body>",
          "[css][selectors][adr-026]") {
    auto doc = honeybee::renderer::html::parse(
        "<html><body><p>x</p></body></html>");
    REQUIRE(doc);

    auto sel = Selector::parse(":root");
    REQUIRE(sel);

    // Find <html> and <body>.
    const auto* html = doc->find_element("html");
    const auto* body = doc->find_element("body");
    REQUIRE(html);
    REQUIRE(body);

    honeybee::renderer::css::RenderState state;
    CHECK(sel->matches(*html, state) == true);
    CHECK(sel->matches(*body, state) == false);
}

// =============================================================
// ADR-026: var() pre-resolution.
//
// Whole-value only: `prop: var(--x[, fallback])`. Partial values
// (e.g. `1px solid var(--x)`) are left unchanged (deferred).
// =============================================================

TEST_CASE("var(): defined variable resolves to value",
          "[css][stylesheet][adr-026][var]") {
    auto sheet = Stylesheet::parse(
        ":root { --color-base: #202122 }"
        "a { color: var(--color-base) }");
    REQUIRE(sheet.rules.size() == 2);
    REQUIRE(sheet.rules[1].declarations.size() == 1);
    CHECK(sheet.rules[1].declarations[0].value == "#202122");
}

TEST_CASE("var(): undefined variable + fallback uses fallback",
          "[css][stylesheet][adr-026][var]") {
    auto sheet = Stylesheet::parse(
        "a { color: var(--undefined-color, #ff0000) }");
    REQUIRE(sheet.rules.size() == 1);
    REQUIRE(sheet.rules[0].declarations.size() == 1);
    CHECK(sheet.rules[0].declarations[0].value == "#ff0000");
}

TEST_CASE("var(): undefined variable + no fallback drops declaration",
          "[css][stylesheet][adr-026][var]") {
    auto sheet = Stylesheet::parse(
        "a { color: var(--undefined-no-fb) }");
    REQUIRE(sheet.rules.size() == 1);
    CHECK(sheet.rules[0].declarations.empty());
}

TEST_CASE("var(): embedded var() in composite value resolved (ADR-037)",
          "[css][stylesheet][adr-026][adr-037][var]") {
    // ADR-026 v1 only resolved whole-value var(); embedded var()
    // in a composite value like "1px solid var(...)" was a
    // documented deferral.
    // ADR-037 generalized the resolver to any embedded var()
    // occurrence (required for var()-inside-calc support, which
    // needs to resolve var() before parse_length sees calc()).
    // This test's contract changed: embedded var() now resolves
    // to its fallback.
    auto sheet = Stylesheet::parse(
        "a { border: 1px solid var(--border-color, #ccc) }");
    REQUIRE(sheet.rules.size() == 1);
    REQUIRE(sheet.rules[0].declarations.size() == 1);
    CHECK(sheet.rules[0].declarations[0].value == "1px solid #ccc");
}

TEST_CASE("var(): non-var value unchanged",
          "[css][stylesheet][adr-026][var]") {
    auto sheet = Stylesheet::parse("a { color: #202122 }");
    REQUIRE(sheet.rules.size() == 1);
    REQUIRE(sheet.rules[0].declarations.size() == 1);
    CHECK(sheet.rules[0].declarations[0].value == "#202122");
}

TEST_CASE("var(): nested var() in variable uses fallback",
          "[css][stylesheet][adr-026][var]") {
    auto sheet = Stylesheet::parse(
        ":root { --image-size-standard: 250px;"
        "        --image-size-user: var(--image-size-standard) }"
        "img { width: var(--image-size-user, 300px) }");
    bool found_img = false;
    for (const auto& rule : sheet.rules) {
        if (rule.selectors.selectors.size() == 1 &&
            rule.selectors.selectors[0].parts.size() == 1 &&
            rule.selectors.selectors[0].parts[0].compound.simples.size() == 1 &&
            rule.selectors.selectors[0].parts[0].compound.simples[0].kind ==
                honeybee::renderer::css::SimpleKind::Type &&
            rule.selectors.selectors[0].parts[0].compound.simples[0].name == "img") {
            REQUIRE(rule.declarations.size() == 1);
            CHECK(rule.declarations[0].value == "300px");
            found_img = true;
        }
    }
    CHECK(found_img);
}

TEST_CASE("var(): custom-property declarations themselves are NOT resolved",
          "[css][stylesheet][adr-026][var]") {
    auto sheet = Stylesheet::parse(
        ":root { --y: red; --x: var(--y) }");
    REQUIRE(sheet.rules.size() == 1);
    bool found_x = false;
    for (const auto& decl : sheet.rules[0].declarations) {
        if (decl.property == "--x") {
            CHECK(decl.value == "var(--y)");
            found_x = true;
        }
    }
    CHECK(found_x);
    REQUIRE(sheet.variables.count("--x") == 1);
    CHECK(sheet.variables.at("--x") == "var(--y)");
}

TEST_CASE("var(): fallback with nested parens (e.g. rgb())",
          "[css][stylesheet][adr-026][var]") {
    auto sheet = Stylesheet::parse(
        "a { color: var(--undefined, rgb(0, 0, 0)) }");
    REQUIRE(sheet.rules.size() == 1);
    REQUIRE(sheet.rules[0].declarations.size() == 1);
    CHECK(sheet.rules[0].declarations[0].value == "rgb(0, 0, 0)");
}

TEST_CASE("var(): malformed var() dropped",
          "[css][stylesheet][adr-026][var]") {
    auto sheet = Stylesheet::parse("a { color: var(notvar) }");
    REQUIRE(sheet.rules.size() == 1);
    CHECK(sheet.rules[0].declarations.empty());
}

// =============================================================
// ADR-027: @media prelude classification + block extraction.
// =============================================================

TEST_CASE("media: min-width parses to min_width_px",
          "[css][stylesheet][adr-027]") {
    const auto s = Stylesheet::parse(
        "@media (min-width: 640px) { a { color: red } }");
    REQUIRE(s.rules.size() == 1);
    REQUIRE(s.rules[0].media.has_value());
    REQUIRE(s.rules[0].media->min_width_px.has_value());
    CHECK(*s.rules[0].media->min_width_px == 640.0f);
    CHECK_FALSE(s.rules[0].media->max_width_px.has_value());
}

TEST_CASE("media: max-width parses to max_width_px",
          "[css][stylesheet][adr-027]") {
    const auto s = Stylesheet::parse(
        "@media (max-width: 1399px) { a { color: red } }");
    REQUIRE(s.rules.size() == 1);
    REQUIRE(s.rules[0].media.has_value());
    REQUIRE(s.rules[0].media->max_width_px.has_value());
    CHECK(*s.rules[0].media->max_width_px == 1399.0f);
}

TEST_CASE("media: min-width and max-width combined",
          "[css][stylesheet][adr-027]") {
    const auto s = Stylesheet::parse(
        "@media (min-width: 640px) and (max-width: 1024px) "
        "{ a { color: red } }");
    REQUIRE(s.rules.size() == 1);
    REQUIRE(s.rules[0].media.has_value());
    CHECK(*s.rules[0].media->min_width_px == 640.0f);
    CHECK(*s.rules[0].media->max_width_px == 1024.0f);
}

TEST_CASE("media: calc(Npx - Mpx) evaluated",
          "[css][stylesheet][adr-027]") {
    const auto s = Stylesheet::parse(
        "@media (max-width: calc(1120px - 1px)) { a { color: red } }");
    REQUIRE(s.rules.size() == 1);
    REQUIRE(s.rules[0].media.has_value());
    REQUIRE(s.rules[0].media->max_width_px.has_value());
    CHECK(*s.rules[0].media->max_width_px == 1119.0f);
}

TEST_CASE("media: calc(Npx + Mpx) evaluated",
          "[css][stylesheet][adr-027]") {
    const auto s = Stylesheet::parse(
        "@media (min-width: calc(640px + 10px)) { a { color: red } }");
    REQUIRE(s.rules.size() == 1);
    REQUIRE(s.rules[0].media.has_value());
    REQUIRE(s.rules[0].media->min_width_px.has_value());
    CHECK(*s.rules[0].media->min_width_px == 650.0f);
}

TEST_CASE("media: print query drops its rules",
          "[css][stylesheet][adr-027]") {
    const auto s = Stylesheet::parse(
        "@media print { a { color: red } }");
    // Unsupported -> rule dropped entirely.
    CHECK(s.rules.empty());
}

TEST_CASE("media: prefers-color-scheme query drops its rules",
          "[css][stylesheet][adr-027]") {
    const auto s = Stylesheet::parse(
        "@media (prefers-color-scheme: dark) { a { color: red } }");
    CHECK(s.rules.empty());
}

TEST_CASE("media: prefers-reduced-motion query drops its rules",
          "[css][stylesheet][adr-027]") {
    const auto s = Stylesheet::parse(
        "@media (prefers-reduced-motion: reduce) { a { color: red } }");
    CHECK(s.rules.empty());
}

TEST_CASE("media: mixed-unit calc drops its rules",
          "[css][stylesheet][adr-027]") {
    const auto s = Stylesheet::parse(
        "@media (min-width: calc(50% - 20px)) { a { color: red } }");
    CHECK(s.rules.empty());
}

TEST_CASE("media: multiple rules inside one block all get the MediaQuery",
          "[css][stylesheet][adr-027]") {
    const auto s = Stylesheet::parse(
        "@media (min-width: 640px) { a { color: red } p { color: blue } }");
    REQUIRE(s.rules.size() == 2);
    REQUIRE(s.rules[0].media.has_value());
    REQUIRE(s.rules[1].media.has_value());
    CHECK(*s.rules[0].media->min_width_px == 640.0f);
    CHECK(*s.rules[1].media->min_width_px == 640.0f);
}

TEST_CASE("media: screen and (min-width) parsed correctly",
          "[css][stylesheet][adr-027]") {
    const auto s = Stylesheet::parse(
        "@media screen and (min-width: 1120px) { a { color: red } }");
    REQUIRE(s.rules.size() == 1);
    REQUIRE(s.rules[0].media.has_value());
    CHECK(*s.rules[0].media->min_width_px == 1120.0f);
}

TEST_CASE("media: bare 'screen' prelude matches all viewports",
          "[css][stylesheet][adr-027]") {
    const auto s = Stylesheet::parse(
        "@media screen { a { color: red } }");
    REQUIRE(s.rules.size() == 1);
    REQUIRE(s.rules[0].media.has_value());
    CHECK_FALSE(s.rules[0].media->min_width_px.has_value());
    CHECK_FALSE(s.rules[0].media->max_width_px.has_value());
}

TEST_CASE("media: declarations inside @media rules are preserved",
          "[css][stylesheet][adr-027]") {
    const auto s = Stylesheet::parse(
        "@media (min-width: 640px) { "
        "a { color: red; font-size: 16px } "
        "}");
    REQUIRE(s.rules.size() == 1);
    REQUIRE(s.rules[0].declarations.size() == 2);
    CHECK(s.rules[0].declarations[0].property == "color");
    CHECK(s.rules[0].declarations[0].value == "red");
    CHECK(s.rules[0].declarations[1].property == "font-size");
    CHECK(s.rules[0].declarations[1].value == "16px");
}

TEST_CASE("media: @import still skipped",
          "[css][stylesheet][adr-027]") {
    // Non-@media at-rules use the original skip_at_rule path.
    const auto s = Stylesheet::parse("@import url(\"x.css\"); h1 {}");
    REQUIRE(s.rules.size() == 1);
    CHECK_FALSE(s.rules[0].media.has_value());
}

// =============================================================
// ADR-028: grid-template shorthand expansion.
// =============================================================

TEST_CASE("grid-template: simple rows / columns expansion",
          "[css][stylesheet][adr-028]") {
    auto sheet = Stylesheet::parse(
        ".x { grid-template: 1fr / 1fr 1fr }");
    REQUIRE(sheet.rules.size() == 1);
    REQUIRE(sheet.rules[0].declarations.size() == 2);
    // Order: rows first, then columns.
    CHECK(sheet.rules[0].declarations[0].property == "grid-template-rows");
    CHECK(sheet.rules[0].declarations[0].value == "1fr");
    CHECK(sheet.rules[0].declarations[1].property == "grid-template-columns");
    CHECK(sheet.rules[0].declarations[1].value == "1fr 1fr");
}

TEST_CASE("grid-template: Wikipedia's actual shorthand",
          "[css][stylesheet][adr-028]") {
    auto sheet = Stylesheet::parse(
        ".mw-page-container-inner { "
        "grid-template: min-content 1fr min-content / 12.25rem minmax(0, 1fr) "
        "}");
    REQUIRE(sheet.rules.size() == 1);
    REQUIRE(sheet.rules[0].declarations.size() == 2);
    CHECK(sheet.rules[0].declarations[0].property == "grid-template-rows");
    CHECK(sheet.rules[0].declarations[0].value
          == "min-content 1fr min-content");
    CHECK(sheet.rules[0].declarations[1].property == "grid-template-columns");
    CHECK(sheet.rules[0].declarations[1].value == "12.25rem minmax(0, 1fr)");
}

TEST_CASE("grid-template: preserves !important on both longhands",
          "[css][stylesheet][adr-028]") {
    auto sheet = Stylesheet::parse(
        ".x { grid-template: 1fr / 1fr !important }");
    REQUIRE(sheet.rules.size() == 1);
    REQUIRE(sheet.rules[0].declarations.size() == 2);
    CHECK(sheet.rules[0].declarations[0].important);
    CHECK(sheet.rules[0].declarations[1].important);
}

TEST_CASE("grid-template: inline-areas form dropped (deferred)",
          "[css][stylesheet][adr-028]") {
    // ADR-028 Defers inline-areas form (`grid-template: "a a" 1fr / 1fr`).
    // The parser drops such rules rather than misparsing them.
    auto sheet = Stylesheet::parse(
        ".x { grid-template: \"a a\" 1fr / 1fr 1fr }");
    REQUIRE(sheet.rules.size() == 1);
    CHECK(sheet.rules[0].declarations.empty());
}

TEST_CASE("grid-template: malformed (no slash) dropped",
          "[css][stylesheet][adr-028]") {
    auto sheet = Stylesheet::parse(".x { grid-template: 1fr }");
    REQUIRE(sheet.rules.size() == 1);
    CHECK(sheet.rules[0].declarations.empty());
}

// =============================================================
// ADR-029: flex shorthand expansion.
// =============================================================

TEST_CASE("flex: single number expands to grow/shrink/basis",
          "[css][stylesheet][adr-029]") {
    auto sheet = Stylesheet::parse(".x { flex: 1 }");
    REQUIRE(sheet.rules.size() == 1);
    REQUIRE(sheet.rules[0].declarations.size() == 3);
    CHECK(sheet.rules[0].declarations[0].property == "flex-grow");
    CHECK(sheet.rules[0].declarations[0].value == "1");
    CHECK(sheet.rules[0].declarations[1].property == "flex-shrink");
    CHECK(sheet.rules[0].declarations[1].value == "1");
    CHECK(sheet.rules[0].declarations[2].property == "flex-basis");
    CHECK(sheet.rules[0].declarations[2].value == "0");
}

TEST_CASE("flex: two numbers expand with default basis 0",
          "[css][stylesheet][adr-029]") {
    auto sheet = Stylesheet::parse(".x { flex: 2 3 }");
    REQUIRE(sheet.rules.size() == 1);
    REQUIRE(sheet.rules[0].declarations.size() == 3);
    CHECK(sheet.rules[0].declarations[0].value == "2");
    CHECK(sheet.rules[0].declarations[1].value == "3");
    CHECK(sheet.rules[0].declarations[2].value == "0");
}

TEST_CASE("flex: Wikipedia's actual shorthand (1 1 100%)",
          "[css][stylesheet][adr-029]") {
    auto sheet = Stylesheet::parse(".x { flex: 1 1 100% }");
    REQUIRE(sheet.rules.size() == 1);
    REQUIRE(sheet.rules[0].declarations.size() == 3);
    CHECK(sheet.rules[0].declarations[0].value == "1");
    CHECK(sheet.rules[0].declarations[1].value == "1");
    CHECK(sheet.rules[0].declarations[2].value == "100%");
}

TEST_CASE("flex: auto keyword", "[css][stylesheet][adr-029]") {
    auto sheet = Stylesheet::parse(".x { flex: auto }");
    REQUIRE(sheet.rules.size() == 1);
    REQUIRE(sheet.rules[0].declarations.size() == 3);
    CHECK(sheet.rules[0].declarations[0].value == "1");
    CHECK(sheet.rules[0].declarations[1].value == "1");
    CHECK(sheet.rules[0].declarations[2].value == "auto");
}

TEST_CASE("flex: none keyword", "[css][stylesheet][adr-029]") {
    auto sheet = Stylesheet::parse(".x { flex: none }");
    REQUIRE(sheet.rules.size() == 1);
    REQUIRE(sheet.rules[0].declarations.size() == 3);
    CHECK(sheet.rules[0].declarations[0].value == "0");
    CHECK(sheet.rules[0].declarations[1].value == "0");
    CHECK(sheet.rules[0].declarations[2].value == "auto");
}

TEST_CASE("flex: preserves !important", "[css][stylesheet][adr-029]") {
    auto sheet = Stylesheet::parse(".x { flex: 1 1 auto !important }");
    REQUIRE(sheet.rules.size() == 1);
    REQUIRE(sheet.rules[0].declarations.size() == 3);
    CHECK(sheet.rules[0].declarations[0].important);
    CHECK(sheet.rules[0].declarations[1].important);
    CHECK(sheet.rules[0].declarations[2].important);
}

// =============================================================
// ADR-029: flex-flow shorthand expansion.
// =============================================================

TEST_CASE("flex-flow: single direction", "[css][stylesheet][adr-029]") {
    auto sheet = Stylesheet::parse(".x { flex-flow: row }");
    REQUIRE(sheet.rules.size() == 1);
    REQUIRE(sheet.rules[0].declarations.size() == 2);
    CHECK(sheet.rules[0].declarations[0].property == "flex-direction");
    CHECK(sheet.rules[0].declarations[0].value == "row");
    CHECK(sheet.rules[0].declarations[1].property == "flex-wrap");
    CHECK(sheet.rules[0].declarations[1].value == "nowrap");
}

TEST_CASE("flex-flow: column", "[css][stylesheet][adr-029]") {
    auto sheet = Stylesheet::parse(".x { flex-flow: column }");
    REQUIRE(sheet.rules.size() == 1);
    REQUIRE(sheet.rules[0].declarations.size() == 2);
    CHECK(sheet.rules[0].declarations[0].value == "column");
    CHECK(sheet.rules[0].declarations[1].value == "nowrap");
}

TEST_CASE("flex-flow: direction + wrap", "[css][stylesheet][adr-029]") {
    auto sheet = Stylesheet::parse(".x { flex-flow: row nowrap }");
    REQUIRE(sheet.rules.size() == 1);
    REQUIRE(sheet.rules[0].declarations.size() == 2);
    CHECK(sheet.rules[0].declarations[0].value == "row");
    CHECK(sheet.rules[0].declarations[1].value == "nowrap");
}

TEST_CASE("flex-flow: single wrap keyword", "[css][stylesheet][adr-029]") {
    auto sheet = Stylesheet::parse(".x { flex-flow: wrap }");
    REQUIRE(sheet.rules.size() == 1);
    REQUIRE(sheet.rules[0].declarations.size() == 2);
    CHECK(sheet.rules[0].declarations[0].value == "row");
    CHECK(sheet.rules[0].declarations[1].value == "wrap");
}

TEST_CASE("flex-flow: wrap then direction", "[css][stylesheet][adr-029]") {
    auto sheet = Stylesheet::parse(".x { flex-flow: wrap column }");
    REQUIRE(sheet.rules.size() == 1);
    REQUIRE(sheet.rules[0].declarations.size() == 2);
    CHECK(sheet.rules[0].declarations[0].value == "column");
    CHECK(sheet.rules[0].declarations[1].value == "wrap");
}

TEST_CASE("flex-flow: preserves !important",
          "[css][stylesheet][adr-029]") {
    auto sheet = Stylesheet::parse(".x { flex-flow: row wrap !important }");
    REQUIRE(sheet.rules.size() == 1);
    REQUIRE(sheet.rules[0].declarations.size() == 2);
    CHECK(sheet.rules[0].declarations[0].important);
    CHECK(sheet.rules[0].declarations[1].important);
}

// =============================================================
// ADR-029: display: flex / inline-flex pass through untouched.
// =============================================================

TEST_CASE("display: flex passes through as single declaration",
          "[css][stylesheet][adr-029]") {
    auto sheet = Stylesheet::parse(".x { display: flex }");
    REQUIRE(sheet.rules.size() == 1);
    REQUIRE(sheet.rules[0].declarations.size() == 1);
    CHECK(sheet.rules[0].declarations[0].property == "display");
    CHECK(sheet.rules[0].declarations[0].value == "flex");
}

TEST_CASE("display: inline-flex passes through",
          "[css][stylesheet][adr-029]") {
    auto sheet = Stylesheet::parse(".x { display: inline-flex }");
    REQUIRE(sheet.rules.size() == 1);
    REQUIRE(sheet.rules[0].declarations.size() == 1);
    CHECK(sheet.rules[0].declarations[0].property == "display");
    CHECK(sheet.rules[0].declarations[0].value == "inline-flex");
}

TEST_CASE("grid-template-columns alone is not a shorthand",
          "[css][stylesheet][adr-028]") {
    auto sheet = Stylesheet::parse(
        ".x { grid-template-columns: 12.25rem minmax(0, 1fr) }");
    REQUIRE(sheet.rules.size() == 1);
    REQUIRE(sheet.rules[0].declarations.size() == 1);
    CHECK(sheet.rules[0].declarations[0].property
          == "grid-template-columns");
    CHECK(sheet.rules[0].declarations[0].value
          == "12.25rem minmax(0, 1fr)");
}

TEST_CASE("grid-template-areas passes through unchanged",
          "[css][stylesheet][adr-028]") {
    auto sheet = Stylesheet::parse(
        ".x { grid-template-areas: 'a a' 'b c' }");
    REQUIRE(sheet.rules.size() == 1);
    REQUIRE(sheet.rules[0].declarations.size() == 1);
    CHECK(sheet.rules[0].declarations[0].property
          == "grid-template-areas");
    CHECK(sheet.rules[0].declarations[0].value == "'a a' 'b c'");
}

TEST_CASE("grid-area on child element stored in ComputedStyle",
          "[css][stylesheet][adr-028]") {
    auto sheet = Stylesheet::parse(
        ".sidebar { grid-area: columnStart }"
        ".content { grid-area: pageContent }");
    REQUIRE(sheet.rules.size() == 2);
    CHECK(sheet.rules[0].declarations[0].property == "grid-area");
    CHECK(sheet.rules[0].declarations[0].value == "columnStart");
    CHECK(sheet.rules[1].declarations[0].value == "pageContent");
}
