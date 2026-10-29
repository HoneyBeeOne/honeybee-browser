#pragma once

// ADR-046: runtime-gated logging infrastructure.
//
// Usage:
//     HB_LOG(Layout, Debug, "child_count=%d", n);
//
// Enable at runtime via env vars:
//     HONEYBEE_LOG=layout,paint,fetch   (or 'all')
//     HONEYBEE_LOG_LEVEL=debug           (default: debug)
//     HONEYBEE_LOG_FILE=/tmp/hb.log     (default: stderr)
//
// When disabled (HONEYBEE_LOG unset), each HB_LOG call is a
// single atomic bool read + branch -- no formatting, no
// allocation, no mutex.

#include <cstdint>
#include <cstdio>

namespace honeybee {

enum class LogCategory : std::uint8_t {
    Pipeline,   // main app, GTK lifecycle, load sequencing
    Css,        // stylesheet parsing, rule extraction
    Cascade,    // matching, specificity, computed styles
    Layout,     // block / grid / flex / inline formatting
    Paint,      // box paint, images, clips
    Fetch,      // HTTP requests, CSS + image fetches
    Event,      // click, wheel, hover, keyboard
};

enum class LogLevel : std::uint8_t {
    Error = 0,
    Warn  = 1,
    Info  = 2,
    Debug = 3,
    Trace = 4,
};

// Read env vars once and configure. Idempotent; safe to call
// from main() before any workers start. On any parse failure,
// logging is disabled (safe default).
void log_init() noexcept;

// Programmatic config (for tests). Bypasses env vars entirely.
// category_mask: bitmask, bit N = LogCategory with value N.
//   e.g. (1u << Layout) | (1u << Paint)
// Log at level L if L <= min_level (numeric comparison).
// output: FILE* for output (nullptr = stderr).
void log_set_config(bool enabled,
                    std::uint32_t category_mask,
                    LogLevel min_level,
                    std::FILE* output) noexcept;

// Hot-path check. noexcept; reads three relaxed atomics.
bool log_enabled(LogCategory cat, LogLevel lvl) noexcept;

// Actual write. Thread-safe (mutex + vfprintf). Called only
// when log_enabled returned true (via HB_LOG macro). Not
// intended for direct user calls.
void log_writev(LogCategory cat, LogLevel lvl,
                const char* file, int line,
                const char* fmt, ...) noexcept;

} // namespace honeybee

// User-facing macro. Zero cost when disabled.
#define HB_LOG(cat, lvl, fmt, ...)                                     \
    do {                                                               \
        if (::honeybee::log_enabled(::honeybee::LogCategory::cat,      \
                                    ::honeybee::LogLevel::lvl)) {      \
            ::honeybee::log_writev(::honeybee::LogCategory::cat,       \
                ::honeybee::LogLevel::lvl, __FILE__, __LINE__,         \
                fmt, ##__VA_ARGS__);                                   \
        }                                                              \
    } while (0)
