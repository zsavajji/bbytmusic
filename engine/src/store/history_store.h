#pragma once
#include <string>
#include <vector>

#include "store/db.h"
#include "store/track_store.h"

namespace bbm {

struct HistoryRow {
    int64_t id = 0;
    std::string track_id;
    int64_t played_at = 0;  // unix seconds
    int64_t ms_played = 0;
};

struct PlayedTrack {
    TrackRow track;
    int64_t played_at = 0;  // the last time, unix seconds
};

bool add_history(Db &db, const std::string &track_id, int64_t played_at, int64_t ms_played, std::string &err);
// The last n entries, newest first (ties by insertion order, newest first).
bool recent_history(Db &db, int n, std::vector<HistoryRow> &out, std::string &err);

// The tracks played, each once with its latest played_at, newest first; history rows whose
// track is not in `tracks` are left out.
bool recent_tracks(Db &db, int n, std::vector<PlayedTrack> &out, std::string &err);

}  // namespace bbm
