#pragma once

#include "core/error.hpp"
#include "job/job.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <giomm/cancellable.h>

namespace filescope::fs {

enum class Confirmation : std::uint8_t { NONE, CONFIRMED };

enum class ConflictPolicy : std::uint8_t { FAIL, SKIP, KEEP_BOTH, OVERWRITE };

// [any] Creates one file without replacing an existing entry.
core::Result<std::string> create_file(const std::string &dir, const std::string &name);
// [any] Creates one directory without replacing an existing entry.
core::Result<std::string> create_dir(const std::string &dir, const std::string &name);
// [any] Renames an entry within its directory without replacing an existing entry.
core::Result<std::string> rename(const std::string &path, const std::string &new_name);
// [any] Moves one entry to the desktop trash; unsupported trash never deletes permanently.
core::Result<void> trash(const std::string &path,
                         const Glib::RefPtr<Gio::Cancellable> &cancellable = {});

// [any] Returns source paths whose basenames already exist in dest_dir.
core::Result<std::vector<std::string>> find_conflicts(const std::vector<std::string> &sources,
                                                      const std::string &dest_dir);

inline constexpr std::size_t kTextMaxBytes = std::size_t{4} * 1024U * 1024U;

struct FileStamp {
    std::uint64_t dev = 0;
    std::uint64_t ino = 0;
    std::uint64_t size = 0;
    std::int64_t mtime_ns = 0;

    bool operator==(const FileStamp &other) const noexcept;
    bool operator!=(const FileStamp &other) const noexcept;
};

struct TextFile {
    std::string contents;
    FileStamp stamp;
};

// [any] Reads at most kTextMaxBytes from a regular file.
core::Result<TextFile> read_text(const std::string &path);
// [any] Atomically replaces a text file if its stamp is unchanged, preserving its mode.
core::Result<FileStamp> write_text(const std::string &path, const std::string &contents,
                                   const std::optional<FileStamp> &expected,
                                   Confirmation confirmation);

// [any] Atomically installs a regular file after checking the expected target stamp.
core::Result<FileStamp> install_file(const std::string &target, const std::string &source,
                                     const std::optional<FileStamp> &expected,
                                     std::optional<std::uint32_t> mode,
                                     const Glib::RefPtr<Gio::Cancellable> &cancellable = {});
// [any] Recursively removes an entry after confirmation and returns the first error.
core::Result<void> remove_tree(const std::string &path, Confirmation confirmation,
                               const Glib::RefPtr<Gio::Cancellable> &cancellable = {});

// [main] Submits recursive copies after validating all paths.
core::Result<job::JobId> copy(job::JobQueue &queue, const std::vector<std::string> &sources,
                              const std::string &dest_dir, ConflictPolicy policy,
                              Confirmation confirmation);
// [main] Submits moves, using rename on the same filesystem and copy/delete on EXDEV.
core::Result<job::JobId> move(job::JobQueue &queue, const std::vector<std::string> &sources,
                              const std::string &dest_dir, ConflictPolicy policy,
                              Confirmation confirmation);
// [main] Submits multiple independent trash operations.
core::Result<job::JobId> trash_many(job::JobQueue &queue, const std::vector<std::string> &paths);
// [main] Submits recursive permanent deletion only after explicit confirmation.
core::Result<job::JobId> delete_permanently(job::JobQueue &queue,
                                            const std::vector<std::string> &paths,
                                            Confirmation confirmation);

} // namespace filescope::fs
