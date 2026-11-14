// A tiny test harness: plain main(), no framework.
//   CHECK(cond);  CHECK_EQ(a, b);  ...  return check_done();
// A failed check prints file:line and (for CHECK_EQ) both values, and the test keeps going.
#pragma once
#include <cstdio>
#include <sstream>
#include <type_traits>
#include <string>

namespace bbm_check {
inline int &failures() { static int n = 0; return n; }
inline int &total() { static int n = 0; return n; }
template <class T> std::string show(const T &v) {
    std::ostringstream o;
    if constexpr (std::is_same_v<T, bool>) o << (v ? "true" : "false");
    else o << v;
    return o.str();
}
}  // namespace bbm_check

#define CHECK(cond)                                                                     \
    do {                                                                                \
        ++bbm_check::total();                                                           \
        if (!(cond)) {                                                                  \
            ++bbm_check::failures();                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                               \
    } while (0)

#define CHECK_EQ(a, b)                                                                  \
    do {                                                                                \
        ++bbm_check::total();                                                           \
        const auto &va_ = (a);                                                          \
        const auto &vb_ = (b);                                                          \
        if (!(va_ == vb_)) {                                                            \
            ++bbm_check::failures();                                                    \
            std::fprintf(stderr, "%s:%d: CHECK_EQ failed: %s == %s\n  left:  %s\n  right: %s\n", \
                         __FILE__, __LINE__, #a, #b, bbm_check::show(va_).c_str(),      \
                         bbm_check::show(vb_).c_str());                                 \
        }                                                                               \
    } while (0)

inline int check_done() {
    std::printf("%d checks, %d failed\n", bbm_check::total(), bbm_check::failures());
    return bbm_check::failures() ? 1 : 0;
}
