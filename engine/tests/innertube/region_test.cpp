// The country out of the recorded sw.js_data (tests/fixtures/innertube), and what is not one.
#include <fstream>
#include <sstream>

#include "check.h"
#include "innertube/region.h"

using namespace bbm;

int main() {
    std::ifstream f(std::string(BBM_FIXTURES) + "/innertube/sw_js_data.txt", std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string body = ss.str();
    std::string gl, err;

    CHECK(parse_sw_data(body, gl, err));
    CHECK_EQ(gl, std::string("IT"));
    gl.clear();
    CHECK(parse_sw_data(body.substr(4), gl, err));   // without the )]}' guard
    CHECK_EQ(gl, std::string("IT"));

    const char *bad[] = {
        "",
        ")]}'",
        ")]}'\n",
        "<html>",
        ")]}'\n{}",
        ")]}'\n[]",
        ")]}'\n[[\"yt.sw.adr\",null,[[[\"it\"]]]]]",                // no index 1
        ")]}'\n[[\"yt.sw.adr\",null,[[[\"it\",\"it\"]]]]]",         // lowercase
        ")]}'\n[[\"yt.sw.adr\",null,[[[\"it\",\"ITA\"]]]]]",        // three letters
        ")]}'\n[[\"yt.sw.adr\",null,[[[\"it\",\"I1\"]]]]]",         // a digit
        ")]}'\n[[\"yt.sw.adr\",null,[[[\"it\",42]]]]]",             // not a string
        ")]}'\n[[\"yt.sw.adr\",null,[[[\"it\",\"IT\"",              // cut short
    };
    for (const char *b : bad) {
        gl = "keep";
        err.clear();
        CHECK(!parse_sw_data(b, gl, err));
        CHECK(!err.empty());
        CHECK_EQ(gl, std::string("keep"));
    }

    CHECK(valid_region("IT") && valid_region("US"));
    CHECK(!valid_region("") && !valid_region("I") && !valid_region("it") && !valid_region("ITA") && !valid_region("I1"));
    return check_done();
}
