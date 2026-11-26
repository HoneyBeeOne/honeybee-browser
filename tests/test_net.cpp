#include <catch2/catch_test_macros.hpp>

#include "net/client.hpp"
#include "net/response.hpp"

// ملاحظة: هذه الاختبارات لا تتصل بالإنترنت إطلاقاً، لتعمل
// بثبات على أي بيئة، بما فيها خادم CI. تختبر فقط حالات
// الفشل (رابط خاطئ، خادم غير موجود).
//
// للتحقق من جلب محتوى فعلي، استخدم يدوياً:
//
//     honeybee::net::fetch("https://example.com")
//
// من برنامج تجريبي، أو أضف اختباراً تكاملياً لاحقاً في مهمة
// منفصلة.

TEST_CASE("fetch returns failed response for empty URL", "[net]") {
    const auto response = honeybee::net::fetch("");
    REQUIRE_FALSE(response.success());
    REQUIRE(response.status_code == 0);
    REQUIRE(response.body.empty());
}

TEST_CASE("fetch returns failed response for unsupported scheme",
          "[net]") {
    const auto response = honeybee::net::fetch("ftp://example.com");
    REQUIRE_FALSE(response.success());
    REQUIRE(response.status_code == 0);
}

TEST_CASE("fetch returns failed response for unreachable host",
          "[net]") {
    // منفذ وهمي على الجهاز المحلي. لن يستجيب أي خادم عليه.
    const auto response =
        honeybee::net::fetch("http://127.0.0.1:1/");
    REQUIRE_FALSE(response.success());
    REQUIRE(response.status_code == 0);
}

TEST_CASE("Response::success returns true only for 2xx", "[net]") {
    honeybee::net::Response ok;
    ok.status_code = 200;
    REQUIRE(ok.success());

    honeybee::net::Response created;
    created.status_code = 201;
    REQUIRE(created.success());

    honeybee::net::Response not_found;
    not_found.status_code = 404;
    REQUIRE_FALSE(not_found.success());

    honeybee::net::Response redirect;
    redirect.status_code = 301;
    REQUIRE_FALSE(redirect.success());

    honeybee::net::Response server_error;
    server_error.status_code = 500;
    REQUIRE_FALSE(server_error.success());

    honeybee::net::Response zero;
    zero.status_code = 0;
    REQUIRE_FALSE(zero.success());
}
