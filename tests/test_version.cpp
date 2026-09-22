#include <catch2/catch_test_macros.hpp>

#include "base/version.hpp"

// اختبار أن دالة version_string تُرجع القيمة المتوقعة
TEST_CASE("version_string returns the expected value", "[version]") {
    REQUIRE(honeybee::version_string() == "0.1.0");
}

// اختبار أن مكونات الإصدار المنفصلة متسقة مع النص
TEST_CASE("version components match the version string", "[version]") {
    REQUIRE(honeybee::version_major() == 0);
    REQUIRE(honeybee::version_minor() == 1);
    REQUIRE(honeybee::version_patch() == 0);
}
