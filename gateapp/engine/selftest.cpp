#include "selftest.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <format>

namespace {
struct Client {
    int fd = -1;
    std::string buf;

    bool open(int port) {
        fd = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = htons(uint16_t(port));
        return fd >= 0 && ::connect(fd, reinterpret_cast<sockaddr *>(&a), sizeof a) == 0;
    }
    void send(const std::string &line) {
        std::printf("-> %s\n", line.c_str());
        std::fflush(stdout);
        const std::string out = line + "\n";
        if (::send(fd, out.data(), out.size(), 0) < 0) std::puts("send failed");
    }
    // One line, or "" after timeout_s / on close.
    std::string recv_line(double timeout_s) {
        const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(timeout_s);
        for (;;) {
            if (size_t nl = buf.find('\n'); nl != std::string::npos) {
                std::string l = buf.substr(0, nl);
                buf.erase(0, nl + 1);
                std::printf("<- %s\n", l.c_str());
                std::fflush(stdout);
                return l;
            }
            const double left = std::chrono::duration<double>(end - std::chrono::steady_clock::now()).count();
            if (left <= 0) return "";
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(fd, &fds);
            timeval tv{};
            tv.tv_sec = long(left);
            tv.tv_usec = long((left - double(tv.tv_sec)) * 1e6);
            if (select(fd + 1, &fds, nullptr, nullptr, &tv) <= 0) return "";
            char chunk[4096];
            const ssize_t n = ::recv(fd, chunk, sizeof chunk, 0);
            if (n <= 0) return "";
            buf.append(chunk, size_t(n));
        }
    }
    // Sends cmd and reads until the reply carrying its "@" id (events may come first).
    std::string call(const std::string &id, const std::string &json, double timeout_s = 10) {
        send(json);
        const std::string tag = "\"@\":\"" + id + "\"";
        for (std::string l; !(l = recv_line(timeout_s)).empty();)
            if (l.find(tag) != std::string::npos) return l;
        return "";
    }
};

bool has(const std::string &s, const char *part) { return s.find(part) != std::string::npos; }
bool check(const char *what, bool ok) {
    std::printf("%s: %s\n", ok ? "pass" : "FAIL", what);
    return ok;
}
}  // namespace

bool selftest_run(int port, const std::string &token, double seconds, double gain_db) {
    bool all = true;
    Client bad;
    all &= check("connect (wrong token)", bad.open(port));
    bad.send(R"({"@":"b","cmd":"hello","token":"nope"})");
    all &= check("wrong token refused", has(bad.recv_line(5), "\"error\":\"auth\""));
    Client c;
    all &= check("connect", c.open(port));
    all &= check("hello", has(c.call("1", std::format(R"({{"@":"1","cmd":"hello","token":"{}"}})", token)), "\"ok\":true"));
    all &= check("status", has(c.call("2", R"({"@":"2","cmd":"status"})"), "\"playing\":false"));
    all &= check("play", has(c.call("3", std::format(R"({{"@":"3","cmd":"play","seconds":{},"gain_db":{}}})", seconds, gain_db)),
                             "\"ok\":true"));
    bool done = false;
    int audio_events = 0;
    for (std::string l; !done && !(l = c.recv_line(seconds + 30)).empty();) {
        if (has(l, "\"ev\":\"audio\"")) audio_events++;
        done = has(l, "\"ev\":\"audio_done\"");
        if (done) all &= check("audio_done without error", has(l, "\"error\":\"\"") && has(l, "\"underruns\":0"));
    }
    all &= check("audio_done arrived", done);
    std::printf("audio events seen: %d\n", audio_events);
    all &= check("status after", has(c.call("4", R"({"@":"4","cmd":"status"})"), "\"playing\":false"));
    all &= check("quit", has(c.call("5", R"({"@":"5","cmd":"quit"})"), "\"ok\":true"));
    return all;
}
