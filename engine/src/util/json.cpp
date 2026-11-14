#include "util/json.h"

#include <cerrno>
#include <cstdlib>

#include "yyjson.h"

namespace bbm {
namespace {
constexpr int kMaxFindDepth = 1000;

Json find_in(yyjson_val *v, std::string_view key, int depth) {
    if (!v || depth > kMaxFindDepth) return {};
    if (yyjson_is_obj(v)) {
        yyjson_val *k = unsafe_yyjson_get_first(v);
        for (size_t n = yyjson_obj_size(v); n; n--) {
            yyjson_val *val = unsafe_yyjson_get_next(k);
            if (std::string_view(yyjson_get_str(k), yyjson_get_len(k)) == key) return Json(val);
            if (Json r = find_in(val, key, depth + 1); !r.is_null()) return r;
            k = unsafe_yyjson_get_next(val);
        }
    } else if (yyjson_is_arr(v)) {
        yyjson_val *e = unsafe_yyjson_get_first(v);
        for (size_t n = yyjson_arr_size(v); n; n--, e = unsafe_yyjson_get_next(e))
            if (Json r = find_in(e, key, depth + 1); !r.is_null()) return r;
    }
    return {};
}
}  // namespace

bool Json::is_null() const { return !v_ || yyjson_is_null(v_); }
bool Json::is_object() const { return yyjson_is_obj(v_); }
bool Json::is_array() const { return yyjson_is_arr(v_); }
bool Json::is_string() const { return yyjson_is_str(v_); }
bool Json::is_number() const { return yyjson_is_num(v_); }
bool Json::is_bool() const { return yyjson_is_bool(v_); }

Json Json::operator[](std::string_view key) const {
    if (!yyjson_is_obj(v_)) return {};
    return Json(yyjson_obj_getn(v_, key.data(), key.size()));
}
Json Json::operator[](size_t i) const { return yyjson_is_arr(v_) ? Json(yyjson_arr_get(v_, i)) : Json(); }
size_t Json::size() const {
    if (yyjson_is_arr(v_)) return yyjson_arr_size(v_);
    if (yyjson_is_obj(v_)) return yyjson_obj_size(v_);
    return 0;
}

std::string_view Json::str() const {
    return yyjson_is_str(v_) ? std::string_view(yyjson_get_str(v_), yyjson_get_len(v_)) : std::string_view();
}

int64_t Json::i64(int64_t dflt) const {
    if (yyjson_is_int(v_)) return yyjson_is_sint(v_) ? yyjson_get_sint(v_) : static_cast<int64_t>(yyjson_get_uint(v_));
    if (yyjson_is_real(v_)) {
        const double d = yyjson_get_real(v_);
        // outside int64 (and NaN) the cast is undefined: give the default
        return d >= -9223372036854775808.0 && d < 9223372036854775808.0 ? static_cast<int64_t>(d) : dflt;
    }
    if (yyjson_is_str(v_)) {
        const std::string s(str());  // NUL-terminated for strtoll
        char *end = nullptr;
        errno = 0;
        const long long n = std::strtoll(s.c_str(), &end, 10);
        if (!s.empty() && end && *end == 0 && errno == 0) return n;
    }
    return dflt;
}

double Json::num(double dflt) const {
    if (yyjson_is_num(v_)) return yyjson_get_num(v_);
    if (yyjson_is_str(v_)) {
        const std::string s(str());
        char *end = nullptr;
        const double d = std::strtod(s.c_str(), &end);
        if (!s.empty() && end && *end == 0) return d;
    }
    return dflt;
}

bool Json::boolean(bool dflt) const { return yyjson_is_bool(v_) ? yyjson_get_bool(v_) : dflt; }

Json Json::find(std::string_view key) const { return find_in(v_, key, 0); }

Json::ItemIter &Json::ItemIter::operator++() {
    cur_ = unsafe_yyjson_get_next(cur_);
    left_--;
    return *this;
}
std::pair<std::string_view, Json> Json::MemberIter::operator*() const {
    return {std::string_view(yyjson_get_str(key_), yyjson_get_len(key_)), Json(unsafe_yyjson_get_next(key_))};
}
Json::MemberIter &Json::MemberIter::operator++() {
    key_ = unsafe_yyjson_get_next(unsafe_yyjson_get_next(key_));
    left_--;
    return *this;
}
Json::Range<Json::ItemIter> Json::items() const {
    Range<ItemIter> r;
    if (yyjson_is_arr(v_) && yyjson_arr_size(v_)) {
        r.b.cur_ = unsafe_yyjson_get_first(v_);
        r.b.left_ = yyjson_arr_size(v_);
    }
    return r;
}
Json::Range<Json::MemberIter> Json::members() const {
    Range<MemberIter> r;
    if (yyjson_is_obj(v_) && yyjson_obj_size(v_)) {
        r.b.key_ = unsafe_yyjson_get_first(v_);
        r.b.left_ = yyjson_obj_size(v_);
    }
    return r;
}

JsonDoc::~JsonDoc() { yyjson_doc_free(doc_); }
JsonDoc &JsonDoc::operator=(JsonDoc &&o) noexcept {
    if (this != &o) {
        yyjson_doc_free(doc_);
        doc_ = std::exchange(o.doc_, nullptr);
    }
    return *this;
}
bool JsonDoc::parse(std::string_view text, JsonDoc &out, std::string &err) {
    yyjson_doc_free(out.doc_);
    out.doc_ = nullptr;
    yyjson_read_err e;
    yyjson_doc *d = yyjson_read_opts(const_cast<char *>(text.data()), text.size(), 0, nullptr, &e);
    if (!d) {
        err = std::string("json: ") + (e.msg ? e.msg : "parse error") + " at byte " + std::to_string(e.pos);
        return false;
    }
    out.doc_ = d;
    return true;
}
Json JsonDoc::root() const { return Json(doc_ ? yyjson_doc_get_root(doc_) : nullptr); }

std::string json_quote(std::string_view text) {
    static const char hex[] = "0123456789abcdef";
    std::string out = "\"";
    out.reserve(text.size() + 2);
    for (const char ch : text) {
        const auto c = static_cast<unsigned char>(ch);
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        default:
            if (c < 0x20 || c == 0x7f) {
                out += "\\u00";
                out += hex[c >> 4];
                out += hex[c & 15];
            } else {
                out += ch;
            }
        }
    }
    out += '"';
    return out;
}
}  // namespace bbm
