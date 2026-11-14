// One entry of the play queue: what the UI shows and what the SourceFactory needs to resolve it.
#pragma once
#include <string>

namespace bbm {

struct Track {
    std::string id, title, artist, album, thumb;
    int duration_s = 0;
};

}  // namespace bbm
