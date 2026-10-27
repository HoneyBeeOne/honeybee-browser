// =============================================================
// Tests for structural pseudo-classes (ADR-038).
//
// Covers :first-child, :last-child, :only-child, :empty,
// :first-of-type, :last-of-type, :only-of-type, and the An+B
// family (:nth-child, :nth-last-child, :nth-of-type,
// :nth-last-of-type).
// =============================================================

#include "renderer/css/stylesheet.hpp"
#include "renderer/css/cascade.hpp"
#include "renderer/css/ua_stylesheet.hpp"
#include "renderer/css/render_state.hpp"
#include "renderer/html/convert.hpp"

#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <string>

using namespace honeybee::renderer;

namespace {

std::string get_color(const std::string& html, const std::string& css,
                      const std::string& target_cls) {
    auto doc = html::parse(html);
    auto sheet = css::Stylesheet::parse(css);
    auto styles = css::cascade(css::ua_stylesheet(), sheet, *doc,
                                css::RenderState{}, 800.0f);
    const dom::Element* target = nullptr;
    std::function<void(const dom::Node&)> find = [&](const dom::Node& n) {
        if (target) return;
        if (n.type() == dom::NodeType::Element) {
            const auto& el = static_cast<const dom::Element&>(n);
            if (el.get_attribute("class").find(target_cls) != std::string::npos) {
                target = &el;
                return;
            }
            for (const auto& c : n.children()) find(*c);
        }
    };
    // Document's children are the top-level elements; recurse from them.
    for (const auto& c : doc->children()) find(*c);
    if (!target) return "(no target)";
    auto it = styles.find(target);
    if (it == styles.end()) return "(no style)";
    auto v = it->second.get("color");
    return v ? *v : "(unset)";
}

}  // namespace

// =============================================================
// :first-child / :last-child / :only-child
// =============================================================

TEST_CASE(":first-child matches first, not second",
          "[css][pseudos][adr-038]") {
    std::string html = "<html><body>"
        "<div class='a'>A</div>"
        "<div class='b'>B</div>"
        "</body></html>";
    std::string css =
        ".a:first-child { color: red }"
        ".b:first-child { color: blue }";
    CHECK(get_color(html, css, "a") == "red");
    CHECK(get_color(html, css, "b") == "(unset)");
}

TEST_CASE(":last-child matches last, not second-to-last",
          "[css][pseudos][adr-038]") {
    std::string html = "<html><body>"
        "<div class='a'>A</div>"
        "<div class='b'>B</div>"
        "<div class='c'>C</div>"
        "</body></html>";
    std::string css =
        ".a:last-child { color: red }"
        ".b:last-child { color: blue }"
        ".c:last-child { color: green }";
    CHECK(get_color(html, css, "a") == "(unset)");
    CHECK(get_color(html, css, "b") == "(unset)");
    CHECK(get_color(html, css, "c") == "green");
}

TEST_CASE(":only-child matches sole element",
          "[css][pseudos][adr-038]") {
    std::string only_html = "<html><body>"
        "<div class='a'>A</div>"
        "</body></html>";
    std::string two_html = "<html><body>"
        "<div class='a'>A</div>"
        "<div class='b'>B</div>"
        "</body></html>";
    std::string css = ".a:only-child { color: red }";
    CHECK(get_color(only_html, css, "a") == "red");
    CHECK(get_color(two_html, css, "a") == "(unset)");
}

// =============================================================
// :empty
// =============================================================

TEST_CASE(":empty matches whitespace-only and empty",
          "[css][pseudos][adr-038]") {
    std::string empty_html = "<html><body>"
        "<div class='a'></div>"
        "</body></html>";
    std::string ws_html = "<html><body>"
        "<div class='a'>   </div>"
        "</body></html>";
    std::string content_html = "<html><body>"
        "<div class='a'>x</div>"
        "</body></html>";
    std::string css = ".a:empty { color: red }";
    CHECK(get_color(empty_html, css, "a") == "red");
    CHECK(get_color(ws_html, css, "a") == "red");
    CHECK(get_color(content_html, css, "a") == "(unset)");
}

// =============================================================
// :nth-child An+B grammar
// =============================================================

TEST_CASE(":nth-child(odd) matches 1, 3",
          "[css][pseudos][adr-038]") {
    std::string html = "<html><body>"
        "<div class='a'>1</div>"
        "<div class='b'>2</div>"
        "<div class='c'>3</div>"
        "</body></html>";
    std::string css =
        ".a:nth-child(odd) { color: red }"
        ".b:nth-child(odd) { color: blue }"
        ".c:nth-child(odd) { color: green }";
    CHECK(get_color(html, css, "a") == "red");
    CHECK(get_color(html, css, "b") == "(unset)");
    CHECK(get_color(html, css, "c") == "green");
}

TEST_CASE(":nth-child(2n) matches evens",
          "[css][pseudos][adr-038]") {
    std::string html = "<html><body>"
        "<div class='a'>1</div>"
        "<div class='b'>2</div>"
        "<div class='c'>3</div>"
        "<div class='d'>4</div>"
        "</body></html>";
    std::string css =
        ".a:nth-child(2n) { color: red }"
        ".b:nth-child(2n) { color: blue }"
        ".c:nth-child(2n) { color: green }"
        ".d:nth-child(2n) { color: orange }";
    CHECK(get_color(html, css, "a") == "(unset)");
    CHECK(get_color(html, css, "b") == "blue");
    CHECK(get_color(html, css, "c") == "(unset)");
    CHECK(get_color(html, css, "d") == "orange");
}

TEST_CASE(":nth-child(3n+2) matches 2, 5",
          "[css][pseudos][adr-038]") {
    std::string html = "<html><body>"
        "<div class='a'>1</div><div class='b'>2</div>"
        "<div class='c'>3</div><div class='d'>4</div>"
        "<div class='e'>5</div>"
        "</body></html>";
    std::string css =
        ".a:nth-child(3n+2) { color: red }"
        ".b:nth-child(3n+2) { color: blue }"
        ".c:nth-child(3n+2) { color: green }"
        ".d:nth-child(3n+2) { color: orange }"
        ".e:nth-child(3n+2) { color: purple }";
    CHECK(get_color(html, css, "a") == "(unset)");
    CHECK(get_color(html, css, "b") == "blue");
    CHECK(get_color(html, css, "c") == "(unset)");
    CHECK(get_color(html, css, "d") == "(unset)");
    CHECK(get_color(html, css, "e") == "purple");
}

TEST_CASE(":nth-child(-n+3) matches 1, 2, 3 but not 4",
          "[css][pseudos][adr-038]") {
    std::string html = "<html><body>"
        "<div class='a'>1</div><div class='b'>2</div>"
        "<div class='c'>3</div><div class='d'>4</div>"
        "</body></html>";
    std::string css =
        ".a:nth-child(-n+3) { color: red }"
        ".b:nth-child(-n+3) { color: blue }"
        ".c:nth-child(-n+3) { color: green }"
        ".d:nth-child(-n+3) { color: orange }";
    CHECK(get_color(html, css, "a") == "red");
    CHECK(get_color(html, css, "b") == "blue");
    CHECK(get_color(html, css, "c") == "green");
    CHECK(get_color(html, css, "d") == "(unset)");
}

TEST_CASE(":nth-child(N) matches exactly index N",
          "[css][pseudos][adr-038]") {
    std::string html = "<html><body>"
        "<div class='a'>1</div><div class='b'>2</div>"
        "<div class='c'>3</div>"
        "</body></html>";
    std::string css =
        ".a:nth-child(3) { color: red }"
        ".b:nth-child(3) { color: blue }"
        ".c:nth-child(3) { color: green }";
    CHECK(get_color(html, css, "a") == "(unset)");
    CHECK(get_color(html, css, "b") == "(unset)");
    CHECK(get_color(html, css, "c") == "green");
}

// =============================================================
// *-of-type variants
// =============================================================

TEST_CASE(":first-of-type counts by tag",
          "[css][pseudos][adr-038]") {
    std::string html = "<html><body>"
        "<p>p1</p>"
        "<div class='a'>d1</div>"
        "<div class='b'>d2</div>"
        "</body></html>";
    std::string css =
        ".a:first-of-type { color: red }"
        ".b:first-of-type { color: blue }";
    // .a is first div (even though preceded by p)
    CHECK(get_color(html, css, "a") == "red");
    CHECK(get_color(html, css, "b") == "(unset)");
}

TEST_CASE(":last-of-type counts by tag",
          "[css][pseudos][adr-038]") {
    std::string html = "<html><body>"
        "<div class='a'>d1</div>"
        "<div class='b'>d2</div>"
        "<p>p1</p>"
        "</body></html>";
    std::string css =
        ".a:last-of-type { color: red }"
        ".b:last-of-type { color: blue }";
    // .b is last div (p follows but is different tag)
    CHECK(get_color(html, css, "a") == "(unset)");
    CHECK(get_color(html, css, "b") == "blue");
}

TEST_CASE(":only-of-type matches sole tag",
          "[css][pseudos][adr-038]") {
    std::string html = "<html><body>"
        "<p>p1</p>"
        "<div class='a'>d1</div>"
        "<p>p2</p>"
        "</body></html>";
    std::string css = ".a:only-of-type { color: red }";
    CHECK(get_color(html, css, "a") == "red");
}

TEST_CASE(":nth-of-type(2n) counts same tag only",
          "[css][pseudos][adr-038]") {
    std::string html = "<html><body>"
        "<div class='a'>d1</div>"
        "<p>x</p>"
        "<div class='b'>d2</div>"
        "</body></html>";
    std::string css =
        ".a:nth-of-type(2n) { color: red }"
        ".b:nth-of-type(2n) { color: blue }";
    // .a is 1st div (odd in div sequence -> no match)
    // .b is 2nd div (even in div sequence -> match)
    CHECK(get_color(html, css, "a") == "(unset)");
    CHECK(get_color(html, css, "b") == "blue");
}

// =============================================================
// ADR-038 Commit 2: logical pseudos :not / :is / :where
// =============================================================

TEST_CASE(":not(.class) inverts class match", "[css][pseudos][adr-038]") {
    std::string html = "<html><body>"
        "<div class='a'>A</div>"
        "<div class='b'>B</div>"
        "</body></html>";
    std::string css = "div:not(.a) { color: red }";
    CHECK(get_color(html, css, "a") == "(unset)");
    CHECK(get_color(html, css, "b") == "red");
}

TEST_CASE(":not([attr]) inverts attribute match",
          "[css][pseudos][adr-038]") {
    std::string html = "<html><body>"
        "<div class='a' role='button'>A</div>"
        "<div class='b'>B</div>"
        "</body></html>";
    std::string css = "div:not([role]) { color: red }";
    CHECK(get_color(html, css, "a") == "(unset)");
    CHECK(get_color(html, css, "b") == "red");
}

TEST_CASE(":not(:first-child) nests pseudo", "[css][pseudos][adr-038]") {
    std::string html = "<html><body>"
        "<div class='a'>A</div>"
        "<div class='b'>B</div>"
        "<div class='c'>C</div>"
        "</body></html>";
    std::string css = "div:not(:first-child) { color: red }";
    CHECK(get_color(html, css, "a") == "(unset)");
    CHECK(get_color(html, css, "b") == "red");
    CHECK(get_color(html, css, "c") == "red");
}

TEST_CASE(":is(.a, .b) matches either", "[css][pseudos][adr-038]") {
    std::string html = "<html><body>"
        "<div class='a'>A</div>"
        "<div class='b'>B</div>"
        "<div class='c'>C</div>"
        "</body></html>";
    std::string css = ":is(.a, .b) { color: red }";
    CHECK(get_color(html, css, "a") == "red");
    CHECK(get_color(html, css, "b") == "red");
    CHECK(get_color(html, css, "c") == "(unset)");
}

TEST_CASE(":where() zero specificity loses to class",
          "[css][pseudos][adr-038]") {
    std::string html = "<html><body>"
        "<div class='a'>A</div>"
        "</body></html>";
    std::string css =
        ":where(.a) { color: red }"
        ".a { color: blue }";
    CHECK(get_color(html, css, "a") == "blue");
}

TEST_CASE(":not() specificity (class+tag) beats plain tag",
          "[css][pseudos][adr-038]") {
    std::string html = "<html><body>"
        "<div class='a'>A</div>"
        "</body></html>";
    // div:not(.missing) has specificity 0,1,1
    // div alone has 0,0,1. The :not() rule wins.
    std::string css =
        "div { color: red }"
        "div:not(.missing) { color: blue }";
    CHECK(get_color(html, css, "a") == "blue");
}

// =============================================================
// ADR-038 Commit 3: :lang()
// =============================================================

TEST_CASE(":lang(en) matches element lang", "[css][pseudos][adr-038]") {
    std::string html = "<html lang='en'><body>"
        "<div class='a'>A</div>"
        "</body></html>";
    std::string css = "div:lang(en) { color: red }";
    CHECK(get_color(html, css, "a") == "red");
}

TEST_CASE(":lang(en) matches prefix 'en-US'", "[css][pseudos][adr-038]") {
    std::string html = "<html lang='en-US'><body>"
        "<div class='a'>A</div>"
        "</body></html>";
    std::string css = "div:lang(en) { color: red }";
    CHECK(get_color(html, css, "a") == "red");
}

TEST_CASE(":lang(en-US) does not match bare 'en'",
          "[css][pseudos][adr-038]") {
    std::string html = "<html lang='en'><body>"
        "<div class='a'>A</div>"
        "</body></html>";
    std::string css = "div:lang(en-US) { color: red }";
    CHECK(get_color(html, css, "a") == "(unset)");
}

TEST_CASE(":lang() no ancestor lang: no match",
          "[css][pseudos][adr-038]") {
    std::string html = "<html><body>"
        "<div class='a'>A</div>"
        "</body></html>";
    std::string css = "div:lang(en) { color: red }";
    CHECK(get_color(html, css, "a") == "(unset)");
}
