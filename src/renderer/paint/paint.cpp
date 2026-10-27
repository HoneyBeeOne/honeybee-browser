#include "renderer/paint/paint.hpp"
#include "renderer/dom/node.hpp"


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

// ADR-034: draw an image (from <img> src or background-image url)
// scaled to the box's rect. The provider owns the surface; we
// borrow it. Zero-height + non-zero-width boxes (the wordmark
// case) derive height from the surface's intrinsic aspect ratio.
void paint_image(cairo_t* cr, const layout::LayoutBox& box,
                 const std::string& url, const ImageProvider* images) {
    if (!images || !images->get_surface) return;
    cairo_surface_t* surf = images->get_surface(url);
    if (!surf) return;

    const float iw = static_cast<float>(cairo_image_surface_get_width(surf));
    const float ih = static_cast<float>(cairo_image_surface_get_height(surf));
    if (iw <= 0.0f || ih <= 0.0f) return;

    float pw = box.width;
    float ph = box.height;
    if (pw > 0.0f && ph <= 0.0f) {
        ph = pw * (ih / iw);
    } else if (pw <= 0.0f && ph <= 0.0f) {
        pw = iw; ph = ih;
    } else if (pw <= 0.0f && ph > 0.0f) {
        pw = ph * (iw / ih);
    }
    if (pw <= 0.0f || ph <= 0.0f) return;

    cairo_save(cr);
    cairo_translate(cr, box.x, box.y);
    cairo_scale(cr, pw / iw, ph / ih);
    cairo_set_source_surface(cr, surf, 0, 0);
    cairo_paint(cr);
    cairo_restore(cr);
}

void paint_children(
    cairo_t* cr,
    const std::vector<std::unique_ptr<layout::LayoutNode>>& children,
    const LineDrawer& drawer,
    const ImageProvider* images);

void paint_box(cairo_t* cr, const layout::LayoutBox& box,
               const LineDrawer& drawer,
               const ImageProvider* images) {
    // ADR-032: paint-authoritative culling. Skip the entire
    // subtree if visibility:hidden or opacity:0. Set on
    // LayoutBox::visible by layout_element. Handles both root
    // and child invocations since they all route through
    // paint_box().
    if (!box.visible) return;

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

    // ADR-034: draw background-image if present.
    if (images && !box.background_image_url.empty()) {
        paint_image(cr, box, box.background_image_url, images);
    }

    // ADR-034: draw <img> element if this box is one.
    if (images && box.element && box.element->tag() == "img") {
        std::string src = box.element->get_attribute("src");
        if (!src.empty()) {
            paint_image(cr, box, src, images);
        }
    }

    paint_children(cr, box.children, drawer, images);
}

void paint_children(
    cairo_t* cr,
    const std::vector<std::unique_ptr<layout::LayoutNode>>& children,
    const LineDrawer& drawer,
    const ImageProvider* images) {
    for (const auto& child : children) {
        if (!child) continue;
        switch (child->kind) {
            case layout::LayoutKind::Box:
                paint_box(
                    cr, static_cast<const layout::LayoutBox&>(*child),
                    drawer, images);
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
    paint(cr, root, std::move(drawer), nullptr);
}

void paint(cairo_t* cr, const layout::LayoutNode& root,
           LineDrawer drawer, const ImageProvider* images) {
    if (cr == nullptr || !drawer) return;
    switch (root.kind) {
        case layout::LayoutKind::Box:
            paint_box(
                cr, static_cast<const layout::LayoutBox&>(root), drawer, images);
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
