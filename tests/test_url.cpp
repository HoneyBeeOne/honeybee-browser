#include <catch2/catch_test_macros.hpp>

#include "base/url.hpp"

// اختبار الروابط الصحيحة.
TEST_CASE("is_valid_url accepts valid URLs", "[url]") {
    REQUIRE(honeybee::is_valid_url("https://example.com"));
    REQUIRE(honeybee::is_valid_url("http://example.com"));
    REQUIRE(honeybee::is_valid_url("https://example.com/path"));
    REQUIRE(honeybee::is_valid_url("https://example.com/path?q=1"));
    REQUIRE(honeybee::is_valid_url("http://localhost:8080"));
    REQUIRE(honeybee::is_valid_url("file:///home/user/file.txt"));
}

// اختبار الروابط الخاطئة.
TEST_CASE("is_valid_url rejects invalid URLs", "[url]") {
    // نص فارغ.
    REQUIRE_FALSE(honeybee::is_valid_url(""));

    // بلا مخطط.
    REQUIRE_FALSE(honeybee::is_valid_url("example.com"));

    // مخطط غير مدعوم.
    REQUIRE_FALSE(honeybee::is_valid_url("ftp://example.com"));

    // مخطط بلا مضيف.
    REQUIRE_FALSE(honeybee::is_valid_url("https://"));
    REQUIRE_FALSE(honeybee::is_valid_url("http://"));

    // مسافة داخل الرابط.
    REQUIRE_FALSE(honeybee::is_valid_url("https://exa mple.com"));

    // شرطة مائلة مباشرة بعد المخطط (في http/https).
    REQUIRE_FALSE(honeybee::is_valid_url("https:///path"));
}

TEST_CASE("resolve_url passes absolute URLs through", "[url][resolve]") {
    CHECK(honeybee::resolve_url("https://example.com/x",
                                "https://other.com/y")
          == "https://example.com/x");
    CHECK(honeybee::resolve_url("http://a.com", "https://b.com")
          == "http://a.com");
    CHECK(honeybee::resolve_url("file:///home/x", "https://b.com")
          == "file:///home/x");
}

TEST_CASE("resolve_url resolves root-relative", "[url][resolve]") {
    CHECK(honeybee::resolve_url("/about",
                                "https://example.com/page.html")
          == "https://example.com/about");
}

TEST_CASE("resolve_url resolves sibling-relative", "[url][resolve]") {
    CHECK(honeybee::resolve_url("other.html",
                                "https://example.com/page.html")
          == "https://example.com/other.html");
}

TEST_CASE("resolve_url returns href when base is empty",
          "[url][resolve]") {
    CHECK(honeybee::resolve_url("other.html", "") == "other.html");
}

// =============================================================
// ADR-028 regression: root-relative URLs against a base with a
// subdirectory. Every previous test used bases without
// subdirectories, which masked this bug for ADR-018 through
// ADR-027.
// =============================================================

TEST_CASE("resolve_url: root-relative against subdirectory base",
          "[url][resolve]") {
    // Base has subdirectory /wiki/. Root-relative /w/load.php
    // must resolve to origin + href, NOT /wiki/ + href.
    CHECK(honeybee::resolve_url(
              "/w/load.php?lang=en",
              "https://en.wikipedia.org/wiki/HTML")
          == "https://en.wikipedia.org/w/load.php?lang=en");
}

TEST_CASE("resolve_url: root-relative against multi-level subdirectory base",
          "[url][resolve]") {
    CHECK(honeybee::resolve_url(
              "/asset.css",
              "https://example.com/a/b/c/page.html")
          == "https://example.com/asset.css");
}

TEST_CASE("resolve_url: root-relative against host-only base",
          "[url][resolve]") {
    CHECK(honeybee::resolve_url(
              "/path", "https://example.com")
          == "https://example.com/path");
}

TEST_CASE("resolve_url: root-relative preserves port",
          "[url][resolve]") {
    CHECK(honeybee::resolve_url(
              "/api", "http://localhost:8080/page.html")
          == "http://localhost:8080/api");
}


// =============================================================
// Protocol-relative URL resolution.
// =============================================================

TEST_CASE("resolve_url: protocol-relative inherits https scheme",
          "[url][protocol-relative]") {
    CHECK(honeybee::resolve_url("//example.com/x", "https://wiki.org/y") ==
          "https://example.com/x");
}

TEST_CASE("resolve_url: protocol-relative inherits http scheme",
          "[url][protocol-relative]") {
    CHECK(honeybee::resolve_url("//example.com/x", "http://wiki.org/y") ==
          "http://example.com/x");
}

TEST_CASE("resolve_url: protocol-relative with schemeless base",
          "[url][protocol-relative]") {
    CHECK(honeybee::resolve_url("//example.com/x", "wiki.org/y") ==
          "https://example.com/x");
}

TEST_CASE("resolve_url: absolute URL unaffected by protocol-relative fix",
          "[url][protocol-relative]") {
    CHECK(honeybee::resolve_url("https://already.com/x", "https://wiki.org/y") ==
          "https://already.com/x");
}
