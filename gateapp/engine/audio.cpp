// From tools/gates/gate04.cpp (verified on the phone: 48 kHz stereo, fragment 4096 bytes), made
// into a looping thread that reports every 5 s.
#include "audio.h"

#include <opus/opus.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <format>
#include <vector>

#include "log.h"
#include "webm.h"

#ifdef __QNX__
#include <audio/audio_manager_device.h>
#include <audio/audio_manager_routing.h>
#include <sys/asoundlib.h>
#endif

namespace {
using Clock = std::chrono::steady_clock;
double since(Clock::time_point t) { return std::chrono::duration<double>(Clock::now() - t).count(); }
}  // namespace

bool Audio::start(double seconds, double gain_db, const std::string &webm, Emit emit, std::string &why) {
    std::lock_guard<std::mutex> lock(m_);
    if (playing_) { why = "already playing"; return false; }
    if (th_.joinable()) th_.join();
    Webm probe;  // fail here, in the reply, rather than later in an event
    if (!webm_load(webm, probe, why)) return false;
    playing_ = true;
    stop_ = false;
    played_ = 0;
    underruns_ = 0;
    th_ = std::thread(&Audio::run, this, seconds, gain_db, webm, std::move(emit));
    return true;
}

void Audio::stop() {
    std::lock_guard<std::mutex> lock(m_);
    stop_ = true;
    if (th_.joinable()) th_.join();
}

AudioStats Audio::stats() { return {playing_, played_, underruns_}; }

void Audio::run(double seconds, double gain_db, std::string path, Emit emit) {
    Webm w;
    std::string error;
    [[maybe_unused]] int write_errors = 0;
    int decode_errors = 0;
    OpusDecoder *dec = nullptr;
    const auto start = Clock::now();
    const std::clock_t cpu0 = std::clock();
    auto cpu_mark = cpu0;
    auto wall_mark = start;
    auto next_report = start + std::chrono::seconds(5);
    long long samples = 0;
#ifdef __QNX__
    snd_pcm_t *pcm = nullptr;
    unsigned int handle = 0;
    audio_manager_device_t route = AUDIO_DEVICE_DEFAULT;  // where the sound goes; watched below
    int route_tick = 0;
#endif
    [[maybe_unused]] int frag = 4096;

    auto fields = [&](double cpu_pct) {
        return std::format("\"played\":{:.1f},\"underruns\":{},\"cpu_pct\":{:.1f}", samples / 48000.0, underruns_.load(), cpu_pct);
    };

#ifdef __QNX__
    // Declared before the first goto: a jump may not pass a lambda's initialisation.
    auto open_pcm = [&]() -> bool {
        char name[] = "pcmPreferred";
        int rc = audio_manager_snd_pcm_open_name(AUDIO_TYPE_MULTIMEDIA, &pcm, &handle, name, SND_PCM_OPEN_PLAYBACK);
        if (rc != 0) { error = std::format("pcm open rc {} ({})", rc, snd_strerror(rc)); pcm = nullptr; return false; }
        snd_pcm_channel_params_t params;
        std::memset(&params, 0, sizeof params);
        params.channel = SND_PCM_CHANNEL_PLAYBACK;
        params.mode = SND_PCM_MODE_BLOCK;
        params.start_mode = SND_PCM_START_FULL;
        params.stop_mode = SND_PCM_STOP_STOP;  // an underrun stops it, so it can be counted
        params.format.interleave = 1;
        params.format.format = SND_PCM_SFMT_S16_LE;
        params.format.rate = 48000;
        params.format.voices = w.channels;
        params.buf.block.frag_size = frag;
        params.buf.block.frags_min = 1;
        params.buf.block.frags_max = 8;
        rc = snd_pcm_plugin_params(pcm, &params);
        if (rc != 0) { error = std::format("pcm params rc {} ({}) why {}", rc, snd_strerror(rc), params.why_failed); return false; }
        snd_pcm_channel_setup_t setup;
        std::memset(&setup, 0, sizeof setup);
        setup.channel = SND_PCM_CHANNEL_PLAYBACK;
        rc = snd_pcm_plugin_setup(pcm, &setup);
        if (rc == 0 && setup.buf.block.frag_size > 0) frag = setup.buf.block.frag_size;
        log_line(std::format("speaker: {} Hz, {} voices, fragment {} bytes x {}, rc {}", setup.format.rate,
                             setup.format.voices, setup.buf.block.frag_size, setup.buf.block.frags, rc));
        rc = snd_pcm_plugin_prepare(pcm, SND_PCM_CHANNEL_PLAYBACK);
        if (rc != 0) { error = std::format("pcm prepare rc {} ({})", rc, snd_strerror(rc)); return false; }
        audio_manager_get_default_device(&route);
        return true;
    };
#endif
    if (!webm_load(path, w, error)) goto done;
    {
        int err = 0;
        dec = opus_decoder_create(48000, w.channels, &err);
        if (!dec) { error = std::format("opus_decoder_create {}", err); goto done; }
    }
#ifdef __QNX__
    if (!open_pcm()) goto done;
#endif
    {
        // At most 1.0 (0 dB), so the product cannot clip.
        const int gain_q16 = int(std::lrint(std::min(std::pow(10.0, gain_db / 20.0), 1.0) * 65536));
        std::vector<opus_int16> frame(5760 * w.channels);
        std::vector<uint8_t> pending;
        int skip = w.pre_skip;
        while (!stop_ && samples / 48000.0 < seconds) {
            for (const Webm::Packet &p : w.packets) {
                if (stop_ || samples / 48000.0 >= seconds) break;
                const int n = opus_decode(dec, &w.bytes[p.offset], opus_int32(p.size), frame.data(), 5760, 0);
                if (n < 0) { decode_errors++; continue; }
                int first = 0;
                if (skip > 0) { first = std::min(skip, n); skip -= first; }
                samples += n - first;
                played_ = samples / 48000.0;
                for (int i = first * w.channels; i < n * w.channels; i++) {
                    // Integer gain: a libm call per sample (lrint) showed up as CPU on the Q10.
                    const opus_int16 s = opus_int16((frame[i] * gain_q16) >> 16);
                    pending.push_back(uint8_t(s));
                    pending.push_back(uint8_t(s >> 8));
                }
#ifdef __QNX__
                // A stream stays on the output it was opened on (Q10, 2026-11-14: headphones took
                // effect only after a stop and a start). So watch the default device, about twice
                // a second: unplugging stops the music, anything else opens the speaker again.
                if (++route_tick >= 25) {
                    route_tick = 0;
                    audio_manager_device_t now = route;
                    if (audio_manager_get_default_device(&now) == 0 && now != route) {
                        const bool unplugged = now == AUDIO_DEVICE_SPEAKER &&
                            (route == AUDIO_DEVICE_HEADSET || route == AUDIO_DEVICE_HEADPHONE || route == AUDIO_DEVICE_A2DP);
                        log_line(std::format("route: {} -> {} ({})", audio_manager_get_device_name(route),
                                             audio_manager_get_device_name(now), unplugged ? "unplugged: stopping" : "reopening"));
                        route = now;
                        if (unplugged) { stop_ = true; break; }
                        snd_pcm_close(pcm);
                        audio_manager_free_handle(handle);
                        pcm = nullptr;
                        handle = 0;
                        pending.clear();
                        if (!open_pcm()) goto done;
                    }
                }
                while (pending.size() >= size_t(frag)) {
                    const ssize_t wrote = snd_pcm_plugin_write(pcm, pending.data(), frag);
                    if (wrote == frag) { pending.erase(pending.begin(), pending.begin() + frag); continue; }
                    snd_pcm_channel_status_t status;
                    std::memset(&status, 0, sizeof status);
                    status.channel = SND_PCM_CHANNEL_PLAYBACK;
                    snd_pcm_plugin_status(pcm, &status);
                    if (status.status == SND_PCM_STATUS_UNDERRUN) underruns_++;
                    else write_errors++;
                    if (underruns_ + write_errors <= 5)
                        log_line(std::format("write gave {} of {}, status {}", int(wrote), frag, status.status));
                    if (snd_pcm_plugin_prepare(pcm, SND_PCM_CHANNEL_PLAYBACK) != 0 || write_errors > 50) {
                        error = "speaker write failed";
                        goto done;
                    }
                }
#else
                pending.clear();  // the Mac: no speaker, so pace to real time
                const double ahead = samples / 48000.0 - since(start);
                if (ahead > 0) std::this_thread::sleep_for(std::chrono::duration<double>(ahead));
#endif
                if (Clock::now() >= next_report) {
                    const std::clock_t cpu = std::clock();
                    const double cpu_pct = 100.0 * double(cpu - cpu_mark) / CLOCKS_PER_SEC / since(wall_mark);
                    cpu_mark = cpu;
                    wall_mark = Clock::now();
                    next_report += std::chrono::seconds(5);
                    emit("audio", fields(cpu_pct));
                }
            }
            opus_decoder_ctl(dec, OPUS_RESET_STATE);  // the file ended: start it again
            skip = w.pre_skip;
        }
    }
done:
#ifdef __QNX__
    if (pcm) {
        snd_pcm_plugin_flush(pcm, SND_PCM_CHANNEL_PLAYBACK);
        snd_pcm_close(pcm);
        audio_manager_free_handle(handle);
    }
#endif
    if (dec) opus_decoder_destroy(dec);
    const double wall = since(start);
    const double cpu_total = 100.0 * double(std::clock() - cpu0) / CLOCKS_PER_SEC / std::max(wall, 0.001);
    if (decode_errors) error += std::format("{}{} decode errors", error.empty() ? "" : "; ", decode_errors);
    played_ = samples / 48000.0;
    playing_ = false;  // before the event: a status after audio_done must say false
    emit("audio_done", fields(cpu_total) + std::format(",\"wall\":{:.1f},\"error\":{}", wall, [&] {
        std::string q = "\"";
        for (char ch : error) { if (ch == '"' || ch == '\\') q += '\\'; q += ch; }
        return q + "\"";
    }()));
}
