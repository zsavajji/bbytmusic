#include "check.h"
#include "util/json.h"

using namespace bbm;

int main() {
    JsonDoc doc;
    std::string err;
    const char *text = R"({"a":{"b":[{"c":"deep"},2]},"n":"12345","f":1.5,"t":true,"z":null,
        "u":"caf\u00e9 \ud83c\udfb5 é","list":[1,2,3],"x":{"k":"first"},"y":[{"k":"second"}],"big":9007199254740993})";
    CHECK(JsonDoc::parse(text, doc, err));
    Json j = doc.root();
    CHECK(j.is_object());
    CHECK_EQ(j["a"]["b"][0]["c"].string(), std::string("deep"));
    CHECK_EQ(j["a"]["b"][1].i64(), 2);
    CHECK(j["missing"]["x"][3]["y"].is_null());   // chains never crash
    CHECK(j["a"][0].is_null());                   // index on an object
    CHECK(j["list"]["key"].is_null());            // key on an array
    CHECK_EQ(j["list"].size(), size_t(3));
    CHECK_EQ(j["list"][3].i64(-1), -1);
    CHECK_EQ(j["n"].i64(), 12345);                // number in a string
    CHECK_EQ(j["f"].num(), 1.5);
    CHECK_EQ(j["n"].num(), 12345.0);
    CHECK_EQ(j["big"].i64(), int64_t(9007199254740993LL));
    CHECK_EQ(j["a"].i64(7), 7);
    {   // found by the fuzzer: a real beyond int64 used to be an undefined cast
        JsonDoc d2;
        std::string e2;
        CHECK(JsonDoc::parse("{\"x\":2.49222e+59,\"y\":-1e30}", d2, e2));
        CHECK_EQ(d2.root()["x"].i64(5), 5);
        CHECK_EQ(d2.root()["y"].i64(6), 6);
    }
    CHECK_EQ(j["a"]["b"][0]["c"].i64(9), 9);      // non-numeric string
    CHECK(j["t"].boolean() && j["t"].is_bool());
    CHECK(!j["list"].boolean());
    CHECK(j["z"].is_null());
    CHECK(j["n"].is_string() && !j["n"].is_number() && j["f"].is_number());
    CHECK_EQ(j["u"].string(), std::string("caf\xc3\xa9 \xf0\x9f\x8e\xb5 \xc3\xa9"));
    CHECK_EQ(j["f"].str().size(), size_t(0));
    CHECK_EQ(j.find("c").string(), std::string("deep"));
    CHECK_EQ(j.find("k").string(), std::string("first"));   // depth first, document order
    CHECK(j.find("nope").is_null());
    CHECK(Json().find("x").is_null());

    int64_t sum = 0;
    for (Json x : j["list"].items()) sum += x.i64();
    CHECK_EQ(sum, 6);
    std::string keys;
    for (auto [k, v] : j["x"].members()) keys += std::string(k) + "=" + v.string();
    CHECK_EQ(keys, std::string("k=first"));
    size_t members = 0;
    for (auto m : j.members()) { (void)m; members++; }
    CHECK_EQ(members, j.size());
    for ([[maybe_unused]] Json x : j["f"].items()) CHECK(false);
    for ([[maybe_unused]] auto m : Json().members()) CHECK(false);

    // Malformed input
    for (const char *bad : {"", "{", "{\"a\":}", "[1,2", "nope", "{\"a\":1} x", "\"\\ud800\""}) {
        JsonDoc d;
        std::string e;
        CHECK(!JsonDoc::parse(bad, d, e));
        CHECK(!e.empty());
        CHECK(d.root().is_null());
    }
    JsonDoc moved = std::move(doc);
    CHECK(moved.root().is_object());

    CHECK_EQ(json_quote("a\"b\\c\n\t\x01 é"), std::string("\"a\\\"b\\\\c\\n\\t\\u0001 \xc3\xa9\""));
    JsonDoc rt;
    CHECK(JsonDoc::parse(json_quote(std::string("x\"\\\x1f\xf0\x9f\x8e\xb5", 8)), rt, err));
    CHECK_EQ(rt.root().str(), std::string_view("x\"\\\x1f\xf0\x9f\x8e\xb5", 8));
    return check_done();
}
