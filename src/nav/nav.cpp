#include "nav/nav.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <functional>
#include <memory>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

#include <gio/gio.h>
#include <glib/gstdio.h>
#include <glibmm/fileutils.h>
#include <glibmm/keyfile.h>
#include <glibmm/miscutils.h>

#include "core/fd.hpp"
#include "core/path.hpp"

namespace filescope::nav {
namespace {

using EntriesResult = core::Result<std::vector<core::Entry>>;
using MatchesResult = core::Result<std::vector<std::string>>;

struct NavTask {
    GSource *source = nullptr;
    std::function<void()> work;
    std::atomic<bool> queued{false};
};

void run_task(void *data, void *) {
    const std::unique_ptr<NavTask> task(static_cast<NavTask *>(data));
    g_assert_true(task->queued.load(std::memory_order_acquire));
    task->work();
    g_source_set_ready_time(task->source, 0);
    g_source_unref(task->source);
}

gboolean deliver_task(void *data) {
    (*static_cast<std::function<void()> *>(data))();
    return G_SOURCE_REMOVE;
}

gboolean dispatch_task(GSource *, GSourceFunc callback, void *data) { return callback(data); }

GThreadPool *nav_pool() {
    static const std::unique_ptr<GThreadPool, void (*)(GThreadPool *)> pool(
        g_thread_pool_new(run_task, nullptr, 10, false, nullptr),
        [](GThreadPool *value) { g_thread_pool_free(value, false, false); });
    return pool.get();
}

template <typename Work, typename Slot> void submit_task(Work work, Slot done) {
    struct ResultSlot {
        std::optional<decltype(work())> value;
        std::atomic<bool> ready{false};
    };
    auto result = std::make_shared<ResultSlot>();
    auto task = std::make_unique<NavTask>();
    task->work = [result, work = std::move(work)] {
        result->value.emplace(work());
        result->ready.store(true, std::memory_order_release);
    };
    auto deliver = std::make_unique<std::function<void()>>([result, done = std::move(done)] {
        g_assert_true(result->ready.load(std::memory_order_acquire));
        done(std::move(result->value.value()));
    });

    // Create and attach the completion source on the caller's context before the worker runs.
    static GSourceFuncs source_functions = {nullptr, nullptr, dispatch_task,
                                            nullptr, nullptr, nullptr};
    task->source = g_source_new(&source_functions, sizeof(GSource));
    g_source_set_callback(task->source, deliver_task, deliver.release(), [](void *data) {
        const std::unique_ptr<std::function<void()>> owned(
            static_cast<std::function<void()> *>(data));
    });
    g_source_attach(task->source, g_main_context_get_thread_default());
    task->queued.store(true, std::memory_order_release);
    g_thread_pool_push(nav_pool(), task.release(), nullptr);
}

struct DirectoryCloser {
    void operator()(DIR *directory) const noexcept { closedir(directory); }
};

core::FileType file_type(mode_t mode) noexcept {
    if (S_ISREG(mode)) {
        return core::FileType::REGULAR;
    }
    if (S_ISDIR(mode)) {
        return core::FileType::DIRECTORY;
    }
    if (S_ISLNK(mode)) {
        return core::FileType::SYMLINK;
    }
    return core::FileType::OTHER;
}

bool cancelled(GCancellable *cancellable) noexcept {
    return cancellable != nullptr && g_cancellable_is_cancelled(cancellable);
}

core::Error cancelled_error() {
    return core::make_error(core::ErrorCode::CANCELLED, "operation cancelled");
}

int retry_fstat(int fd, struct stat *status) noexcept {
    int result = 0;
    do {
        result = fstat(fd, status);
    } while (result < 0 && errno == EINTR);
    return result;
}

int retry_fstatat(int fd, const char *name, struct stat *status, int flags) noexcept {
    int result = 0;
    do {
        result = fstatat(fd, name, status, flags);
    } while (result < 0 && errno == EINTR);
    return result;
}

template <typename T, typename Mapper>
core::Result<std::vector<T>> scan_dir(const std::string &path, bool show_hidden, bool dirs_only,
                                      GCancellable *cancellable, Mapper map) {
    if (const auto valid = core::validate_absolute(path); !valid) {
        return valid.error();
    }
    if (cancelled(cancellable)) {
        return cancelled_error();
    }

    int raw_fd = -1;
    do {
        raw_fd = open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    } while (raw_fd < 0 && errno == EINTR);
    if (raw_fd < 0) {
        const int saved_errno = errno;
        return core::error_from_errno(saved_errno, "open directory", path);
    }
    core::UniqueFd fd(raw_fd);

    struct stat parent_status {};
    if (retry_fstat(fd.get(), &parent_status) != 0) {
        const int saved_errno = errno;
        return core::error_from_errno(saved_errno, "stat directory", path);
    }

    DIR *raw_directory = fdopendir(fd.get());
    if (raw_directory == nullptr) {
        const int saved_errno = errno;
        return core::error_from_errno(saved_errno, "read directory", path);
    }
    fd.release();
    std::unique_ptr<DIR, DirectoryCloser> directory(raw_directory);
    std::vector<T> entries;

    for (;;) {
        if (cancelled(cancellable)) {
            return cancelled_error();
        }
        errno = 0;
        dirent *item = readdir(directory.get());
        if (item == nullptr) {
            if (errno == EINTR) {
                continue;
            }
            if (errno != 0) {
                const int saved_errno = errno;
                return core::error_from_errno(saved_errno, "read directory", path);
            }
            break;
        }

        const std::string name(item->d_name);
        if (name == "." || name == ".." || (!show_hidden && name.front() == '.')) {
            continue;
        }

        core::Entry entry;
        entry.name = name;
        entry.is_hidden = name.front() == '.';
        struct stat item_status {};
        if (retry_fstatat(dirfd(directory.get()), item->d_name, &item_status,
                          AT_SYMLINK_NOFOLLOW) == 0) {
            entry.type = file_type(item_status.st_mode);
            entry.target_type = entry.type;
            entry.is_mount_point = item_status.st_dev != parent_status.st_dev;
            entry.size = static_cast<std::uint64_t>(item_status.st_size);
            entry.mode = static_cast<std::uint32_t>(item_status.st_mode);
            entry.uid = static_cast<std::uint32_t>(item_status.st_uid);
            entry.gid = static_cast<std::uint32_t>(item_status.st_gid);
            entry.dev = static_cast<std::uint64_t>(item_status.st_dev);
            entry.ino = static_cast<std::uint64_t>(item_status.st_ino);
            entry.mtime_ns = static_cast<std::int64_t>(item_status.st_mtim.tv_sec) * 1'000'000'000 +
                             item_status.st_mtim.tv_nsec;
            if (entry.type == core::FileType::SYMLINK) {
                struct stat target_status {};
                if (retry_fstatat(dirfd(directory.get()), item->d_name, &target_status, 0) == 0) {
                    entry.target_type = file_type(target_status.st_mode);
                } else if (errno == ENOENT || errno == ENOTDIR) {
                    entry.is_broken_link = true;
                    entry.target_type = core::FileType::OTHER;
                }
            }
        }
        if (dirs_only && entry.type != core::FileType::DIRECTORY &&
            !(entry.type == core::FileType::SYMLINK &&
              entry.target_type == core::FileType::DIRECTORY)) {
            continue;
        }
        if (auto mapped = map(std::move(entry)); mapped.has_value()) {
            entries.push_back(std::move(mapped.value()));
        }
    }
    return entries;
}

EntriesResult list_dir(const std::string &path, bool show_hidden, bool dirs_only,
                       GCancellable *cancellable) {
    return scan_dir<core::Entry>(path, show_hidden, dirs_only, cancellable, [](core::Entry entry) {
        return std::optional<core::Entry>(std::move(entry));
    });
}

MatchesResult complete(const std::string &input, bool show_hidden, std::size_t max_results,
                       GCancellable *cancellable) {
    if (const auto valid = core::validate_absolute(input); !valid) {
        return valid.error();
    }
    const std::size_t separator = input.find_last_of('/');
    const std::string directory = separator == 0 ? "/" : input.substr(0, separator);
    const std::string prefix = input.substr(separator + 1);
    auto matches = scan_dir<std::string>(
        directory, show_hidden, true, cancellable,
        [&directory, &prefix](const core::Entry &entry) -> std::optional<std::string> {
            if (entry.name.compare(0, prefix.size(), prefix) != 0) {
                return std::nullopt;
            }
            return core::path_join(directory, entry.name);
        });
    if (matches) {
        auto &values = matches.value();
        std::sort(values.begin(), values.end());
        if (values.size() > max_results) {
            values.resize(max_results);
        }
    }
    return matches;
}

std::string bookmarks_path() {
    return Glib::build_filename(Glib::get_user_config_dir(), "filescope", "bookmarks.ini");
}

} // namespace

void list_dir_async(const std::string &path, bool show_hidden, bool dirs_only,
                    const Glib::RefPtr<Gio::Cancellable> &cancellable, ListDirSlot done) {
    submit_task(
        [path, show_hidden, dirs_only, cancellable] {
            return list_dir(path, show_hidden, dirs_only,
                            cancellable ? cancellable->gobj() : nullptr);
        },
        std::move(done));
}

History::History(std::string initial_path) { paths_.push_back(std::move(initial_path)); }

const std::string &History::current() const noexcept { return paths_[current_]; }

void History::push(const std::string &path) {
    if (path == current()) {
        return;
    }
    paths_.erase(paths_.begin() + static_cast<std::ptrdiff_t>(current_ + 1), paths_.end());
    paths_.push_back(path);
    if (paths_.size() > kMaxEntries) {
        paths_.erase(paths_.begin());
    }
    current_ = paths_.size() - 1;
}

bool History::can_back() const noexcept { return current_ > 0; }

bool History::can_forward() const noexcept { return current_ + 1 < paths_.size(); }

std::optional<std::string> History::back() {
    if (!can_back()) {
        return std::nullopt;
    }
    return paths_[--current_];
}

std::optional<std::string> History::forward() {
    if (!can_forward()) {
        return std::nullopt;
    }
    return paths_[++current_];
}

std::optional<std::string> parent(const std::string &path) {
    const std::string normalized = core::canonicalize(path);
    if (normalized.empty() || normalized.front() != '/' || normalized == "/") {
        return std::nullopt;
    }
    const std::size_t separator = normalized.find_last_of('/');
    return separator == 0 ? std::optional<std::string>("/")
                          : std::optional<std::string>(normalized.substr(0, separator));
}

std::vector<std::string> breadcrumb(const std::string &path) {
    const std::string normalized = core::canonicalize(path);
    if (normalized.empty() || normalized.front() != '/') {
        return {};
    }
    std::vector<std::string> result{"/"};
    for (std::size_t i = 1; i < normalized.size(); ++i) {
        if (normalized[i] == '/') {
            result.push_back(normalized.substr(0, i));
        }
    }
    if (normalized != "/") {
        result.push_back(normalized);
    }
    return result;
}

void complete_async(const std::string &input, bool show_hidden, std::size_t max_results,
                    const Glib::RefPtr<Gio::Cancellable> &cancellable, CompleteSlot done) {
    submit_task(
        [input, show_hidden, max_results, cancellable] {
            return complete(input, show_hidden, max_results,
                            cancellable ? cancellable->gobj() : nullptr);
        },
        std::move(done));
}

std::string home() { return Glib::get_home_dir(); }

core::Result<Bookmarks> Bookmarks::load() {
    const std::string path = bookmarks_path();
    if (!Glib::file_test(path, Glib::FILE_TEST_EXISTS)) {
        return Bookmarks{};
    }
    try {
        Glib::KeyFile key_file;
        key_file.load_from_file(path);
        Bookmarks bookmarks;
        bookmarks.paths_ = key_file.get_string_list("Bookmarks", "paths");
        for (const auto &bookmark : bookmarks.paths_) {
            if (const auto valid = core::validate_absolute(bookmark); !valid) {
                return valid.error();
            }
        }
        return bookmarks;
    } catch (const Glib::Error &error) {
        return core::error_from_glib(error);
    }
}

const std::vector<std::string> &Bookmarks::list() const noexcept { return paths_; }

core::Result<void> Bookmarks::add(const std::string &path) {
    if (const auto valid = core::validate_absolute(path); !valid) {
        return valid.error();
    }
    if (std::find(paths_.begin(), paths_.end(), path) != paths_.end()) {
        return core::make_error(core::ErrorCode::EXISTS, "bookmark already exists");
    }
    paths_.push_back(path);
    return {};
}

bool Bookmarks::remove(const std::string &path) {
    const auto found = std::find(paths_.begin(), paths_.end(), path);
    if (found == paths_.end()) {
        return false;
    }
    paths_.erase(found);
    return true;
}

core::Result<void> Bookmarks::save() const {
    const std::string path = bookmarks_path();
    const std::string directory = Glib::path_get_dirname(path);
    if (g_mkdir_with_parents(directory.c_str(), 0700) != 0) {
        const int saved_errno = errno;
        return core::error_from_errno(saved_errno, "create config directory", directory);
    }
    try {
        Glib::KeyFile key_file;
        key_file.set_string_list("Bookmarks", "paths", paths_);
        Glib::file_set_contents(path, key_file.to_data());
        return {};
    } catch (const Glib::Error &error) {
        return core::error_from_glib(error);
    }
}

} // namespace filescope::nav
