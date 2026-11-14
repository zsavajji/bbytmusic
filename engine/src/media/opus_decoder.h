// libopus behind a small class; integer samples only.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "media/webm_demux.h"

struct OpusDecoder;   // libopus's own (global) type

namespace bbm {

class OpusDecoder {
public:
    OpusDecoder() = default;
    ~OpusDecoder();
    OpusDecoder(const OpusDecoder &) = delete;
    OpusDecoder &operator=(const OpusDecoder &) = delete;

    bool init(int channels, std::string &err);
    // Appends interleaved 48 kHz samples to pcm (minus any frames still to skip).
    bool decode(const Packet &p, std::vector<int16_t> &pcm, std::string &err);
    // Resets decoder state and the pending skip (call set_skip again after).
    void reset();
    // Drop this many leading frames of the coming output (pre-skip, or pre-roll after a seek).
    void set_skip(int samples) { skip_ = samples > 0 ? samples : 0; }
    int channels() const { return channels_; }

private:
    ::OpusDecoder *dec_ = nullptr;
    int channels_ = 0;
    int skip_ = 0;
};

}  // namespace bbm
