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
        CHECK(!p.continuation.empty());

        JsonDoc m;
        BrowsePage more;
        CHECK(load("browse_playlist_more.json", m) && parse_browse_more(m.root(), more, err));
        CHECK_EQ(more.items.size(), size_t(3));
        CHECK_EQ(more.items.at(0).title, std::string("Screwzooo (feat. Willlean Wiz)"));
        CHECK_EQ(more.items[0].artist, std::string("ESG"));
        CHECK(more.items[0].kind == "song" || more.items[0].kind == "video");
        CHECK(!more.items[0].thumb.empty());
        CHECK_EQ(more.continuation, std::string(""));
        CHECK(more.sections.empty());
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
        CHECK(more.items.empty());

        JsonDoc empty;
        std::string e;
        CHECK(JsonDoc::parse("{}", empty, e));
        CHECK(!parse_browse(empty.root(), "FEmusic_home", p, err));
        CHECK_EQ(err, std::string("browse: no feed in the response"));
        CHECK(!parse_browse_more(empty.root(), more, err));
        CHECK_EQ(err, std::string("browse: no continuation in the response"));
    }
    {   // Home's Podcasts chip: a feed of episodes, a genre a shelf
        JsonDoc d;
        BrowsePage p;
        CHECK(load("browse_podcasts.json", d) && parse_browse(d.root(), "FEmusic_home", p, err));
        CHECK_EQ(p.kind, std::string("feed"));
        CHECK_EQ(p.sections.size(), size_t(3));
        const char *genre[] = {"Films", "Lifestyle", "History"};
        for (size_t i = 0; i < p.sections.size() && i < 3; i++) {
            CHECK_EQ(p.sections[i].title, std::string(genre[i]));
            CHECK_EQ(p.sections[i].items.size(), size_t(10));
            for (const Item &it : p.sections[i].items) {
                CHECK_EQ(it.kind, std::string("episode"));
                CHECK(it.id.size() == 11 && !it.title.empty() && !it.thumb.empty());
                CHECK(it.artist.size() > 0 && it.album_id.rfind("MPSPPL", 0) == 0);
                CHECK(it.artist_id.empty() && it.album.empty());
                CHECK(it.duration_s > 0 && !it.plays.empty() && it.subtitle.find(it.plays) == 0);
            }
        }
        const Item &e = p.sections[0].items.at(0);
        CHECK_EQ(e.id, std::string("XUh-fUA_o30"));
        CHECK_EQ(e.title, std::string("Epsiodio 399 - El doctor me sac\xC3\xB3 el jueguito de princesa"));
        CHECK_EQ(e.artist, std::string("La Cotorrisa - Episodios"));
        CHECK_EQ(e.album_id, std::string("MPSPPLIlZe8cHMkHClGxhYOFqA1E38BJe7VHXq"));
        CHECK_EQ(e.subtitle, std::string("1.2M views \xE2\x80\xA2 3d ago"));
        CHECK_EQ(e.plays, std::string("1.2M views"));
        CHECK_EQ(e.duration_s, 7200);
        CHECK(e.thumb.find("/vi/XUh-fUA_o30/hqdefault.jpg") != std::string::npos);
        CHECK_EQ(p.sections[0].items.at(1).duration_s, 16 * 60);
        CHECK(!p.continuation.empty());
    }
    {   // A podcast show: the episodes of an album-like page, and the next ones
        JsonDoc d;
        BrowsePage p;
        const std::string id = "MPSPPLAU7ZtBFN5JI";
        CHECK(load("browse_show.json", d) && parse_browse(d.root(), id, p, err));
        CHECK_EQ(p.kind, std::string("show"));
        CHECK_EQ(p.id, id);
        CHECK_EQ(p.title, std::string("Cele mai frumoase Pove\xC8\x99ti Rom\xC3\xA2ne\xC8\x99ti \xC8\x99i Basme pentru Copii | Podcast Audio TriluLino \xF0\x9F\x8E\xA7\xE2\x9C\xA8"));
        CHECK_EQ(p.artist, std::string("TriluLino"));
        CHECK_EQ(p.artist_id, std::string("UCp237FySYs0e9Ju_KnXM5xQ"));
        CHECK(!p.thumb.empty() && !p.description.empty());
        CHECK_EQ(p.items.size(), size_t(20));
        CHECK(!p.continuation.empty());
        for (const Item &it : p.items) {
            CHECK_EQ(it.kind, std::string("episode"));
            CHECK_EQ(it.artist, p.title);   // the rows name no show: the page's name and id
            CHECK_EQ(it.album_id, id);
            CHECK(!it.title.empty() && !it.thumb.empty() && it.duration_s > 0 && !it.plays.empty());
        }
        const Item &e = p.items.at(0);
        CHECK_EQ(e.id, std::string("hhBFrTXGlTw"));
        CHECK_EQ(e.plays, std::string("38K views"));
        CHECK_EQ(e.subtitle, std::string("38K views \xE2\x80\xA2 6d ago"));
        CHECK_EQ(e.duration_s, 14 * 60 + 53);
        CHECK_EQ(p.items.at(2).duration_s, 16 * 60);

        JsonDoc m;
        BrowsePage more;
        CHECK(load("browse_show_more.json", m) && parse_browse_more(m.root(), more, err));
        CHECK_EQ(more.items.size(), size_t(24));
        CHECK_EQ(more.items.at(0).kind, std::string("episode"));
        CHECK_EQ(more.items.back().id, std::string("1omHNJW7_Cw"));
        CHECK_EQ(more.items.back().duration_s, 5 * 60 + 9);
        CHECK(more.items[0].artist.empty() && more.items[0].album_id.empty());   // the UI's page names them
        CHECK_EQ(more.continuation, std::string(""));
        CHECK(more.sections.empty());
    }
    CHECK_EQ(browse_id("MPSPPLAU7ZtBFN5JI"), "MPSPPLAU7ZtBFN5JI");
    CHECK_EQ(browse_id("MPREb_W0LXV3Jg2OV"), "MPREb_W0LXV3Jg2OV");
    CHECK_EQ(browse_id("VLPL123"), "VLPL123");
    CHECK_EQ(browse_id("UCvMA46Xu1xApqZyXFIrnukw"), "UCvMA46Xu1xApqZyXFIrnukw");
    CHECK_EQ(browse_id("FEmusic_home"), "FEmusic_home");
    CHECK_EQ(browse_id("PL123"), "VLPL123");
    CHECK_EQ(browse_id("OLAK5uy_x"), "VLOLAK5uy_x");
    return check_done();
}
