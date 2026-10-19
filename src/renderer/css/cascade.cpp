#include "renderer/css/cascade.hpp"

#include <cstddef>
#include <string_view>
#include <utility>

namespace honeybee::renderer::css {

bool is_inherited(std::string_view property) {
    return property == "color" ||
           property == "font-family" ||
           property == "font-size" ||
           property == "font-style" ||
           property == "font-weight" ||
           property == "line-height" ||
           property == "visibility";
}

namespace {

bool beats(const CascadeValue& a, const CascadeValue& b) {
    if (a.important != b.important) return a.important;
    if (a.specificity == b.specificity) {
        return a.source_order > b.source_order;
    }
    return b.specificity < a.specificity;
}

void apply_rule(const Rule& rule, const dom::Element& el,
                std::size_t& counter, ComputedStyle& style) {
    auto spec = rule.selectors.matches(el);
    if (!spec) return;

    for (const auto& d : rule.declarations) {
        CascadeValue v;
        v.value = d.value;
        v.important = d.important;
        v.specificity = *spec;
        v.source_order = counter++;

        auto it = style.declarations.find(d.property);
        if (it == style.declarations.end()) {
            style.declarations[d.property] = std::move(v);
        } else if (beats(v, it->second)) {
            it->second = std::move(v);
        }
    }
}

void compute_styles(const dom::Node& node, const Stylesheet& sheet,
                    ComputedStyleMap& out) {
    if (node.type() == dom::NodeType::Element) {
        const auto& el = static_cast<const dom::Element&>(node);
        ComputedStyle style;
        std::size_t counter = 0;
        for (const auto& rule : sheet.rules) {
            apply_rule(rule, el, counter, style);
        }
        out[&el] = std::move(style);
    }
    for (const auto& child : node.children()) {
        compute_styles(*child, sheet, out);
    }
}

void propagate_inheritance(const dom::Node& node,
                           ComputedStyleMap& out) {
    if (node.type() == dom::NodeType::Element) {
        const auto& el = static_cast<const dom::Element&>(node);
        const dom::Node* parent_node = el.parent();
        if (parent_node && parent_node->type() == dom::NodeType::Element) {
            const auto* parent_el =
                static_cast<const dom::Element*>(parent_node);
            auto parent_it = out.find(parent_el);
            if (parent_it != out.end()) {
                auto& style = out[&el];
                for (const auto& [prop, val] : parent_it->second.declarations) {
                    if (!is_inherited(prop)) continue;
                    if (style.declarations.find(prop) !=
                        style.declarations.end()) {
                        continue;
                    }
                    style.declarations[prop] = val;
                }
            }
        }
    }
    for (const auto& child : node.children()) {
        propagate_inheritance(*child, out);
    }
}

} // namespace

bool ComputedStyle::has(const std::string& property) const {
    return declarations.find(property) != declarations.end();
}

std::optional<std::string>
ComputedStyle::get(const std::string& property) const {
    auto it = declarations.find(property);
    if (it == declarations.end()) return std::nullopt;
    return it->second.value;
}

ComputedStyleMap cascade(const Stylesheet& sheet, const dom::Document& doc) {
    ComputedStyleMap out;
    compute_styles(doc, sheet, out);
    propagate_inheritance(doc, out);
    return out;
}

} // namespace honeybee::renderer::css
