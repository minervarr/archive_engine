#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ae/result.hh"

namespace ae {

struct CoverArt {
    std::string mime_type; // e.g. "image/jpeg"
    std::vector<uint8_t> data;
};

// Generic tag payload. Field names use Vorbis-style uppercase keys
// ("TITLE", "ARTIST", "ALBUM", "ALBUMARTIST", "COMPOSER", "GENRE", "DATE",
// "TRACKNUMBER", "DISCNUMBER", "ISRC", ...). TagLib maps them to the
// container's native form: Vorbis Comments on FLAC, ID3v2 frames on MP3
// (unknown keys become TXXX frames). Repeating a key produces a
// multi-valued field where the container supports it (FLAC).
struct TagData {
    std::vector<std::pair<std::string, std::string>> fields;
    std::optional<CoverArt> cover;
    // When true (default), existing tags and pictures are replaced wholesale,
    // matching the Rust embedder's tag.clear() semantics.
    bool clear_existing = true;
};

// Writes tags to an audio file (format auto-detected by TagLib). Saves are
// retried 3x with 250ms/500ms backoff to ride out transient I/O failures.
Result<void> write_tags(const std::string &path, const TagData &tags);

} // namespace ae
