// Gate 0.5 (PLAN.md §9): can the engine hold the system's now-playing service itself, with no
// Qt? bb::multimedia::NowPlayingConnection is a client of two PPS objects; this talks to them
// directly, to find the messages and to see the overlay's buttons arrive in a plain process.
//
//   gate05 <seconds to listen> <message> [message ...]
//
// Each message is written to /pps/services/multimedia/mediaplayer/control in one write, with
// "\n" in the argument turned into a newline; whatever the service answers is printed. Then it
// listens for the given time, and prints mediaplayer/status at the end.
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

namespace {

const char *kControl = "/pps/services/multimedia/mediaplayer/control";
const char *kStatus = "/pps/services/multimedia/mediaplayer/status";

void drain(int fd, int ms) {
    pollfd p{fd, POLLIN, 0};
    while (poll(&p, 1, ms) > 0) {
        char buf[4096];
        const ssize_t n = read(fd, buf, sizeof buf - 1);
        if (n <= 0) {
            std::printf("  read: %zd (%s)\n", n, n < 0 ? std::strerror(errno) : "closed");
            return;
        }
        buf[n] = 0;
        std::printf("<<< %s", buf);
        if (buf[n - 1] != '\n') std::puts("");
        // Answer the overlay's button as a player would, so its glyph follows.
        // The buttons come as msg::track with the command in dat (pause, play, next, previous);
        // sendData and holdData are the overlay opening and closing.
        const char *answer = std::strstr(buf, "dat::pause") ? "msg::state\nid::90\ndat::paused\n"
                           : std::strstr(buf, "dat::play") ? "msg::state\nid::91\ndat::playing\n" : nullptr;
        if (answer) {
            const ssize_t w = write(fd, answer, std::strlen(answer));
            std::printf(">>> %s  wrote %zd\n", answer, w);
        }
        std::fflush(stdout);
        ms = 300;  // more may follow at once
    }
}

void show(const char *path) {
    char buf[2048];
    const int fd = open(path, O_RDONLY);
    const ssize_t n = fd >= 0 ? read(fd, buf, sizeof buf - 1) : -1;
    if (fd >= 0) close(fd);
    if (n > 0) {
        buf[n] = 0;
        std::printf("--- %s\n%s", path, buf);
    }
}

}  // namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        std::puts("usage: gate05 <seconds> <message> [message ...]");
        return 0;
    }
    std::printf("pid %d\n", int(getpid()));
    const int fd = open(kControl, O_RDWR);
    if (fd < 0) {
        std::printf("open %s: %s\n", kControl, std::strerror(errno));
        return 1;
    }
    for (int i = 2; i < argc; i++) {
        std::string m = argv[i];
        for (size_t p; (p = m.find("\\n")) != std::string::npos;) m.replace(p, 2, "\n");
        for (size_t p; (p = m.find("$PID")) != std::string::npos;) m.replace(p, 4, std::to_string(getpid()));
        if (m.empty() || m.back() != '\n') m += '\n';
        const ssize_t n = write(fd, m.data(), m.size());
        std::printf(">>> %s  wrote %zd%s%s\n", m.c_str(), n, n < 0 ? ": " : "", n < 0 ? std::strerror(errno) : "");
        std::fflush(stdout);
        drain(fd, 1500);
    }
    // Listen for the whole time: drain() returns 300 ms after each message, which ended the
    // first two runs as soon as the overlay opened and asked for the track's data.
    const time_t until = time(nullptr) + std::atoi(argv[1]);
    while (time(nullptr) < until) drain(fd, 1000);
    show(kStatus);
    close(fd);
    return 0;
}
