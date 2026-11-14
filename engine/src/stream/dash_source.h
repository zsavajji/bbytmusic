// A Source over a direct url: byte ranges, one GET per segment.
#pragma once
#include <atomic>

#include "stream/http_send.h"
#include "stream/source.h"
#include "stream/webm_index.h"

namespace bbm {

class DashSource : public Source {
public:
    explicit DashSource(StreamInfo info, HttpSend send = http_sender());
    bool open(std::string &err) override;   // GETs bytes 0..index_end: the head, then the cues
    const std::vector<uint8_t> &init() const override { return init_; }
    int segment_count() const override { return int(index_.segments.size()); }
    int64_t duration_ms() const override { return index_.duration_ms; }
    int segment_at(int64_t ms) const override { return index_.segment_at(ms); }
    bool read(int index, Segment &out, std::string &err) override;
    // Aborts the open() or read() under way; one that starts afterwards runs normally.
    void cancel() override { cancelled_ = true; }

private:
    bool get(int64_t first, int64_t last, std::vector<uint8_t> &out, std::string &err);
    StreamInfo info_;
    HttpSend send_;
    std::vector<uint8_t> init_;
    WebmIndex index_;
    std::atomic<bool> cancelled_{false};
};

}  // namespace bbm
