// A protocol client for the engine tests: a raw socket, replies matched by id, events kept.
#pragma once
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <functional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "util/json.h"

namespace etest {
using namespace bbm;

inline int64_t ms_since(std::chrono::steady_clock::time_point t) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t).count();
}

struct Client {
    int fd = -1;
    std::string buf;
    std::vector<std::string> events;   // every event line seen, in order
    int ids = 0;

    explicit Client(int port) {
        fd = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = htons(uint16_t(port));
        if (::connect(fd, reinterpret_cast<sockaddr *>(&a), sizeof a) != 0) throw std::runtime_error("connect");
    }
    ~Client() { ::close(fd); }
    Client(const Client &) = delete;
    Client &operator=(const Client &) = delete;

    // One line, "" on timeout, "<EOF>" when the engine closed.
    std::string line(int ms) {
        for (;;) {
            const size_t nl = buf.find('\n');
            if (nl != std::string::npos) {
                std::string l = buf.substr(0, nl);
                buf.erase(0, nl + 1);
                return l;
            }
            pollfd p{fd, POLLIN, 0};
            if (::poll(&p, 1, ms) <= 0) return "";
            char c[65536];
            const ssize_t n = ::recv(fd, c, sizeof c, 0);
            if (n <= 0) return "<EOF>";
            buf.append(c, size_t(n));
        }
    }
    // Reads one line within ms; an event is kept. False on timeout or EOF.
    bool pump(int ms, std::string *reply = nullptr) {
        const std::string l = line(ms);
        if (l.empty() || l == "<EOF>") return false;
        if (l.find("\"ev\":") != std::string::npos && l.find("\"@\":") == std::string::npos) events.push_back(l);
        else if (reply) *reply = l;
        return true;
    }
    // Sends {"@":id,"cmd":cmd,<fields>}; the reply, or "" if none came in time.
    std::string cmd(const std::string &cmd, const std::string &fields = "", int ms = 20000) {
        const std::string id = std::to_string(++ids);
        const std::string req = "{\"@\":\"" + id + "\",\"cmd\":\"" + cmd + "\"" + (fields.empty() ? "" : "," + fields) + "}\n";
        (void)::send(fd, req.data(), req.size(), 0);
        const auto t0 = std::chrono::steady_clock::now();
        while (ms_since(t0) < ms) {
            std::string r;
            if (!pump(int(ms - ms_since(t0)), &r)) return "";
            if (r.find("\"@\":\"" + id + "\"") != std::string::npos) return r;
        }
        return "";
    }
    bool hello(const std::string &token) {
        return cmd("hello", "\"token\":\"" + token + "\",\"proto\":1").find("\"ok\":true") != std::string::npos;
    }
    // The index of the first event at or after `from` that pred accepts, reading more as needed;
    // -1 on timeout.
    int wait_event(const std::function<bool(Json)> &pred, int ms = 15000, size_t from = 0) {
        const auto t0 = std::chrono::steady_clock::now();
        for (size_t i = from;;) {
            for (; i < events.size(); i++) {
                JsonDoc d;
                std::string e;
                if (JsonDoc::parse(events[i], d, e) && pred(d.root())) return int(i);
            }
            const int64_t left = ms - ms_since(t0);
            if (left <= 0) return -1;
            if (!pump(int(left))) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
};

// The parsed reply; valid while the JsonDoc lives.
inline Json parse(const std::string &line, JsonDoc &d) {
    std::string e;
    JsonDoc::parse(line, d, e);
    return d.root();
}
inline bool is_ev(Json e, const char *ev) { return e["ev"].str() == ev; }

}  // namespace etest
