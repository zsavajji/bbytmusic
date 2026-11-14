#include "store/history_store.h"

namespace bbm {

bool add_history(Db &db, const std::string &track_id, int64_t played_at, int64_t ms_played, std::string &err) {
    std::lock_guard<std::recursive_mutex> lk(db.mu());
    Stmt s;
    if (!s.prepare(db, "INSERT INTO history(track_id,played_at,ms_played) VALUES(?,?,?)", err)) return false;
    s.bind(1, track_id);
    s.bind(2, played_at);
    s.bind(3, ms_played);
    if (s.step() != Step::Done) {
        err = s.error();
        return false;
    }
    return true;
}

bool recent_history(Db &db, int n, std::vector<HistoryRow> &out, std::string &err) {
    std::lock_guard<std::recursive_mutex> lk(db.mu());
    out.clear();
    Stmt s;
    if (!s.prepare(db, "SELECT id,track_id,played_at,ms_played FROM history ORDER BY played_at DESC,id DESC LIMIT ?",
                   err))
        return false;
    s.bind(1, int64_t(n));
    for (Step r; (r = s.step()) == Step::Row;) out.push_back({s.i64(0), s.text(1), s.i64(2), s.i64(3)});
    return true;
}

}  // namespace bbm
