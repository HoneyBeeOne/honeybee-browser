#include <catch2/catch_test_macros.hpp>

#include "browser/page_cache.hpp"

using honeybee::browser::PageCache;

TEST_CASE("PageCache: get on empty returns nullopt",
          "[page-cache]") {
    PageCache c;
    CHECK(c.size() == 0);
    CHECK(c.bytes() == 0);
    CHECK_FALSE(c.contains("https://a.example/"));
    CHECK_FALSE(c.get("https://a.example/").has_value());
}

TEST_CASE("PageCache: put then get returns the stored body",
          "[page-cache]") {
    PageCache c;
    c.put("https://a.example/", "<html>page A</html>");
    CHECK(c.size() == 1);
    CHECK(c.contains("https://a.example/"));
    auto body = c.get("https://a.example/");
    REQUIRE(body.has_value());
    CHECK(*body == "<html>page A</html>");
}

TEST_CASE("PageCache: get unknown URL on non-empty cache returns nullopt",
          "[page-cache]") {
    PageCache c;
    c.put("https://a.example/", "A");
    CHECK_FALSE(c.get("https://b.example/").has_value());
    CHECK_FALSE(c.contains("https://b.example/"));
}

TEST_CASE("PageCache: put overwrites existing entry",
          "[page-cache]") {
    PageCache c;
    c.put("https://a.example/", "old body");
    c.put("https://a.example/", "new body");
    CHECK(c.size() == 1);
    auto body = c.get("https://a.example/");
    REQUIRE(body.has_value());
    CHECK(*body == "new body");
}

TEST_CASE("PageCache: size counts unique URLs",
          "[page-cache]") {
    PageCache c;
    c.put("A", "x");
    c.put("B", "y");
    c.put("A", "z");  // overwrite, doesn't grow
    CHECK(c.size() == 2);
}

TEST_CASE("PageCache: bytes sums HTML body lengths",
          "[page-cache]") {
    PageCache c;
    c.put("A", "hello");        // 5
    c.put("B", "world!!");      // 7
    CHECK(c.bytes() == 12);

    // Overwrite adjusts bytes (old length subtracted)
    c.put("A", "hi");           // 2
    CHECK(c.bytes() == 9);      // 2 + 7
}

TEST_CASE("PageCache: clear resets everything",
          "[page-cache]") {
    PageCache c;
    c.put("A", "hello");
    c.put("B", "world!!");
    c.clear();
    CHECK(c.size() == 0);
    CHECK(c.bytes() == 0);
    CHECK_FALSE(c.contains("A"));
    CHECK_FALSE(c.contains("B"));
    CHECK_FALSE(c.get("A").has_value());
}

TEST_CASE("PageCache: empty body is a valid cache entry",
          "[page-cache]") {
    PageCache c;
    c.put("A", "");
    CHECK(c.size() == 1);
    CHECK(c.contains("A"));
    auto body = c.get("A");
    REQUIRE(body.has_value());
    CHECK(body->empty());
    CHECK(c.bytes() == 0);
}

// =============================================================
// ADR-025: PageCache extension for external CSS.
// =============================================================

TEST_CASE("PageCache: get_entry returns html + external_css",
          "[page-cache]") {
    using honeybee::browser::CacheEntry;
    honeybee::browser::PageCache c;
    c.put("https://a.example/", "<p>A</p>", ".x { color: red }");
    auto e = c.get_entry("https://a.example/");
    REQUIRE(e.has_value());
    CHECK(e->html == "<p>A</p>");
    CHECK(e->external_css == ".x { color: red }");
}

TEST_CASE("PageCache: get_entry on missing URL returns nullopt",
          "[page-cache]") {
    honeybee::browser::PageCache c;
    CHECK_FALSE(c.get_entry("https://missing.example/").has_value());
}

TEST_CASE("PageCache: compat put(url, html) yields empty external_css",
          "[page-cache]") {
    honeybee::browser::PageCache c;
    c.put("A", "hello");
    auto e = c.get_entry("A");
    REQUIRE(e.has_value());
    CHECK(e->html == "hello");
    CHECK(e->external_css.empty());
}

TEST_CASE("PageCache: bytes() sums html + external_css",
          "[page-cache]") {
    honeybee::browser::PageCache c;
    c.put("A", "hello", "world");     // 5 + 5 = 10
    c.put("B", "abc", "defghij");     // 3 + 7 = 10
    CHECK(c.bytes() == 20);
    c.put("A", "hi", "x");            // 2 + 1 = 3, replaces 10
    CHECK(c.bytes() == 13);
}

TEST_CASE("PageCache: overwrite replaces external_css",
          "[page-cache]") {
    honeybee::browser::PageCache c;
    c.put("A", "old html", "old css");
    c.put("A", "new html", "new css");
    auto e = c.get_entry("A");
    REQUIRE(e.has_value());
    CHECK(e->html == "new html");
    CHECK(e->external_css == "new css");
}
