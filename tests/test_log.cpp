// =============================================================
// ADR-046: logging infrastructure tests.
// =============================================================

#include "base/log.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {

// RAII: reset log config to disabled at scope exit so tests
// don't leak global state.
struct ResetLog {
    ~ResetLog() {
        honeybee::log_set_config(false, 0u,
                                 honeybee::LogLevel::Error, stderr);
    }
};

// RAII: restore an env var to its prior state (or unset) at
// scope exit.
struct EnvScope {
    std::string name;
    bool had_original = false;
    std::string original;

    explicit EnvScope(const char* n) : name(n) {
        if (const char* v = std::getenv(n)) {
            had_original = true;
            original = v;
        }
    }
    ~EnvScope() {
        if (had_original) {
            setenv(name.c_str(), original.c_str(), 1);
        } else {
            unsetenv(name.c_str());
        }
    }
};

// Read all bytes from a FILE* (after flushing).
std::string read_file(std::FILE* f) {
    std::fflush(f);
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    if (size <= 0) return "";
    std::fseek(f, 0, SEEK_SET);
    std::string out(static_cast<size_t>(size), '\0');
    const size_t n = std::fread(out.data(), 1, out.size(), f);
    out.resize(n);
    return out;
}

// Count newlines in a string.
size_t count_lines(const std::string& s) {
    size_t n = 0;
    for (char c : s) if (c == '\n') ++n;
    return n;
}

constexpr auto kLayoutBit = 1u <<
    static_cast<int>(honeybee::LogCategory::Layout);
constexpr auto kPaintBit = 1u <<
    static_cast<int>(honeybee::LogCategory::Paint);
constexpr auto kCssBit = 1u <<
    static_cast<int>(honeybee::LogCategory::Css);

} // namespace

TEST_CASE("log: disabled config -> log_enabled false everywhere",
          "[log][adr-046]") {
    ResetLog r;
    honeybee::log_set_config(false, 0xFFFFFFFFu,
                             honeybee::LogLevel::Trace, stderr);
    CHECK_FALSE(honeybee::log_enabled(
        honeybee::LogCategory::Layout, honeybee::LogLevel::Error));
    CHECK_FALSE(honeybee::log_enabled(
        honeybee::LogCategory::Layout, honeybee::LogLevel::Trace));
}

TEST_CASE("log: mask enables only specified categories",
          "[log][adr-046]") {
    ResetLog r;
    honeybee::log_set_config(true, kLayoutBit | kPaintBit,
                             honeybee::LogLevel::Debug, stderr);
    CHECK(honeybee::log_enabled(
        honeybee::LogCategory::Layout, honeybee::LogLevel::Debug));
    CHECK(honeybee::log_enabled(
        honeybee::LogCategory::Paint, honeybee::LogLevel::Debug));
    CHECK_FALSE(honeybee::log_enabled(
        honeybee::LogCategory::Css, honeybee::LogLevel::Debug));
}

TEST_CASE("log: level threshold excludes severities above min",
          "[log][adr-046]") {
    ResetLog r;
    honeybee::log_set_config(true, kLayoutBit,
                             honeybee::LogLevel::Info, stderr);
    // Error(0), Warn(1), Info(2) at or below threshold.
    CHECK(honeybee::log_enabled(
        honeybee::LogCategory::Layout, honeybee::LogLevel::Error));
    CHECK(honeybee::log_enabled(
        honeybee::LogCategory::Layout, honeybee::LogLevel::Warn));
    CHECK(honeybee::log_enabled(
        honeybee::LogCategory::Layout, honeybee::LogLevel::Info));
    // Debug(3), Trace(4) above threshold.
    CHECK_FALSE(honeybee::log_enabled(
        honeybee::LogCategory::Layout, honeybee::LogLevel::Debug));
    CHECK_FALSE(honeybee::log_enabled(
        honeybee::LogCategory::Layout, honeybee::LogLevel::Trace));
}

TEST_CASE("log_writev emits correctly formatted line",
          "[log][adr-046]") {
    ResetLog r;
    std::FILE* tmp = std::tmpfile();
    REQUIRE(tmp != nullptr);
    honeybee::log_set_config(true, kLayoutBit,
                             honeybee::LogLevel::Debug, tmp);
    honeybee::log_writev(honeybee::LogCategory::Layout,
                         honeybee::LogLevel::Debug,
                         "src/renderer/layout/layout.cpp", 482,
                         "child_count=%d", 3);
    const std::string out = read_file(tmp);
    std::fclose(tmp);

    CHECK(out.find("[honeybee][layout][debug]") != std::string::npos);
    CHECK(out.find("src/renderer/layout/layout.cpp:482") !=
          std::string::npos);
    CHECK(out.find("child_count=3") != std::string::npos);
    CHECK(count_lines(out) == 1);
}

TEST_CASE("log_writev trims absolute path at /src/",
          "[log][adr-046]") {
    ResetLog r;
    std::FILE* tmp = std::tmpfile();
    REQUIRE(tmp != nullptr);
    honeybee::log_set_config(true, kLayoutBit,
                             honeybee::LogLevel::Debug, tmp);
    honeybee::log_writev(honeybee::LogCategory::Layout,
                         honeybee::LogLevel::Debug,
                         "/home/user/project/src/renderer/layout/layout.cpp",
                         42, "x");
    const std::string out = read_file(tmp);
    std::fclose(tmp);

    CHECK(out.find("src/renderer/layout/layout.cpp:42") !=
          std::string::npos);
    CHECK(out.find("/home/user/project/") == std::string::npos);
}

TEST_CASE("log_writev leaves non-src path unchanged",
          "[log][adr-046]") {
    ResetLog r;
    std::FILE* tmp = std::tmpfile();
    REQUIRE(tmp != nullptr);
    honeybee::log_set_config(true, kLayoutBit,
                             honeybee::LogLevel::Debug, tmp);
    honeybee::log_writev(honeybee::LogCategory::Layout,
                         honeybee::LogLevel::Debug,
                         "/tmp/generated.cpp", 17, "x");
    const std::string out = read_file(tmp);
    std::fclose(tmp);

    CHECK(out.find("/tmp/generated.cpp:17") != std::string::npos);
}

TEST_CASE("log: multithreaded writes produce complete lines",
          "[log][adr-046]") {
    ResetLog r;
    std::FILE* tmp = std::tmpfile();
    REQUIRE(tmp != nullptr);
    honeybee::log_set_config(true, kLayoutBit,
                             honeybee::LogLevel::Debug, tmp);

    constexpr int kThreads = 4;
    constexpr int kPerThread = 50;
    std::vector<std::thread> threads;
    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([i]() {
            for (int n = 0; n < kPerThread; ++n) {
                HB_LOG(Layout, Debug, "thread=%d n=%d", i, n);
            }
        });
    }
    for (auto& t : threads) t.join();

    const std::string out = read_file(tmp);
    std::fclose(tmp);

    // 4 * 50 = 200 complete lines; none interleaved (mutex
    // ensures the whole fprintf-vfprintf-newline sequence is
    // atomic).
    CHECK(count_lines(out) ==
          static_cast<size_t>(kThreads * kPerThread));
}

TEST_CASE("log_init: no env vars -> disabled",
          "[log][adr-046]") {
    ResetLog r;
    EnvScope s1("HONEYBEE_LOG");
    EnvScope s2("HONEYBEE_LOG_LEVEL");
    EnvScope s3("HONEYBEE_LOG_FILE");
    unsetenv("HONEYBEE_LOG");
    unsetenv("HONEYBEE_LOG_LEVEL");
    unsetenv("HONEYBEE_LOG_FILE");

    honeybee::log_init();

    CHECK_FALSE(honeybee::log_enabled(
        honeybee::LogCategory::Layout, honeybee::LogLevel::Error));
}

TEST_CASE("log_init: HONEYBEE_LOG=all enables all categories",
          "[log][adr-046]") {
    ResetLog r;
    EnvScope s1("HONEYBEE_LOG");
    EnvScope s2("HONEYBEE_LOG_LEVEL");
    EnvScope s3("HONEYBEE_LOG_FILE");
    setenv("HONEYBEE_LOG", "all", 1);
    unsetenv("HONEYBEE_LOG_LEVEL");
    unsetenv("HONEYBEE_LOG_FILE");

    honeybee::log_init();

    CHECK(honeybee::log_enabled(
        honeybee::LogCategory::Layout, honeybee::LogLevel::Debug));
    CHECK(honeybee::log_enabled(
        honeybee::LogCategory::Css, honeybee::LogLevel::Debug));
    CHECK(honeybee::log_enabled(
        honeybee::LogCategory::Fetch, honeybee::LogLevel::Debug));
    CHECK(honeybee::log_enabled(
        honeybee::LogCategory::Event, honeybee::LogLevel::Debug));
}

TEST_CASE("log_init: explicit category list",
          "[log][adr-046]") {
    ResetLog r;
    EnvScope s1("HONEYBEE_LOG");
    EnvScope s2("HONEYBEE_LOG_LEVEL");
    EnvScope s3("HONEYBEE_LOG_FILE");
    setenv("HONEYBEE_LOG", "layout,paint", 1);
    unsetenv("HONEYBEE_LOG_LEVEL");
    unsetenv("HONEYBEE_LOG_FILE");

    honeybee::log_init();

    CHECK(honeybee::log_enabled(
        honeybee::LogCategory::Layout, honeybee::LogLevel::Debug));
    CHECK(honeybee::log_enabled(
        honeybee::LogCategory::Paint, honeybee::LogLevel::Debug));
    CHECK_FALSE(honeybee::log_enabled(
        honeybee::LogCategory::Css, honeybee::LogLevel::Debug));
}

TEST_CASE("log_init: HONEYBEE_LOG_LEVEL=info excludes debug",
          "[log][adr-046]") {
    ResetLog r;
    EnvScope s1("HONEYBEE_LOG");
    EnvScope s2("HONEYBEE_LOG_LEVEL");
    EnvScope s3("HONEYBEE_LOG_FILE");
    setenv("HONEYBEE_LOG", "layout", 1);
    setenv("HONEYBEE_LOG_LEVEL", "info", 1);
    unsetenv("HONEYBEE_LOG_FILE");

    honeybee::log_init();

    CHECK(honeybee::log_enabled(
        honeybee::LogCategory::Layout, honeybee::LogLevel::Info));
    CHECK_FALSE(honeybee::log_enabled(
        honeybee::LogCategory::Layout, honeybee::LogLevel::Debug));
}

TEST_CASE("log_init: malformed HONEYBEE_LOG disables",
          "[log][adr-046]") {
    ResetLog r;
    EnvScope s1("HONEYBEE_LOG");
    EnvScope s2("HONEYBEE_LOG_LEVEL");
    EnvScope s3("HONEYBEE_LOG_FILE");
    setenv("HONEYBEE_LOG", "garbage_value", 1);
    unsetenv("HONEYBEE_LOG_LEVEL");
    unsetenv("HONEYBEE_LOG_FILE");

    honeybee::log_init();

    CHECK_FALSE(honeybee::log_enabled(
        honeybee::LogCategory::Layout, honeybee::LogLevel::Error));
}

TEST_CASE("HB_LOG macro: no output when disabled",
          "[log][adr-046]") {
    ResetLog r;
    std::FILE* tmp = std::tmpfile();
    REQUIRE(tmp != nullptr);
    honeybee::log_set_config(false, kLayoutBit,
                             honeybee::LogLevel::Trace, tmp);
    HB_LOG(Layout, Debug, "should not appear");
    const std::string out = read_file(tmp);
    std::fclose(tmp);
    CHECK(out.empty());
}

TEST_CASE("HB_LOG macro: one line when enabled",
          "[log][adr-046]") {
    ResetLog r;
    std::FILE* tmp = std::tmpfile();
    REQUIRE(tmp != nullptr);
    honeybee::log_set_config(true, kLayoutBit,
                             honeybee::LogLevel::Debug, tmp);
    HB_LOG(Layout, Debug, "value=%d", 42);
    const std::string out = read_file(tmp);
    std::fclose(tmp);
    CHECK(count_lines(out) == 1);
    CHECK(out.find("[honeybee][layout][debug]") != std::string::npos);
    CHECK(out.find("value=42") != std::string::npos);
}
