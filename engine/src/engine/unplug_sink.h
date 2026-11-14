// Headphones out pauses (gate 0.6). QsaSink's write() fails with "unplugged" once the route went
// back to the speaker, and the player stops on a failed write; this wrapper turns that failure
// into a pause instead: it calls on_unplugged and swallows the writes until the player has
// paused. The next open() (on resume) starts the device again. Any other sink passes through.
#pragma once
#include <functional>
#include <memory>

#include "audio/sink.h"

namespace bbm {

class UnplugSink : public Sink {
public:
    UnplugSink(std::unique_ptr<Sink> inner, std::function<void()> on_unplugged)
        : inner_(std::move(inner)), on_unplugged_(std::move(on_unplugged)) {}
    bool open(int rate, int channels, std::string &err) override {
        unplugged_ = false;
        return inner_->open(rate, channels, err);
    }
    bool write(const int16_t *pcm, size_t frames, std::string &err) override {
        if (unplugged_) return true;
        if (inner_->write(pcm, frames, err)) return true;
        if (err != "unplugged") return false;
        unplugged_ = true;   // ponytail: the frames of this write and the next few are dropped
        on_unplugged_();
        return true;
    }
    void close() override { inner_->close(); }

private:
    std::unique_ptr<Sink> inner_;
    std::function<void()> on_unplugged_;
    bool unplugged_ = false;
};

}  // namespace bbm
