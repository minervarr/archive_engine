#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace ae {

// Converts bytes to a lowercase hexadecimal string.
std::string to_hex(const std::uint8_t *bytes, std::size_t len);

// Computes the MD5 digest of the input (self-contained, RFC 1321).
std::array<std::uint8_t, 16> md5(std::string_view input);

// Convenience: lowercase hex string of the MD5 digest.
std::string md5_hex(std::string_view input);

} // namespace ae
