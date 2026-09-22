#include <catch2/catch_test_macros.hpp>

#include "base/version.hpp"

// اختبار حارس يثبّت أن دالة version_string تُرجع قيمة الإصدار الحالي وليست فارغة
TEST_CASE("version_string returns the expected value", "[version]") {
    REQUIRE_FALSE(honeybee::version_string().empty());
    REQUIRE(honeybee::version_string() == "0.1.0");
}

// اختبار تماسك يضمن أن مكونات الإصدار المنفصلة متسقة مع النص
TEST_CASE("version components match the version string", "[version]") {
    REQUIRE(honeybee::version_major() == 0);
    REQUIRE(honeybee::version_minor() == 1);
    REQUIRE(honeybee::version_patch() == 0);

    const std::string expected = std::to_string(honeybee::version_major()) + "." +
                                 std::to_string(honeybee::version_minor()) + "." +
                                 std::to_string(honeybee::version_patch());
    REQUIRE(honeybee::version_string() == expected);
}
