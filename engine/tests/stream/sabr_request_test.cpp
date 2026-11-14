#include "check.h"
#include "proto/pb.h"
#include "stream/sabr_request.h"
#include "stream/ump.h"

#include <string>
#include <vector>

using namespace bbm;
using Bytes = std::vector<uint8_t>;

static Bytes vec(std::span<const uint8_t> s) { return Bytes(s.begin(), s.end()); }

static StreamInfo info() {
    StreamInfo si;
    si.client = "VISIONOS";
    si.client_id = 101;
    si.client_version = "1.02";
    si.itag = 251;
    si.last_modified = 1766955883819090;
    si.ustreamer_config = "CgQIARAC";  // 0A 04 08 01 10 02, url-safe base64 without padding
    return si;
}

static void request_fields() {  // the fields fetch_probe.py's sabr_request writes
    SabrRequestState st;
    st.player_ms = 0;
    Bytes body;
    std::string err;
    CHECK(build_sabr_request(info(), st, body, err));
    PbMsg req(body);
    CHECK(req.ok());
    PbMsg abr(req.bytes(1));
    CHECK(abr.ok() && abr.u(21) == 360 && abr.u(28, 99) == 0 && abr.u(34) == 1 && abr.u(40) == 1);
    CHECK(abr.has(35) && abr.u(35) == 0x3F800000u);  // 1.0f
    CHECK(!req.has(2));                             // not initialized yet
    CHECK(!req.has(3));                             // nothing buffered
    CHECK(vec(req.bytes(5)) == (Bytes{0x0A, 0x04, 0x08, 0x01, 0x10, 0x02}));
    PbMsg fid(req.bytes(16));
    CHECK(fid.u(1) == 251 && fid.u(2) == 1766955883819090ull && !fid.has(3));
    PbMsg ctx(req.bytes(19));
    PbMsg client(ctx.bytes(1));
    CHECK(client.u(16) == 101 && client.str(17) == "1.02");
    CHECK(client.str(18) == "visionOS" && client.str(19) == "26.5.23O471");
    CHECK(!ctx.has(3));

    st.player_ms = 30000;
    st.initialized = true;
    st.buffered_first = 1;
    st.buffered_last = 3;
    st.buffered_start_ms = 0;
    st.buffered_duration_ms = 30000;
    st.cookie = {0xAA, 0xBB};
    CHECK(build_sabr_request(info(), st, body, err));
    PbMsg r2(body);
    CHECK(PbMsg(r2.bytes(1)).u(28) == 30000);
    CHECK(PbMsg(r2.bytes(2)).u(1) == 251);
    PbMsg range(r2.bytes(3));
    CHECK(PbMsg(range.bytes(1)).u(1) == 251);
    CHECK(range.u(2, 9) == 0 && range.u(3) == 30000 && range.u(4) == 1 && range.u(5) == 3);
    CHECK(vec(PbMsg(r2.bytes(19)).bytes(3)) == (Bytes{0xAA, 0xBB}));

    // ANDROID has an os; a client not in the table sends none
    StreamInfo other = info();
    other.client = "SOMETHING";
    CHECK(build_sabr_request(other, {}, body, err));
    CHECK(!PbMsg(PbMsg(PbMsg(body).bytes(19)).bytes(1)).has(18));
    other.ustreamer_config = "not base64!";
    CHECK(!build_sabr_request(other, {}, body, err));
    CHECK(!err.empty());
}

static void parts() {
    // a WebM header: 11 and 12 at 0, time in time_range (ticks of 1/1000 s here, 1/48000 too)
    PbWriter tr;
    tr.varint(1, 110001).varint(2, 10000).varint(3, 1000);
    PbWriter h;
    h.varint(1, 3).string(2, "dQw4w9WgXcQ").varint(3, 251).varint(4, 1766955883819090ull).varint(9, 12).message(15, tr);
    SabrMediaHeader mh;
    CHECK(parse_media_header(h.data(), mh));
    CHECK(mh.header_id == 3 && mh.itag == 251 && !mh.is_init && mh.segment == 12);
    CHECK_EQ(mh.start_ms, 110001);
    CHECK_EQ(mh.duration_ms, 10000);
    PbWriter tr48;
    tr48.varint(1, 48000 * 20).varint(2, 48000 * 10).varint(3, 48000);
    CHECK(parse_media_header(PbWriter().varint(3, 251).message(15, tr48).data(), mh));
    CHECK(mh.start_ms == 20000 && mh.duration_ms == 10000);
    // an MP4 header: start_ms and duration_ms given
    CHECK(parse_media_header(PbWriter().varint(1, 0).varint(3, 140).varint(8, 1).varint(11, 5).varint(12, 7).data(), mh));
    CHECK(mh.is_init && mh.itag == 140 && mh.start_ms == 5 && mh.duration_ms == 7);
    // timescale 0 and huge ticks: no division by zero, no overflow
    PbWriter bad_tr;
    bad_tr.varint(1, UINT64_MAX).varint(2, 5).varint(3, 0);
    CHECK(parse_media_header(PbWriter().message(15, bad_tr).data(), mh));
    CHECK(mh.start_ms == 0 && mh.duration_ms == 5);
    CHECK(!parse_media_header(Bytes{0x0A, 0x09}, mh));

    SabrPolicy p;
    CHECK(parse_next_request_policy(PbWriter().varint(1, 15000).varint(4, 2500).bytes(7, Bytes{1, 2}).data(), p));
    CHECK(p.backoff_ms == 2500 && p.has_cookie && p.cookie == (Bytes{1, 2}));
    CHECK(parse_next_request_policy(PbWriter().varint(4, 1u << 31).data(), p));
    CHECK(p.backoff_ms == 30000 && !p.has_cookie);  // clamped

    PbWriter fid;
    fid.varint(1, 251).varint(2, 1);
    Bytes fim = PbWriter().string(1, "dQw4w9WgXcQ").message(2, fid).varint(3, 213061).varint(4, 22).data();
    CHECK_EQ(parse_end_segment(fim, 251), 22);
    CHECK_EQ(parse_end_segment(fim, 140), 0);
    CHECK_EQ(parse_end_segment(Bytes{0xFF}, 251), 0);

    CHECK_EQ(sabr_part_error(ump::SABR_ERROR, PbWriter().string(1, "sabr.malformed_config").varint(2, 3).data()),
             "sabr: SABR_ERROR sabr.malformed_config (code 3)");
    CHECK(sabr_part_error(ump::RELOAD_PLAYER_RESPONSE, {}).find("reload") != std::string::npos);
    CHECK(sabr_part_error(ump::STREAM_PROTECTION_STATUS, PbWriter().varint(1, 3).data()).find("attestation required") !=
          std::string::npos);
    CHECK_EQ(sabr_part_error(ump::STREAM_PROTECTION_STATUS, PbWriter().varint(1, 1).data()), "");
    CHECK_EQ(sabr_part_error(ump::STREAM_PROTECTION_STATUS, PbWriter().varint(1, 2).data()), "");
    CHECK_EQ(sabr_part_error(ump::MEDIA, Bytes{1, 2, 3}), "");
}

int main() {
    request_fields();
    parts();
    return check_done();
}
