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
