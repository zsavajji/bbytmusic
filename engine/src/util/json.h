// A read-only view over yyjson, so that no other module includes yyjson.h.
// A Json is a cheap copyable pointer into a JsonDoc; it is valid while the JsonDoc lives.
// Every accessor is safe on a null view and on the wrong type: it gives null/empty/the default.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

struct yyjson_val;
struct yyjson_doc;

namespace bbm {

class Json {
public:
    Json() = default;
    explicit Json(yyjson_val *v) : v_(v) {}

    bool is_null() const;  // absent, or a JSON null
    bool is_object() const;
    bool is_array() const;
    bool is_string() const;
    bool is_number() const;
    bool is_bool() const;

    Json operator[](std::string_view key) const;  // null view if absent or not an object
    Json operator[](size_t i) const;              // null view if out of range or not an array
    size_t size() const;                          // elements of an array, members of an object, else 0

    std::string_view str() const;    // empty if not a string; valid while the JsonDoc lives
    std::string string() const { return std::string(str()); }
    int64_t i64(int64_t dflt = 0) const;    // numbers, and numeric strings ("12345")
    double num(double dflt = 0) const;      // likewise
    bool boolean(bool dflt = false) const;

    // The first value stored under `key` anywhere beneath this one, depth first.
    Json find(std::string_view key) const;

    // for (Json x : arr.items())   for (auto [key, value] : obj.members())
    class ItemIter {
    public:
        Json operator*() const { return Json(cur_); }
        ItemIter &operator++();
        bool operator!=(const ItemIter &o) const { return left_ != o.left_; }
    private:
        friend class Json;
        yyjson_val *cur_ = nullptr;
        size_t left_ = 0;
    };
    class MemberIter {
    public:
        std::pair<std::string_view, Json> operator*() const;
        MemberIter &operator++();
        bool operator!=(const MemberIter &o) const { return left_ != o.left_; }
    private:
        friend class Json;
        yyjson_val *key_ = nullptr;
        size_t left_ = 0;
    };
    template <class I> struct Range {
        I b, e;
        I begin() const { return b; }
        I end() const { return e; }
    };
    Range<ItemIter> items() const;      // empty unless an array
    Range<MemberIter> members() const;  // empty unless an object

private:
    yyjson_val *v_ = nullptr;
};

class JsonDoc {
public:
    JsonDoc() = default;
    ~JsonDoc();
    JsonDoc(JsonDoc &&o) noexcept : doc_(std::exchange(o.doc_, nullptr)) {}
    JsonDoc &operator=(JsonDoc &&o) noexcept;
    JsonDoc(const JsonDoc &) = delete;
    JsonDoc &operator=(const JsonDoc &) = delete;

    // Copies the text; on failure returns false, sets err and leaves `out` empty.
    static bool parse(std::string_view text, JsonDoc &out, std::string &err);
    Json root() const;  // null view for an empty JsonDoc

private:
    yyjson_doc *doc_ = nullptr;
};

// The text as a JSON string literal, quotes included. Escapes ", \, and control characters;
// UTF-8 passes through untouched.
std::string json_quote(std::string_view text);

}  // namespace bbm
