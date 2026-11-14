#include "store/track_store.h"

namespace bbm {

bool upsert_track(Db &db, const TrackRow &t, std::string &err) {
    std::lock_guard<std::recursive_mutex> lk(db.mu());
    Stmt s;
    if (!s.prepare(db,
                   // INSERT OR REPLACE, not ON CONFLICT … DO UPDATE: the phone's SQLite is
                   // 3.7.14 and upsert came in 3.24 ("near ON: syntax error" on the Q10). The
                   // row has no column this statement doesn't set, so replacing it loses nothing.
                   "INSERT OR REPLACE INTO tracks(id,title,artist,album,thumb,duration_s,artist_id,album_id) "
                   "VALUES(?,?,?,?,?,?,?,?)",
                   err))
        return false;
    s.bind(1, t.id);
    s.bind(2, t.title);
    s.bind(3, t.artist);
    s.bind(4, t.album);
    s.bind(5, t.thumb);
    s.bind(6, t.duration_s);
    s.bind(7, t.artist_id);
    s.bind(8, t.album_id);
    if (s.step() != Step::Done) {
        err = s.error();
        return false;
    }
    return true;
}

TrackRow read_track(const Stmt &s, int c) {
    TrackRow t;
    t.id = s.text(c);
    t.title = s.text(c + 1);
    t.artist = s.text(c + 2);
    t.album = s.text(c + 3);
    t.thumb = s.text(c + 4);
    t.duration_s = s.i64(c + 5);
    t.artist_id = s.text(c + 6);
    t.album_id = s.text(c + 7);
    return t;
}

std::optional<TrackRow> get_track(Db &db, const std::string &id, std::string &err) {
    std::lock_guard<std::recursive_mutex> lk(db.mu());
    Stmt s;
    if (!s.prepare(db, (std::string("SELECT ") + kTrackColumns + " FROM tracks t WHERE t.id=?").c_str(), err))
        return std::nullopt;
    s.bind(1, id);
    const Step r = s.step();
    if (r == Step::Error) err = s.error();
    if (r != Step::Row) return std::nullopt;
    return read_track(s, 0);
}

}  // namespace bbm
