#pragma once

// =============================================================
// File: page_cache.hpp
// Purpose: Session-scoped URL -> (HTML, CSS) cache (ADR-020, ADR-025).
//
// Pure data structure: unordered_map of resolved URL strings to
// fetched content. No GTK, no fetch, no render.
//
// ADR-020 introduced URL -> HTML caching so Back/Forward are
// instant.
//
// ADR-025 extends each entry to also hold the external CSS fetched
// for that page, so Back/Forward re-render with the same stylesheets
// the original load used -- no re-fetch. Reload re-fetches both.
//
// Backward-compatible surface:
//   put(url, html)               -- external_css = ""
//   get(url) -> optional<string> -- returns just the html
// Additive surface:
//   put(url, html, external_css)
//   get_entry(url) -> optional<CacheEntry>
// =============================================================

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

namespace honeybee::browser {

struct CacheEntry {
    std::string html;
    std::string external_css;
};

class PageCache {
public:
    // ---- ADR-020 compat surface ----

    // Store (or overwrite) HTML for a URL; external_css is cleared.
    void put(const std::string& url, std::string html) {
        put(url, std::move(html), std::string{});
    }

    // Return the cached HTML for `url`, or nullopt if absent.
    std::optional<std::string> get(const std::string& url) const {
        auto e = get_entry(url);
        if (!e) return std::nullopt;
        return std::move(e->html);
    }

    // ---- ADR-025 additive surface ----

    // Store (or overwrite) HTML + external CSS for a URL.
    // Overwriting replaces both fields.
    void put(const std::string& url, std::string html,
             std::string external_css) {
        auto it = entries_.find(url);
        if (it != entries_.end()) {
            bytes_ -= it->second.html.size() +
                      it->second.external_css.size();
            it->second.html = std::move(html);
            it->second.external_css = std::move(external_css);
        } else {
            auto [new_it, _] = entries_.emplace(
                url, CacheEntry{std::move(html), std::move(external_css)});
            it = new_it;
        }
        bytes_ += it->second.html.size() +
                  it->second.external_css.size();
    }

    // Return the full (html, external_css) entry for `url`,
    // or nullopt if absent.
    std::optional<CacheEntry> get_entry(const std::string& url) const {
        auto it = entries_.find(url);
        if (it == entries_.end()) return std::nullopt;
        return it->second;
    }

    // ---- Unchanged surface ----

    bool contains(const std::string& url) const {
        return entries_.find(url) != entries_.end();
    }

    void clear() {
        entries_.clear();
        bytes_ = 0;
    }

    std::size_t size() const { return entries_.size(); }

    // Total bytes of all cached HTML + external CSS bodies.
    // O(1) thanks to the maintained counter.
    std::size_t bytes() const { return bytes_; }

private:
    std::unordered_map<std::string, CacheEntry> entries_;
    std::size_t bytes_ = 0;
};

} // namespace honeybee::browser
