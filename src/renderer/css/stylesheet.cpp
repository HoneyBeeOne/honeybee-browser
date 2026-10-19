#include "renderer/css/stylesheet.hpp"

#include <string>
#include <string_view>
#include <utility>

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

// Detect and strip a trailing !important. Case-insensitive per
// CSS spec, and whitespace between '!' and 'important' is allowed.
// Returns true if !important was present (and removed).
bool strip_important(std::string& value) {
    const size_t e = value.size();
    // Find last '!'
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

    // Strip !important plus any trailing whitespace before it.
    size_t new_e = bang;
    while (new_e > 0 && is_ws(value[new_e - 1])) --new_e;
    value.resize(new_e);
    return true;
}

// Parse one declaration; malformed chunks are skipped silently.
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

    Declaration d;
    d.property = std::move(property);
    d.value = std::move(value);
    d.important = important;
    out.push_back(std::move(d));
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
        result.rules.push_back(std::move(rule));
    }

    return result;
}

} // namespace honeybee::renderer::css
