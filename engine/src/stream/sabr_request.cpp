#include "stream/sabr_request.h"

#include <algorithm>
#include <climits>

#include "proto/pb.h"
#include "stream/clients.h"
#include "stream/ump.h"
#include "util/base64.h"

namespace bbm {
namespace {

constexpr int64_t kMaxBackoffMs = 30000;   // a hostile or broken policy can't park us for long

int64_t ticks_ms(uint64_t ticks, uint64_t scale) {
    if (scale == 0) scale = 1000;
    return ticks > uint64_t(INT64_MAX) / 1000 ? 0 : int64_t(ticks * 1000 / scale);
}

int to_int(uint64_t v) { return v > uint64_t(INT_MAX) ? 0 : int(v); }

}  // namespace

bool build_sabr_request(const StreamInfo &info, const SabrRequestState &st, std::vector<uint8_t> &body,
                        std::string &err) {
    std::vector<uint8_t> ustreamer;
    if (!base64_decode(info.ustreamer_config, ustreamer)) {
        err = "sabr: the ustreamer config is not base64";
        return false;
    }
    PbWriter fid;  // FormatId: itag, last_modified (xtags: none for the audio formats seen)
    fid.varint(1, uint64_t(info.itag)).varint(2, uint64_t(info.last_modified));
    PbWriter abr;  // ClientAbrState: sticky resolution, player time, visibility, rate, audio only
    abr.varint(21, 360).varint(28, uint64_t(std::max<int64_t>(st.player_ms, 0))).varint(34, 1).float32(35, 1.0f).varint(40, 1);
    PbWriter req;
    req.message(1, abr);
    if (st.initialized) req.message(2, fid);  // initialization_format_ids
    if (st.buffered_last > 0) {
        PbWriter range;  // BufferedRange
        range.message(1, fid)
            .varint(2, uint64_t(st.buffered_start_ms))
            .varint(3, uint64_t(st.buffered_duration_ms))
            .varint(4, uint64_t(st.buffered_first))
            .varint(5, uint64_t(st.buffered_last));
        req.message(3, range);
    }
    req.bytes(5, ustreamer);
    req.message(16, fid);  // selected_audio_format_ids
    PbWriter client;       // StreamerContext.ClientInfo
    client.varint(16, uint64_t(info.client_id)).string(17, info.client_version);
    if (const PlayerClient *pc = find_player_client(info.client); pc && *pc->os_name)
        client.string(18, pc->os_name).string(19, pc->os_version);
    PbWriter ctx;
    ctx.message(1, client);
    if (!st.cookie.empty()) ctx.bytes(3, st.cookie);
    req.message(19, ctx);
    body = req.data();
    return true;
}

bool parse_media_header(std::span<const uint8_t> payload, SabrMediaHeader &out) {
    PbMsg h(payload);
    if (!h.ok()) return false;
    out = {};
    out.header_id = uint32_t(std::min<uint64_t>(h.u(1), UINT32_MAX));
    out.itag = to_int(h.u(3));
    out.is_init = h.u(8) != 0;
    out.segment = to_int(h.u(9));
    out.start_ms = ticks_ms(h.u(11), 1000);
    out.duration_ms = ticks_ms(h.u(12), 1000);
    // WebM headers leave 11 and 12 at 0 and carry time_range: start, duration, timescale
    PbMsg tr(h.bytes(15));
    if (!out.start_ms) out.start_ms = ticks_ms(tr.u(1), tr.u(3, 1000));
    if (!out.duration_ms) out.duration_ms = ticks_ms(tr.u(2), tr.u(3, 1000));
    return true;
}

bool parse_next_request_policy(std::span<const uint8_t> payload, SabrPolicy &out) {
    PbMsg p(payload);
    if (!p.ok()) return false;
    out = {};
    out.backoff_ms = int64_t(std::min<uint64_t>(p.u(4), kMaxBackoffMs));
    out.has_cookie = p.has(7);
    auto c = p.bytes(7);
    out.cookie.assign(c.begin(), c.end());
    return true;
}

int parse_end_segment(std::span<const uint8_t> payload, int itag) {
    PbMsg m(payload);
    PbMsg fid(m.bytes(2));
    return m.ok() && fid.u(1) == uint64_t(itag) ? to_int(m.u(4)) : 0;
}

std::string sabr_part_error(uint32_t type, std::span<const uint8_t> payload) {
    PbMsg m(payload);
    switch (type) {
    case ump::SABR_ERROR:
        return "sabr: SABR_ERROR " + std::string(m.str(1)) + " (code " + std::to_string(m.u(2)) + ")";
    case ump::RELOAD_PLAYER_RESPONSE:
        return "sabr: reload requested: the /player response must be fetched again";
    case ump::STREAM_PROTECTION_STATUS:
        return m.u(1) == 3 ? "sabr: attestation required (stream protection status 3)" : "";
    default:
        return "";
    }
}

}  // namespace bbm
