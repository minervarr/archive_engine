#pragma once

#include <string>
#include <string_view>

namespace ae {

// Sanitizes a string for use as a file or directory name.
//
// Replaces characters that are invalid in filenames on Windows, macOS, and
// Linux with underscores, then trims leading/trailing whitespace and dots.
// Returns "unnamed" if the result would be empty. UTF-8 safe: only ASCII
// bytes are ever replaced.
std::string sanitize_filename(std::string_view name);

} // namespace ae
