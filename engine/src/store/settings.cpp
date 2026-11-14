#include "store/settings.h"

namespace bbm {

std::string get_setting(Db &db, const std::string &key, const std::string &dflt) {
    std::lock_guard<std::recursive_mutex> lk(db.mu());
    Stmt s;
    std::string err;
    if (!s.prepare(db, "SELECT value FROM settings WHERE key=?", err)) return dflt;
    s.bind(1, key);
    return s.step() == Step::Row && !s.is_null(0) ? s.text(0) : dflt;
}

bool set_setting(Db &db, const std::string &key, const std::string &value, std::string &err) {
    std::lock_guard<std::recursive_mutex> lk(db.mu());
    Stmt s;
    if (!s.prepare(db, "INSERT OR REPLACE INTO settings(key,value) VALUES(?,?)", err)) return false;
    s.bind(1, key);
    s.bind(2, value);
    if (s.step() != Step::Done) {
        err = s.error();
        return false;
    }
    return true;
}

}  // namespace bbm
