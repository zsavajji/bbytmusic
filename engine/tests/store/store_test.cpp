#include <cstdlib>
#include <unistd.h>
#include <string>
#include <vector>

#include "check.h"
#include "store/history_store.h"
#include "store/queue_store.h"
#include "store/schema.h"
#include "store/settings.h"
#include "store/track_store.h"

using namespace bbm;

static TrackRow track(const std::string &id, const std::string &title) {
    return {id, title, "Artist " + id, "Album", "http://t/" + id, "UCa", "MPa", 200};
}

int main() {
    char tmpl[] = "/tmp/bbm_store_XXXXXX";
    const std::string dir = mkdtemp(tmpl);
    const std::string path = dir + "/music.db";
    std::string err;

    {  // fresh open reaches version 1
        Db db;
        CHECK(open_store(db, path, err));
        CHECK_EQ(db.user_version(), 1);
        CHECK(upsert_track(db, track("a", "Alpha"), err));
        CHECK(upsert_track(db, track("b", "Beta"), err));
        CHECK(upsert_track(db, track("a", "Alpha 2"), err));  // upsert: replaces
        CHECK(set_setting(db, "volume", "70", err));
    }
    {  // reopen keeps the data and the version
        Db db;
        CHECK(open_store(db, path, err));
        CHECK_EQ(db.user_version(), 1);
        auto a = get_track(db, "a", err);
        CHECK(a.has_value());
        CHECK_EQ(a->title, "Alpha 2");
        CHECK_EQ(a->duration_s, 200);
        CHECK_EQ(a->artist_id, "UCa");
        CHECK(!get_track(db, "zzz", err).has_value());
        CHECK_EQ(err, "");
        CHECK_EQ(get_setting(db, "volume", "50"), "70");
        CHECK_EQ(get_setting(db, "missing", "50"), "50");
        CHECK(set_setting(db, "volume", "30", err));
        CHECK_EQ(get_setting(db, "volume", "50"), "30");
    }
    {  // a database at version 0 with no tables upgrades
        Db raw;
        CHECK(raw.open(dir + "/old.db", err));
        CHECK_EQ(raw.user_version(), 0);
        raw.close();
        Db db;
        CHECK(open_store(db, dir + "/old.db", err));
        CHECK_EQ(db.user_version(), 1);
        CHECK(upsert_track(db, track("x", "X"), err));
    }
    {  // a database from the future is refused
        Db db;
        CHECK(db.open(dir + "/new.db", err));
        CHECK(db.exec("PRAGMA user_version=99", err));
        db.close();
        CHECK(!open_store(db, dir + "/new.db", err));
    }
    {  // a failing migration rolls back whole and leaves the version
        static const Migration steps[] = {{1, "CREATE TABLE a(x);"}, {2, "CREATE TABLE b(x); CREATE TABLE a(x);"}};
        Db db;
        CHECK(db.open(dir + "/mig.db", err));
        CHECK(!db.migrate(steps, 2, err));
        CHECK(err.find("migration 2") != std::string::npos);
        CHECK_EQ(db.user_version(), 1);
        CHECK(!db.exec("SELECT * FROM b", err));  // step 2's table is not there
    }
    {  // queue round trip: order, current index, position
        Db db;
        CHECK(open_store(db, path, err));
        CHECK(replace_queue(db, {"b", "a", "ghost"}, "radio:b", err));
        std::vector<QueueRow> q;
        CHECK(load_queue(db, q, err));
        CHECK_EQ(q.size(), size_t(3));
        CHECK_EQ(q[0].track_id, "b");
        CHECK(q[0].known);
        CHECK_EQ(q[0].track.title, "Beta");
        CHECK_EQ(q[1].track.title, "Alpha 2");
        CHECK_EQ(q[2].track_id, "ghost");
        CHECK(!q[2].known);
        CHECK_EQ(q[2].source, "radio:b");
        CHECK_EQ(q[2].position, 2);
        QueueState st;
        CHECK(load_queue_state(db, st, err));
        CHECK_EQ(st.current, -1);
        CHECK(save_queue_state(db, {1, 45123}, err));
        CHECK(save_queue_state(db, {2, 9000}, err));
        CHECK(load_queue_state(db, st, err));
        CHECK_EQ(st.current, 2);
        CHECK_EQ(st.position_ms, 9000);
        // atomic replace: a failure in the middle leaves the old queue
        CHECK(db.exec("CREATE TRIGGER boom BEFORE INSERT ON queue WHEN NEW.track_id='bad' "
                      "BEGIN SELECT RAISE(ABORT,'boom'); END;",
                      err));
        CHECK(!replace_queue(db, {"x", "bad", "y"}, "s", err));
        CHECK(err.find("boom") != std::string::npos);
        CHECK(load_queue(db, q, err));
        CHECK_EQ(q.size(), size_t(3));
        CHECK_EQ(q[0].track_id, "b");
        CHECK(replace_queue(db, {}, "", err));  // an empty list empties the queue
        CHECK(load_queue(db, q, err));
        CHECK_EQ(q.size(), size_t(0));
    }
    {  // history: newest first, limited
        Db db;
        CHECK(open_store(db, path, err));
        CHECK(add_history(db, "a", 100, 1000, err));
        CHECK(add_history(db, "b", 300, 2000, err));
        CHECK(add_history(db, "c", 200, 3000, err));
        CHECK(add_history(db, "d", 300, 4000, err));  // same second as b: later insert first
        std::vector<HistoryRow> h;
        CHECK(recent_history(db, 3, h, err));
        CHECK_EQ(h.size(), size_t(3));
        CHECK_EQ(h[0].track_id, "d");
        CHECK_EQ(h[1].track_id, "b");
        CHECK_EQ(h[2].track_id, "c");
        CHECK_EQ(h[2].ms_played, 3000);
        CHECK(recent_history(db, 50, h, err));
        CHECK_EQ(h.size(), size_t(4));
    }
    {  // the tracks played: each once, its latest time, newest first; unknown tracks left out
        Db db;
        CHECK(open_store(db, path, err));
        CHECK(upsert_track(db, track("c", "Gamma"), err));
        CHECK(add_history(db, "ghost", 900, 1, err));  // not in tracks
        std::vector<PlayedTrack> p;
        CHECK(recent_tracks(db, 10, p, err));
        // a: 100, b: 300, c: 200, d: 300 (d is not a track here), ghost: 900
        CHECK_EQ(p.size(), size_t(3));
        CHECK_EQ(p[0].track.id, "b");
        CHECK_EQ(p[0].played_at, 300);
        CHECK_EQ(p[0].track.title, "Beta");
        CHECK_EQ(p[1].track.id, "c");
        CHECK_EQ(p[1].played_at, 200);
        CHECK_EQ(p[2].track.id, "a");
        CHECK_EQ(p[2].played_at, 100);
        CHECK(add_history(db, "a", 250, 1, err));  // a again: once, at its latest
        CHECK(recent_tracks(db, 10, p, err));
        CHECK_EQ(p.size(), size_t(3));
        CHECK_EQ(p[1].track.id, "a");
        CHECK_EQ(p[1].played_at, 250);
        CHECK(recent_tracks(db, 2, p, err));
        CHECK_EQ(p.size(), size_t(2));
    }
    {  // WAL: a second connection reads while the first has a write open
        Db w, r;
        CHECK(open_store(w, path, err));
        CHECK(open_store(r, path, err));
        CHECK(w.exec("BEGIN IMMEDIATE", err));
        CHECK(w.exec("INSERT OR REPLACE INTO settings(key,value) VALUES('during','tx')", err));
        CHECK_EQ(get_setting(r, "volume", "?"), "30");  // reads the committed data, not blocked
        CHECK_EQ(get_setting(r, "during", "none"), "none");
        CHECK(w.exec("COMMIT", err));
        CHECK_EQ(get_setting(r, "during", "none"), "tx");
    }
    std::system(("rm -rf " + dir).c_str());
    return check_done();
}
