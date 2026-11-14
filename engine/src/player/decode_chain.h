// WebM bytes in, interleaved 48 kHz PCM out, for one track at a time: the demuxer and the Opus
// decoder with the skip and the position bookkeeping that a seek needs. Not thread-safe.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "media/opus_decoder.h"
#include "media/webm_demux.h"

namespace bbm {

class DecodeChain {
public:
    enum Result { Frames, NeedData, Error };

    void reset() { dm_.reset(); }   // drop buffered bytes (after a seek or a failure)
    // Starts a track from its init bytes. first_segment 0 skips the file's pre_skip; any other
    // segment skips the decoder's pre-roll. Frames before target_ms are decoded and dropped.
    bool begin(const std::vector<uint8_t> &init, int first_segment, int64_t target_ms, std::string &err);
    bool feed(const std::vector<uint8_t> &bytes, std::string &err);
    // Decodes one packet; pcm is replaced (it can come back empty while frames are dropped).
    Result next(std::vector<int16_t> &pcm, std::string &err);
    int channels() const { return dm_.channels(); }
    int64_t position_ms() const { return cursor_ / 48; }   // track time of the next frame

private:
    WebmDemuxer dm_;
    OpusDecoder dec_;
    int64_t cursor_ = 0, target_ = 0;   // in frames at 48 kHz
    int skip_ = 0;
    bool first_ = true;
};

}  // namespace bbm
