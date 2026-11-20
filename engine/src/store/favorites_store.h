#pragma once
#include <string>
#include <vector>

#include "store/db.h"
#include "store/track_store.h"

namespace bbm {

struct FavoriteRow {
    TrackRow track;
    int64_t added_at = 0;  // unix seconds
    bool offline = false;  // its audio is whole in the song cache
};

// Upserts the track's row and adds the favourite; one already there keeps its added_at and offline.
bool add_favorite(Db &db, const TrackRow &t, int64_t added_at, std::string &err);
// True when it was not there too.
bool remove_favorite(Db &db, const std::string &track_id, std::string &err);
// Newest first (ties by rowid, newest first); a favourite whose track row is missing is left out.
bool list_favorites(Db &db, std::vector<FavoriteRow> &out, std::string &err);
bool set_favorite_offline(Db &db, const std::string &track_id, bool offline, std::string &err);

}  // namespace bbm
