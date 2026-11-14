#include "util/log.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <mutex>

namespace bbm {
namespace {
std::mutex g_mutex;
std::FILE *g_file = nullptr;  // null: stderr
std::atomic<int> g_level{static_cast<int>(LogLevel::Info)};
}  // namespace

void set_log_level(LogLevel level) { g_level = static_cast<int>(level); }

bool set_log_file(const std::string &path) {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::FILE *f = nullptr;
    if (!path.empty()) {
        f = std::fopen(path.c_str(), "a");
        if (!f) return false;
    }
    if (g_file) std::fclose(g_file);
    g_file = f;
    return true;
}

void log(LogLevel level, std::string_view message) {
    if (static_cast<int>(level) < g_level) return;
    static const char *const names[] = {"DEBUG", "INFO ", "WARN ", "ERROR"};
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    const int ms = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000);
    std::tm tm{};
    localtime_r(&t, &tm);
    char stamp[32];
    std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", &tm);
    std::lock_guard<std::mutex> lock(g_mutex);
    std::FILE *out = g_file ? g_file : stderr;
    std::fprintf(out, "%s.%03d %s %.*s\n", stamp, ms, names[static_cast<int>(level)],
                 static_cast<int>(message.size()), message.data());
    std::fflush(out);
}
}  // namespace bbm
