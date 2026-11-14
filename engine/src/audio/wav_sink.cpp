#include "audio/wav_sink.h"

#include <cstring>

namespace bbm {
namespace {
void put32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = uint8_t(v >> 8 * i); }
void put16(uint8_t *p, uint32_t v) { p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); }
void header(uint8_t h[44], int rate, int ch, uint32_t data) {
    std::memcpy(h, "RIFF", 4);  put32(h + 4, 36 + data);
    std::memcpy(h + 8, "WAVEfmt ", 8);  put32(h + 16, 16);
    put16(h + 20, 1);  put16(h + 22, uint32_t(ch));  put32(h + 24, uint32_t(rate));
    put32(h + 28, uint32_t(rate * ch * 2));  put16(h + 32, uint32_t(ch * 2));  put16(h + 34, 16);
    std::memcpy(h + 36, "data", 4);  put32(h + 40, data);
}
}  // namespace

bool WavSink::open(int rate, int channels, std::string &err) {
    close();
    if (rate <= 0 || channels < 1 || channels > 8) { err = "wav: bad format"; return false; }
    f_ = std::fopen(path_.c_str(), "wb");
    if (!f_) { err = "wav: cannot create " + path_; return false; }
    uint8_t h[44];
    header(h, rate, channels, 0);
    if (std::fwrite(h, 1, 44, f_) != 44) { err = "wav: write failed"; close(); return false; }
    channels_ = channels;
    rate_ = rate;
    data_bytes_ = 0;
    return true;
}

bool WavSink::write(const int16_t *s, size_t frames, std::string &err) {
    if (!f_) { err = "wav: not open"; return false; }
    // little-endian bytes explicitly, so the file is right on any host
    std::string out;
    out.resize(frames * size_t(channels_) * 2);
    for (size_t i = 0; i < frames * size_t(channels_); i++) {
        out[2 * i] = char(uint16_t(s[i]) & 0xff);
        out[2 * i + 1] = char(uint16_t(s[i]) >> 8);
    }
    if (std::fwrite(out.data(), 1, out.size(), f_) != out.size()) { err = "wav: write failed"; return false; }
    data_bytes_ += out.size();
    return true;
}

void WavSink::close() {
    if (!f_) return;
    uint8_t h[44];
    header(h, rate_, channels_, uint32_t(data_bytes_ > 0xffffffd0u ? 0xffffffd0u : data_bytes_));
    std::fseek(f_, 0, SEEK_SET);
    std::fwrite(h, 1, 44, f_);
    std::fclose(f_);
    f_ = nullptr;
}

}  // namespace bbm
