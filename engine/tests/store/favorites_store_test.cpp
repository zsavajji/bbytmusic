#include <unistd.h>

#include <cstdlib>
#include <string>
#include <vector>

#include "check.h"
#include "store/favorites_store.h"
#include "store/schema.h"

using namespace bbm;

static TrackRow track(const std::string &id, const std::string &title) {
    return {id, title, "Artist " + id, "Album", "http://t/" + id, "UCa", "MPa", 200};
}

int main() {
    char tmpl[] = "/tmp/bbm_fav_XXXXXX";
    const std::string dir = mkdtemp(tmpl);
    std::string err;
    std::vector<FavoriteRow> rows;

    {
        Db db;
        CHECK(open_store(db, dir + "/music.db", err));
        CHECK(list_favorites(db, rows, err));
        CHECK(rows.empty());

        // newest first; the same second goes by rowid, newest first
        CHECK(add_favorite(db, track("a", "Alpha"), 100, err));
        CHECK(add_favorite(db, track("b", "Beta"), 300, err));
        CHECK(add_favorite(db, track("c", "Gamma"), 300, err));
        CHECK(list_favorites(db, rows, err));
        CHECK_EQ(rows.size(), size_t(3));
        CHECK_EQ(rows[0].track.id, "c");
        CHECK_EQ(rows[1].track.id, "b");
        CHECK_EQ(rows[2].track.id, "a");
        CHECK_EQ(rows[2].track.title, "Alpha");
        CHECK_EQ(rows[2].track.duration_s, 200);
        CHECK_EQ(rows[2].added_at, 100);
        CHECK(!rows[2].offline);

        // twice: the track is refreshed, added_at and offline stay
        CHECK(set_favorite_offline(db, "a", true, err));
        CHECK(add_favorite(db, track("a", "Alpha 2"), 999, err));
        CHECK(list_favorites(db, rows, err));
        CHECK_EQ(rows.size(), size_t(3));
        CHECK_EQ(rows[2].track.title, "Alpha 2");
        CHECK_EQ(rows[2].added_at, 100);
        CHECK(rows[2].offline);
        CHECK(set_favorite_offline(db, "a", false, err));
        CHECK(list_favorites(db, rows, err));
        CHECK(!rows[2].offline);

        // remove; removing what is not there is fine too
        CHECK(remove_favorite(db, "b", err));
        CHECK(remove_favorite(db, "nobody", err));
        CHECK(list_favorites(db, rows, err));
        CHECK_EQ(rows.size(), size_t(2));
        CHECK_EQ(rows[0].track.id, "c");
        CHECK_EQ(rows[1].track.id, "a");
    }
    {  // a favourite whose track row is missing is left out
        Db db;
        CHECK(open_store(db, dir + "/music.db", err));
        CHECK(db.exec("DELETE FROM tracks WHERE id='c'", err));
        CHECK(list_favorites(db, rows, err));
        CHECK_EQ(rows.size(), size_t(1));
    }
    {  // a database at version 1 migrates to 2 with its rows intact
        Db raw;
        CHECK(raw.open(dir + "/v1.db", err));
        CHECK(raw.exec("CREATE TABLE tracks(id TEXT PRIMARY KEY, title TEXT, artist TEXT, album TEXT, thumb TEXT,"
                       " duration_s INTEGER, artist_id TEXT, album_id TEXT);"
                       "INSERT INTO tracks(id,title) VALUES('old','Old song');"
                       "PRAGMA user_version=1",
                       err));
        CHECK_EQ(raw.user_version(), 1);
        raw.close();
        Db db;
        CHECK(open_store(db, dir + "/v1.db", err));
        CHECK_EQ(db.user_version(), 2);
        auto t = get_track(db, "old", err);
        CHECK(t.has_value());
        CHECK_EQ(t->title, "Old song");
        CHECK(add_favorite(db, *t, 5, err));
        CHECK(list_favorites(db, rows, err));
        CHECK_EQ(rows.size(), size_t(1));
        CHECK_EQ(rows[0].track.id, "old");
    }
    return check_done();
}
