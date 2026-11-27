#include "fs/fs.hpp"

#include "core/fd.hpp"
#include "core/path.hpp"

#include <array>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <limits>
#include <memory>
#include <optional>
#include <string_view>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <unordered_map>
#include <utility>

#include <giomm/file.h>
#include <glib.h>

namespace filescope::fs {
namespace {

constexpr std::size_t kCopyBufferBytes = std::size_t{64} * 1024U;
std::atomic<int> next_write_error{0};
std::atomic<void (*)(job::Job &)> before_copy_write{nullptr};
std::atomic<void (*)(const std::string &)> before_source_removal{nullptr};
std::atomic<bool> force_cross_device{false};

struct Snapshot {
    std::string path;
    mode_t type = 0;
};

struct FileStatus {
    mode_t mode = 0;
    dev_t dev = 0;
    ino_t ino = 0;
    off_t size = 0;
    timespec access_time{};
    timespec modification_time{};
};

FileStatus file_status_from_stat(const struct stat &status) {
    return {status.st_mode, status.st_dev,  status.st_ino,
            status.st_size, status.st_atim, status.st_mtim};
}

struct DirectoryCloser {
    void operator()(DIR *directory) const noexcept { closedir(directory); }
};

class TemporaryPath {
  public:
    explicit TemporaryPath(std::string path, int parent = AT_FDCWD)
        : path_(std::move(path)), parent_(parent) {}
    ~TemporaryPath() {
        if (!path_.empty()) {
            unlinkat(parent_, path_.c_str(), 0);
        }
    }
    TemporaryPath(const TemporaryPath &) = delete;
    TemporaryPath &operator=(const TemporaryPath &) = delete;

    const std::string &get() const noexcept { return path_; }
    void dismiss() noexcept { path_.clear(); }

  private:
    std::string path_;
    int parent_;
};

core::Error cancelled_error() {
    return core::make_error(core::ErrorCode::CANCELLED, "File operation cancelled");
}

core::Result<void> validate_write_path(const std::string &path, core::PathOp operation) {
    if (const auto valid = core::validate_absolute(path); !valid) {
        return valid.error();
    }
    return core::check_protected(path, operation);
}

std::string parent_path(const std::string &path) {
    const std::size_t slash = path.find_last_of('/');
    return slash == 0 ? "/" : path.substr(0, slash);
}

std::string base_name(const std::string &path) {
    const std::size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

struct Entry {
    std::string path;
    int parent = AT_FDCWD;

    std::string name() const { return parent == AT_FDCWD ? path : base_name(path); }

    Entry child(const char *name, int fd) const { return {core::path_join(path, name), fd}; }
};

core::Result<void> read_status(const Entry &entry, FileStatus &result) {
    struct stat status {};
    if (fstatat(entry.parent, entry.name().c_str(), &status, AT_SYMLINK_NOFOLLOW) != 0) {
        const int saved_errno = errno;
        return core::error_from_errno(saved_errno, "fstatat", entry.path);
    }
    result = file_status_from_stat(status);
    return {};
}

FileStamp stamp_from_stat(const FileStatus &status) {
    return {static_cast<std::uint64_t>(status.dev), static_cast<std::uint64_t>(status.ino),
            static_cast<std::uint64_t>(status.size),
            static_cast<std::int64_t>(status.modification_time.tv_sec) * 1000000000LL +
                status.modification_time.tv_nsec};
}

bool same_type(mode_t first, mode_t second) noexcept {
    return (first & S_IFMT) == (second & S_IFMT);
}

struct CopiedEntries {
    struct Item {
        mode_t type;
        FileStamp stamp;
    };

    std::size_t root_length;
    std::unordered_map<std::string, Item> items;

    std::string relative(const Entry &entry) const {
        const bool has_separator =
            entry.path.size() > root_length && entry.path[root_length] == '/';
        return entry.path.substr(root_length + static_cast<std::size_t>(has_separator));
    }
};

bool is_fatal(core::ErrorCode code) noexcept {
    return code == core::ErrorCode::NO_SPACE || code == core::ErrorCode::CANCELLED;
}

core::Result<void> validate_sources(const std::vector<std::string> &paths, core::PathOp operation,
                                    std::vector<Snapshot> &snapshots) {
    snapshots.clear();
    snapshots.reserve(paths.size());
    for (const auto &path : paths) {
        if (const auto safe = validate_write_path(path, operation); !safe) {
            return safe.error();
        }
        FileStatus status;
        if (const auto read = read_status({path}, status); !read) {
            return read.error();
        }
        snapshots.push_back({path, status.mode & S_IFMT});
    }
    return {};
}

core::Result<void> validate_destination(const std::string &dest_dir) {
    if (const auto safe = validate_write_path(dest_dir, core::PathOp::WRITE); !safe) {
        return safe.error();
    }
    FileStatus status;
    if (const auto read = read_status({dest_dir}, status); !read) {
        return read.error();
    }
    if (!S_ISDIR(status.mode)) {
        return core::make_error(core::ErrorCode::NOT_DIRECTORY, "Destination is not a directory");
    }
    return {};
}

core::Result<void> revalidate_source(const Snapshot &snapshot, core::PathOp operation) {
    struct stat status {};
    if (lstat(snapshot.path.c_str(), &status) != 0) {
        const int saved_errno = errno;
        return core::error_from_errno(saved_errno, "lstat", snapshot.path);
    }
    if (!same_type(status.st_mode, snapshot.type)) {
        return core::make_error(core::ErrorCode::CONFLICT, "Source type changed after submission");
    }
    return validate_write_path(snapshot.path, operation);
}

core::Result<void> write_bytes(int fd, const char *data, std::size_t size,
                               const std::string &path) {
    std::size_t written = 0;
    while (written < size) {
        const ssize_t count = write(fd, data + written, size - written);
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            const int saved_errno = errno;
            return core::error_from_errno(saved_errno, "write", path);
        }
        written += static_cast<std::size_t>(count);
    }
    return {};
}

core::Result<void> write_all(int fd, const char *data, std::size_t size, const std::string &path) {
    const int injected = next_write_error.exchange(0);
    if (injected != 0) {
        return core::make_error(static_cast<core::ErrorCode>(injected),
                                "Injected file write failure");
    }
    return write_bytes(fd, data, size, path);
}

core::Result<void> rename_entry(const Entry &source, const Entry &target, bool replace) {
    const std::string source_name = source.name();
    const std::string target_name = target.name();
    const long result =
        replace ? renameat(source.parent, source_name.c_str(), target.parent, target_name.c_str())
                : syscall(SYS_renameat2, source.parent, source_name.c_str(), target.parent,
                          target_name.c_str(), RENAME_NOREPLACE);
    if (result != 0) {
        return core::error_from_errno(errno, "rename", target.path);
    }
    return {};
}

core::Result<void> rename_into_place(const std::string &source, const std::string &target,
                                     bool replace) {
    return rename_entry({source}, {target}, replace);
}

std::string temporary_template(const std::string &directory) {
    return core::path_join(directory, ".filescope-XXXXXX");
}

core::UniqueFd open_directory(const Entry &entry) {
    return core::UniqueFd(openat(entry.parent, entry.name().c_str(),
                                 O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
}

struct Removal {
    job::Job *job = nullptr;
    const CopiedEntries *copied = nullptr;
    bool private_copy = false;
    const Gio::Cancellable *cancellable = nullptr;
    bool continue_on_error = false;

    bool is_cancelled() const {
        return (job != nullptr && job->is_cancelled()) ||
               (cancellable != nullptr && cancellable->is_cancelled());
    }
};

core::Error move_conflict() {
    return core::make_error(core::ErrorCode::CONFLICT, "Source changed during move");
}

core::Result<void> remove_entry(const Entry &entry, const Removal &removal);

core::Result<void> remove_directory(const Entry &path, const FileStatus &status,
                                    const Removal &removal, const FileStamp *copied_stamp) {
    auto fd = open_directory(path);
    if (!fd) {
        return core::error_from_errno(errno, "open directory", path.path);
    }
    struct stat opened {};
    if (fstat(fd.get(), &opened) != 0) {
        return core::error_from_errno(errno, "fstat directory", path.path);
    }
    if (opened.st_dev != status.dev || opened.st_ino != status.ino) {
        return core::make_error(core::ErrorCode::CONFLICT, "Directory changed during removal");
    }
    if (removal.private_copy && fchmod(fd.get(), 0700) != 0) {
        return core::error_from_errno(errno, "fchmod private copy", path.path);
    }
    // A changed directory can still contain unchanged children, but must itself remain.
    const bool changed =
        copied_stamp != nullptr && *copied_stamp != stamp_from_stat(file_status_from_stat(opened));
    std::optional<core::Error> child_error;
    std::unique_ptr<DIR, DirectoryCloser> directory(fdopendir(fd.get()));
    if (!directory) {
        return core::error_from_errno(errno, "fdopendir", path.path);
    }
    static_cast<void>(fd.release());
    while (true) {
        errno = 0;
        const dirent *entry = readdir(directory.get());
        if (entry == nullptr) {
            if (errno != 0) {
                if (removal.continue_on_error && child_error) {
                    return *child_error;
                }
                return core::error_from_errno(errno, "readdir", path.path);
            }
            break;
        }
        const std::string_view name(entry->d_name);
        if (name == "." || name == "..") {
            continue;
        }
        const Entry child = path.child(entry->d_name, dirfd(directory.get()));
        if (const auto removed = remove_entry(child, removal); !removed) {
            if (removal.continue_on_error) {
                if (removed.error().code == core::ErrorCode::CANCELLED) {
                    return removed.error();
                }
                if (!child_error) {
                    child_error = removed.error();
                }
                continue;
            }
            if (removal.copied == nullptr || is_fatal(removed.error().code)) {
                return removed.error();
            }
            removal.job->add_item_result({child.path, removed.error(), false});
            if (!child_error || removed.error().code == core::ErrorCode::CONFLICT) {
                child_error = removed.error();
            }
        }
    }
    if (changed) {
        return move_conflict();
    }
    if (child_error) {
        return *child_error;
    }
    if (removal.is_cancelled()) {
        return cancelled_error();
    }
    FileStatus current;
    if (const auto read = read_status(path, current); !read) {
        return read.error();
    }
    if (current.dev != status.dev || current.ino != status.ino) {
        return core::make_error(core::ErrorCode::CONFLICT, "Directory changed during removal");
    }
    if (unlinkat(path.parent, path.name().c_str(), AT_REMOVEDIR) != 0) {
        if (removal.copied != nullptr && errno == ENOTEMPTY) {
            return move_conflict();
        }
        return core::error_from_errno(errno, "unlinkat directory", path.path);
    }
    return {};
}

core::Result<void> remove_entry(const Entry &path, const Removal &removal) {
    if (removal.is_cancelled()) {
        return cancelled_error();
    }
    FileStatus status;
    if (const auto read = read_status(path, status); !read) {
        return read.error();
    }
    const FileStamp *copied_stamp = nullptr;
    if (removal.copied != nullptr) {
        const auto &items = removal.copied->items;
        const auto item = items.find(removal.copied->relative(path));
        const FileStamp current = stamp_from_stat(status);
        if (item == items.end() || !same_type(item->second.type, status.mode) ||
            item->second.stamp.dev != current.dev || item->second.stamp.ino != current.ino ||
            (!S_ISDIR(status.mode) && item->second.stamp != current)) {
            return move_conflict();
        }
        copied_stamp = &item->second.stamp;
    }
    if (S_ISDIR(status.mode)) {
        return remove_directory(path, status, removal, copied_stamp);
    }
    if (unlinkat(path.parent, path.name().c_str(), 0) != 0) {
        return core::error_from_errno(errno, "unlinkat", path.path);
    }
    return {};
}

struct ResolvedTarget {
    std::string path;
    bool skipped = false;
    bool replace = false;
};

core::Result<ResolvedTarget> keep_both_path(const std::string &target, const job::Job &job) {
    const std::string directory = parent_path(target);
    const std::string name = base_name(target);
    const std::size_t dot = name.find_last_of('.');
    const bool has_extension = dot != std::string::npos && dot != 0;
    const std::string stem = has_extension ? name.substr(0, dot) : name;
    const std::string extension = has_extension ? name.substr(dot) : "";
    for (unsigned index = 1;; ++index) {
        if (job.is_cancelled()) {
            return cancelled_error();
        }
        std::string candidate_name = stem;
        candidate_name += " (";
        candidate_name += std::to_string(index);
        candidate_name += ')';
        candidate_name += extension;
        const std::string candidate = core::path_join(directory, candidate_name);
        struct stat ignored {};
        if (lstat(candidate.c_str(), &ignored) != 0) {
            if (errno == ENOENT) {
                return ResolvedTarget{candidate};
            }
            return core::error_from_errno(errno, "lstat", candidate);
        }
    }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): source and target follow the operation.
core::Result<ResolvedTarget> resolve_target(const std::string &source, const std::string &target,
                                            ConflictPolicy policy, Confirmation confirmation,
                                            const job::Job &job) {
    struct stat target_status {};
    if (lstat(target.c_str(), &target_status) != 0) {
        if (errno == ENOENT) {
            return ResolvedTarget{target, false};
        }
        const int saved_errno = errno;
        return core::error_from_errno(saved_errno, "lstat", target);
    }
    switch (policy) {
    case ConflictPolicy::FAIL:
        return core::make_error(core::ErrorCode::EXISTS, "Destination already exists");
    case ConflictPolicy::SKIP:
        return ResolvedTarget{target, true};
    case ConflictPolicy::KEEP_BOTH:
        return keep_both_path(target, job);
    case ConflictPolicy::OVERWRITE: {
        if (confirmation != Confirmation::CONFIRMED) {
            return core::make_error(core::ErrorCode::NOT_CONFIRMED,
                                    "Overwrite requires confirmation");
        }
        FileStatus source_status;
        if (const auto read = read_status({source}, source_status); !read) {
            return read.error();
        }
        if (!same_type(source_status.mode, target_status.st_mode)) {
            return core::make_error(core::ErrorCode::CONFLICT,
                                    "Cannot overwrite an entry of a different type");
        }
        return ResolvedTarget{target, false, true};
    }
    }
    return core::make_error(core::ErrorCode::FAILED, "Unknown conflict policy");
}

Entry temporary_sibling(const Entry &target) {
    return {core::path_join(parent_path(target.path), ".filescope-" +
                                                          std::to_string(g_random_int()) + "-" +
                                                          std::to_string(g_random_int())),
            target.parent};
}

core::Result<void> copy_entry(const Entry &source, const Entry &target, bool replace, job::Job &job,
                              CopiedEntries *copied = nullptr);

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): source and target follow copy.
core::Result<void> copy_regular(const Entry &source, const Entry &target, const FileStatus &status,
                                bool replace, job::Job &job) {
    core::UniqueFd source_fd(openat(source.parent, source.name().c_str(),
                                    O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC));
    if (!source_fd) {
        const int saved_errno = errno;
        return core::error_from_errno(saved_errno, "open", source.path);
    }

    struct stat opened {};
    if (fstat(source_fd.get(), &opened) != 0) {
        return core::error_from_errno(errno, "fstat", source.path);
    }
    if (opened.st_dev != status.dev || opened.st_ino != status.ino) {
        return core::make_error(core::ErrorCode::CONFLICT, "Source changed during copy");
    }

    const Entry temporary = temporary_sibling(target);
    const std::string temporary_name = temporary.name();
    core::UniqueFd target_fd(openat(temporary.parent, temporary_name.c_str(),
                                    O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600));
    if (!target_fd) {
        return core::error_from_errno(errno, "open temporary file", temporary.path);
    }
    TemporaryPath cleanup(temporary_name, temporary.parent);
    if (fchmod(target_fd.get(), status.mode & 07777) != 0) {
        const int saved_errno = errno;
        return core::error_from_errno(saved_errno, "fchmod", temporary.path);
    }

    std::array<char, kCopyBufferBytes> buffer{};
    while (true) {
        if (job.is_cancelled()) {
            return cancelled_error();
        }
        const ssize_t count = read(source_fd.get(), buffer.data(), buffer.size());
        if (count == 0) {
            break;
        }
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            const int saved_errno = errno;
            return core::error_from_errno(saved_errno, "read", source.path);
        }
        if (const auto hook = before_copy_write.load(); hook != nullptr) {
            hook(job);
        }
        if (const auto written = write_all(target_fd.get(), buffer.data(),
                                           static_cast<std::size_t>(count), temporary.path);
            !written) {
            return written.error();
        }
    }
    const std::array<timespec, 2> times = {status.access_time, status.modification_time};
    if (futimens(target_fd.get(), times.data()) != 0 || fsync(target_fd.get()) != 0) {
        const int saved_errno = errno;
        return core::error_from_errno(saved_errno, "sync copied file", temporary.path);
    }
    if (job.is_cancelled()) {
        return cancelled_error();
    }
    target_fd.reset();
    if (const auto renamed = rename_entry(temporary, target, replace); !renamed) {
        return renamed.error();
    }
    cleanup.dismiss();
    return {};
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): source and target follow copy.
core::Result<void> copy_symlink(const Entry &source, const Entry &target, const FileStatus &status,
                                bool replace) {
    std::vector<char> link(static_cast<std::size_t>(status.size) + 2U);
    const ssize_t length =
        readlinkat(source.parent, source.name().c_str(), link.data(), link.size() - 1U);
    if (length < 0) {
        const int saved_errno = errno;
        return core::error_from_errno(saved_errno, "readlink", source.path);
    }
    if (static_cast<std::size_t>(length) == link.size() - 1U) {
        return core::make_error(core::ErrorCode::CONFLICT, "Symlink changed during copy");
    }
    link[static_cast<std::size_t>(length)] = '\0';
    const Entry temporary = temporary_sibling(target);
    const std::string temporary_name = temporary.name();
    if (symlinkat(link.data(), temporary.parent, temporary_name.c_str()) != 0) {
        return core::error_from_errno(errno, "symlinkat", temporary.path);
    }
    TemporaryPath cleanup(temporary_name, temporary.parent);
    const std::array<timespec, 2> times = {status.access_time, status.modification_time};
    if (utimensat(temporary.parent, temporary_name.c_str(), times.data(), AT_SYMLINK_NOFOLLOW) !=
        0) {
        const int saved_errno = errno;
        return core::error_from_errno(saved_errno, "preserve link metadata", temporary.path);
    }
    if (const auto renamed = rename_entry(temporary, target, replace); !renamed) {
        return renamed.error();
    }
    cleanup.dismiss();
    return {};
}

core::Result<void> copy_directory(const Entry &source, const Entry &target,
                                  const FileStatus &status, job::Job &job, CopiedEntries *copied) {
    auto fd = open_directory(source);
    if (!fd) {
        return core::error_from_errno(errno, "open directory", source.path);
    }
    struct stat opened {};
    if (fstat(fd.get(), &opened) != 0) {
        return core::error_from_errno(errno, "fstat directory", source.path);
    }
    if (opened.st_dev != status.dev || opened.st_ino != status.ino) {
        return core::make_error(core::ErrorCode::CONFLICT, "Directory changed during copy");
    }
    if (mkdirat(target.parent, target.name().c_str(), 0700) != 0) {
        return core::error_from_errno(errno, "mkdirat", target.path);
    }

    std::unique_ptr<DIR, DirectoryCloser> directory(fdopendir(fd.get()));
    if (!directory) {
        const int saved_errno = errno;
        return core::error_from_errno(saved_errno, "fdopendir", source.path);
    }
    static_cast<void>(fd.release());
    auto target_fd = open_directory(target);
    if (!target_fd) {
        return core::error_from_errno(errno, "open directory", target.path);
    }
    bool partial = false;
    while (true) {
        errno = 0;
        const dirent *entry = readdir(directory.get());
        if (entry == nullptr) {
            if (errno != 0) {
                return core::error_from_errno(errno, "readdir", source.path);
            }
            break;
        }
        const std::string_view name(entry->d_name);
        if (name == "." || name == "..") {
            continue;
        }
        if (job.is_cancelled()) {
            return cancelled_error();
        }
        const Entry child_source = source.child(entry->d_name, dirfd(directory.get()));
        const Entry child_target = target.child(entry->d_name, target_fd.get());
        const auto result = copy_entry(child_source, child_target, false, job, copied);
        if (!result) {
            partial = true;
            job.add_item_result({child_source.path, result.error(), false});
            if (is_fatal(result.error().code)) {
                return result.error();
            }
        }
    }
    const std::array<timespec, 2> times = {status.access_time, status.modification_time};
    if (fchmod(target_fd.get(), status.mode & 07777) != 0 ||
        futimens(target_fd.get(), times.data()) != 0) {
        const int saved_errno = errno;
        return core::error_from_errno(saved_errno, "preserve directory metadata", target.path);
    }
    if (partial) {
        return core::make_error(core::ErrorCode::PARTIAL, "Directory copy partially failed");
    }
    return {};
}

core::Result<void> copy_entry(const Entry &source, const Entry &target, bool replace, job::Job &job,
                              CopiedEntries *copied) {
    if (job.is_cancelled()) {
        return cancelled_error();
    }
    FileStatus status;
    if (const auto read = read_status(source, status); !read) {
        return read.error();
    }
    core::Result<void> result;
    if (S_ISREG(status.mode)) {
        result = copy_regular(source, target, status, replace, job);
    } else if (S_ISDIR(status.mode)) {
        result = copy_directory(source, target, status, job, copied);
    } else if (S_ISLNK(status.mode)) {
        result = copy_symlink(source, target, status, replace);
    } else {
        return core::make_error(core::ErrorCode::NOT_SUPPORTED, "Special files cannot be copied");
    }
    if (result && copied != nullptr) {
        copied->items.emplace(copied->relative(source),
                              CopiedEntries::Item{status.mode & S_IFMT, stamp_from_stat(status)});
    }
    return result;
}

template <typename Operation>
core::Result<void> replace_directory(const std::string &target, Operation operation) {
    FileStatus existing;
    if (const auto read = read_status({target}, existing); !read) {
        if (read.error().code == core::ErrorCode::NOT_FOUND) {
            return operation(std::string{});
        }
        return read.error();
    }
    if (!S_ISDIR(existing.mode)) {
        return core::make_error(core::ErrorCode::CONFLICT, "Destination type changed");
    }
    std::string staging = temporary_template(parent_path(target));
    if (mkdtemp(staging.data()) == nullptr) {
        return core::error_from_errno(errno, "mkdtemp", staging);
    }
    const std::string backup = core::path_join(staging, "original");
    if (const auto saved = rename_into_place(target, backup, false); !saved) {
        rmdir(staging.c_str());
        return saved.error();
    }

    FileStatus saved;
    const auto read = read_status({backup}, saved);
    auto result = read && saved.dev == existing.dev && saved.ino == existing.ino
                      ? operation(staging)
                      : core::Result<void>(
                            core::make_error(core::ErrorCode::CONFLICT, "Destination changed"));
    if (!result) {
        if (const auto restored = rename_into_place(backup, target, false); !restored) {
            return core::make_error(core::ErrorCode::PARTIAL,
                                    "Original destination preserved at " + backup);
        }
    } else if (const auto removed = remove_entry({backup}, {}); !removed) {
        return removed.error();
    }
    if (rmdir(staging.c_str()) != 0) {
        return core::error_from_errno(errno, "rmdir", staging);
    }
    return result;
}

core::Result<void> copy_into_directory_target(const std::string &staging, const Entry &source,
                                              const std::string &target, job::Job &job,
                                              CopiedEntries *copied = nullptr) {
    if (staging.empty()) {
        return copy_entry(source, {target}, false, job, copied);
    }
    const std::string incoming = core::path_join(staging, "incoming");
    auto result = copy_entry(source, {incoming}, false, job, copied);
    if (result && job.is_cancelled()) {
        result = cancelled_error();
    }
    if (result) {
        FileStatus status;
        const auto read = read_status({incoming}, status);
        if (!read) {
            result = read.error();
        } else if (!S_ISDIR(status.mode)) {
            result = move_conflict();
        } else {
            result = rename_into_place(incoming, target, false);
        }
    }
    if (!result) {
        const auto removed = remove_entry({incoming}, {nullptr, nullptr, true});
        if (!removed && removed.error().code != core::ErrorCode::NOT_FOUND) {
            return removed.error();
        }
    }
    return result;
}

core::Result<void> move_entry(const Snapshot &source, const std::string &target, bool replace,
                              job::Job &job) {
    FileStatus before;
    if (const auto read = read_status({source.path}, before); !read) {
        return read.error();
    }
    if (!same_type(before.mode, source.type)) {
        return move_conflict();
    }
    if (job.is_cancelled()) {
        return cancelled_error();
    }
    CopiedEntries copied{source.path.size(), {}};
    const bool directory = S_ISDIR(source.type);
    const auto move = [&](const std::string &staging) {
        auto renamed = force_cross_device.exchange(false)
                           ? core::Result<void>(core::error_from_errno(EXDEV, "rename", target))
                           : rename_into_place(source.path, target, replace && !directory);
        if (renamed || renamed.error().code != core::ErrorCode::CROSS_DEVICE) {
            return renamed;
        }
        if (directory) {
            return copy_into_directory_target(staging, {source.path}, target, job, &copied);
        }
        return copy_entry({source.path}, {target}, replace, job, &copied);
    };
    auto result = replace && directory ? replace_directory(target, move) : move({});
    if (!result || copied.items.empty()) {
        return result;
    }
    const auto &root = copied.items.at("");
    const FileStamp original = stamp_from_stat(before);
    if (!same_type(root.type, before.mode) || root.stamp.dev != original.dev ||
        root.stamp.ino != original.ino) {
        return move_conflict();
    }
    if (const auto hook = before_source_removal.exchange(nullptr); hook != nullptr) {
        hook(source.path);
    }
    return remove_entry({source.path}, {&job, &copied});
}

core::Result<void> process_copy(job::Job &job, const std::vector<Snapshot> &sources,
                                const std::string &dest_dir, ConflictPolicy policy,
                                Confirmation confirmation, bool moving) {
    if (const auto valid = validate_destination(dest_dir); !valid) {
        return valid.error();
    }
    job.report_total(-1, static_cast<std::int32_t>(sources.size()));
    std::uint32_t completed = 0;
    for (const auto &source : sources) {
        if (job.is_cancelled()) {
            return cancelled_error();
        }
        if (const auto valid =
                revalidate_source(source, moving ? core::PathOp::REMOVE : core::PathOp::READ);
            !valid) {
            job.add_item_result({source.path, valid.error(), false});
            ++completed;
            job.report_progress(0, completed, source.path);
            continue;
        }
        const std::string initial_target = core::path_join(dest_dir, base_name(source.path));
        const auto resolved =
            resolve_target(source.path, initial_target, policy, confirmation, job);
        if (!resolved) {
            if (resolved.error().code == core::ErrorCode::CANCELLED) {
                return resolved.error();
            }
            job.add_item_result({source.path, resolved.error(), false});
            ++completed;
            job.report_progress(0, completed, source.path);
            continue;
        }
        if (resolved.value().skipped) {
            job.add_item_result({source.path, std::nullopt, true});
            ++completed;
            job.report_progress(0, completed, source.path);
            continue;
        }

        core::Result<void> result;
        const bool replace = resolved.value().replace;
        if (moving) {
            result = move_entry(source, resolved.value().path, replace, job);
        } else if (replace && S_ISDIR(source.type)) {
            const auto &target = resolved.value().path;
            result = replace_directory(target, [&](const std::string &staging) {
                return copy_into_directory_target(staging, {source.path}, target, job);
            });
        } else {
            result = copy_entry({source.path}, {resolved.value().path}, replace, job);
        }

        std::optional<core::Error> item_error;
        if (!result) {
            item_error = result.error();
        }
        job.add_item_result({source.path, std::move(item_error), false});
        ++completed;
        job.report_progress(0, completed, source.path);
        if (!result && is_fatal(result.error().code)) {
            return result.error();
        }
    }
    return {};
}

core::Result<job::JobId> submit_copy(job::JobQueue &queue,
                                     const std::vector<std::string> &source_paths,
                                     const std::string &dest_dir, ConflictPolicy policy,
                                     Confirmation confirmation, bool moving) {
    if (policy == ConflictPolicy::OVERWRITE && confirmation != Confirmation::CONFIRMED) {
        return core::make_error(core::ErrorCode::NOT_CONFIRMED, "Overwrite requires confirmation");
    }
    if (const auto valid = validate_destination(dest_dir); !valid) {
        return valid.error();
    }
    std::vector<Snapshot> sources;
    if (const auto valid = validate_sources(
            source_paths, moving ? core::PathOp::REMOVE : core::PathOp::READ, sources);
        !valid) {
        return valid.error();
    }
    const std::string canonical_dest = core::canonicalize(dest_dir);
    for (const auto &source : sources) {
        const std::string canonical_source = core::canonicalize(source.path);
        const std::string target = core::path_join(canonical_dest, base_name(source.path));
        if (canonical_source == target ||
            (S_ISDIR(source.type) && core::is_ancestor(canonical_source, target))) {
            return core::make_error(core::ErrorCode::INVALID_PATH,
                                    "Destination cannot be the source or its descendant");
        }
    }
    return queue.submit(
        moving ? "Move files" : "Copy files",
        [snapshots = std::move(sources), dest_dir, policy, confirmation, moving](job::Job &job) {
            return process_copy(job, snapshots, dest_dir, policy, confirmation, moving);
        });
}

} // namespace

namespace testing {

void fail_next_write(core::ErrorCode code) noexcept {
    next_write_error.store(static_cast<int>(code));
}

void set_copy_write_hook(void (*hook)(job::Job &)) noexcept { before_copy_write.store(hook); }

void set_source_removal_hook(void (*hook)(const std::string &)) noexcept {
    before_source_removal.store(hook);
}

void force_next_cross_device_move() noexcept { force_cross_device.store(true); }

} // namespace testing

bool FileStamp::operator==(const FileStamp &other) const noexcept {
    return dev == other.dev && ino == other.ino && size == other.size && mtime_ns == other.mtime_ns;
}

bool FileStamp::operator!=(const FileStamp &other) const noexcept { return !(*this == other); }

core::Result<std::string> create_file(const std::string &dir, const std::string &name) {
    if (const auto valid = core::validate_absolute(dir); !valid) {
        return valid.error();
    }
    if (const auto valid = core::validate_name(name); !valid) {
        return valid.error();
    }
    const std::string path = core::path_join(dir, name);
    if (const auto safe = core::check_protected(path, core::PathOp::WRITE); !safe) {
        return safe.error();
    }
    core::UniqueFd fd(open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0666));
    if (!fd) {
        const int saved_errno = errno;
        return core::error_from_errno(saved_errno, "create file", path);
    }
    return path;
}

core::Result<std::string> create_dir(const std::string &dir, const std::string &name) {
    if (const auto valid = core::validate_absolute(dir); !valid) {
        return valid.error();
    }
    if (const auto valid = core::validate_name(name); !valid) {
        return valid.error();
    }
    const std::string path = core::path_join(dir, name);
    if (const auto safe = core::check_protected(path, core::PathOp::WRITE); !safe) {
        return safe.error();
    }
    if (mkdir(path.c_str(), 0777) != 0) {
        const int saved_errno = errno;
        return core::error_from_errno(saved_errno, "create directory", path);
    }
    return path;
}

core::Result<std::string> rename(const std::string &path, const std::string &new_name) {
    if (const auto valid = validate_write_path(path, core::PathOp::REMOVE); !valid) {
        return valid.error();
    }
    if (const auto valid = core::validate_name(new_name); !valid) {
        return valid.error();
    }
    const std::string target = core::path_join(parent_path(path), new_name);
    if (const auto safe = core::check_protected(target, core::PathOp::WRITE); !safe) {
        return safe.error();
    }
    if (syscall(SYS_renameat2, AT_FDCWD, path.c_str(), AT_FDCWD, target.c_str(),
                RENAME_NOREPLACE) != 0) {
        const int saved_errno = errno;
        return core::error_from_errno(saved_errno, "rename", path);
    }
    return target;
}

core::Result<void> trash(const std::string &path,
                         const Glib::RefPtr<Gio::Cancellable> &cancellable) {
    if (cancellable && cancellable->is_cancelled()) {
        return cancelled_error();
    }
    if (const auto safe = validate_write_path(path, core::PathOp::REMOVE); !safe) {
        return safe.error();
    }
    struct stat source {};
    if (lstat(path.c_str(), &source) != 0) {
        return core::error_from_errno(errno, "lstat", path);
    }
    // GIO uses the parent device for non-directories, including on overlayfs.
    const auto parent = parent_path(path);
    if (!S_ISDIR(source.st_mode) && stat(parent.c_str(), &source) != 0) {
        return core::error_from_errno(errno, "stat", parent);
    }
    struct stat home {};
    const char *home_path = g_get_home_dir();
    if (stat(home_path, &home) != 0) {
        return core::error_from_errno(errno, "stat", home_path);
    }
    std::string destination = core::path_join(g_get_user_data_dir(), "Trash/files");
    struct stat trash_status {};
    while (stat(destination.c_str(), &trash_status) != 0) {
        if (errno != ENOENT || destination == "/") {
            return core::error_from_errno(errno, "stat", destination);
        }
        destination = parent_path(destination);
    }
    if (source.st_dev != home.st_dev || source.st_dev != trash_status.st_dev) {
        return core::make_error(core::ErrorCode::NOT_SUPPORTED,
                                "The home trash is on a different filesystem");
    }
    try {
        Gio::File::create_for_path(path)->trash(cancellable);
        return {};
    } catch (const Glib::Error &error) {
        return core::error_from_glib(error);
    }
}

core::Result<std::vector<std::string>> find_conflicts(const std::vector<std::string> &sources,
                                                      const std::string &dest_dir) {
    if (const auto valid = validate_destination(dest_dir); !valid) {
        return valid.error();
    }
    std::vector<std::string> conflicts;
    for (const auto &source : sources) {
        if (const auto valid = core::validate_absolute(source); !valid) {
            return valid.error();
        }
        const std::string name = base_name(source);
        if (const auto valid = core::validate_name(name); !valid) {
            return valid.error();
        }
        struct stat ignored {};
        if (lstat(core::path_join(dest_dir, name).c_str(), &ignored) == 0) {
            conflicts.push_back(source);
        } else if (errno != ENOENT) {
            const int saved_errno = errno;
            return core::error_from_errno(saved_errno, "lstat", dest_dir);
        }
    }
    return conflicts;
}

core::Result<TextFile> read_text(const std::string &path) {
    if (const auto valid = validate_write_path(path, core::PathOp::READ); !valid) {
        return valid.error();
    }
    core::UniqueFd fd(open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC));
    if (!fd) {
        const int saved_errno = errno;
        return core::error_from_errno(saved_errno, "open", path);
    }
    struct stat status {};
    if (fstat(fd.get(), &status) != 0) {
        const int saved_errno = errno;
        return core::error_from_errno(saved_errno, "fstat", path);
    }
    if (!S_ISREG(status.st_mode)) {
        return core::make_error(S_ISDIR(status.st_mode) ? core::ErrorCode::IS_DIRECTORY
                                                        : core::ErrorCode::NOT_SUPPORTED,
                                "Text input must be a regular file");
    }
    if (status.st_size < 0 || static_cast<std::uint64_t>(status.st_size) > kTextMaxBytes) {
        return core::make_error(core::ErrorCode::TOO_LARGE, "Text file exceeds 4 MiB");
    }
    std::string contents;
    contents.reserve(static_cast<std::size_t>(status.st_size));
    std::array<char, kCopyBufferBytes> buffer{};
    while (true) {
        const ssize_t count = read(fd.get(), buffer.data(), buffer.size());
        if (count == 0) {
            break;
        }
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            const int saved_errno = errno;
            return core::error_from_errno(saved_errno, "read", path);
        }
        if (contents.size() + static_cast<std::size_t>(count) > kTextMaxBytes) {
            return core::make_error(core::ErrorCode::TOO_LARGE, "Text file exceeds 4 MiB");
        }
        contents.append(buffer.data(), static_cast<std::size_t>(count));
    }
    return TextFile{std::move(contents), stamp_from_stat(file_status_from_stat(status))};
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): public contract fixes the order.
core::Result<FileStamp> write_text(const std::string &path, const std::string &contents,
                                   const std::optional<FileStamp> &expected,
                                   Confirmation confirmation) {
    if (!expected.has_value() && confirmation != Confirmation::CONFIRMED) {
        return core::make_error(core::ErrorCode::NOT_CONFIRMED, "Force save requires confirmation");
    }
    if (const auto safe = validate_write_path(path, core::PathOp::WRITE); !safe) {
        return safe.error();
    }
    FileStatus original;
    if (const auto read = read_status({path}, original); !read) {
        return read.error();
    }
    if (!S_ISREG(original.mode)) {
        return core::make_error(S_ISDIR(original.mode) ? core::ErrorCode::IS_DIRECTORY
                                                       : core::ErrorCode::NOT_SUPPORTED,
                                "Text output must be a regular file");
    }
    if (expected.has_value() && stamp_from_stat(original) != expected.value()) {
        return core::make_error(core::ErrorCode::CONFLICT, "Text file changed since it was read");
    }

    std::string pattern = temporary_template(parent_path(path));
    core::UniqueFd fd(mkstemp(pattern.data()));
    if (!fd) {
        const int saved_errno = errno;
        return core::error_from_errno(saved_errno, "mkstemp", pattern);
    }
    TemporaryPath temporary(pattern);
    if (fchmod(fd.get(), original.mode & 07777) != 0) {
        const int saved_errno = errno;
        return core::error_from_errno(saved_errno, "fchmod", pattern);
    }
    if (const auto written = write_all(fd.get(), contents.data(), contents.size(), pattern);
        !written) {
        return written.error();
    }
    if (fsync(fd.get()) != 0) {
        const int saved_errno = errno;
        return core::error_from_errno(saved_errno, "fsync", pattern);
    }
    FileStatus current;
    if (const auto read = read_status({path}, current); !read) {
        return read.error();
    }
    if (expected.has_value() && stamp_from_stat(current) != expected.value()) {
        return core::make_error(core::ErrorCode::CONFLICT, "Text file changed during save");
    }
    fd.reset();
    if (::rename(pattern.c_str(), path.c_str()) != 0) {
        const int saved_errno = errno;
        return core::error_from_errno(saved_errno, "replace text file", path);
    }
    temporary.dismiss();
    FileStatus saved;
    if (const auto read = read_status({path}, saved); !read) {
        return read.error();
    }
    return stamp_from_stat(saved);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): public contract fixes the order.
core::Result<FileStamp> install_file(const std::string &target, const std::string &source,
                                     const std::optional<FileStamp> &expected,
                                     std::optional<std::uint32_t> mode,
                                     const Glib::RefPtr<Gio::Cancellable> &cancellable) {
    if (cancellable && cancellable->is_cancelled()) {
        return cancelled_error();
    }
    if (const auto safe = validate_write_path(target, core::PathOp::WRITE); !safe) {
        return safe.error();
    }
    if (const auto valid = core::validate_name(base_name(target)); !valid) {
        return valid.error();
    }
    if (const auto valid = core::validate_absolute(source); !valid) {
        return valid.error();
    }
    const auto parent = parent_path(target);
    const core::UniqueFd directory(open(parent.c_str(), O_PATH | O_DIRECTORY | O_CLOEXEC));
    if (!directory) {
        return core::error_from_errno(errno, "open target parent", parent);
    }
    const Entry destination{target, directory.get()};
    FileStatus original;
    const auto check_expected = [&](const FileStamp &stamp) -> core::Result<void> {
        if (const auto read = read_status(destination, original); !read) {
            return read.error();
        }
        if (stamp_from_stat(original) != stamp) {
            return core::make_error(core::ErrorCode::CONFLICT, "Target changed since it was read");
        }
        return {};
    };
    if (expected) {
        if (const auto checked = check_expected(*expected); !checked) {
            return checked.error();
        }
    } else if (const auto read = read_status(destination, original); read) {
        return core::make_error(core::ErrorCode::EXISTS, "Target already exists");
    } else if (read.error().code != core::ErrorCode::NOT_FOUND) {
        return read.error();
    }

    const std::string source_path = source.substr(0, source.find_last_not_of('/') + 1U);
    FileStatus source_status;
    if (const auto read = read_status({source_path.empty() ? "/" : source_path}, source_status);
        !read) {
        return read.error();
    }
    if (!S_ISREG(source_status.mode) || source.back() == '/') {
        return core::make_error(core::ErrorCode::NOT_SUPPORTED, "Source must be a regular file");
    }
    const core::UniqueFd input(
        open(source.c_str(), O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC));
    if (!input) {
        return core::error_from_errno(errno, "open source", source);
    }
    struct stat opened {};
    if (fstat(input.get(), &opened) != 0) {
        return core::error_from_errno(errno, "fstat source", source);
    }
    if (opened.st_dev != source_status.dev || opened.st_ino != source_status.ino) {
        return core::make_error(core::ErrorCode::CONFLICT, "Source changed during open");
    }

    const Entry temporary = temporary_sibling(destination);
    const bool default_mode = !mode && !expected;
    const core::UniqueFd output(openat(temporary.parent, temporary.name().c_str(),
                                       O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC,
                                       default_mode ? 0666 : 0600));
    if (!output) {
        return core::error_from_errno(errno, "open temporary file", temporary.path);
    }
    TemporaryPath cleanup(temporary.name(), temporary.parent);
    mode_t permissions = mode.value_or(original.mode) & 07777;
    if (default_mode) {
        // Let the kernel apply the umask without changing process state.
        struct stat created {};
        if (fstat(output.get(), &created) != 0 || fchmod(output.get(), 0600) != 0) {
            return core::error_from_errno(errno, "prepare temporary file", temporary.path);
        }
        permissions = created.st_mode & 07777;
    }

    std::array<char, kCopyBufferBytes> buffer{};
    while (true) {
        if (cancellable && cancellable->is_cancelled()) {
            return cancelled_error();
        }
        const ssize_t count = read(input.get(), buffer.data(), buffer.size());
        if (count == 0) {
            break;
        }
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            return core::error_from_errno(errno, "read source", source);
        }
        if (const auto written = write_bytes(output.get(), buffer.data(),
                                             static_cast<std::size_t>(count), temporary.path);
            !written) {
            return written.error();
        }
    }
    struct stat saved {};
    if (fchmod(output.get(), permissions) != 0 || fsync(output.get()) != 0 ||
        fstat(output.get(), &saved) != 0) {
        return core::error_from_errno(errno, "finish temporary file", temporary.path);
    }
    if (expected) {
        if (const auto checked = check_expected(*expected); !checked) {
            return checked.error();
        }
    }
    if (cancellable && cancellable->is_cancelled()) {
        return cancelled_error();
    }
    if (const auto renamed = rename_entry(temporary, destination, expected.has_value()); !renamed) {
        return renamed.error();
    }
    cleanup.dismiss();
    return stamp_from_stat(file_status_from_stat(saved));
}

core::Result<void> remove_tree(const std::string &path, Confirmation confirmation,
                               const Glib::RefPtr<Gio::Cancellable> &cancellable) {
    if (confirmation != Confirmation::CONFIRMED) {
        return core::make_error(core::ErrorCode::NOT_CONFIRMED,
                                "Permanent deletion requires confirmation");
    }
    if (const auto safe = validate_write_path(path, core::PathOp::REMOVE); !safe) {
        return safe.error();
    }
    const Removal removal{nullptr, nullptr, false, cancellable.operator->(), true};
    if (removal.is_cancelled()) {
        return cancelled_error();
    }
    // A trailing slash must not make a symbolic link resolve to its target.
    const std::string entry = path.substr(0, path.find_last_not_of('/') + 1U);
    if (const auto valid = core::validate_name(base_name(entry)); !valid) {
        return valid.error();
    }
    const auto parent = parent_path(entry);
    const core::UniqueFd directory(open(parent.c_str(), O_PATH | O_DIRECTORY | O_CLOEXEC));
    if (!directory) {
        return core::error_from_errno(errno, "open parent", parent);
    }
    return remove_entry({entry, directory.get()}, removal);
}

core::Result<job::JobId> copy(job::JobQueue &queue, const std::vector<std::string> &sources,
                              const std::string &dest_dir, ConflictPolicy policy,
                              Confirmation confirmation) {
    return submit_copy(queue, sources, dest_dir, policy, confirmation, false);
}

core::Result<job::JobId> move(job::JobQueue &queue, const std::vector<std::string> &sources,
                              const std::string &dest_dir, ConflictPolicy policy,
                              Confirmation confirmation) {
    return submit_copy(queue, sources, dest_dir, policy, confirmation, true);
}

core::Result<job::JobId> trash_many(job::JobQueue &queue, const std::vector<std::string> &paths) {
    std::vector<Snapshot> snapshots;
    if (const auto valid = validate_sources(paths, core::PathOp::REMOVE, snapshots); !valid) {
        return valid.error();
    }
    return queue.submit("Move files to trash", [entries = std::move(snapshots)](job::Job &job) {
        job.report_total(-1, static_cast<std::int32_t>(entries.size()));
        std::uint32_t completed = 0;
        for (const auto &entry : entries) {
            core::Result<void> result = revalidate_source(entry, core::PathOp::REMOVE);
            if (result) {
                result = trash(entry.path, job.cancellable());
            }
            std::optional<core::Error> item_error;
            if (!result) {
                item_error = result.error();
            }
            job.add_item_result({entry.path, std::move(item_error), false});
            job.report_progress(0, ++completed, entry.path);
            if (!result && is_fatal(result.error().code)) {
                return result;
            }
        }
        return core::Result<void>{};
    });
}

core::Result<job::JobId> delete_permanently(job::JobQueue &queue,
                                            const std::vector<std::string> &paths,
                                            Confirmation confirmation) {
    if (confirmation != Confirmation::CONFIRMED) {
        return core::make_error(core::ErrorCode::NOT_CONFIRMED,
                                "Permanent deletion requires confirmation");
    }
    std::vector<Snapshot> snapshots;
    if (const auto valid = validate_sources(paths, core::PathOp::REMOVE, snapshots); !valid) {
        return valid.error();
    }
    return queue.submit(
        "Delete files permanently", [entries = std::move(snapshots)](job::Job &job) {
            job.report_total(-1, static_cast<std::int32_t>(entries.size()));
            std::uint32_t completed = 0;
            for (const auto &entry : entries) {
                core::Result<void> result = revalidate_source(entry, core::PathOp::REMOVE);
                if (result) {
                    result = remove_entry({entry.path}, {&job});
                }
                std::optional<core::Error> item_error;
                if (!result) {
                    item_error = result.error();
                }
                job.add_item_result({entry.path, std::move(item_error), false});
                job.report_progress(0, ++completed, entry.path);
                if (!result && is_fatal(result.error().code)) {
                    return result;
                }
            }
            return core::Result<void>{};
        });
}

} // namespace filescope::fs
