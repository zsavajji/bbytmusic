#include "innertube/lyrics.h"

#include "innertube/items.h"
#include "innertube/next.h"

namespace bbm {

bool parse_yt_lyrics(Json response, std::string &text, std::string &credit, std::string &err) {
    text.clear();
    credit.clear();
    if (!response.is_object()) {
        err = "lyrics: not an object";
        return false;
    }
    const Json shelf = response["contents"]["sectionListRenderer"]["contents"][0]["musicDescriptionShelfRenderer"];
    if (shelf.is_null()) return true;
    text = runs_text(shelf["description"]);
    credit = runs_text(shelf["footer"]);
    return true;
}

bool yt_lyrics(InnerTube &yt, const std::string &video_id, std::string &text, std::string &credit, std::string &err) {
    text.clear();
    credit.clear();
    QueuePage page;
    if (!watch_queue(yt, video_id, "", page, err)) return false;
    if (page.lyrics_browse_id.empty()) return true;
    JsonDoc doc;
    return yt.post("browse", "\"browseId\":" + json_quote(page.lyrics_browse_id), doc, err) &&
           parse_yt_lyrics(doc.root(), text, credit, err);
}

}  // namespace bbm
