// SponsorBlock: the segments of a video that the engine skips. Only the first four hex digits of
// the id's SHA-256 go to the service, never the id.
#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "net/http.h"

namespace bbm {

struct SkipSegment {
    std::string category;
    int64_t from_ms = 0, to_ms = 0;
};

// The categories the engine knows, in the order the UI lists them.
const std::vector<std::string> &skip_categories();

// The request's URL for a video.
std::string sponsor_url(const std::string &video_id, const std::vector<std::string> &categories);

// The segments of `video_id` in an answer (every video whose hash shares the prefix is in it):
// only actionType "skip", only the given categories, sorted by start, overlapping or touching
// ones merged (the merged one keeps the first's category).
bool parse_segments(std::string_view body, const std::string &video_id, const std::vector<std::string> &categories,
                    std::vector<SkipSegment> &out, std::string &err);

// GET with the engine's Http. A 404 is "no segments"; any other failure is false.
bool fetch_segments(Http &http, const std::string &video_id, const std::vector<std::string> &categories,
                    std::vector<SkipSegment> &out, std::string &err);

}  // namespace bbm
