// The system's now-playing service, spoken to directly over PPS (phase0.md, gate 0.5): the
// overlay shows our metadata and its buttons arrive as commands. BB10 only; on the host
// start() fails with "not on this platform".
#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace bbm {

class NowPlaying {
public:
    enum class State { Playing, Paused, Stopped };
    ~NowPlaying() { stop(); }

    // Opens the control object, registers, acquires and starts the reader thread. on_command gets
    // "play", "pause", "next", "prev" and "revoked" (another player took over: nothing more is
    // sent until acquire() is called again). It runs on the reader thread.
    bool start(const std::string &name, std::function<void(const std::string &command)> on_command, std::string &err);
    void set_metadata(const std::string &title, const std::string &artist, const std::string &album,
                      int64_t duration_ms, int64_t position_ms, bool next_enabled, bool prev_enabled,
                      const std::string &artwork_path = "");
    void set_state(State s);
    bool acquire(std::string &err);
    void stop();

private:
    // build(id) makes the message; the id and the write happen under one lock, so ids rise.
    bool send(const std::function<std::string(int)> &build, std::string &err);
    void send_metadata(std::string &err);
    void read_loop();

    struct Meta {
        std::string title, artist, album, artwork;
        int64_t duration = 0, position = 0;
        bool next = false, prev = false, set = false;
    };
    int fd_ = -1;
    std::mutex mu_;
    int id_ = 0;
    Meta meta_;   // the last metadata, re-sent when the overlay asks for it (sendData)
    std::function<void(const std::string &)> on_command_;
    std::atomic<bool> running_{false}, revoked_{false};
    std::thread reader_;
};

// Whether the sound goes out over Bluetooth (A2DP): there the same metadata reaches the car or the
// speaker (AVRCP), which shows the title itself. False off BB10.
bool bluetooth_route();

}  // namespace bbm
