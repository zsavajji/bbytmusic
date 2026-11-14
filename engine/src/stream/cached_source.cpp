#include "stream/cached_source.h"

#include "util/log.h"

namespace bbm {

CachedSource::CachedSource(SegmentCache &cache, std::string video_id, Inner inner)
    : cache_(cache), id_(std::move(video_id)), make_(std::move(inner)) {}

CachedSource::~CachedSource() {
    if (!key_.empty()) cache_.unpin(key_);
}

bool CachedSource::open_inner(std::string &err) {
    StreamInfo info;
    std::unique_ptr<Source> src = make_(info, err);
    if (!src) {
        if (err.empty()) err = "no stream for " + id_;
        return false;
    }
    Source *raw = src.get();
    {
        std::lock_guard<std::mutex> lk(m_);
        inner_ = std::move(src);
    }
    cache_.fetches++;
    if (cancelled_ || !raw->open(err) || cancelled_) {
        if (err.empty()) err = "cancelled";
        std::lock_guard<std::mutex> lk(m_);
        inner_.reset();
        return false;
    }
    if (passthrough()) {   // open(): the head is new
        init_ = raw->init();
        WebmIndex idx;
        std::string perr;
        if (!SegmentCache::valid_id(id_) || info.content_length <= 0 ||
            !parse_webm_index(init_, info.content_length, info.duration_ms, idx, perr) ||
            int(idx.segments.size()) != raw->segment_count()) {
            log_warn("cache: " + id_ + " is not cached (" + (perr.empty() ? "no length or index" : perr) + ")");
            return true;
        }
        idx.duration_ms = raw->duration_ms();
        meta_ = {info.itag, info.content_length, idx.duration_ms, info.last_modified, int64_t(init_.size()), idx.segments};
        const std::string key = id_ + "." + std::to_string(info.itag);
        if (cache_.create_entry(key, meta_, init_)) {
            key_ = key;
            index_ = std::move(idx);
        }
        return true;
    }
    // read(): a gap in a cached track. The stream must be the file whose head the player has.
    const bool same = info.itag == meta_.itag && info.content_length == meta_.content_length &&
                      raw->segment_count() == int(meta_.segments.size()) &&
                      (!info.last_modified || !meta_.last_modified || info.last_modified == meta_.last_modified);
    if (same) return true;
    err = "cache: " + key_ + " changed on the server; dropped";
    log_warn(err);
    cache_.unpin(key_);
    cache_.remove(key_);
    key_.clear();
    std::lock_guard<std::mutex> lk(m_);
    inner_.reset();
    return false;
}

bool CachedSource::open(std::string &err) {
    cancelled_ = false;
    if (SegmentCache::valid_id(id_) && cache_.open_entry(id_, key_, meta_, init_)) {
        index_.duration_ms = meta_.duration_ms;
        index_.segments = meta_.segments;
        return true;
    }
    return open_inner(err);
}

int CachedSource::segment_count() const {
    if (passthrough()) return inner_ ? inner_->segment_count() : 0;
    return int(index_.segments.size());
}

int64_t CachedSource::duration_ms() const {
    if (passthrough()) return inner_ ? inner_->duration_ms() : 0;
    return index_.duration_ms;
}

int CachedSource::segment_at(int64_t ms) const {
    if (passthrough()) return inner_ ? inner_->segment_at(ms) : -1;
    return index_.segment_at(ms);
}

bool CachedSource::read(int index, Segment &out, std::string &err) {
    cancelled_ = false;
    if (passthrough()) {
        if (!inner_) { err = "cache: " + id_ + " is not open"; return false; }
        cache_.fetches++;
        return inner_->read(index, out, err);
    }
    if (index < 0 || index >= int(index_.segments.size())) {
        err = "cache: no segment " + std::to_string(index);
        return false;
    }
    const SegmentRange &r = index_.segments[size_t(index)];
    if (cache_.get_segment(key_, index, r.size, out.bytes)) {
        out.index = index;
        out.start_ms = r.start_ms;
        out.duration_ms = r.duration_ms;
        return true;
    }
    if (!inner_ && !open_inner(err)) return false;
    cache_.fetches++;
    Segment seg;
    if (!inner_->read(index, seg, err)) return false;   // a failed read is not kept
    if (int64_t(seg.bytes.size()) == r.size) cache_.put_segment(key_, index, seg.bytes);
    out = std::move(seg);
    return true;
}

void CachedSource::cancel() {
    cancelled_ = true;
    std::lock_guard<std::mutex> lk(m_);
    if (inner_) inner_->cancel();
}

}  // namespace bbm
