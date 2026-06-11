#include "ae/hash.hh"

#include <cstring>

namespace ae {

std::string to_hex(const std::uint8_t *bytes, std::size_t len) {
    static const char kHexChars[] = "0123456789abcdef";
    std::string s;
    s.reserve(len * 2);
    for (std::size_t i = 0; i < len; ++i) {
        s.push_back(kHexChars[bytes[i] >> 4]);
        s.push_back(kHexChars[bytes[i] & 0x0F]);
    }
    return s;
}

namespace {

// Per-round shift amounts (RFC 1321).
constexpr std::uint32_t kShift[64] = {
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
    5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
    6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};

// Precomputed table: floor(2^32 * abs(sin(i + 1))).
constexpr std::uint32_t kSine[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a,
    0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
    0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340,
    0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8,
    0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
    0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa,
    0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92,
    0xffeff47d, 0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
    0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};

inline std::uint32_t rotl(std::uint32_t x, std::uint32_t n) {
    return (x << n) | (x >> (32 - n));
}

void process_block(const std::uint8_t *block, std::uint32_t state[4]) {
    std::uint32_t m[16];
    for (int i = 0; i < 16; ++i) {
        std::memcpy(&m[i], block + i * 4, 4); // little-endian load
    }

    std::uint32_t a = state[0], b = state[1], c = state[2], d = state[3];

    for (int i = 0; i < 64; ++i) {
        std::uint32_t f;
        int g;
        if (i < 16) {
            f = (b & c) | (~b & d);
            g = i;
        } else if (i < 32) {
            f = (d & b) | (~d & c);
            g = (5 * i + 1) % 16;
        } else if (i < 48) {
            f = b ^ c ^ d;
            g = (3 * i + 5) % 16;
        } else {
            f = c ^ (b | ~d);
            g = (7 * i) % 16;
        }
        std::uint32_t tmp = d;
        d = c;
        c = b;
        b = b + rotl(a + f + kSine[i] + m[g], kShift[i]);
        a = tmp;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
}

} // namespace

std::array<std::uint8_t, 16> md5(std::string_view input) {
    std::uint32_t state[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};

    const auto *data = reinterpret_cast<const std::uint8_t *>(input.data());
    std::size_t len = input.size();

    std::size_t full_blocks = len / 64;
    for (std::size_t i = 0; i < full_blocks; ++i) {
        process_block(data + i * 64, state);
    }

    // Final block(s): remaining bytes + 0x80 padding + 64-bit bit length.
    std::uint8_t tail[128] = {0};
    std::size_t rem = len - full_blocks * 64;
    std::memcpy(tail, data + full_blocks * 64, rem);
    tail[rem] = 0x80;

    std::size_t tail_len = (rem < 56) ? 64 : 128;
    std::uint64_t bit_len = static_cast<std::uint64_t>(len) * 8;
    std::memcpy(tail + tail_len - 8, &bit_len, 8); // little-endian store

    process_block(tail, state);
    if (tail_len == 128) {
        process_block(tail + 64, state);
    }

    std::array<std::uint8_t, 16> digest{};
    std::memcpy(digest.data(), state, 16);
    return digest;
}

std::string md5_hex(std::string_view input) {
    auto digest = md5(input);
    return to_hex(digest.data(), digest.size());
}

} // namespace ae
