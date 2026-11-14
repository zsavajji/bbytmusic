#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace bbm {

class Sink {
public:
    virtual ~Sink() = default;
    virtual bool open(int rate, int channels, std::string &err) = 0;
    virtual bool write(const int16_t *interleaved, size_t frames, std::string &err) = 0;   // blocks
    virtual void close() = 0;
};

}  // namespace bbm
