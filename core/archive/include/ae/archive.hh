#pragma once

#include <string>
#include <vector>

#include "ae/result.hh"

namespace ae {

// Output container format for compression.
enum class ArchiveFormat {
    TarGz,
    Zip,
};

// Extracts an archive (tar/tar.gz/tar.bz2/tar.xz/zip, auto-detected) into
// dest_dir, which must already exist.
Result<void> archive_extract(const std::string &archive_path, const std::string &dest_dir);

// Compresses one or more source paths (files or directories) into dest_path.
Result<void> archive_compress(const std::vector<std::string> &src_paths,
                              const std::string &dest_path, ArchiveFormat format);

} // namespace ae
