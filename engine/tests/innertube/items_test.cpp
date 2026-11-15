// Parses the recorded responses of tests/fixtures/innertube (see its README).
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "check.h"
#include "innertube/next.h"
#include "innertube/search.h"

using namespace bbm;

namespace {
bool load(const char *name, JsonDoc &doc) {
    std::ifstream f(std::string(BBM_FIXTURES) + "/innertube/" + name, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    std::string err;
    if (!JsonDoc::parse(ss.str(), doc, err)) {
        std::fprintf(stderr, "cannot load %s: %s\n", name, err.c_str());
        return false;
    }
    return true;
}

size_t count_items(const SearchPage &p) {
    size_t n = 0;
    for (const auto &s : p.sections) n += s.items.size();
    return n;
}

void every_item_is_whole(const SearchPage &p) {
    for (const auto &s : p.sections)
        for (const Item &it : s.items) {
            CHECK(!it.kind.empty());
            CHECK(!it.id.empty());
            CHECK(!it.title.empty());
            JsonDoc back;
            std::string err;
            CHECK(JsonDoc::parse(to_json(it), back, err));
            CHECK_EQ(back.root()["id"].string(), it.id);
            CHECK_EQ(back.root()["title"].string(), it.title);
            CHECK_EQ(back.root()["duration_s"].i64(), it.duration_s);
        }
}
}  // namespace

int main() {
    std::string err;
    CHECK_EQ(parse_duration("3:34"), 214);
    CHECK_EQ(parse_duration("1:02:03"), 3723);
    CHECK_EQ(parse_duration("1.8B views"), 0);
    CHECK_EQ(parse_duration("2026"), 0);

    {   // Songs filter
        JsonDoc d;
        SearchPage p;
        CHECK(load("search_songs.json", d) && parse_search(d.root(), p, err));
        CHECK(!p.continuation.empty());
        CHECK(count_items(p) >= 15);
        every_item_is_whole(p);
        const Item &first = p.sections.at(0).items.at(0);
        CHECK_EQ(p.sections[0].title, std::string("Songs"));
        CHECK_EQ(first.kind, std::string("song"));
        CHECK_EQ(first.title, std::string("Never Gonna Give You Up"));
        CHECK_EQ(first.artist, std::string("Rick Astley"));
        CHECK_EQ(first.artist_id, std::string("UCwZEU0wAwIyZb4x5G_KJp2w"));
        CHECK_EQ(first.album, std::string("Whenever You Need Somebody"));
        CHECK(first.album_id.rfind("MPREb_", 0) == 0);
        CHECK(first.duration_s >= 210 && first.duration_s <= 216);
        CHECK(first.thumb.rfind("https://", 0) == 0);
        CHECK(first.thumb.find("w120") != std::string::npos);   // the 120 px one, not wider than 544
        CHECK_EQ(first.subtitle, std::string("Rick Astley \xE2\x80\xA2 Whenever You Need Somebody"));
        for (const Item &it : p.sections[0].items) CHECK_EQ(it.kind, std::string("song"));
    }
    {   // No filter: the flat layout, with a top result
        JsonDoc d;
        SearchPage p;
        CHECK(load("search_all.json", d) && parse_search(d.root(), p, err));
        CHECK(count_items(p) >= 20);
        every_item_is_whole(p);
        std::string titles;
        for (const auto &s : p.sections) titles += s.title + "|";
        CHECK_EQ(titles, std::string("Top result|Songs|Albums|Artists|Videos|Playlists|Podcasts|Episodes|"));
        CHECK_EQ(p.sections.at(0).title, std::string("Top result"));
        CHECK_EQ(p.sections[0].items.size(), size_t(1));
        for (const auto &s : p.sections) {
            const std::string want = s.title == "Songs" ? "song" : s.title == "Albums" ? "album" : "";
            if (!want.empty())
                for (const Item &it : s.items) CHECK_EQ(it.kind, want);
        }
        bool plays = false;
        for (const Item &it : p.sections.at(1).items)
            if (it.title == "Never Gonna Give You Up" && it.artist == "Rick Astley" && it.plays == "2B plays") plays = true;
        CHECK(plays);
        const Item &top = p.sections[0].items.at(0);
        CHECK_EQ(top.id, std::string("dQw4w9WgXcQ"));
        CHECK_EQ(top.kind, std::string("video"));
        CHECK_EQ(top.artist, std::string("Rick Astley"));
        CHECK_EQ(top.duration_s, 214);
        bool song = false, artist = false, album = false, playlist = false;
        for (const auto &s : p.sections)
            for (const Item &it : s.items) {
                song |= it.kind == "song" && !it.artist.empty();
                artist |= it.kind == "artist" && it.id.rfind("UC", 0) == 0;
                album |= it.kind == "album" && it.id.rfind("MPREb_", 0) == 0;
                playlist |= it.kind == "playlist" && it.playlist_id.rfind("VL", 0) != 0;
            }
        CHECK(song && artist && album && playlist);
    }
    {   // Albums filter: the year, from the subtitle
        JsonDoc d;
        SearchPage p;
        CHECK(load("search_albums.json", d) && parse_search(d.root(), p, err));
        const Item &a = p.sections.at(0).items.at(0);
        CHECK_EQ(a.kind, std::string("album"));
        CHECK_EQ(a.title, std::string("In the Aeroplane Over the Sea"));
        CHECK_EQ(a.year, std::string("1998"));
        CHECK_EQ(a.subtitle, std::string("Album \xE2\x80\xA2 Neutral Milk Hotel \xE2\x80\xA2 1998"));
        CHECK_EQ(a.id, std::string("MPREb_W0LXV3Jg2OV"));
    }
    {   // Podcasts filter: a shelf of shows (the publisher is the artist)
        JsonDoc d;
        SearchPage p;
        CHECK(load("search_podcasts.json", d) && parse_search(d.root(), p, err));
        CHECK_EQ(p.sections.size(), size_t(1));
        CHECK_EQ(p.sections[0].title, std::string("Podcasts"));
        CHECK(p.sections[0].items.size() >= 15);
        CHECK(!p.continuation.empty());
        every_item_is_whole(p);
        for (const Item &it : p.sections[0].items) {
            CHECK_EQ(it.kind, std::string("show"));
            CHECK(it.id.rfind("MPSP", 0) == 0);
            CHECK(!it.artist.empty() && it.thumb.rfind("https://", 0) == 0);
        }
        const Item &s = p.sections[0].items.at(1);
        CHECK_EQ(s.title, std::string("The Restt Is History"));
        CHECK_EQ(s.artist, std::string("Cave Production"));
        CHECK_EQ(s.id, std::string("MPSPPLhGgTInWib7OR3rJ1EGKgIfKy_wi6wGpM"));
    }
    {   // No filter, a podcast query: the show on top (a shelf without a title), then the kinds, ending in
        // Podcasts and Episodes (rows with a watchEndpoint of type PODCAST_EPISODE)
        JsonDoc d;
        SearchPage p;
        CHECK(load("search_all_podcast.json", d) && parse_search(d.root(), p, err));
        every_item_is_whole(p);
        std::string titles;
        for (const auto &s : p.sections) titles += s.title + "|";
        CHECK_EQ(titles, std::string("Top result|Songs|Albums|Artists|Videos|Playlists|Podcasts|Episodes|"));
        CHECK_EQ(p.sections[0].items.size(), size_t(1));
        CHECK_EQ(p.sections[0].items[0].kind, std::string("show"));
        CHECK_EQ(p.sections[0].items[0].id, std::string("MPSPPLEbAHi3fZpuEyBOPtr158TY-FW7P1l4Fg"));
        CHECK_EQ(p.sections[0].items[0].artist, std::string("The Rest Is History"));
        const auto &shows = p.sections.at(6), &episodes = p.sections.at(7);
        CHECK(shows.items.size() >= 3);
        for (const Item &it : shows.items) CHECK_EQ(it.kind, std::string("show"));
        CHECK_EQ(episodes.items.size(), size_t(3));
        const Item &e = episodes.items[0];
        CHECK_EQ(e.kind, std::string("episode"));
        CHECK_EQ(e.id, std::string("2dcT4tqPL_s"));
        CHECK_EQ(e.title, std::string("Augustus: Visionary Statesman or Destroyer Of The Republic?"));
        CHECK_EQ(e.artist, std::string("The Rest Is History"));      // the show
        CHECK_EQ(e.album_id, std::string("MPSPPLEbAHi3fZpuEyBOPtr158TY-FW7P1l4Fg"));
        CHECK(e.thumb.rfind("https://", 0) == 0);
        for (const Item &it : episodes.items) CHECK_EQ(it.kind, std::string("episode"));
    }
    {   // An episode as the top result: a card whose title links to the episode's page
        JsonDoc d;
        SearchPage p;
        CHECK(load("search_all_episode.json", d) && parse_search(d.root(), p, err));
        every_item_is_whole(p);
        CHECK_EQ(p.sections.at(0).title, std::string("Top result"));
        const Item &top = p.sections[0].items.at(0);
        CHECK_EQ(top.kind, std::string("episode"));
        CHECK_EQ(top.id, std::string("MlPCmUiJd1Q"));
        CHECK_EQ(top.title, std::string("Why does America have so many SERIAL KILLERS?"));
        CHECK_EQ(top.artist, std::string("Where Did All The Serial Killers Go?"));
        CHECK_EQ(top.album_id, std::string("MPSPPL1l34hkJI7_wejxtEhwhBtvAsdlYItEwj"));
    }
    {   // A continuation page
        JsonDoc d;
        SearchPage p;
        CHECK(load("search_more.json", d) && parse_search(d.root(), p, err));
        CHECK_EQ(p.sections.size(), size_t(1));
        CHECK(p.sections[0].items.size() >= 15);
        CHECK(!p.continuation.empty());
        every_item_is_whole(p);
    }
    {   // The explicit badge
        JsonDoc d;
        SearchPage p;
        CHECK(load("search_explicit.json", d) && parse_search(d.root(), p, err));
        bool explicit_seen = false;
        for (const Item &it : p.sections.at(0).items) explicit_seen |= it.is_explicit;
        CHECK(explicit_seen);
        CHECK(!p.sections[0].items[0].is_explicit || p.sections[0].items[0].kind == "song");
    }
    {   // Suggestions
        JsonDoc d;
        std::vector<std::string> s;
        CHECK(load("suggestions.json", d) && parse_suggestions(d.root(), s));
        CHECK(s.size() >= 4);
        CHECK_EQ(s.at(0), std::string("never gonna give you up"));
        for (const auto &x : s) CHECK(x.rfind("never gon", 0) == 0);
    }
    {   // The radio of a song
        JsonDoc d;
        QueuePage q;
        CHECK(load("next_radio.json", d) && parse_queue(d.root(), q, err));
        CHECK(q.items.size() > 10);
        CHECK_EQ(q.items.at(0).id, std::string("dQw4w9WgXcQ"));
        CHECK_EQ(q.items[0].title, std::string("Never Gonna Give You Up"));
        CHECK_EQ(q.items[0].artist, std::string("Rick Astley"));
        CHECK_EQ(q.items[0].duration_s, 214);
        CHECK(!q.items[0].thumb.empty());
        CHECK(!q.continuation.empty());
        CHECK(q.lyrics_browse_id.rfind("MPLYt_", 0) == 0);
        CHECK(q.related_browse_id.rfind("MPTRt_", 0) == 0);
        for (const Item &it : q.items) {
            CHECK(!it.id.empty());
            CHECK(!it.kind.empty());
            CHECK(it.duration_s > 0);
        }
        JsonDoc d2;
        QueuePage q2;
        CHECK(load("next_more.json", d2) && parse_queue(d2.root(), q2, err));
        CHECK(q2.items.size() > 10);
        CHECK(!q2.continuation.empty());
    }
    return check_done();
}
