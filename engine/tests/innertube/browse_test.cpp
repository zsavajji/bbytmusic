// Parses the recorded browse responses of tests/fixtures/innertube.
#include <fstream>
#include <sstream>

#include "check.h"
#include "innertube/browse.h"

using namespace bbm;

namespace {
bool load(const char *name, JsonDoc &doc) {
    std::ifstream f(std::string(BBM_FIXTURES) + "/innertube/" + name, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    std::string err;
    return JsonDoc::parse(ss.str(), doc, err);
}
}  // namespace

int main() {
    std::string err;
    {   // An album
        JsonDoc d;
        BrowsePage p;
        CHECK(load("browse_album.json", d) && parse_browse(d.root(), "MPREb_W0LXV3Jg2OV", p, err));
        CHECK_EQ(p.kind, std::string("album"));
        CHECK_EQ(p.title, std::string("In the Aeroplane Over the Sea"));
        CHECK_EQ(p.artist, std::string("Neutral Milk Hotel"));
        CHECK_EQ(p.artist_id, std::string("UCvMA46Xu1xApqZyXFIrnukw"));
        CHECK_EQ(p.year, std::string("1998"));
        CHECK_EQ(p.subtitle, std::string("Album \xE2\x80\xA2 1998"));
        CHECK_EQ(p.second_subtitle, std::string("11 songs \xE2\x80\xA2 40 minutes"));
        CHECK_EQ(p.playlist_id, std::string("OLAK5uy_n7L3OCuk8HJTOLrjdSY-Vx9UMF6pn_W7Q"));
        CHECK(!p.description.empty());
        const std::string tail = "=w544-h544-l90-rj";
        CHECK(p.thumb.size() > tail.size() && p.thumb.compare(p.thumb.size() - tail.size(), tail.size(), tail) == 0);
        CHECK_EQ(p.items.size(), size_t(11));
        const Item &t = p.items.at(0);
        CHECK_EQ(t.id, std::string("eUPNdwyVUJs"));
        CHECK_EQ(t.kind, std::string("song"));
        CHECK_EQ(t.title, std::string("King of Carrot Flowers Pt. 1"));
        CHECK_EQ(t.duration_s, 121);
        CHECK_EQ(t.plays, std::string("2.7M plays"));
        CHECK_EQ(t.artist, std::string("Neutral Milk Hotel"));
        CHECK_EQ(t.album, p.title);
        CHECK_EQ(t.album_id, std::string("MPREb_W0LXV3Jg2OV"));
        CHECK(!t.thumb.empty());
        for (const Item &i : p.items) CHECK(!i.artist.empty() && !i.thumb.empty() && i.duration_s > 0);
    }
    {   // A playlist
        JsonDoc d;
        BrowsePage p;
        CHECK(load("browse_playlist.json", d) &&
              parse_browse(d.root(), "VLRDCLAK5uy_kVuz2Y7g6AUnbFdB2hLHo6EH8ieytGHbg", p, err));
        CHECK_EQ(p.kind, std::string("playlist"));
        CHECK_EQ(p.title, std::string("Billion Views Club - Pop"));
        CHECK_EQ(p.artist, std::string("YouTube Music"));
        CHECK(!p.playlist_id.empty());
        CHECK(p.items.size() >= 90);
        CHECK_EQ(p.items.at(0).title, std::string("New Rules"));
        CHECK_EQ(p.items[0].artist, std::string("Dua Lipa"));
        CHECK_EQ(p.items[0].duration_s, 225);
        CHECK(!p.items[0].thumb.empty());
    }
    {   // Nothing to show
        JsonDoc d;
        BrowsePage p;
        std::string e;
        CHECK(JsonDoc::parse("{}", d, e));
        CHECK(!parse_browse(d.root(), "MPREb_x", p, e));
        CHECK(!e.empty());
    }
    return check_done();
}
