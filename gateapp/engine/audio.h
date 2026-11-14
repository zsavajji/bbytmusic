#pragma once
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

struct AudioStats {
    bool playing = false;
    double played = 0;  // seconds of audio written
    int underruns = 0;
};

// One playback thread: decodes a WebM/Opus file in a loop to the speaker (libasound on the phone,
// a paced null sink elsewhere, so the protocol can be tried on the Mac).
class Audio {
public:
    // emit(ev, fields): ev is "audio" or "audio_done"; fields is the JSON members without braces.
    using Emit = std::function<void(const char *ev, const std::string &fields)>;

    // False (with why) if already playing or the file cannot be read.
    bool start(double seconds, double gain_db, const std::string &webm, Emit emit, std::string &why);
    void stop();  // returns after the thread has ended
    AudioStats stats();

private:
    void run(double seconds, double gain_db, std::string webm, Emit emit);

    std::mutex m_;
    std::thread th_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> playing_{false};
    std::atomic<double> played_{0};
    std::atomic<int> underruns_{0};
};
