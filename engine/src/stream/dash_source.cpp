#include "stream/dash_source.h"

#include <utility>

namespace bbm {

DashSource::DashSource(StreamInfo info, HttpSend send) : info_(std::move(info)), send_(std::move(send)) {}

bool DashSource::get(int64_t first, int64_t last, std::vector<uint8_t> &out, std::string &err) {
    HttpRequest req;
    req.url = info_.url;
    req.headers = {{"User-Agent", info_.user_agent},
                   {"Range", "bytes=" + std::to_string(first) + "-" + std::to_string(last)},
                   {"Accept-Encoding", "identity"}};
    req.cancel = &cancelled_;
    HttpResponse r = send_(req);
    const std::string what = "dash: bytes " + std::to_string(first) + "-" + std::to_string(last) + ": ";
    if (!r.error.empty()) {
        err = what + r.error;
        return false;
    }
    if (r.status != 206 && r.status != 200) {
        err = what + "http " + std::to_string(r.status);
        return false;
    }
    if (int64_t(r.body.size()) != last - first + 1) {  // a 200 that ignored the Range lands here too
        err = what + "got " + std::to_string(r.body.size()) + " bytes";
        return false;
    }
    out.assign(r.body.begin(), r.body.end());
    return true;
}

bool DashSource::open(std::string &err) {
    cancelled_ = false;
    if (info_.url.empty() || info_.index_end <= 0 || info_.content_length <= info_.index_end) {
        err = "dash: no url or ranges for itag " + std::to_string(info_.itag);
        return false;
    }
    std::vector<uint8_t> head;
    WebmIndex index;
    if (!get(0, info_.index_end, head, err) ||
        !parse_webm_index(head, info_.content_length, info_.duration_ms, index, err))
        return false;
    init_ = std::move(head);
    index_ = std::move(index);
    return true;
}

bool DashSource::read(int index, Segment &out, std::string &err) {
    cancelled_ = false;
    if (index < 0 || index >= segment_count()) {
        err = "dash: no segment " + std::to_string(index);
        return false;
    }
    const SegmentRange &s = index_.segments[size_t(index)];
    std::vector<uint8_t> bytes;
    if (!get(s.offset, s.offset + s.size - 1, bytes, err)) return false;
    out.index = index;
    out.start_ms = s.start_ms;
    out.duration_ms = s.duration_ms;
    out.bytes = std::move(bytes);
    return true;
}

}  // namespace bbm
