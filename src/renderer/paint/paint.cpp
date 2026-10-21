#include "renderer/paint/paint.hpp"

#include <cstddef>
#include <memory>
#include <vector>

namespace honeybee::renderer::paint {

namespace {

void fill_rect(cairo_t* cr, const layout::Color& c,
               float x, float y, float w, float h) {
    if (c.a == 0.0f) return;
    if (w <= 0.0f || h <= 0.0f) return;
    cairo_set_source_rgba(cr, c.r, c.g, c.b, c.a);
    cairo_rectangle(cr, x, y, w, h);
    cairo_fill(cr);
}

void paint_children(
    cairo_t* cr,
    const std::vector<std::unique_ptr<layout::LayoutNode>>& children,
    const LineDrawer& drawer);

void paint_box(cairo_t* cr, const layout::LayoutBox& box,
               const LineDrawer& drawer) {
    fill_rect(cr, box.background, box.x, box.y, box.width, box.height);

    const float& bt = box.border[0];
    const float& br = box.border[1];
    const float& bb = box.border[2];
    const float& bl = box.border[3];
    fill_rect(cr, box.border_color, box.x, box.y, box.width, bt);
    fill_rect(cr, box.border_color, box.x + box.width - br, box.y,
              br, box.height);
    fill_rect(cr, box.border_color, box.x, box.y + box.height - bb,
              box.width, bb);
    fill_rect(cr, box.border_color, box.x, box.y, bl, box.height);

    paint_children(cr, box.children, drawer);
}

void paint_children(
    cairo_t* cr,
    const std::vector<std::unique_ptr<layout::LayoutNode>>& children,
    const LineDrawer& drawer) {
    for (const auto& child : children) {
        if (!child) continue;
        switch (child->kind) {
            case layout::LayoutKind::Box:
                paint_box(
                    cr, static_cast<const layout::LayoutBox&>(*child),
                    drawer);
                break;
            case layout::LayoutKind::Text: {
                const auto& lt =
                    static_cast<const layout::LayoutText&>(*child);
                for (const auto& frag : lt.lines) drawer(cr, frag);
                break;
            }
            case layout::LayoutKind::InlineContext: {
                const auto& ctx =
                    static_cast<const layout::LayoutInlineContext&>(*child);
                for (const auto& frag : ctx.lines) drawer(cr, frag);
                break;
            }
            case layout::LayoutKind::Inline:
                // Structural, not painted directly.
                break;
        }
    }
}

} // namespace

void paint(cairo_t* cr, const layout::LayoutNode& root,
           LineDrawer drawer) {
    if (cr == nullptr || !drawer) return;
    switch (root.kind) {
        case layout::LayoutKind::Box:
            paint_box(
                cr, static_cast<const layout::LayoutBox&>(root), drawer);
            break;
        case layout::LayoutKind::Text: {
            const auto& lt =
                static_cast<const layout::LayoutText&>(root);
            for (const auto& frag : lt.lines) drawer(cr, frag);
            break;
        }
        case layout::LayoutKind::InlineContext: {
            const auto& ctx =
                static_cast<const layout::LayoutInlineContext&>(root);
            for (const auto& frag : ctx.lines) drawer(cr, frag);
            break;
        }
        case layout::LayoutKind::Inline:
            break;
    }
}

} // namespace honeybee::renderer::paint
