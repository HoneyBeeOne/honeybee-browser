// =============================================================
// Tests for the lexbor -> DOM converter (html::parse).
// =============================================================

#include <catch2/catch_test_macros.hpp>

#include <string>

#include "renderer/dom/node.hpp"
#include "renderer/html/convert.hpp"

using honeybee::renderer::html::parse;

TEST_CASE("parse extracts title", "[renderer][html][convert]") {
    auto doc = parse(
        "<html><head><title>Hi</title></head><body><p>Hello</p></body></html>");
    REQUIRE(doc != nullptr);
    REQUIRE(doc->title() == "Hi");
    const auto* body = doc->find_element("body");
    REQUIRE(body != nullptr);
    REQUIRE(body->text_content().find("Hello") != std::string::npos);
}

TEST_CASE("parse with missing title returns empty",
          "[renderer][html][convert]") {
    auto doc = parse("<html><body><p>Hi</p></body></html>");
    REQUIRE(doc != nullptr);
    REQUIRE(doc->title().empty());
    const auto* body = doc->find_element("body");
    REQUIRE(body != nullptr);
    REQUIRE(body->text_content().find("Hi") != std::string::npos);
}

TEST_CASE("parse handles malformed HTML", "[renderer][html][convert]") {
    auto doc = parse("<html><body><p>Unclosed");
    REQUIRE(doc != nullptr);
    const auto* body = doc->find_element("body");
    REQUIRE(body != nullptr);
    REQUIRE(body->text_content().find("Unclosed") != std::string::npos);
}

TEST_CASE("parse handles empty input", "[renderer][html][convert]") {
    auto doc = parse("");
    REQUIRE(doc != nullptr);
    REQUIRE(doc->title().empty());
}

TEST_CASE("parse preserves UTF-8 content", "[renderer][html][convert]") {
    auto doc = parse(
        "<html><body><p>\xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7</p></body></html>");
    REQUIRE(doc != nullptr);
    const auto* body = doc->find_element("body");
    REQUIRE(body != nullptr);
    REQUIRE(body->text_content().find(
        "\xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7") != std::string::npos);
}

TEST_CASE("parse preserves attributes", "[renderer][html][convert]") {
    auto doc = parse(
        R"(<html><body><a href="https://example.com" class="link">text</a></body></html>)");
    REQUIRE(doc != nullptr);
    const auto* a = doc->find_element("a");
    REQUIRE(a != nullptr);
    REQUIRE(a->tag() == "a");
    REQUIRE(a->get_attribute("href") == "https://example.com");
    REQUIRE(a->get_attribute("class") == "link");
    REQUIRE(a->get_attribute("missing").empty());
    REQUIRE(a->text_content() == "text");
}

TEST_CASE("parse preserves element hierarchy",
          "[renderer][html][convert]") {
    auto doc = parse(
        "<html><body><div><p>A</p><p>B</p></div></body></html>");
    REQUIRE(doc != nullptr);
    const auto* div = doc->find_element("div");
    REQUIRE(div != nullptr);
    REQUIRE(div->children().size() == 2);
    REQUIRE(div->text_content() == "AB");
}

TEST_CASE("parse builds fully-owned tree (lexbor destroyed)",
          "[renderer][html][convert]") {
    // After parse() returns, the lexbor document has been destroyed.
    // If our DOM held any lexbor pointers, these reads would be UB.
    // The test passing is itself the proof.
    auto doc = parse(
        "<html><head><title>Owned</title></head>"
        "<body><p>text survives</p></body></html>");
    REQUIRE(doc != nullptr);
    REQUIRE(doc->title() == "Owned");
    const auto* body = doc->find_element("body");
    REQUIRE(body != nullptr);
    REQUIRE(body->text_content() == "text survives");
}
