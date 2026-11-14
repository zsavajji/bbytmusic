// Five peaking bands on the PCM stream, picked by preset. The player's thread calls reset() and
// process(); set_preset() and preset() may be called from any thread.
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace bbm {

constexpr int kEqBands = 5;   // 60, 230, 910, 3600, 14000 Hz

struct EqPreset {
    const char *name;
    float gain_db[kEqBands];
};

const std::vector<EqPreset> &eq_presets();   // "off" first (all zeros)
int eq_preset_index(std::string_view name);   // -1 when there is none

class Equalizer {
public:
    void set_preset(int index);   // takes effect at the next process(); out of range is ignored
    int preset() const { return want_; }
    void reset(int rate, int channels);   // a new stream: clears the filter state
    void process(int16_t *interleaved, size_t frames);   // in place

private:
    struct Band {
        float b0, b1, b2, a1, a2;   // normalised: a0 is 1
        float z1[2], z2[2];         // transposed direct form II, per channel
    };
    void recompute();

    std::atomic<int> want_{0};
    int have_ = 0;      // the preset the coefficients are for; player thread only
    int rate_ = 48000, channels_ = 2;
    float pre_ = 1.f;   // the pre-gain, linear
    int used_ = 0;      // bands in use, packed at the front of bands_
    Band bands_[kEqBands] = {};
};

}  // namespace bbm
