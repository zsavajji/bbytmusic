#pragma once
#include <string>
#include <vector>

#include "store/db.h"

namespace bbm {

struct HistoryRow {
    int64_t id = 0;
    std::string track_id;
    int64_t played_at = 0;  // unix seconds
    int64_t ms_played = 0;
};

bool add_history(Db &db, const std::string &track_id, int64_t played_at, int64_t ms_played, std::string &err);
// The last n entries, newest first (ties by insertion order, newest first).
bool recent_history(Db &db, int n, std::vector<HistoryRow> &out, std::string &err);

}  // namespace bbm
