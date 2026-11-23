// =============================================================
// Tests for the Pango-backed text measurer (ADR-082).
// =============================================================

#include "browser/pango_text.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using honeybee::browser::measure_with_pango;

TEST_CASE("Pango text measurement returns dimensions and baseline",
          "[pango][adr-082]") {
    const auto metrics = measure_with_pango("Baseline", "Sans", 16.0f);

    REQUIRE(metrics.width > 0.0f);
    REQUIRE(metrics.height > 0.0f);
    REQUIRE(metrics.baseline > 0.0f);
    REQUIRE(metrics.baseline <= metrics.height);
}

TEST_CASE("Pango text measurement provides fallback metrics for empty text",
          "[pango][adr-082]") {
    constexpr float font_size = 20.0f;

    const auto metrics = measure_with_pango("", "Sans", font_size);

    REQUIRE(metrics.width == 0.0f);
    REQUIRE(metrics.height == Catch::Approx(font_size));
    REQUIRE(metrics.baseline == Catch::Approx(font_size * 0.8f));
}

TEST_CASE("Pango font measurement returns usable font metrics",
          "[pango][adr-082]") {
    const auto metrics =
        honeybee::browser::measure_font_metrics_with_pango("Sans", 16.0f);

    REQUIRE(metrics.ascent > 0.0f);
    REQUIRE(metrics.descent > 0.0f);
    REQUIRE(metrics.x_height > 0.0f);
    REQUIRE(metrics.normal_line_height > 0.0f);

    REQUIRE(metrics.ascent > metrics.x_height);
    REQUIRE(metrics.normal_line_height >= metrics.x_height);
}
