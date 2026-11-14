// A Source over SABR: POSTs of a VideoPlaybackAbrRequest, answers in UMP, one segment (one
// WebM cluster) per request as of 2026-11 (phase0.md, gate 0.2).
#pragma once
#include <atomic>
#include <map>

#include "stream/http_send.h"
#include "stream/sabr_request.h"
#include "stream/source.h"
#include "stream/webm_index.h"

namespace bbm {

class SabrSource : public Source {
public:
    explicit SabrSource(StreamInfo info, HttpSend send = http_sender());
    bool open(std::string &err) override;   // asks from 0 ms until the init segment comes
    const std::vector<uint8_t> &init() const override { return init_; }
    int segment_count() const override { return int(index_.segments.size()); }
    int64_t duration_ms() const override { return index_.duration_ms; }
    int segment_at(int64_t ms) const override { return index_.segment_at(ms); }
    // The segment after the last one received continues the session; any other is a seek:
    // player time goes to its start, and only what is held from there on counts as buffered.
    bool read(int index, Segment &out, std::string &err) override;
    // Aborts the open() or read() under way; one that starts afterwards runs normally.
    void cancel() override { cancelled_ = true; }

    int requests() const { return rn_; }   // POSTs so far

private:
    struct Held { int64_t start_ms = 0, duration_ms = 0; std::vector<uint8_t> bytes; };
    struct Pending { SabrMediaHeader header; std::vector<uint8_t> bytes; };

    bool fetch(int segment, std::string &err);           // requests until `segment` is held
    bool request(int64_t target_ms, std::string &err);   // one POST, its parts handled
    bool on_part(uint32_t type, std::span<const uint8_t> payload, std::string &err);
    void finish(Pending &p);                             // a MEDIA_END: keep what came
    void restart_run(int segment);                       // a seek: buffered = what is held from there
    bool wait_backoff();                                 // false if cancelled meanwhile

    StreamInfo info_;
    HttpSend send_;
    std::string url_;
    int rn_ = 0;
    std::vector<uint8_t> cookie_;
    int64_t backoff_ms_ = 0;
    int end_segment_ = 0;                                // from FORMAT_INITIALIZATION_METADATA
    std::vector<uint8_t> init_;
    WebmIndex index_;
    std::map<uint32_t, Pending> pending_;                // by header id, until MEDIA_END
    std::map<int, Held> held_;                           // by SABR segment number (index + 1)
    int got_ = 0;                                        // segments received by the last request
    // The run of segments the server has sent from the play position on: SABR's "buffered".
    int run_first_ = 0, run_last_ = 0;
    int64_t run_start_ms_ = 0, run_end_ms_ = 0;
    std::atomic<bool> cancelled_{false};
};

}  // namespace bbm
