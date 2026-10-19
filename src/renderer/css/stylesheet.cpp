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

        if (depth > 0) {
            break;
        }

        ++i;

        if (selector_text.empty()) continue;

        auto selectors = SelectorList::parse(selector_text);
        if (!selectors) continue;

        Rule rule;
        rule.selectors = std::move(*selectors);
        result.rules.push_back(std::move(rule));
    }

    return result;
}

} // namespace honeybee::renderer::css
