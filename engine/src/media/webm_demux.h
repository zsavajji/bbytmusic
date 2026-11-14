// Incremental WebM demuxer: bytes in (any chunking), Opus packets out.
#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace bbm {

struct Packet { int64_t pts_ms = 0; const uint8_t *data = nullptr; size_t size = 0; };

class WebmDemuxer {
public:
    // Appends bytes. False (with err) when the stream was found malformed or buffering overflows;
    // after that only reset() makes it usable again.
    bool feed(std::span<const uint8_t> bytes, std::string &err);
    // Next complete packet of track 1; data is valid until the next feed()/next().
    bool next(Packet &out);
    // Drops buffered bytes and parse state, keeps the track info (after a seek).
    void reset();

    bool has_header() const { return have_header_; }
    int channels() const { return channels_; }
    int pre_skip() const { return pre_skip_; }
    int64_t codec_delay_ns() const { return codec_delay_ns_; }
    int64_t seek_preroll_ns() const { return seek_preroll_ns_; }
    const std::string &error() const { return error_; }

private:
    bool step(Packet &out, bool &produced, bool take_blocks);   // one element; false = need more bytes or error
    void compact();
    void fail(const char *why) { if (error_.empty()) error_ = why; }

    std::vector<uint8_t> buf_;
    size_t pos_ = 0;                // consumed prefix of buf_
    uint64_t skip_ = 0;             // bytes of a skipped element still to drop
    int64_t cluster_tc_ = 0;
    int64_t scale_ns_ = 1000000;
    bool have_header_ = false;
    int channels_ = 2, pre_skip_ = 0;
    int64_t codec_delay_ns_ = 0, seek_preroll_ns_ = 0;
    std::string error_;
};

}  // namespace bbm
