#include "pango_text.hpp"

#include <pango/pangocairo.h>

#include <string>

namespace honeybee::browser {

namespace {

// Creates a Pango layout for `t` on `cr`. Caller owns it and
// must g_object_unref() it.
PangoLayout* make_layout(cairo_t* cr,
                         const renderer::layout::LayoutText& t) {
    PangoLayout* layout = pango_cairo_create_layout(cr);

    const char* family =
        t.font_family.empty() ? "Sans" : t.font_family.c_str();
    PangoFontDescription* desc = pango_font_description_new();
    pango_font_description_set_family(desc, family);
    pango_font_description_set_absolute_size(
        desc, static_cast<double>(t.font_size) * PANGO_SCALE);
    pango_layout_set_font_description(layout, desc);
    pango_layout_set_text(layout, t.text.data(),
                          static_cast<int>(t.text.size()));

    pango_font_description_free(desc);
    return layout;
}

} // namespace

std::pair<float, float> measure_with_pango(
    const renderer::layout::LayoutText& t) {
    if (t.text.empty()) return {0.0f, t.font_size};

    cairo_surface_t* surface =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t* cr = cairo_create(surface);

    PangoLayout* layout = make_layout(cr, t);
    int w = 0, h = 0;
    pango_layout_get_pixel_size(layout, &w, &h);

    g_object_unref(layout);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);

    return {static_cast<float>(w), static_cast<float>(h)};
}

void draw_with_pango(cairo_t* cr,
                     const renderer::layout::LayoutText& t) {
    if (cr == nullptr || t.text.empty()) return;

    PangoLayout* layout = make_layout(cr, t);
    cairo_set_source_rgba(cr, t.color.r, t.color.g, t.color.b, t.color.a);
    cairo_move_to(cr, t.x, t.y);
    pango_cairo_show_layout(cr, layout);
    g_object_unref(layout);
}

} // namespace honeybee::browser
