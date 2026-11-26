#include "renderer/dom/node.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace honeybee::renderer::dom;

TEST_CASE("Empty document has no title and no text", "[dom]") {
    Document doc;
    CHECK(doc.title().empty());
    CHECK(doc.text_content().empty());
    CHECK(doc.children().empty());
}

TEST_CASE("Element holds tag and attributes", "[dom]") {
    Element el("a");
    CHECK(el.tag() == "a");
    CHECK(el.attributes().empty());
    CHECK(el.get_attribute("href").empty());

    el.set_attribute("href", "https://example.com");
    el.set_attribute("class", "link");

    CHECK(el.get_attribute("href") == "https://example.com");
    CHECK(el.get_attribute("class") == "link");
    CHECK(el.get_attribute("missing").empty());
    CHECK(el.attributes().size() == 2);
}

TEST_CASE("Element overrides attribute value on re-set", "[dom]") {
    Element el("div");
    el.set_attribute("id", "first");
    el.set_attribute("id", "second");
    CHECK(el.get_attribute("id") == "second");
    CHECK(el.attributes().size() == 1);
}

TEST_CASE("append_child sets parent back-pointer", "[dom]") {
    auto parent = std::make_unique<Element>("div");
    auto child = std::make_unique<TextNode>("hello");
    Node* child_raw = child.get();

    parent->append_child(std::move(child));

    CHECK(child_raw->parent() == parent.get());
    CHECK(parent->children().size() == 1);
}

TEST_CASE("text_content concatenates descendant text nodes", "[dom]") {
    auto doc = std::make_unique<Document>();
    auto html = std::make_unique<Element>("html");
    auto body = std::make_unique<Element>("body");
    auto p1 = std::make_unique<Element>("p");
    auto p2 = std::make_unique<Element>("p");

    p1->append_child(std::make_unique<TextNode>("hello "));
    p2->append_child(std::make_unique<TextNode>("world"));

    body->append_child(std::move(p1));
    body->append_child(std::move(p2));
    html->append_child(std::move(body));
    doc->append_child(std::move(html));

    CHECK(doc->text_content() == "hello world");
}

TEST_CASE("title finds first <title> anywhere in tree", "[dom]") {
    auto doc = std::make_unique<Document>();
    auto html = std::make_unique<Element>("html");
    auto head = std::make_unique<Element>("head");
    auto title = std::make_unique<Element>("title");
    auto body = std::make_unique<Element>("body");

    title->append_child(std::make_unique<TextNode>("My Page"));
    body->append_child(std::make_unique<TextNode>("body text"));

    head->append_child(std::move(title));
    html->append_child(std::move(head));
    html->append_child(std::move(body));
    doc->append_child(std::move(html));

    CHECK(doc->title() == "My Page");
    CHECK(doc->text_content() == "My Pagebody text");
}

TEST_CASE("title returns empty when no <title> present", "[dom]") {
    auto doc = std::make_unique<Document>();
    auto body = std::make_unique<Element>("body");
    body->append_child(std::make_unique<TextNode>("no title here"));
    doc->append_child(std::move(body));

    CHECK(doc->title().empty());
    CHECK(doc->text_content() == "no title here");
}

TEST_CASE("Empty <title> returns empty string", "[dom]") {
    auto doc = std::make_unique<Document>();
    auto html = std::make_unique<Element>("html");
    auto title = std::make_unique<Element>("title");
    html->append_child(std::move(title));
    doc->append_child(std::move(html));

    CHECK(doc->title().empty());
}

TEST_CASE("Deeply nested tree computes text_content", "[dom]") {
    auto doc = std::make_unique<Document>();
    auto a = std::make_unique<Element>("a");
    auto b = std::make_unique<Element>("b");
    auto c = std::make_unique<Element>("c");
    auto d = std::make_unique<Element>("d");

    d->append_child(std::make_unique<TextNode>("deep"));
    c->append_child(std::move(d));
    b->append_child(std::move(c));
    a->append_child(std::move(b));
    doc->append_child(std::move(a));

    CHECK(doc->text_content() == "deep");
}
