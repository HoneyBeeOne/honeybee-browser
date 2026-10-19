#include "renderer/css/cascade.hpp"

#include <cstddef>
#include <utility>

namespace honeybee::renderer::css {

namespace {

// Returns true if `a` beats `b` for the same property.
// Order per ADR-010: importance > specificity > source order.
bool beats(const CascadeValue& a, const CascadeValue& b) {
    if (a.important != b.important) return a.important;
    if (a.specificity == b.specificity) {
        return a.source_order > b.source_order;
    }
    return b.specificity < a.specificity;
}

// Apply one rule's declarations to `style`, if the rule's
// selector list matches `el`. `counter` is a monotonic
// per-element counter that captures "later in source order wins"
// including later declarations within the same rule.
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

void walk(const dom::Node& node, const Stylesheet& sheet,
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
        walk(*child, sheet, out);
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
    walk(doc, sheet, out);
    return out;
}

} // namespace honeybee::renderer::css
