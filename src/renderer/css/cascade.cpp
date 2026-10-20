#include "renderer/css/cascade.hpp"

#include <cstddef>
#include <string_view>
#include <utility>

namespace honeybee::renderer::css {

bool is_inherited(std::string_view property) {
    return property == "color" ||
           property == "font-family" ||
           property == "font-style" ||
           property == "font-weight" ||
           property == "line-height" ||
           property == "visibility";
}

namespace {

// Order per ADR-010 + ADR-014:
//   1. importance (important beats normal)
//   2. origin (Author beats UserAgent for normal declarations;
//      v1 has no UA-important rules, so the inverted case is not
//      exercised)
//   3. specificity (higher wins)
//   4. source order (later wins on a full tie)
bool beats(const CascadeValue& a, const CascadeValue& b) {
    if (a.important != b.important) return a.important;
    if (a.origin != b.origin) return a.origin == Origin::Author;
    if (a.specificity == b.specificity) {
        return a.source_order > b.source_order;
    }
    return b.specificity < a.specificity;
}

void apply_rule(const Rule& rule, const dom::Element& el,
                Origin origin, std::size_t& counter,
                ComputedStyle& style) {
    auto spec = rule.selectors.matches(el);
    if (!spec) return;

    for (const auto& d : rule.declarations) {
        CascadeValue v;
        v.value = d.value;
        v.important = d.important;
        v.origin = origin;
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

void compute_styles(const dom::Node& node,
                    const Stylesheet& ua_sheet,
                    const Stylesheet& author_sheet,
                    ComputedStyleMap& out) {
    if (node.type() == dom::NodeType::Element) {
        const auto& el = static_cast<const dom::Element&>(node);
        ComputedStyle style;
        std::size_t counter = 0;
        for (const auto& rule : ua_sheet.rules) {
            apply_rule(rule, el, Origin::UserAgent, counter, style);
        }
        for (const auto& rule : author_sheet.rules) {
            apply_rule(rule, el, Origin::Author, counter, style);
        }
        out[&el] = std::move(style);
    }
    for (const auto& child : node.children()) {
        compute_styles(*child, ua_sheet, author_sheet, out);
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

ComputedStyleMap cascade(const Stylesheet& ua_sheet,
                         const Stylesheet& author_sheet,
                         const dom::Document& doc) {
    ComputedStyleMap out;
    compute_styles(doc, ua_sheet, author_sheet, out);
    propagate_inheritance(doc, out);
    return out;
}

ComputedStyleMap cascade(const Stylesheet& author_sheet,
                         const dom::Document& doc) {
    const Stylesheet empty_ua;
    return cascade(empty_ua, author_sheet, doc);
}

} // namespace honeybee::renderer::css
