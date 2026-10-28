// =============================================================
// ADR-042 Commit 2: pseudo-element synthesis in layout.
// =============================================================

#include "renderer/css/cascade.hpp"
#include "renderer/css/render_state.hpp"
#include "renderer/css/stylesheet.hpp"
#include "renderer/css/ua_stylesheet.hpp"
#include "renderer/html/convert.hpp"
#include "renderer/layout/layout.hpp"
#include "renderer/dom/node.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <memory>

namespace {

using namespace honeybee::renderer;

struct TestDoc {
    std::unique_ptr<dom::Document> doc;
    layout::LayoutBox root;
};

std::pair<float, float> fixed_measurer(
        std::string_view text, std::string_view /*family*/,
        float font_size) {
    return {text.size() * font_size * 0.5f, font_size};
}

TestDoc run(const std::string& html, const std::string& css,
            float viewport_w = 800.0f) {
    TestDoc t;
    t.doc = html::parse(html);
    if (!t.doc) return t;
    auto sheet = css::Stylesheet::parse(css);
    auto result = css::cascade_with_pseudo(
        css::ua_stylesheet(), sheet, *t.doc,
        css::RenderState{}, viewport_w);
    t.root = layout::layout_with_pseudo(
        *t.doc, result.element_styles, result.pseudo_styles,
        &fixed_measurer, viewport_w, nullptr);
    return t;
}

const layout::LayoutBox* find_class(const layout::LayoutNode& node,
                                     const std::string& cls) {
    if (node.kind == layout::LayoutKind::Box) {
        const auto& b = static_cast<const layout::LayoutBox&>(node);
        if (b.element) {
            const std::string c = b.element->get_attribute("class");
            std::string::size_type pos = 0;
            while (pos < c.size()) {
                const auto end = c.find(' ', pos);
                const auto word = c.substr(pos,
                    end == std::string::npos ? c.size() - pos : end - pos);
                if (word == cls) return &b;
                if (end == std::string::npos) break;
                pos = end + 1;
            }
        }
        for (const auto& child : b.children) {
            if (auto r = find_class(*child, cls)) return r;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("pseudo: ::after { content: '' } synthesizes an empty box",
          "[layout][pseudo-el][adr-042]") {
    auto t = run(
        "<div class='p'></div>",
        "body,html{margin:0;padding:0}"
        ".p{width:200px;height:100px;position:relative;margin:0;padding:0}"
        ".p::after{content:'';display:block;position:absolute;"
        "bottom:0;left:0;right:0;height:1px;background-color:red}");
    const auto* p = find_class(t.root, "p");
    REQUIRE(p != nullptr);
    CHECK(p->children.size() == 1);
    const auto& pb = static_cast<const layout::LayoutBox&>(*p->children[0]);
    CHECK(pb.element == nullptr);
    CHECK(pb.position == layout::Position::Absolute);
    CHECK(pb.x == Catch::Approx(0.0f).margin(1.0f));
    CHECK(pb.y == Catch::Approx(99.0f).margin(1.0f));
    CHECK(pb.width == Catch::Approx(200.0f).margin(2.0f));
    CHECK(pb.height == Catch::Approx(1.0f).margin(1.0f));
}

TEST_CASE("pseudo: ::before { content: '' } inserted before children",
          "[layout][pseudo-el][adr-042]") {
    auto t = run(
        "<div class='p'><span class='c'>C</span></div>",
        "body,html{margin:0;padding:0}"
        ".p{width:100px;height:100px;position:relative;margin:0;padding:0}"
        ".p::before{content:'';position:absolute;top:0;left:0;"
        "width:10px;height:10px;background-color:green}");
    const auto* p = find_class(t.root, "p");
    REQUIRE(p != nullptr);
    REQUIRE(p->children.size() >= 1);
    const auto& first = static_cast<const layout::LayoutBox&>(*p->children[0]);
    CHECK(first.element == nullptr);
}

TEST_CASE("pseudo: no content property -> no box synthesized",
          "[layout][pseudo-el][adr-042]") {
    auto t = run(
        "<div class='p'></div>",
        "body,html{margin:0;padding:0}"
        ".p{width:100px;height:100px;position:relative;margin:0;padding:0}"
        ".p::after{display:block;position:absolute;top:0;left:0;"
        "width:10px;height:10px;background-color:red}");
    const auto* p = find_class(t.root, "p");
    REQUIRE(p != nullptr);
    CHECK(p->children.empty());
}

TEST_CASE("pseudo: literal text content 'abc' deferred (no box)",
          "[layout][pseudo-el][adr-042]") {
    auto t = run(
        "<div class='p'></div>",
        "body,html{margin:0;padding:0}"
        ".p{width:100px;height:100px;position:relative;margin:0;padding:0}"
        ".p::after{content:'abc';display:block;position:absolute;"
        "top:0;left:0;width:20px;height:20px;background-color:red}");
    const auto* p = find_class(t.root, "p");
    REQUIRE(p != nullptr);
    CHECK(p->children.empty());
}

TEST_CASE("pseudo: content: '' with no position -> static append",
          "[layout][pseudo-el][adr-042]") {
    auto t = run(
        "<div class='p'></div>",
        "body,html{margin:0;padding:0}"
        ".p{width:100px;height:100px;position:relative;margin:0;padding:0}"
        ".p::after{content:'';display:block;height:5px;"
        "background-color:blue}");
    const auto* p = find_class(t.root, "p");
    REQUIRE(p != nullptr);
    REQUIRE(p->children.size() == 1);
    const auto& pb = static_cast<const layout::LayoutBox&>(*p->children[0]);
    CHECK(pb.position == layout::Position::Static);
    CHECK(pb.height == Catch::Approx(5.0f).margin(1.0f));
}

TEST_CASE("pseudo: titlebar-like pattern (::after absolute bar)",
          "[layout][pseudo-el][adr-042]") {
    auto t = run(
        "<header class='titlebar'><h1>Title</h1></header>",
        "body,html{margin:0;padding:0}"
        ".titlebar{position:relative;"
        "width:400px;height:50px;margin:0;padding:0}"
        ".titlebar h1{margin:0;font-size:24px;line-height:1}"
        ".titlebar::after{content:'';display:block;position:absolute;"
        "bottom:0;left:0;right:0;height:1px;background-color:#a2a9b1}");
    const auto* tb = find_class(t.root, "titlebar");
    REQUIRE(tb != nullptr);
    const layout::LayoutBox* pseudo = nullptr;
    for (const auto& c : tb->children) {
        if (c->kind == layout::LayoutKind::Box) {
            const auto& cb = static_cast<const layout::LayoutBox&>(*c);
            if (cb.element == nullptr) { pseudo = &cb; break; }
        }
    }
    REQUIRE(pseudo != nullptr);
    CHECK(pseudo->position == layout::Position::Absolute);
    CHECK(pseudo->y == Catch::Approx(49.0f).margin(2.0f));
    CHECK(pseudo->width == Catch::Approx(400.0f).margin(2.0f));
    CHECK(pseudo->height == Catch::Approx(1.0f).margin(1.0f));
}

TEST_CASE("pseudo: no pseudo rules -> no extra boxes (regression)",
          "[layout][pseudo-el][adr-042]") {
    auto t = run(
        "<div class='p'></div>",
        "body,html{margin:0;padding:0}"
        ".p{width:100px;height:100px;margin:0;padding:0}");
    const auto* p = find_class(t.root, "p");
    REQUIRE(p != nullptr);
    CHECK(p->children.empty());
}

TEST_CASE("pseudo: ::before and ::after both synthesize",
          "[layout][pseudo-el][adr-042]") {
    auto t = run(
        "<div class='p'></div>",
        "body,html{margin:0;padding:0}"
        ".p{width:100px;height:100px;position:relative;margin:0;padding:0}"
        ".p::before{content:'';position:absolute;top:0;left:0;"
        "width:10px;height:10px;background-color:green}"
        ".p::after{content:'';position:absolute;bottom:0;right:0;"
        "width:10px;height:10px;background-color:red}");
    const auto* p = find_class(t.root, "p");
    REQUIRE(p != nullptr);
    CHECK(p->children.size() == 2);
}
