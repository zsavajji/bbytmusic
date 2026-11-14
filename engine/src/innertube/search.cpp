#include "innertube/search.h"

#include <algorithm>

namespace bbm {
namespace {

// A renderer's continuation token: shelf.continuations[0].nextContinuationData.continuation.
std::string shelf_continuation(Json shelf) {
    Json c = shelf["continuations"][0];
    Json n = c["nextContinuationData"];
    if (n.is_null()) n = c["reloadContinuationData"];
    return n["continuation"].string();
}

void add_items(Json contents, SearchSection &sec) {
    for (Json c : contents.items()) {
        Item it;
        if (parse_item(c, it)) sec.items.push_back(std::move(it));
    }
}

void push_if_any(SearchPage &out, SearchSection &&sec) {
    if (!sec.items.empty()) out.sections.push_back(std::move(sec));
}

// One entry of sectionListRenderer.contents. Shelves ("Songs") are sections of their own; the
// flat layout YouTube sometimes serves wraps each result in an itemSectionRenderer, and those
// (with a top result's own list) are gathered into one list that parse_search() groups by kind.
void parse_section(Json c, SearchPage &out, SearchSection &flat) {
    if (Json shelf = c["musicShelfRenderer"]; !shelf.is_null()) {
        SearchSection sec{runs_text(shelf["title"]), {}};
        add_items(shelf["contents"], sec);
        push_if_any(out, std::move(sec));
        if (out.continuation.empty()) out.continuation = shelf_continuation(shelf);
    } else if (Json card = c["musicCardShelfRenderer"]; !card.is_null()) {
        SearchSection sec{"Top result", {}};
        Item top;
        if (parse_item(c, top)) sec.items.push_back(std::move(top));
        push_if_any(out, std::move(sec));
        add_items(card["contents"], flat);
    } else if (Json is = c["itemSectionRenderer"]; !is.is_null()) {
        add_items(is["contents"], flat);
    } else if (Json cont = c["continuationItemRenderer"]; !cont.is_null()) {
        if (out.continuation.empty())
            out.continuation = cont["continuationEndpoint"]["continuationCommand"]["token"].string();
    }
}

// The flat list as sections of one kind each, in order; the empty ones are left out.
void group_by_kind(SearchSection &flat, SearchPage &out) {
    static const std::pair<const char *, std::vector<const char *>> groups[] = {
        {"Songs", {"song"}}, {"Albums", {"album"}}, {"Artists", {"artist"}},
        {"Videos", {"video"}}, {"Playlists", {"playlist", "station"}}};
    for (const auto &[title, kinds] : groups) {
        SearchSection sec{title, {}};
        for (Item &it : flat.items)
            if (std::find(kinds.begin(), kinds.end(), it.kind) != kinds.end()) sec.items.push_back(it);
        push_if_any(out, std::move(sec));
    }
}

}  // namespace

std::string_view search_params(SearchFilter f) {
    // From the filter chips of a live search response (2026-11-14); Playlists is the
    // "Community playlists" chip, the one that returns user playlists rather than YouTube's.
    switch (f) {
        case SearchFilter::Songs:     return "EgWKAQIIAWoSEAQQAxAFEAoQEBAJEBUQDhAR";
        case SearchFilter::Videos:    return "EgWKAQIQAWoSEAQQAxAFEAoQEBAJEBUQDhAR";
        case SearchFilter::Albums:    return "EgWKAQIYAWoSEAQQAxAFEAoQEBAJEBUQDhAR";
        case SearchFilter::Artists:   return "EgWKAQIgAWoSEAQQAxAFEAoQEBAJEBUQDhAR";
        case SearchFilter::Playlists: return "EgeKAQQoAEABahIQBBADEAUQChAQEAkQFRAOEBE%3D";
        case SearchFilter::All:       break;
    }
    return {};
}

bool parse_search(Json response, SearchPage &out, std::string &err) {
    out = SearchPage{};
    Json sl = response["contents"]["tabbedSearchResultsRenderer"]["tabs"][0]["tabRenderer"]["content"]
                      ["sectionListRenderer"];
    SearchSection flat;
    if (!sl.is_null()) {
        for (Json c : sl["contents"].items()) parse_section(c, out, flat);
    } else if (Json cc = response["continuationContents"]; !cc.is_null()) {
        Json shelf = cc["musicShelfContinuation"];
        SearchSection sec;
        add_items(shelf["contents"], sec);
        push_if_any(out, std::move(sec));
        out.continuation = shelf_continuation(shelf);
    } else {
        err = "search: no results in the response";
        return false;
    }
    group_by_kind(flat, out);
    return true;
}

bool search(InnerTube &yt, std::string_view query, SearchFilter filter, SearchPage &out, std::string &err) {
    std::string fields = "\"query\":" + json_quote(query);
    if (filter != SearchFilter::All) fields += ",\"params\":" + json_quote(search_params(filter));
    JsonDoc doc;
    return yt.post("search", fields, doc, err) && parse_search(doc.root(), out, err);
}

bool search_more(InnerTube &yt, const std::string &continuation, SearchPage &out, std::string &err) {
    JsonDoc doc;
    return yt.post("search", "\"continuation\":" + json_quote(continuation), doc, err) &&
           parse_search(doc.root(), out, err);
}

bool parse_suggestions(Json response, std::vector<std::string> &out) {
    out.clear();
    for (Json section : response["contents"].items())
        for (Json c : section["searchSuggestionsSectionRenderer"]["contents"].items()) {
            Json r = c["searchSuggestionRenderer"];
            std::string q = r["navigationEndpoint"]["searchEndpoint"]["query"].string();
            if (q.empty()) q = runs_text(r["suggestion"]);
            if (!q.empty()) out.push_back(std::move(q));
        }
    return true;
}

bool suggestions(InnerTube &yt, std::string_view prefix, std::vector<std::string> &out, std::string &err) {
    JsonDoc doc;
    if (!yt.post("music/get_search_suggestions", "\"input\":" + json_quote(prefix), doc, err)) return false;
    return parse_suggestions(doc.root(), out);
}

}  // namespace bbm
