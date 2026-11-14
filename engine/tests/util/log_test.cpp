#include <cstdio>
#include <fstream>
#include <sstream>
#include <thread>
#include <vector>

#include "check.h"
#include "util/log.h"

using namespace bbm;

int main() {
    const std::string path = std::string(BBM_FIXTURES) + "/../log_test.tmp";
    std::remove(path.c_str());
    CHECK(set_log_file(path));
    set_log_level(LogLevel::Warn);
    log(LogLevel::Info, "dropped");
    log(LogLevel::Error, "kept error");
    set_log_level(LogLevel::Debug);
    std::vector<std::thread> ts;
    for (int t = 0; t < 4; t++)
        ts.emplace_back([t] { for (int i = 0; i < 50; i++) log_debug("thread " + std::to_string(t)); });
    for (auto &t : ts) t.join();
    CHECK(set_log_file(""));
    CHECK(!set_log_file("/nonexistent-dir/x.log"));
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string s = ss.str();
    CHECK(s.find("dropped") == std::string::npos);
    CHECK(s.find("ERROR kept error") != std::string::npos);
    size_t lines = 0;
    for (char c : s) lines += c == '\n';
    CHECK_EQ(lines, size_t(201));
    std::remove(path.c_str());
    return check_done();
}
