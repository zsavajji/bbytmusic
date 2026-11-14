// The NDJSON server on loopback (docs/protocol.md). The core only: framing, hello, replies,
// events. The commands are registered by the modules that own them, with on().
#pragma once
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "server/conn.h"
#include "util/json.h"

namespace bbm {

struct Reply {
    bool ok = true;
    std::string result;  // a JSON object's text, or ""
    std::string code, message;
    static Reply error(std::string code, std::string message) {
        Reply r;
        r.ok = false;
        r.code = std::move(code);
        r.message = std::move(message);
        return r;
    }
};

class Server {
public:
    using Handler = std::function<Reply(Json request)>;  // Json is valid during the call only

    Server() = default;
    ~Server();
    Server(const Server &) = delete;
    Server &operator=(const Server &) = delete;

    // Listens on 127.0.0.1, any port; writes <data_dir>/engine.json (mode 0600).
    bool start(const std::string &data_dir, std::string &err);
    // Handlers run on the reader thread of the connection that sent the command.
    void on(std::string cmd, Handler h);
    // {"ev":"<ev>",<fields>} to every authenticated connection. fields may be empty.
    void emit(std::string_view ev, std::string_view fields);
    void run();   // the accept loop; blocks until stop()
    void stop();  // from another thread: closes everything, removes engine.json
    int clients() const;  // authenticated, still open
    int port() const { return port_; }
    const std::string &token() const { return token_; }

private:
    void serve(Conn &c);
    bool dispatch(Conn &c, std::string_view line);  // false: close the connection
    void reap(bool all);
    void shutdown_all();

    int listen_fd_ = -1;
    int port_ = 0;
    std::string token_, state_file_;
    std::atomic<bool> stopping_{false};
    mutable std::mutex m_;
    std::vector<std::unique_ptr<Conn>> conns_;
    std::map<std::string, Handler> handlers_;
};

}  // namespace bbm
