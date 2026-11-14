// SABR's messages, as tools/gates/fetch_probe.py writes and reads them (field numbers from
// LuanRT/googlevideo's .proto files): the VideoPlaybackAbrRequest body, and the UMP parts of
// the answer the session acts on.
#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "stream/source.h"

namespace bbm {

struct SabrRequestState {
    int64_t player_ms = 0;
    bool initialized = false;            // the init segment is held: the format goes in field 2
    // What the client holds, as one BufferedRange; segment numbers are SABR's, from 1. 0: none.
    int buffered_first = 0, buffered_last = 0;
    int64_t buffered_start_ms = 0, buffered_duration_ms = 0;
    std::vector<uint8_t> cookie;         // the last NEXT_REQUEST_POLICY's playback cookie, as sent
};

// False only if info.ustreamer_config is not base64.
bool build_sabr_request(const StreamInfo &info, const SabrRequestState &state, std::vector<uint8_t> &body,
                        std::string &err);

struct SabrMediaHeader {
    uint32_t header_id = 0;
    int itag = 0;
    bool is_init = false;
    int segment = 0;                     // segment_num, from 1
    int64_t start_ms = 0, duration_ms = 0;   // start_ms/duration_ms, else time_range (WebM)
};
bool parse_media_header(std::span<const uint8_t> payload, SabrMediaHeader &out);

struct SabrPolicy {                      // NEXT_REQUEST_POLICY
    int64_t backoff_ms = 0;
    bool has_cookie = false;
    std::vector<uint8_t> cookie;
};
bool parse_next_request_policy(std::span<const uint8_t> payload, SabrPolicy &out);

// FORMAT_INITIALIZATION_METADATA: the last segment number, if it is about `itag`; else 0.
int parse_end_segment(std::span<const uint8_t> payload, int itag);

// The parts that end a request with an error, as text; "" if the part is not one, or is
// harmless (protection status 1 or 2).
std::string sabr_part_error(uint32_t type, std::span<const uint8_t> payload);

}  // namespace bbm
