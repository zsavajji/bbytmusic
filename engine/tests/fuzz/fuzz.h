// Shared by the fuzz targets: a failed promise aborts, so libFuzzer saves the input.
#pragma once
#include <cstdio>
#include <cstdlib>

#define FUZZ_ASSERT(c)                                                       \
    do {                                                                     \
        if (!(c)) { std::fprintf(stderr, "promise broken: %s (%s:%d)\n", #c, __FILE__, __LINE__); std::abort(); } \
    } while (0)
