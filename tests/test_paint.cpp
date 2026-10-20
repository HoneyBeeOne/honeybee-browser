#include "renderer/layout/layout_box.hpp"
#include "renderer/paint/paint.hpp"

#include <cairo.h>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>

namespace layout = honeybee::renderer::layout;
namespace paint = honeybee::renderer::paint;

namespace {

struct Canvas {
    cairo_surface_t* surface;
    cairo_t* cr;

    Canvas(int w, int h) {
        surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
        cr = cairo_create(surface);
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_paint(cr);
    }
    ~Canvas() {
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
    }
};

struct Pixel {
    uint8_t a, r, g, b;
};

Pixel pixel_at(cairo_surface_t* surface, int x, int y) {
    cairo_surface_flush(surface);
    auto* data = reinterpret_cast<uint32_t*>(
        cairo_image_surface_get_data(surface));
    const int stride = cairo_image_surface_get_stride(surface);
    const uint32_t px = data[y * (stride / 4) + x];
    return {
        static_cast<uint8_t>((px >> 24) & 0xff),
        static_cast<uint8_t>((px >> 16) & 0xff),
        static_cast<uint8_t>((px >> 8) & 0xff),
        static_cast<uint8_t>(px & 0xff),
    };
}

bool is_red(const Pixel& p) {
    return p.r > 200 && p.g < 50 && p.b < 50 && p.a > 200;
}
bool is_white(const Pixel& p) {
    return p.r > 200 && p.g > 200 && p.b > 200;
}
bool is_blue(const Pixel& p) {
    return p.b > 200 && p.r < 50 && p.g < 50;
}

void fake_drawer(cairo_t* cr, const layout::LayoutText& t) {
    cairo_set_source_rgb(cr, 0.0, 0.0, 1.0);
    cairo_rectangle(cr, t.x, t.y, t.width, t.height);
    cairo_fill(cr);
}

} // namespace

TEST_CASE("paint: canvas baseline is white", "[paint]") {
    Canvas c(100, 100);
    CHECK(is_white(pixel_at(c.surface, 50, 50)));
}

TEST_CASE("paint: background fills border-box", "[paint]") {
    Canvas c(200, 100);

    layout::LayoutBox box;
    box.x = 10.0f;
    box.y = 10.0f;
    box.width = 100.0f;
    box.height = 50.0f;
    box.background = {1.0f, 0.0f, 0.0f, 1.0f};

    paint::paint(c.cr, box, fake_drawer);
    CHECK(is_red(pixel_at(c.surface, 50, 30)));
    CHECK(is_white(pixel_at(c.surface, 5, 5)));
}

TEST_CASE("paint: transparent background draws nothing",
          "[paint]") {
    Canvas c(200, 100);

    layout::LayoutBox box;
    box.x = 10.0f;
    box.y = 10.0f;
    box.width = 100.0f;
    box.height = 50.0f;

    paint::paint(c.cr, box, fake_drawer);
    CHECK(is_white(pixel_at(c.surface, 50, 30)));
}

TEST_CASE("paint: border draws four strips", "[paint]") {
    Canvas c(200, 200);

    layout::LayoutBox box;
    box.x = 0.0f;
    box.y = 0.0f;
    box.width = 100.0f;
    box.height = 100.0f;
    box.border[0] = 4.0f;
    box.border[1] = 4.0f;
    box.border[2] = 4.0f;
    box.border[3] = 4.0f;
    box.border_color = {1.0f, 0.0f, 0.0f, 1.0f};

    paint::paint(c.cr, box, fake_drawer);

    CHECK(is_red(pixel_at(c.surface, 50, 1)));
    CHECK(is_red(pixel_at(c.surface, 98, 50)));
    CHECK(is_red(pixel_at(c.surface, 50, 98)));
    CHECK(is_red(pixel_at(c.surface, 1, 50)));
    CHECK(is_white(pixel_at(c.surface, 50, 50)));
}

TEST_CASE("paint: text drawer called at text position",
          "[paint]") {
    Canvas c(200, 100);

    layout::LayoutBox root;
    root.width = 200.0f;
    root.height = 100.0f;

    auto lt = std::make_unique<layout::LayoutText>();
    lt->x = 20.0f;
    lt->y = 20.0f;
    lt->width = 40.0f;
    lt->height = 16.0f;
    lt->text = "Hi";
    root.children.push_back(std::move(lt));

    paint::paint(c.cr, root, fake_drawer);
    CHECK(is_blue(pixel_at(c.surface, 30, 25)));
    CHECK(is_white(pixel_at(c.surface, 5, 5)));
}

TEST_CASE("paint: null drawer is a no-op", "[paint]") {
    Canvas c(100, 100);
    layout::LayoutBox box;
    box.width = 100.0f;
    box.height = 100.0f;
    box.background = {1.0f, 0.0f, 0.0f, 1.0f};

    paint::paint(c.cr, box, nullptr);
    CHECK(is_white(pixel_at(c.surface, 50, 50)));
}

TEST_CASE("paint: null cr is a no-op", "[paint]") {
    layout::LayoutBox box;
    box.background = {1.0f, 0.0f, 0.0f, 1.0f};
    paint::paint(nullptr, box, fake_drawer);
    CHECK(true);
}
