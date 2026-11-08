#include "base/log.hpp"
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <queue>
#include <condition_variable>
#include <string>
#include <vector>

#include <atomic>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>


namespace {
    std::mutex g_queue_mutex;
    std::condition_variable g_queue_cv;
    std::queue<std::string> g_queue;
    bool g_logger_running = false;
    std::thread g_logger_thread;
    
    // Ensure the background thread joins cleanly on process exit.
    struct LoggerCleanup {
        ~LoggerCleanup() {
            if (g_logger_running) {
                {
                    std::lock_guard<std::mutex> lock(g_queue_mutex);
                    g_logger_running = false;
                }
                g_queue_cv.notify_all();
                if (g_logger_thread.joinable()) {
                    g_logger_thread.join();
                }
            }
        }
    } g_logger_cleanup;
}

namespace honeybee {
namespace {

std::atomic<bool>          g_enabled{false};
std::atomic<bool>          g_async{false};  // true = production (queue); false = test (sync)
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
    // Parse env vars
    const char* env_cats = std::getenv("HONEYBEE_LOG");
    const char* env_lvl = std::getenv("HONEYBEE_LOG_LEVEL");
    const char* env_file = std::getenv("HONEYBEE_LOG_FILE");

    bool enabled = false;
    std::uint32_t mask = 0;
    LogLevel min_lvl = LogLevel::Debug;
    std::FILE* out = stderr;

    if (env_cats) {
        enabled = true;
        std::string cats(env_cats);
        if (cats == "all") {
            mask = 0xFFFFFFFF;
        } else {
            // simple parser
            size_t start = 0;
            while (start < cats.size()) {
                size_t end = cats.find(',', start);
                if (end == std::string::npos) end = cats.size();
                std::string c = cats.substr(start, end - start);
                if (c == "pipeline") mask |= (1u << static_cast<int>(LogCategory::Pipeline));
                else if (c == "css") mask |= (1u << static_cast<int>(LogCategory::Css));
                else if (c == "cascade") mask |= (1u << static_cast<int>(LogCategory::Cascade));
                else if (c == "layout") mask |= (1u << static_cast<int>(LogCategory::Layout));
                else if (c == "paint") mask |= (1u << static_cast<int>(LogCategory::Paint));
                else if (c == "fetch") mask |= (1u << static_cast<int>(LogCategory::Fetch));
                else if (c == "event") mask |= (1u << static_cast<int>(LogCategory::Event));
                else if (c == "layout_detail") mask |= (1u << static_cast<int>(LogCategory::LayoutDetail));
                else if (c == "paint_detail") mask |= (1u << static_cast<int>(LogCategory::PaintDetail));
                start = end + 1;
            }
        }
    }
    if (env_lvl) {
        std::string l(env_lvl);
        if (l == "error") min_lvl = LogLevel::Error;
        else if (l == "warn") min_lvl = LogLevel::Warn;
        else if (l == "info") min_lvl = LogLevel::Info;
        else if (l == "debug") min_lvl = LogLevel::Debug;
        else if (l == "trace") min_lvl = LogLevel::Trace;
    }
    if (env_file) {
        out = std::fopen(env_file, "a");
        if (!out) out = stderr;
    }

    log_set_config(enabled, mask, min_lvl, out);

    // Production uses async (non-blocking). log_set_config above forces
    // sync for tests; we override here.
    if (enabled) {
        g_async.store(true, std::memory_order_relaxed);
    }

    // Start async writer thread
    if (enabled && !g_logger_running) {
        g_logger_running = true;
        g_logger_thread = std::thread([out]() {
            while (true) {
                std::string msg;
                {
                    std::unique_lock<std::mutex> lock(g_queue_mutex);
                    g_queue_cv.wait(lock, []{ return !g_queue.empty() || !g_logger_running; });
                    if (!g_logger_running && g_queue.empty()) break;
                    if (g_queue.empty()) continue;
                    msg = std::move(g_queue.front());
                    g_queue.pop();
                }
                std::fprintf(out, "%s", msg.c_str());
                std::fflush(out);
            }
        });
            }
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
    g_async.store(false, std::memory_order_relaxed);
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
    char buf[4096];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    char cat_str[32] = "unknown";
    switch (cat) {
        case LogCategory::Pipeline:     strncpy(cat_str, "pipeline", 31); break;
        case LogCategory::Css:          strncpy(cat_str, "css", 31); break;
        case LogCategory::Cascade:      strncpy(cat_str, "cascade", 31); break;
        case LogCategory::Layout:       strncpy(cat_str, "layout", 31); break;
        case LogCategory::Paint:        strncpy(cat_str, "paint", 31); break;
        case LogCategory::Fetch:        strncpy(cat_str, "fetch", 31); break;
        case LogCategory::Event:        strncpy(cat_str, "event", 31); break;
        case LogCategory::LayoutDetail: strncpy(cat_str, "layout_detail", 31); break;
        case LogCategory::PaintDetail:  strncpy(cat_str, "paint_detail", 31); break;
    }
    const char* lvl_str = "unknown";
    switch (lvl) {
        case LogLevel::Error: lvl_str = "error"; break;
        case LogLevel::Warn:  lvl_str = "warn"; break;
        case LogLevel::Info:  lvl_str = "info"; break;
        case LogLevel::Debug: lvl_str = "debug"; break;
        case LogLevel::Trace: lvl_str = "trace"; break;
    }

    // ADR-046: trim absolute paths to start at "src/" for readability.
    // E.g., "/home/user/project/src/foo.cpp" -> "src/foo.cpp".
    // Paths without "/src/" are left unchanged (e.g. "/tmp/generated.cpp").
    std::string file_str(file);
    auto src_pos = file_str.find("/src/");
    if (src_pos != std::string::npos) {
        file_str = file_str.substr(src_pos + 1);  // skip leading '/'
    }

    char full_msg[8192];
    snprintf(full_msg, sizeof(full_msg), "[honeybee][%s][%s] %s:%d %s\n",
             cat_str, lvl_str, file_str.c_str(), line, buf);

    if (g_async.load(std::memory_order_relaxed)) {
        // Production path: enqueue, return immediately (non-blocking)
        {
            std::lock_guard<std::mutex> lock(g_queue_mutex);
            g_queue.push(std::string(full_msg));
        }
        g_queue_cv.notify_one();
    } else {
        // Test path: synchronous write (matches pre-async contract)
        std::lock_guard<std::mutex> lock(g_write_mutex);
        std::fprintf(g_output, "%s", full_msg);
        std::fflush(g_output);
    }
}


void log_shutdown() noexcept {
    {
        std::lock_guard<std::mutex> lock(g_queue_mutex);
        g_logger_running = false;
    }
    g_queue_cv.notify_all();
    if (g_logger_thread.joinable()) {
        g_logger_thread.join();
    }
}
} // namespace honeybee
