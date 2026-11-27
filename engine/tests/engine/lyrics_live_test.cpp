// Against lrclib.net and music.youtube.com. Run on request: ctest -L live. Prints counts, not lyrics.
#include <cstdio>

#include "check.h"
#include "engine/lyrics.h"
#include "innertube/lyrics.h"

using namespace bbm;

int main() {
    Http http;
    Lyrics l;
    std::string plain, err;
    CHECK(fetch_lrclib(http, "Never Gonna Give You Up", "Rick Astley", 213, l, plain, err));
    std::printf("lrclib: synced %d, %zu lines, first ms %lld, credit %s, plain %zu bytes\n", int(l.synced), l.lines.size(),
                l.lines.empty() ? -1LL : (long long)l.lines[0].ms, l.credit.c_str(), plain.size());
    CHECK(l.synced && l.lines.size() > 10);
    CHECK(fetch_lrclib(http, "Zzzz No Such Song Qqqq", "Nobody Atall", 100, l, plain, err));
    CHECK(l.lines.empty() && plain.empty());

    InnerTube yt;
    std::string text, credit;
    CHECK(yt_lyrics(yt, "dQw4w9WgXcQ", text, credit, err));
    std::printf("youtube: %zu bytes, %zu lines, credit %s, starts \"%.30s\"\n", text.size(), plain_lines(text).size(), credit.c_str(), text.c_str());
    CHECK(!text.empty());
    return check_done();
}
