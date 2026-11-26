#include <catch2/catch_test_macros.hpp>

#include "renderer/css/cascade.hpp"
#include "renderer/css/stylesheet.hpp"
#include "renderer/layout/grid_layout.hpp"

using honeybee::renderer::layout::compute_column_widths;
using honeybee::renderer::layout::GridTemplate;
using honeybee::renderer::layout::LengthContext;
using honeybee::renderer::layout::parse_grid_areas;
using honeybee::renderer::layout::parse_grid_template;
using honeybee::renderer::layout::parse_track_list;
using honeybee::renderer::layout::TrackSize;

namespace {
LengthContext make_ctx() {
    LengthContext ctx;
    ctx.font_size = 16.0f;
    ctx.parent_font_size = 16.0f;
    ctx.root_font_size = 16.0f;
    ctx.parent_content_width = 1024.0f;
    return ctx;
}
} // namespace

// =============================================================
// ADR-028: track list parsing.
// =============================================================

TEST_CASE("parse_track_list: single fixed rem",
          "[layout][grid][adr-028]") {
    const auto tracks = parse_track_list("12.25rem", make_ctx());
    REQUIRE(tracks.size() == 1);
    CHECK(tracks[0].kind == TrackSize::Kind::Fixed);
    CHECK(tracks[0].value == 196.0f);  // 12.25 * 16
}

TEST_CASE("parse_track_list: single fixed px",
          "[layout][grid][adr-028]") {
    const auto tracks = parse_track_list("200px", make_ctx());
    REQUIRE(tracks.size() == 1);
    CHECK(tracks[0].kind == TrackSize::Kind::Fixed);
    CHECK(tracks[0].value == 200.0f);
}

TEST_CASE("parse_track_list: 1fr",
          "[layout][grid][adr-028]") {
    const auto tracks = parse_track_list("1fr", make_ctx());
    REQUIRE(tracks.size() == 1);
    CHECK(tracks[0].kind == TrackSize::Kind::Fr);
    CHECK(tracks[0].value == 1.0f);
}

TEST_CASE("parse_track_list: minmax(0, 1fr)",
          "[layout][grid][adr-028]") {
    const auto tracks = parse_track_list("minmax(0, 1fr)", make_ctx());
    REQUIRE(tracks.size() == 1);
    CHECK(tracks[0].kind == TrackSize::Kind::MinMaxFr);
    CHECK(tracks[0].value == 1.0f);
    CHECK(tracks[0].min_value == 0.0f);
}

TEST_CASE("parse_track_list: minmax(100px, 1fr)",
          "[layout][grid][adr-028]") {
    const auto tracks = parse_track_list("minmax(100px, 1fr)", make_ctx());
    REQUIRE(tracks.size() == 1);
    CHECK(tracks[0].kind == TrackSize::Kind::MinMaxFr);
    CHECK(tracks[0].min_value == 100.0f);
}

TEST_CASE("parse_track_list: minmax(100px, 300px)",
          "[layout][grid][adr-028]") {
    const auto tracks = parse_track_list("minmax(100px, 300px)", make_ctx());
    REQUIRE(tracks.size() == 1);
    CHECK(tracks[0].kind == TrackSize::Kind::MinMaxFixed);
    CHECK(tracks[0].min_value == 100.0f);
    CHECK(tracks[0].max_value == 300.0f);
}

TEST_CASE("parse_track_list: Wikipedia's actual column list",
          "[layout][grid][adr-028]") {
    // From .mw-page-container-inner { grid-template-columns:
    //   15.5rem minmax(0, 1fr) }
    const auto tracks =
        parse_track_list("15.5rem minmax(0, 1fr)", make_ctx());
    REQUIRE(tracks.size() == 2);
    CHECK(tracks[0].kind == TrackSize::Kind::Fixed);
    CHECK(tracks[0].value == 248.0f);  // 15.5 * 16
    CHECK(tracks[1].kind == TrackSize::Kind::MinMaxFr);
    CHECK(tracks[1].value == 1.0f);
}

TEST_CASE("parse_track_list: empty returns empty",
          "[layout][grid][adr-028]") {
    CHECK(parse_track_list("", make_ctx()).empty());
    CHECK(parse_track_list("   ", make_ctx()).empty());
}

// =============================================================
// ADR-028: grid-template-areas parsing.
// =============================================================

TEST_CASE("parse_grid_areas: single row single cell",
          "[layout][grid][adr-028]") {
    const auto areas = parse_grid_areas("'main'");
    REQUIRE(areas.size() == 1);
    CHECK(areas.at("main").row == 0);
    CHECK(areas.at("main").col == 0);
    CHECK(areas.at("main").row_span == 1);
    CHECK(areas.at("main").col_span == 1);
}

TEST_CASE("parse_grid_areas: 2x2 single-cell areas",
          "[layout][grid][adr-028]") {
    const auto areas = parse_grid_areas("'a b' 'c d'");
    REQUIRE(areas.size() == 4);
    CHECK(areas.at("a").row == 0);
    CHECK(areas.at("a").col == 0);
    CHECK(areas.at("b").row == 0);
    CHECK(areas.at("b").col == 1);
    CHECK(areas.at("c").row == 1);
    CHECK(areas.at("c").col == 0);
    CHECK(areas.at("d").row == 1);
    CHECK(areas.at("d").col == 1);
}

TEST_CASE("parse_grid_areas: Wikipedia's actual 3-row layout",
          "[layout][grid][adr-028]") {
    // From .mw-page-container-inner { grid-template-areas:
    //   'siteNotice siteNotice'
    //   'columnStart pageContent'
    //   'footer footer' }
    const auto areas = parse_grid_areas(
        "'siteNotice siteNotice' "
        "'columnStart pageContent' "
        "'footer footer'");
    REQUIRE(areas.size() == 4);

    CHECK(areas.at("siteNotice").row == 0);
    CHECK(areas.at("siteNotice").col == 0);
    CHECK(areas.at("siteNotice").col_span == 2);

    CHECK(areas.at("columnStart").row == 1);
    CHECK(areas.at("columnStart").col == 0);
    CHECK(areas.at("columnStart").col_span == 1);

    CHECK(areas.at("pageContent").row == 1);
    CHECK(areas.at("pageContent").col == 1);

    CHECK(areas.at("footer").row == 2);
    CHECK(areas.at("footer").col == 0);
    CHECK(areas.at("footer").col_span == 2);
}

TEST_CASE("parse_grid_areas: '.' is empty cell, not a name",
          "[layout][grid][adr-028]") {
    const auto areas = parse_grid_areas("'a .' '. b'");
    CHECK(areas.size() == 2);
    CHECK(areas.at("a").row == 0);
    CHECK(areas.at("a").col == 0);
    CHECK(areas.at("b").row == 1);
    CHECK(areas.at("b").col == 1);
}

TEST_CASE("parse_grid_areas: empty string returns empty",
          "[layout][grid][adr-028]") {
    CHECK(parse_grid_areas("").empty());
}

// =============================================================
// ADR-028: parse_grid_template from ComputedStyle.
// =============================================================

TEST_CASE("parse_grid_template: Wikipedia's actual style",
          "[layout][grid][adr-028]") {
    using honeybee::renderer::css::Stylesheet;
    auto sheet = Stylesheet::parse(
        ".g { display: grid; "
        "     grid-template-columns: 15.5rem minmax(0, 1fr); "
        "     grid-template-rows: min-content 1fr min-content; "
        "     grid-template-areas: 'siteNotice siteNotice' "
        "                          'columnStart pageContent' "
        "                          'footer footer' }");
    REQUIRE(sheet.rules.size() == 1);
    // Build a fake ComputedStyle for parsing.
    honeybee::renderer::css::ComputedStyle style;
    for (const auto& d : sheet.rules[0].declarations) {
        honeybee::renderer::css::CascadeValue v;
        v.value = d.value;
        style.declarations[d.property] = v;
    }
    const auto tmpl = parse_grid_template(style, make_ctx());
    REQUIRE(tmpl.has_value());
    CHECK(tmpl->columns.size() == 2);
    CHECK(tmpl->rows.size() == 3);
    CHECK(tmpl->areas.size() == 4);
    CHECK(tmpl->areas.at("siteNotice").col_span == 2);
}

TEST_CASE("parse_grid_template: no columns -> nullopt",
          "[layout][grid][adr-028]") {
    honeybee::renderer::css::ComputedStyle style;
    CHECK_FALSE(parse_grid_template(style, make_ctx()).has_value());
}

// =============================================================
// ADR-028: column width computation.
// =============================================================

TEST_CASE("compute_column_widths: fixed + fr in 800-wide container",
          "[layout][grid][adr-028]") {
    const auto tracks = parse_track_list("200px 1fr", make_ctx());
    const auto widths = compute_column_widths(tracks, 800.0f, 0.0f);
    REQUIRE(widths.size() == 2);
    CHECK(widths[0] == 200.0f);
    CHECK(widths[1] == 600.0f);
}

TEST_CASE("compute_column_widths: with 24px gap",
          "[layout][grid][adr-028]") {
    const auto tracks = parse_track_list("200px 1fr", make_ctx());
    const auto widths = compute_column_widths(tracks, 800.0f, 24.0f);
    REQUIRE(widths.size() == 2);
    CHECK(widths[0] == 200.0f);
    CHECK(widths[1] == 576.0f);  // 800 - 24 - 200
}

TEST_CASE("compute_column_widths: Wikipedia's actual template",
          "[layout][grid][adr-028]") {
    const auto tracks =
        parse_track_list("15.5rem minmax(0, 1fr)", make_ctx());
    const auto widths = compute_column_widths(tracks, 1024.0f, 24.0f);
    REQUIRE(widths.size() == 2);
    CHECK(widths[0] == 248.0f);  // 15.5rem * 16
    CHECK(widths[1] == 752.0f);  // 1024 - 24 - 248
}

TEST_CASE("compute_column_widths: minmax(100px, 1fr) with min floor",
          "[layout][grid][adr-028]") {
    // Available = 150. Fixed = 0. fr_sum = 1.
    // 1fr = 150 * 1 / 1 = 150 > min (100), so 150.
    const auto tracks = parse_track_list("minmax(100px, 1fr)", make_ctx());
    const auto widths = compute_column_widths(tracks, 150.0f, 0.0f);
    REQUIRE(widths.size() == 1);
    CHECK(widths[0] == 150.0f);
}

TEST_CASE("compute_column_widths: minmax min kicks in when fr under-flows",
          "[layout][grid][adr-028]") {
    // Available = 80. minmax(100px, 1fr): fr calc gives 80 < min 100,
    // so clamp to 100.
    const auto tracks = parse_track_list("minmax(100px, 1fr)", make_ctx());
    const auto widths = compute_column_widths(tracks, 80.0f, 0.0f);
    REQUIRE(widths.size() == 1);
    CHECK(widths[0] == 100.0f);
}

TEST_CASE("compute_column_widths: empty tracks",
          "[layout][grid][adr-028]") {
    CHECK(compute_column_widths({}, 800.0f, 0.0f).empty());
}

// =============================================================
// ADR-028: content-sizing keywords (min-content, max-content, auto).
// =============================================================

TEST_CASE("parse_track_list: min-content / max-content / auto",
          "[layout][grid][adr-028]") {
    const auto tracks =
        parse_track_list("min-content max-content auto", make_ctx());
    REQUIRE(tracks.size() == 3);
    CHECK(tracks[0].kind == TrackSize::Kind::Auto);
    CHECK(tracks[1].kind == TrackSize::Kind::Auto);
    CHECK(tracks[2].kind == TrackSize::Kind::Auto);
}

TEST_CASE("parse_track_list: Wikipedia's min-content 1fr min-content",
          "[layout][grid][adr-028]") {
    // From .mw-page-container-inner { grid-template-rows:
    //   min-content 1fr min-content }
    const auto tracks =
        parse_track_list("min-content 1fr min-content", make_ctx());
    REQUIRE(tracks.size() == 3);
    CHECK(tracks[0].kind == TrackSize::Kind::Auto);
    CHECK(tracks[1].kind == TrackSize::Kind::Fr);
    CHECK(tracks[1].value == 1.0f);
    CHECK(tracks[2].kind == TrackSize::Kind::Auto);
}
