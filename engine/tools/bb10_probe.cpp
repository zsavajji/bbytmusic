// bb10_probe: the engine's real media, audio and platform modules, run on the phone.
//   bb10_probe <file.webm> [seconds=20] [gain_db=-40]
// Plays the file (looping) through QsaSink, holds now-playing, obeys the overlay's buttons.
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

#include "audio/qsa_sink.h"
#include "media/opus_decoder.h"
#include "media/webm_demux.h"
#include "platform/navigator.h"
#include "platform/now_playing.h"

using namespace bbm;
using Clock = std::chrono::steady_clock;

static std::atomic<bool> g_quit{false}, g_paused{false}, g_excuse{false};
static std::atomic<int> g_route_changes{0};

static int say_failed(const std::string &why) {
    std::printf("probe: %s\nprobe: FAILED\n", why.c_str());
    return 1;
}

int main(int argc, char **argv) {
    if (argc < 2 || std::string(argv[1]) == "-h") {
        std::puts("usage: bb10_probe <file.webm> [seconds=20] [gain_db=-40]");
        return 0;
    }
    Navigator nav;
    nav.start([] { g_quit = true; });   // first: the 6 s rule
    const double seconds = argc > 2 ? std::atof(argv[2]) : 20, gain_db = argc > 3 ? std::atof(argv[3]) : -40;
    std::ifstream f(argv[1], std::ios::binary);
    const std::vector<uint8_t> file((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (file.empty()) return say_failed(std::string("cannot read ") + argv[1]);

    // At most 1.0 (0 dB), so the product cannot clip. Computed once; the sample path is integers.
    const int gain_q16 = int(std::lrint(std::fmin(std::pow(10.0, gain_db / 20.0), 1.0) * 65536));

    std::string err;
    QsaSink sink;
    sink.set_route_listener([](const std::string &from, const std::string &to, bool unplugged) {
        std::printf("route: %s -> %s%s\n", from.c_str(), to.c_str(), unplugged ? " (unplugged)" : "");
        std::fflush(stdout);
        g_route_changes++;
        g_excuse = true;
    });
    if (!sink.open(48000, 2, err)) return say_failed("speaker: " + err);

    NowPlaying np;
    const bool have_np = np.start("bbmusic-probe", [&](const std::string &cmd) {
        std::printf("command: %s\n", cmd.c_str());
        std::fflush(stdout);
        if (cmd == "pause") { g_paused = true; np.set_state(NowPlaying::State::Paused); g_excuse = true; }
        else if (cmd == "play") { g_paused = false; np.set_state(NowPlaying::State::Playing); g_excuse = true; }
    }, err);
    if (have_np) {
        np.set_metadata("bbmusic probe", "the real engine", "bb10_probe", int64_t(seconds * 1000), 0, true, true);
        np.set_state(NowPlaying::State::Playing);
    } else {
        std::printf("now-playing: %s (going on without)\n", err.c_str());
    }

    bbm::OpusDecoder dec;
    std::vector<int16_t> pcm;
    pcm.reserve(5760 * 2);
    int64_t frames = 0, last_report = 0;
    int bad_underruns = 0, seen_underruns = 0, decode_errors = 0;
    const std::clock_t cpu0 = std::clock();
    std::clock_t cpu_mark = cpu0;
    auto wall_mark = Clock::now();
    bool failed_open = false, stopped_by_route = false;

    while (!g_quit && !stopped_by_route && !failed_open && double(frames) / 48000 < seconds) {
        WebmDemuxer dm;   // a new one per loop of the file
        if (!dm.feed(file, err) || !dm.has_header()) { failed_open = true; err = "demux: " + (err.empty() ? dm.error() : err); break; }
        if (!dec.init(dm.channels(), err)) { failed_open = true; err = "decoder: " + err; break; }
        dec.set_skip(dm.pre_skip());
        Packet p;
        while (!g_quit && double(frames) / 48000 < seconds && dm.next(p)) {
            while (g_paused && !g_quit) std::this_thread::sleep_for(std::chrono::milliseconds(50));
            pcm.clear();
            if (!dec.decode(p, pcm, err)) { decode_errors++; continue; }
            // Integer gain: a libm call per sample showed up as CPU on the Q10 (phase0.md, gate 0.4).
            for (int16_t &s : pcm) s = int16_t((int(s) * gain_q16) >> 16);
            if (!sink.write(pcm.data(), pcm.size() / 2, err)) {
                if (err == "unplugged") { stopped_by_route = true; break; }
                failed_open = true;
                err = "write: " + err;
                break;
            }
            frames += int64_t(pcm.size() / 2);
            // Underruns after a pause or a route change are not the engine's fault.
            if (g_excuse.exchange(false)) seen_underruns = sink.underruns();
            if (sink.underruns() > seen_underruns) { bad_underruns += sink.underruns() - seen_underruns; seen_underruns = sink.underruns(); }
            if (frames - last_report >= 5 * 48000) {
                last_report = frames;
                const std::clock_t cpu = std::clock();
                const double wall = std::chrono::duration<double>(Clock::now() - wall_mark).count();
                std::printf("played %.1f s, underruns %d, cpu %.1f %%\n", double(frames) / 48000, sink.underruns(),
                            100.0 * double(cpu - cpu_mark) / CLOCKS_PER_SEC / (wall > 0 ? wall : 1));
                std::fflush(stdout);
                cpu_mark = cpu;
                wall_mark = Clock::now();
            }
        }
    }
    if (have_np) { np.set_state(NowPlaying::State::Stopped); np.stop(); }
    sink.close();
    std::printf("probe: %.1f s played, %d underruns (%d not from a pause or route change), %d decode errors, %d route changes%s\n",
                double(frames) / 48000, sink.underruns(), bad_underruns, decode_errors, g_route_changes.load(),
                stopped_by_route ? ", stopped: unplugged" : "");
    if (failed_open) return say_failed(err);
    if (decode_errors || bad_underruns) return say_failed("decode errors or underruns");
    std::puts("probe: all passed");
    return 0;
}
