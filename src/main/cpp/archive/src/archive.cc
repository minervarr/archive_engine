#include "ae/archive.hh"

#include <cerrno>
#include <cstdio>

#include <archive.h>
#include <archive_entry.h>

#include "ae/log.hh"

namespace ae {

namespace {

Error archive_err(ErrorKind kind, const char *ctx, const char *detail) {
    std::string msg = std::string(ctx) + ": " + (detail ? detail : "unknown");
    AE_LOGE("%s", msg.c_str());
    return Error{kind, 0, std::move(msg)};
}

int copy_data(archive *src, archive *dst) {
    const void *buf;
    size_t size;
    la_int64_t offset;
    for (;;) {
        int r = archive_read_data_block(src, &buf, &size, &offset);
        if (r == ARCHIVE_EOF) return ARCHIVE_OK;
        if (r < ARCHIVE_OK) return r;
        if (archive_write_data_block(dst, buf, size, offset) < ARCHIVE_OK) {
            return ARCHIVE_FATAL;
        }
    }
}

} // namespace

Result<void> archive_extract(const std::string &archive_path, const std::string &dest_dir) {
    AE_LOGI("extract src=%s dest=%s", archive_path.c_str(), dest_dir.c_str());

    archive *a = archive_read_new();
    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);

    archive *out = archive_write_disk_new();
    archive_write_disk_set_options(out, ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM |
                                            ARCHIVE_EXTRACT_SECURE_NODOTDOT);
    archive_write_disk_set_standard_lookup(out);

    if (archive_read_open_filename(a, archive_path.c_str(), 16384) != ARCHIVE_OK) {
        Error e = archive_err(ErrorKind::Io, "archive_read_open_filename", archive_error_string(a));
        archive_read_free(a);
        archive_write_free(out);
        return e;
    }

    archive_entry *entry;
    Result<void> result;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        std::string entry_path = dest_dir + "/" + archive_entry_pathname(entry);
        archive_entry_set_pathname(entry, entry_path.c_str());

        if (archive_write_header(out, entry) != ARCHIVE_OK) {
            result = archive_err(ErrorKind::Io, "archive_write_header", archive_error_string(out));
            break;
        }
        if (copy_data(a, out) != ARCHIVE_OK) {
            result = archive_err(ErrorKind::Io, "copy_data", archive_error_string(out));
            break;
        }
    }

    archive_read_free(a);
    archive_write_free(out);
    if (result.ok()) AE_LOGI("extract done");
    return result;
}

Result<void> archive_compress(const std::vector<std::string> &src_paths,
                              const std::string &dest_path, ArchiveFormat format) {
    AE_LOGI("compress dest=%s", dest_path.c_str());

    archive *a = archive_write_new();
    if (format == ArchiveFormat::Zip) {
        archive_write_set_format_zip(a);
    } else {
        archive_write_set_format_gnutar(a);
        archive_write_add_filter_gzip(a);
    }

    if (archive_write_open_filename(a, dest_path.c_str()) != ARCHIVE_OK) {
        Error e = archive_err(ErrorKind::Io, "archive_write_open_filename", archive_error_string(a));
        archive_write_free(a);
        return e;
    }

    Result<void> result;
    for (const auto &src : src_paths) {
        archive *disk = archive_read_disk_new();
        archive_read_disk_set_standard_lookup(disk);

        if (archive_read_disk_open(disk, src.c_str()) != ARCHIVE_OK) {
            result = archive_err(ErrorKind::Io, "archive_read_disk_open", archive_error_string(disk));
            archive_read_free(disk);
            break;
        }

        archive_entry *entry = archive_entry_new();
        int r;
        while ((r = archive_read_next_header2(disk, entry)) == ARCHIVE_OK) {
            if (archive_entry_filetype(entry) == AE_IFDIR) {
                archive_read_disk_descend(disk);
            }

            if (archive_write_header(a, entry) != ARCHIVE_OK) {
                result = archive_err(ErrorKind::Io, "archive_write_header", archive_error_string(a));
                break;
            }

            if (archive_entry_filetype(entry) == AE_IFREG) {
                const char *path = archive_entry_sourcepath(entry);
                if (path) {
                    FILE *f = std::fopen(path, "rb");
                    if (f) {
                        char buf[65536];
                        size_t n;
                        while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) {
                            archive_write_data(a, buf, n);
                        }
                        std::fclose(f);
                    } else {
                        AE_LOGW("fopen failed for %s errno=%d", path, errno);
                    }
                }
            }
        }
        archive_entry_free(entry);
        archive_read_free(disk);
        if (!result.ok()) break;
    }

    archive_write_close(a);
    archive_write_free(a);
    if (result.ok()) AE_LOGI("compress done dest=%s", dest_path.c_str());
    return result;
}

} // namespace ae
