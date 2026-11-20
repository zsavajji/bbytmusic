#include "store/favorites_store.h"

namespace bbm {

bool add_favorite(Db &db, const TrackRow &t, int64_t added_at, std::string &err) {
    std::lock_guard<std::recursive_mutex> lk(db.mu());
    if (!upsert_track(db, t, err)) return false;
    Stmt s;
    // INSERT OR IGNORE: the phone's SQLite (3.7.14) has no upsert; a row already there stays as it is.
    if (!s.prepare(db, "INSERT OR IGNORE INTO favorites(track_id,added_at,offline) VALUES(?,?,0)", err)) return false;
    s.bind(1, t.id);
    s.bind(2, added_at);
    if (s.step() != Step::Done) {
        err = s.error();
        return false;
    }
    return true;
}

bool remove_favorite(Db &db, const std::string &track_id, std::string &err) {
    std::lock_guard<std::recursive_mutex> lk(db.mu());
    Stmt s;
    if (!s.prepare(db, "DELETE FROM favorites WHERE track_id=?", err)) return false;
    s.bind(1, track_id);
    if (s.step() != Step::Done) {
        err = s.error();
        return false;
    }
    return true;
}

bool list_favorites(Db &db, std::vector<FavoriteRow> &out, std::string &err) {
    std::lock_guard<std::recursive_mutex> lk(db.mu());
    out.clear();
    Stmt s;
    if (!s.prepare(db,
                   (std::string("SELECT ") + kTrackColumns +
                    ",f.added_at,f.offline FROM favorites f JOIN tracks t ON t.id=f.track_id "
                    "ORDER BY f.added_at DESC,f.rowid DESC").c_str(),
                   err))
        return false;
    Step r;
    while ((r = s.step()) == Step::Row) out.push_back({read_track(s, 0), s.i64(8), s.i64(9) != 0});
    if (r == Step::Error) {
        err = s.error();
        return false;
    }
    return true;
}

bool set_favorite_offline(Db &db, const std::string &track_id, bool offline, std::string &err) {
    std::lock_guard<std::recursive_mutex> lk(db.mu());
    Stmt s;
    if (!s.prepare(db, "UPDATE favorites SET offline=? WHERE track_id=?", err)) return false;
    s.bind(1, int64_t(offline ? 1 : 0));
    s.bind(2, track_id);
    if (s.step() != Step::Done) {
        err = s.error();
        return false;
    }
    return true;
}

}  // namespace bbm
