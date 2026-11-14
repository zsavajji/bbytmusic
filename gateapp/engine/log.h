// Log file, paths and the one exit path of the gate engine.
#pragma once
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <mutex>
#include <string>

// Where the log and engine.json live: the app sandbox ($HOME there), else data/ beside the cwd.
inline std::string data_path(const char *name) {
    const char *home = std::getenv("HOME");
    return std::string(home && *home ? home : "data") + "/" + name;
}

// Appends "HH:MM:SS.mmm text" to gate.log, flushed per line. Also to stderr when BBM_STDERR is set
// (the selftest sets it), so a shell run shows the engine's side.
inline void log_line(const std::string &text) {
    static std::mutex m;
    static FILE *f = std::fopen(data_path("gate.log").c_str(), "a");
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    const int ms = int(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000);
    std::tm tm{};
    localtime_r(&t, &tm);
    char stamp[32];
    std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", &tm);
    std::lock_guard<std::mutex> lock(m);
    if (f) {
        std::fprintf(f, "%s.%03d %s\n", stamp, ms, text.c_str());
        std::fflush(f);
    }
    if (std::getenv("BBM_STDERR")) std::fprintf(stderr, "[engine] %s\n", text.c_str());
}

// Removes engine.json, stops the speaker and ends the process (main.cpp). Every exit goes here.
[[noreturn]] void bbm_exit(int code, const char *why);
