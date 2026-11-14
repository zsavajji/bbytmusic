// The segment cache on disk (PLAN.md §4.5, step 1.10). Under one directory, a folder per
// `<video_id>.<itag>` holding `init` (the file's head), `meta` (the numbers and the segment
// table) and one file per segment (`0`, `1`, …); every file is written to a temp file and renamed.
// A fixed cap in bytes, kept by evicting whole tracks, least recently used first, never one that
// is pinned (open). Thread-safe.
#pragma once
#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "stream/webm_index.h"

namespace bbm {

struct CacheMeta {
    int itag = 0;
    int64_t content_length = 0, duration_ms = 0, last_modified = 0, init_size = 0;
    std::vector<SegmentRange> segments;   // start_ms, duration_ms, size; offsets are not kept
};

class SegmentCache {
public:
    // Creates the directory if needed, forgets folders without a meta and leftover temp files,
    // and evicts down to the cap.
    SegmentCache(std::string dir, int64_t cap_bytes);
    SegmentCache(const SegmentCache &) = delete;
    SegmentCache &operator=(const SegmentCache &) = delete;

    // The most recently used entry of video_id whose meta reads back and agrees with its init
    // file and content length; an entry that does not is deleted. Pins it on success.
    bool open_entry(const std::string &video_id, std::string &key, CacheMeta &meta, std::vector<uint8_t> &init);
    // Writes key's head, replacing anything there, and pins it. False if it cannot fit or a write failed.
    bool create_entry(const std::string &key, const CacheMeta &meta, const std::vector<uint8_t> &init);
    void unpin(const std::string &key);
    void remove(const std::string &key);

    // Segment `index` of key if its file holds exactly `size` bytes; a file of another size is deleted.
    bool get_segment(const std::string &key, int index, int64_t size, std::vector<uint8_t> &out);
    // Kept if it fits under the cap after evicting unpinned tracks; else dropped.
    void put_segment(const std::string &key, int index, const std::vector<uint8_t> &bytes);

    int64_t bytes() const;
    bool has(const std::string &key) const;
    static bool valid_id(std::string_view id);   // [A-Za-z0-9_-]+: it becomes a folder name

    std::atomic<int> fetches{0};   // what the CachedSources asked of the network: inner open()s and read()s

private:
    struct Entry { int64_t bytes = 0; uint64_t used = 0; int pins = 0; };
    std::string path(const std::string &key, const std::string &file = "") const;
    void touch(const std::string &key, Entry &e);
    bool make_room(int64_t need, const std::string &keep);   // m_ held
    void erase(const std::string &key);                      // m_ held: the folder and the entry
    void scan();

    const std::string dir_;
    const int64_t cap_;
    mutable std::mutex m_;   // ponytail: one lock around the file IO too; per-entry locks if two tracks ever read at once
    std::map<std::string, Entry> entries_;
    int64_t total_ = 0;
    uint64_t clock_ = 0;
};

}  // namespace bbm
