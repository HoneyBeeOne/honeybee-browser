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
enum class Display : std::uint8_t { Block, Inline, None };

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

struct LayoutBox final : LayoutNode {
    float margin[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float border[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float padding[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    Color background = Color::transparent();
    Color border_color = Color::transparent();

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
