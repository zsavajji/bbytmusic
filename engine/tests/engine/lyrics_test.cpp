// The LRC parser, the title cleaner, LRCLIB's URL and answer; no network. The fixtures' lines are made up.
#include <fstream>
#include <sstream>

#include "check.h"
#include "engine/lyrics.h"

using namespace bbm;

static std::string load(const char *name) {
    std::ifstream f(std::string(BBM_FIXTURES) + "/lyrics/" + name, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

int main() {
    std::vector<LyricLine> v;
    CHECK(parse_lrc("[00:18.83]One\r\n[00:22.8]Two\n[01:05]Three\n[00:30.123] Four  \n", v));
    CHECK_EQ(v.size(), size_t(4));
    if (v.size() == 4) {
        CHECK_EQ(v[0].ms, int64_t(18830));
        CHECK_EQ(v[0].text, std::string("One"));
        CHECK_EQ(v[1].ms, int64_t(22800));
        CHECK_EQ(v[2].ms, int64_t(30123));
        CHECK_EQ(v[2].text, std::string("Four"));
        CHECK_EQ(v[3].ms, int64_t(65000));
    }

    // Several stamps, sorted; an empty text is a gap and stays; a bracket inside the text stays.
    CHECK(parse_lrc("[00:20.00][00:10.00]Chorus\n[00:05.00]\n[00:15.00][Sung] again", v));
    CHECK_EQ(v.size(), size_t(4));
    if (v.size() == 4) {
        CHECK_EQ(v[0].ms, int64_t(5000));
        CHECK_EQ(v[0].text, std::string(""));
        CHECK_EQ(v[1].ms, int64_t(10000));
        CHECK_EQ(v[2].text, std::string("[Sung] again"));
        CHECK_EQ(v[3].ms, int64_t(20000));
        CHECK_EQ(v[3].text, std::string("Chorus"));
    }

    // Offset: positive makes lines earlier, clamped at 0; metadata and unstamped lines are skipped.
    CHECK(parse_lrc("[ar:X]\n[ti:Y]\n[length:3:20]\n[offset:+500]\nno stamp here\n[00:00.20]A\n[00:01.00]B", v));
    CHECK_EQ(v.size(), size_t(2));
    if (v.size() == 2) {
        CHECK_EQ(v[0].ms, int64_t(0));
        CHECK_EQ(v[1].ms, int64_t(500));
    }
    CHECK(parse_lrc("[00:01.00]A\n[offset:-250]", v));
    CHECK_EQ(v[0].ms, int64_t(1250));

    CHECK(!parse_lrc("", v));
    CHECK(!parse_lrc("just words\n[ar:X]\n[00:xx]nope\n[", v));
    CHECK(v.empty());

    // Plain text
    auto p = plain_lines("\n\r\nOne \r\nTwo\n\n  Three\n\n");
    CHECK_EQ(p.size(), size_t(4));
    if (p.size() == 4) {
        CHECK_EQ(p[0].text, std::string("One"));
        CHECK_EQ(p[1].text, std::string("Two"));
        CHECK_EQ(p[2].text, std::string(""));
        CHECK_EQ(p[3].text, std::string("Three"));
        CHECK_EQ(p[3].ms, int64_t(0));
    }
    CHECK(plain_lines("").empty());

    // Titles
    CHECK_EQ(clean_title("Song (Official Video)", "A"), std::string("Song"));
    CHECK_EQ(clean_title("Song [Official Audio]", "A"), std::string("Song"));
    CHECK_EQ(clean_title("Song (feat. X)", "A"), std::string("Song"));
    CHECK_EQ(clean_title("Song (ft. X) [HD]", "A"), std::string("Song"));
    CHECK_EQ(clean_title("Artist - Song (Lyric Video)", "Artist"), std::string("Song"));
    CHECK_EQ(clean_title("ARTIST - Song", "artist"), std::string("Song"));
    CHECK_EQ(clean_title("Song (Remix)", "A"), std::string("Song (Remix)"));
    CHECK_EQ(clean_title("Song (Live)", "A"), std::string("Song (Live)"));
    CHECK_EQ(clean_title("Other - Song", "Artist"), std::string("Other - Song"));
    CHECK_EQ(clean_title("Song (HDR mix)", "A"), std::string("Song (HDR mix)"));
    CHECK_EQ(clean_title("A  (Official Video) B", "X"), std::string("A B"));
    CHECK_EQ(clean_title("Song (unclosed", "A"), std::string("Song (unclosed"));

    // URL
    CHECK_EQ(lrclib_url("Don't Stop", "AC/DC & Co", 213),
             std::string("https://lrclib.net/api/get?track_name=Don%27t%20Stop&artist_name=AC%2FDC%20%26%20Co&duration=213"));
    CHECK_EQ(lrclib_url("\xC3\xA9", "a", 0), std::string("https://lrclib.net/api/get?track_name=%C3%A9&artist_name=a"));

    // The answers
    Lyrics l;
    std::string plain, err;
    CHECK(parse_lrclib(load("lrclib_get.json"), l, plain, err));
    CHECK(l.synced);
    CHECK_EQ(l.credit, std::string("Source: LRCLIB"));
    CHECK_EQ(l.lines.size(), size_t(4));
    if (l.lines.size() == 4) {
        CHECK_EQ(l.lines[0].ms, int64_t(500));
        CHECK_EQ(l.lines[0].text, std::string(""));
        CHECK_EQ(l.lines[1].ms, int64_t(18830));
        CHECK_EQ(l.lines[3].ms, int64_t(65100));
    }
    CHECK_EQ(plain_lines(plain).size(), size_t(4));
    CHECK(parse_lrclib(load("lrclib_instrumental.json"), l, plain, err));
    CHECK(!l.synced && l.lines.empty() && plain.empty());
    CHECK(parse_lrclib("{\"syncedLyrics\":null,\"plainLyrics\":\"x\"}", l, plain, err));
    CHECK(!l.synced && l.lines.empty());
    CHECK_EQ(plain, std::string("x"));
    CHECK(!parse_lrclib("nope", l, plain, err));
    CHECK(!parse_lrclib("[1]", l, plain, err));
    return check_done();
}
