// One client connection: its socket, and a bounded queue of outgoing lines with a writer thread
// of its own, so a client that stops reading can stall nobody else.
#pragma once
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

namespace bbm {

class Conn {
public:
    static constexpr size_t kMaxQueue = 256;  // lines queued before the client is dropped

    explicit Conn(int fd);
    ~Conn();  // kills, joins both threads, closes the socket
    Conn(const Conn &) = delete;
    Conn &operator=(const Conn &) = delete;

    // Queues one line ("\n" is added). False if the connection is closed or closing, or the
    // queue was full (the connection is then dropped).
    bool push(std::string line);
    void close_after();  // closes once what is queued has been sent
    void kill();         // closes now; wakes the reader and the writer
    bool alive() const { return !dead_; }
    int fd() const { return fd_; }

    std::atomic<bool> authed{false};
    std::atomic<bool> done{false};  // set by the reader thread as its last act
    std::thread reader;             // started and joined by the Server

private:
    void write_loop();
    bool send_all(const std::string &s);

    int fd_;
    std::mutex m_;
    std::condition_variable cv_;
    std::deque<std::string> q_;
    bool closing_ = false;
    std::atomic<bool> dead_{false};
    std::thread writer_;  // last: starts once everything above exists
};

}  // namespace bbm
