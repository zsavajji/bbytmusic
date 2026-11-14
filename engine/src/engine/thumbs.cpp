#include "engine/thumbs.h"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>

#include "net/http.h"
#include "util/log.h"
#include "util/sha1.h"

namespace bbm {
namespace {

constexpr size_t kMaxUrls = 200;          // per request
constexpr int kLarge = 200;               // px: from here a picture goes to the front of the queue
constexpr size_t kMaxBytes = 2u << 20;    // per picture
constexpr size_t kTrimAbove = 3000, kTrimBy = 1000;   // files in the folder
// The same desktop user agent as innertube/client.cpp.
constexpr const char *kUserAgent =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
    "Chrome/140.0.0.0 Safari/537.36";

bool exists(const std::string &p) {
    struct stat st;
    return ::stat(p.c_str(), &st) == 0;
}

bool has_suffix(const std::string &s, const std::string &suf) {
    return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

// ponytail: a count cap, not bytes; LRU by mtime only (a hit does not touch the file).
void trim(const std::string &dir) {
    std::vector<std::pair<time_t, std::string>> files;
    if (DIR *d = ::opendir(dir.c_str())) {
        while (dirent *e = ::readdir(d)) {
            const std::string n = e->d_name;
            if (n == "." || n == "..") continue;
            const std::string p = dir + "/" + n;
            if (has_suffix(n, ".tmp")) {   // a fetch that never finished
                std::remove(p.c_str());
                continue;
            }
            struct stat st;
            if (::stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode)) files.emplace_back(st.st_mtime, p);
        }
        ::closedir(d);
    }
    if (files.size() <= kTrimAbove) return;
    std::sort(files.begin(), files.end());
    for (size_t i = 0; i < kTrimBy; i++) std::remove(files[i].second.c_str());
    log_info("thumbs: dropped " + std::to_string(kTrimBy) + " of " + std::to_string(files.size()) + " files");
}

}  // namespace

std::string Thumbs::sized_url(const std::string &url, int size) {
    std::string u = url;
    if (size > 0 && u.find("googleusercontent.com") != std::string::npos) {
        const size_t slash = u.rfind('/'), eq = u.rfind('=');
        if (eq != std::string::npos && (slash == std::string::npos || eq > slash)) u.erase(eq);
        const std::string n = std::to_string(size);
        u += "=w" + n + "-h" + n + "-l90-rj";
    }
    if (const size_t at = u.find("/vi_webp/"); at != std::string::npos) u.replace(at, 9, "/vi/");
    const size_t q = u.find('?'), end = q == std::string::npos ? u.size() : q;
    if (end >= 5 && u.compare(end - 5, 5, ".webp") == 0) u.replace(end - 5, 5, ".jpg");
    return u;
}

Thumbs::Thumbs(std::string dir, Ready ready) : dir_(std::move(dir)), ready_(std::move(ready)) {
    ::mkdir(dir_.c_str(), 0700);
    trim(dir_);
    thread_ = std::thread([this] { run(); });
}

Thumbs::~Thumbs() {
    cancel_ = true;
    {
        std::lock_guard<std::mutex> lk(m_);
        quit_ = true;
    }
    cv_.notify_all();
    thread_.join();
}

std::string Thumbs::find(const std::string &sized) const {
    const std::string base = dir_ + "/" + sha1_hex(sized);
    for (const char *ext : {".jpg", ".png"})
        if (exists(base + ext)) return base + ext;
    return "";
}

std::map<std::string, std::string> Thumbs::request(const std::vector<std::string> &urls, int size) {
    std::map<std::string, std::string> have;
    bool queued = false;
    for (size_t i = 0; i < urls.size() && i < kMaxUrls; i++) {
        const std::string &u = urls[i];
        if (u.rfind("http://", 0) != 0 && u.rfind("https://", 0) != 0) continue;
        const std::string sized = sized_url(u, size);
        if (std::string f = find(sized); !f.empty()) {
            have[u] = f;
            continue;
        }
        std::lock_guard<std::mutex> lk(m_);
        const auto [waiting, fresh] = pending_.try_emplace(sized);
        waiting->second.insert(u);
        if (fresh) {
            // A large picture is the one being looked at (Now Playing, a page's header): it does
            // not wait behind a list's rows.
            if (size >= kLarge) queue_.emplace_front(u, size);
            else queue_.emplace_back(u, size);
            queued = true;
        }
    }
    if (queued) cv_.notify_all();
    return have;
}

void Thumbs::run() {
    std::unique_lock<std::mutex> lk(m_);
    while (!quit_) {
        if (queue_.empty()) {
            cv_.wait(lk);
            continue;
        }
        const auto [url, size] = queue_.front();
        queue_.pop_front();
        lk.unlock();
        const std::string sized = sized_url(url, size);
        const std::string file = fetch(sized);
        lk.lock();
        const std::set<std::string> asked = std::move(pending_[sized]);
        pending_.erase(sized);
        if (file.empty()) continue;
        lk.unlock();
        for (const std::string &u : asked) ready_(u, size, file);
        lk.lock();
    }
}

std::string Thumbs::fetch(const std::string &sized) {
    HttpRequest req;
    req.url = sized;
    req.headers = {{"User-Agent", kUserAgent}};
    req.timeout_s = 15;
    req.cancel = &cancel_;
    const HttpResponse r = http_.send(req);
    if (cancel_) return "";
    if (!r.ok() || r.body.empty() || r.body.size() > kMaxBytes) {
        log_warn("thumbs: " + sized + ": " + (r.error.empty() ? "status " + std::to_string(r.status) + ", " +
                                                                    std::to_string(r.body.size()) + " bytes"
                                                              : r.error));
        return "";
    }
    const std::string base = dir_ + "/" + sha1_hex(sized);
    const std::string file = base + (r.body.compare(0, 4, "\x89PNG") == 0 ? ".png" : ".jpg");
    const std::string tmp = base + ".tmp";   // renamed into place: the UI never opens half a file
    FILE *f = std::fopen(tmp.c_str(), "wb");
    const bool ok = f && std::fwrite(r.body.data(), 1, r.body.size(), f) == r.body.size();
    if (f && std::fclose(f) != 0) {
        std::remove(tmp.c_str());
        return "";
    }
    if (!ok || std::rename(tmp.c_str(), file.c_str()) != 0) {
        log_warn("thumbs: cannot write " + file);
        std::remove(tmp.c_str());
        return "";
    }
    return file;
}

}  // namespace bbm
