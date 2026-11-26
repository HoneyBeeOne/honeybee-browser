// ADR-049: text-align unit tests (pure helpers).
#include "renderer/layout/layout.hpp"

#include <catch2/catch_test_macros.hpp>

namespace layout = honeybee::renderer::layout;
using TA = layout::TextAlign;
using D  = layout::Direction;

TEST_CASE("ADR-049: offset math LTR", "[adr049]") {
    CHECK(layout::text_align_offset(TA::Left,   D::Ltr, 100.0f, 400.0f) == 0.0f);
    CHECK(layout::text_align_offset(TA::Right,  D::Ltr, 100.0f, 400.0f) == 300.0f);
    CHECK(layout::text_align_offset(TA::Center, D::Ltr, 100.0f, 400.0f) == 150.0f);
    CHECK(layout::text_align_offset(TA::Start,  D::Ltr, 100.0f, 400.0f) == 0.0f);
    CHECK(layout::text_align_offset(TA::End,    D::Ltr, 100.0f, 400.0f) == 300.0f);
}

TEST_CASE("ADR-049: offset math RTL", "[adr049]") {
    CHECK(layout::text_align_offset(TA::Start,  D::Rtl, 100.0f, 400.0f) == 300.0f);
    CHECK(layout::text_align_offset(TA::End,    D::Rtl, 100.0f, 400.0f) == 0.0f);
    CHECK(layout::text_align_offset(TA::Center, D::Rtl, 100.0f, 400.0f) == 150.0f);
}

TEST_CASE("ADR-049: edge cases clamp to zero", "[adr049]") {
    CHECK(layout::text_align_offset(TA::Center, D::Ltr, 400.0f, 400.0f) == 0.0f);
    CHECK(layout::text_align_offset(TA::Right,  D::Ltr, 500.0f, 400.0f) == 0.0f);
}

TEST_CASE("ADR-049: value parsing", "[adr049]") {
    CHECK(layout::resolve_text_align_value("left")    == TA::Left);
    CHECK(layout::resolve_text_align_value("center")  == TA::Center);
    CHECK(layout::resolve_text_align_value("right")   == TA::Right);
    CHECK(layout::resolve_text_align_value("start")   == TA::Start);
    CHECK(layout::resolve_text_align_value("end")     == TA::End);
    CHECK(layout::resolve_text_align_value("justify") == TA::Start);
    CHECK(layout::resolve_text_align_value("")        == TA::Start);
    CHECK(layout::resolve_text_align_value("garbage") == TA::Start);
}
