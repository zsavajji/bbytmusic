// The equalizer in front of the real sink. The Engine owns the Equalizer; sinks come and go.
#pragma once
#include <memory>
#include <vector>

#include "audio/equalizer.h"
#include "audio/sink.h"

namespace bbm {

class EqSink : public Sink {
public:
    EqSink(std::unique_ptr<Sink> inner, Equalizer &eq) : inner_(std::move(inner)), eq_(eq) {}
    bool open(int rate, int channels, std::string &err) override {
        channels_ = channels;
        eq_.reset(rate, channels);
        return inner_->open(rate, channels, err);
    }
    bool write(const int16_t *pcm, size_t frames, std::string &err) override {
        if (eq_.preset() == 0) return inner_->write(pcm, frames, err);   // off: no copy
        buf_.assign(pcm, pcm + frames * size_t(channels_));
        eq_.process(buf_.data(), frames);
        return inner_->write(buf_.data(), frames, err);
    }
    void close() override { inner_->close(); }

private:
    std::unique_ptr<Sink> inner_;
    Equalizer &eq_;
    int channels_ = 2;
    std::vector<int16_t> buf_;
};

}  // namespace bbm
