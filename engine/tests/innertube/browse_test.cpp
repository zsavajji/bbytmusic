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
        CHECK(p.sections.empty());
        CHECK(p.items.size() >= 90);
        CHECK_EQ(p.items.at(0).title, std::string("New Rules"));
        CHECK_EQ(p.items[0].artist, std::string("Dua Lipa"));
        CHECK_EQ(p.items[0].duration_s, 225);
        CHECK(!p.items[0].thumb.empty());
    }
    {   // An artist
        JsonDoc d;
        BrowsePage p;
        CHECK(load("browse_artist.json", d) && parse_browse(d.root(), "UCvMA46Xu1xApqZyXFIrnukw", p, err));
        CHECK_EQ(p.kind, std::string("artist"));
        CHECK_EQ(p.title, std::string("Neutral Milk Hotel"));
        CHECK_EQ(p.subtitle, std::string("42.5K subscribers"));
        CHECK(!p.thumb.empty());
        CHECK(p.description.rfind("Neutral Milk Hotel was an American band", 0) == 0);
        CHECK_EQ(p.items.size(), size_t(5));
        const Item &t = p.items.at(0);
        CHECK_EQ(t.title, std::string("In The Aeroplane Over The Sea"));
        CHECK_EQ(t.kind, std::string("song"));
        CHECK_EQ(t.plays, std::string("15M plays"));
        CHECK_EQ(t.album, std::string("In The Aeroplane Over The Sea"));
        CHECK(t.album_id.rfind("MPRE", 0) == 0);
        CHECK_EQ(t.artist, std::string("Neutral Milk Hotel"));
        const char *want[] = {"Albums", "Singles & EPs", "Videos", "Featured on", "Fans might also like"};
        CHECK_EQ(p.sections.size(), size_t(5));
        for (size_t i = 0; i < p.sections.size() && i < 5; i++) CHECK_EQ(p.sections[i].title, std::string(want[i]));
        const Item &a = p.sections.at(0).items.at(0);
        CHECK_EQ(a.kind, std::string("album"));
        CHECK_EQ(a.title, std::string("Ferris Wheel on Fire"));
        CHECK_EQ(a.year, std::string("2023"));
        for (const Item &i : p.sections.at(3).items) CHECK_EQ(i.kind, std::string("playlist"));
        for (const Item &i : p.sections.at(4).items) CHECK_EQ(i.kind, std::string("artist"));
    }
    {   // Nothing to show
        JsonDoc d;
        BrowsePage p;
        std::string e;
        CHECK(JsonDoc::parse("{}", d, e));
        CHECK(!parse_browse(d.root(), "MPREb_x", p, e));
        CHECK(!e.empty());
    }
    {   // A feed and its continuation
        JsonDoc d;
        BrowsePage p;
        CHECK(load("browse_home.json", d) && parse_browse(d.root(), "FEmusic_home", p, err));
        CHECK_EQ(p.kind, std::string("feed"));
        CHECK_EQ(p.id, std::string("FEmusic_home"));
        CHECK_EQ(p.sections.size(), size_t(3));
        CHECK_EQ(p.sections.at(0).title, std::string("Featured playlists for you"));
        CHECK_EQ(p.sections.at(1).title, std::string("New releases"));
        CHECK_EQ(p.sections.at(2).title, std::string("Trending songs for you"));
        CHECK(!p.continuation.empty());
        CHECK_EQ(p.sections[1].items.at(0).kind, std::string("album"));
        CHECK_EQ(p.sections[1].items.at(0).title, std::string("GIANNAGOLD"));
        CHECK(!p.sections[2].items.empty());
        for (const Item &it : p.sections[2].items) {
            CHECK(it.kind == "song" || it.kind == "video");
            CHECK(!it.artist.empty());
        }
        CHECK_EQ(p.sections[2].items[0].artist, std::string("Future"));
        CHECK_EQ(p.sections[2].items[0].plays, std::string("6M plays"));

        JsonDoc m;
        BrowsePage more;
        CHECK(load("browse_home_more.json", m) && parse_browse_more(m.root(), more, err));
        CHECK_EQ(more.sections.size(), size_t(1));
        CHECK_EQ(more.sections.at(0).title, std::string("Covers and remixes"));
        CHECK_EQ(more.sections[0].items.size(), size_t(4));
        CHECK_EQ(more.continuation, std::string(""));

        JsonDoc empty;
        std::string e;
        CHECK(JsonDoc::parse("{}", empty, e));
        CHECK(!parse_browse(empty.root(), "FEmusic_home", p, err));
        CHECK_EQ(err, std::string("browse: no feed in the response"));
        CHECK(!parse_browse_more(empty.root(), more, err));
        CHECK_EQ(err, std::string("browse: no continuation in the response"));
    }
    return check_done();
}
