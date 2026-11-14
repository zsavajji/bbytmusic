#pragma once
#include <string>
#include <vector>

#include "store/db.h"
#include "store/track_store.h"

namespace bbm {

struct QueueRow {
    int64_t position = 0;
    std::string track_id, source;
    bool known = false;  // tracks has a row for track_id
    TrackRow track;      // valid when known
};

struct QueueState {
    int64_t current = -1;  // index into the queue, -1: none
    int64_t position_ms = 0;
};

// Replaces the whole queue in one transaction: positions 0..n-1 in the order given. On a
// failure the old queue is untouched.
bool replace_queue(Db &db, const std::vector<std::string> &track_ids, const std::string &source,
                   std::string &err);
bool load_queue(Db &db, std::vector<QueueRow> &out, std::string &err);  // in order
bool save_queue_state(Db &db, const QueueState &st, std::string &err);
bool load_queue_state(Db &db, QueueState &out, std::string &err);  // the defaults if never saved

}  // namespace bbm
