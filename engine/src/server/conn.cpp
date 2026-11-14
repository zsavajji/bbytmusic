#include "server/conn.h"

#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>

namespace bbm {

Conn::Conn(int fd) : fd_(fd), writer_(&Conn::write_loop, this) {}

Conn::~Conn() {
    kill();
    if (reader.joinable()) reader.join();
    if (writer_.joinable()) writer_.join();
    ::close(fd_);
}

bool Conn::push(std::string line) {
    bool drop = false;
    {
        std::lock_guard<std::mutex> lk(m_);
        if (dead_ || closing_) return false;
        if (q_.size() >= kMaxQueue) drop = true;
        else {
            line += '\n';
            q_.push_back(std::move(line));
        }
    }
    if (drop) {
        kill();
        return false;
    }
    cv_.notify_one();
    return true;
}

void Conn::close_after() {
    {
        std::lock_guard<std::mutex> lk(m_);
        closing_ = true;
    }
    cv_.notify_one();
}

void Conn::kill() {
    {
        std::lock_guard<std::mutex> lk(m_);
        dead_ = true;
        q_.clear();
    }
    ::shutdown(fd_, SHUT_RDWR);  // unblocks a recv() and a send() stuck on a full socket
    cv_.notify_all();
}

bool Conn::send_all(const std::string &s) {
    size_t off = 0;
    while (off < s.size()) {
        const ssize_t n = ::send(fd_, s.data() + off, s.size() - off, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        off += size_t(n);
    }
    return true;
}

void Conn::write_loop() {
    for (;;) {
        std::string line;
        {
            std::unique_lock<std::mutex> lk(m_);
            cv_.wait(lk, [&] { return dead_ || closing_ || !q_.empty(); });
            if (dead_ || q_.empty()) break;  // dead, or closing with everything sent
            line = std::move(q_.front());
            q_.pop_front();
        }
        if (!send_all(line)) break;
    }
    kill();
}

}  // namespace bbm
