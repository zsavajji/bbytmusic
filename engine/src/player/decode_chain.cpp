#include "player/decode_chain.h"

#include <algorithm>

namespace bbm {

bool DecodeChain::begin(const std::vector<uint8_t> &init, int first_segment, int64_t target_ms,
                        std::string &err) {
    dm_.reset();
    if (!dm_.feed(init, err)) return false;
    if (!dm_.has_header()) { err = "webm: no Opus header in the init bytes"; return false; }
    if (!dec_.init(dm_.channels(), err)) return false;
    int64_t preroll = dm_.seek_preroll_ns() * 48 / 1000000;
    if (preroll <= 0) preroll = 3840;   // 80 ms
    skip_ = first_segment == 0 ? dm_.pre_skip() : int(preroll);
    dec_.set_skip(skip_);
    target_ = std::max<int64_t>(target_ms, 0) * 48;
    cursor_ = 0;
    first_ = true;
    return true;
}

bool DecodeChain::feed(const std::vector<uint8_t> &bytes, std::string &err) {
    return dm_.feed(bytes, err);
}

DecodeChain::Result DecodeChain::next(std::vector<int16_t> &pcm, std::string &err) {
    pcm.clear();
    Packet p;
    if (!dm_.next(p)) {
        if (!dm_.error().empty()) { err = dm_.error(); return Error; }
        return NeedData;
    }
    // A packet at pts p holds frames p*48 - pre_skip + i; the decoder drops the first skip_.
    if (first_) {
        cursor_ = p.pts_ms * 48 - dm_.pre_skip() + skip_;
        first_ = false;
    }
    if (!dec_.decode(p, pcm, err)) return Error;
    const int ch = dec_.channels();
    const int64_t n = int64_t(pcm.size()) / ch;
    if (cursor_ < target_) {
        const int64_t drop = std::min(n, target_ - cursor_);
        pcm.erase(pcm.begin(), pcm.begin() + drop * ch);
    }
    cursor_ += n;
    return Frames;
}

}  // namespace bbm
