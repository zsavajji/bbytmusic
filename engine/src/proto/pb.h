// Protocol buffers by hand: just the wire format, no schema.
// PbWriter appends fields; PbReader walks the fields of one message, bounds-checked:
// malformed input makes next() return false, and nothing is ever read past the span.
#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace bbm {

enum class PbWire : uint8_t { Varint = 0, Fixed64 = 1, Bytes = 2, Fixed32 = 5 };

class PbWriter {
public:
    PbWriter &varint(uint32_t field, uint64_t v);
    PbWriter &fixed32(uint32_t field, uint32_t v);
    PbWriter &float32(uint32_t field, float v);
    PbWriter &bytes(uint32_t field, std::span<const uint8_t> v);
    PbWriter &string(uint32_t field, std::string_view v);
    PbWriter &message(uint32_t field, const PbWriter &m) { return bytes(field, m.data()); }

    const std::vector<uint8_t> &data() const { return buf_; }
    static void put_varint(std::vector<uint8_t> &out, uint64_t v);

private:
    void tag(uint32_t field, PbWire wire) { put_varint(buf_, uint64_t(field) << 3 | uint8_t(wire)); }
    std::vector<uint8_t> buf_;
};

struct PbField {
    uint32_t number = 0;
    PbWire wire = PbWire::Varint;
    uint64_t value = 0;                 // Varint, Fixed32, Fixed64 (little-endian, as an integer)
    std::span<const uint8_t> bytes;     // Bytes: a view into the reader's buffer
    float f32() const;                  // a Fixed32 read as a float
};

class PbReader {
public:
    explicit PbReader(std::span<const uint8_t> buf) : buf_(buf) {}
    // The next field. False at the end of the message, or on malformed input (then failed()).
    bool next(PbField &f);
    bool failed() const { return failed_; }
    bool done() const { return pos_ == buf_.size(); }

    // A protobuf varint at buf[pos]: at most 10 bytes, no overflow past 64 bits.
    static bool read_varint(std::span<const uint8_t> buf, size_t &pos, uint64_t &v);

private:
    bool fail() { failed_ = true; return false; }
    std::span<const uint8_t> buf_;
    size_t pos_ = 0;
    bool failed_ = false;
};

// One message's fields, read at once; a field's last value wins, as in protobuf. Views point
// into `buf`, which must outlive this. A malformed message gives ok() false and no fields.
class PbMsg {
public:
    explicit PbMsg(std::span<const uint8_t> buf);
    bool ok() const { return ok_; }
    bool has(uint32_t field) const { return find(field) != nullptr; }
    uint64_t u(uint32_t field, uint64_t dflt = 0) const;            // a varint or fixed field
    std::span<const uint8_t> bytes(uint32_t field) const;           // empty if absent
    std::string_view str(uint32_t field) const;

private:
    const PbField *find(uint32_t field) const;
    std::vector<PbField> fields_;
    bool ok_ = false;
};

}  // namespace bbm
