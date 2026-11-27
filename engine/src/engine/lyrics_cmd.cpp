// Engine: the `lyrics` command. LRCLIB's synced lines, else YouTube Music's text, else LRCLIB's plain text.
#include <algorithm>

#include "engine/engine.h"
#include "engine/lyrics.h"
#include "innertube/lyrics.h"
#include "util/log.h"

namespace bbm {
namespace {

Reply answer(const std::string &id, bool synced, const std::vector<LyricLine> &lines, const std::string &credit) {
    std::string out = "{\"id\":" + json_quote(id) + ",\"synced\":" + (synced ? "true" : "false") + ",\"lines\":[";
    for (size_t i = 0; i < lines.size(); i++)
        out += (i ? ",{\"ms\":" : "{\"ms\":") + std::to_string(lines[i].ms) + ",\"text\":" + json_quote(lines[i].text) + "}";
    Reply r;
    r.result = out + "],\"credit\":" + json_quote(credit) + "}";
    return r;
}

}  // namespace

Reply Engine::lyrics_command(Json req) {
    if (!req["id"].is_string() || req["id"].str().empty()) return Reply::error("bad_request", "id: a video id is required");
    const std::string id = req["id"].string();
    const std::vector<Track> q = player_->queue();
    const auto t = std::find_if(q.begin(), q.end(), [&](const Track &x) { return x.id == id; });
    if (t == q.end()) return Reply::error("not_found", "lyrics: " + id + " is not in the queue");

    Lyrics lrc;
    std::string plain, lrc_err, yt_text, yt_credit, yt_err;
    bool lrc_ok;
    {
        std::lock_guard<std::mutex> lk(lyrics_mu_);
        lrc_ok = fetch_lrclib(lyrics_http_, clean_title(t->title, t->artist), t->artist, t->duration_s, lrc, plain, lrc_err);
    }
    if (!lrc_ok) log_warn(lrc_err);
    else if (lrc.synced) return answer(id, true, lrc.lines, lrc.credit);

    const bool yt_ok = yt_call([&](InnerTube &yt, std::string &e) { return yt_lyrics(yt, id, yt_text, yt_credit, e); }, yt_err);
    if (!yt_ok) log_warn("lyrics: " + yt_err);
    else if (!yt_text.empty()) return answer(id, false, plain_lines(yt_text), yt_credit);

    if (!plain.empty()) return answer(id, false, plain_lines(plain), "Source: LRCLIB");
    if (!lrc_ok && !yt_ok) return Reply::error("network", yt_err);
    return Reply::error("not_found", "lyrics: none found");
}

}  // namespace bbm
