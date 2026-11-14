// A small RAII layer over the SQLite C API. One Db is one connection; it may be used from
// several threads if each store call holds mu() (the store functions do).
#pragma once
#include <sqlite3.h>

#include <cstdint>
#include <mutex>
#include <string>

namespace bbm {

struct Migration {
    int version;      // 1, 2, 3 ... in order, no gaps
    const char *sql;  // may hold several statements
};

class Db {
public:
    Db() = default;
    ~Db();
    Db(const Db &) = delete;
    Db &operator=(const Db &) = delete;

    // Opens (creating) the file, in WAL mode, with a 5 s busy timeout.
    bool open(const std::string &path, std::string &err);
    void close();
    bool exec(const char *sql, std::string &err);
    int user_version();  // -1 on error
    // Applies every migration newer than user_version, each in its own transaction that also
    // sets user_version. A failing step rolls back and leaves the version where it was.
    bool migrate(const Migration *steps, size_t count, std::string &err);

    sqlite3 *handle() const { return db_; }
    std::recursive_mutex &mu() { return mu_; }
    std::string errmsg() const;

private:
    sqlite3 *db_ = nullptr;
    std::recursive_mutex mu_;
};

enum class Step { Row, Done, Error };

// A prepared statement. Parameters are numbered from 1, columns from 0. A failed bind shows up
// as Step::Error from step().
class Stmt {
public:
    Stmt() = default;
    ~Stmt();
    Stmt(const Stmt &) = delete;
    Stmt &operator=(const Stmt &) = delete;

    bool prepare(Db &db, const char *sql, std::string &err);
    void bind(int i, const std::string &text);
    void bind(int i, int64_t v);
    void bind_null(int i);
    Step step();
    void reset();  // to run again with new bindings
    const std::string &error() const { return err_; }

    std::string text(int col) const;  // "" for NULL
    int64_t i64(int col) const;       // 0 for NULL
    bool is_null(int col) const;

private:
    void check(int rc);
    Db *db_ = nullptr;
    sqlite3_stmt *st_ = nullptr;
    std::string err_;
};

// BEGIN IMMEDIATE ... COMMIT; rolls back in the destructor unless committed.
class Tx {
public:
    explicit Tx(Db &db) : db_(db) {}
    ~Tx();
    Tx(const Tx &) = delete;
    Tx &operator=(const Tx &) = delete;
    bool begin(std::string &err);
    bool commit(std::string &err);

private:
    Db &db_;
    bool open_ = false;
};

}  // namespace bbm
