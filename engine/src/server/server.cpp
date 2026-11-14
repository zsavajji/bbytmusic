#include "server/server.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <csignal>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <exception>

#include "util/log.h"

namespace bbm {
namespace {

constexpr size_t kMaxLine = 1 << 20;       // a request line, without its newline
constexpr size_t kSkipLimit = 4 << 20;     // an over-long line longer than this closes the connection
constexpr int kProto = 1;

// Any app on the phone can reach loopback and guess: compare in time that doesn't depend on
// how many leading characters were right.
bool token_equal(std::string_view a, std::string_view b) {
    unsigned char diff = a.size() == b.size() ? 0 : 1;
    for (size_t i = 0; i < b.size(); i++) diff |= static_cast<unsigned char>((i < a.size() ? a[i] : 0) ^ b[i]);
    return diff == 0;
}

std::string make_token(std::string &err) {
    unsigned char raw[16];
    FILE *f = std::fopen("/dev/urandom", "rb");
    const bool ok = f && std::fread(raw, 1, sizeof raw, f) == sizeof raw;
    if (f) std::fclose(f);
    if (!ok) {
        err = "cannot read /dev/urandom";
        return "";
    }
    static const char hex[] = "0123456789abcdef";
    std::string t;
    for (unsigned char b : raw) {
        t += hex[b >> 4];
        t += hex[b & 15];
    }
    return t;
}

// {"@":"id","ok":...}
std::string reply_line(std::string_view id, const Reply &r) {
    std::string s = "{";
    if (!id.empty()) s += "\"@\":" + json_quote(id) + ",";
    if (r.ok) {
        s += "\"ok\":true";
        if (!r.result.empty()) s += ",\"result\":" + r.result;
    } else {
        s += "\"ok\":false,\"error\":{\"code\":" + json_quote(r.code) + ",\"message\":" +
             json_quote(r.message) + "}";
    }
    return s + "}";
}

bool write_state(const std::string &path, int port, const std::string &token, std::string &err) {
    const std::string tmp = path + ".tmp";
    const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        err = "cannot write " + tmp + ": " + std::strerror(errno);
        return false;
    }
    ::fchmod(fd, 0600);  // an old temp file keeps its mode through O_TRUNC
    const std::string body = "{\"port\":" + std::to_string(port) + ",\"token\":\"" + token + "\"}\n";
    const bool ok = ::write(fd, body.data(), body.size()) == ssize_t(body.size());
    ::close(fd);
    if (!ok || std::rename(tmp.c_str(), path.c_str()) != 0) {
        err = "cannot write " + path;
        std::remove(tmp.c_str());
        return false;
    }
    return true;
}

}  // namespace

Server::~Server() {
    stop();
    reap(true);
    if (listen_fd_ >= 0) ::close(listen_fd_);
}

bool Server::start(const std::string &data_dir, std::string &err) {
    std::signal(SIGPIPE, SIG_IGN);
    token_ = make_token(err);
    if (token_.empty()) return false;
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t len = sizeof a;
    if (listen_fd_ < 0 || ::bind(listen_fd_, reinterpret_cast<sockaddr *>(&a), sizeof a) != 0 ||
        ::listen(listen_fd_, 16) != 0 ||
        ::getsockname(listen_fd_, reinterpret_cast<sockaddr *>(&a), &len) != 0) {
        err = std::string("listen: ") + std::strerror(errno);
        return false;
    }
    port_ = ntohs(a.sin_port);
    state_file_ = data_dir + "/engine.json";
    if (!write_state(state_file_, port_, token_, err)) return false;
    log_info("listening on 127.0.0.1:" + std::to_string(port_));
    return true;
}

void Server::on(std::string cmd, Handler h) {
    std::lock_guard<std::mutex> lk(m_);
    handlers_[std::move(cmd)] = std::move(h);
}

void Server::emit(std::string_view ev, std::string_view fields) {
    std::string line = "{\"ev\":" + json_quote(ev);
    if (!fields.empty()) line += "," + std::string(fields);
    line += "}";
    std::lock_guard<std::mutex> lk(m_);
    for (auto &c : conns_)
        if (c->authed && c->alive() && !c->push(line)) log_warn("client too far behind: dropped");
}

int Server::clients() const {
    std::lock_guard<std::mutex> lk(m_);
    int n = 0;
    for (auto &c : conns_) n += c->authed && c->alive();
    return n;
}

// Joins and frees the connections whose reader has finished (all: after killing them).
void Server::reap(bool all) {
    std::vector<std::unique_ptr<Conn>> gone;
    {
        std::lock_guard<std::mutex> lk(m_);
        for (auto it = conns_.begin(); it != conns_.end();) {
            if (all || ((*it)->done && !(*it)->alive())) {  // close_after() ends in the writer's kill()
                gone.push_back(std::move(*it));
                it = conns_.erase(it);
            } else {
                ++it;
            }
        }
    }
    gone.clear();  // the destructors join; no lock held, a reader may still want m_
}

void Server::shutdown_all() {
    std::lock_guard<std::mutex> lk(m_);
    for (auto &c : conns_) c->kill();
}

void Server::stop() {
    if (stopping_.exchange(true)) return;
    shutdown_all();
    if (!state_file_.empty()) std::remove(state_file_.c_str());
}

void Server::run() {
    while (!stopping_) {
        pollfd p{listen_fd_, POLLIN, 0};
        const int n = ::poll(&p, 1, 100);  // a timeout, not a wake-up: closing a listener doesn't wake accept() everywhere
        reap(false);
        if (n <= 0 || stopping_) continue;
        const int fd = ::accept(listen_fd_, nullptr, nullptr);
        if (fd < 0) continue;
        auto c = std::make_unique<Conn>(fd);
        Conn *raw = c.get();
        {
            std::lock_guard<std::mutex> lk(m_);
            conns_.push_back(std::move(c));
        }
        raw->reader = std::thread([this, raw] {
            serve(*raw);
            raw->done = true;
        });
    }
    shutdown_all();
    reap(true);
    ::close(listen_fd_);
    listen_fd_ = -1;
}

void Server::serve(Conn &c) {
    std::string buf;
    size_t skipped = 0;  // bytes of an over-long line thrown away so far
    bool skipping = false;
    char chunk[65536];
    bool open = true;
    while (open && c.alive()) {
        const ssize_t n = ::recv(c.fd(), chunk, sizeof chunk, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        buf.append(chunk, size_t(n));
        for (;;) {
            const size_t nl = buf.find('\n');
            if (nl == std::string::npos) {
                if (buf.size() > kMaxLine) {
                    skipped += buf.size();
                    skipping = true;
                    buf.clear();
                    if (skipped > kSkipLimit) open = false;
                }
                break;
            }
            std::string line = buf.substr(0, nl);
            buf.erase(0, nl + 1);
            if (skipping || line.size() > kMaxLine) {
                skipping = false;
                skipped = 0;
                c.push(reply_line("", Reply::error("bad_request", "line over 1 MiB")));
                continue;
            }
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            if (!dispatch(c, line)) {
                c.close_after();  // the writer sends what is queued, then closes
                return;

            }
        }
    }
    c.kill();
}

bool Server::dispatch(Conn &c, std::string_view line) {
    JsonDoc doc;
    std::string perr;
    const bool parsed = JsonDoc::parse(line, doc, perr) && doc.root().is_object();
    const Json req = doc.root();
    const std::string cmd = req["cmd"].string();
    const std::string id = req["@"].is_string() ? req["@"].string() : "";

    if (cmd == "hello" || !c.authed) {
        Reply r;
        if (!parsed || cmd != "hello" || !token_equal(req["token"].str(), token_)) {
            r = Reply::error("auth", "send hello with the token first");
        } else if (req["proto"].i64() != kProto) {
            r = Reply::error("proto", "unsupported protocol version");
        } else {
            c.authed = true;
            r.result = "{\"proto\":" + std::to_string(kProto) + "}";
        }
        c.push(reply_line(id, r));
        return r.ok;
    }
    if (!parsed || cmd.empty()) {
        c.push(reply_line(id, Reply::error("bad_request", parsed ? "no cmd" : "invalid json: " + perr)));
        return true;
    }
    Handler h;
    {
        std::lock_guard<std::mutex> lk(m_);
        auto it = handlers_.find(cmd);
        if (it != handlers_.end()) h = it->second;
    }
    log_info("cmd " + cmd);   // hello never comes here, so no token is logged
    Reply r;
    if (!h) {
        r = Reply::error("unknown", "unknown command: " + cmd);
    } else {
        try {
            r = h(req);
        } catch (const std::exception &e) {
            r = Reply::error("internal", e.what());
        } catch (...) {
            r = Reply::error("internal", "unknown exception");
        }
    }
    c.push(reply_line(id, r));
    return true;
}

}  // namespace bbm
