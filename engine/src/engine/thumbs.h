// The thumbnail cache: pictures fetched in the background into a folder the UI reads files from.
// One fetch thread with its own Http; each url is queued once while it waits or is in flight.
#pragma once
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "net/http.h"

namespace bbm {

class Thumbs {
public:
    // Called on the fetch thread with the url and size as requested, and the file now in the cache.
    using Ready = std::function<void(const std::string &url, int size, const std::string &file)>;

    Thumbs(std::string dir, Ready ready);   // makes the folder; starts the fetch thread, which trims it first
    ~Thumbs();                              // cancels a fetch in flight and joins
    Thumbs(const Thumbs &) = delete;
    Thumbs &operator=(const Thumbs &) = delete;

    // The urls already cached, each with its file; the others are queued (once each) and
    // reported through `ready`. Only http(s) urls are taken, at most 200 per call.
    std::map<std::string, std::string> request(const std::vector<std::string> &urls, int size);

    // What is fetched for `url` at `size`: a googleusercontent url at size x size as a JPEG; WebP
    // forms of YouTube's image hosts as JPEG (BB10 cannot decode WebP).
    static std::string sized_url(const std::string &url, int size);

private:
    std::string find(const std::string &sized) const;   // the cached file, or ""
    void run();
    std::string fetch(const std::string &sized);   // the file written, or ""

    std::string dir_;
    Ready ready_;
    std::mutex m_;
    std::condition_variable cv_;
    std::deque<std::pair<std::string, int>> queue_;
    // Sized urls queued or in flight, each with the urls it was asked as: two urls of one picture
    // (the album's cover at w120 and at w544) are one fetch and two answers.
    std::map<std::string, std::set<std::string>> pending_;
    Http http_;   // the fetch thread's own, so its connections stay alive
    std::atomic<bool> cancel_{false};
    bool quit_ = false;
    std::thread thread_;   // last
};

}  // namespace bbm
