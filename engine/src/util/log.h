// Timestamped log lines, to stderr by default. Thread-safe.
#pragma once
#include <string>
#include <string_view>

namespace bbm {
enum class LogLevel { Debug, Info, Warn, Error };

void log(LogLevel level, std::string_view message);
void set_log_level(LogLevel level);                // default Info; lower levels are dropped
bool set_log_file(const std::string &path);        // appends; "" goes back to stderr; false if it can't open

inline void log_debug(std::string_view m) { log(LogLevel::Debug, m); }
inline void log_info(std::string_view m) { log(LogLevel::Info, m); }
inline void log_warn(std::string_view m) { log(LogLevel::Warn, m); }
inline void log_error(std::string_view m) { log(LogLevel::Error, m); }
}  // namespace bbm
