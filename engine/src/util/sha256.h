#pragma once
#include <string>
#include <string_view>

namespace bbm {
std::string sha256_hex(std::string_view data);  // 64 lowercase hex digits
}  // namespace bbm
