// The InnerTube parsers over one JSON document.
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "fuzz.h"
#include "innertube/browse.h"
#include "innertube/next.h"
#include "innertube/search.h"
#include "util/json.h"

using namespace bbm;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    JsonDoc doc;
    std::string err;
    if (!JsonDoc::parse({reinterpret_cast<const char *>(data), size}, doc, err)) return 0;
    {
        SearchPage page;
        std::string e;
        if (!parse_search(doc.root(), page, e)) FUZZ_ASSERT(!e.empty());
    }
    {
        QueuePage page;
        std::string e;
        if (!parse_queue(doc.root(), page, e)) FUZZ_ASSERT(!e.empty());
    }
    {
        BrowsePage page;
        std::string e;
        if (!parse_browse(doc.root(), "MPREb_x", page, e)) FUZZ_ASSERT(!e.empty());
        if (!parse_browse(doc.root(), "VLPL_x", page, e)) FUZZ_ASSERT(!e.empty());
    }
    {
        std::vector<std::string> out;
        (void)parse_suggestions(doc.root(), out);
    }
    return 0;
}
