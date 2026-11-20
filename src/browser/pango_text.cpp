#include "pango_text.hpp"
#include "base/log.hpp"

#include <pango/pangocairo.h>

#include <string>
#include <string_view>

namespace honeybee::browser {

namespace {

// Creates a Pango layout for `t` on `cr`. Caller owns it and
// must g_object_unref() it.
PangoLayout* make_layout(cairo_t* cr,
                         std::string_view text,
                         std::string_view font_family,
                         float font_size) {
    PangoLayout* layout = pango_cairo_create_layout(cr);

    std::string family_str = font_family.empty()
                                 ? std::string("Sans")
                                 : std::string(font_family);
    PangoFontDescription* desc = pango_font_description_new();
    pango_font_description_set_family(desc, family_str.c_str());
    pango_font_description_set_absolute_size(
        desc, static_cast<double>(font_size) * PANGO_SCALE);
    pango_layout_set_font_description(layout, desc);
    pango_layout_set_text(layout, text.data(),
                          static_cast<int>(text.size()));

    pango_font_description_free(desc);
    return layout;
}

} // namespace

renderer::layout::TextMetrics measure_with_pango(
    std::string_view text, std::string_view font_family, float font_size) {
    if (text.empty()) {
        return {0.0f, font_size, font_size * 0.8f};
    }

    cairo_surface_t* surface =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t* cr = cairo_create(surface);

    PangoLayout* layout = make_layout(cr, text, font_family, font_size);
    int w = 0, h = 0;
    pango_layout_get_pixel_size(layout, &w, &h);
    const float baseline =
        static_cast<float>(pango_layout_get_baseline(layout)) /
        static_cast<float>(PANGO_SCALE);

    g_object_unref(layout);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);

    return {
        static_cast<float>(w),
        static_cast<float>(h),
        baseline
    };
}

renderer::layout::FontMetrics measure_font_metrics_with_pango(
    std::string_view font_family, float font_size) {
    cairo_surface_t* surface =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t* cr = cairo_create(surface);

    // Use a real glyph to estimate x-height from its ink extents.
    PangoLayout* layout = make_layout(cr, "x", font_family, font_size);
    PangoContext* context = pango_layout_get_context(layout);

    const std::string family_str = font_family.empty()
        ? std::string("Sans")
        : std::string(font_family);

    PangoFontDescription* desc = pango_font_description_new();
    pango_font_description_set_family(desc, family_str.c_str());
    pango_font_description_set_absolute_size(
        desc, static_cast<double>(font_size) * PANGO_SCALE);

    PangoFontMetrics* pango_metrics = pango_context_get_metrics(
        context, desc, pango_context_get_language(context));

    const float ascent =
        static_cast<float>(pango_font_metrics_get_ascent(pango_metrics)) /
        static_cast<float>(PANGO_SCALE);
    const float descent =
        static_cast<float>(pango_font_metrics_get_descent(pango_metrics)) /
        static_cast<float>(PANGO_SCALE);

    float normal_height =
        static_cast<float>(pango_font_metrics_get_height(pango_metrics)) /
        static_cast<float>(PANGO_SCALE);
    if (normal_height <= 0.0f) {
        normal_height = ascent + descent;
    }

    PangoRectangle ink{};
    PangoRectangle logical{};
    pango_layout_get_extents(layout, &ink, &logical);

    const float x_height =
        static_cast<float>(ink.height) / static_cast<float>(PANGO_SCALE);

    pango_font_metrics_unref(pango_metrics);
    pango_font_description_free(desc);
    g_object_unref(layout);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);

    return {ascent, descent, x_height, normal_height};
}

void draw_line_with_pango(
    cairo_t* cr,
    const renderer::layout::LineFragment& frag) {
    if (cr == nullptr || frag.text.empty()) return;

    HB_LOG(Paint, Trace, "  FRAG: text='%.30s' x=%.1f y=%.1f w=%.1f h=%.1f color=(%.2f,%.2f,%.2f,%.2f)",
           frag.text.c_str(), frag.x, frag.y, frag.width, frag.height,
           frag.color.r, frag.color.g, frag.color.b, frag.color.a);

    HB_LOG(Paint, Trace, "PAINT frag: x=%.1f y=%.1f w=%.1f h=%.1f size=%.1f color=(%.2f,%.2f,%.2f,%.2f) text='%.40s'",
           frag.x, frag.y, frag.width, frag.height, frag.font_size,
           frag.color.r, frag.color.g, frag.color.b, frag.color.a,
           frag.text.c_str());

    PangoLayout* layout =
        make_layout(cr, frag.text, frag.font_family, frag.font_size);
    cairo_set_source_rgba(
        cr, frag.color.r, frag.color.g, frag.color.b, frag.color.a);
    cairo_move_to(cr, frag.x, frag.y);
    pango_cairo_show_layout(cr, layout);
    g_object_unref(layout);
}

} // namespace honeybee::browser
