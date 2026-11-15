// Against music.youtube.com, anonymously. Run on request: ctest -L live.
#include <cstdio>

#include "check.h"
#include "innertube/browse.h"
#include "innertube/next.h"
#include "innertube/region.h"
#include "innertube/search.h"

using namespace bbm;

int main() {
    InnerTube yt;
    std::string err;
    const std::string q = "never gonna give you up";

    struct F { SearchFilter f; const char *name, *kind; };
    for (F x : {F{SearchFilter::Songs, "songs", "song"}, F{SearchFilter::Videos, "videos", "video"},
                F{SearchFilter::Albums, "albums", "album"}, F{SearchFilter::Artists, "artists", "artist"},
                F{SearchFilter::Playlists, "playlists", "playlist"}, F{SearchFilter::Podcasts, "podcasts", "show"}}) {
        SearchPage p;
        const bool ok = search(yt, q, x.f, p, err);
        if (!ok) std::fprintf(stderr, "%s: %s\n", x.name, err.c_str());
        CHECK(ok);
        size_t n = 0, wrong = 0;
        for (const auto &s : p.sections)
            for (const Item &it : s.items) {
                ++n;
                if (it.kind != x.kind) ++wrong;
            }
        std::printf("filter %-9s %zu items, %zu of another kind, continuation %s\n", x.name, n, wrong,
                    p.continuation.empty() ? "no" : "yes");
        CHECK(n > 0);
        CHECK_EQ(wrong, size_t(0));
    }
    CHECK(!yt.visitor_data().empty());

    SearchPage songs, more, all;
    CHECK(search(yt, q, SearchFilter::Songs, songs, err));
    CHECK(!songs.sections.empty() && !songs.sections[0].items.empty());
    if (!songs.sections.empty() && !songs.sections[0].items.empty()) {
        const Item &it = songs.sections[0].items[0];
        CHECK_EQ(it.title, std::string("Never Gonna Give You Up"));
        CHECK_EQ(it.artist, std::string("Rick Astley"));
        CHECK(it.duration_s >= 210 && it.duration_s <= 216);
        CHECK(!it.id.empty() && !it.thumb.empty());
    }
    CHECK(!songs.continuation.empty());
    const bool more_ok = search_more(yt, songs.continuation, more, err);
    if (!more_ok) std::fprintf(stderr, "search_more: %s\n", err.c_str());
    CHECK(more_ok);
    std::printf("continuation: %zu items\n", more.sections.empty() ? size_t(0) : more.sections[0].items.size());
    CHECK(!more.sections.empty() && more.sections[0].items.size() >= 10);

    CHECK(search(yt, q, SearchFilter::All, all, err));
    CHECK(!all.sections.empty());

    // A podcast query on All: the show first (an untitled shelf), its episodes grouped as kind episode.
    SearchPage hist;
    CHECK(search(yt, "joe rogan", SearchFilter::All, hist, err));
    size_t shows = 0, episodes = 0;
    for (const auto &s : hist.sections)
        for (const Item &it : s.items) {
            shows += it.kind == "show" && it.id.rfind("MPSP", 0) == 0;
            episodes += it.kind == "episode" && it.album_id.rfind("MPSP", 0) == 0;
        }
    std::printf("all, \"joe rogan\": %zu shows, %zu episodes\n", shows, episodes);
    CHECK(shows > 0 && episodes > 0);

    std::vector<std::string> sug;
    CHECK(suggestions(yt, "never gon", sug, err));
    std::printf("suggestions: %zu, first \"%s\"\n", sug.size(), sug.empty() ? "" : sug[0].c_str());
    CHECK(sug.size() >= 3);

    QueuePage qp, qp2;
    const bool qok = watch_queue(yt, "dQw4w9WgXcQ", "", qp, err);
    if (!qok) std::fprintf(stderr, "next: %s\n", err.c_str());
    CHECK(qok);
    std::printf("queue: %zu items, lyrics %s, related %s\n", qp.items.size(), qp.lyrics_browse_id.c_str(),
                qp.related_browse_id.c_str());
    CHECK(qp.items.size() > 10);
    CHECK(!qp.items.empty() && qp.items[0].id == "dQw4w9WgXcQ");
    CHECK(!qp.lyrics_browse_id.empty());
    CHECK(!qp.continuation.empty());
    CHECK(watch_queue_more(yt, qp.continuation, qp2, err));
    std::printf("queue continuation: %zu items\n", qp2.items.size());
    CHECK(qp2.items.size() > 10);

    // Home's Podcasts chip (FEmusic_home and its params), its next genres, and a show's page.
    BrowsePage pod, pod_more, show, show_more;
    const char *podcasts = "ggNCSgQIDBADSgQICRABSgQIBxABSgQICBABSgQIBBABSgQIDhABSgQIAxABSgQIDRABSgQIChABSgQIBhABSgQIBRAB";
    const bool pok = browse(yt, "FEmusic_home", pod, err, podcasts);
    if (!pok) std::fprintf(stderr, "browse podcasts: %s\n", err.c_str());
    CHECK(pok);
    CHECK(!pod.sections.empty() && !pod.sections[0].items.empty() && !pod.continuation.empty());
    if (pok && !pod.sections.empty() && !pod.sections[0].items.empty()) {
        const Item &ep = pod.sections[0].items[0];
        std::printf("podcasts: %zu shelves, first \"%s\" (%s) of \"%s\" %s, %d s\n", pod.sections.size(),
                    ep.title.c_str(), ep.id.c_str(), ep.artist.c_str(), ep.album_id.c_str(), ep.duration_s);
        CHECK_EQ(ep.kind, std::string("episode"));
        CHECK(browse_more(yt, pod.continuation, pod_more, err));
        CHECK(!pod_more.sections.empty() && !pod_more.sections[0].items.empty());
        if (!pod_more.sections.empty() && !pod_more.sections[0].items.empty())
            CHECK_EQ(pod_more.sections[0].items[0].kind, std::string("episode"));
        const bool sok = browse(yt, ep.album_id, show, err);
        if (!sok) std::fprintf(stderr, "browse show: %s\n", err.c_str());
        CHECK(sok);
        CHECK_EQ(show.kind, std::string("show"));
        CHECK(!show.title.empty() && show.items.size() >= 5);
        std::printf("show: \"%s\", %zu episodes, continuation %s\n", show.title.c_str(), show.items.size(),
                    show.continuation.empty() ? "no" : "yes");
        if (!show.continuation.empty()) {
            CHECK(browse_more(yt, show.continuation, show_more, err));
            CHECK(!show_more.items.empty());
            if (!show_more.items.empty()) CHECK_EQ(show_more.items[0].kind, std::string("episode"));
        }
    }

    std::string gl;
    const bool rok = fetch_region(yt, gl, err);
    if (!rok) std::fprintf(stderr, "region: %s\n", err.c_str());
    CHECK(rok);
    std::printf("region: %s\n", gl.c_str());
    CHECK(valid_region(gl));
    return check_done();
}
