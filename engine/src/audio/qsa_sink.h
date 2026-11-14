// The BB10 speaker: libasound through audio_manager (phase0.md, gate 0.4). On the host open()
// fails with "not on this platform". Integer samples only.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "audio/sink.h"

namespace bbm {

class QsaSink : public Sink {
public:
    ~QsaSink() override { close(); }
    bool open(int rate, int channels, std::string &err) override;
    // Whole frames, interleaved S16. Buffers to whole fragments and blocks while writing them.
    // False with err "unplugged" after the route listener was told so, until the next open().
    bool write(const int16_t *interleaved, size_t frames, std::string &err) override;
    void close() override;   // flushes what the device has, frees the handle

    // Called (from write's thread) when the default output changed to the speaker from a
    // headset, headphones or A2DP (unplugged = true: playback stops) or otherwise (the
    // device was reopened and playback goes on).
    void set_route_listener(std::function<void(const std::string &from, const std::string &to, bool unplugged)> f) { listener_ = std::move(f); }
    int underruns() const { return underruns_; }

private:
    bool open_pcm(std::string &err);
    void close_pcm();
    bool check_route(std::string &err);   // false: stop writing (err set)

    std::function<void(const std::string &, const std::string &, bool)> listener_;
    [[maybe_unused]] void *pcm_ = nullptr;          // snd_pcm_t *
    [[maybe_unused]] unsigned handle_ = 0;          // audio_manager handle
    [[maybe_unused]] int route_ = 0;                // audio_manager_device_t
    [[maybe_unused]] int rate_ = 0, channels_ = 0, frag_ = 4096;
    [[maybe_unused]] uint64_t since_check_ = 0;     // frames written since the route was last looked at
    int underruns_ = 0;
    bool open_ = false;
    [[maybe_unused]] bool unplugged_ = false;
    std::vector<uint8_t> pending_; // bytes not yet a whole fragment
};

}  // namespace bbm
