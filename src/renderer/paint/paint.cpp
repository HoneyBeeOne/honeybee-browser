#include "renderer/paint/paint.hpp"
#include "renderer/dom/node.hpp"


#include <cmath>
#include <algorithm>
#include <cstddef>
#include <memory>
#include <vector>

#include "base/log.hpp"

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

// ADR-047: draw mask-image over the already-drawn background.
// Cairo path: set source to background_color, then cairo_mask
// with the loaded surface pattern (scaled + positioned). The
// pattern's alpha channel determines where the fill shows.
// v1 renders only when background.a > 0 (transparent bg =
// invisible mask). Documented deviation: for boxes with
// border-radius, the border ring is also masked (v1 draws
// mask after the whole if(any_radius)/else block). 0 target
// rules combine mask + border-radius.
void paint_mask_image(cairo_t* cr, const layout::LayoutBox& box,
                      const ImageProvider* images) {
    if (!box.mask_has || box.mask_image_url.empty()) return;
    if (box.background.a <= 0.0001f) return;
    if (!images || !images->get_surface) return;
    cairo_surface_t* surf = images->get_surface(box.mask_image_url);
    if (!surf) return;

    const float iw = static_cast<float>(
        cairo_image_surface_get_width(surf));
    const float ih = static_cast<float>(
        cairo_image_surface_get_height(surf));
    if (iw <= 0.0f || ih <= 0.0f) return;

    // Size: mask_size_w > 0 wins; if mask_size_h is 0, aspect-
    // derive. Both 0 = intrinsic.
    float pw = box.mask_size_w > 0.0f ? box.mask_size_w : iw;
    float ph = box.mask_size_h > 0.0f ? box.mask_size_h
             : (box.mask_size_w > 0.0f
                ? box.mask_size_w * (ih / iw)
                : ih);
    if (pw <= 0.0f || ph <= 0.0f) return;

    // Position keywords.
    float ix = box.x;
    if (box.mask_pos_x == layout::BgPosX::Right) {
        ix = box.x + box.width - pw;
    } else if (box.mask_pos_x == layout::BgPosX::Center) {
        ix = box.x + (box.width - pw) * 0.5f;
    }
    float iy = box.y;
    if (box.mask_pos_y == layout::BgPosY::Bottom) {
        iy = box.y + box.height - ph;
    } else if (box.mask_pos_y == layout::BgPosY::Center) {
        iy = box.y + (box.height - ph) * 0.5f;
    }

    HB_LOG(Paint, Debug, "mask: %s at (%.0f,%.0f) size %.0fx%.0f",
           box.mask_image_url.c_str(), ix, iy, pw, ph);

    // Pattern matrix maps device coords -> surface coords.
    // device (ix, iy)         -> surf (0, 0)
    // device (ix+pw, iy+ph)   -> surf (iw, ih)
    // M = scale(iw/pw, ih/ph) * translate(-ix, -iy)
    cairo_save(cr);
    cairo_set_source_rgba(cr, box.background.r, box.background.g,
                          box.background.b, box.background.a);
    cairo_pattern_t* pat = cairo_pattern_create_for_surface(surf);
    cairo_matrix_t mat;
    cairo_matrix_init_scale(&mat, iw / pw, ih / ph);
    cairo_matrix_translate(&mat, -ix, -iy);
    cairo_pattern_set_matrix(pat, &mat);
    cairo_mask(cr, pat);
    cairo_pattern_destroy(pat);
    cairo_restore(cr);
}

void paint_children(
    cairo_t* cr,
    const std::vector<std::unique_ptr<layout::LayoutNode>>& children,
    const LineDrawer& drawer,
    const ImageProvider* images);

// ADR-044: trace a rounded-rect path. Radii in TL, TR, BR, BL
// order. Each is clamped to min(w/2, h/2) -- Wikipedia's 2px on
// 100px+ boxes never triggers clamping; simple bound prevents
// self-overlap on tiny boxes (full CSS SS5.5 proportional clamp
// deferred).
void rounded_rect_path(cairo_t* cr, float x, float y,
                       float w, float h, const float r[4]) {
    if (w <= 0.0f || h <= 0.0f) return;
    const float half_min = std::min(w, h) * 0.5f;
    const float r_tl = std::max(0.0f, std::min(r[0], half_min));
    const float r_tr = std::max(0.0f, std::min(r[1], half_min));
    const float r_br = std::max(0.0f, std::min(r[2], half_min));
    const float r_bl = std::max(0.0f, std::min(r[3], half_min));

    cairo_new_path(cr);
    // Top-left corner: start on left edge, arc to top edge.
    if (r_tl > 0.0f) {
        cairo_arc(cr, x + r_tl, y + r_tl, r_tl, M_PI, 1.5f * M_PI);
    } else {
        cairo_move_to(cr, x, y);
    }
    // Top edge to TR corner start.
    cairo_line_to(cr, x + w - r_tr, y);
    // Top-right corner: top edge to right edge.
    if (r_tr > 0.0f) {
        cairo_arc(cr, x + w - r_tr, y + r_tr, r_tr,
                  1.5f * M_PI, 2.0f * M_PI);
    }
    // Right edge to BR corner start.
    cairo_line_to(cr, x + w, y + h - r_br);
    // Bottom-right corner: right edge to bottom edge.
    if (r_br > 0.0f) {
        cairo_arc(cr, x + w - r_br, y + h - r_br, r_br,
                  0.0f, 0.5f * M_PI);
    }
    // Bottom edge to BL corner start.
    cairo_line_to(cr, x + r_bl, y + h);
    // Bottom-left corner: bottom edge to left edge.
    if (r_bl > 0.0f) {
        cairo_arc(cr, x + r_bl, y + h - r_bl, r_bl, 0.5f * M_PI, M_PI);
    }
    cairo_close_path(cr);
}

void paint_box(cairo_t* cr, const layout::LayoutBox& box,
               const LineDrawer& drawer,
               const ImageProvider* images) {
    // ADR-032: paint-authoritative culling. Skip the entire
    // subtree if visibility:hidden or opacity:0. Set on
    // LayoutBox::visible by layout_element. Handles both root
    // and child invocations since they all route through
    // paint_box().
    if (!box.visible) return;

    // ADR-043: draw box-shadow before background (CSS z-order:
    // shadow renders behind the element's own background/border).
    // Only non-inset, non-blurred shadows render in v1; inset
    // and blurred shadows are parsed and stored but skipped
    // here (deferrals in ADR-043).
    if (box.box_shadow_has
        && !box.box_shadow_inset
        && box.box_shadow_blur <= 0.0001f) {
        const float sx = box.x + box.box_shadow_ox - box.box_shadow_spread;
        const float sy = box.y + box.box_shadow_oy - box.box_shadow_spread;
        const float sw = box.width  + 2.0f * box.box_shadow_spread;
        const float sh = box.height + 2.0f * box.box_shadow_spread;
        // CSS spec: "outer box-shadow is drawn outside the
        // border edge only; it is clipped inside the border-
        // box." Even-odd clip of (shadow_rect, box_rect)
        // produces the L-shaped outside-only region.
        cairo_save(cr);
        cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
        cairo_rectangle(cr, sx, sy, sw, sh);
        cairo_rectangle(cr, box.x, box.y, box.width, box.height);
        cairo_clip(cr);
        fill_rect(cr, box.box_shadow_color, sx, sy, sw, sh);
        cairo_restore(cr);
    }

    // ADR-044: if any border-radius > 0, use rounded-rect path
    // (two-fill: outer border_color ring + inner background_color).
    // Zero-radius boxes take the existing rect path byte-for-byte.
    const bool any_radius = box.border_radius[0] > 0.001f
                         || box.border_radius[1] > 0.001f
                         || box.border_radius[2] > 0.001f
                         || box.border_radius[3] > 0.001f;

    if (any_radius) {
        const float bt = box.border[0], br = box.border[1];
        const float bb = box.border[2], bl = box.border[3];

        // Outer: fill with border_color (visible ring when
        // border-width > 0 AND border_color.a > 0). Transparent
        // border_color produces no visible ring -- correct.
        rounded_rect_path(cr, box.x, box.y, box.width, box.height,
                          box.border_radius);
        cairo_set_source_rgba(cr, box.border_color.r, box.border_color.g,
                              box.border_color.b, box.border_color.a);
        cairo_fill(cr);

        // Inner: fill with background_color (inset by border).
        // Radius reduced by border-width per corner, clamped to 0.
        const float inner_x = box.x + bl;
        const float inner_y = box.y + bt;
        const float inner_w = box.width  - bl - br;
        const float inner_h = box.height - bt - bb;
        if (inner_w > 0.0f && inner_h > 0.0f) {
            const float inner_r[4] = {
                std::max(0.0f, box.border_radius[0] - bt),
                std::max(0.0f, box.border_radius[1] - bt),
                std::max(0.0f, box.border_radius[2] - bb),
                std::max(0.0f, box.border_radius[3] - bb),
            };
            rounded_rect_path(cr, inner_x, inner_y, inner_w, inner_h,
                              inner_r);
            cairo_set_source_rgba(cr, box.background.r, box.background.g,
                                  box.background.b, box.background.a);
            cairo_fill(cr);
        }
    } else {
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
    }

    // ADR-047: mask draws after background/border fill, before
    // background-image (which paints above the mask layer).
    paint_mask_image(cr, box, images);

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

    // ADR-039: clip children to content bounds when
    // overflow: hidden. Save / clip / paint / restore.
    if (box.clip_children) {
        cairo_save(cr);
        const float cx = box.x + box.padding[3];
        const float cy = box.y + box.padding[0];
        const float cw = box.width - box.padding[3] - box.padding[1];
        const float ch = box.height - box.padding[0] - box.padding[2];
        cairo_rectangle(cr, cx, cy,
                        cw > 0.0f ? cw : 0.0f,
                        ch > 0.0f ? ch : 0.0f);
        cairo_clip(cr);
    }
    paint_children(cr, box.children, drawer, images);
    if (box.clip_children) cairo_restore(cr);
}

// ADR-045: draw an inline-element background-image on a
// LineFragment. Resolves bg_pos_x/y keywords against loaded
// icon dimensions (layout couldn't -- no image access there).
void paint_inline_image(cairo_t* cr,
                        const layout::LineFragment& frag,
                        const ImageProvider* images) {
    if (frag.bg_image_url.empty()) return;
    if (!images || !images->get_surface) return;
    cairo_surface_t* surf = images->get_surface(frag.bg_image_url);
    if (!surf) return;

    const float iw = static_cast<float>(
        cairo_image_surface_get_width(surf));
    const float ih = static_cast<float>(
        cairo_image_surface_get_height(surf));
    if (iw <= 0.0f || ih <= 0.0f) return;

    // Size: explicit bg_size_w wins; if bg_size_h is 0, aspect-
    // derive from bg_size_w. Both 0 = intrinsic size.
    float pw = frag.bg_size_w > 0.0f ? frag.bg_size_w : iw;
    float ph = frag.bg_size_h > 0.0f ? frag.bg_size_h
             : (frag.bg_size_w > 0.0f
                ? frag.bg_size_w * (ih / iw)
                : ih);
    if (pw <= 0.0f || ph <= 0.0f) return;

    // Position: keyword resolve.
    float ix = frag.x;
    if (frag.bg_pos_x == layout::BgPosX::Right) {
        ix = frag.x + frag.width - pw;
    } else if (frag.bg_pos_x == layout::BgPosX::Center) {
        ix = frag.x + (frag.width - pw) * 0.5f;
    }
    float iy = frag.y;
    if (frag.bg_pos_y == layout::BgPosY::Bottom) {
        iy = frag.y + frag.height - ph;
    } else if (frag.bg_pos_y == layout::BgPosY::Center) {
        iy = frag.y + (frag.height - ph) * 0.5f;
    }

    cairo_save(cr);
    cairo_translate(cr, ix, iy);
    cairo_scale(cr, pw / iw, ph / ih);
    cairo_set_source_surface(cr, surf, 0, 0);
    cairo_paint(cr);
    cairo_restore(cr);
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
                // ADR-045: draw inline backgrounds first, then
                // glyphs (correct z-order).
                for (const auto& frag : lt.lines) {
                    paint_inline_image(cr, frag, images);
                }
                for (const auto& frag : lt.lines) drawer(cr, frag);
                break;
            }
            case layout::LayoutKind::InlineContext: {
                const auto& ctx =
                    static_cast<const layout::LayoutInlineContext&>(*child);
                // ADR-045: draw inline backgrounds first, then
                // glyphs (correct z-order).
                for (const auto& frag : ctx.lines) {
                    paint_inline_image(cr, frag, images);
                }
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
    HB_LOG(Paint, Debug, "paint: root kind=%d",
           static_cast<int>(root.kind));
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
