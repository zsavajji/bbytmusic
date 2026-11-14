// Flat JSON objects with string and number values: all the gate protocol needs.
#pragma once
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

struct Msg {
    struct Val { std::string text; bool is_str; };
    std::map<std::string, Val> v;
    bool ok = false;  // parsed

    std::string str(const std::string &k, const std::string &def = "") const {
        auto i = v.find(k);
        return i == v.end() ? def : i->second.text;
    }
    double num(const std::string &k, double def) const {
        auto i = v.find(k);
        return i == v.end() || i->second.is_str ? def : std::atof(i->second.text.c_str());
    }
    // The "@" id as JSON, "" if absent: echoed back verbatim in the same type.
    std::string id_json() const;
};

inline std::string json_quote(const std::string &s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += char(c); }
        else if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; }
        else o += char(c);
    }
    return o + "\"";
}

inline std::string Msg::id_json() const {
    auto i = v.find("@");
    if (i == v.end()) return "";
    return i->second.is_str ? json_quote(i->second.text) : i->second.text;
}

// ponytail: \uXXXX escapes are kept as typed (the UI never sends them); nested values fail the parse.
inline Msg json_parse(const std::string &s) {
    Msg m;
    size_t p = 0;
    auto ws = [&] { while (p < s.size() && (s[p] == ' ' || s[p] == '\t' || s[p] == '\r' || s[p] == '\n')) p++; };
    auto str = [&](std::string &out) {
        if (p >= s.size() || s[p] != '"') return false;
        p++;
        while (p < s.size() && s[p] != '"') {
            if (s[p] == '\\' && p + 1 < s.size()) {
                p++;
                switch (s[p]) { case 'n': out += '\n'; break; case 't': out += '\t'; break; case 'r': out += '\r'; break; default: out += s[p]; }
            } else out += s[p];
            p++;
        }
        if (p >= s.size()) return false;
        p++;
        return true;
    };
    ws();
    if (p >= s.size() || s[p] != '{') return m;
    p++;
    ws();
    if (p < s.size() && s[p] == '}') { m.ok = true; return m; }
    for (;;) {
        ws();
        std::string key;
        if (!str(key)) return m;
        ws();
        if (p >= s.size() || s[p] != ':') return m;
        p++;
        ws();
        Msg::Val val{"", false};
        if (p < s.size() && s[p] == '"') { val.is_str = true; if (!str(val.text)) return m; }
        else {
            const size_t a = p;
            while (p < s.size() && s[p] != ',' && s[p] != '}' && s[p] != ' ') p++;
            val.text = s.substr(a, p - a);
            if (val.text.empty() || val.text[0] == '{' || val.text[0] == '[') return m;
        }
        m.v[key] = val;
        ws();
        if (p < s.size() && s[p] == ',') { p++; continue; }
        if (p < s.size() && s[p] == '}') { m.ok = true; return m; }
        return m;
    }
}
