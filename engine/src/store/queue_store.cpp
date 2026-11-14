#include "store/queue_store.h"

namespace bbm {

bool replace_queue(Db &db, const std::vector<std::string> &ids, const std::string &source, std::string &err) {
    std::lock_guard<std::recursive_mutex> lk(db.mu());
    Tx tx(db);  // rolls back on any early return
    if (!tx.begin(err) || !db.exec("DELETE FROM queue", err)) return false;
    Stmt s;
    if (!s.prepare(db, "INSERT INTO queue(position,track_id,source) VALUES(?,?,?)", err)) return false;
    int64_t pos = 0;
    for (const std::string &id : ids) {
        s.reset();
        s.bind(1, pos++);
        s.bind(2, id);
        s.bind(3, source);
        if (s.step() != Step::Done) {
            err = s.error();
            return false;
        }
    }
    return tx.commit(err);
}

bool load_queue(Db &db, std::vector<QueueRow> &out, std::string &err) {
    std::lock_guard<std::recursive_mutex> lk(db.mu());
    out.clear();
    Stmt s;
    if (!s.prepare(db,
                   (std::string("SELECT q.position,q.track_id,q.source,") + kTrackColumns +
                    " FROM queue q LEFT JOIN tracks t ON t.id=q.track_id ORDER BY q.position")
                       .c_str(),
                   err))
        return false;
    for (Step r; (r = s.step()) == Step::Row;) {
        QueueRow row;
        row.position = s.i64(0);
        row.track_id = s.text(1);
        row.source = s.text(2);
        row.known = !s.is_null(3);
        if (row.known) row.track = read_track(s, 3);
        out.push_back(std::move(row));
    }
    return true;
}

bool save_queue_state(Db &db, const QueueState &st, std::string &err) {
    std::lock_guard<std::recursive_mutex> lk(db.mu());
    Stmt s;
    if (!s.prepare(db, "INSERT OR REPLACE INTO queue_state(id,current,position_ms) VALUES(1,?,?)", err)) return false;
    s.bind(1, st.current);
    s.bind(2, st.position_ms);
    if (s.step() != Step::Done) {
        err = s.error();
        return false;
    }
    return true;
}

bool load_queue_state(Db &db, QueueState &out, std::string &err) {
    std::lock_guard<std::recursive_mutex> lk(db.mu());
    out = QueueState{};
    Stmt s;
    if (!s.prepare(db, "SELECT current,position_ms FROM queue_state WHERE id=1", err)) return false;
    const Step r = s.step();
    if (r == Step::Error) {
        err = s.error();
        return false;
    }
    if (r == Step::Row) {
        out.current = s.i64(0);
        out.position_ms = s.i64(1);
    }
    return true;
}

}  // namespace bbm
