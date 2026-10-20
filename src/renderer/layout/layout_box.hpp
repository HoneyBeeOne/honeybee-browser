// =============================================================
// File: layout_box.hpp
// Purpose: Layout tree types (per ADR-011).
//
// Pure types with no external dependencies: no dom, no css,
// no GTK. LayoutNode is an abstract base; LayoutBox and
// LayoutText derive from it. This is a deviation from the
// variant-based snippet in ADR-011 -- inheritance avoids the
// recursive-variant out-of-line-destructor gymnastics and
// matches the pattern used by WebKit and Chromium.
// =============================================================

#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace honeybee::renderer::dom { class Element; }

namespace honeybee::renderer::layout {

// RGBA color, each channel in [0, 1]. Alpha 0 means transparent.
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
    // Box model (top, right, bottom, left)
    float margin[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float border[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float padding[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    Color background = Color::transparent();
    Color border_color = Color::transparent();

    const dom::Element* element = nullptr;
    std::vector<std::unique_ptr<LayoutNode>> children;

    LayoutBox() : LayoutNode(LayoutKind::Box) {}
};

struct LayoutText final : LayoutNode {
    std::string text;
    std::string font_family;
    float font_size = 16.0f;
    Color color = Color::black();

    LayoutText() : LayoutNode(LayoutKind::Text) {}
};

// Callback for measuring text. Production uses Pango; tests use
// a fake (chars * 8 wide, 16 tall) for deterministic layout.
using TextMeasurer =
    std::function<std::pair<float, float>(const LayoutText&)>;

} // namespace honeybee::renderer::layout
