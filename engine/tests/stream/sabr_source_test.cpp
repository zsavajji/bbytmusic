// SabrSource against a fake SABR server that answers the way googlevideo did in gate 0.2:
// one segment per request, the init segment (and segment 1) to an uninitialized client.
#include "check.h"
#include "proto/pb.h"
#include "stream/sabr_source.h"
#include "stream/ump.h"
#include "stream/webm_fixture.h"

#include <chrono>
#include <string>
#include <thread>
#include <vector>

using namespace bbm;
using webm_fixture::Bytes;

struct Seen { int64_t player_ms = 0; bool initialized = false; int buf_first = 0, buf_last = 0; std::string cookie; };

struct FakeSabr {
    webm_fixture::File file = webm_fixture::make({0, 10000, 20000, 30000, 40000}, {1000, 1200, 900, 1100, 300}, 43500.0);
    std::vector<int64_t> starts{0, 10000, 20000, 30000, 40000}, durs{10000, 10000, 10000, 10000, 3500};
    std::vector<Seen> seen;
    std::vector<std::string> urls;
    bool init_with_first = true, send_nothing = false, short_segment = false, block = false;
    int fail_rn = 0;
    std::string fail;  // error, attest, reload, http403, redirect

    StreamInfo info() const {
        StreamInfo si;
        si.client = "IOS";
        si.client_id = 5;
        si.client_version = "21.26.4";
        si.itag = 251;
        si.last_modified = 1700000000000001;
        si.content_length = int64_t(file.whole().size());
        si.duration_ms = 43000;
        si.sabr_url = "https://example.invalid/sabr?x=1";
        si.ustreamer_config = "AAEC";
        return si;
    }

    void media(Bytes &out, uint32_t id, int itag, int seg, const Bytes &data) {
        PbWriter h;
        h.varint(1, id).varint(3, uint64_t(itag)).varint(4, 1700000000000001);
        if (seg == 0) {
            h.varint(8, 1);
        } else {
            PbWriter tr;
            tr.varint(1, uint64_t(starts[size_t(seg - 1)])).varint(2, uint64_t(durs[size_t(seg - 1)])).varint(3, 1000);
            h.varint(9, uint64_t(seg)).message(15, tr);
        }
        ump_write_part(out, ump::MEDIA_HEADER, h.data());
        size_t half = data.size() / 2;
        for (auto [a, b] : {std::pair{size_t(0), half}, std::pair{half, data.size()}}) {
            Bytes m{uint8_t(id)};
            m.insert(m.end(), data.begin() + long(a), data.begin() + long(b));
            ump_write_part(out, ump::MEDIA, m);
        }
        ump_write_part(out, ump::MEDIA_END, Bytes{uint8_t(id)});
    }

    HttpResponse serve(const HttpRequest &req) {
        urls.push_back(req.url);
        const int rn = std::stoi(req.url.substr(req.url.rfind("&rn=") + 4));
        HttpResponse resp;
        resp.status = 200;
        auto stream = [&](const Bytes &out, size_t chunk) {  // the body, a few bytes at a time
            for (size_t at = 0; at < out.size(); at += chunk)
                if (!req.on_data(out.data() + at, std::min(chunk, out.size() - at))) {
                    resp.error = "aborted by on_data";
                    return;
                }
        };
        if (block && rn > 1) {
            while (!req.cancel->load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            resp.error = "cancelled";
            return resp;
        }
        if (fail_rn == rn && fail == "http403") {
            resp.status = 403;
            std::string html = "<html>Forbidden</html>";
            stream(Bytes(html.begin(), html.end()), 5);
            return resp;
        }
        Bytes body(req.body.begin(), req.body.end());
        PbMsg msg(body), range(msg.bytes(3));
        Seen s;
        s.player_ms = int64_t(PbMsg(msg.bytes(1)).u(28));
        s.initialized = msg.has(2);
        s.buf_first = int(range.u(4));
        s.buf_last = int(range.u(5));
        s.cookie = std::string(PbMsg(msg.bytes(19)).str(3));
        seen.push_back(s);

        Bytes out;
        ump_write_part(out, ump::REQUEST_IDENTIFIER, Bytes{1, 2, 3});
        if (!send_nothing && !s.initialized) {
            PbWriter fid;
            fid.varint(1, 251).varint(2, 1700000000000001);
            ump_write_part(out, ump::FORMAT_INITIALIZATION_METADATA, PbWriter().message(2, fid).varint(4, 5).data());
            media(out, 0, 251, 0, file.head);
            if (init_with_first) media(out, 1, 251, 1, file.clusters[0]);
        } else if (!send_nothing) {
            media(out, 9, 140, 3, Bytes(50, 0xEE));  // another format: ignored
            int seg = s.buf_last ? s.buf_last + 1 : 1;
            if (!s.buf_last)
                while (seg < 5 && starts[size_t(seg)] <= s.player_ms) seg++;
            if (seg <= 5) {
                Bytes data = file.clusters[size_t(seg - 1)];
                if (short_segment) data.pop_back();
                media(out, 2, 251, seg, data);
            }
        }
        ump_write_part(out, ump::NEXT_REQUEST_POLICY,
                       PbWriter().varint(1, 15000).bytes(7, Bytes{'c', uint8_t('0' + rn)}).data());
        if (fail_rn == rn && fail == "error")
            ump_write_part(out, ump::SABR_ERROR, PbWriter().string(1, "sabr.test").varint(2, 7).data());
        else if (fail_rn == rn && fail == "attest")
            ump_write_part(out, ump::STREAM_PROTECTION_STATUS, PbWriter().varint(1, 3).data());
        else if (fail_rn == rn && fail == "reload")
            ump_write_part(out, ump::RELOAD_PLAYER_RESPONSE, Bytes{});
        else if (fail_rn == rn && fail == "redirect")
            ump_write_part(out, ump::SABR_REDIRECT, PbWriter().string(1, "https://other.invalid/sabr?y=2").data());
        else
            ump_write_part(out, ump::STREAM_PROTECTION_STATUS, PbWriter().varint(1, 1).data());
        stream(out, 7);
        return resp;
    }
    HttpSend sender() {
        return [this](const HttpRequest &r) { return serve(r); };
    }
};

static void sequential() {
    FakeSabr fake;
    SabrSource src(fake.info(), fake.sender());
    std::string err;
    CHECK(src.open(err));
    CHECK(src.init() == fake.file.head);
    CHECK_EQ(src.segment_count(), 5);
    CHECK_EQ(src.duration_ms(), 43500);
    CHECK_EQ(src.segment_at(25000), 2);
    Bytes whole = src.init();
    for (int i = 0; i < 5; i++) {
        Segment seg;
        CHECK(src.read(i, seg, err));
        CHECK(seg.bytes == fake.file.clusters[size_t(i)]);
        CHECK_EQ(seg.start_ms, fake.starts[size_t(i)]);
        CHECK_EQ(seg.duration_ms, fake.durs[size_t(i)]);
        whole.insert(whole.end(), seg.bytes.begin(), seg.bytes.end());
    }
    CHECK(whole == fake.file.whole());
    CHECK_EQ(src.requests(), 5);  // the first brought the init segment and segment 1
    CHECK_EQ(fake.seen.size(), size_t(5));
    if (fake.seen.size() != 5) return;
    CHECK(!fake.seen[0].initialized && fake.seen[0].player_ms == 0 && fake.seen[0].buf_last == 0);
    for (size_t r = 1; r < 5; r++) {  // each continues from the end of what is buffered
        CHECK(fake.seen[r].initialized);
        CHECK_EQ(fake.seen[r].buf_first, 1);
        CHECK_EQ(fake.seen[r].buf_last, int(r));
        CHECK_EQ(fake.seen[r].player_ms, fake.starts[r]);
        CHECK_EQ(fake.seen[r].cookie, "c" + std::to_string(r));  // the last policy's cookie, back
        CHECK(fake.urls[r] == "https://example.invalid/sabr?x=1&rn=" + std::to_string(r + 1));
    }
    Segment seg;
    CHECK(!src.read(5, seg, err));
    CHECK(!src.read(-1, seg, err));
}

static void seeks() {
    FakeSabr fake;
    SabrSource src(fake.info(), fake.sender());
    std::string err;
    Segment seg;
    CHECK(src.open(err));
    CHECK(src.read(3, seg, err));  // a seek: player time at its start, nothing buffered
    CHECK(seg.bytes == fake.file.clusters[3]);
    CHECK(fake.seen.size() == 2 && fake.seen[1].player_ms == 30000 && fake.seen[1].buf_last == 0);
    CHECK(src.read(4, seg, err));  // goes on from there
    CHECK(seg.bytes == fake.file.clusters[4]);
    CHECK(fake.seen.size() == 3 && fake.seen[2].buf_first == 4 && fake.seen[2].buf_last == 4 &&
          fake.seen[2].player_ms == 40000);
    CHECK(src.read(1, seg, err));  // back: a seek again
    CHECK(seg.bytes == fake.file.clusters[1]);
    CHECK(fake.seen.size() == 4 && fake.seen[3].player_ms == 10000 && fake.seen[3].buf_last == 0);
    CHECK(src.read(1, seg, err));  // the same one twice: asked for again
    CHECK(seg.bytes == fake.file.clusters[1]);
    CHECK_EQ(fake.seen.size(), size_t(5));

    FakeSabr bare;  // the init segment alone at first; segment 0 is then asked for at 0 ms
    bare.init_with_first = false;
    SabrSource src2(bare.info(), bare.sender());
    CHECK(src2.open(err));
    CHECK(src2.read(0, seg, err));
    CHECK(seg.bytes == bare.file.clusters[0]);
    CHECK(bare.seen.size() == 2 && bare.seen[1].initialized && bare.seen[1].player_ms == 0);
}

static std::string fail_at(const std::string &kind, int rn, bool &ok) {
    FakeSabr fake;
    fake.fail = kind;
    fake.fail_rn = rn;
    SabrSource src(fake.info(), fake.sender());
    std::string err;
    Segment seg;
    ok = src.open(err) && src.read(0, seg, err) && src.read(1, seg, err) && src.read(2, seg, err);
    return err;
}

static void errors() {
    bool ok;
    CHECK_EQ(fail_at("error", 2, ok), "sabr: SABR_ERROR sabr.test (code 7)");
    CHECK(!ok);
    CHECK(fail_at("attest", 1, ok).find("attestation required") != std::string::npos);
    CHECK(!ok);
    CHECK(fail_at("reload", 3, ok).find("reload") != std::string::npos);
    CHECK(!ok);
    CHECK_EQ(fail_at("http403", 2, ok), "sabr: http 403");
    CHECK(!ok);
    fail_at("redirect", 2, ok);
    CHECK(ok);
    {
        FakeSabr fake;
        fake.fail = "redirect";
        fake.fail_rn = 2;
        SabrSource src(fake.info(), fake.sender());
        std::string err;
        Segment seg;
        CHECK(src.open(err) && src.read(1, seg, err) && src.read(2, seg, err));
        CHECK(fake.urls.size() == 3 && fake.urls[2] == "https://other.invalid/sabr?y=2&rn=3");
    }
    std::string err;
    Segment seg;
    {
        FakeSabr fake;  // a segment one byte short of what the cues say
        SabrSource src(fake.info(), fake.sender());
        CHECK(src.open(err));
        fake.short_segment = true;
        CHECK(!src.read(1, seg, err));
        CHECK(err.find("the cues say") != std::string::npos);
    }
    {
        FakeSabr fake;  // answers without media: gives up after a few
        SabrSource src(fake.info(), fake.sender());
        CHECK(src.open(err));
        fake.send_nothing = true;
        CHECK(!src.read(2, seg, err));
        CHECK(err.find("not received") != std::string::npos);
        CHECK(fake.seen.size() < 10);
    }
    {
        FakeSabr fake;
        fake.send_nothing = true;
        SabrSource src(fake.info(), fake.sender());
        CHECK(!src.open(err));
        CHECK(err.find("no init segment") != std::string::npos);
    }
    {
        FakeSabr fake;
        StreamInfo si = fake.info();
        si.sabr_url.clear();
        SabrSource src(si, fake.sender());
        CHECK(!src.open(err));
        CHECK(fake.urls.empty());
    }
}

static void cancel() {
    FakeSabr fake;
    SabrSource src(fake.info(), fake.sender());
    std::string err;
    Segment seg;
    CHECK(src.open(err));
    fake.block = true;  // the next request hangs until cancelled
    std::thread t([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        src.cancel();
    });
    CHECK(!src.read(1, seg, err));
    t.join();
    CHECK(err.find("cancelled") != std::string::npos);
    fake.block = false;
    CHECK(src.read(1, seg, err));  // a read after the cancel runs normally
    CHECK(seg.bytes == fake.file.clusters[1]);
}

int main() {
    sequential();
    seeks();
    errors();
    cancel();
    return check_done();
}
