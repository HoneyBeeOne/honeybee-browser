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

void paint_children(cairo_t* cr,
                    const std::vector<std::unique_ptr<layout::LayoutNode>>&
                        children,
                    const TextDrawer& drawer);

void paint_box(cairo_t* cr, const layout::LayoutBox& box,
               const TextDrawer& drawer) {
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

void paint_children(cairo_t* cr,
                    const std::vector<std::unique_ptr<layout::LayoutNode>>&
                        children,
                    const TextDrawer& drawer) {
    for (const auto& child : children) {
        if (!child) continue;
        if (child->kind == layout::LayoutKind::Box) {
            paint_box(cr, static_cast<const layout::LayoutBox&>(*child),
                      drawer);
        } else {
            const auto& lt =
                static_cast<const layout::LayoutText&>(*child);
            for (const auto& frag : lt.lines) {
                drawer(cr, frag, lt);
            }
        }
    }
}

} // namespace

void paint(cairo_t* cr, const layout::LayoutNode& root,
           TextDrawer drawer) {
    if (cr == nullptr || !drawer) return;
    if (root.kind == layout::LayoutKind::Box) {
        paint_box(cr, static_cast<const layout::LayoutBox&>(root), drawer);
    } else {
        const auto& lt = static_cast<const layout::LayoutText&>(root);
        for (const auto& frag : lt.lines) {
            drawer(cr, frag, lt);
        }
    }
}

} // namespace honeybee::renderer::paint
