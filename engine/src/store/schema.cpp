#include "store/schema.h"

namespace bbm {
namespace {

// Append only: a released step is never edited, a change is a new step.
const Migration kSteps[] = {
    {1, R"sql(
CREATE TABLE tracks(
  id TEXT PRIMARY KEY, title TEXT, artist TEXT, album TEXT, thumb TEXT,
  duration_s INTEGER, artist_id TEXT, album_id TEXT);
CREATE TABLE queue(
  position INTEGER PRIMARY KEY, track_id TEXT NOT NULL, source TEXT);
CREATE TABLE queue_state(
  id INTEGER PRIMARY KEY CHECK (id = 1), current INTEGER, position_ms INTEGER);
CREATE TABLE history(
  id INTEGER PRIMARY KEY, track_id TEXT NOT NULL, played_at INTEGER, ms_played INTEGER);
CREATE INDEX history_played_at ON history(played_at);
CREATE TABLE settings(key TEXT PRIMARY KEY, value TEXT);
)sql"},
    {2, R"sql(
CREATE TABLE favorites(track_id TEXT PRIMARY KEY, added_at INTEGER, offline INTEGER NOT NULL DEFAULT 0);
)sql"},
};

}  // namespace

bool open_store(Db &db, const std::string &path, std::string &err) {
    if (!db.open(path, err)) return false;
    if (db.user_version() > kSchemaVersion) {
        err = path + " was written by a newer version";
        return false;
    }
    return db.migrate(kSteps, sizeof kSteps / sizeof kSteps[0], err);
}

}  // namespace bbm
