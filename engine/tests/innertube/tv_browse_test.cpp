// Parses the recorded TV-client library and playlist pages of tests/fixtures/innertube/tv.
#include <fstream>
#include <sstream>

#include "check.h"
#include "innertube/tv_browse.h"

using namespace bbm;

namespace {
bool load(const char *name, JsonDoc &doc) {
    std::ifstream f(std::string(BBM_FIXTURES) + "/innertube/tv/" + name, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    std::string err;
    return JsonDoc::parse(ss.str(), doc, err);
}

bool page(const char *name, TvPage &p) {
    static JsonDoc d;   // the view must outlive the call only
    return load(name, d) && parse_tv_page(d.root(), p);
}

// A tile from inline JSON.
bool tile(const std::string &json, Item &out) {
    JsonDoc d;
    std::string err;
    return JsonDoc::parse(json, d, err) && parse_tile(d.root(), out);
}

void check_tabs(const TvPage &p, size_t selected) {
    static const char *ids[] = {"FEmusic_last_played", "FEmusic_liked_playlists", "FEmusic_liked_albums",
                                "FEmusic_liked_videos", "FEmusic_library_corpus_artists"};
    static const char *titles[] = {"Recent activity", "Playlists", "Albums", "Liked songs", "Subscriptions"};
    CHECK_EQ(p.tabs.size(), size_t(5));
    for (size_t i = 0; i < p.tabs.size() && i < 5; ++i) {
        CHECK_EQ(p.tabs[i].id, std::string(ids[i]));
        CHECK_EQ(p.tabs[i].title, std::string(titles[i]));
        CHECK_EQ(p.tabs[i].selected, i == selected);
    }
}
}  // namespace

int main() {
    {   // Recent activity
        TvPage p;
        CHECK(page("tv_library_recent.json", p));
        check_tabs(p, 0);
        CHECK(p.title.empty() && p.subtitle.empty());
        CHECK(p.continuation.empty());   // only reload continuations
        CHECK_EQ(p.items.size(), size_t(4));
        const Item &a = p.items.at(0);
        CHECK_EQ(a.kind, std::string("episode"));
        CHECK_EQ(a.id, std::string("vid00002AAA"));
        CHECK_EQ(a.playlist_id, std::string("RD0003AAAAAAAAAAA"));
        CHECK_EQ(a.title, std::string("Text 1"));
        CHECK_EQ(a.subtitle, std::string("Text 2"));
        CHECK_EQ(a.artist, std::string("Text 2"));
        CHECK_EQ(a.duration_s, 6649);   // 1:50:49
        CHECK_EQ(a.thumb, std::string("https://i.ytimg.com/fake/1/img.jpg"));
        CHECK_EQ(p.items.at(1).id, std::string("vid00005AAA"));
        CHECK_EQ(p.items.at(1).duration_s, 4355);
        const Item &v = p.items.at(2);
        CHECK_EQ(v.kind, std::string("video"));
        CHECK_EQ(v.id, std::string("vid00007AAA"));
        CHECK_EQ(v.title, std::string("Text 5"));
        CHECK_EQ(v.subtitle, std::string("Text 6 Text 7"));
        CHECK_EQ(v.artist, std::string("Text 6"));
        CHECK_EQ(v.duration_s, 524);
        const Item &l = p.items.at(3);
        CHECK_EQ(l.kind, std::string("playlist"));
        CHECK_EQ(l.id, std::string("VLOL0010AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"));
        CHECK_EQ(l.playlist_id, std::string("OL0010AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"));
        CHECK_EQ(l.title, std::string("Text 8"));
        CHECK_EQ(l.subtitle, std::string("Text 9 Text 10"));
        CHECK_EQ(l.duration_s, 0);
        CHECK_EQ(l.thumb, std::string("https://yt3.googleusercontent.com/fake/9/img.jpg"));
    }
    {   // Playlists tab selected
        TvPage p;
        CHECK(page("tv_library_playlists.json", p));
        check_tabs(p, 1);
        CHECK_EQ(p.items.size(), size_t(1));
        CHECK_EQ(p.items.at(0).kind, std::string("playlist"));
        CHECK_EQ(p.items.at(0).id, std::string("VLRD0011"));
        CHECK_EQ(p.items.at(0).playlist_id, std::string("RD0011"));
        CHECK_EQ(p.items.at(0).title, std::string("Text 12"));
        CHECK_EQ(p.items.at(0).subtitle, std::string("Text 13"));
        CHECK_EQ(p.items.at(0).thumb, std::string("https://www.gstatic.com/fake/11/img.jpg"));
    }
    {   // An empty tab
        TvPage p;
        CHECK(page("tv_library_empty.json", p));
        check_tabs(p, 2);
        CHECK(p.items.empty() && p.continuation.empty());
    }
    {   // A library continuation: tiles only
        TvPage p;
        CHECK(page("tv_library_more.json", p));
        CHECK(p.tabs.empty() && p.title.empty() && p.continuation.empty());
        CHECK_EQ(p.items.size(), size_t(4));
        CHECK_EQ(p.items.at(0).id, std::string("vid00002AAA"));
        CHECK_EQ(p.items.at(3).kind, std::string("playlist"));
    }
    {   // A playlist
        TvPage p;
        CHECK(page("tv_playlist.json", p));
        CHECK_EQ(p.title, std::string("Text 15"));
        CHECK_EQ(p.subtitle, std::string("Text 16 \xE2\x80\xA2 Text 17Text 18 \xE2\x80\xA2 Text 19"));
        CHECK(p.tabs.empty());
        CHECK_EQ(p.continuation, std::string("CONTINUATION_6"));
        CHECK_EQ(p.items.size(), size_t(3));
        const Item &a = p.items.at(0);
        CHECK_EQ(a.kind, std::string("video"));
        CHECK_EQ(a.id, std::string("vid00012AAA"));
        CHECK_EQ(a.playlist_id, std::string("PL0013AAAAAAAAAAAAAAAAAAAAAAAAAAAA"));
        CHECK_EQ(a.title, std::string("Text 20"));
        CHECK_EQ(a.artist, std::string("Text 16"));
        CHECK_EQ(a.subtitle, std::string("Text 16 Text 22 \xE2\x80\xA2 Text 23"));
        CHECK_EQ(a.duration_s, 5621);
        CHECK_EQ(a.thumb, std::string("https://i.ytimg.com/fake/13/img.jpg"));
        CHECK_EQ(p.items.at(1).id, std::string("vid00014AAA"));
        CHECK_EQ(p.items.at(1).duration_s, 7700);
        CHECK_EQ(p.items.at(2).id, std::string("vid00015AAA"));
        CHECK_EQ(p.items.at(2).duration_s, 5086);
    }
    {   // A playlist's continuation
        TvPage p;
        CHECK(page("tv_playlist_more.json", p));
        CHECK(p.title.empty() && p.subtitle.empty() && p.tabs.empty());
        CHECK_EQ(p.continuation, std::string("CONTINUATION_7"));
        CHECK_EQ(p.items.size(), size_t(3));
        CHECK_EQ(p.items.at(0).id, std::string("vid00016AAA"));
        CHECK_EQ(p.items.at(0).title, std::string("Text 30"));
        CHECK_EQ(p.items.at(0).duration_s, 5000);
        CHECK_EQ(p.items.at(2).id, std::string("vid00018AAA"));
        CHECK_EQ(p.items.at(2).duration_s, 5896);
    }
    {   // www.youtube.com's library: Shorts, feed links and untyped tiles are dropped
        TvPage p;
        CHECK(page("tv_www_library.json", p));
        CHECK_EQ(p.items.size(), size_t(10));
        size_t videos = 0, playlists = 0;
        for (const Item &i : p.items) {
            CHECK(i.kind == "video" || i.kind == "playlist");
            CHECK(i.id.rfind("FE", 0) != 0);
            (i.kind == "video" ? videos : playlists)++;
        }
        CHECK_EQ(videos, size_t(7));
        CHECK_EQ(playlists, size_t(3));
        CHECK_EQ(p.items.at(0).id, std::string("vid00019AAA"));
        CHECK_EQ(p.items.at(0).duration_s, 128);
        CHECK_EQ(p.items.at(0).subtitle, std::string("Text 42 Text 43 \xE2\x80\xA2 Text 44"));
        CHECK_EQ(p.items.at(3).id, std::string("vid00025AAA"));
        CHECK_EQ(p.items.at(3).playlist_id, std::string("WL"));
        CHECK_EQ(p.items.at(4).id, std::string("VLPL0013AAAAAAAAAAAAAAAAAAAAAAAAAAAA"));
        CHECK_EQ(p.items.at(5).id, std::string("VLLL"));
        CHECK_EQ(p.items.at(6).id, std::string("VLPL0028AAAAAAAAAAAA"));
        CHECK_EQ(p.items.at(9).id, std::string("vid00031AAA"));
    }
    {   // Inline shapes
        Item i;
        CHECK(tile(R"({"contentType":"TILE_CONTENT_TYPE_PLAYLIST","contentId":"OLAK5uy_x",
            "onSelectCommand":{"browseEndpoint":{"browseId":"MPREb_abc"}},
            "metadata":{"tileMetadataRenderer":{"title":{"simpleText":"Alb"}}}})", i));
        CHECK_EQ(i.kind, std::string("album"));
        CHECK_EQ(i.id, std::string("MPREb_abc"));
        CHECK_EQ(i.title, std::string("Alb"));
        Item i2;   // an album whatever its contentType
        CHECK(tile(R"({"onSelectCommand":{"browseEndpoint":{"browseId":"MPREb_z"}}})", i2));
        CHECK_EQ(i2.kind, std::string("album"));

        Item c;
        CHECK(tile(R"({"contentType":"TILE_CONTENT_TYPE_CHANNEL",
            "onSelectCommand":{"browseEndpoint":{"browseId":"UCabc"}},
            "metadata":{"tileMetadataRenderer":{"title":{"runs":[{"text":"Chan"}]}}}})", c));
        CHECK_EQ(c.kind, std::string("artist"));
        CHECK_EQ(c.id, std::string("UCabc"));
        CHECK_EQ(c.title, std::string("Chan"));

        Item s;
        CHECK(tile(R"({"contentType":"TILE_CONTENT_TYPE_VIDEO",
            "onSelectCommand":{"watchEndpoint":{"videoId":"v1","watchEndpointMusicSupportedConfigs":
              {"watchEndpointMusicConfig":{"musicVideoType":"MUSIC_VIDEO_TYPE_ATV"}}}}})", s));
        CHECK_EQ(s.kind, std::string("song"));
        CHECK_EQ(s.id, std::string("v1"));

        Item live;
        CHECK(tile(R"({"contentType":"TILE_CONTENT_TYPE_VIDEO",
            "header":{"tileHeaderRenderer":{"thumbnailOverlays":[
              {"thumbnailOverlayTimeStatusRenderer":{"text":{"simpleText":"LIVE"}}}]}},
            "onSelectCommand":{"watchEndpoint":{"videoId":"v2"}}})", live));
        CHECK_EQ(live.kind, std::string("video"));
        CHECK_EQ(live.duration_s, 0);

        Item badge;   // a badge has no text: skipped
        CHECK(tile(R"({"contentType":"TILE_CONTENT_TYPE_VIDEO",
            "metadata":{"tileMetadataRenderer":{"lines":[{"lineRenderer":{"items":[
              {"lineItemRenderer":{"badge":{}}},{"lineItemRenderer":{"text":{"simpleText":"A"}}}]}},
              {"lineRenderer":{"items":[{"lineItemRenderer":{"text":{"runs":[{"text":"B"},{"text":"C"}]}}}]}}]}},
            "onSelectCommand":{"watchEndpoint":{"videoId":"v3"}}})", badge));
        CHECK_EQ(badge.subtitle, std::string("A BC"));

        Item no;   // rejected: leaves `out` untouched
        no.id = "keep";
        CHECK(!tile(R"({"contentType":"TILE_CONTENT_TYPE_VIDEO","onSelectCommand":{"reelWatchEndpoint":{"videoId":"s"}}})", no));
        CHECK(!tile(R"({"contentType":"TILE_CONTENT_TYPE_CHANNEL","onSelectCommand":{"browseEndpoint":{"browseId":"FEhistory"}}})", no));
        CHECK(!tile(R"({"onSelectCommand":{"watchEndpoint":{"videoId":"v"}}})", no));
        CHECK(!tile(R"({"contentType":"TILE_CONTENT_TYPE_VIDEO","onSelectCommand":{"watchEndpoint":{}}})", no));
        CHECK_EQ(no.id, std::string("keep"));
    }
    {   // Neither contents nor continuationContents; and garbage
        for (const char *j : {R"({})", R"({"contents":"x"})", R"({"contents":[1,2],"continuationContents":5})",
                              R"("str")", R"(null)", R"([])"}) {
            JsonDoc d;
            std::string err;
            TvPage p;
            CHECK(JsonDoc::parse(j, d, err));
            CHECK(!parse_tv_page(d.root(), p));
        }
        Item i;
        CHECK(!tile(R"("x")", i));
        CHECK(!tile(R"({"contentType":5,"onSelectCommand":"x","metadata":[],"header":"h"})", i));
        TvPage p;
        JsonDoc d;
        std::string err;
        CHECK(JsonDoc::parse(R"({"contents":{"tvBrowseRenderer":"x","a":[{"tileRenderer":"s"},7,null]}})", d, err));
        CHECK(parse_tv_page(d.root(), p));
        CHECK(p.items.empty() && p.tabs.empty());
        JsonDoc d2;
        CHECK(JsonDoc::parse(R"({"continuationContents":{"x":{"tileRenderer":{"contentType":"TILE_CONTENT_TYPE_VIDEO",
            "onSelectCommand":{"watchEndpoint":{"videoId":"q"}}}},"nextContinuationData":{"continuation":"C9"}}})", d2, err));
        CHECK(parse_tv_page(d2.root(), p));
        CHECK_EQ(p.items.size(), size_t(1));
        CHECK_EQ(p.continuation, std::string("C9"));
    }
    return check_done();
}
