#include "stream/segment_cache.h"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <sstream>

#include "util/log.h"

namespace bbm {
namespace {

int64_t file_size(const std::string &p) {
    struct stat st;
    return ::stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode) ? int64_t(st.st_size) : -1;
}

std::vector<std::string> list_dir(const std::string &p) {
    std::vector<std::string> out;
    if (DIR *d = ::opendir(p.c_str())) {
        while (dirent *e = ::readdir(d))
            if (std::string n = e->d_name; n != "." && n != "..") out.push_back(n);
        ::closedir(d);
    }
    return out;
}

bool read_file(const std::string &p, std::vector<uint8_t> &out) {
    const int64_t n = file_size(p);
    FILE *f = n >= 0 ? std::fopen(p.c_str(), "rb") : nullptr;
    if (!f) return false;
    out.resize(size_t(n));
    const bool ok = std::fread(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok;
}

bool write_file(const std::string &p, const void *data, size_t n) {
    const std::string tmp = p + ".tmp";
    FILE *f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(data, 1, n, f) == n;
    ok = std::fclose(f) == 0 && ok;
    if (ok && std::rename(tmp.c_str(), p.c_str()) == 0) return true;
    std::remove(tmp.c_str());
    return false;
}

std::string meta_text(const CacheMeta &m) {
    std::ostringstream o;
    o << "bbmusic-cache 1\nitag " << m.itag << "\ncontent_length " << m.content_length << "\nduration_ms "
      << m.duration_ms << "\nlast_modified " << m.last_modified << "\ninit " << m.init_size << "\nsegments "
      << m.segments.size() << "\n";
    for (const SegmentRange &s : m.segments) o << s.start_ms << ' ' << s.duration_ms << ' ' << s.size << '\n';
    return o.str();
}

bool parse_meta(const std::vector<uint8_t> &text, CacheMeta &m) {
    std::istringstream in(std::string(text.begin(), text.end()));
    std::string w[7];
    int version = 0;
    size_t n = 0;
    in >> w[0] >> version >> w[1] >> m.itag >> w[2] >> m.content_length >> w[3] >> m.duration_ms >> w[4] >>
        m.last_modified >> w[5] >> m.init_size >> w[6] >> n;
    if (!in || w[0] != "bbmusic-cache" || version != 1 || w[1] != "itag" || w[2] != "content_length" ||
        w[3] != "duration_ms" || w[4] != "last_modified" || w[5] != "init" || w[6] != "segments" || n > 100000)
        return false;
    m.segments.assign(n, {});
    int64_t offset = m.init_size;
    for (SegmentRange &s : m.segments) {
        in >> s.start_ms >> s.duration_ms >> s.size;
        if (!in || s.size <= 0) return false;
        s.offset = offset;
        offset += s.size;
    }
    std::string extra;
    return !(in >> extra) && m.init_size > 0;
}

}  // namespace

bool SegmentCache::valid_id(std::string_view id) {
    if (id.empty() || id.size() > 64) return false;
    for (char c : id)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_')) return false;
    return true;
}

SegmentCache::SegmentCache(std::string dir, int64_t cap_bytes) : dir_(std::move(dir)), cap_(cap_bytes) {
    ::mkdir(dir_.c_str(), 0700);
    scan();
}

std::string SegmentCache::path(const std::string &key, const std::string &file) const {
    return dir_ + "/" + key + (file.empty() ? "" : "/" + file);
}

void SegmentCache::scan() {
    std::lock_guard<std::mutex> lk(m_);
    std::vector<std::pair<time_t, std::string>> by_age;
    for (const std::string &key : list_dir(dir_)) {
        struct stat st;
        const std::string meta = path(key, "meta");
        if (::stat(meta.c_str(), &st) != 0) {   // never finished, or not ours
            if (struct stat d; ::stat(path(key).c_str(), &d) == 0 && S_ISDIR(d.st_mode)) erase(key);
            continue;
        }
        Entry e;
        for (const std::string &f : list_dir(path(key))) {
            const std::string p = path(key, f);
            if (f.size() > 4 && f.compare(f.size() - 4, 4, ".tmp") == 0) std::remove(p.c_str());
            else if (const int64_t n = file_size(p); n > 0) e.bytes += n;
        }
        entries_[key] = e;
        total_ += e.bytes;
        by_age.emplace_back(st.st_mtime, key);
    }
    std::sort(by_age.begin(), by_age.end());
    for (auto &[t, key] : by_age) entries_[key].used = ++clock_;
    make_room(0, "");
    log_info("cache: " + std::to_string(entries_.size()) + " tracks, " + std::to_string(total_ / 1024) + " KiB in " + dir_);
}

void SegmentCache::touch(const std::string &key, Entry &e) {
    e.used = ++clock_;
    ::utime(path(key, "meta").c_str(), nullptr);   // the order survives a restart through the mtime
}

void SegmentCache::erase(const std::string &key) {
    for (const std::string &f : list_dir(path(key))) std::remove(path(key, f).c_str());
    ::rmdir(path(key).c_str());
    if (auto it = entries_.find(key); it != entries_.end()) {
        total_ -= it->second.bytes;
        entries_.erase(it);
    }
}

bool SegmentCache::make_room(int64_t need, const std::string &keep) {
    while (total_ + need > cap_) {
        auto victim = entries_.end();
        for (auto it = entries_.begin(); it != entries_.end(); ++it)
            if (it->second.pins == 0 && it->first != keep && (victim == entries_.end() || it->second.used < victim->second.used))
                victim = it;
        if (victim == entries_.end()) return false;
        log_debug("cache: evicting " + victim->first);
        erase(victim->first);
    }
    return true;
}

bool SegmentCache::open_entry(const std::string &video_id, std::string &key, CacheMeta &meta, std::vector<uint8_t> &init) {
    std::lock_guard<std::mutex> lk(m_);
    const std::string prefix = video_id + ".";
    for (;;) {
        auto best = entries_.end();
        for (auto it = entries_.lower_bound(prefix); it != entries_.end() && it->first.compare(0, prefix.size(), prefix) == 0; ++it)
            if (best == entries_.end() || it->second.used > best->second.used) best = it;
        if (best == entries_.end()) return false;
        const std::string k = best->first;
        std::vector<uint8_t> text;
        CacheMeta m;
        int64_t sum = 0;
        bool ok = read_file(path(k, "meta"), text) && parse_meta(text, m) && read_file(path(k, "init"), init) &&
                  int64_t(init.size()) == m.init_size;
        if (ok) {
            sum = m.init_size;
            for (const SegmentRange &s : m.segments) sum += s.size;
            ok = m.content_length <= 0 || sum == m.content_length;
        }
        if (!ok) {   // a damaged head: start that track again
            log_warn("cache: " + k + ": damaged head, dropped");
            erase(k);
            init.clear();
            continue;
        }
        key = k;
        meta = std::move(m);
        best->second.pins++;
        touch(k, best->second);
        return true;
    }
}

bool SegmentCache::create_entry(const std::string &key, const CacheMeta &meta, const std::vector<uint8_t> &init) {
    std::lock_guard<std::mutex> lk(m_);
    int pins = 0;
    if (auto it = entries_.find(key); it != entries_.end()) {
        pins = it->second.pins;
        erase(key);
    }
    const std::string text = meta_text(meta);
    const int64_t need = int64_t(text.size() + init.size());
    bool ok = make_room(need, key) && (::mkdir(path(key).c_str(), 0700) == 0 || errno == EEXIST) &&
              write_file(path(key, "init"), init.data(), init.size()) && write_file(path(key, "meta"), text.data(), text.size());
    if (!ok) {
        for (const std::string &f : list_dir(path(key))) std::remove(path(key, f).c_str());
        ::rmdir(path(key).c_str());
        return false;
    }
    Entry &e = entries_[key];
    e.bytes = need;
    e.pins = pins + 1;
    e.used = ++clock_;
    total_ += need;
    return true;
}

void SegmentCache::unpin(const std::string &key) {
    std::lock_guard<std::mutex> lk(m_);
    if (auto it = entries_.find(key); it != entries_.end() && it->second.pins > 0) it->second.pins--;
}

void SegmentCache::remove(const std::string &key) {
    std::lock_guard<std::mutex> lk(m_);
    erase(key);
}

bool SegmentCache::get_segment(const std::string &key, int index, int64_t size, std::vector<uint8_t> &out) {
    std::lock_guard<std::mutex> lk(m_);
    auto it = entries_.find(key);
    if (it == entries_.end()) return false;
    const std::string p = path(key, std::to_string(index));
    const int64_t n = file_size(p);
    if (n < 0) return false;
    if (n == size && read_file(p, out) && int64_t(out.size()) == size) return true;
    log_warn("cache: " + key + "/" + std::to_string(index) + ": " + std::to_string(n) + " bytes, want " +
             std::to_string(size) + ": dropped");
    std::remove(p.c_str());
    int64_t now = 0;   // the file changed behind our back: count the folder again
    for (const std::string &f : list_dir(path(key))) now += std::max<int64_t>(0, file_size(path(key, f)));
    total_ += now - it->second.bytes;
    it->second.bytes = now;
    out.clear();
    return false;
}

void SegmentCache::put_segment(const std::string &key, int index, const std::vector<uint8_t> &bytes) {
    std::lock_guard<std::mutex> lk(m_);
    if (entries_.find(key) == entries_.end()) return;
    const std::string p = path(key, std::to_string(index));
    const int64_t old = std::max<int64_t>(0, file_size(p));
    entries_[key].bytes -= old;
    total_ -= old;
    if (!make_room(int64_t(bytes.size()), key)) {   // only open tracks are left: keep the cap
        std::remove(p.c_str());
        return;
    }
    auto it = entries_.find(key);   // make_room never erases `key`, but look again rather than trust it
    if (it == entries_.end() || !write_file(p, bytes.data(), bytes.size())) return;
    it->second.bytes += int64_t(bytes.size());
    total_ += int64_t(bytes.size());
    touch(key, it->second);
}

void SegmentCache::set_cap(int64_t cap_bytes) {
    std::lock_guard<std::mutex> lk(m_);
    cap_ = cap_bytes;
    make_room(0, "");
}

int64_t SegmentCache::cap() const {
    std::lock_guard<std::mutex> lk(m_);
    return cap_;
}

void SegmentCache::clear() {
    std::lock_guard<std::mutex> lk(m_);
    std::vector<std::string> keys;   // erase() takes the entry out of the map: collect first
    for (const auto &[key, e] : entries_)
        if (e.pins == 0) keys.push_back(key);
    for (const std::string &key : keys) erase(key);
}

int64_t SegmentCache::bytes() const {
    std::lock_guard<std::mutex> lk(m_);
    return total_;
}

bool SegmentCache::has(const std::string &key) const {
    std::lock_guard<std::mutex> lk(m_);
    return entries_.count(key) != 0;
}

}  // namespace bbm
