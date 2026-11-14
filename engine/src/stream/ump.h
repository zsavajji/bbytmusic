// UMP, the framing of a SABR response: parts of (type varint, size varint, payload).
// Its varint is not protobuf's: the first byte's high bits give the length (1-5 bytes).
#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace bbm {

namespace ump {
enum : uint32_t {
    MEDIA_HEADER = 20,
    MEDIA = 21,
    MEDIA_END = 22,
    NEXT_REQUEST_POLICY = 35,
    FORMAT_INITIALIZATION_METADATA = 42,
    SABR_REDIRECT = 43,
    SABR_ERROR = 44,
    SABR_SEEK = 45,
    RELOAD_PLAYER_RESPONSE = 46,
    PLAYBACK_START_POLICY = 47,
    START_BW_SAMPLING_HINT = 49,
    SELECTABLE_FORMATS = 51,
    REQUEST_IDENTIFIER = 52,
    REQUEST_CANCELLATION_POLICY = 53,
    SABR_CONTEXT_UPDATE = 57,
    STREAM_PROTECTION_STATUS = 58,
};
}  // namespace ump

// False if buf ends before the varint does.
bool ump_read_varint(std::span<const uint8_t> buf, size_t &pos, uint32_t &v);
void ump_write_varint(std::vector<uint8_t> &out, uint32_t v);   // the shortest form
void ump_write_part(std::vector<uint8_t> &out, uint32_t type, std::span<const uint8_t> payload);

// Fed the response body in chunks as they arrive (a part may span any number of them),
// hands out whole parts. The payload is valid until the next feed().
class UmpReader {
public:
    void feed(std::span<const uint8_t> chunk);
    bool next(uint32_t &type, std::span<const uint8_t> &payload);
    size_t pending() const { return buf_.size() - pos_; }   // bytes of an unfinished part

private:
    std::vector<uint8_t> buf_;
    size_t pos_ = 0;
};

}  // namespace bbm
