#include "audio/null_sink.h"

#include <thread>

namespace bbm {

bool NullSink::open(int rate, int channels, std::string &err) {
    if (rate <= 0 || channels < 1) { err = "null: bad format"; return false; }
    rate_ = rate;
    frames_ = 0;
    open_ = true;
    start_ = std::chrono::steady_clock::now();
    return true;
}

bool NullSink::write(const int16_t *, size_t frames, std::string &err) {
    if (!open_) { err = "null: not open"; return false; }
    frames_ += frames;
    if (realtime_) {
        // Sleep until the wall clock has caught up with the audio written so far.
        std::this_thread::sleep_until(start_ + std::chrono::microseconds(frames_ * 1000000ull / uint64_t(rate_)));
    }
    return true;
}

}  // namespace bbm
