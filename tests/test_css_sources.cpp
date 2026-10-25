#include <catch2/catch_test_macros.hpp>

#include "renderer/html/css_sources.hpp"
#include "renderer/html/convert.hpp"

using honeybee::renderer::html::collect_css_sources;
using honeybee::renderer::html::CssSource;

namespace {

std::unique_ptr<honeybee::renderer::dom::Document> parse(const char* h) {
    return honeybee::renderer::html::parse(h);
}

} // namespace

TEST_CASE("collect_css_sources: empty document", "[css-sources]") {
    auto doc = parse("<html><body></body></html>");
    REQUIRE(doc);
    CHECK(collect_css_sources(*doc).empty());
}

TEST_CASE("collect_css_sources: <style> only", "[css-sources]") {
    auto doc = parse(
        "<html><head><style>body{color:red}</style></head>"
        "<body></body></html>");
    REQUIRE(doc);
    auto s = collect_css_sources(*doc);
    REQUIRE(s.size() == 1);
    CHECK(s[0].inline_style == true);
    CHECK(s[0].content == "body{color:red}");
}

TEST_CASE("collect_css_sources: <link rel=stylesheet> only",
          "[css-sources]") {
    auto doc = parse(
        "<html><head>"
        "<link rel=\"stylesheet\" href=\"/style.css\">"
        "</head><body></body></html>");
    REQUIRE(doc);
    auto s = collect_css_sources(*doc);
    REQUIRE(s.size() == 1);
    CHECK(s[0].inline_style == false);
    CHECK(s[0].content == "/style.css");
}

TEST_CASE("collect_css_sources: preserves document order across kinds",
          "[css-sources]") {
    auto doc = parse(
        "<html><head>"
        "<link rel=\"stylesheet\" href=\"/first.css\">"
        "<style>body{color:red}</style>"
        "<link rel=\"stylesheet\" href=\"/second.css\">"
        "</head><body></body></html>");
    REQUIRE(doc);
    auto s = collect_css_sources(*doc);
    REQUIRE(s.size() == 3);
    CHECK(s[0].content == "/first.css");
    CHECK(s[1].content == "body{color:red}");
    CHECK(s[2].content == "/second.css");
    CHECK(s[0].inline_style == false);
    CHECK(s[1].inline_style == true);
    CHECK(s[2].inline_style == false);
}

TEST_CASE("collect_css_sources: alternate stylesheet is skipped",
          "[css-sources]") {
    auto doc = parse(
        "<html><head>"
        "<link rel=\"alternate stylesheet\" href=\"/alt.css\">"
        "<link rel=\"stylesheet\" href=\"/normal.css\">"
        "</head><body></body></html>");
    REQUIRE(doc);
    auto s = collect_css_sources(*doc);
    REQUIRE(s.size() == 1);
    CHECK(s[0].content == "/normal.css");
}

TEST_CASE("collect_css_sources: link without stylesheet token is skipped",
          "[css-sources]") {
    auto doc = parse(
        "<html><head>"
        "<link rel=\"preload\" href=\"/x.css\">"
        "<link rel=\"icon\" href=\"/favicon.ico\">"
        "</head><body></body></html>");
    REQUIRE(doc);
    CHECK(collect_css_sources(*doc).empty());
}

TEST_CASE("collect_css_sources: multi-token rel is parsed per spec",
          "[css-sources]") {
    auto doc = parse(
        "<html><head>"
        "<link rel=\"stylesheet nofollow\" href=\"/a.css\">"
        "<link rel=\"noopener stylesheet\" href=\"/b.css\">"
        "</head><body></body></html>");
    REQUIRE(doc);
    auto s = collect_css_sources(*doc);
    REQUIRE(s.size() == 2);
    CHECK(s[0].content == "/a.css");
    CHECK(s[1].content == "/b.css");
}

TEST_CASE("collect_css_sources: case-insensitive rel",
          "[css-sources]") {
    auto doc = parse(
        "<html><head>"
        "<link rel=\"Stylesheet\" href=\"/a.css\">"
        "<link rel=\"STYLESHEET\" href=\"/b.css\">"
        "</head><body></body></html>");
    REQUIRE(doc);
    auto s = collect_css_sources(*doc);
    REQUIRE(s.size() == 2);
}

TEST_CASE("collect_css_sources: decodes HTML entities in href",
          "[css-sources]") {
    // The ADR's flagged gotcha: Wikipedia's load.php URLs contain
    // &amp; in the source. Whether lexbor surfaces raw or decoded,
    // the returned href must be usable for a fetch (& not &amp;).
    auto doc = parse(
        "<html><head>"
        "<link rel=\"stylesheet\" "
        "href=\"/load.php?a=1&amp;b=2&amp;c=3\">"
        "</head><body></body></html>");
    REQUIRE(doc);
    auto s = collect_css_sources(*doc);
    REQUIRE(s.size() == 1);
    CHECK(s[0].content.find("&amp;") == std::string::npos);
    CHECK(s[0].content == "/load.php?a=1&b=2&c=3");
}

TEST_CASE("collect_css_sources: empty href is skipped",
          "[css-sources]") {
    auto doc = parse(
        "<html><head>"
        "<link rel=\"stylesheet\" href=\"\">"
        "</head><body></body></html>");
    REQUIRE(doc);
    CHECK(collect_css_sources(*doc).empty());
}
