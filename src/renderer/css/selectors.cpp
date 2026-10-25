#include "renderer/css/selectors.hpp"
#include "base/url.hpp"
#include "renderer/css/render_state.hpp"
#include "renderer/dom/node.hpp"

#include <utility>

namespace honeybee::renderer::css {

bool operator<(const Specificity& lhs, const Specificity& rhs) {
    if (lhs.a != rhs.a) return lhs.a < rhs.a;
    if (lhs.b != rhs.b) return lhs.b < rhs.b;
    return lhs.c < rhs.c;
}

namespace {

bool is_whitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

bool is_ident_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           c == '_' || c == '-';
}

bool is_ident_char(char c) {
    return is_ident_start(c) || (c >= '0' && c <= '9');
}

class Parser {
public:
    explicit Parser(std::string_view text) : text_(text) {}

    std::optional<Selector> parse_single() {
        auto sel = parse_selector();
        if (!sel) return std::nullopt;
        skip_ws();
        if (!at_end()) return std::nullopt;
        return sel;
    }

    std::optional<SelectorList> parse_list() {
        skip_ws();
        if (at_end()) return std::nullopt;

        SelectorList list;
        while (!failed_ && !at_end()) {
            auto sel = parse_selector();
            if (!sel) { failed_ = true; break; }
            list.selectors.push_back(std::move(*sel));
            skip_ws();
            if (peek() == ',') {
                advance();
                skip_ws();
                if (at_end()) { failed_ = true; break; }
                continue;
            }
            break;
        }

        if (failed_ || list.selectors.empty() || !at_end()) return std::nullopt;
        return list;
    }

private:
    std::optional<Selector> parse_selector() {
        Selector sel;
        skip_ws();
        auto first = parse_compound();
        if (!first) return std::nullopt;
        sel.parts.push_back({Combinator::None, std::move(*first)});

        while (!failed_) {
            const size_t save = pos_;
            const bool had_ws = skip_ws();

            if (at_end() || peek() == ',') {
                pos_ = save;
                break;
            }

            Combinator comb;
            const char c = peek();
            if (c == '>') {
                advance();
                skip_ws();
                comb = Combinator::Child;
            } else if (c == '+') {
                advance();
                skip_ws();
                comb = Combinator::Adjacent;
            } else if (c == '~') {
                advance();
                skip_ws();
                comb = Combinator::Sibling;
            } else if (had_ws) {
                comb = Combinator::Descendant;
            } else {
                failed_ = true;
                break;
            }

            auto next = parse_compound();
            if (!next) { failed_ = true; break; }
            sel.parts.push_back({comb, std::move(*next)});
        }

        if (failed_) return std::nullopt;
        return sel;
    }

    std::optional<CompoundSelector> parse_compound() {
        CompoundSelector comp;
        while (!failed_) {
            auto s = try_parse_simple();
            if (!s) break;
            comp.simples.push_back(std::move(*s));
        }
        if (failed_ || comp.simples.empty()) return std::nullopt;
        return comp;
    }

    std::optional<SimpleSelector> try_parse_simple() {
        if (at_end()) return std::nullopt;
        const char c = peek();

        SimpleSelector out;

        if (c == '*') {
            advance();
            out.kind = SimpleKind::Universal;
            return out;
        }
        if (c == '.') {
            advance();
            auto name = parse_ident();
            if (!name) { failed_ = true; return std::nullopt; }
            out.kind = SimpleKind::Class;
            out.name = std::move(*name);
            return out;
        }
        if (c == '#') {
            advance();
            auto name = parse_ident();
            if (!name) { failed_ = true; return std::nullopt; }
            out.kind = SimpleKind::Id;
            out.name = std::move(*name);
            return out;
        }
        if (c == '[') {
            return parse_attribute();
        }
        if (is_ident_start(c)) {
            auto name = parse_ident();
            if (!name) return std::nullopt;
            out.kind = SimpleKind::Type;
            out.name = std::move(*name);
            return out;
        }
        if (c == ':') {
            advance();
            auto name = parse_ident();
            if (!name) { failed_ = true; return std::nullopt; }
            if (*name == "link")    { out.kind = SimpleKind::PseudoLink;    return out; }
            if (*name == "visited") { out.kind = SimpleKind::PseudoVisited; return out; }
            if (*name == "hover")   { out.kind = SimpleKind::PseudoHover;   return out; }
            if (*name == "root")    { out.kind = SimpleKind::PseudoRoot;    return out; }
            failed_ = true;  // unknown pseudo-class
            return std::nullopt;
        }
        return std::nullopt;
    }

    std::optional<SimpleSelector> parse_attribute() {
        advance();
        skip_ws();
        auto name = parse_ident();
        if (!name) { failed_ = true; return std::nullopt; }
        skip_ws();

        SimpleSelector out;
        out.kind = SimpleKind::Attribute;
        out.name = std::move(*name);

        if (peek() == ']') {
            advance();
            return out;
        }

        AttributeMatch m;
        const char c = peek();
        if (c == '=') {
            advance();
            m = AttributeMatch::Exact;
        } else if (c == '^' || c == '$' || c == '*' || c == '~' || c == '|') {
            advance();
            if (peek() != '=') { failed_ = true; return std::nullopt; }
            advance();
            switch (c) {
                case '^': m = AttributeMatch::Prefix;   break;
                case '$': m = AttributeMatch::Suffix;   break;
                case '*': m = AttributeMatch::Contains; break;
                case '~': m = AttributeMatch::Word;     break;
                default:  m = AttributeMatch::Hyphen;   break;
            }
        } else {
            failed_ = true;
            return std::nullopt;
        }

        skip_ws();
        auto value = parse_attribute_value();
        if (!value) { failed_ = true; return std::nullopt; }
        skip_ws();
        if (peek() != ']') { failed_ = true; return std::nullopt; }
        advance();

        out.attr_match = m;
        out.attr_value = std::move(*value);
        return out;
    }

    std::optional<std::string> parse_attribute_value() {
        if (at_end()) return std::nullopt;
        const char q = peek();
        if (q == '"' || q == '\'') {
            advance();
            std::string out;
            while (!at_end() && peek() != q) out.push_back(advance());
            if (at_end()) return std::nullopt;
            advance();
            return out;
        }
        return parse_ident();
    }

    std::optional<std::string> parse_ident() {
        if (at_end() || !is_ident_start(peek())) return std::nullopt;
        std::string out;
        while (!at_end() && is_ident_char(peek())) out.push_back(advance());
        return out;
    }

    bool at_end() const { return pos_ >= text_.size(); }
    char peek() const { return at_end() ? '\0' : text_[pos_]; }
    char advance() { return text_[pos_++]; }

    bool skip_ws() {
        bool any = false;
        while (!at_end() && is_whitespace(peek())) { advance(); any = true; }
        return any;
    }

    std::string_view text_;
    size_t pos_ = 0;
    bool failed_ = false;
};

} // namespace

std::optional<Selector> Selector::parse(std::string_view text) {
    Parser p(text);
    return p.parse_single();
}

Specificity Selector::specificity() const {
    Specificity out;
    for (const auto& part : parts) {
        for (const auto& s : part.compound.simples) {
            switch (s.kind) {
                case SimpleKind::Id:        ++out.a; break;
                case SimpleKind::Class:
                case SimpleKind::Attribute:
                case SimpleKind::PseudoLink:
                case SimpleKind::PseudoVisited:
                case SimpleKind::PseudoHover:
                case SimpleKind::PseudoRoot: ++out.b; break;
                case SimpleKind::Type:      ++out.c; break;
                case SimpleKind::Universal: break;
            }
        }
    }
    return out;
}

std::optional<SelectorList> SelectorList::parse(std::string_view text) {
    Parser p(text);
    return p.parse_list();
}


// =============================================================
// Matcher (Commit 2 of ADR-008).
// =============================================================


namespace {

const dom::Node* previous_sibling(const dom::Node* node) {
    if (!node) return nullptr;
    const dom::Node* parent = node->parent();
    if (!parent) return nullptr;
    const auto& children = parent->children();
    for (size_t i = 0; i < children.size(); ++i) {
        if (children[i].get() == node) {
            return i == 0 ? nullptr : children[i - 1].get();
        }
    }
    return nullptr;
}

bool is_ws(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

bool contains_word(std::string_view text, std::string_view word) {
    if (word.empty()) return false;
    size_t pos = 0;
    while (pos < text.size()) {
        while (pos < text.size() && is_ws(text[pos])) ++pos;
        size_t start = pos;
        while (pos < text.size() && !is_ws(text[pos])) ++pos;
        if (text.substr(start, pos - start) == word) return true;
    }
    return false;
}

bool matches_attribute(const SimpleSelector& s, const dom::Element& el) {
    const auto& attrs = el.attributes();
    auto it = attrs.find(s.name);
    if (it == attrs.end()) return false;
    if (s.attr_match == AttributeMatch::Presence) return true;

    const std::string& value = it->second;
    // Per CSS: for non-Exact operators, an empty match value never matches.
    if (s.attr_value.empty() && s.attr_match != AttributeMatch::Exact) {
        return false;
    }
    switch (s.attr_match) {
        case AttributeMatch::Presence: return true;
        case AttributeMatch::Exact:    return value == s.attr_value;
        case AttributeMatch::Prefix:   return value.starts_with(s.attr_value);
        case AttributeMatch::Suffix:   return value.ends_with(s.attr_value);
        case AttributeMatch::Contains:
            return value.find(s.attr_value) != std::string::npos;
        case AttributeMatch::Word:
            return contains_word(value, s.attr_value);
        case AttributeMatch::Hyphen:
            if (value == s.attr_value) return true;
            return value.size() > s.attr_value.size() &&
                   value.starts_with(s.attr_value) &&
                   value[s.attr_value.size()] == '-';
    }
    return false;
}

bool matches_simple(const SimpleSelector& s, const dom::Element& el,
                    const RenderState& state) {
    switch (s.kind) {
        case SimpleKind::Universal:
            return true;
        case SimpleKind::Type:
            return el.tag() == s.name;
        case SimpleKind::Class: {
            auto it = el.attributes().find("class");
            if (it == el.attributes().end()) return false;
            return contains_word(it->second, s.name);
        }
        case SimpleKind::Id: {
            auto it = el.attributes().find("id");
            return it != el.attributes().end() && it->second == s.name;
        }
        case SimpleKind::Attribute:
            return matches_attribute(s, el);
        case SimpleKind::PseudoLink: {
            if (el.tag() != "a") return false;
            const std::string href = el.get_attribute("href");
            if (href.empty()) return false;
            const std::string resolved = honeybee::resolve_url(href, state.base_url);
            return state.visited_urls.find(resolved) == state.visited_urls.end();
        }
        case SimpleKind::PseudoVisited: {
            if (el.tag() != "a") return false;
            const std::string href = el.get_attribute("href");
            if (href.empty()) return false;
            const std::string resolved = honeybee::resolve_url(href, state.base_url);
            return state.visited_urls.find(resolved) != state.visited_urls.end();
        }
        case SimpleKind::PseudoHover: {
            if (state.hovered_element == nullptr) return false;
            // :hover matches the hovered element AND all its
            // ancestors. Walk up from the hovered element,
            // checking if we pass through &el.
            const dom::Node* n = state.hovered_element;
            while (n) {
                if (n == &el) return true;
                n = n->parent();
            }
            return false;
        }
        case SimpleKind::PseudoRoot: {
            // ADR-026: :root matches the document's root element
            // (<html> in any parsed HTML). Its parent is either
            // nullptr (detached tree) or the Document node.
            const dom::Node* p = el.parent();
            return p == nullptr ||
                   p->type() == dom::NodeType::Document;
        }
    }
    return false;
}

bool matches_compound(const CompoundSelector& c, const dom::Element& el,
                      const RenderState& state) {
    for (const auto& s : c.simples) {
        if (!matches_simple(s, el, state)) return false;
    }
    return true;
}

bool matches_from(const Selector& sel, const dom::Node* node, size_t index,
                  const RenderState& state) {
    if (!node || index >= sel.parts.size()) return false;
    if (node->type() != dom::NodeType::Element) return false;

    const auto* el = static_cast<const dom::Element*>(node);
    if (!matches_compound(sel.parts[index].compound, *el, state)) return false;

    if (index == 0) return true;

    switch (sel.parts[index].combinator) {
        case Combinator::None:
            return false;
        case Combinator::Child:
            return matches_from(sel, node->parent(), index - 1, state);
        case Combinator::Descendant:
            for (const dom::Node* p = node->parent(); p; p = p->parent()) {
                if (matches_from(sel, p, index - 1, state)) return true;
            }
            return false;
        case Combinator::Adjacent:
            return matches_from(sel, previous_sibling(node), index - 1, state);
        case Combinator::Sibling:
            for (const dom::Node* p = previous_sibling(node); p;
                 p = previous_sibling(p)) {
                if (matches_from(sel, p, index - 1, state)) return true;
            }
            return false;
    }
    return false;
}

} // namespace

bool Selector::matches(const dom::Node& node) const {
    return matches(node, RenderState{});
}

std::optional<Specificity>
SelectorList::matches(const dom::Node& node) const {
    return matches(node, RenderState{});
}

bool Selector::matches(const dom::Node& node,
                       const RenderState& state) const {
    if (parts.empty()) return false;
    return matches_from(*this, &node, parts.size() - 1, state);
}

std::optional<Specificity>
SelectorList::matches(const dom::Node& node,
                      const RenderState& state) const {
    std::optional<Specificity> best;
    for (const auto& sel : selectors) {
        if (!sel.matches(node, state)) continue;
        const auto spec = sel.specificity();
        if (!best || *best < spec) best = spec;
    }
    return best;
}

} // namespace honeybee::renderer::css
