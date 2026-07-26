#include "ae/base64.hh"

#include <array>
#include <cctype>
#include <cstdint>

namespace ae {

namespace {

constexpr char kAlphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::array<std::int8_t, 256> build_reverse_table() {
    std::array<std::int8_t, 256> table{};
    table.fill(-1);
    for (int i = 0; i < 64; ++i) {
        table[static_cast<unsigned char>(kAlphabet[i])] = static_cast<std::int8_t>(i);
    }
    return table;
}

} // namespace

std::string base64_encode(std::string_view input) {
    std::string out;
    out.reserve((input.size() + 2) / 3 * 4);

    std::size_t i = 0;
    while (i + 3 <= input.size()) {
        std::uint32_t n = (static_cast<unsigned char>(input[i]) << 16) |
                          (static_cast<unsigned char>(input[i + 1]) << 8) |
                          static_cast<unsigned char>(input[i + 2]);
        out.push_back(kAlphabet[(n >> 18) & 63]);
        out.push_back(kAlphabet[(n >> 12) & 63]);
        out.push_back(kAlphabet[(n >> 6) & 63]);
        out.push_back(kAlphabet[n & 63]);
        i += 3;
    }

    std::size_t rem = input.size() - i;
    if (rem == 1) {
        std::uint32_t n = static_cast<unsigned char>(input[i]) << 16;
        out.push_back(kAlphabet[(n >> 18) & 63]);
        out.push_back(kAlphabet[(n >> 12) & 63]);
        out.append("==");
    } else if (rem == 2) {
        std::uint32_t n = (static_cast<unsigned char>(input[i]) << 16) |
                          (static_cast<unsigned char>(input[i + 1]) << 8);
        out.push_back(kAlphabet[(n >> 18) & 63]);
        out.push_back(kAlphabet[(n >> 12) & 63]);
        out.push_back(kAlphabet[(n >> 6) & 63]);
        out.push_back('=');
    }
    return out;
}

std::optional<std::string> base64_decode(std::string_view input) {
    static const std::array<std::int8_t, 256> kReverse = build_reverse_table();

    std::string out;
    out.reserve(input.size() / 4 * 3);

    std::uint32_t buffer = 0;
    int bits = 0;
    for (char ch : input) {
        unsigned char c = static_cast<unsigned char>(ch);
        if (std::isspace(c)) continue;
        if (c == '=') break; // padding: remaining chars are ignored
        std::int8_t v = kReverse[c];
        if (v < 0) return std::nullopt;
        buffer = (buffer << 6) | static_cast<std::uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((buffer >> bits) & 0xFF));
        }
    }
    return out;
}

} // namespace ae
