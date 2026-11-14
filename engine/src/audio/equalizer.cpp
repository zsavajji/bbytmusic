#include "audio/equalizer.h"

#include <algorithm>
#include <cmath>

namespace bbm {

const std::vector<EqPreset> &eq_presets() {
    static const std::vector<EqPreset> p = {
        {"off", {0, 0, 0, 0, 0}},
        {"bass_boost", {6, 4, 0, 0, 0}},
        {"treble_boost", {0, 0, 0, 3, 6}},
        {"vocal", {-2, 0, 3, 3, 0}},
        {"rock", {4, 2, -1, 2, 4}},
        {"pop", {-1, 2, 4, 2, -1}},
        {"jazz", {3, 1, -1, 1, 3}},
        {"classical", {3, 2, 0, 2, 3}},
        {"electronic", {5, 3, 0, 2, 4}},
        {"loudness", {5, 2, 0, 1, 3}},
    };
    return p;
}

int eq_preset_index(std::string_view name) {
    const auto &p = eq_presets();
    for (size_t i = 0; i < p.size(); i++)
        if (name == p[i].name) return int(i);
    return -1;
}

namespace {
const float kHz[kEqBands] = {60, 230, 910, 3600, 14000};
const float kQ = 1.0f;
}  // namespace

void Equalizer::set_preset(int index) {
    if (index >= 0 && size_t(index) < eq_presets().size()) want_ = index;
}

void Equalizer::reset(int rate, int channels) {
    rate_ = rate;
    channels_ = channels;
    for (Band &b : bands_) std::fill(std::begin(b.z1), std::end(b.z1), 0.f), std::fill(std::begin(b.z2), std::end(b.z2), 0.f);
    recompute();
}

// RBJ cookbook peakingEQ. The state is kept, so a change of preset mid-stream does not click
// beyond the coefficient jump.
void Equalizer::recompute() {
    have_ = want_;
    const EqPreset &p = eq_presets()[size_t(have_)];
    float top = 0;
    for (float g : p.gain_db) top = std::max(top, g);
    pre_ = std::pow(10.f, -top / 20.f);
    used_ = 0;
    for (int i = 0; i < kEqBands; i++) {
        if (p.gain_db[i] == 0 || kHz[i] >= rate_ / 2.f) continue;   // a band past Nyquist has no meaning
        const double A = std::pow(10.0, p.gain_db[i] / 40.0);
        const double w = 2 * 3.14159265358979323846 * kHz[i] / rate_;
        const double alpha = std::sin(w) / (2 * kQ);
        const double a0 = 1 + alpha / A;
        Band &b = bands_[used_++];
        b.b0 = float((1 + alpha * A) / a0);
        b.b1 = float(-2 * std::cos(w) / a0);
        b.b2 = float((1 - alpha * A) / a0);
        b.a1 = b.b1;
        b.a2 = float((1 - alpha / A) / a0);
    }
}

void Equalizer::process(int16_t *pcm, size_t frames) {
    if (want_ != have_) recompute();
    if (have_ == 0 || channels_ > 2) return;   // off: the samples are not touched
    const size_t ch = size_t(channels_);
    for (size_t f = 0; f < frames; f++) {
        for (size_t c = 0; c < ch; c++) {
            float x = float(pcm[f * ch + c]) * pre_;
            for (int i = 0; i < used_; i++) {
                Band &b = bands_[i];
                const float y = b.b0 * x + b.z1[c];
                b.z1[c] = b.b1 * x - b.a1 * y + b.z2[c];
                b.z2[c] = b.b2 * x - b.a2 * y;
                x = y;
            }
            pcm[f * ch + c] = int16_t(std::lrintf(std::clamp(x, -32768.f, 32767.f)));
        }
    }
}

}  // namespace bbm
