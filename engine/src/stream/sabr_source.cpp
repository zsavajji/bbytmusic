#include "stream/sabr_source.h"

#include <algorithm>
#include <chrono>
#include <thread>
#include <utility>

#include "proto/pb.h"
#include "stream/ump.h"
#include "util/log.h"

namespace bbm {
namespace {
constexpr int kMaxRequests = 30;   // per open() or per segment
constexpr int kMaxEmpty = 5;       // answers in a row that bring nothing new
}  // namespace

SabrSource::SabrSource(StreamInfo info, HttpSend send)
    : info_(std::move(info)), send_(std::move(send)), url_(info_.sabr_url) {}

bool SabrSource::open(std::string &err) {
    cancelled_ = false;
    if (url_.empty() || info_.ustreamer_config.empty() || info_.itag <= 0 || info_.last_modified <= 0) {
        err = "sabr: no SABR url, ustreamer config or format for itag " + std::to_string(info_.itag);
        return false;
    }
    for (int n = 0, empties = 0; init_.empty(); n++) {
        if (n >= kMaxRequests || empties >= kMaxEmpty) {
            err = "sabr: no init segment after " + std::to_string(n) + " requests";
            return false;
        }
        if (!wait_backoff()) {
            err = "sabr: cancelled";
            return false;
        }
        got_ = 0;
        if (!request(0, err)) return false;
        empties = got_ || !init_.empty() ? 0 : empties + 1;
    }
    // Without a content length the last segment's size is unknown; read() then skips its check.
    int64_t len = info_.content_length > 0 ? info_.content_length : INT64_MAX;
    if (!parse_webm_index(init_, len, info_.duration_ms, index_, err)) {
        err = "sabr: init segment: " + err;
        init_.clear();
        return false;
    }
    return true;
}

bool SabrSource::read(int index, Segment &out, std::string &err) {
    cancelled_ = false;
    if (index < 0 || index >= segment_count()) {
        err = "sabr: no segment " + std::to_string(index);
        return false;
    }
    const int seg = index + 1;
    if (!held_.count(seg)) {
        if (run_last_ == 0 || seg != run_last_ + 1) restart_run(seg);
        if (!fetch(seg, err)) return false;
    }
    std::vector<uint8_t> bytes = std::move(held_[seg].bytes);
    held_.erase(held_.begin(), held_.upper_bound(seg));  // behind the play position now
    const SegmentRange &s = index_.segments[size_t(index)];
    if (info_.content_length > 0 && int64_t(bytes.size()) != s.size) {
        err = "sabr: segment " + std::to_string(seg) + " is " + std::to_string(bytes.size()) +
              " bytes, the cues say " + std::to_string(s.size);
        return false;
    }
    out.index = index;
    out.start_ms = s.start_ms;
    out.duration_ms = s.duration_ms;
    out.bytes = std::move(bytes);
    return true;
}

void SabrSource::restart_run(int segment) {
    run_first_ = run_last_ = 0;
    for (auto it = held_.find(segment); it != held_.end() && it->first == segment; ++it, ++segment) {
        if (!run_last_) {
            run_first_ = segment;
            run_start_ms_ = it->second.start_ms;
        }
        run_last_ = segment;
        run_end_ms_ = it->second.start_ms + it->second.duration_ms;
    }
}

bool SabrSource::fetch(int segment, std::string &err) {
    const int64_t target_ms = index_.segments[size_t(segment - 1)].start_ms;
    for (int n = 0, empties = 0; !held_.count(segment); n++) {
        const std::string what = "sabr: segment " + std::to_string(segment) + ": ";
        if (end_segment_ && segment > end_segment_) {
            err = what + "past the last one, " + std::to_string(end_segment_);
            return false;
        }
        if (n >= kMaxRequests || empties >= kMaxEmpty) {
            err = what + "not received after " + std::to_string(n) + " requests";
            return false;
        }
        if (!wait_backoff()) {
            err = "sabr: cancelled";
            return false;
        }
        got_ = 0;
        if (!request(target_ms, err)) return false;
        empties = got_ ? 0 : empties + 1;
    }
    return true;
}

bool SabrSource::wait_backoff() {
    for (int64_t left = backoff_ms_; left > 0 && !cancelled_; left -= 50)
        std::this_thread::sleep_for(std::chrono::milliseconds(std::min<int64_t>(left, 50)));
    backoff_ms_ = 0;
    return !cancelled_;
}

bool SabrSource::request(int64_t target_ms, std::string &err) {
    SabrRequestState st;
    st.initialized = !init_.empty();
    st.cookie = cookie_;
    if (run_last_ > 0) {  // continue from the end of the run
        st.player_ms = run_end_ms_;
        st.buffered_first = run_first_;
        st.buffered_last = run_last_;
        st.buffered_start_ms = run_start_ms_;
        st.buffered_duration_ms = run_end_ms_ - run_start_ms_;
    } else {
        st.player_ms = target_ms;
    }
    std::vector<uint8_t> body;
    if (!build_sabr_request(info_, st, body, err)) return false;
    HttpRequest req;
    req.method = "POST";
    req.url = url_ + "&rn=" + std::to_string(++rn_);
    req.headers = {{"User-Agent", info_.user_agent},
                   {"Content-Type", "application/x-protobuf"},
                   {"Accept", "application/vnd.yt-ump"},
                   {"Accept-Encoding", "identity"}};
    req.body.assign(body.begin(), body.end());
    req.cancel = &cancelled_;
    UmpReader ump;
    std::string part_err;
    pending_.clear();
    req.on_data = [&](const uint8_t *data, size_t size) {
        ump.feed({data, size});
        uint32_t type;
        std::span<const uint8_t> payload;
        while (ump.next(type, payload))
            if (!on_part(type, payload, part_err)) return false;
        return true;
    };
    HttpResponse r = send_(req);
    log_debug("sabr: rn=" + std::to_string(rn_) + " player_ms=" + std::to_string(st.player_ms) + " buffered=" +
              std::to_string(st.buffered_first) + "-" + std::to_string(st.buffered_last) + " http " +
              std::to_string(r.status) + ", " + std::to_string(got_) + " new segments");
    // The status first: a refusal's body is not UMP, and what it parsed to means nothing.
    if (r.status != 0 && r.status != 200) {
        err = "sabr: http " + std::to_string(r.status);
        return false;
    }
    if (!part_err.empty() || !r.error.empty()) {
        err = part_err.empty() ? "sabr: " + r.error : part_err;
        return false;
    }
    if (ump.pending())
        log_warn("sabr: rn=" + std::to_string(rn_) + " cut short, " + std::to_string(ump.pending()) +
                 " bytes of a part dropped");
    return true;
}

bool SabrSource::on_part(uint32_t type, std::span<const uint8_t> payload, std::string &err) {
    switch (type) {
    case ump::MEDIA_HEADER: {
        SabrMediaHeader h;
        if (parse_media_header(payload, h)) pending_[h.header_id] = Pending{h, {}};
        return true;
    }
    case ump::MEDIA:  // header id (one byte), then bytes
        if (auto it = payload.empty() ? pending_.end() : pending_.find(payload[0]); it != pending_.end())
            it->second.bytes.insert(it->second.bytes.end(), payload.begin() + 1, payload.end());
        return true;
    case ump::MEDIA_END:
        if (auto it = payload.empty() ? pending_.end() : pending_.find(payload[0]); it != pending_.end()) {
            finish(it->second);
            pending_.erase(it);
        }
        return true;
    case ump::NEXT_REQUEST_POLICY: {
        SabrPolicy p;
        if (parse_next_request_policy(payload, p)) {
            backoff_ms_ = p.backoff_ms;
            if (p.has_cookie) cookie_ = std::move(p.cookie);
        }
        return true;
    }
    case ump::FORMAT_INITIALIZATION_METADATA:
        if (int last = parse_end_segment(payload, info_.itag)) end_segment_ = last;
        return true;
    case ump::SABR_REDIRECT:
        if (PbMsg m(payload); !m.str(1).empty()) {
            url_ = std::string(m.str(1));
            log_info("sabr: redirected");
        }
        return true;
    case ump::SABR_SEEK: {  // the server moving the player (live streams); logged, not followed
        PbMsg m(payload);
        log_debug("sabr: SABR_SEEK to " + std::to_string(m.u(1)) + "/" + std::to_string(m.u(2, 1000)));
        return true;
    }
    case ump::STREAM_PROTECTION_STATUS:
        if (PbMsg(payload).u(1) == 2) log_warn("sabr: attestation pending (stream protection status 2)");
        break;
    default:
        break;
    }
    err = sabr_part_error(type, payload);
    return err.empty();
}

void SabrSource::finish(Pending &p) {
    const SabrMediaHeader &h = p.header;
    if (h.itag != info_.itag) return;
    if (h.is_init) {
        if (init_.empty()) init_ = std::move(p.bytes);
        return;
    }
    if (h.segment <= 0 || held_.count(h.segment)) return;
    held_[h.segment] = Held{h.start_ms, h.duration_ms, std::move(p.bytes)};
    got_++;
    if (run_last_ == 0) {
        run_first_ = h.segment;
        run_start_ms_ = h.start_ms;
    } else if (h.segment != run_last_ + 1) {
        return;  // not where the run goes on: held, but not reported as buffered
    }
    run_last_ = h.segment;
    run_end_ms_ = h.start_ms + h.duration_ms;
}

}  // namespace bbm
