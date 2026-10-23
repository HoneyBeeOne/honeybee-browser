#include "render_pipeline.hpp"

#include <functional>
#include <string>

#include "pango_text.hpp"
#include "renderer/css/cascade.hpp"
#include "renderer/css/ua_stylesheet.hpp"
#include "renderer/css/stylesheet.hpp"
#include "renderer/html/convert.hpp"
#include "renderer/layout/layout.hpp"
#include "renderer/layout/style_values.hpp"
#include "renderer/paint/paint.hpp"

namespace honeybee::browser {

namespace {

// Collect the concatenated text of all <style> elements in
// `doc`. v1 only supports inline styles; <link rel=stylesheet>
// requires network loading and is deferred.
std::string extract_inline_css(const renderer::dom::Document& doc) {
    std::string css;
    std::function<void(const renderer::dom::Node&)> walk =
        [&](const renderer::dom::Node& n) {
            if (n.type() == renderer::dom::NodeType::Element) {
                const auto& el =
                    static_cast<const renderer::dom::Element&>(n);
                if (el.tag() == "style") {
                    css += el.text_content();
                    css += "\n";
                }
            }
            for (const auto& c : n.children()) walk(*c);
        };
    walk(doc);
    return css;
}

// ADR-021: walk the layout tree updating paint-only color
// fields from freshly-recomputed styles. Skips dimensions
// entirely -- x, y, width, height are never touched.
//
// Diagnostic finding (see commit message): paint only reads
// LayoutBox::background, LayoutBox::border_color, and
// LineFragment::color. LayoutText::color and LayoutInline::color
// are dead state -- inline nodes are flattened before paint,
// and LayoutText itself is not drawn directly (its fragments
// are). So the walk updates exactly those three fields and
// nothing else; no LayoutText::element backfill was needed
// because every LineFragment already carries its source
// element from ADR-018.
void restyle_walk(renderer::layout::LayoutNode& node,
                  const renderer::css::ComputedStyleMap& styles) {
    using renderer::layout::LayoutBox;
    using renderer::layout::LayoutInline;
    using renderer::layout::LayoutInlineContext;
    using renderer::layout::LayoutKind;
    using renderer::layout::LayoutText;
    using renderer::layout::parse_color;

    switch (node.kind) {
        case LayoutKind::Box: {
            auto& box = static_cast<LayoutBox&>(node);
            if (box.element) {
                auto it = styles.find(box.element);
                if (it != styles.end()) {
                    const auto bg = it->second.get("background-color");
                    box.background = bg
                        ? parse_color(*bg)
                        : renderer::layout::Color::transparent();
                    const auto bc = it->second.get("border-color");
                    box.border_color = bc
                        ? parse_color(*bc)
                        : renderer::layout::Color::transparent();
                }
            }
            for (auto& child : box.children) {
                restyle_walk(*child, styles);
            }
            break;
        }
        case LayoutKind::Text: {
            auto& lt = static_cast<LayoutText&>(node);
            for (auto& frag : lt.lines) {
                if (!frag.element) continue;
                auto it = styles.find(frag.element);
                if (it == styles.end()) continue;
                const auto cv = it->second.get("color");
                if (cv) frag.color = parse_color(*cv);
            }
            break;
        }
        case LayoutKind::Inline: {
            auto& inl = static_cast<LayoutInline&>(node);
            for (auto& child : inl.children) {
                restyle_walk(*child, styles);
            }
            break;
        }
        case LayoutKind::InlineContext: {
            auto& ctx = static_cast<LayoutInlineContext&>(node);
            for (auto& frag : ctx.lines) {
                if (!frag.element) continue;
                auto it = styles.find(frag.element);
                if (it == styles.end()) continue;
                const auto cv = it->second.get("color");
                if (cv) frag.color = parse_color(*cv);
            }
            for (auto& child : ctx.children) {
                restyle_walk(*child, styles);
            }
            break;
        }
    }
}

} // namespace

void RenderPipeline::render(const std::string& html,
                            float viewport_width) {
    // ADR-018: hovered_element is a raw pointer into doc_. When
    // doc_ is replaced (below), the old DOM is destroyed and
    // hovered_element becomes dangling. The next relayout()
    // would then dereference it during cascade (PseudoHover
    // walks the parent chain), causing a segfault. Clear it
    // before replacing the document.
    state_.hovered_element = nullptr;

    doc_ = renderer::html::parse(html);
    if (!doc_) {
        layout_root_ = renderer::layout::LayoutBox{};
        return;
    }
    relayout(viewport_width);
}

void RenderPipeline::relayout(float viewport_width) {
    if (!doc_) return;

    const std::string css = extract_inline_css(*doc_);
    auto sheet = renderer::css::Stylesheet::parse(css);
    auto styles = renderer::css::cascade(
        renderer::css::ua_stylesheet(), sheet, *doc_, state_);

    layout_root_ = renderer::layout::layout(
        *doc_, styles, &measure_with_pango, viewport_width);
}

void RenderPipeline::paint(cairo_t* cr) const {
    renderer::paint::paint(cr, layout_root_, &draw_line_with_pango);
}

std::string RenderPipeline::title() const {
    return doc_ ? doc_->title() : std::string{};
}

float RenderPipeline::layout_height() const {
    return layout_root_.height;
}

void RenderPipeline::restyle() {
    if (!doc_) return;
    const std::string css = extract_inline_css(*doc_);
    auto sheet = renderer::css::Stylesheet::parse(css);
    auto styles = renderer::css::cascade(
        renderer::css::ua_stylesheet(), sheet, *doc_, state_);
    restyle_walk(layout_root_, styles);
}

void RenderPipeline::record_visit(const std::string& absolute_url) {
    state_.base_url = absolute_url;
    state_.visited_urls.insert(absolute_url);
}

void RenderPipeline::set_hovered_element(
    const renderer::dom::Element* el) {
    state_.hovered_element = el;
}

const renderer::dom::Element* RenderPipeline::hit_test(
    float x, float y) const {
    return renderer::layout::hit_test(layout_root_, x, y);
}

} // namespace honeybee::browser
