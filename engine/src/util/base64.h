#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace bbm {
// Accepts the standard (+/) and url-safe (-_) alphabets, padded or not, mixed freely.
// False on any other character or an impossible length.
bool base64_decode(std::string_view text, std::vector<uint8_t> &out);
std::string base64_encode(const uint8_t *data, size_t size);                 // standard, padded
std::string base64url_encode(const uint8_t *data, size_t size, bool pad = false);  // url-safe
}  // namespace bbm
