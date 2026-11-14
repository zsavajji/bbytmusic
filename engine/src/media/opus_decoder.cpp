#include "media/opus_decoder.h"

#include <opus/opus.h>

namespace bbm {
namespace {
constexpr int kMaxFrames = 5760;   // 120 ms at 48 kHz
}

OpusDecoder::~OpusDecoder() {
    if (dec_) opus_decoder_destroy(dec_);
}

bool OpusDecoder::init(int channels, std::string &err) {
    if (channels < 1 || channels > 2) { err = "opus: unsupported channel count"; return false; }
    if (dec_) { opus_decoder_destroy(dec_); dec_ = nullptr; }
    int e = 0;
    dec_ = opus_decoder_create(48000, channels, &e);
    if (!dec_ || e != OPUS_OK) {
        dec_ = nullptr;
        err = std::string("opus: ") + opus_strerror(e);
        return false;
    }
    channels_ = channels;
    skip_ = 0;
    return true;
}

bool OpusDecoder::decode(const Packet &p, std::vector<int16_t> &pcm, std::string &err) {
    if (!dec_) { err = "opus: not initialised"; return false; }
    if (!p.data || p.size == 0 || p.size > 0x7fffffff) { err = "opus: empty packet"; return false; }
    const size_t base = pcm.size();
    pcm.resize(base + size_t(kMaxFrames) * channels_);
    const int n = opus_decode(dec_, p.data, static_cast<opus_int32>(p.size), pcm.data() + base, kMaxFrames, 0);
    if (n < 0) {
        pcm.resize(base);
        err = std::string("opus: ") + opus_strerror(n);
        return false;
    }
    const int drop = n < skip_ ? n : skip_;
    skip_ -= drop;
    pcm.resize(base + size_t(n) * channels_);
    if (drop) pcm.erase(pcm.begin() + base, pcm.begin() + base + size_t(drop) * channels_);
    return true;
}

void OpusDecoder::reset() {
    if (dec_) opus_decoder_ctl(dec_, OPUS_RESET_STATE);
    skip_ = 0;
}

}  // namespace bbm
