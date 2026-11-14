// Gate 0.4 (PLAN.md §9), the part that needs no .bar: WebM/Opus decoded with libopus and
// written to the phone's speaker through libasound, for minutes on end. It reports underruns,
// how much of a core the whole process took, and how fast the decoder alone runs.
//
//   gate04 <file.webm> [gain dB = -50] [seconds = 600] [null]
//
// `null` decodes without opening the speaker (the decoder's speed alone). The gain is far down
// by default: this runs unattended on someone's phone.
#include <audio/audio_manager_routing.h>
#include <opus/opus.h>
#include <sys/asoundlib.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct Packet {
    size_t offset;
    size_t size;
};

struct Webm {
    std::vector<uint8_t> bytes;
    std::vector<Packet> packets;
    int channels = 2;
    int pre_skip = 0;
};

// EBML ids keep their length marker; sizes drop it. An all-ones size means "unknown".
bool read_id(const std::vector<uint8_t> &b, size_t &pos, uint32_t &id) {
    if (pos >= b.size() || b[pos] == 0) return false;
    int n = 1;
    for (uint8_t mask = 0x80; !(b[pos] & mask); mask >>= 1) n++;
    if (n > 4 || pos + n > b.size()) return false;
    id = 0;
    for (int i = 0; i < n; i++) id = id << 8 | b[pos++];
    return true;
}

bool read_size(const std::vector<uint8_t> &b, size_t &pos, uint64_t &size, bool &unknown) {
    if (pos >= b.size() || b[pos] == 0) return false;
    int n = 1;
    uint8_t mask = 0x80;
    for (; !(b[pos] & mask); mask >>= 1) n++;
    if (pos + n > b.size()) return false;
    size = b[pos++] & (mask - 1);
    bool ones = size == uint64_t(mask - 1);
    for (int i = 1; i < n; i++) {
        ones = ones && b[pos] == 0xFF;
        size = size << 8 | b[pos++];
    }
    unknown = ones;
    return true;
}

// Walks the file once: containers are entered, SimpleBlocks and the Opus header are recorded,
// everything else is skipped by its size.
bool parse(Webm &w) {
    const auto &b = w.bytes;
    size_t pos = 0;
    while (pos < b.size()) {
        uint32_t id;
        uint64_t size;
        bool unknown;
        const size_t at = pos;
        if (!read_id(b, pos, id) || !read_size(b, pos, size, unknown)) {
            std::printf("parse: bad element header at %zu of %zu\n", at, b.size());
            return false;
        }
        switch (id) {
        case 0x18538067:  // Segment
        case 0x1F43B675:  // Cluster
        case 0x1654AE6B:  // Tracks
        case 0xAE:        // TrackEntry
        case 0xA0:        // BlockGroup
            continue;     // descend
        case 0x63A2:      // CodecPrivate: OpusHead
            if (size >= 12 && std::memcmp(&b[pos], "OpusHead", 8) == 0) {
                w.channels = b[pos + 9];
                w.pre_skip = b[pos + 10] | b[pos + 11] << 8;
            }
            break;
        case 0xA3:  // SimpleBlock: track number (1 byte here), timecode (2), flags (1), frame
        case 0xA1:  // Block
            if (size > 4) w.packets.push_back({pos + 4, size_t(size) - 4});
            break;
        default:
            break;
        }
        if (unknown || pos + size > b.size()) {
            std::printf("parse: element %x at %zu, size %llu%s, file %zu\n", id, at, (unsigned long long)size,
                        unknown ? " (unknown)" : "", b.size());
            return false;
        }
        pos += size;
    }
    return !w.packets.empty();
}

double seconds_since(Clock::time_point t) { return std::chrono::duration<double>(Clock::now() - t).count(); }

}  // namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        std::puts("usage: gate04 <file.webm> [gain dB] [seconds] [null]");
        return 0;
    }
    const double gain = std::pow(10.0, (argc > 2 ? std::atof(argv[2]) : -50.0) / 20.0);
    const double limit = argc > 3 ? std::atof(argv[3]) : 600.0;
    const bool null_sink = argc > 4 && std::string(argv[4]) == "null";

    Webm w;
    if (FILE *f = std::fopen(argv[1], "rb")) {
        std::fseek(f, 0, SEEK_END);
        w.bytes.resize(std::ftell(f));
        std::fseek(f, 0, SEEK_SET);
        if (std::fread(w.bytes.data(), 1, w.bytes.size(), f) != w.bytes.size()) w.bytes.clear();
        std::fclose(f);
    }
    if (w.bytes.empty() || !parse(w)) {
        std::puts("gate04: cannot read or parse the file");
        return 1;
    }
    std::printf("file: %zu bytes, %zu packets, %d channels, pre-skip %d\n", w.bytes.size(), w.packets.size(),
                w.channels, w.pre_skip);

    int err = 0;
    OpusDecoder *dec = opus_decoder_create(48000, w.channels, &err);
    if (!dec) {
        std::printf("gate04: opus_decoder_create failed, %d\n", err);
        return 1;
    }

    snd_pcm_t *pcm = nullptr;
    unsigned int handle = 0;
    int frag = 4096;
    if (!null_sink) {
        char name[] = "pcmPreferred";
        int rc = audio_manager_snd_pcm_open_name(AUDIO_TYPE_MULTIMEDIA, &pcm, &handle, name, SND_PCM_OPEN_PLAYBACK);
        if (rc != 0) {
            std::printf("gate04: open failed, rc %d (%s)\n", rc, snd_strerror(rc));
            return 1;
        }
        snd_pcm_channel_params_t params;
        std::memset(&params, 0, sizeof params);
        params.channel = SND_PCM_CHANNEL_PLAYBACK;
        params.mode = SND_PCM_MODE_BLOCK;
        params.start_mode = SND_PCM_START_FULL;  // start once the buffer is full: a cushion from the first sample
        params.stop_mode = SND_PCM_STOP_STOP;    // an underrun stops it, so it can be counted
        params.format.interleave = 1;
        params.format.format = SND_PCM_SFMT_S16_LE;
        params.format.rate = 48000;
        params.format.voices = w.channels;
        params.buf.block.frag_size = frag;
        params.buf.block.frags_min = 1;
        params.buf.block.frags_max = 8;
        rc = snd_pcm_plugin_params(pcm, &params);
        if (rc != 0) {
            std::printf("gate04: params failed, rc %d (%s), why %u\n", rc, snd_strerror(rc), params.why_failed);
            return 1;
        }
        snd_pcm_channel_setup_t setup;
        std::memset(&setup, 0, sizeof setup);
        setup.channel = SND_PCM_CHANNEL_PLAYBACK;
        rc = snd_pcm_plugin_setup(pcm, &setup);
        if (rc == 0 && setup.buf.block.frag_size > 0) frag = setup.buf.block.frag_size;
        std::printf("speaker: %d Hz, %d voices, fragment %d bytes x %d, rc %d\n", setup.format.rate,
                    setup.format.voices, setup.buf.block.frag_size, setup.buf.block.frags, rc);
        rc = snd_pcm_plugin_prepare(pcm, SND_PCM_CHANNEL_PLAYBACK);
        if (rc != 0) {
            std::printf("gate04: prepare failed, rc %d (%s)\n", rc, snd_strerror(rc));
            return 1;
        }
    }
    std::fflush(stdout);

    std::vector<opus_int16> frame(5760 * w.channels);
    std::vector<uint8_t> pending;
    const auto start = Clock::now();
    const std::clock_t cpu0 = std::clock();
    double decode_s = 0, next_report = 60;
    long long samples = 0;
    int underruns = 0, write_errors = 0, decode_errors = 0, skip = w.pre_skip;

    for (const Packet &p : w.packets) {
        const auto t = Clock::now();
        const int n = opus_decode(dec, &w.bytes[p.offset], opus_int32(p.size), frame.data(), 5760, 0);
        decode_s += seconds_since(t);
        if (n < 0) {
            decode_errors++;
            continue;
        }
        int first = 0;
        if (skip > 0) {
            first = skip < n ? skip : n;
            skip -= first;
        }
        samples += n - first;
        for (int i = first * w.channels; i < n * w.channels; i++) {
            const opus_int16 s = opus_int16(frame[i] * gain);
            pending.push_back(uint8_t(s));
            pending.push_back(uint8_t(s >> 8));
        }
        while (!null_sink && pending.size() >= size_t(frag)) {
            const ssize_t wrote = snd_pcm_plugin_write(pcm, pending.data(), frag);
            if (wrote == frag) {
                pending.erase(pending.begin(), pending.begin() + frag);
                continue;
            }
            snd_pcm_channel_status_t status;
            std::memset(&status, 0, sizeof status);
            status.channel = SND_PCM_CHANNEL_PLAYBACK;
            snd_pcm_plugin_status(pcm, &status);
            if (status.status == SND_PCM_STATUS_UNDERRUN) underruns++;
            else write_errors++;
            if (underruns + write_errors <= 5)
                std::printf("write gave %d of %d, status %d\n", int(wrote), frag, status.status);
            if (snd_pcm_plugin_prepare(pcm, SND_PCM_CHANNEL_PLAYBACK) != 0 || write_errors > 50) goto done;
        }
        if (null_sink) pending.clear();
        const double played = samples / 48000.0;
        if (played >= next_report) {
            std::printf("  %4.0f s played, %.1f s wall, underruns %d\n", played, seconds_since(start), underruns);
            std::fflush(stdout);
            next_report += 60;
        }
        if (played >= limit) break;
    }
done:
    if (pcm) {
        snd_pcm_plugin_flush(pcm, SND_PCM_CHANNEL_PLAYBACK);
        snd_pcm_close(pcm);
        audio_manager_free_handle(handle);
    }
    const double wall = seconds_since(start);
    const double cpu = double(std::clock() - cpu0) / CLOCKS_PER_SEC;
    const double audio = samples / 48000.0;
    std::printf("audio %.1f s, wall %.1f s, process cpu %.2f s (%.1f%% of a core), decoder alone %.2f s (%.0fx real time)\n",
                audio, wall, cpu, 100.0 * cpu / (null_sink ? audio : wall), decode_s, audio / decode_s);
    std::printf("underruns %d, write errors %d, decode errors %d\n", underruns, write_errors, decode_errors);
    const bool ok = underruns == 0 && write_errors == 0 && decode_errors == 0 && audio > 1;
    std::puts(ok ? "gate04: all passed" : "gate04: FAILED");
    opus_decoder_destroy(dec);
    return ok ? 0 : 1;
}
