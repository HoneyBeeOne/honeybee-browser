#include "renderer/css/stylesheet.hpp"

#include <string>
#include <cctype>
#include <optional>
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
// ADR-028: expand `grid-template: <rows> / <columns>` into two
// longhand declarations (`grid-template-rows`, `grid-template-columns`).
// Follows ADR-012's shorthand expansion pattern.
//
// Conservative scope:
//   - No inline-areas form (`grid-template: 'a a' 1fr / 1fr 1fr`)
//     -- that's deferred per ADR-028's Deferred section. Rules
//     with quotes in the shorthand are dropped (safe).
//   - Requires exactly one top-level `/` separator. Malformed
//     input drops the declaration.
//
// The parser does NOT validate track syntax (e.g. `minmax(0, 1fr)`).
// Track parsing happens at layout time; malformed tracks degrade
// to auto-flow there. This keeps parse-time logic string-only,
// matching how `margin`, `width`, etc. already work.
bool try_expand_grid_template(std::string_view value, bool important,
                              std::vector<Declaration>& out) {
    // Reject inline-areas form (quotes) -- deferred.
    if (value.find('\'') != std::string_view::npos ||
        value.find('"') != std::string_view::npos) {
        return false;
    }

    // Find the first `/` outside any parens. Parenthesized values
    // like minmax(0, 1fr) contain no `/` today, but tracking
    // depth is ~3 lines and safe for future track functions.
    std::size_t slash = std::string_view::npos;
    int depth = 0;
    for (std::size_t i = 0; i < value.size(); ++i) {
        const char c = value[i];
        if (c == '(') ++depth;
        else if (c == ')' && depth > 0) --depth;
        else if (c == '/' && depth == 0) { slash = i; break; }
    }
    if (slash == std::string_view::npos) return false;

    const std::string rows = trim(value.substr(0, slash));
    const std::string cols = trim(value.substr(slash + 1));
    if (rows.empty() || cols.empty()) return false;

    Declaration d_rows;
    d_rows.property = "grid-template-rows";
    d_rows.value = rows;
    d_rows.important = important;
    out.push_back(std::move(d_rows));

    Declaration d_cols;
    d_cols.property = "grid-template-columns";
    d_cols.value = cols;
    d_cols.important = important;
    out.push_back(std::move(d_cols));

    return true;
}

void expand_shorthand(std::string_view property, std::string_view value,
                      bool important, std::vector<Declaration>& out) {
    // ADR-028: grid-template shorthand.
    if (property == "grid-template") {
        if (try_expand_grid_template(value, important, out)) return;
        // Malformed or inline-areas form: drop declaration entirely.
        return;
    }

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
// ADR-027: case-insensitive prefix check at position `i`.
bool starts_with_ci(std::string_view s, std::size_t i, std::string_view prefix) {
    if (i + prefix.size() > s.size()) return false;
    for (std::size_t k = 0; k < prefix.size(); ++k) {
        if (std::tolower(static_cast<unsigned char>(s[i + k])) !=
            std::tolower(static_cast<unsigned char>(prefix[k]))) {
            return false;
        }
    }
    return true;
}

// ADR-027: parse a length that may be `Npx` or `calc(Npx +/- Mpx)`.
// Returns nullopt on anything else (mixed units, more than one
// operation, non-px units, malformed expressions). This narrower
// parser is intentional: ADR-027 v1 supports exactly the calc
// patterns Wikipedia uses (`calc(1120px - 1px)`, etc.). Broader
// calc falls to unsupported -- safe drop, per CSS computed-value
// semantics.
std::optional<float> parse_length_px(std::string_view text) {
    const std::string s = trim(text);
    if (s.empty()) return std::nullopt;

    if (s.rfind("calc(", 0) == 0 && s.back() == ')') {
        const std::string inner = s.substr(5, s.size() - 6);
        // Find first +/- that isn't the leading sign.
        std::size_t op = std::string::npos;
        char op_char = 0;
        for (std::size_t k = 1; k < inner.size(); ++k) {
            if (inner[k] == '-' || inner[k] == '+') {
                op = k;
                op_char = inner[k];
                break;
            }
        }
        if (op == std::string::npos) return std::nullopt;
        const auto lhs = parse_length_px(inner.substr(0, op));
        const auto rhs = parse_length_px(inner.substr(op + 1));
        if (!lhs || !rhs) return std::nullopt;
        return (op_char == '-') ? (*lhs - *rhs) : (*lhs + *rhs);
    }

    if (s.size() >= 3 &&
        s[s.size() - 2] == 'p' && s[s.size() - 1] == 'x') {
        const std::string num = s.substr(0, s.size() - 2);
        try {
            std::size_t idx = 0;
            const float v = std::stof(num, &idx);
            if (idx != num.size()) return std::nullopt;
            return v;
        } catch (...) {
            return std::nullopt;
        }
    }

    return std::nullopt;
}

// ADR-027: classify an @media prelude into width constraints.
// Returns true for supported preludes (populating `out`);
// returns false for unsupported ones, meaning their rules must
// NOT enter the stylesheet (safe drop per CSS spec).
//
// Supported grammar (lowercased, whitespace tolerant):
//   [screen|all] [and (min-width: LEN) | and (max-width: LEN)]*
// where LEN is Npx or calc(Npx +/- Mpx).
// Anything else -- print, prefers-*, aspect-ratio, orientation,
// resolution, hover, pointer, range syntax, mixed-unit calc,
// unsupported keywords -- returns false.
bool parse_media_prelude(std::string_view prelude, MediaQuery& out) {
    std::string lower;
    lower.reserve(prelude.size());
    for (char c : prelude) {
        lower.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(c))));
    }

    // Whole-string unsupported keyword check (fast rejection).
    static const char* kUnsupported[] = {
        "print", "prefers-", "aspect-ratio", "orientation",
        "resolution", "hover", "pointer", ">", "<",
    };
    for (const char* kw : kUnsupported) {
        if (lower.find(kw) != std::string::npos) return false;
    }

    std::size_t i = 0;
    while (i < lower.size()) {
        while (i < lower.size() &&
               std::isspace(static_cast<unsigned char>(lower[i]))) {
            ++i;
        }
        if (i >= lower.size()) break;

        if (lower[i] == '(') {
            // Find the matching ')' at the same paren depth. Naive
            // find(')') would hit the inner close of calc(...) and
            // truncate the condition string.
            std::size_t close = i + 1;
            int paren_depth = 1;
            while (close < lower.size() && paren_depth > 0) {
                if (lower[close] == '(') ++paren_depth;
                else if (lower[close] == ')') --paren_depth;
                ++close;
            }
            if (paren_depth > 0) return false;
            --close;  // back to the ')' character
            const std::string cond =
                trim(lower.substr(i + 1, close - i - 1));

            if (cond.rfind("min-width:", 0) == 0) {
                const auto px = parse_length_px(cond.substr(10));
                if (!px) return false;
                out.min_width_px = *px;
            } else if (cond.rfind("max-width:", 0) == 0) {
                const auto px = parse_length_px(cond.substr(10));
                if (!px) return false;
                out.max_width_px = *px;
            } else {
                return false;
            }
            i = close + 1;
        } else if (std::isalpha(static_cast<unsigned char>(lower[i]))) {
            const std::size_t start = i;
            while (i < lower.size() &&
                   std::isalpha(static_cast<unsigned char>(lower[i]))) {
                ++i;
            }
            const std::string word = lower.substr(start, i - start);
            if (word != "screen" && word != "all" && word != "and" &&
                word != "only") {
                return false;
            }
        } else {
            return false;
        }
    }
    return true;
}

// Forward declaration: parse_media_block needs this, but
// split_declarations is defined below it in the file.
void split_declarations(std::string_view block,
                        std::vector<Declaration>& out);

// ADR-027: parse an @media block starting at `i` (which points at
// '@'). Extracts its body as Rules with a MediaQuery attached. If
// the prelude is unsupported, the whole block is dropped (no rules
// added). Mirrors the top-level rule-parsing loop.
void parse_media_block(std::string_view stripped, std::size_t& i,
                       Stylesheet& result) {
    // Skip "@media" (parse() already verified the prefix).
    i += 6;

    // Read prelude text up to '{'.
    const std::size_t prelude_start = i;
    while (i < stripped.size() && stripped[i] != '{') ++i;
    if (i >= stripped.size()) return;  // malformed: no block
    const std::string prelude =
        trim(stripped.substr(prelude_start, i - prelude_start));
    ++i;  // skip '{'

    // Find the matching '}'.
    const std::size_t body_start = i;
    int depth = 1;
    while (i < stripped.size() && depth > 0) {
        if (stripped[i] == '{') ++depth;
        else if (stripped[i] == '}') --depth;
        ++i;
    }
    if (depth > 0) return;  // malformed: unmatched brace
    const std::string_view body =
        stripped.substr(body_start, i - 1 - body_start);

    MediaQuery mq;
    if (!parse_media_prelude(prelude, mq)) return;  // unsupported -> drop

    // Parse rules inside the body.
    std::size_t j = 0;
    while (j < body.size()) {
        while (j < body.size() && is_ws(body[j])) ++j;
        if (j >= body.size()) break;

        // Nested at-rule: skip (we don't support nested @media).
        if (body[j] == '@') {
            std::size_t skip = j;
            skip_at_rule(body, skip);
            j = skip;
            continue;
        }

        // Selector up to '{' or '}'.
        const std::size_t selector_start = j;
        while (j < body.size() && body[j] != '{' && body[j] != '}') ++j;
        if (j >= body.size()) break;
        if (body[j] == '}') { ++j; continue; }

        const std::string selector_text =
            trim(body.substr(selector_start, j - selector_start));
        ++j;

        // Declarations block.
        const std::size_t decl_start = j;
        int decl_depth = 1;
        while (j < body.size() && decl_depth > 0) {
            if (body[j] == '{') ++decl_depth;
            else if (body[j] == '}') --decl_depth;
            ++j;
        }
        if (decl_depth > 0) break;
        const std::string_view decl_block =
            body.substr(decl_start, j - 1 - decl_start);

        if (selector_text.empty()) continue;
        auto selectors = SelectorList::parse(selector_text);
        if (!selectors) continue;

        Rule rule;
        rule.selectors = std::move(*selectors);
        split_declarations(decl_block, rule.declarations);
        rule.media = mq;
        result.rules.push_back(std::move(rule));
    }
}

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
            // ADR-027: @media blocks are extracted with a MediaQuery;
            // other at-rules (@import, @charset, etc.) still skip.
            if (starts_with_ci(stripped, i, "@media")) {
                parse_media_block(stripped, i, result);
            } else {
                skip_at_rule(stripped, i);
            }
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
