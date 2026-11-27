#include "engine/lyrics.h"

#include <algorithm>
#include <cstdio>

#include "util/json.h"

namespace bbm {

namespace {

bool is_digit(char c) { return c >= '0' && c <= '9'; }
bool is_alnum(char c) { return is_digit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
char lower(char c) { return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c; }

std::string_view trim(std::string_view s) {
    const char *ws = " \t\r\n";
    const size_t a = s.find_first_not_of(ws);
    if (a == std::string_view::npos) return {};
    return s.substr(a, s.find_last_not_of(ws) - a + 1);
}

std::string lower_str(std::string_view s) {
    std::string r(s);
    for (char &c : r) c = lower(c);
    return r;
}

// `mm:ss`, `mm:ss.f`, `mm:ss.ff`, `mm:ss.fff` (the fraction after '.' or ':') as ms; false if it is not a stamp.
bool parse_stamp(std::string_view s, int64_t &ms) {
    size_t i = 0;
    auto number = [&](int64_t &v, size_t &digits) {
        v = 0;
        digits = 0;
        while (i < s.size() && is_digit(s[i])) {
            if (digits < 9) v = v * 10 + (s[i] - '0');
            digits++, i++;
        }
        return digits > 0;
    };
    int64_t m, sec, frac = 0;
    size_t d;
    if (!number(m, d) || i >= s.size() || s[i++] != ':' || !number(sec, d)) return false;
    if (i < s.size()) {
        if (s[i] != '.' && s[i] != ':') return false;
        i++;
        if (!number(frac, d) || i != s.size()) return false;
        while (d > 3) frac /= 10, d--;   // finer than ms: cut
        while (d < 3) frac *= 10, d++;
    }
    ms = m * 60000 + sec * 1000 + frac;
    return true;
}

// "[offset:+250]" as 250; false if `tag` (the text between the brackets) is not an offset.
bool parse_offset(std::string_view tag, int64_t &off) {
    if (tag.substr(0, 7) != "offset:") return false;
    std::string_view v = trim(tag.substr(7));
    bool neg = false;
    if (!v.empty() && (v[0] == '+' || v[0] == '-')) neg = v[0] == '-', v.remove_prefix(1);
    if (v.empty() || v.size() > 9) return false;
    int64_t n = 0;
    for (char c : v) {
        if (!is_digit(c)) return false;
        n = n * 10 + (c - '0');
    }
    off = neg ? -n : n;
    return true;
}

}  // namespace

bool parse_lrc(std::string_view text, std::vector<LyricLine> &out) {
    out.clear();
    int64_t offset = 0;
    while (!text.empty()) {
        const size_t nl = text.find('\n');
        std::string_view line = trim(text.substr(0, nl));
        text = nl == std::string_view::npos ? std::string_view() : text.substr(nl + 1);
        std::vector<int64_t> stamps;
        while (!line.empty() && line[0] == '[') {
            const size_t close = line.find(']');
            if (close == std::string_view::npos) break;
            const std::string_view tag = line.substr(1, close - 1);
            int64_t ms;
            if (parse_stamp(tag, ms)) stamps.push_back(ms);
            else if (stamps.empty() && parse_offset(tag, ms)) offset = ms;
            else if (stamps.empty()) line = {};   // a metadata tag: the line is skipped
            else break;                           // a bracket in the text
            if (line.empty()) break;
            line.remove_prefix(close + 1);
        }
        for (int64_t ms : stamps) out.push_back({ms, std::string(trim(line))});
    }
    for (LyricLine &l : out) l.ms = std::max<int64_t>(0, l.ms - offset);
    std::stable_sort(out.begin(), out.end(), [](const LyricLine &a, const LyricLine &b) { return a.ms < b.ms; });
    return !out.empty();
}

std::vector<LyricLine> plain_lines(std::string_view text) {
    std::vector<LyricLine> out;
    while (true) {
        const size_t nl = text.find('\n');
        out.push_back({0, std::string(trim(text.substr(0, nl)))});
        if (nl == std::string_view::npos) break;
        text.remove_prefix(nl + 1);
    }
    while (!out.empty() && out.back().text.empty()) out.pop_back();
    size_t lead = 0;
    while (lead < out.size() && out[lead].text.empty()) lead++;
    out.erase(out.begin(), out.begin() + lead);
    return out;
}

namespace {

// Does a bracket's content start with one of the words that mark a video's decoration?
bool is_decoration(std::string_view content) {
    static const char *const words[] = {"official", "lyric video", "lyrics", "audio", "video", "mv", "visualizer",
                                        "visualiser", "hd", "4k", "feat", "ft", "featuring"};
    const std::string c = lower_str(trim(content));
    for (const char *w : words) {
        const size_t n = std::char_traits<char>::length(w);
        if (c.compare(0, n, w) == 0 && (c.size() == n || !is_alnum(c[n]))) return true;   // "feat." and "feat X" yes, "hdr" no
    }
    return false;
}

}  // namespace

std::string clean_title(const std::string &title, const std::string &artist) {
    std::string t;
    for (size_t i = 0; i < title.size();) {
        const char open = title[i];
        if (open == '(' || open == '[') {
            const size_t close = title.find(open == '(' ? ')' : ']', i + 1);
            if (close != std::string::npos && is_decoration(std::string_view(title).substr(i + 1, close - i - 1))) {
                i = close + 1;
                continue;
            }
        }
        t += title[i++];
    }
    std::string r;   // runs of spaces collapsed
    for (char c : t)
        if (c != ' ' || (!r.empty() && r.back() != ' ')) r += c;
    std::string s(trim(r));
    if (const size_t dash = s.find(" - "); dash != std::string::npos && lower_str(trim(std::string_view(s).substr(0, dash))) == lower_str(trim(artist)))
        s = std::string(trim(std::string_view(s).substr(dash + 3)));
    return s;
}

namespace {
// RFC 3986: unreserved kept, everything else (UTF-8 bytes too) as %XX.
std::string pct(const std::string &s) {
    std::string r;
    for (unsigned char c : s) {
        if (is_alnum(char(c)) || c == '-' || c == '.' || c == '_' || c == '~') r += char(c);
        else {
            char b[4];
            std::snprintf(b, sizeof b, "%%%02X", c);
            r += b;
        }
    }
    return r;
}
}  // namespace

std::string lrclib_url(const std::string &title, const std::string &artist, int duration_s) {
    std::string u = "https://lrclib.net/api/get?track_name=" + pct(title) + "&artist_name=" + pct(artist);
    if (duration_s > 0) u += "&duration=" + std::to_string(duration_s);
    return u;
}

bool parse_lrclib(std::string_view body, Lyrics &synced, std::string &plain, std::string &err) {
    synced = {};
    plain.clear();
    JsonDoc doc;
    if (!JsonDoc::parse(body, doc, err)) return false;
    if (!doc.root().is_object()) {
        err = "not an object";
        return false;
    }
    if (doc.root()["instrumental"].boolean()) return true;
    if (parse_lrc(doc.root()["syncedLyrics"].str(), synced.lines)) {
        synced.synced = true;
        synced.credit = "Source: LRCLIB";
    }
    plain = doc.root()["plainLyrics"].string();
    return true;
}

bool fetch_lrclib(Http &http, const std::string &title, const std::string &artist, int duration_s, Lyrics &synced,
                  std::string &plain, std::string &err) {
    synced = {};
    plain.clear();
    HttpRequest req;
    req.url = lrclib_url(title, artist, duration_s);
    req.headers.push_back({"User-Agent", "BBMusic (https://github.com/zsavajji/bbytmusic)"});
    req.timeout_s = 10;
    const HttpResponse r = http.send(req);
    if (r.status == 404 && r.error.empty()) return true;
    if (!r.ok()) {
        err = "lrclib: " + (r.error.empty() ? "HTTP " + std::to_string(r.status) : r.error);
        return false;
    }
    if (parse_lrclib(r.body, synced, plain, err)) return true;
    err = "lrclib: " + err;
    return false;
}

}  // namespace bbm
