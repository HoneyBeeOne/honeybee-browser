// =============================================================
// File: layout_box.hpp
// Purpose: Layout tree types (ADR-011, ADR-015).
//
// Pure types with no external dependencies: no dom, no css,
// no GTK. LayoutNode is an abstract base; LayoutBox and
// LayoutText derive from it. LayoutText holds a vector of
// LineFragment (ADR-015) for line-broken text.
// =============================================================

#pragma once

#include <cstddef>
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

enum class LayoutKind { Box, Text };

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

// One rendered line of a text node (ADR-015). Position is
// absolute, in the same coordinate space as the parent
// LayoutText's bounding box.
struct LineFragment {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    std::string text;
};

struct LayoutText final : LayoutNode {
    std::string text;               // full collapsed text
    std::string font_family;
    float font_size = 16.0f;
    Color color = Color::black();
    std::vector<LineFragment> lines;  // one entry per rendered line
    bool rtl = false;                 // first-strong heuristic

    LayoutText() : LayoutNode(LayoutKind::Text) {}
};

// Measures a single string at a given font. Called once per
// word during line breaking, and once per line for height.
using TextMeasurer = std::function<std::pair<float, float>(
    std::string_view text,
    std::string_view font_family,
    float font_size)>;

} // namespace honeybee::renderer::layout
