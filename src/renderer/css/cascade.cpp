#include "renderer/css/cascade.hpp"
#include "renderer/css/render_state.hpp"

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

// ADR-022 test-only: total apply_rule() invocations since last reset.
// Accessed via the public apply_rule_call_count() / reset_apply_rule_call_count()
// accessors. thread_local so tests running in one thread don't see
// increments from another (defensive; currently tests are single-threaded).
thread_local std::size_t g_apply_rule_call_count = 0;

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

// ADR-027: does the rule's media query match the current viewport?
// Rules with no media field (nullopt) always match. Rules with a
// populated MediaQuery match if all populated bounds are satisfied.
bool media_matches(const std::optional<MediaQuery>& mq, float vw) {
    if (!mq) return true;  // top-level rule, no condition
    if (mq->min_width_px && vw < *mq->min_width_px) return false;
    if (mq->max_width_px && vw > *mq->max_width_px) return false;
    return true;
}

void apply_rule(const Rule& rule, const dom::Element& el,
                Origin origin, std::size_t& counter,
                const RenderState& state, float viewport_width,
                ComputedStyle& style) {
    ++g_apply_rule_call_count;
    if (!media_matches(rule.media, viewport_width)) return;
    auto spec = rule.selectors.matches(el, state);
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
                    const RenderState& state,
                    float viewport_width,
                    ComputedStyleMap& out) {
    if (node.type() == dom::NodeType::Element) {
        const auto& el = static_cast<const dom::Element&>(node);
        ComputedStyle style;
        std::size_t counter = 0;
        for (const auto& rule : ua_sheet.rules) {
            apply_rule(rule, el, Origin::UserAgent, counter, state,
                       viewport_width, style);
        }
        for (const auto& rule : author_sheet.rules) {
            apply_rule(rule, el, Origin::Author, counter, state,
                       viewport_width, style);
        }
        out[&el] = std::move(style);
    }
    for (const auto& child : node.children()) {
        compute_styles(*child, ua_sheet, author_sheet, state,
                       viewport_width, out);
    }
}

} // namespace

std::size_t apply_rule_call_count() { return g_apply_rule_call_count; }
void reset_apply_rule_call_count() { g_apply_rule_call_count = 0; }

// ADR-022: moved from the anonymous namespace so the targeted
// restyle path can call it directly after targeted cascade_subtree.
// Body unchanged.
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
                         const dom::Document& doc,
                         float viewport_width) {
    return cascade(ua_sheet, author_sheet, doc, RenderState{},
                   viewport_width);
}

ComputedStyleMap cascade(const Stylesheet& author_sheet,
                         const dom::Document& doc) {
    const Stylesheet empty_ua;
    return cascade(empty_ua, author_sheet, doc);
}

ComputedStyleMap cascade(const Stylesheet& ua_sheet,
                         const Stylesheet& author_sheet,
                         const dom::Document& doc,
                         const RenderState& state,
                         float viewport_width) {
    ComputedStyleMap out;
    compute_styles(doc, ua_sheet, author_sheet, state, viewport_width,
                   out);
    propagate_inheritance(doc, out);
    return out;
}

// ADR-022: targeted cascade primitives.

void cascade_subtree(const dom::Element& el,
                     const Stylesheet& ua_sheet,
                     const Stylesheet& author_sheet,
                     const RenderState& state,
                     float viewport_width,
                     ComputedStyleMap& out) {
    // compute_styles already traverses the element's subtree;
    // starting from `el` scopes it correctly. Inherited properties
    // are intentionally left unfilled -- the caller runs
    // propagate_inheritance after erasing + re-cascading all dirty
    // subtrees, so inheritance is computed once for the whole tree.
    compute_styles(el, ua_sheet, author_sheet, state, viewport_width,
                   out);
}

void erase_subtree(const dom::Element& el, ComputedStyleMap& out) {
    out.erase(&el);
    for (const auto& child : el.children()) {
        if (child->type() == dom::NodeType::Element) {
            erase_subtree(static_cast<const dom::Element&>(*child), out);
        }
    }
}

} // namespace honeybee::renderer::css
