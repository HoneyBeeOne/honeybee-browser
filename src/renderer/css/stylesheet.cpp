#include "renderer/css/stylesheet.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace honeybee::renderer::css {

namespace {

bool is_ws(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

std::string strip_comments(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        if (i + 1 < s.size() && s[i] == '/' && s[i + 1] == '*') {
            i += 2;
            while (i + 1 < s.size() && !(s[i] == '*' && s[i + 1] == '/')) {
                ++i;
            }
            if (i + 1 < s.size()) {
                i += 2;
            } else {
                i = s.size();
            }
        } else {
            out.push_back(s[i]);
            ++i;
        }
    }
    return out;
}

void skip_at_rule(std::string_view s, size_t& i) {
    ++i;
    int depth = 0;
    while (i < s.size()) {
        const char c = s[i];
        if (c == ';' && depth == 0) { ++i; return; }
        if (c == '{') {
            ++depth;
        } else if (c == '}') {
            --depth;
            if (depth <= 0) { ++i; return; }
        }
        ++i;
    }
}

std::string trim(std::string_view s) {
    size_t b = 0, e = s.size();
    while (b < e && is_ws(s[b])) ++b;
    while (e > b && is_ws(s[e - 1])) --e;
    return std::string(s.substr(b, e - b));
}

// ADR-026: whole-value var() resolution.
//
// Given a declaration value, return one of three states:
//   NotVar    -- value doesn't start with "var(...)", leave alone
//   Resolved  -- value is whole-value var(), resolved to `result.value`
//   Failed    -- value is whole-value var() that can't resolve
//                (undefined, no fallback, or nested) -- declaration
//                should be dropped per CSS computed-value-time semantics
//
// Whole-value only: partial substitutions (e.g. `1px solid var(--x)`)
// return NotVar and are left unchanged. ADR-026's measurement showed
// partial-value declarations target consumed properties only 20% of
// the time; the 40-line whole-value resolver captures 89% of the
// visible win at half the complexity of substitution + calc.
struct VarResult {
    enum Kind { NotVar, Resolved, Failed };
    Kind kind;
    std::string value;  // valid when kind == Resolved
};

VarResult resolve_var_whole(
    std::string_view raw,
    const std::unordered_map<std::string, std::string>& vars) {
    const std::string value = trim(raw);

    // Fast path: not a var() at all.
    if (value.size() < 5 ||
        value.compare(0, 4, "var(") != 0 ||
        value.back() != ')') {
        return {VarResult::NotVar, {}};
    }

    // Extract inside of var(...): strip "var(" prefix and ")" suffix.
    const std::string inner_full =
        trim(std::string_view(value).substr(4, value.size() - 5));

    // Find first top-level comma (fallback separation). Commas can
    // appear inside the fallback expression (e.g. rgb(0, 0, 0)) so
    // we track paren depth.
    std::size_t depth = 0;
    std::size_t comma = std::string::npos;
    for (std::size_t i = 0; i < inner_full.size(); ++i) {
        const char c = inner_full[i];
        if (c == '(') ++depth;
        else if (c == ')' && depth > 0) --depth;
        else if (c == ',' && depth == 0) { comma = i; break; }
    }

    std::string var_name;
    std::string fallback;
    bool has_fallback = false;
    if (comma == std::string::npos) {
        var_name = trim(inner_full);
    } else {
        var_name = trim(inner_full.substr(0, comma));
        fallback = trim(inner_full.substr(comma + 1));
        has_fallback = true;
    }

    // Malformed: name must start with "--".
    if (var_name.size() < 2 ||
        var_name[0] != '-' || var_name[1] != '-') {
        return {VarResult::Failed, {}};
    }

    // Variable lookup.
    auto it = vars.find(var_name);
    if (it != vars.end()) {
        // Nested var() in the variable's value: single-pass
        // resolution can't follow it. If a fallback exists, use
        // it; otherwise drop (same as unresolved).
        if (it->second.find("var(") != std::string::npos) {
            if (has_fallback) return {VarResult::Resolved, fallback};
            return {VarResult::Failed, {}};
        }
        return {VarResult::Resolved, it->second};
    }

    // Variable not defined: use fallback, or fail.
    if (has_fallback) return {VarResult::Resolved, fallback};
    return {VarResult::Failed, {}};
}

bool strip_important(std::string& value) {
    const size_t e = value.size();
    size_t bang = std::string::npos;
    for (size_t k = e; k > 0; --k) {
        if (value[k - 1] == '!') { bang = k - 1; break; }
    }
    if (bang == std::string::npos) return false;

    size_t j = bang + 1;
    while (j < e && is_ws(value[j])) ++j;

    constexpr const char* kTarget = "important";
    constexpr size_t kLen = 9;
    if (e - j != kLen) return false;

    for (size_t k = 0; k < kLen; ++k) {
        char c = value[j + k];
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (c != kTarget[k]) return false;
    }

    size_t new_e = bang;
    while (new_e > 0 && is_ws(value[new_e - 1])) --new_e;
    value.resize(new_e);
    return true;
}

// Split on whitespace into tokens. Returns one token per
// contiguous non-whitespace run.
std::vector<std::string> split_tokens(std::string_view s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && is_ws(s[i])) ++i;
        if (i >= s.size()) break;
        const size_t start = i;
        while (i < s.size() && !is_ws(s[i])) ++i;
        out.emplace_back(s.substr(start, i - start));
    }
    return out;
}

bool is_box_shorthand(std::string_view prop) {
    return prop == "margin" || prop == "padding" ||
           prop == "border-width";
}

// Push one declaration, expanding if it's a box shorthand.
// ADR-012: 1/2/3/4 tokens -> top/right/bottom/left longhands.
void expand_shorthand(std::string_view property, std::string_view value,
                      bool important, std::vector<Declaration>& out) {
    if (!is_box_shorthand(property)) {
        Declaration d;
        d.property = std::string(property);
        d.value = std::string(value);
        d.important = important;
        out.push_back(std::move(d));
        return;
    }

    const auto tokens = split_tokens(value);
    if (tokens.empty() || tokens.size() > 4) return;

    std::string top, right, bottom, left;
    switch (tokens.size()) {
        case 1:
            top = right = bottom = left = tokens[0];
            break;
        case 2:
            top = bottom = tokens[0];
            right = left = tokens[1];
            break;
        case 3:
            top = tokens[0];
            right = left = tokens[1];
            bottom = tokens[2];
            break;
        case 4:
            top = tokens[0];
            right = tokens[1];
            bottom = tokens[2];
            left = tokens[3];
            break;
    }

    const char* longhand_names[4] = {nullptr, nullptr, nullptr, nullptr};
    if (property == "margin") {
        longhand_names[0] = "margin-top";
        longhand_names[1] = "margin-right";
        longhand_names[2] = "margin-bottom";
        longhand_names[3] = "margin-left";
    } else if (property == "padding") {
        longhand_names[0] = "padding-top";
        longhand_names[1] = "padding-right";
        longhand_names[2] = "padding-bottom";
        longhand_names[3] = "padding-left";
    } else if (property == "border-width") {
        longhand_names[0] = "border-top-width";
        longhand_names[1] = "border-right-width";
        longhand_names[2] = "border-bottom-width";
        longhand_names[3] = "border-left-width";
    }

    const std::string* values[4] = {&top, &right, &bottom, &left};
    for (int k = 0; k < 4; ++k) {
        Declaration d;
        d.property = longhand_names[k];
        d.value = *values[k];
        d.important = important;
        out.push_back(std::move(d));
    }
}

void add_declaration(std::string_view raw, std::vector<Declaration>& out) {
    const std::string trimmed = trim(raw);
    if (trimmed.empty()) return;

    const size_t colon = trimmed.find(':');
    if (colon == std::string::npos) return;

    std::string property = trim(trimmed.substr(0, colon));
    std::string value = trim(trimmed.substr(colon + 1));

    if (property.empty() || value.empty()) return;

    const bool important = strip_important(value);
    if (value.empty()) return;

    expand_shorthand(property, value, important, out);
}

// Split a declaration block on top-level ';', respecting nested
// parentheses/brackets and single/double quotes.
void split_declarations(std::string_view block, std::vector<Declaration>& out) {
    size_t start = 0;
    int depth = 0;
    char quote = 0;
    for (size_t i = 0; i <= block.size(); ++i) {
        if (i == block.size()) {
            add_declaration(block.substr(start), out);
            break;
        }
        const char c = block[i];
        if (quote) {
            if (c == quote) quote = 0;
        } else if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == '(' || c == '[') {
            ++depth;
        } else if (c == ')' || c == ']') {
            if (depth > 0) --depth;
        } else if (c == ';' && depth == 0) {
            add_declaration(block.substr(start, i - start), out);
            start = i + 1;
        }
    }
}

} // namespace

Stylesheet Stylesheet::parse(std::string_view text) {
    Stylesheet result;
    const std::string stripped = strip_comments(text);

    size_t i = 0;
    while (i < stripped.size()) {
        while (i < stripped.size() && is_ws(stripped[i])) ++i;
        if (i >= stripped.size()) break;

        if (stripped[i] == '@') {
            skip_at_rule(stripped, i);
            continue;
        }

        const size_t selector_start = i;
        while (i < stripped.size() &&
               stripped[i] != '{' && stripped[i] != '}') {
            ++i;
        }

        if (i >= stripped.size()) break;

        if (stripped[i] == '}') {
            ++i;
            continue;
        }

        const std::string selector_text =
            trim(stripped.substr(selector_start, i - selector_start));
        ++i;

        const size_t block_start = i;
        int depth = 1;
        while (i < stripped.size() && depth > 0) {
            if (stripped[i] == '{') {
                ++depth;
                ++i;
            } else if (stripped[i] == '}') {
                --depth;
                if (depth > 0) ++i;
            } else {
                ++i;
            }
        }

        if (depth > 0) break;

        const std::string_view block =
            std::string_view(stripped).substr(block_start, i - block_start);
        ++i;

        if (selector_text.empty()) continue;

        auto selectors = SelectorList::parse(selector_text);
        if (!selectors) continue;

        Rule rule;
        rule.selectors = std::move(*selectors);
        split_declarations(block, rule.declarations);
        // ADR-026: extract custom-property declarations from
        // :root-only rules into the stylesheet's variable map.
        //
        // Conservative trigger: exactly one selector, exactly one
        // simple, that simple is PseudoRoot. This covers the
        // canonical `:root { --x: val }` case. Multi-selector
        // rules like `:root, .foo { --x: val }` are NOT extracted
        // (their variable scoping is more complex than :root-only;
        // deferred until custom-property inheritance is a v1
        // feature).
        if (rule.selectors.selectors.size() == 1) {
            const auto& sel = rule.selectors.selectors[0];
            if (sel.parts.size() == 1 &&
                sel.parts[0].compound.simples.size() == 1 &&
                sel.parts[0].compound.simples[0].kind ==
                    SimpleKind::PseudoRoot) {
                for (const auto& decl : rule.declarations) {
                    if (decl.property.size() >= 2 &&
                        decl.property[0] == '-' &&
                        decl.property[1] == '-') {
                        result.variables[decl.property] = decl.value;
                    }
                }
            }
        }

        result.rules.push_back(std::move(rule));
    }

    // ADR-026: resolve var() in all declarations using the
    // collected variable map. Runs once, after all rules have
    // been parsed (variables need to be complete).
    //
    // Skips custom-property declarations themselves (--x: val);
    // their values may be recursive or refer to variables defined
    // later, and single-pass resolution would either miss or
    // misinterpret. Deferred recursive resolution (ADR-026
    // Deferred).
    for (auto& rule : result.rules) {
        auto decl_it = rule.declarations.begin();
        while (decl_it != rule.declarations.end()) {
            const auto& prop = decl_it->property;
            const bool is_custom_property =
                prop.size() >= 2 && prop[0] == '-' && prop[1] == '-';
            if (is_custom_property) {
                ++decl_it;
                continue;
            }
            auto vr = resolve_var_whole(decl_it->value, result.variables);
            switch (vr.kind) {
                case VarResult::NotVar:
                    ++decl_it;
                    break;
                case VarResult::Resolved:
                    decl_it->value = std::move(vr.value);
                    ++decl_it;
                    break;
                case VarResult::Failed:
                    decl_it = rule.declarations.erase(decl_it);
                    break;
            }
        }
    }

    return result;
}

} // namespace honeybee::renderer::css
