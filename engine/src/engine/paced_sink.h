// A sink that lets audio pass at the sample rate in front of one that does not (a WAV file, a
// null sink), so `bbmusicd --wav` and `--null` play in real time as the speaker would. After a
// stall (a track loading) it goes on at the sample rate from then, as a device that underran
// would, instead of catching up in a burst.
#pragma once
#include <algorithm>
#include <chrono>
#include <memory>
#include <thread>

#include "audio/sink.h"

namespace bbm {

class PacedSink : public Sink {
public:
    explicit PacedSink(std::unique_ptr<Sink> inner) : inner_(std::move(inner)) {}
    bool open(int rate, int channels, std::string &err) override {
        rate_ = rate;
        due_ = {};
        return inner_->open(rate, channels, err);
    }
    bool write(const int16_t *pcm, size_t frames, std::string &err) override {
        if (!inner_->write(pcm, frames, err)) return false;
        due_ = std::max(due_, std::chrono::steady_clock::now()) + std::chrono::microseconds(frames * 1000000 / size_t(rate_));
        std::this_thread::sleep_until(due_);
        return true;
    }
    void close() override { inner_->close(); }

private:
    std::unique_ptr<Sink> inner_;
    int rate_ = 48000;
    std::chrono::steady_clock::time_point due_;
};

}  // namespace bbm
