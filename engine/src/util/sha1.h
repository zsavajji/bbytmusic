#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace bbm {
std::string sha1_hex(const uint8_t *data, size_t size);  // 40 lowercase hex digits
inline std::string sha1_hex(std::string_view s) { return sha1_hex(reinterpret_cast<const uint8_t *>(s.data()), s.size()); }
}  // namespace bbm
