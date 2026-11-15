#include "audio/qsa_sink.h"

#include <cstring>

#include "util/log.h"

#ifdef __QNX__
#include <audio/audio_manager_device.h>
#include <audio/audio_manager_routing.h>
#include <sys/asoundlib.h>
#endif

namespace bbm {

#ifdef __QNX__

static snd_pcm_t *P(void *p) { return static_cast<snd_pcm_t *>(p); }

bool QsaSink::open_pcm(std::string &err) {
    snd_pcm_t *pcm = nullptr;
    audio_manager_device_t route = AUDIO_DEVICE_DEFAULT;
    audio_manager_get_default_device(&route);
    // pcmPreferredp stays on the codec card (pcmC0D1p) for good; Bluetooth is its own card, behind
    // a2dpp (Q20, 2026-11-15: A2DP connected, the sound stayed on the speaker).
    char preferred[] = "pcmPreferred", a2dp[] = "a2dpp";
    char *name = route == AUDIO_DEVICE_A2DP ? a2dp : preferred;
    int rc = audio_manager_snd_pcm_open_name(AUDIO_TYPE_MULTIMEDIA, &pcm, &handle_, name, SND_PCM_OPEN_PLAYBACK);
    if (rc != 0) { err = "pcm open rc " + std::to_string(rc) + " (" + snd_strerror(rc) + ")"; return false; }
    pcm_ = pcm;
    snd_pcm_channel_params_t params;
    std::memset(&params, 0, sizeof params);
    params.channel = SND_PCM_CHANNEL_PLAYBACK;
    params.mode = SND_PCM_MODE_BLOCK;
    params.start_mode = SND_PCM_START_FULL;
    params.stop_mode = SND_PCM_STOP_STOP;   // an underrun stops it, so it can be counted
    params.format.interleave = 1;
    params.format.format = SND_PCM_SFMT_S16_LE;
    params.format.rate = rate_;
    params.format.voices = channels_;
    params.buf.block.frag_size = frag_;
    params.buf.block.frags_min = 1;
    params.buf.block.frags_max = 8;
    rc = snd_pcm_plugin_params(pcm, &params);
    if (rc != 0) {
        err = "pcm params rc " + std::to_string(rc) + " (" + snd_strerror(rc) + ") why " + std::to_string(params.why_failed);
        close_pcm();
        return false;
    }
    snd_pcm_channel_setup_t setup;
    std::memset(&setup, 0, sizeof setup);
    setup.channel = SND_PCM_CHANNEL_PLAYBACK;
    rc = snd_pcm_plugin_setup(pcm, &setup);
    if (rc == 0 && setup.buf.block.frag_size > 0) frag_ = setup.buf.block.frag_size;
    log_info(std::string("speaker: ") + name + " (" + audio_manager_get_device_name(route) + "), " + std::to_string(setup.format.rate) + " Hz, " + std::to_string(setup.format.voices) +
             " voices, fragment " + std::to_string(setup.buf.block.frag_size) + " bytes x " +
             std::to_string(setup.buf.block.frags) + ", rc " + std::to_string(rc));
    rc = snd_pcm_plugin_prepare(pcm, SND_PCM_CHANNEL_PLAYBACK);
    if (rc != 0) { err = "pcm prepare rc " + std::to_string(rc) + " (" + snd_strerror(rc) + ")"; close_pcm(); return false; }
    route_ = int(route);
    return true;
}

void QsaSink::close_pcm() {
    if (!pcm_) return;
    snd_pcm_plugin_flush(P(pcm_), SND_PCM_CHANNEL_PLAYBACK);
    snd_pcm_close(P(pcm_));
    audio_manager_free_handle(handle_);
    pcm_ = nullptr;
    handle_ = 0;
}

bool QsaSink::open(int rate, int channels, std::string &err) {
    close();
    rate_ = rate;
    channels_ = channels;
    frag_ = 4096;
    underruns_ = 0;
    since_check_ = 0;
    unplugged_ = false;
    pending_.clear();
    pending_.reserve(size_t(frag_) * 4);
    if (!open_pcm(err)) return false;
    open_ = true;
    return true;
}

// A stream stays on the output it was opened on (Q10, 2026-11-14: headphones took effect only
// after a stop and a start). So watch the default device: unplugging stops, anything else reopens.
bool QsaSink::check_route(std::string &err) {
    audio_manager_device_t now = AUDIO_DEVICE_DEFAULT, was = audio_manager_device_t(route_);
    if (audio_manager_get_default_device(&now) != 0 || now == was) return true;
    const std::string from = audio_manager_get_device_name(was), to = audio_manager_get_device_name(now);
    const bool unplugged = now == AUDIO_DEVICE_SPEAKER &&
        (was == AUDIO_DEVICE_HEADSET || was == AUDIO_DEVICE_HEADPHONE || was == AUDIO_DEVICE_A2DP);
    log_info("route: " + from + " -> " + to + (unplugged ? " (unplugged: stopping)" : " (reopening)"));
    route_ = int(now);
    pending_.clear();
    close_pcm();
    if (unplugged) unplugged_ = true;
    if (listener_) listener_(from, to, unplugged);
    if (unplugged) { err = "unplugged"; return false; }
    return open_pcm(err);
}

bool QsaSink::write(const int16_t *s, size_t frames, std::string &err) {
    if (unplugged_) { err = "unplugged"; return false; }
    if (!open_) { err = "not open"; return false; }
    since_check_ += frames;
    if (since_check_ >= uint64_t(rate_ / 2)) {
        since_check_ = 0;
        if (!check_route(err)) return false;
    }
    const auto *b = reinterpret_cast<const uint8_t *>(s);   // little-endian ARM: S16_LE as it is
    pending_.insert(pending_.end(), b, b + frames * size_t(channels_) * 2);
    size_t pos = 0;
    int write_errors = 0;
    while (pending_.size() - pos >= size_t(frag_)) {
        const ssize_t wrote = snd_pcm_plugin_write(P(pcm_), pending_.data() + pos, size_t(frag_));
        if (wrote == frag_) { pos += size_t(frag_); continue; }
        snd_pcm_channel_status_t status;
        std::memset(&status, 0, sizeof status);
        status.channel = SND_PCM_CHANNEL_PLAYBACK;
        snd_pcm_plugin_status(P(pcm_), &status);
        if (status.status == SND_PCM_STATUS_UNDERRUN) underruns_++;
        else write_errors++;
        if (underruns_ + write_errors <= 5)
            log_warn("write gave " + std::to_string(wrote) + " of " + std::to_string(frag_) + ", status " + std::to_string(status.status));
        if (snd_pcm_plugin_prepare(P(pcm_), SND_PCM_CHANNEL_PLAYBACK) != 0 || write_errors > 50) {
            // Bluetooth off: a2dpp dies before the default device moves (Q20, 2026-11-15), so
            // the route check never sees it. Pause as for headphones out.
            if (route_ == AUDIO_DEVICE_A2DP) {
                log_info("route: a2dp gone (unplugged: stopping)");
                pending_.clear();
                close_pcm();
                unplugged_ = true;
                err = "unplugged";
                return false;
            }
            err = "speaker write failed";
            pending_.erase(pending_.begin(), pending_.begin() + long(pos));
            return false;
        }
    }
    pending_.erase(pending_.begin(), pending_.begin() + long(pos));
    return true;
}

void QsaSink::close() {
    close_pcm();
    pending_.clear();
    open_ = false;
}

#else  // the host

bool QsaSink::open_pcm(std::string &err) { err = "not on this platform"; return false; }
void QsaSink::close_pcm() {}
bool QsaSink::check_route(std::string &) { return true; }
bool QsaSink::open(int, int, std::string &err) { err = "not on this platform"; return false; }
bool QsaSink::write(const int16_t *, size_t, std::string &err) { err = "not on this platform"; return false; }
void QsaSink::close() { open_ = false; }

#endif

}  // namespace bbm
