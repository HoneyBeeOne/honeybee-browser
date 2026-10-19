#include "renderer/css/selectors.hpp"

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
                case SimpleKind::Attribute: ++out.b; break;
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

} // namespace honeybee::renderer::css
