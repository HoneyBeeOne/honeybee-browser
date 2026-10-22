#include "render_pipeline.hpp"

#include <functional>
#include <string>

#include "pango_text.hpp"
#include "renderer/css/cascade.hpp"
#include "renderer/css/ua_stylesheet.hpp"
#include "renderer/css/stylesheet.hpp"
#include "renderer/html/convert.hpp"
#include "renderer/layout/layout.hpp"
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

} // namespace

void RenderPipeline::render(const std::string& html,
                            float viewport_width) {
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
