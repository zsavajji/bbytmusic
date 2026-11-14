// http against a loopback server of our own: no network needed.
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <thread>

#include "check.h"
#include "net/http.h"

using namespace bbm;

namespace {
std::atomic<bool> g_stop{false};

void send_all(int fd, const std::string &s) {
    for (size_t off = 0; off < s.size();) {
        const ssize_t n = ::send(fd, s.data() + off, s.size() - off, 0);
        if (n <= 0) return;
        off += size_t(n);
    }
}

void serve_one(int fd) {
    std::string req;
    char buf[4096];
    size_t body_at = std::string::npos;
    while ((body_at = req.find("\r\n\r\n")) == std::string::npos) {
        const ssize_t n = ::recv(fd, buf, sizeof buf, 0);
        if (n <= 0) return;
        req.append(buf, size_t(n));
    }
    body_at += 4;
    size_t want = 0;
    if (const size_t p = req.find("Content-Length: "); p != std::string::npos) want = size_t(std::atol(req.c_str() + p + 16));
    while (req.size() - body_at < want) {
        const ssize_t n = ::recv(fd, buf, sizeof buf, 0);
        if (n <= 0) return;
        req.append(buf, size_t(n));
    }
    const std::string line = req.substr(0, req.find("\r\n"));
    auto reply = [&](const char *status, const std::string &body, const char *extra = "") {
        send_all(fd, std::string("HTTP/1.1 ") + status + "\r\nConnection: close\r\nX-Test: Yes\r\n" + extra +
                         "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body);
    };
    if (line.find(" /hello ") != std::string::npos) reply("200 OK", "hello");
    else if (line.find(" /echo ") != std::string::npos) reply("200 OK", line.substr(0, line.find(' ')) + ":" + req.substr(body_at));
    else if (line.find(" /redir ") != std::string::npos) reply("302 Found", "", "Location: /hello\r\n");
    else if (line.find(" /big ") != std::string::npos) reply("200 OK", std::string(300000, 'x'));
    else if (line.find(" /slow ") != std::string::npos) {
        send_all(fd, "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 100000\r\n\r\n");
        for (int i = 0; i < 500 && !g_stop; i++) {
            send_all(fd, std::string(200, 'z'));
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    } else reply("404 Not Found", "nope");
}

int g_listen = -1;
int start_server() {
    g_listen = ::socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(g_listen, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0;
    bind(g_listen, reinterpret_cast<sockaddr *>(&a), sizeof a);
    listen(g_listen, 8);
    socklen_t len = sizeof a;
    getsockname(g_listen, reinterpret_cast<sockaddr *>(&a), &len);
    return ntohs(a.sin_port);
}
}  // namespace

int main() {
    const int port = start_server();
    std::thread server([] {
        while (!g_stop) {
            pollfd p{g_listen, POLLIN, 0};
            if (poll(&p, 1, 50) <= 0) continue;
            const int fd = ::accept(g_listen, nullptr, nullptr);
            if (fd < 0) continue;
            std::thread([fd] { serve_one(fd); ::close(fd); }).detach();
        }
    });
    const std::string base = "http://127.0.0.1:" + std::to_string(port);
    Http http;

    HttpRequest r;
    r.url = base + "/hello";
    HttpResponse resp = http.send(r);
    CHECK(resp.ok());
    CHECK_EQ(resp.status, 200L);
    CHECK_EQ(resp.body, std::string("hello"));
    CHECK_EQ(resp.header("x-test"), std::string("Yes"));

    r.url = base + "/echo";
    r.method = "POST";
    r.body = "payload";
    r.headers = {{"Content-Type", "text/plain"}};
    resp = http.send(r);                       // same Http again: the handle is reused
    CHECK_EQ(resp.body, std::string("POST:payload"));
    r.method = "PUT";
    CHECK_EQ(http.send(r).body, std::string("PUT:payload"));

    r = {};
    r.url = base + "/redir";
    resp = http.send(r);
    CHECK(resp.ok() && resp.body == "hello");

    r.url = base + "/missing";
    resp = http.send(r);
    CHECK(!resp.ok());
    CHECK_EQ(resp.status, 404L);
    CHECK(resp.error.empty());
    CHECK_EQ(resp.body, std::string("nope"));

    // Streamed: the body goes to on_data, not to response.body.
    size_t got = 0;
    r.url = base + "/big";
    r.on_data = [&](const uint8_t *, size_t n) { got += n; return true; };
    resp = http.send(r);
    CHECK(resp.ok());
    CHECK_EQ(got, size_t(300000));
    CHECK(resp.body.empty());

    // on_data returning false aborts.
    got = 0;
    r.on_data = [&](const uint8_t *, size_t n) { got += n; return false; };
    resp = http.send(r);
    CHECK(!resp.ok());
    CHECK(resp.error.find("aborted") != std::string::npos);
    CHECK(got > 0 && got < 300000);

    // Unreachable: an error and status 0.
    r = {};
    r.url = "http://127.0.0.1:1/";
    r.timeout_s = 5;
    resp = http.send(r);
    CHECK(!resp.ok());
    CHECK_EQ(resp.status, 0L);
    CHECK(!resp.error.empty());
    CHECK(http.send(HttpRequest{.url = "not a url"}).error.size() > 0);

    // Cancel from another thread, mid-stream.
    std::atomic<bool> cancel{false};
    r = {};
    r.url = base + "/slow";
    r.cancel = &cancel;
    r.on_data = [](const uint8_t *, size_t) { return true; };
    std::thread canceller([&] { std::this_thread::sleep_for(std::chrono::milliseconds(200)); cancel = true; });
    const auto t0 = std::chrono::steady_clock::now();
    resp = http.send(r);
    canceller.join();
    CHECK_EQ(resp.error, std::string("cancelled"));
    CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(3));
    // A set flag stops a request before it starts, and the handle still works afterwards.
    CHECK(!http.send(r).ok());
    r = {};
    r.url = base + "/hello";
    CHECK(http.send(r).ok());

    g_stop = true;
    server.join();
    ::close(g_listen);
    return check_done();
}
