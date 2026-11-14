#include "store/db.h"

namespace bbm {

Db::~Db() { close(); }

void Db::close() {
    if (db_) sqlite3_close(db_);
    db_ = nullptr;
}

std::string Db::errmsg() const { return db_ ? sqlite3_errmsg(db_) : "database not open"; }

bool Db::open(const std::string &path, std::string &err) {
    close();
    if (sqlite3_open_v2(path.c_str(), &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                        nullptr) != SQLITE_OK) {
        err = "open " + path + ": " + errmsg();
        close();
        return false;
    }
    sqlite3_busy_timeout(db_, 5000);
    return exec("PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL;", err);
}

bool Db::exec(const char *sql, std::string &err) {
    char *msg = nullptr;
    if (!db_ || sqlite3_exec(db_, sql, nullptr, nullptr, &msg) != SQLITE_OK) {
        err = msg ? msg : "database not open";
        sqlite3_free(msg);
        return false;
    }
    return true;
}

int Db::user_version() {
    Stmt s;
    std::string err;
    if (!s.prepare(*this, "PRAGMA user_version", err) || s.step() != Step::Row) return -1;
    return int(s.i64(0));
}

bool Db::migrate(const Migration *steps, size_t count, std::string &err) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    const int have = user_version();
    if (have < 0) {
        err = "cannot read user_version: " + errmsg();
        return false;
    }
    for (size_t i = 0; i < count; ++i) {
        if (steps[i].version <= have) continue;
        Tx tx(*this);
        if (!tx.begin(err) || !exec(steps[i].sql, err) ||
            !exec((std::string("PRAGMA user_version=") + std::to_string(steps[i].version)).c_str(), err) ||
            !tx.commit(err)) {
            err = "migration " + std::to_string(steps[i].version) + ": " + err;
            return false;
        }
    }
    return true;
}

Stmt::~Stmt() { sqlite3_finalize(st_); }

bool Stmt::prepare(Db &db, const char *sql, std::string &err) {
    db_ = &db;
    sqlite3_finalize(st_);
    st_ = nullptr;
    err_.clear();
    if (sqlite3_prepare_v2(db.handle(), sql, -1, &st_, nullptr) != SQLITE_OK) {
        err = db.errmsg();
        return false;
    }
    return true;
}

void Stmt::check(int rc) {
    if (rc != SQLITE_OK && rc != SQLITE_ROW && rc != SQLITE_DONE && err_.empty()) err_ = db_->errmsg();
}

void Stmt::bind(int i, const std::string &text) {
    check(sqlite3_bind_text(st_, i, text.data(), int(text.size()), SQLITE_TRANSIENT));
}
void Stmt::bind(int i, int64_t v) { check(sqlite3_bind_int64(st_, i, v)); }
void Stmt::bind_null(int i) { check(sqlite3_bind_null(st_, i)); }

Step Stmt::step() {
    if (!err_.empty()) return Step::Error;
    const int rc = sqlite3_step(st_);
    if (rc == SQLITE_ROW) return Step::Row;
    if (rc == SQLITE_DONE) return Step::Done;
    check(rc);
    return Step::Error;
}

void Stmt::reset() {
    sqlite3_reset(st_);
    sqlite3_clear_bindings(st_);
    err_.clear();
}

std::string Stmt::text(int col) const {
    const unsigned char *t = sqlite3_column_text(st_, col);
    return t ? std::string(reinterpret_cast<const char *>(t), size_t(sqlite3_column_bytes(st_, col))) : "";
}
int64_t Stmt::i64(int col) const { return sqlite3_column_int64(st_, col); }
bool Stmt::is_null(int col) const { return sqlite3_column_type(st_, col) == SQLITE_NULL; }

Tx::~Tx() {
    std::string ignore;
    if (open_) db_.exec("ROLLBACK", ignore);
}
bool Tx::begin(std::string &err) { return open_ = db_.exec("BEGIN IMMEDIATE", err); }
bool Tx::commit(std::string &err) {
    if (!db_.exec("COMMIT", err)) return false;
    open_ = false;
    return true;
}

}  // namespace bbm
