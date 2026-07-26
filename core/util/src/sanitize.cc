#include "ae/sanitize.hh"

namespace ae {

namespace {

bool is_invalid_filename_char(unsigned char c) {
    switch (c) {
    case '/':
    case '\\':
    case ':':
    case '*':
    case '?':
    case '"':
    case '<':
    case '>':
    case '|':
        return true;
    default:
        return c <= 0x1F;
    }
}

bool is_ascii_space(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

std::string_view trim_matches(std::string_view s, bool (*pred)(unsigned char)) {
    while (!s.empty() && pred(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && pred(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return s;
}

} // namespace

std::string sanitize_filename(std::string_view name) {
    std::string sanitized;
    sanitized.reserve(name.size());
    for (char ch : name) {
        unsigned char c = static_cast<unsigned char>(ch);
        sanitized.push_back(is_invalid_filename_char(c) ? '_' : ch);
    }

    std::string_view trimmed = trim_matches(sanitized, is_ascii_space);
    trimmed = trim_matches(trimmed, [](unsigned char c) { return c == '.'; });
    trimmed = trim_matches(trimmed, [](unsigned char c) { return c == ' '; });

    if (trimmed.empty()) {
        return "unnamed";
    }
    return std::string(trimmed);
}

} // namespace ae
