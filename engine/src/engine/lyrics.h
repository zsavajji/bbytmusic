// Lyrics from LRCLIB: the LRC parser, the title cleaner and the /api/get call. Pure functions,
// no state. The request goes out only when the UI asks for a song's lyrics.
#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "net/http.h"

namespace bbm {

struct LyricLine {
    int64_t ms = 0;
    std::string text;   // empty in synced lines: an instrumental gap
};

struct Lyrics {
    bool synced = false;
    std::vector<LyricLine> lines;
    std::string credit;
};

// LRC text: `[mm:ss.xx]text`, `[mm:ss.xxx]`, `[mm:ss]`, several stamps on a line, `[offset:N]` (ms,
// positive means earlier). Metadata tags and lines without a stamp are skipped; empty texts are
// kept. `out` is sorted by time. False when no line came out.
bool parse_lrc(std::string_view text, std::vector<LyricLine> &out);

// Plain text as lines (ms 0): stanza breaks kept, leading and trailing empty lines dropped.
std::vector<LyricLine> plain_lines(std::string_view text);

// A video's title as a song's: without "(Official Video)", "[Lyric Video]", "(feat. X)" and the
// like, and without a leading "Artist - " when the artist is the one given.
std::string clean_title(const std::string &title, const std::string &artist);

// LRCLIB's /api/get URL; the duration is left out when it is not positive.
std::string lrclib_url(const std::string &title, const std::string &artist, int duration_s);

// An /api/get answer: the synced lines (credit "Source: LRCLIB") and the plain text, either of them
// empty when LRCLIB has none; an instrumental has neither. False only for invalid JSON.
bool parse_lrclib(std::string_view body, Lyrics &synced, std::string &plain, std::string &err);

// GET with the engine's Http. A 404 is "no lyrics" (true, both empty); any other failure is false.
bool fetch_lrclib(Http &http, const std::string &title, const std::string &artist, int duration_s, Lyrics &synced,
                  std::string &plain, std::string &err);

}  // namespace bbm
