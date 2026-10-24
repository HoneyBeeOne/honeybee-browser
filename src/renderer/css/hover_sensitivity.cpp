#include "renderer/css/hover_sensitivity.hpp"

#include <functional>
#include <vector>

#include "renderer/dom/node.hpp"

namespace honeybee::renderer::css {

namespace {

// Walk every selector in `sheet`, classifying each by :hover
// position. Updates `overall` to the max level seen.
void classify_sheet(const Stylesheet& sheet, HoverLevel& overall) {
    for (const auto& rule : sheet.rules) {
        for (const auto& sel : rule.selectors.selectors) {
            std::size_t total_hover = 0;
            bool last_part_has_hover = false;
            bool any_other_part_has_hover = false;

            for (std::size_t i = 0; i < sel.parts.size(); ++i) {
                bool part_has_hover = false;
                for (const auto& simple : sel.parts[i].compound.simples) {
                    if (simple.kind == SimpleKind::PseudoHover) {
                        part_has_hover = true;
                        ++total_hover;
                    }
                }
                if (part_has_hover) {
                    if (i == sel.parts.size() - 1) {
                        last_part_has_hover = true;
                    } else {
                        any_other_part_has_hover = true;
                    }
                }
            }

            if (total_hover == 0) continue;

            HoverLevel this_sel;
            if (total_hover == 1 && last_part_has_hover &&
                !any_other_part_has_hover) {
                this_sel = HoverLevel::AnchorOnly;
            } else {
                this_sel = HoverLevel::Compound;
            }

            if (static_cast<int>(this_sel) > static_cast<int>(overall)) {
                overall = this_sel;
            }
        }
    }
}

} // namespace

HoverLevel detect_level(const Stylesheet& ua,
                        const Stylesheet& author) {
    HoverLevel overall = HoverLevel::None;
    classify_sheet(ua, overall);
    classify_sheet(author, overall);
    return overall;
}

std::unordered_set<const dom::Element*> compute_hover_sensitive(
    const dom::Document& doc,
    const Stylesheet& ua,
    const Stylesheet& author,
    const RenderState& state) {
    // Collect anchor-hover selectors from both sheets: those whose
    // last part contains a PseudoHover simple.
    std::vector<const Selector*> anchor_hover;

    auto collect = [&](const Stylesheet& sheet) {
        for (const auto& rule : sheet.rules) {
            for (const auto& sel : rule.selectors.selectors) {
                if (sel.parts.empty()) continue;
                const auto& last = sel.parts.back().compound.simples;
                for (const auto& simple : last) {
                    if (simple.kind == SimpleKind::PseudoHover) {
                        anchor_hover.push_back(&sel);
                        break;
                    }
                }
            }
        }
    };
    collect(ua);
    collect(author);

    std::unordered_set<const dom::Element*> result;
    if (anchor_hover.empty()) return result;

    // One RenderState copy total; hovered_element is mutated per
    // element. matches() only reads state.hovered_element, so this
    // is safe and avoids O(visited_urls) copies per element.
    RenderState temp = state;

    std::function<void(const dom::Node&)> walk =
        [&](const dom::Node& n) {
            if (n.type() == dom::NodeType::Element) {
                const auto& el = static_cast<const dom::Element&>(n);
                temp.hovered_element = &el;
                for (const auto* sel : anchor_hover) {
                    if (sel->matches(el, temp)) {
                        result.insert(&el);
                        break;
                    }
                }
            }
            for (const auto& child : n.children()) walk(*child);
        };
    walk(doc);
    return result;
}

} // namespace honeybee::renderer::css
