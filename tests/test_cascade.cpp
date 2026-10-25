// =============================================================
// Tests for the cascade (Commit 1: matching + specificity
// ordering). Inheritance tests arrive in Commit 2.
// =============================================================

#include "renderer/html/convert.hpp"
#include "renderer/css/ua_stylesheet.hpp"
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

TEST_CASE("cascade: :visited resolves sibling relative URL",
          "[css][cascade][pseudo]") {
    // ADR-018: href="other.html" with base ".../page.html"
    // resolves to ".../other.html" (base's last segment is
    // stripped, trailing slash kept).
    auto doc = make_anchor_doc("other.html");
    const auto* a = doc->find_element("a");
    REQUIRE(a);

    css::RenderState state;
    state.base_url = "https://example.com/page.html";
    state.visited_urls.insert("https://example.com/other.html");

    const auto author = Stylesheet::parse("a:visited { color: purple }");
    const auto result = cascade(Stylesheet{}, author, *doc, state);
    CHECK(result.at(a).get("color").value_or("") == "purple");
}

TEST_CASE("cascade: :visited on absolute URL ignores base",
          "[css][cascade][pseudo]") {
    // Absolute hrefs pass through untouched regardless of
    // base_url (potentially a different host).
    auto doc = make_anchor_doc("https://example.com/about");
    const auto* a = doc->find_element("a");
    REQUIRE(a);

    css::RenderState state;
    state.base_url = "https://other.com/page.html";
    state.visited_urls.insert("https://example.com/about");

    const auto author = Stylesheet::parse("a:visited { color: purple }");
    const auto result = cascade(Stylesheet{}, author, *doc, state);
    CHECK(result.at(a).get("color").value_or("") == "purple");
}

// =============================================================
// ADR-022: targeted cascade primitives.
// =============================================================

TEST_CASE("erase_subtree: removes leaf element only",
          "[cascade][targeted]") {
    auto doc = honeybee::renderer::html::parse(
        "<html><body><p>before <a href=\"#\">x</a> after</p>"
        "</body></html>");
    REQUIRE(doc);
    const auto* a = doc->find_element("a");
    const auto* p = doc->find_element("p");
    REQUIRE(a);
    REQUIRE(p);

    const auto ua = css::ua_stylesheet();
    const auto sheet = css::Stylesheet::parse("");
    auto styles = css::cascade(ua, sheet, *doc);

    REQUIRE(styles.count(a) == 1);
    REQUIRE(styles.count(p) == 1);

    css::erase_subtree(*a, styles);

    CHECK(styles.count(a) == 0);        // erased
    CHECK(styles.count(p) == 1);        // parent untouched
}

TEST_CASE("erase_subtree: recursively removes descendants",
          "[cascade][targeted]") {
    auto doc = honeybee::renderer::html::parse(
        "<html><body><div><span>x</span></div></body></html>");
    REQUIRE(doc);
    const auto* div = doc->find_element("div");
    const auto* span = doc->find_element("span");
    REQUIRE(div);
    REQUIRE(span);

    const auto ua = css::ua_stylesheet();
    const auto sheet = css::Stylesheet::parse("");
    auto styles = css::cascade(ua, sheet, *doc);

    REQUIRE(styles.count(div) == 1);
    REQUIRE(styles.count(span) == 1);

    css::erase_subtree(*div, styles);

    CHECK(styles.count(div) == 0);      // erased
    CHECK(styles.count(span) == 0);     // descendant also erased
}

TEST_CASE("cascade_subtree + propagate matches full cascade for subtree",
          "[cascade][targeted]") {
    auto doc = honeybee::renderer::html::parse(
        "<html><body><p>before <a href=\"#\">x</a> after</p>"
        "</body></html>");
    REQUIRE(doc);
    const auto* body = doc->find_element("body");
    const auto* p = doc->find_element("p");
    const auto* a = doc->find_element("a");
    REQUIRE(body);
    REQUIRE(p);
    REQUIRE(a);

    const auto ua = css::ua_stylesheet();
    const auto sheet = css::Stylesheet::parse("");

    // Full cascade: what the pipeline currently uses.
    const auto full = css::cascade(ua, sheet, *doc);

    // Recompose <body>'s subtree from scratch via targeted primitives,
    // then propagate inheritance for the full document.
    css::ComputedStyleMap targeted;
    css::cascade_subtree(*body, ua, sheet, css::RenderState{}, 1024.0f, targeted);
    css::propagate_inheritance(*doc, targeted);

    // Every element in <body>'s subtree should have the same
    // declarations as the full cascade produced.
    for (const auto* el : {body, p, a}) {
        REQUIRE(full.count(el) == 1);
        REQUIRE(targeted.count(el) == 1);
        for (const auto& [prop, val] : full.at(el).declarations) {
            const auto t = targeted.at(el).get(prop);
            REQUIRE(t.has_value());
            CHECK(*t == val.value);
        }
    }
}

// =============================================================
// ADR-027: @media viewport filtering at cascade time.
// =============================================================

namespace {
std::string rule_color_under_media(
    const std::string& media_prelude,
    const std::string& rule_selector_and_decls,
    float viewport_width) {
    auto doc = honeybee::renderer::html::parse(
        "<html><body><p class=\"x\">text</p></body></html>");
    auto sheet = css::Stylesheet::parse(
        "@media " + media_prelude + " { " +
        rule_selector_and_decls + " }");
    auto styles = css::cascade(css::ua_stylesheet(), sheet, *doc,
                               css::RenderState{}, viewport_width);
    const auto* p = doc->find_element("p");
    return styles.at(p).get("color").value_or("");
}
} // namespace

TEST_CASE("media: min-width: 640px fires at viewport 1024",
          "[css][cascade][adr-027]") {
    CHECK(rule_color_under_media("(min-width: 640px)",
                                 ".x { color: red }", 1024.0f) == "red");
    CHECK(rule_color_under_media("(min-width: 640px)",
                                 ".x { color: red }", 500.0f) == "");
    CHECK(rule_color_under_media("(min-width: 640px)",
                                 ".x { color: red }", 1500.0f) == "red");
}

TEST_CASE("media: max-width: 640px fires only below threshold",
          "[css][cascade][adr-027]") {
    CHECK(rule_color_under_media("(max-width: 640px)",
                                 ".x { color: red }", 500.0f) == "red");
    CHECK(rule_color_under_media("(max-width: 640px)",
                                 ".x { color: red }", 641.0f) == "");
    CHECK(rule_color_under_media("(max-width: 640px)",
                                 ".x { color: red }", 1024.0f) == "");
}

TEST_CASE("media: min-width: 1120px does NOT fire at viewport 1024",
          "[css][cascade][adr-027]") {
    CHECK(rule_color_under_media("(min-width: 1120px)",
                                 ".x { color: red }", 1024.0f) == "");
    CHECK(rule_color_under_media("(min-width: 1120px)",
                                 ".x { color: red }", 1200.0f) == "red");
}

TEST_CASE("media: max-width: calc(1120px - 1px) fires at viewport 1024",
          "[css][cascade][adr-027]") {
    // The critical Wikipedia mid-size layout case.
    CHECK(rule_color_under_media("(max-width: calc(1120px - 1px))",
                                 ".x { color: red }", 1024.0f) == "red");
    CHECK(rule_color_under_media("(max-width: calc(1120px - 1px))",
                                 ".x { color: red }", 1119.0f) == "red");
    CHECK(rule_color_under_media("(max-width: calc(1120px - 1px))",
                                 ".x { color: red }", 1120.0f) == "");
}

TEST_CASE("media: min-width AND max-width both required",
          "[css][cascade][adr-027]") {
    const std::string prelude = "(min-width: 640px) and (max-width: 1024px)";
    CHECK(rule_color_under_media(prelude, ".x { color: red }", 700.0f)
          == "red");
    CHECK(rule_color_under_media(prelude, ".x { color: red }", 1024.0f)
          == "red");
    CHECK(rule_color_under_media(prelude, ".x { color: red }", 500.0f)
          == "");
    CHECK(rule_color_under_media(prelude, ".x { color: red }", 1100.0f)
          == "");
}

TEST_CASE("media: top-level rule (no media) always matches",
          "[css][cascade][adr-027]") {
    auto doc = honeybee::renderer::html::parse(
        "<html><body><p class=\"x\">text</p></body></html>");
    auto sheet = css::Stylesheet::parse(".x { color: red }");
    // Viewport 500 and 1500 both see the top-level rule.
    for (float vw : {500.0f, 1024.0f, 1500.0f}) {
        auto styles = css::cascade(css::ua_stylesheet(), sheet, *doc,
                                   css::RenderState{}, vw);
        const auto* p = doc->find_element("p");
        CHECK(styles.at(p).get("color").value_or("") == "red");
    }
}

TEST_CASE("media: print @media never fires (dropped at parse)",
          "[css][cascade][adr-027]") {
    auto doc = honeybee::renderer::html::parse(
        "<html><body><p class=\"x\">text</p></body></html>");
    auto sheet = css::Stylesheet::parse(
        "@media print { .x { color: red } }");
    // print rule dropped at parse -> stylesheet empty -> no color.
    for (float vw : {500.0f, 1024.0f, 1500.0f}) {
        auto styles = css::cascade(css::ua_stylesheet(), sheet, *doc,
                                   css::RenderState{}, vw);
        const auto* p = doc->find_element("p");
        CHECK_FALSE(styles.at(p).get("color").has_value());
    }
}

// =============================================================
// ADR-028: display: grid parse-through.
// =============================================================

TEST_CASE("cascade: display: grid reaches ComputedStyle",
          "[css][cascade][adr-028]") {
    auto doc = honeybee::renderer::html::parse(
        "<html><body><div class=\"x\"></div></body></html>");
    auto sheet = css::Stylesheet::parse(".x { display: grid }");
    auto styles = css::cascade(css::ua_stylesheet(), sheet, *doc);
    const auto* div = doc->find_element("div");
    REQUIRE(div);
    CHECK(styles.at(div).get("display").value_or("") == "grid");
}
