#include "renderer/html/css_sources.hpp"

#include <cctype>
#include <functional>
#include <string_view>

namespace honeybee::renderer::html {

namespace {

// Minimal HTML entity decoder. Handles the five standard named
// entities plus numeric decimal and hex forms. Idempotent: if the
// input is already decoded (no "&..." patterns), it comes back
// unchanged. This makes collect_css_sources robust to whether the
// underlying parser (lexbor) surfaces attribute values raw or
// pre-decoded -- both produce the correct output after this step.
std::string decode_html_entities(std::string_view in) {
    std::string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (in[i] != '&') {
            out.push_back(in[i]);
            continue;
        }
        // Named entities: check for the standard set.
        struct Named { const char* name; char value; };
        static constexpr Named kNamed[] = {
            {"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'},
            {"&quot;", '"'}, {"&apos;", '\''}, {"&#39;", '\''},
        };
        bool matched = false;
        for (const auto& n : kNamed) {
            const std::size_t len = std::char_traits<char>::length(n.name);
            if (in.size() - i >= len &&
                in.compare(i, len, n.name) == 0) {
                out.push_back(n.value);
                i += len - 1;
                matched = true;
                break;
            }
        }
        if (matched) continue;
        // Numeric entities: &#NNN; or &#xHHHH;
        if (i + 2 < in.size() && in[i + 1] == '#') {
            std::size_t j = i + 2;
            bool hex = false;
            if (j < in.size() && (in[j] == 'x' || in[j] == 'X')) {
                hex = true;
                ++j;
            }
            std::size_t start = j;
            unsigned long cp = 0;
            while (j < in.size()) {
                const unsigned char c = static_cast<unsigned char>(in[j]);
                int digit;
                if (std::isdigit(c)) digit = c - '0';
                else if (hex && std::isxdigit(c))
                    digit = std::tolower(c) - 'a' + 10;
                else break;
                cp = cp * (hex ? 16UL : 10UL) + static_cast<unsigned long>(digit);
                ++j;
            }
            if (j > start && j < in.size() && in[j] == ';' &&
                cp > 0 && cp < 0x80) {
                out.push_back(static_cast<char>(cp));
                i = j;
                continue;
            }
        }
        // Not a recognized entity: keep the '&' literal.
        out.push_back('&');
    }
    return out;
}

// True iff `rel` (space-separated tokens, case-insensitive per
// HTML spec) contains "stylesheet" and does NOT contain
// "alternate". Alternate stylesheets require UI to activate; we
// skip them rather than misapply them.
bool is_normal_stylesheet_rel(std::string_view rel) {
    bool has_stylesheet = false;
    bool has_alternate = false;
    std::size_t i = 0;
    while (i < rel.size()) {
        while (i < rel.size() && std::isspace(static_cast<unsigned char>(rel[i]))) ++i;
        const std::size_t start = i;
        while (i < rel.size() && !std::isspace(static_cast<unsigned char>(rel[i]))) ++i;
        if (i == start) break;
        std::string token;
        token.reserve(i - start);
        for (std::size_t k = start; k < i; ++k) {
            token.push_back(static_cast<char>(
                std::tolower(static_cast<unsigned char>(rel[k]))));
        }
        if (token == "stylesheet") has_stylesheet = true;
        else if (token == "alternate") has_alternate = true;
    }
    return has_stylesheet && !has_alternate;
}

} // namespace

std::vector<CssSource> collect_css_sources(const dom::Document& doc) {
    std::vector<CssSource> out;
    std::function<void(const dom::Node&)> walk =
        [&](const dom::Node& n) {
            if (n.type() == dom::NodeType::Element) {
                const auto& el = static_cast<const dom::Element&>(n);
                if (el.tag() == "style") {
                    out.push_back({true, el.text_content()});
                } else if (el.tag() == "link") {
                    const std::string rel = el.get_attribute("rel");
                    if (!is_normal_stylesheet_rel(rel)) {
                        // Fall through to children (none for link,
                        // but keeps the walk uniform).
                        return;
                    }
                    const std::string href = el.get_attribute("href");
                    if (href.empty()) return;
                    out.push_back({false, decode_html_entities(href)});
                }
            }
            for (const auto& c : n.children()) walk(*c);
        };
    walk(doc);
    return out;
}

} // namespace honeybee::renderer::html
