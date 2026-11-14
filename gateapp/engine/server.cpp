#include "server.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <cerrno>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <format>
#include <thread>

#include "json.h"
#include "log.h"
#include "net.h"

struct Conn {
    int fd;
    std::atomic<bool> authed{false};
    std::mutex wm;
    explicit Conn(int f) : fd(f) {}
    ~Conn() { ::close(fd); }
    void send_line(const std::string &line) {
        std::lock_guard<std::mutex> lock(wm);
        const std::string out = line + "\n";
        size_t off = 0;
        while (off < out.size()) {
            const ssize_t n = ::send(fd, out.data() + off, out.size() - off, 0);
            if (n <= 0) return;  // the reader thread sees the close
            off += size_t(n);
        }
    }
};

namespace {
long now_s() { return long(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count()); }

std::string random_token() {
    unsigned char raw[16] = {};
    if (FILE *f = std::fopen("/dev/urandom", "rb")) {
        if (std::fread(raw, 1, sizeof raw, f) != sizeof raw) std::memset(raw, 0, sizeof raw);
        std::fclose(f);
    }
    std::string t;
    for (unsigned char b : raw) t += std::format("{:02x}", b);
    return t;
}

// {"@":id,<body>}
std::string reply(const Msg &m, const std::string &body) {
    const std::string id = m.id_json();
    return "{" + (id.empty() ? "" : "\"@\":" + id + ",") + body + "}";
}
}  // namespace

bool Server::start(std::string &why) {
    start_s_ = now_s();
    token_ = random_token();
    fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0;
    socklen_t len = sizeof a;
    if (fd_ < 0 || ::bind(fd_, reinterpret_cast<sockaddr *>(&a), sizeof a) != 0 || ::listen(fd_, 8) != 0 ||
        ::getsockname(fd_, reinterpret_cast<sockaddr *>(&a), &len) != 0) {
        why = std::format("listen: {}", std::strerror(errno));
        return false;
    }
    port_ = ntohs(a.sin_port);
    const std::string path = data_path("engine.json"), tmp = path + ".tmp";
    FILE *f = std::fopen(tmp.c_str(), "w");
    if (!f) { why = "cannot write " + tmp; return false; }
    ::fchmod(fileno(f), 0600);
    std::fprintf(f, "{\"port\":%d,\"token\":\"%s\"}\n", port_, token_.c_str());
    std::fclose(f);
    if (std::rename(tmp.c_str(), path.c_str()) != 0) { why = "cannot rename engine.json"; return false; }
    log_line(std::format("listening on 127.0.0.1:{}, engine.json written", port_));
    return true;
}

void Server::remove_state() { std::remove(data_path("engine.json").c_str()); }

void Server::accept_loop() {
    for (;;) {
        const int fd = ::accept(fd_, nullptr, nullptr);
        if (fd < 0) { std::this_thread::sleep_for(std::chrono::milliseconds(100)); continue; }
        auto c = std::make_shared<Conn>(fd);
        std::thread(&Server::serve, this, c).detach();
    }
}

int Server::clients() {
    std::lock_guard<std::mutex> lock(m_);
    return int(conns_.size());
}

void Server::broadcast(const std::string &line) {
    std::vector<std::shared_ptr<Conn>> list;
    {
        std::lock_guard<std::mutex> lock(m_);
        list = conns_;
    }
    for (auto &c : list) if (c->authed) c->send_line(line);
}

void Server::audio_event(const char *ev, const std::string &fields) {
    const std::string line = std::format("{{\"ev\":\"{}\",{},\"clients\":{}}}", ev, fields, clients());
    log_line(line);
    broadcast(line);
}

std::string Server::assets(const char *file) const {
    const char *dir = std::getenv("BBM_ASSETS");
    return std::string(dir && *dir ? dir : "app/native/assets") + "/" + file;
}

void Server::serve(std::shared_ptr<Conn> c) {
    {
        std::lock_guard<std::mutex> lock(m_);
        conns_.push_back(c);
    }
    log_line(std::format("client connected (fd {})", c->fd));
    std::string buf;
    char chunk[4096];
    bool open = true;
    while (open) {
        const ssize_t n = ::recv(c->fd, chunk, sizeof chunk, 0);
        if (n <= 0) break;
        buf.append(chunk, size_t(n));
        if (buf.size() > 65536 && buf.find('\n') == std::string::npos) break;
        for (size_t nl; open && (nl = buf.find('\n')) != std::string::npos;) {
            const std::string line = buf.substr(0, nl);
            buf.erase(0, nl + 1);
            const Msg m = json_parse(line);
            const std::string cmd = m.str("cmd");
            if (!c->authed) {
                if (!m.ok || cmd != "hello" || m.str("token") != token_) {
                    c->send_line(reply(m, "\"ok\":false,\"error\":\"auth\""));
                    log_line("client refused: auth");
                    open = false;
                    break;
                }
                c->authed = true;
                c->send_line(reply(m, std::format("\"ok\":true,\"result\":{{\"pid\":{}}}", int(getpid()))));
                continue;
            }
            bool quit = false;
            c->send_line(reply(m, handle(cmd, m, quit)));
            if (quit) {
                if (quit_exits) bbm_exit(0, "quit command");
                log_line("quit command (selftest: not exiting)");
            }
        }
    }
    {
        std::lock_guard<std::mutex> lock(m_);
        std::erase(conns_, c);
    }
    log_line(std::format("client gone (fd {})", c->fd));
}

// The body of the reply: "ok":...
std::string Server::handle(const std::string &cmd, const Msg &m, bool &quit) {
    if (cmd == "status") {
        const AudioStats s = audio_.stats();
        return std::format("\"ok\":true,\"result\":{{\"playing\":{},\"played\":{:.1f},\"underruns\":{},\"pid\":{},\"uptime\":{},\"clients\":{}}}",
                           s.playing ? "true" : "false", s.played, s.underruns, int(getpid()), now_s() - start_s_, clients());
    }
    if (cmd == "net") {
        const NetResult r = net_probe(assets("cacert.pem"), m.str("proxy"));
        log_line(std::format("net: http {} bytes {} {:.2f} s error '{}'", r.http, r.bytes, r.seconds, r.error));
        return std::format("\"ok\":true,\"result\":{{\"http\":{},\"bytes\":{},\"seconds\":{:.2f},\"error\":{}}}", r.http, r.bytes,
                           r.seconds, json_quote(r.error));
    }
    if (cmd == "play") {
        const double seconds = m.num("seconds", 600), gain = m.num("gain_db", -40);
        std::string why;
        if (!audio_.start(seconds, gain, assets("test.webm"),
                          [this](const char *ev, const std::string &f) { audio_event(ev, f); }, why)) {
            log_line("play refused: " + why);
            return "\"ok\":false,\"error\":" + json_quote(why);
        }
        log_line(std::format("play: {} s at {} dB", seconds, gain));
        return "\"ok\":true";
    }
    if (cmd == "stop") {
        audio_.stop();
        log_line("stop");
        return "\"ok\":true";
    }
    if (cmd == "quit") {
        quit = true;
        return "\"ok\":true";
    }
    return "\"ok\":false,\"error\":" + json_quote("unknown cmd");
}
