#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "audio.h"

struct Conn;
struct Msg;

// Loopback NDJSON server: engine.json, token handshake, one thread per connection.
class Server {
public:
    // Listens on 127.0.0.1:0 and writes engine.json (0600). False if it cannot.
    bool start(std::string &why);
    void accept_loop();  // never returns
    void remove_state();  // deletes engine.json
    Audio &audio() { return audio_; }

    int port() const { return port_; }
    const std::string &token() const { return token_; }
    // The selftest drives `quit` itself and ends the process after reading the reply.
    bool quit_exits = true;

private:
    void serve(std::shared_ptr<Conn> c);
    std::string handle(const std::string &cmd, const Msg &m, bool &quit);
    void broadcast(const std::string &line);
    int clients();
    std::string assets(const char *file) const;
    void audio_event(const char *ev, const std::string &fields);

    int fd_ = -1;
    int port_ = 0;
    std::string token_;
    long start_s_ = 0;
    std::mutex m_;
    std::vector<std::shared_ptr<Conn>> conns_;
    Audio audio_;
};
