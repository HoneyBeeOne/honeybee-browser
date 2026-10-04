// =============================================================
// Tests for the Document wrapper around lexbor.
// =============================================================

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>

#include "renderer/html/document.hpp"

using honeybee::renderer::html::Document;

TEST_CASE("Document::parse extracts title", "[renderer][html][document]") {
    auto doc = Document::parse(
        "<html><head><title>Hi</title></head><body><p>Hello</p></body></html>");
    REQUIRE(doc != nullptr);
    REQUIRE(doc->title() == "Hi");
    REQUIRE(doc->text_content().find("Hello") != std::string::npos);
}

TEST_CASE("Document::parse with missing title returns empty",
          "[renderer][html][document]") {
    auto doc = Document::parse("<html><body><p>Hi</p></body></html>");
    REQUIRE(doc != nullptr);
    REQUIRE(doc->title().empty());
    REQUIRE(doc->text_content().find("Hi") != std::string::npos);
}

TEST_CASE("Document::parse handles malformed HTML",
          "[renderer][html][document]") {
    auto doc = Document::parse("<html><body><p>Unclosed");
    REQUIRE(doc != nullptr);
    REQUIRE(doc->text_content().find("Unclosed") != std::string::npos);
}

TEST_CASE("Document::parse handles empty input",
          "[renderer][html][document]") {
    auto doc = Document::parse("");
    REQUIRE(doc != nullptr);
    REQUIRE(doc->title().empty());
    REQUIRE(doc->text_content().empty());
}

TEST_CASE("Document::parse preserves UTF-8 content",
          "[renderer][html][document]") {
    auto doc = Document::parse(
        "<html><body><p>\xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7</p></body></html>");
    REQUIRE(doc != nullptr);
    REQUIRE(doc->text_content().find("\xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7") !=
            std::string::npos);
}

TEST_CASE("Document is move-only", "[renderer][html][document]") {
    auto a = Document::parse("<html><body><p>A</p></body></html>");
    REQUIRE(a != nullptr);

    auto b = std::move(a);
    REQUIRE(a == nullptr);
    REQUIRE(b != nullptr);
    REQUIRE(b->text_content().find("A") != std::string::npos);
}

TEST_CASE("Document::text_content collects nested text in document order",
          "[renderer][html][document]") {
    auto doc = Document::parse(
        "<html><body><div><p>A</p><p>B</p></div></body></html>");
    REQUIRE(doc != nullptr);
    const std::string text = doc->text_content();
    const auto pos_a = text.find("A");
    const auto pos_b = text.find("B");
    REQUIRE(pos_a != std::string::npos);
    REQUIRE(pos_b != std::string::npos);
    REQUIRE(pos_a < pos_b);
}
