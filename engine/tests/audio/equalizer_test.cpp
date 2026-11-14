#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "audio/equalizer.h"
#include "check.h"

using namespace bbm;

namespace {

const double kPi = 3.14159265358979323846;

std::vector<int16_t> sine(double hz, double amp, int rate, int ch, size_t frames) {
    std::vector<int16_t> v(frames * size_t(ch));
    for (size_t f = 0; f < frames; f++)
        for (int c = 0; c < ch; c++) v[f * size_t(ch) + size_t(c)] = int16_t(std::lround(amp * std::sin(2 * kPi * hz * double(f) / rate)));
    return v;
}

// RMS of the first channel, from frame `skip` on.
double rms(const std::vector<int16_t> &v, int ch, size_t skip) {
    double s = 0;
    size_t n = 0;
    for (size_t f = skip; f * size_t(ch) < v.size(); f++, n++) s += double(v[f * size_t(ch)]) * v[f * size_t(ch)];
    return std::sqrt(s / double(n));
}

double db(double ratio) { return 20 * std::log10(ratio); }

// The preset's output level against its input level, in dB, for a sine.
double gain_db(const char *preset, double hz, double amp) {
    Equalizer eq;
    eq.set_preset(eq_preset_index(preset));
    eq.reset(48000, 2);
    std::vector<int16_t> v = sine(hz, amp, 48000, 2, 48000);
    const std::vector<int16_t> in = v;
    eq.process(v.data(), 48000);
    return db(rms(v, 2, 4800) / rms(in, 2, 4800));
}

}  // namespace

int main() {
    // off: byte for byte the same.
    {
        Equalizer eq;
        eq.reset(48000, 2);
        std::vector<int16_t> v = sine(440, 20000, 48000, 2, 4800), in = v;
        eq.process(v.data(), 4800);
        CHECK(std::memcmp(v.data(), in.data(), v.size() * 2) == 0);
    }

    // bass_boost: 60 Hz comes out above 3600 Hz by the preset's 6 dB; the 230 Hz band (+4 dB)
    // adds a skirt of about 0.3 dB at 60 Hz (measured 6.28 dB).
    {
        const double lo = gain_db("bass_boost", 60, 16384), hi = gain_db("bass_boost", 3600, 16384);
        std::printf("bass_boost: 60 Hz %.2f dB, 3600 Hz %.2f dB, difference %.2f dB\n", lo, hi, lo - hi);
        CHECK(hi < -5.0 && hi > -6.5);   // under the pre-gain (-6 dB)
        CHECK(lo - hi > 6.0 - 1.5);
        CHECK(lo - hi < 6.0 + 1.5);
    }

    // A full-scale 60 Hz sine: never past int16, the level where the pre-gain left it.
    {
        Equalizer eq;
        eq.set_preset(eq_preset_index("bass_boost"));
        eq.reset(48000, 2);
        std::vector<int16_t> v = sine(60, 32767, 48000, 2, 48000), in = v;
        eq.process(v.data(), 48000);
        int peak = 0;
        for (int16_t s : v) peak = std::max(peak, std::abs(int(s)));
        CHECK(peak <= 32768);   // -32768 is an int16
        // no wrap: neighbours never jump by more than the sine's own slope plus the filter's skirt
        int jump = 0;
        for (size_t i = 2; i < v.size(); i += 2) jump = std::max(jump, std::abs(int(v[i]) - int(v[i - 2])));
        CHECK(jump < 3000);
        const double d = db(rms(v, 2, 4800) / rms(in, 2, 4800));
        std::printf("bass_boost full-scale 60 Hz: peak %d, level %.2f dB\n", peak, d);
        CHECK(std::fabs(d) < 1.0);
    }

    // A hard boost on a full-scale tone saturates, it does not wrap.
    {
        Equalizer eq;
        eq.set_preset(eq_preset_index("bass_boost"));
        eq.reset(48000, 2);
        std::vector<int16_t> v = sine(100, 32767, 48000, 2, 48000);
        eq.process(v.data(), 48000);
        int jump = 0;
        for (size_t i = 2; i < v.size(); i += 2) jump = std::max(jump, std::abs(int(v[i]) - int(v[i - 2])));
        CHECK(jump < 3000);
    }

    // A preset changed mid-stream takes effect, with the state kept.
    {
        Equalizer eq;
        eq.reset(48000, 2);
        std::vector<int16_t> a = sine(60, 8000, 48000, 2, 9600);
        std::vector<int16_t> in = a;
        eq.process(a.data(), 4800);   // off: untouched
        CHECK(std::memcmp(a.data(), in.data(), 4800 * 4) == 0);
        eq.set_preset(eq_preset_index("bass_boost"));
        CHECK_EQ(eq.preset(), eq_preset_index("bass_boost"));
        eq.process(a.data() + 4800 * 2, 4800);
        CHECK(std::memcmp(a.data() + 4800 * 2, in.data() + 4800 * 2, 4800 * 4) != 0);
        eq.set_preset(99);   // not a preset: ignored
        CHECK_EQ(eq.preset(), eq_preset_index("bass_boost"));
    }

    // Mono at another rate; more channels pass through.
    {
        Equalizer eq;
        eq.set_preset(eq_preset_index("rock"));
        eq.reset(44100, 1);
        std::vector<int16_t> v = sine(1000, 10000, 44100, 1, 4410), in = v;
        eq.process(v.data(), 4410);
        CHECK(v != in);
        eq.reset(48000, 6);
        std::vector<int16_t> s = sine(1000, 10000, 48000, 6, 480), s_in = s;
        eq.process(s.data(), 480);
        CHECK(s == s_in);
    }

    // The names.
    CHECK_EQ(eq_preset_index("off"), 0);
    CHECK_EQ(eq_preset_index("nope"), -1);
    CHECK_EQ(eq_preset_index(""), -1);
    for (const EqPreset &p : eq_presets())
        for (const char *c = p.name; *c; c++) CHECK((*c >= 'a' && *c <= 'z') || *c == '_');
    return check_done();
}
