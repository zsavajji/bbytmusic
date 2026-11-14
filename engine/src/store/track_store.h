#pragma once
#include <optional>
#include <string>

#include "store/db.h"

namespace bbm {

struct TrackRow {
    std::string id, title, artist, album, thumb, artist_id, album_id;
    int64_t duration_s = 0;
};

bool upsert_track(Db &db, const TrackRow &t, std::string &err);
// nullopt if there is no such track; err is set only on a failure.
std::optional<TrackRow> get_track(Db &db, const std::string &id, std::string &err);

// The columns of tracks, in TrackRow order; the stores that join tracks share them.
constexpr const char *kTrackColumns = "t.id,t.title,t.artist,t.album,t.thumb,t.duration_s,t.artist_id,t.album_id";
TrackRow read_track(const Stmt &s, int first_col);

}  // namespace bbm
