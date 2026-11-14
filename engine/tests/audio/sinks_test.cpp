#include <cstdint>
#include <chrono>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "audio/null_sink.h"
#include "audio/wav_sink.h"
#include "check.h"

using namespace bbm;

static uint32_t le(const std::vector<uint8_t> &b, size_t at, int n) {
    uint32_t v = 0;
    for (int i = n - 1; i >= 0; i--) v = v << 8 | b[at + i];
    return v;
}

int main() {
    std::string err;
    const std::string path = std::string(BBM_FIXTURES) + "/../wav_sink_test.tmp.wav";
    std::vector<int16_t> pcm;
    for (int i = 0; i < 1000; i++) { pcm.push_back(int16_t(i * 31 - 15000)); pcm.push_back(int16_t(-i * 29)); }
    {
        WavSink w(path);
        CHECK(!w.write(pcm.data(), 1, err));   // not open
        CHECK(w.open(48000, 2, err));
        CHECK(w.write(pcm.data(), 400, err));
        CHECK(w.write(pcm.data() + 800, 600, err));
        w.close();
    }
    std::ifstream f(path, std::ios::binary);
    std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    f.close();
    std::remove(path.c_str());
    CHECK_EQ(b.size(), size_t(44 + 4000));
    CHECK(std::string(b.begin(), b.begin() + 4) == "RIFF");
    CHECK_EQ(le(b, 4, 4), 36u + 4000u);
    CHECK_EQ(le(b, 22, 2), 2u);
    CHECK_EQ(le(b, 24, 4), 48000u);
    CHECK_EQ(le(b, 28, 4), 192000u);
    CHECK_EQ(le(b, 34, 2), 16u);
    CHECK_EQ(le(b, 40, 4), 4000u);
    bool same = true;
    for (size_t i = 0; i < pcm.size(); i++) same = same && int16_t(le(b, 44 + 2 * i, 2)) == pcm[i];
    CHECK(same);

    NullSink n;
    CHECK(n.open(48000, 2, err));
    CHECK(n.write(pcm.data(), 1000, err));
    CHECK_EQ(n.frames_written(), uint64_t(1000));
    NullSink rt(true);
    CHECK(rt.open(48000, 2, err));
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 5; i++) CHECK(rt.write(pcm.data(), 1000, err));   // 5000 frames ~ 104 ms
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    CHECK(ms >= 100 && ms < 400);
    return check_done();
}
