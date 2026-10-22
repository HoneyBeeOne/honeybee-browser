#pragma once

// =============================================================
// File: page_cache.hpp
// Purpose: Session-scoped URL -> HTML cache (ADR-020).
//
// Pure data structure: unordered_map of resolved URL strings
// to fetched HTML bodies. No GTK, no fetch, no render.
// HoneyBeeWindow populates on every successful fetch and
// consults it only for NavKind::Back / Forward, so returning
// to a previous page is instant instead of re-fetching.
// =============================================================

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>

namespace honeybee::browser {

class PageCache {
public:
    // Store (or overwrite) the HTML for a URL. Overwriting is
    // intentional -- a Reload success replaces the previous
    // body so a subsequent Back to the same URL returns the
    // refreshed content.
    void put(const std::string& url, std::string html) {
        bytes_ -= entries_[url].size();
        auto [it, _] = entries_.insert_or_assign(url, std::move(html));
        bytes_ += it->second.size();
    }

    // Return the stored HTML for `url`, or nullopt if not cached.
    std::optional<std::string> get(const std::string& url) const {
        auto it = entries_.find(url);
        if (it == entries_.end()) return std::nullopt;
        return it->second;
    }

    bool contains(const std::string& url) const {
        return entries_.find(url) != entries_.end();
    }

    void clear() {
        entries_.clear();
        bytes_ = 0;
    }

    std::size_t size() const { return entries_.size(); }

    // Total bytes of all cached HTML bodies. O(1) thanks to the
    // maintained counter -- for future eviction diagnostics.
    std::size_t bytes() const { return bytes_; }

private:
    std::unordered_map<std::string, std::string> entries_;
    std::size_t bytes_ = 0;
};

} // namespace honeybee::browser
