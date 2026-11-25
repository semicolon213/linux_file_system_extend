#include "trash/trash.hpp"

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <dirent.h>
#include <exception>
#include <fcntl.h>
#include <limits.h>
#include <memory>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

#include <gio/gio.h>
#include <glibmm/miscutils.h>

#include "core/fd.hpp"
#include "core/path.hpp"

namespace filescope::trash {
namespace {

using core::ErrorCode;
using core::Result;
using ListResult = Result<std::vector<Item>>;
constexpr std::string_view kInfoSuffix = ".trashinfo";

struct DirectoryCloser {
    void operator()(DIR *dir) const noexcept { closedir(dir); }
};

struct TrashDirectories {
    std::string path;
    core::UniqueFd root;
    core::UniqueFd files;
    core::UniqueFd info;
};

core::Error cancelled_error() {
    return core::make_error(ErrorCode::CANCELLED, "operation cancelled");
}

bool cancelled(GCancellable *token) {
    return token != nullptr && g_cancellable_is_cancelled(token);
}

Result<void> open_trash(TrashDirectories &dirs) {
    constexpr int flags = O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC;
    dirs.root.reset(open(dirs.path.c_str(), flags));
    if (!dirs.root) {
        if (errno == ENOENT) {
            return {};
        }
        return core::error_from_errno(errno, "open", dirs.path);
    }
    dirs.files.reset(openat(dirs.root.get(), "files", flags));
    if (!dirs.files && errno != ENOENT) {
        return core::error_from_errno(errno, "open", dirs.path + "/files");
    }
    dirs.info.reset(openat(dirs.root.get(), "info", flags));
    if (!dirs.info && errno != ENOENT) {
        return core::error_from_errno(errno, "open", dirs.path + "/info");
    }
    return {};
}

Result<void> read_names(const core::UniqueFd &fd, GCancellable *token,
                        std::vector<std::string> &names) {
    if (!fd) {
        return {};
    }
    // Open a separate directory stream so reading it does not consume the caller's offset.
    core::UniqueFd scan(openat(fd.get(), ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    if (!scan) {
        return core::error_from_errno(errno, "open", "trash directory");
    }
    const std::unique_ptr<DIR, DirectoryCloser> stream(fdopendir(scan.get()));
    if (!stream) {
        return core::error_from_errno(errno, "fdopendir", "trash directory");
    }
    static_cast<void>(scan.release());
    while (!cancelled(token)) {
        errno = 0;
        const dirent *entry = readdir(stream.get());
        if (entry == nullptr) {
            if (errno != 0) {
                return core::error_from_errno(errno, "readdir", "trash directory");
            }
            return {};
        }
        const std::string_view name(entry->d_name);
        if (name != "." && name != "..") {
            names.emplace_back(name);
        }
    }
    return cancelled_error();
}

std::string info_name(const std::string &id) { return id + std::string(kInfoSuffix); }

std::string decode_path(const std::string &encoded) {
    if (encoded.find('\0') != std::string::npos) {
        return {};
    }
    const std::unique_ptr<char, decltype(&g_free)> decoded(
        g_uri_unescape_string(encoded.c_str(), nullptr), g_free);
    if (!decoded || *decoded == '\0') {
        return {};
    }
    std::string path(decoded.get());
    if (path.front() != '/') {
        const std::string components = '/' + path + '/';
        if (components.find("/../") != std::string::npos) {
            return {};
        }
        path = core::path_join(Glib::get_user_data_dir(), path);
    }
    const auto slash = path.find_last_of('/');
    if (!core::validate_absolute(path) || !core::validate_name(path.substr(slash + 1))) {
        return {};
    }
    return path;
}

std::optional<std::int64_t> deletion_time(const std::string &value) {
    if (value.size() != 19 || value[4] != '-' || value[7] != '-' || value[10] != 'T' ||
        value[13] != ':' || value[16] != ':') {
        return std::nullopt;
    }
    const std::unique_ptr<GTimeZone, decltype(&g_time_zone_unref)> zone(g_time_zone_new_local(),
                                                                        g_time_zone_unref);
    const std::unique_ptr<GDateTime, decltype(&g_date_time_unref)> date(
        g_date_time_new_from_iso8601(value.c_str(), zone.get()), g_date_time_unref);
    if (!date) {
        return std::nullopt;
    }
    return g_date_time_to_unix(date.get()) * G_USEC_PER_SEC;
}

constexpr std::size_t kMaxInfoLine = std::size_t{3} * PATH_MAX + sizeof("Path=");

bool read_info_line(std::FILE *stream, std::string &line, GCancellable *token) {
    line.clear();
    std::size_t bytes = 0;
    for (int ch = getc_unlocked(stream); ch != EOF; ch = getc_unlocked(stream)) {
        if (bytes++ % 4096U == 0 && cancelled(token)) {
            return false;
        }
        if (ch == '\n') {
            if (!line.empty() && line.back() == '\r' && line.size() < kMaxInfoLine) {
                line.pop_back();
            }
            return true;
        }
        if (line.size() < kMaxInfoLine) {
            line.push_back(static_cast<char>(ch));
        }
    }
    return std::ferror(stream) == 0 && !line.empty();
}

void read_info(int directory, Item &item, GCancellable *token) {
    item.info_missing = true;
    core::UniqueFd fd(openat(directory, info_name(item.id).c_str(),
                             O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
    struct stat status {};
    if (!fd || fstat(fd.get(), &status) != 0 || !S_ISREG(status.st_mode)) {
        return;
    }
    const std::unique_ptr<std::FILE, int (*)(std::FILE *)> stream(fdopen(fd.get(), "r"),
                                                                  std::fclose);
    if (!stream) {
        return;
    }
    static_cast<void>(fd.release());
    std::string line;
    if (!read_info_line(stream.get(), line, token) || line != "[Trash Info]") {
        return;
    }
    bool found_path = false;
    bool found_date = false;
    while (!(found_path && found_date) && read_info_line(stream.get(), line, token)) {
        if (!found_path && line.compare(0, 5, "Path=") == 0) {
            found_path = true;
            if (line.size() < kMaxInfoLine) {
                item.original_path = decode_path(line.substr(5));
                item.info_missing = item.original_path.empty();
            }
        } else if (!found_date && line.compare(0, 13, "DeletionDate=") == 0) {
            found_date = true;
            item.deleted_at_us = deletion_time(line.substr(13));
        }
    }
}

ListResult list(GCancellable *token) {
    if (cancelled(token)) {
        return cancelled_error();
    }
    TrashDirectories dirs{trash_dir(), {}, {}, {}};
    const auto opened = open_trash(dirs);
    if (!opened) {
        return opened.error();
    }
    std::vector<std::string> entries;
    if (const auto read = read_names(dirs.files, token, entries); !read) {
        return read.error();
    }
    std::vector<Item> items;
    for (const auto &id : entries) {
        if (cancelled(token)) {
            return cancelled_error();
        }
        struct stat status {};
        if (fstatat(dirs.files.get(), id.c_str(), &status, AT_SYMLINK_NOFOLLOW) != 0) {
            if (errno == ENOENT) {
                continue;
            }
            return core::error_from_errno(errno, "stat trash item", id);
        }
        Item item;
        item.id = id;
        if (S_ISREG(status.st_mode)) {
            item.type = core::FileType::REGULAR;
            item.size = static_cast<std::uint64_t>(status.st_size);
        } else if (S_ISDIR(status.st_mode)) {
            item.type = core::FileType::DIRECTORY;
        } else if (S_ISLNK(status.st_mode)) {
            item.type = core::FileType::SYMLINK;
        }
        read_info(dirs.info.get(), item, token);
        items.push_back(std::move(item));
    }
    return items;
}

struct ListResultState {
    Glib::RefPtr<Gio::Cancellable> cancellable;
    GCancellable *token = nullptr;
    ListResult result{std::vector<Item>{}};
    std::atomic<bool> ready{false};
};

struct ListTask {
    std::shared_ptr<ListResultState> state;
    GSource *source = nullptr;
    std::atomic<bool> queued{false};
};

struct ListDelivery {
    std::shared_ptr<ListResultState> state;
    ListSlot done;
};

void run_list(void *payload, void *) {
    const std::unique_ptr<ListTask> task(static_cast<ListTask *>(payload));
    g_assert_true(task->queued.load(std::memory_order_acquire));
    auto &state = *task->state;
    try {
        state.result = list(state.token);
    } catch (const std::exception &error) {
        state.result = core::make_error(ErrorCode::FAILED, error.what());
    }
    state.ready.store(true, std::memory_order_release);
    g_source_set_ready_time(task->source, 0);
    g_source_unref(task->source);
}

GThreadPool *list_pool() {
    static const std::unique_ptr<GThreadPool, void (*)(GThreadPool *)> pool(
        g_thread_pool_new(run_list, nullptr, 10, false, nullptr),
        [](GThreadPool *value) { g_thread_pool_free(value, false, false); });
    return pool.get();
}

gboolean deliver_list(void *payload) {
    auto &delivery = *static_cast<ListDelivery *>(payload);
    auto &state = *delivery.state;
    g_assert_true(state.ready.load(std::memory_order_acquire));
    if (delivery.done) {
        if (cancelled(state.token)) {
            delivery.done(cancelled_error());
        } else {
            delivery.done(state.result);
        }
    }
    return G_SOURCE_REMOVE;
}

gboolean dispatch_list(GSource *, GSourceFunc callback, void *data) { return callback(data); }

Result<void> unlink_info(const TrashDirectories &dirs, const std::string &name) {
    if (dirs.info && unlinkat(dirs.info.get(), name.c_str(), 0) != 0 && errno != ENOENT) {
        return core::error_from_errno(errno, "unlink trash metadata", name);
    }
    return {};
}

Result<void> restore_file(const TrashDirectories &dirs, const std::string &id, std::string &path,
                          const job::Job &job) {
    Item item;
    item.id = id;
    read_info(dirs.info.get(), item, job.cancellable()->gobj());
    if (job.is_cancelled()) {
        return cancelled_error();
    }
    if (!dirs.files || item.info_missing) {
        return core::make_error(ErrorCode::NOT_FOUND, "Trash item or its metadata is missing");
    }
    path = std::move(item.original_path);
    if (const auto safe = core::check_protected(path, core::PathOp::WRITE); !safe) {
        return safe.error();
    }
    const auto slash = path.find_last_of('/');
    const auto parent = path.substr(0, slash == 0 ? 1 : slash);
    const auto name = path.substr(slash + 1);
    const core::UniqueFd target(open(parent.c_str(), O_PATH | O_DIRECTORY | O_CLOEXEC));
    if (!target) {
        return core::error_from_errno(errno, "open restore directory", parent);
    }
    // Check the directory the kernel opened, including any symlinked ancestors.
    const std::string fd_path = "/proc/self/fd/" + std::to_string(target.get());
    const std::unique_ptr<char, decltype(&g_free)> resolved(
        g_file_read_link(fd_path.c_str(), nullptr), g_free);
    if (!resolved) {
        return core::make_error(ErrorCode::IO, "Cannot resolve restore directory");
    }
    if (const auto safe =
            core::check_protected(core::path_join(resolved.get(), name), core::PathOp::WRITE);
        !safe) {
        return safe.error();
    }
    if (job.is_cancelled()) {
        return cancelled_error();
    }
    if (renameat2(dirs.files.get(), id.c_str(), target.get(), name.c_str(), RENAME_NOREPLACE) !=
        0) {
        return core::error_from_errno(errno, "restore trash item", path);
    }
    return unlink_info(dirs, info_name(id));
}

bool same_directory(int fd, const std::string &path) {
    struct stat opened {};
    struct stat current {};
    return fstat(fd, &opened) == 0 && lstat(path.c_str(), &current) == 0 &&
           opened.st_dev == current.st_dev && opened.st_ino == current.st_ino;
}

Result<void> delete_file(const TrashDirectories &dirs, const std::string &id, const job::Job &job,
                         const std::string &path) {
    if (!dirs.files) {
        return core::make_error(ErrorCode::NOT_FOUND, "Trash files directory is missing");
    }
    if (!same_directory(dirs.root.get(), dirs.path) ||
        !same_directory(dirs.files.get(), dirs.path + "/files")) {
        return core::make_error(ErrorCode::CONFLICT, "Trash directory changed");
    }
    const auto removed = fs::remove_tree(path, fs::Confirmation::CONFIRMED, job.cancellable());
    if (!removed) {
        return removed.error();
    }
    // Once the payload is gone, finish removing its metadata even if cancellation arrived.
    return unlink_info(dirs, info_name(id));
}

enum class Action : std::uint8_t { RESTORE, DELETE, EMPTY };

Result<void> run_job(Action action, const std::vector<std::string> &selected, job::Job &job) {
    if (job.is_cancelled()) {
        return cancelled_error();
    }
    TrashDirectories dirs{trash_dir(), {}, {}, {}};
    const auto opened = open_trash(dirs);
    if (!opened) {
        return opened.error();
    }
    auto *token = job.cancellable()->gobj();
    std::vector<std::string> info;
    std::vector<std::string> files;
    if (action == Action::EMPTY) {
        // Snapshot info first to avoid sweeping metadata newly created after the files snapshot.
        if (const auto read = read_names(dirs.info, token, info); !read) {
            return read.error();
        }
        if (const auto read = read_names(dirs.files, token, files); !read) {
            return read.error();
        }
    }
    const auto &entries = action == Action::EMPTY ? files : selected;
    job.report_total(-1, static_cast<std::int32_t>(entries.size()));
    bool failed = false;
    std::uint32_t progress = 0;
    for (const auto &id : entries) {
        if (job.is_cancelled()) {
            return cancelled_error();
        }
        std::string path = core::path_join(dirs.path + "/files", id);
        const auto result = action == Action::RESTORE ? restore_file(dirs, id, path, job)
                                                      : delete_file(dirs, id, job, path);
        job.add_item_result({path, result ? std::nullopt : std::optional(result.error()), false});
        job.report_progress(0, ++progress, path);
        if (!result && result.error().code == ErrorCode::CANCELLED) {
            return result.error();
        }
        failed = failed || !result;
    }

    for (const auto &name : info) {
        if (job.is_cancelled()) {
            return cancelled_error();
        }
        if (name.size() > kInfoSuffix.size() &&
            name.compare(name.size() - kInfoSuffix.size(), kInfoSuffix.size(), kInfoSuffix) == 0) {
            const auto id = name.substr(0, name.size() - kInfoSuffix.size());
            struct stat status {};
            if (dirs.files &&
                fstatat(dirs.files.get(), id.c_str(), &status, AT_SYMLINK_NOFOLLOW) == 0) {
                continue;
            }
            if (dirs.files && errno != ENOENT) {
                job.add_item_result({dirs.path + "/info/" + name,
                                     core::error_from_errno(errno, "stat trash item", id), false});
                failed = true;
                continue;
            }
        }
        if (const auto removed = unlink_info(dirs, name); !removed) {
            job.add_item_result({dirs.path + "/info/" + name, removed.error(), false});
            failed = true;
        }
    }
    if (failed) {
        return core::make_error(ErrorCode::PARTIAL, "One or more trash items failed");
    }
    return {};
}

Result<job::JobId> submit(job::JobQueue &queue, const std::vector<std::string> &ids, Action action,
                          const char *title) {
    for (const auto &id : ids) {
        if (!core::validate_name(id)) {
            return core::make_error(ErrorCode::INVALID_PATH, "Invalid trash item id");
        }
    }
    return queue.submit(title, [ids, action](job::Job &job) { return run_job(action, ids, job); });
}

Result<void> confirm(fs::Confirmation confirmation) {
    if (confirmation != fs::Confirmation::CONFIRMED) {
        return core::make_error(ErrorCode::NOT_CONFIRMED,
                                "Permanent deletion requires confirmation");
    }
    return {};
}

} // namespace

std::string trash_dir() { return core::path_join(Glib::get_user_data_dir(), "Trash"); }

void list_async(const Glib::RefPtr<Gio::Cancellable> &cancellable, ListSlot done) {
    auto state = std::make_shared<ListResultState>();
    state->cancellable = cancellable;
    state->token = cancellable ? cancellable->gobj() : nullptr;
    auto delivery = std::make_unique<ListDelivery>(ListDelivery{state, std::move(done)});
    auto task = std::make_unique<ListTask>();
    task->state = std::move(state);
    // Like nav, attach the main-context source before dispatching to the GLib pool.
    static GSourceFuncs functions = {nullptr, nullptr, dispatch_list, nullptr, nullptr, nullptr};
    task->source = g_source_new(&functions, sizeof(GSource));
    g_source_set_callback(task->source, deliver_list, delivery.release(), [](void *data) {
        const std::unique_ptr<ListDelivery> owned(static_cast<ListDelivery *>(data));
    });
    g_source_attach(task->source, g_main_context_get_thread_default());
    task->queued.store(true, std::memory_order_release);
    g_thread_pool_push(list_pool(), task.release(), nullptr);
}

Result<job::JobId> restore(job::JobQueue &queue, const std::vector<std::string> &ids) {
    return submit(queue, ids, Action::RESTORE, "Restore from trash");
}

Result<job::JobId> delete_items(job::JobQueue &queue, const std::vector<std::string> &ids,
                                fs::Confirmation confirmation) {
    if (const auto confirmed = confirm(confirmation); !confirmed) {
        return confirmed.error();
    }
    return submit(queue, ids, Action::DELETE, "Delete trash items permanently");
}

Result<job::JobId> empty(job::JobQueue &queue, fs::Confirmation confirmation) {
    if (const auto confirmed = confirm(confirmation); !confirmed) {
        return confirmed.error();
    }
    return submit(queue, {}, Action::EMPTY, "Empty trash");
}

} // namespace filescope::trash
