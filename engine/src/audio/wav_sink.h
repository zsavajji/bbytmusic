// Writes 16-bit PCM to a WAV file; sizes in the header are fixed up on close().
#pragma once
#include <cstdio>
#include <string>

#include "audio/sink.h"

namespace bbm {

class WavSink : public Sink {
public:
    explicit WavSink(std::string path) : path_(std::move(path)) {}
    ~WavSink() override { close(); }
    bool open(int rate, int channels, std::string &err) override;
    bool write(const int16_t *interleaved, size_t frames, std::string &err) override;
    void close() override;

private:
    std::string path_;
    FILE *f_ = nullptr;
    int channels_ = 0, rate_ = 0;
    uint64_t data_bytes_ = 0;
};

}  // namespace bbm
