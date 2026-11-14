#include "proto/pb.h"

#include <cstring>

namespace bbm {

void PbWriter::put_varint(std::vector<uint8_t> &out, uint64_t v) {
    while (v >= 0x80) {
        out.push_back(uint8_t(v) | 0x80);
        v >>= 7;
    }
    out.push_back(uint8_t(v));
}

PbWriter &PbWriter::varint(uint32_t field, uint64_t v) {
    tag(field, PbWire::Varint);
    put_varint(buf_, v);
    return *this;
}

PbWriter &PbWriter::fixed32(uint32_t field, uint32_t v) {
    tag(field, PbWire::Fixed32);
    for (int i = 0; i < 4; i++) buf_.push_back(uint8_t(v >> (8 * i)));
    return *this;
}

PbWriter &PbWriter::float32(uint32_t field, float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, 4);
    return fixed32(field, bits);
}

PbWriter &PbWriter::bytes(uint32_t field, std::span<const uint8_t> v) {
    tag(field, PbWire::Bytes);
    put_varint(buf_, v.size());
    buf_.insert(buf_.end(), v.begin(), v.end());
    return *this;
}

PbWriter &PbWriter::string(uint32_t field, std::string_view v) {
    return bytes(field, {reinterpret_cast<const uint8_t *>(v.data()), v.size()});
}

float PbField::f32() const {
    uint32_t bits = uint32_t(value);
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

bool PbReader::read_varint(std::span<const uint8_t> buf, size_t &pos, uint64_t &v) {
    v = 0;
    for (int i = 0; i < 10; i++) {
        if (pos >= buf.size()) return false;
        uint8_t b = buf[pos++];
        if (i == 9 && b > 1) return false;  // past 64 bits
        v |= uint64_t(b & 0x7F) << (7 * i);
        if (!(b & 0x80)) return true;
    }
    return false;
}

bool PbReader::next(PbField &f) {
    if (failed_ || pos_ >= buf_.size()) return false;
    uint64_t key;
    if (!read_varint(buf_, pos_, key)) return fail();
    uint64_t number = key >> 3;
    if (number == 0 || number > 0x1FFFFFFF) return fail();
    f = PbField{};
    f.number = uint32_t(number);
    switch (key & 7) {
    case 0:
        f.wire = PbWire::Varint;
        if (!read_varint(buf_, pos_, f.value)) return fail();
        return true;
    case 2: {
        f.wire = PbWire::Bytes;
        uint64_t n;
        if (!read_varint(buf_, pos_, n) || n > buf_.size() - pos_) return fail();
        f.bytes = buf_.subspan(pos_, size_t(n));
        pos_ += size_t(n);
        return true;
    }
    case 1:
    case 5: {
        size_t n = (key & 7) == 1 ? 8 : 4;
        f.wire = n == 8 ? PbWire::Fixed64 : PbWire::Fixed32;
        if (n > buf_.size() - pos_) return fail();
        for (size_t i = 0; i < n; i++) f.value |= uint64_t(buf_[pos_ + i]) << (8 * i);
        pos_ += n;
        return true;
    }
    default:  // 3, 4: groups, long gone; 6, 7: not wire types
        return fail();
    }
}

PbMsg::PbMsg(std::span<const uint8_t> buf) {
    PbReader r(buf);
    PbField f;
    while (r.next(f)) fields_.push_back(f);
    ok_ = !r.failed();
    if (!ok_) fields_.clear();
}

const PbField *PbMsg::find(uint32_t field) const {
    for (size_t i = fields_.size(); i-- > 0;)
        if (fields_[i].number == field) return &fields_[i];
    return nullptr;
}

uint64_t PbMsg::u(uint32_t field, uint64_t dflt) const {
    const PbField *f = find(field);
    return f && f->wire != PbWire::Bytes ? f->value : dflt;
}

std::span<const uint8_t> PbMsg::bytes(uint32_t field) const {
    const PbField *f = find(field);
    return f && f->wire == PbWire::Bytes ? f->bytes : std::span<const uint8_t>{};
}

std::string_view PbMsg::str(uint32_t field) const {
    auto b = bytes(field);
    return {reinterpret_cast<const char *>(b.data()), b.size()};
}

}  // namespace bbm
