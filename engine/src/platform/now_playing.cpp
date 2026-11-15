#include "platform/now_playing.h"

#include "platform/np_protocol.h"
#include "util/log.h"

#ifdef __QNX__
#include <audio/audio_manager_device.h>
#include <audio/audio_manager_routing.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#endif

namespace bbm {

#ifdef __QNX__

static const char *kControl = "/pps/services/multimedia/mediaplayer/control";

bool NowPlaying::send(const std::function<std::string(int)> &build, std::string &err) {
    std::lock_guard<std::mutex> lock(mu_);
    if (fd_ < 0) { err = "now-playing is not open"; return false; }
    const std::string m = build(++id_);
    const ssize_t n = ::write(fd_, m.data(), m.size());
    if (n != ssize_t(m.size())) { err = std::string("write: ") + (n < 0 ? std::strerror(errno) : "short"); return false; }
    return true;
}

bool NowPlaying::start(const std::string &name, std::function<void(const std::string &)> on_command, std::string &err) {
    if (running_) { err = "already started"; return false; }
    fd_ = ::open(kControl, O_RDWR);
    if (fd_ < 0) { err = std::string("open ") + kControl + ": " + std::strerror(errno); return false; }
    on_command_ = std::move(on_command);
    id_ = 0;
    revoked_ = false;
    running_ = true;
    reader_ = std::thread(&NowPlaying::read_loop, this);
    const int pid = int(::getpid());
    if (!send([&](int id) { return np::register_msg(id, name, pid); }, err) || !acquire(err)) { stop(); return false; }
    return true;
}

bool NowPlaying::acquire(std::string &err) {
    if (!send(np::acquire_msg, err)) return false;
    revoked_ = false;
    return true;
}

void NowPlaying::send_metadata(std::string &err) {
    Meta m;
    { std::lock_guard<std::mutex> lock(mu_); m = meta_; }
    if (!m.set) return;
    send([&](int id) { return np::metadata_msg(id, m.title, m.artist, m.album, m.duration, m.position, m.next, m.prev, m.artwork); }, err);
}

void NowPlaying::set_metadata(const std::string &title, const std::string &artist, const std::string &album,
                              int64_t duration_ms, int64_t position_ms, bool next_enabled, bool prev_enabled,
                              const std::string &artwork_path) {
    { std::lock_guard<std::mutex> lock(mu_); meta_ = {title, artist, album, artwork_path, duration_ms, position_ms, next_enabled, prev_enabled, true}; }
    std::string err;
    send_metadata(err);
    if (!err.empty()) log_warn("now-playing metadata: " + err);
}

void NowPlaying::set_state(State s) {
    const char *name = s == State::Playing ? "playing" : s == State::Paused ? "paused" : "stopped";
    std::string err;
    if (!send([&](int id) { return np::state_msg(id, name); }, err)) log_warn("now-playing state: " + err);
}

void NowPlaying::read_loop() {
    std::string buf;
    char tmp[4096];
    while (running_) {
        pollfd p{fd_, POLLIN, 0};
        if (poll(&p, 1, 200) <= 0) continue;
        const ssize_t n = ::read(fd_, tmp, sizeof tmp);
        if (n <= 0) { log_warn("now-playing: control object closed"); break; }
        const np::Parsed r = np::parse_message(std::string_view(tmp, size_t(n)));
        switch (r.kind) {
            case np::Parsed::Reply:
                if (r.is_error) log_warn("now-playing replied err::" + r.error);
                break;
            case np::Parsed::SendData: {
                std::string err;
                send_metadata(err);
                break;
            }
            case np::Parsed::Revoked:
                revoked_ = true;
                if (on_command_) on_command_("revoked");
                break;
            case np::Parsed::Command:
                if (!revoked_ && on_command_) on_command_(r.command);
                break;
            default: break;
        }
    }
}

void NowPlaying::stop() {
    running_ = false;
    if (reader_.joinable()) reader_.join();   // not from on_command: it would join itself
    std::lock_guard<std::mutex> lock(mu_);
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
}

bool bluetooth_route() {
    audio_manager_device_t d = AUDIO_DEVICE_DEFAULT;
    return audio_manager_get_default_device(&d) == 0 && d == AUDIO_DEVICE_A2DP;
}

#else  // the host: nothing to talk to

bool bluetooth_route() { return false; }

bool NowPlaying::send(const std::function<std::string(int)> &, std::string &err) { err = "not on this platform"; return false; }
bool NowPlaying::start(const std::string &, std::function<void(const std::string &)>, std::string &err) { err = "not on this platform"; return false; }
bool NowPlaying::acquire(std::string &err) { err = "not on this platform"; return false; }
void NowPlaying::send_metadata(std::string &) {}
void NowPlaying::set_metadata(const std::string &, const std::string &, const std::string &, int64_t, int64_t, bool, bool, const std::string &) {}
void NowPlaying::set_state(State) {}
void NowPlaying::read_loop() {}
void NowPlaying::stop() {}

#endif

}  // namespace bbm
