#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace ae {

// Standard base64 (RFC 4648) with '=' padding.
std::string base64_encode(std::string_view input);

// Decodes standard base64; whitespace is skipped, padding optional.
// Returns std::nullopt on invalid input.
std::optional<std::string> base64_decode(std::string_view input);

} // namespace ae
