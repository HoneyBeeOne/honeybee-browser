#pragma once

// =============================================================
// File: navigation_history.hpp
// Purpose: Session-scoped back/forward history (ADR-019).
//
// A pure data structure: vector of URLs + cursor. No GTK, no
// fetch, no render. HoneyBeeWindow dispatches navigation through
// this; tests exercise the edge cases (truncate-on-push,
// back-at-start, forward-at-end) without a running GUI.
// =============================================================

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace honeybee::browser {

class NavigationHistory {
public:
    // Push a new URL. Truncates any forward history (entries
    // after the cursor), then appends. Cursor lands on the new
    // entry.
    void push(const std::string& url) {
        if (index_ + 1 < entries_.size()) {
            entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(index_ + 1),
                           entries_.end());
        }
        entries_.push_back(url);
        index_ = entries_.size() - 1;
    }

    bool can_back() const { return index_ > 0; }
    bool can_forward() const { return index_ + 1 < entries_.size(); }
    bool empty() const { return entries_.empty(); }

    // Caller must check !empty() first. Returns reference to
    // the current URL, so no copy on the hot path.
    const std::string& current() const { return entries_[index_]; }

    // Move the cursor. Returns the newly-current URL, or nullopt
    // when the move is impossible (already at start / end).
    // Callers can distinguish "moved" from "not possible" without
    // a separate can_* check.
    std::optional<std::string> back() {
        if (!can_back()) return std::nullopt;
        --index_;
        return entries_[index_];
    }

    std::optional<std::string> forward() {
        if (!can_forward()) return std::nullopt;
        ++index_;
        return entries_[index_];
    }

    void clear() {
        entries_.clear();
        index_ = 0;
    }

    std::size_t size() const { return entries_.size(); }
    std::size_t index() const { return index_; }

private:
    std::vector<std::string> entries_;
    std::size_t index_ = 0;
};

} // namespace honeybee::browser
