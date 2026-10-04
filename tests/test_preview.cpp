// =============================================================
// Tests for make_preview().
// =============================================================

#include <catch2/catch_test_macros.hpp>

#include <string>

#include "renderer/html/preview.hpp"

using honeybee::renderer::html::make_preview;

TEST_CASE("make_preview extracts title and text",
          "[renderer][html][preview]") {
    const auto p = make_preview(
        "<html><head><title>Hi</title></head><body><p>Hello</p></body></html>");
    REQUIRE(p.title == "Hi");
    REQUIRE(p.text.find("Hello") != std::string::npos);
}

TEST_CASE("make_preview with no title leaves title empty",
          "[renderer][html][preview]") {
    const auto p = make_preview("<html><body><p>Hi</p></body></html>");
    REQUIRE(p.title.empty());
    REQUIRE(p.text.find("Hi") != std::string::npos);
}

TEST_CASE("make_preview truncates text longer than max_text",
          "[renderer][html][preview]") {
    std::string html = "<html><body><p>";
    html += std::string(100, 'x');
    html += "</p></body></html>";
    const auto p = make_preview(html, 50);
    REQUIRE(p.text.size() == 53);
    REQUIRE(p.text.substr(0, 50) == std::string(50, 'x'));
    REQUIRE(p.text.substr(50) == "...");
}

TEST_CASE("make_preview honors custom max_text",
          "[renderer][html][preview]") {
    const auto p = make_preview(
        "<html><body><p>0123456789ABCDEF</p></body></html>", 10);
    REQUIRE(p.text.size() == 13);
    REQUIRE(p.text.substr(0, 10) == "0123456789");
    REQUIRE(p.text.substr(10) == "...");
}

TEST_CASE("make_preview handles malformed HTML",
          "[renderer][html][preview]") {
    const auto p = make_preview("<html><body><p>Unclosed");
    REQUIRE(p.text.find("Unclosed") != std::string::npos);
}

TEST_CASE("make_preview handles empty input",
          "[renderer][html][preview]") {
    const auto p = make_preview("");
    REQUIRE(p.title.empty());
    REQUIRE(p.text.empty());
}

TEST_CASE("make_preview does not append ellipsis at or below max_text",
          "[renderer][html][preview]") {
    const auto p = make_preview(
        "<html><body><p>short</p></body></html>", 100);
    REQUIRE(p.text.find("short") != std::string::npos);
    REQUIRE(p.text.find("...") == std::string::npos);
}
