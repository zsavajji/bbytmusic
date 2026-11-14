// Discards audio. With realtime=true write() sleeps so frames pass at the sample rate.
#pragma once
#include <chrono>

#include "audio/sink.h"

namespace bbm {

class NullSink : public Sink {
public:
    explicit NullSink(bool realtime = false) : realtime_(realtime) {}
    bool open(int rate, int channels, std::string &err) override;
    bool write(const int16_t *interleaved, size_t frames, std::string &err) override;
    void close() override { open_ = false; }
    uint64_t frames_written() const { return frames_; }

private:
    bool realtime_, open_ = false;
    int rate_ = 0;
    uint64_t frames_ = 0;
    std::chrono::steady_clock::time_point start_;
};

}  // namespace bbm
