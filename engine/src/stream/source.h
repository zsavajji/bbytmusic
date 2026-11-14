// One track's audio as the container's own bytes (phase1.md, "Contracts between the pieces").
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace bbm {

struct StreamInfo {               // what resolve() learned from /player
    std::string video_id, client;        // "VISIONOS"
    int itag = 0; std::string mime;      // 251, audio/webm; codecs="opus"
    int64_t content_length = 0, duration_ms = 0, last_modified = 0;
    int64_t init_end = 0, index_start = 0, index_end = 0;   // initRange.end, indexRange
    std::string url;                     // direct url, "" if the client gave none
    std::string sabr_url, ustreamer_config;   // "" if no SABR
    std::string user_agent, client_version; int client_id = 0;
    double loudness_db = 0;
};

struct Segment { int index = 0; int64_t start_ms = 0, duration_ms = 0; std::vector<uint8_t> bytes; };

class Source {
public:
    virtual ~Source() = default;
    virtual bool open(std::string &err) = 0;                 // fetches the init bytes and the index
    virtual const std::vector<uint8_t> &init() const = 0;    // the file's start, through the Cues
    virtual int segment_count() const = 0;                   // clusters in the file
    virtual int64_t duration_ms() const = 0;
    virtual int segment_at(int64_t ms) const = 0;            // the segment that holds ms
    virtual bool read(int index, Segment &out, std::string &err) = 0;   // blocks; any index, any order
    virtual void cancel() = 0;                               // from another thread: unblocks read()
};
// init() + read(0) + read(1) + … is the same file whichever Source gave it (gate 0.2).

// DASH when the StreamInfo has a direct url (SABR instead if prefer_sabr and it has SABR),
// else SABR; nullptr if it has neither. Not opened yet.
std::unique_ptr<Source> make_source(const StreamInfo &info, bool prefer_sabr = false);

}  // namespace bbm
