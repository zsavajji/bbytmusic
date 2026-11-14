// The server core, with a raw-socket client.
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <thread>

#include "check.h"
#include "server/server.h"
#include "util/json.h"

using namespace bbm;

namespace {

struct Client {
    int fd = -1;
    std::string buf;
    explicit Client(int port) {
        fd = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = htons(uint16_t(port));
        if (::connect(fd, reinterpret_cast<sockaddr *>(&a), sizeof a) != 0) throw std::runtime_error("connect");
    }
    ~Client() { ::close(fd); }
    void send(const std::string &s) { (void)::send(fd, s.data(), s.size(), 0); }
    // One line, or "" on timeout, or "<EOF>" when the server closed.
    std::string line(int ms = 2000) {
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
    bool hello(const std::string &token) {
        send("{\"@\":\"h\",\"cmd\":\"hello\",\"token\":\"" + token + "\",\"proto\":1}\n");
        return line().find("\"ok\":true") != std::string::npos;
    }
};

std::string code_of(const std::string &reply) {
    JsonDoc d;
    std::string e;
    JsonDoc::parse(reply, d, e);
    return d.root()["error"]["code"].string();
}

}  // namespace

int main() {
    char tmpl[] = "/tmp/bbm_server_XXXXXX";
    const std::string dir = mkdtemp(tmpl);
    Server srv;
    std::string err;
    CHECK(srv.start(dir, err));
    CHECK_EQ(err, "");
    srv.on("echo", [](Json req) {
        Reply r;
        r.result = "{\"text\":" + json_quote(req["text"].str()) + "}";
        return r;
    });
    srv.on("boom", [](Json) -> Reply { throw std::runtime_error("kaboom"); });
    srv.on("fail", [](Json) { return Reply::error("nope", "no way"); });
    std::thread runner([&] { srv.run(); });

    // engine.json: port, token, mode 0600
    const std::string state = dir + "/engine.json";
    struct stat st{};
    CHECK_EQ(::stat(state.c_str(), &st), 0);
    CHECK_EQ(int(st.st_mode & 0777), 0600);
    std::ifstream f(state);
    std::stringstream ss;
    ss << f.rdbuf();
    JsonDoc sd;
    CHECK(JsonDoc::parse(ss.str(), sd, err));
    CHECK_EQ(int(sd.root()["port"].i64()), srv.port());
    CHECK_EQ(sd.root()["token"].string(), srv.token());
    CHECK_EQ(srv.token().size(), size_t(32));
    CHECK_EQ(srv.token().find_first_not_of("0123456789abcdef"), std::string::npos);

    // wrong token: refused and closed
    {
        Client c(srv.port());
        c.send("{\"@\":\"1\",\"cmd\":\"hello\",\"token\":\"wrong\",\"proto\":1}\n");
        const std::string r = c.line();
        CHECK_EQ(code_of(r), "auth");
        CHECK(r.find("\"@\":\"1\"") != std::string::npos);
        CHECK_EQ(c.line(), "<EOF>");
    }
    // a command before hello: refused and closed
    {
        Client c(srv.port());
        c.send("{\"@\":\"1\",\"cmd\":\"echo\",\"text\":\"x\"}\n");
        CHECK_EQ(code_of(c.line()), "auth");
        CHECK_EQ(c.line(), "<EOF>");
    }
    // wrong proto: refused and closed
    {
        Client c(srv.port());
        c.send("{\"@\":\"1\",\"cmd\":\"hello\",\"token\":\"" + srv.token() + "\",\"proto\":2}\n");
        CHECK_EQ(code_of(c.line()), "proto");
        CHECK_EQ(c.line(), "<EOF>");
    }
    // hello, echo, errors, bad lines
    {
        Client c(srv.port());
        c.send("{\"@\":\"h\",\"cmd\":\"hello\",\"token\":\"" + srv.token() + "\",\"proto\":1}\n");
        CHECK_EQ(c.line(), "{\"@\":\"h\",\"ok\":true,\"result\":{\"proto\":1}}");
        c.send("{\"@\":\"7\",\"cmd\":\"echo\",\"text\":\"h\\u00e9llo \\\"q\\\"\"}\n");
        CHECK_EQ(c.line(), "{\"@\":\"7\",\"ok\":true,\"result\":{\"text\":\"h\xc3\xa9llo \\\"q\\\"\"}}");
        c.send("{\"@\":\"8\",\"cmd\":\"nothing\"}\n");
        const std::string u = c.line();
        CHECK_EQ(code_of(u), "unknown");
        CHECK(u.find("\"@\":\"8\"") != std::string::npos);
        c.send("{\"@\":\"9\",\"cmd\":\"boom\"}\n");
        CHECK_EQ(code_of(c.line()), "internal");
        c.send("{\"@\":\"10\",\"cmd\":\"fail\"}\n");
        CHECK_EQ(c.line(),
                 "{\"@\":\"10\",\"ok\":false,\"error\":{\"code\":\"nope\",\"message\":\"no way\"}}");
        c.send("this is not json\n");
        CHECK_EQ(code_of(c.line()), "bad_request");
        c.send("[1,2]\n{\"@\":\"11\"}\n");
        CHECK_EQ(code_of(c.line()), "bad_request");
        CHECK_EQ(code_of(c.line()), "bad_request");
        // a line over 1 MiB (with its newline): an error, and the connection stays
        c.send("{\"cmd\":\"echo\",\"text\":\"" + std::string((1 << 20) + 100, 'a') + "\"}\n");
        CHECK_EQ(code_of(c.line()), "bad_request");
        // replies stay in request order, pipelined
        c.send("{\"@\":\"a\",\"cmd\":\"echo\",\"text\":\"1\"}\n{\"@\":\"b\",\"cmd\":\"echo\",\"text\":\"2\"}\n");
        CHECK(c.line().find("\"@\":\"a\"") != std::string::npos);
        CHECK(c.line().find("\"@\":\"b\"") != std::string::npos);
        // still alive after all that
        c.send("{\"@\":\"z\",\"cmd\":\"echo\",\"text\":\"end\"}\n");
        CHECK(c.line().find("\"end\"") != std::string::npos);
    }
    // a line far over the limit without a newline closes the connection
    {
        Client c(srv.port());
        CHECK(c.hello(srv.token()));
        const std::string chunk(1 << 20, 'x');
        for (int i = 0; i < 6; ++i) c.send(chunk);
        CHECK_EQ(c.line(), "<EOF>");
    }
    // an event reaches two clients and not an unauthenticated one
    {
        Client a(srv.port()), b(srv.port()), u(srv.port());
        CHECK(a.hello(srv.token()));
        CHECK(b.hello(srv.token()));
        CHECK_EQ(srv.clients(), 2);
        srv.emit("state", "\"playing\":true");
        srv.emit("ping", "");
        CHECK_EQ(a.line(), "{\"ev\":\"state\",\"playing\":true}");
        CHECK_EQ(b.line(), "{\"ev\":\"state\",\"playing\":true}");
        CHECK_EQ(a.line(), "{\"ev\":\"ping\"}");
        CHECK_EQ(b.line(), "{\"ev\":\"ping\"}");
        CHECK_EQ(u.line(300), "");
    }
    // a client that never reads is dropped after 256 lines; another keeps receiving
    {
        for (int i = 0; i < 100 && srv.clients() > 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        Client slow(srv.port()), good(srv.port());
        CHECK(slow.hello(srv.token()));
        CHECK(good.hello(srv.token()));
        const std::string fields = "\"pad\":\"" + std::string(60000, 'p') + "\"";
        int sent = 0, got = 0;
        while (srv.clients() == 2 && sent < 5000) {
            srv.emit("big", fields);
            ++sent;
            if (good.line().rfind("{\"ev\":\"big\"", 0) == 0) ++got;
        }
        CHECK_EQ(srv.clients(), 1);
        CHECK_EQ(got, sent);
        CHECK(sent > 256);
        srv.emit("after", "");  // the survivor still gets events
        std::string l;
        do l = good.line(); while (l.rfind("{\"ev\":\"after\"", 0) != 0 && l != "" && l != "<EOF>");
        CHECK_EQ(l, "{\"ev\":\"after\"}");
        // the dropped one drains what it was sent, then sees the close
        std::string last;
        for (int i = 0; i < 10000; ++i) {
            last = slow.line(500);
            if (last == "<EOF>" || last.empty()) break;
        }
        CHECK_EQ(last, "<EOF>");
    }
    // stop() ends run() and removes engine.json
    srv.stop();
    runner.join();
    CHECK(::stat(state.c_str(), &st) != 0);
    {
        // and nobody can connect any more
        int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = htons(uint16_t(srv.port()));
        CHECK(::connect(fd, reinterpret_cast<sockaddr *>(&a), sizeof a) != 0);
        ::close(fd);
    }
    std::system(("rm -rf " + dir).c_str());
    return check_done();
}
