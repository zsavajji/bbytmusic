// A Source in front of the segment cache. open() takes the head from the disk when the cache has
// the track; read() takes each segment from the disk when it is there. Only for what is missing
// does it call the factory of the real Source (resolve + make_source), open that and read
// through it, keeping what it read. A track that is all on disk plays with no network at all.
#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include "stream/segment_cache.h"
#include "stream/source.h"
#include "stream/webm_index.h"

namespace bbm {

class CachedSource : public Source {
public:
    // Makes the real Source (not yet opened) and fills `info` with what resolve() learned.
    using Inner = std::function<std::unique_ptr<Source>(StreamInfo &info, std::string &err)>;

    CachedSource(SegmentCache &cache, std::string video_id, Inner inner);
    ~CachedSource() override;   // unpins the track

    bool open(std::string &err) override;
    const std::vector<uint8_t> &init() const override { return init_; }
    int segment_count() const override;
    int64_t duration_ms() const override;
    int segment_at(int64_t ms) const override;
    bool read(int index, Segment &out, std::string &err) override;
    void cancel() override;   // passes through to the real Source, if there is one yet

private:
    bool open_inner(std::string &err);   // the factory, then open(); checks it against the cached head
    bool passthrough() const { return key_.empty(); }   // nothing of this track is kept

    SegmentCache &cache_;
    const std::string id_;
    Inner make_;
    std::string key_;              // "<video_id>.<itag>" while cached and pinned
    CacheMeta meta_;
    WebmIndex index_;
    std::vector<uint8_t> init_;
    std::mutex m_;                 // guards inner_ against cancel() from another thread
    std::unique_ptr<Source> inner_;
    std::atomic<bool> cancelled_{false};
};

}  // namespace bbm
