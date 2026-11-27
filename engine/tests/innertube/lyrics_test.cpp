// YouTube Music's lyrics tab, on hand-written answers with made-up lines; no network.
#include <fstream>
#include <sstream>

#include "check.h"
#include "innertube/lyrics.h"

using namespace bbm;

static bool load(const char *name, JsonDoc &doc) {
    std::ifstream f(std::string(BBM_FIXTURES) + "/innertube/" + name, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    std::string err;
    return JsonDoc::parse(ss.str(), doc, err);
}

int main() {
    std::string text, credit, err;
    JsonDoc d;
    CHECK(load("browse_lyrics.json", d) && parse_yt_lyrics(d.root(), text, credit, err));
    CHECK_EQ(text, std::string("First made-up line\nSecond made-up line\n\nThird made-up line"));
    CHECK_EQ(credit, std::string("Source: Musixmatch"));

    JsonDoc none;
    CHECK(load("browse_lyrics_none.json", none) && parse_yt_lyrics(none.root(), text, credit, err));
    CHECK(text.empty() && credit.empty());

    CHECK(!parse_yt_lyrics(Json(), text, credit, err));
    return check_done();
}
