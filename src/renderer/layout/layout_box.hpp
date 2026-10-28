// =============================================================
// File: layout_box.hpp
// Purpose: Layout tree types (ADR-011, ADR-015, ADR-016).
//
// LayoutNode is an abstract base; LayoutBox, LayoutText,
// LayoutInline, and LayoutInlineContext derive from it.
// LayoutText holds a vector of LineFragment (ADR-015) for
// line-broken text. LayoutInline and LayoutInlineContext are
// introduced in ADR-016 for inline formatting.
// =============================================================

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace honeybee::renderer::dom { class Element; }

namespace honeybee::renderer::layout {

struct Color {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 0.0f;

    static Color black() { return {0.0f, 0.0f, 0.0f, 1.0f}; }
    static Color transparent() { return {0.0f, 0.0f, 0.0f, 0.0f}; }

    bool operator==(const Color&) const = default;
};

// CSS display value (ADR-016). None and the layout modes
// (flex/grid/table) are deferred; v1 has Block and Inline.
// ADR-028: Grid added. `display: grid` parses to this value and
// is dispatched to layout_grid in Commit 2. In Commit 1 (this
// commit), Grid still falls through to block layout --
// behavior-neutral. Same isolation discipline as ADR-025/026/027's
// Commit 1.
enum class Display : std::uint8_t {
    Block, Inline, None, Grid,
    Flex, InlineFlex        // ADR-029: CSS flex layout
};

// Text direction for a block container (ADR-016). Resolved from
// the dir attribute on the element or its ancestors; defaults to
// Ltr at <html>.
enum class Direction : std::uint8_t { Ltr, Rtl };

enum class LayoutKind {
    Box,
    Text,
    Inline,
    InlineContext,
};

struct LayoutNode {
    LayoutKind kind;
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;

    virtual ~LayoutNode() = default;

    LayoutNode(const LayoutNode&) = delete;
    LayoutNode& operator=(const LayoutNode&) = delete;
    LayoutNode(LayoutNode&&) = default;
    LayoutNode& operator=(LayoutNode&&) = default;

protected:
    explicit LayoutNode(LayoutKind k) : kind(k) {}
};

// ADR-033: CSS position value. Determines flow participation.
//   Static:    default; in-flow block
//   Relative:  in-flow; positioning context for future absolute
//              children (no visible effect in v1; offsets ignored)
//   Absolute:  stored, but treated as Static in v1 (needs
//              containing-block tracking; ADR-034)
//   Fixed:     out-of-flow; viewport-anchored at (top, left)
//   Sticky:    stored, but treated as Static in v1 (needs
//              scroll-aware layout; ADR-034+)
enum class Position : std::uint8_t {
    Static, Relative, Absolute, Fixed, Sticky
};

struct LayoutBox final : LayoutNode {
    float margin[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float border[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float padding[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    Color background = Color::transparent();
    Color border_color = Color::transparent();

    // ADR-044: border-radius per corner (TL, TR, BR, BL).
    // 0 = no rounding on that corner. Any value > 0 triggers
    // the rounded paint path in Commit 2.
    float border_radius[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    // ADR-043: parsed box-shadow (non-inset, non-blurred
    // rendered in v1; inset / blur / multi parsed and stored
    // but skipped at paint). `box_shadow_has` gates painting.
    float box_shadow_ox = 0.0f;
    float box_shadow_oy = 0.0f;
    float box_shadow_blur = 0.0f;
    float box_shadow_spread = 0.0f;
    Color box_shadow_color = Color::transparent();
    bool  box_shadow_inset = false;
    bool  box_shadow_has = false;

    // ADR-032: paint-authoritative culling signal. Set during
    // layout from ComputedStyle's visibility/opacity. When false,
    // paint skips this box and its entire subtree. Layout does
    // not skip it (visibility:hidden, unlike display:none, still
    // occupies space).
    bool visible = true;

    // ADR-033: parsed position. Fixed is honored by layout
    // (out-of-flow + viewport anchor); Relative/Absolute/Sticky
    // are stored but treated as Static in v1 (ADR-042 extends
    // this: relative becomes a containing block marker;
    // absolute becomes out-of-flow + positioned).
    Position position = Position::Static;

    // ADR-042: containing block marker. Set when position !=
    // Static (matches CSS: relative/absolute/fixed/sticky all
    // establish containing blocks for absolute descendants).
    // Layout uses this to decide whether absolute children are
    // positioned against this element or fall back to static.
    bool is_containing_block = false;

    // ADR-034: background-image url() extracted from CSS during
    // layout. Empty when no background-image. Paint fetches +
    // decodes (PNG via Cairo, SVG via librsvg) and draws at box
    // rect. Handles data: URLs (search icon).
    std::string background_image_url;

    // ADR-039: paint clips children to box content bounds when
    // true. Set during layout from `overflow: hidden`. Children
    // render normally (positions computed as if not clipped);
    // paint wraps their draw in a Cairo clip rectangle.
    bool clip_children = false;

    const dom::Element* element = nullptr;
    std::vector<std::unique_ptr<LayoutNode>> children;

    LayoutBox() : LayoutNode(LayoutKind::Box) {}
};

// One rendered line of a text node (ADR-015). Style fields
// added in ADR-016 so fragments derived from an <em> or a
// <span style="color:red"> carry their own font and color.
struct LineFragment {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    std::string text;
    std::string font_family;
    float font_size = 16.0f;
    Color color = Color::black();

    // ADR-018 Commit 4b: source element for hit-testing. Text
    // directly inside a block gets the block element; text
    // inside an inline element (e.g. <a>) gets the inline
    // element. Nullptr for legacy fragments created before
    // this field existed (tests, future code paths).
    const dom::Element* element = nullptr;
};

struct LayoutText final : LayoutNode {
    std::string text;
    std::string font_family;
    float font_size = 16.0f;
    Color color = Color::black();
    std::vector<LineFragment> lines;
    bool rtl = false;

    LayoutText() : LayoutNode(LayoutKind::Text) {}
};

// Structural inline element (ADR-016). Its children are the
// element's inline descendants (LayoutText and nested
// LayoutInline). Flattened during inline context construction;
// not painted directly.
struct LayoutInline : LayoutNode {
    const dom::Element* element = nullptr;
    std::vector<std::unique_ptr<LayoutNode>> children;
    std::string font_family;
    float font_size = 16.0f;
    Color color = Color::black();
    Display display = Display::Inline;

    LayoutInline() : LayoutNode(LayoutKind::Inline) {}
};

// A block container's inline run (ADR-016). Collects inline
// items, flattens them, runs line breaking across the whole
// sequence, and holds the resulting line fragments.
struct LayoutInlineContext : LayoutNode {
    std::vector<std::unique_ptr<LayoutNode>> children;
    std::vector<LineFragment> lines;
    bool rtl = false;

    LayoutInlineContext() : LayoutNode(LayoutKind::InlineContext) {}
};

// Measures a single string at a given font (ADR-015).
using TextMeasurer = std::function<std::pair<float, float>(
    std::string_view text,
    std::string_view font_family,
    float font_size)>;

} // namespace honeybee::renderer::layout
