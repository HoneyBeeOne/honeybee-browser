#include "base/log.hpp"

#include <atomic>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

namespace honeybee {
namespace {

std::atomic<bool>          g_enabled{false};
std::atomic<std::uint32_t> g_category_mask{0};
std::atomic<LogLevel>      g_min_level{LogLevel::Debug};
std::FILE*                 g_output = stderr;  // guarded by g_write_mutex
std::mutex                 g_write_mutex;

const char* category_name(LogCategory c) {
    switch (c) {
        case LogCategory::Pipeline: return "pipeline";
        case LogCategory::Css:      return "css";
        case LogCategory::Cascade:  return "cascade";
        case LogCategory::Layout:   return "layout";
        case LogCategory::Paint:    return "paint";
        case LogCategory::Fetch:    return "fetch";
        case LogCategory::Event:    return "event";
    }
    return "?";
}

const char* level_name(LogLevel l) {
    switch (l) {
        case LogLevel::Error: return "error";
        case LogLevel::Warn:  return "warn";
        case LogLevel::Info:  return "info";
        case LogLevel::Debug: return "debug";
        case LogLevel::Trace: return "trace";
    }
    return "?";
}

// Shorten "/home/user/project/src/renderer/layout/layout.cpp"
// to "src/renderer/layout/layout.cpp". Paths without "/src/"
// (or already starting with "src/") are returned unchanged.
const char* trim_path(const char* file) {
    if (!file) return "?";
    if (const char* p = std::strstr(file, "/src/")) return p + 1;
    return file;
}

std::uint32_t category_bit(const std::string& t) {
    if (t == "pipeline") return 1u << static_cast<int>(LogCategory::Pipeline);
    if (t == "css")      return 1u << static_cast<int>(LogCategory::Css);
    if (t == "cascade")  return 1u << static_cast<int>(LogCategory::Cascade);
    if (t == "layout")   return 1u << static_cast<int>(LogCategory::Layout);
    if (t == "paint")    return 1u << static_cast<int>(LogCategory::Paint);
    if (t == "fetch")    return 1u << static_cast<int>(LogCategory::Fetch);
    if (t == "event")    return 1u << static_cast<int>(LogCategory::Event);
    return 0;
}

// Parse "layout,paint,fetch" or "all". Returns 0 on any
// unrecognized tokens (disables logging for safety).
std::uint32_t parse_category_mask(const char* s) {
    if (!s || !*s) return 0;
    if (std::strcmp(s, "all") == 0) {
        std::uint32_t m = 0;
        for (int i = 0; i <= static_cast<int>(LogCategory::Event); ++i) {
            m |= 1u << i;
        }
        return m;
    }
    std::uint32_t mask = 0;
    std::string token;
    for (const char* p = s; ; ++p) {
        if (*p == ',' || *p == '\0') {
            mask |= category_bit(token);
            token.clear();
            if (*p == '\0') break;
        } else {
            token.push_back(*p);
        }
    }
    return mask;
}

LogLevel parse_level(const char* s) {
    if (!s) return LogLevel::Debug;
    if (std::strcmp(s, "error") == 0) return LogLevel::Error;
    if (std::strcmp(s, "warn")  == 0) return LogLevel::Warn;
    if (std::strcmp(s, "info")  == 0) return LogLevel::Info;
    if (std::strcmp(s, "debug") == 0) return LogLevel::Debug;
    if (std::strcmp(s, "trace") == 0) return LogLevel::Trace;
    return LogLevel::Debug;
}

} // namespace

void log_init() noexcept {
    const char* cats = std::getenv("HONEYBEE_LOG");
    if (!cats || !*cats) {
        g_enabled.store(false, std::memory_order_release);
        return;
    }
    const std::uint32_t mask = parse_category_mask(cats);
    if (mask == 0) {
        g_enabled.store(false, std::memory_order_release);
        return;
    }
    const LogLevel lvl = parse_level(std::getenv("HONEYBEE_LOG_LEVEL"));

    std::FILE* out = stderr;
    if (const char* path = std::getenv("HONEYBEE_LOG_FILE");
        path && *path) {
        if (std::FILE* f = std::fopen(path, "w")) out = f;
        // fopen failure: fall back to stderr (don't disable).
    }

    g_category_mask.store(mask, std::memory_order_relaxed);
    g_min_level.store(lvl, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(g_write_mutex);
        g_output = out;
    }
    g_enabled.store(true, std::memory_order_release);
}

void log_set_config(bool enabled,
                    std::uint32_t category_mask,
                    LogLevel min_level,
                    std::FILE* output) noexcept {
    g_category_mask.store(category_mask, std::memory_order_relaxed);
    g_min_level.store(min_level, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(g_write_mutex);
        g_output = output ? output : stderr;
    }
    g_enabled.store(enabled, std::memory_order_release);
}

bool log_enabled(LogCategory cat, LogLevel lvl) noexcept {
    if (!g_enabled.load(std::memory_order_acquire)) return false;
    const auto mask = g_category_mask.load(std::memory_order_relaxed);
    if (!(mask & (1u << static_cast<int>(cat)))) return false;
    return static_cast<int>(lvl) <=
           static_cast<int>(g_min_level.load(std::memory_order_relaxed));
}

void log_writev(LogCategory cat, LogLevel lvl,
                const char* file, int line,
                const char* fmt, ...) noexcept {
    std::lock_guard<std::mutex> lock(g_write_mutex);
    std::FILE* out = g_output ? g_output : stderr;
    std::fprintf(out, "[honeybee][%s][%s] %s:%d ",
                 category_name(cat), level_name(lvl),
                 trim_path(file), line);
    va_list args;
    va_start(args, fmt);
    std::vfprintf(out, fmt, args);
    va_end(args);
    std::fputc('\n', out);
    std::fflush(out);
}

} // namespace honeybee
