#include "mon/mon.hpp"

#include "mon/mon_internal.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <deque>
#include <dirent.h>
#include <fstream>
#include <limits>
#include <mutex>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <variant>

#include <glib.h>
#include <glibmm/dispatcher.h>

#include "core/fd.hpp"
#include "core/path.hpp"

namespace filescope::mon {
namespace {

constexpr std::uint32_t kWatchMask = IN_CREATE | IN_DELETE | IN_MODIFY | IN_CLOSE_WRITE |
                                     IN_ATTRIB | IN_MOVED_FROM | IN_MOVED_TO | IN_DELETE_SELF |
                                     IN_MOVE_SELF | IN_DONT_FOLLOW | IN_ONLYDIR | IN_EXCL_UNLINK;
constexpr std::size_t kReadBufferSize = std::size_t{64} * 1024U;
constexpr std::uint32_t kDefaultQueueCapacity = 8192;

struct DirectoryCloser {
    void operator()(DIR *directory) const noexcept { closedir(directory); }
};

std::int64_t monotonic_ns() {
    timespec value{};
    clock_gettime(CLOCK_MONOTONIC, &value);
    return static_cast<std::int64_t>(value.tv_sec) * 1'000'000'000LL + value.tv_nsec;
}

std::string join_path(const std::string &parent, const std::string &name) {
    return parent == "/" ? parent + name : parent + "/" + name;
}

std::string parent_path(const std::string &path) {
    const auto slash = path.find_last_of('/');
    return slash == 0 ? "/" : path.substr(0, slash);
}

std::string base_name(const std::string &path) { return path.substr(path.find_last_of('/') + 1); }

std::uint32_t read_watch_limit() {
    std::ifstream input("/proc/sys/fs/inotify/max_user_watches");
    std::uint64_t value = 0;
    input >> value;
    return value > std::numeric_limits<std::uint32_t>::max()
               ? std::numeric_limits<std::uint32_t>::max()
               : static_cast<std::uint32_t>(value);
}

core::EventKind kind_for_mask(std::uint32_t mask) {
    if ((mask & IN_CREATE) != 0U) {
        return core::EventKind::CREATE;
    }
    if ((mask & IN_DELETE) != 0U) {
        return core::EventKind::DELETE;
    }
    if ((mask & IN_MODIFY) != 0U) {
        return core::EventKind::MODIFY;
    }
    if ((mask & IN_CLOSE_WRITE) != 0U) {
        return core::EventKind::CLOSE_WRITE;
    }
    if ((mask & IN_ATTRIB) != 0U) {
        return core::EventKind::ATTRIB;
    }
    if ((mask & IN_MOVED_FROM) != 0U) {
        return core::EventKind::MOVED_OUT;
    }
    if ((mask & IN_MOVED_TO) != 0U) {
        return core::EventKind::MOVED_IN;
    }
    if ((mask & IN_DELETE_SELF) != 0U) {
        return core::EventKind::DELETE_SELF;
    }
    if ((mask & IN_MOVE_SELF) != 0U) {
        return core::EventKind::MOVE_SELF;
    }
    return core::EventKind::UNMOUNT;
}

} // namespace

class Watcher::Impl {
  public:
    Impl(core::UniqueFd inotify_fd, core::UniqueFd wake_fd)
        : inotify_fd_(std::move(inotify_fd)), wake_fd_(std::move(wake_fd)),
          watch_limit_(read_watch_limit()) {
        dispatcher_connection_ = dispatcher_.connect(sigc::mem_fun(*this, &Impl::drain_messages));
        worker_ = std::thread(&Impl::run, this);
    }

    ~Impl() {
        dispatcher_connection_.disconnect();
        enqueue(Command{CommandKind::STOP});
        if (worker_.joinable()) {
            worker_.join();
        }
        std::lock_guard<std::mutex> lock(delivery_mutex_);
        delivery_.clear();
    }

    core::Result<core::WatchId> add_root(const std::string &input, WatchMode mode) {
        auto valid = core::validate_absolute(input);
        if (!valid) {
            return valid.error();
        }
        const std::string path = core::canonicalize(input);
        struct stat status {};
        if (lstat(path.c_str(), &status) != 0) {
            return core::error_from_errno(errno, "lstat", path);
        }
        if (!S_ISDIR(status.st_mode) || S_ISLNK(status.st_mode)) {
            return core::make_error(core::ErrorCode::NOT_DIRECTORY,
                                    "add watch: path is not a directory");
        }
        for (const auto &[id, info] : main_roots_) {
            static_cast<void>(id);
            if (core::is_ancestor(info.path, path) || core::is_ancestor(path, info.path)) {
                return core::make_error(core::ErrorCode::EXISTS, "add watch: overlapping root");
            }
        }
        core::WatchId id = next_watch_id_++;
        if (id == 0) {
            id = next_watch_id_++;
        }
        WatchInfo info;
        info.id = id;
        info.path = path;
        info.mode = mode;
        main_roots_.emplace(id, info);
        Command command{CommandKind::ADD};
        command.info = info;
        enqueue(std::move(command));
        return id;
    }

    bool remove_root(core::WatchId id) {
        if (main_roots_.find(id) == main_roots_.end()) {
            return false;
        }
        Command command{CommandKind::REMOVE};
        command.id = id;
        enqueue(std::move(command));
        return true;
    }

    std::vector<WatchInfo> list_roots() const {
        std::vector<WatchInfo> result;
        result.reserve(main_roots_.size());
        for (const auto &[id, info] : main_roots_) {
            static_cast<void>(id);
            result.push_back(info);
        }
        std::sort(result.begin(), result.end(),
                  [](const WatchInfo &left, const WatchInfo &right) { return left.id < right.id; });
        return result;
    }

    WatchUsage usage() const {
        WatchUsage result;
        result.limit = watch_limit_;
        for (const auto &[id, info] : main_roots_) {
            static_cast<void>(id);
            result.used += info.watch_count;
        }
        return result;
    }

    core::WatchId find_root(const std::string &input) const {
        const std::string path = core::canonicalize(input);
        for (const auto &[id, info] : main_roots_) {
            if (info.state != WatchState::STOPPED && info.state != WatchState::ERROR &&
                core::is_ancestor(info.path, path)) {
                return id;
            }
        }
        return 0;
    }

    std::optional<RootTotal> root_total(core::WatchId id) const {
        const auto found = main_roots_.find(id);
        if (found == main_roots_.end() || !found->second.total_known) {
            return std::nullopt;
        }
        return RootTotal{found->second.total_bytes, found->second.file_count,
                         found->second.total_partial};
    }

    void inject_overflow() { enqueue(Command{CommandKind::OVERFLOW}); }

    void set_queue_capacity(std::uint32_t capacity) {
        Command command{CommandKind::CAPACITY};
        command.value = std::max(capacity, 1U);
        enqueue(std::move(command));
    }

    void set_move_window_ms(std::uint32_t ms) {
        Command command{CommandKind::MOVE_WINDOW};
        command.value = ms;
        enqueue(std::move(command));
    }

    sigc::signal<void(const std::vector<core::EventPtr> &)> event_signal_;
    sigc::signal<void(const WatchInfo &, const std::optional<core::Error> &)> state_signal_;

  private:
    enum class CommandKind : std::uint8_t { ADD, REMOVE, OVERFLOW, CAPACITY, MOVE_WINDOW, STOP };
    struct Command {
        explicit Command(CommandKind command_kind) : kind(command_kind) {}
        CommandKind kind;
        WatchInfo info;
        core::WatchId id = 0;
        std::uint32_t value = 0;
    };
    struct Directory {
        core::WatchId root_id = 0;
        std::string path;
        std::uint64_t device = 0;
        std::unordered_map<std::string, std::uint64_t> files;
    };
    struct Root {
        WatchInfo info;
        std::uint64_t device = 0;
        std::unordered_set<int> wds;
    };
    struct PendingMove {
        core::WatchId root_id = 0;
        std::string path;
        bool is_dir = false;
        std::uint32_t cookie = 0;
        std::uint32_t raw_mask = 0;
        std::int64_t observed_at_us = 0;
        std::int64_t received_ns = 0;
    };
    struct StateMessage {
        WatchInfo info;
        std::optional<core::Error> error;
        bool notify = true;
    };
    using Batch = std::vector<core::EventPtr>;
    using Message = std::variant<StateMessage, Batch>;

    void enqueue(Command command) {
        {
            std::lock_guard<std::mutex> lock(command_mutex_);
            commands_.push_back(std::move(command));
        }
        const std::uint64_t one = 1;
        const auto ignored = write(wake_fd_.get(), &one, sizeof(one));
        static_cast<void>(ignored);
    }

    void run() noexcept {
        try {
            run_loop();
        } catch (const std::exception &error) {
            g_warning("mon watcher exception: %s", error.what());
        } catch (...) {
            g_warning("mon watcher unknown exception");
        }
    }

    void run_loop() {
        alignas(inotify_event) std::array<std::uint8_t, kReadBufferSize> buffer{};
        bool stopping = false;
        while (!stopping) {
            const int timeout = poll_timeout_ms();
            pollfd descriptors[2] = {{inotify_fd_.get(), POLLIN, 0}, {wake_fd_.get(), POLLIN, 0}};
            const int ready = poll(descriptors, 2, timeout);
            if (ready < 0 && errno != EINTR) {
                g_warning("mon poll failed: %s", std::strerror(errno));
            }
            if ((descriptors[1].revents & POLLIN) != 0) {
                std::uint64_t value = 0;
                while (read(wake_fd_.get(), &value, sizeof(value)) < 0 && errno == EINTR) {
                }
                stopping = process_commands();
            }
            if ((descriptors[0].revents & POLLIN) != 0) {
                for (;;) {
                    const ssize_t count = read(inotify_fd_.get(), buffer.data(), buffer.size());
                    if (count < 0 && errno == EINTR) {
                        continue;
                    }
                    if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                        break;
                    }
                    if (count <= 0) {
                        break;
                    }
                    process_raw(buffer.data(), static_cast<std::size_t>(count));
                }
            }
            expire_moves();
            flush_if_due();
        }
        batch_.clear();
        pending_moves_.clear();
        roots_.clear();
        directories_.clear();
    }

    int poll_timeout_ms() const {
        int timeout = batch_.empty() ? -1 : 16;
        const auto now = monotonic_ns();
        for (const auto &[cookie, move] : pending_moves_) {
            static_cast<void>(cookie);
            const auto remaining =
                move.received_ns + static_cast<std::int64_t>(move_window_ms_) * 1'000'000LL - now;
            timeout =
                std::min(timeout < 0 ? 50 : timeout,
                         static_cast<int>(std::max<std::int64_t>(remaining / 1'000'000LL, 0)));
        }
        return timeout;
    }

    bool process_commands() {
        std::deque<Command> commands;
        {
            std::lock_guard<std::mutex> lock(command_mutex_);
            commands.swap(commands_);
        }
        for (auto &command : commands) {
            switch (command.kind) {
            case CommandKind::ADD:
                register_root(std::move(command.info));
                break;
            case CommandKind::REMOVE:
                stop_root(command.id, WatchReason::USER, false);
                break;
            case CommandKind::OVERFLOW:
                handle_overflow();
                break;
            case CommandKind::CAPACITY:
                queue_capacity_ = command.value;
                break;
            case CommandKind::MOVE_WINDOW:
                move_window_ms_ = command.value;
                break;
            case CommandKind::STOP:
                return true;
            }
        }
        return false;
    }

    void register_root(WatchInfo info) {
        struct stat status {};
        if (lstat(info.path.c_str(), &status) != 0) {
            fail_registration(std::move(info), errno);
            return;
        }
        Root root;
        root.info = std::move(info);
        root.device = static_cast<std::uint64_t>(status.st_dev);
        const core::WatchId root_id = root.info.id;
        roots_.emplace(root_id, std::move(root));
        auto &stored = roots_.at(root_id);
        const bool success = scan_directory(stored, stored.info.path, false);
        if (stored.info.watch_count == 0) {
            const int saved = errno == 0 ? EIO : errno;
            fail_existing_root(stored.info.id, saved);
            return;
        }
        drain_registration_events();
        stored.info.total_known = true;
        stored.info.state = stored.info.total_partial ? WatchState::DEGRADED : WatchState::ACTIVE;
        stored.info.reason =
            stored.info.total_partial ? WatchReason::WATCH_LIMIT : WatchReason::NONE;
        std::optional<core::Error> error;
        if (!success && stored.info.total_partial) {
            error = core::make_error(core::ErrorCode::WATCH_LIMIT,
                                     "inotify_add_watch: watch limit reached");
        }
        publish_state(stored.info, std::move(error));
    }

    void drain_registration_events() {
        alignas(inotify_event) std::array<std::uint8_t, kReadBufferSize> buffer{};
        for (;;) {
            const ssize_t count = read(inotify_fd_.get(), buffer.data(), buffer.size());
            if (count < 0 && errno == EINTR) {
                continue;
            }
            if (count <= 0) {
                return;
            }
            process_raw(buffer.data(), static_cast<std::size_t>(count));
        }
    }

    void fail_registration(WatchInfo info, int saved_errno) {
        info.state = WatchState::ERROR;
        info.reason = WatchReason::IO;
        publish_state(info, core::error_from_errno(saved_errno, "register watch", info.path));
    }

    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    void fail_existing_root(core::WatchId id, int saved_errno) {
        auto found = roots_.find(id);
        if (found == roots_.end()) {
            return;
        }
        WatchInfo info = found->second.info;
        clear_root_watches(found->second);
        roots_.erase(found);
        info.state = WatchState::ERROR;
        info.reason = WatchReason::IO;
        info.total_known = false;
        publish_state(info, core::error_from_errno(saved_errno, "register watch", info.path));
    }

    bool scan_directory(Root &root, const std::string &path, bool synthetic) {
        struct stat directory_status {};
        if (lstat(path.c_str(), &directory_status) != 0 || !S_ISDIR(directory_status.st_mode) ||
            S_ISLNK(directory_status.st_mode) ||
            static_cast<std::uint64_t>(directory_status.st_dev) != root.device) {
            root.info.total_partial = true;
            return false;
        }
        const int wd = inotify_add_watch(inotify_fd_.get(), path.c_str(), kWatchMask);
        if (wd < 0) {
            if (errno == ENOSPC) {
                root.info.total_partial = true;
            }
            return false;
        }
        auto existing = directories_.find(wd);
        if (existing != directories_.end()) {
            return existing->second.path == path;
        }
        Directory node;
        node.root_id = root.info.id;
        node.path = path;
        node.device = root.device;
        directories_.emplace(wd, std::move(node));
        root.wds.insert(wd);
        root.info.watch_count = static_cast<std::uint32_t>(root.wds.size());

        std::unique_ptr<DIR, DirectoryCloser> directory(opendir(path.c_str()));
        if (!directory) {
            root.info.total_partial = true;
            return false;
        }
        bool complete = true;
        while (dirent *entry = readdir(directory.get())) {
            const std::string name(entry->d_name);
            if (name == "." || name == "..") {
                continue;
            }
            const std::string child = join_path(path, name);
            struct stat status {};
            if (lstat(child.c_str(), &status) != 0) {
                complete = false;
                continue;
            }
            if (S_ISREG(status.st_mode)) {
                const auto size = static_cast<std::uint64_t>(status.st_size);
                directories_.at(wd).files[name] = size;
                root.info.total_bytes += size;
                ++root.info.file_count;
                if (synthetic) {
                    core::Event event = make_base_event(root.info.id, core::EventKind::CREATE,
                                                        child, IN_CREATE, false);
                    event.synthetic = true;
                    event.stat = stat_for(status);
                    set_known_delta(event, root, static_cast<std::int64_t>(size));
                    append_event(std::move(event));
                }
            } else if (S_ISDIR(status.st_mode) && root.info.mode == WatchMode::RECURSIVE &&
                       static_cast<std::uint64_t>(status.st_dev) == root.device) {
                if (!scan_directory(root, child, synthetic)) {
                    complete = false;
                }
            }
        }
        return complete;
    }

    void process_raw(const std::uint8_t *data, std::size_t size) {
        std::size_t offset = 0;
        while (offset + sizeof(inotify_event) <= size) {
            const auto *raw = reinterpret_cast<const inotify_event *>(data + offset);
            const std::size_t event_size = sizeof(inotify_event) + raw->len;
            if (event_size > size - offset) {
                break;
            }
            process_one(*raw);
            offset += event_size;
        }
    }

    void process_one(const inotify_event &raw) {
        if ((raw.mask & IN_Q_OVERFLOW) != 0U) {
            handle_overflow();
            return;
        }
        const auto directory_it = directories_.find(raw.wd);
        if (directory_it == directories_.end()) {
            return;
        }
        const Directory snapshot = directory_it->second;
        auto root_it = roots_.find(snapshot.root_id);
        if (root_it == roots_.end()) {
            return;
        }
        Root &root = root_it->second;
        const std::string name = raw.len == 0 ? std::string() : std::string(raw.name);
        const std::string path = name.empty() ? snapshot.path : join_path(snapshot.path, name);
        const bool is_dir = (raw.mask & IN_ISDIR) != 0U;

        if ((raw.mask & IN_IGNORED) != 0U) {
            erase_watch(raw.wd, root);
            return;
        }
        if ((raw.mask & (IN_DELETE_SELF | IN_MOVE_SELF | IN_UNMOUNT)) != 0U &&
            path == root.info.path) {
            const auto kind = kind_for_mask(raw.mask);
            core::Event event = make_base_event(root.info.id, kind, path, raw.mask, true);
            event.size_delta.reset();
            event.root_total.reset();
            append_event(std::move(event));
            const WatchReason reason =
                kind == core::EventKind::DELETE_SELF ? WatchReason::ROOT_DELETED
                : kind == core::EventKind::MOVE_SELF ? WatchReason::ROOT_MOVED
                                                     : WatchReason::UNMOUNTED;
            stop_root(root.info.id, reason, true);
            return;
        }
        if (name.empty()) {
            if ((raw.mask & IN_ATTRIB) != 0U && path == root.info.path) {
                append_file_event(root, snapshot, path, name, core::EventKind::ATTRIB, raw.mask,
                                  true);
            }
            return;
        }
        if ((raw.mask & IN_MOVED_FROM) != 0U) {
            PendingMove move;
            move.root_id = root.info.id;
            move.path = path;
            move.is_dir = is_dir;
            move.cookie = raw.cookie;
            move.raw_mask = raw.mask;
            move.observed_at_us = g_get_real_time();
            move.received_ns = monotonic_ns();
            pending_moves_[raw.cookie] = std::move(move);
            return;
        }
        if ((raw.mask & IN_MOVED_TO) != 0U) {
            auto pending = pending_moves_.find(raw.cookie);
            if (pending != pending_moves_.end()) {
                handle_paired_move(root, pending->second, path, raw.mask, is_dir);
                pending_moves_.erase(pending);
            } else {
                handle_moved_in(root, path, raw.mask, is_dir, raw.cookie);
            }
            return;
        }
        if ((raw.mask & IN_CREATE) != 0U) {
            if (is_dir && root.info.mode == WatchMode::RECURSIVE) {
                core::Event event =
                    make_base_event(root.info.id, core::EventKind::CREATE, path, raw.mask, true);
                set_known_delta(event, root, 0);
                append_event(std::move(event));
                scan_directory(root, path, true);
                publish_state(root.info, std::nullopt);
            } else {
                append_file_event(root, snapshot, path, name, core::EventKind::CREATE, raw.mask,
                                  is_dir);
            }
            return;
        }
        if ((raw.mask & IN_DELETE) != 0U) {
            append_delete(root, snapshot, path, name, raw.mask, is_dir);
            return;
        }
        if ((raw.mask & IN_MODIFY) != 0U) {
            append_file_event(root, snapshot, path, name, core::EventKind::MODIFY, raw.mask,
                              is_dir);
        }
        if ((raw.mask & IN_CLOSE_WRITE) != 0U) {
            append_file_event(root, snapshot, path, name, core::EventKind::CLOSE_WRITE, raw.mask,
                              is_dir);
        }
        if ((raw.mask & IN_ATTRIB) != 0U) {
            append_file_event(root, snapshot, path, name, core::EventKind::ATTRIB, raw.mask,
                              is_dir);
        }
    }

    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    void append_file_event(Root &root, const Directory &directory, const std::string &path,
                           const std::string &name, core::EventKind kind, std::uint32_t mask,
                           bool is_dir) {
        core::Event event = make_base_event(root.info.id, kind, path, mask, is_dir);
        if (is_dir) {
            set_known_delta(event, root, 0);
            append_event(std::move(event));
            return;
        }
        struct stat status {};
        const bool exists = lstat(path.c_str(), &status) == 0;
        auto node = directories_.find(find_wd(directory.path));
        if (!exists || node == directories_.end()) {
            event.size_delta.reset();
            event.root_total.reset();
            append_event(std::move(event));
            return;
        }
        event.stat = stat_for(status);
        if (!S_ISREG(status.st_mode)) {
            set_known_delta(event, root, 0);
            append_event(std::move(event));
            return;
        }
        const std::uint64_t size = static_cast<std::uint64_t>(status.st_size);
        auto old = node->second.files.find(name);
        const std::uint64_t previous = old == node->second.files.end() ? 0 : old->second;
        if (old == node->second.files.end()) {
            ++root.info.file_count;
            publish_snapshot(root.info);
        }
        node->second.files[name] = size;
        apply_size_change(root, previous, size);
        set_known_delta(event, root, signed_difference(size, previous));
        append_event(std::move(event));
    }

    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    void append_delete(Root &root, const Directory &directory, const std::string &path,
                       const std::string &name, std::uint32_t mask, bool is_dir) {
        core::Event event =
            make_base_event(root.info.id, core::EventKind::DELETE, path, mask, is_dir);
        std::uint64_t removed = 0;
        if (is_dir) {
            removed = remove_subtree(root, path);
        } else {
            auto node = directories_.find(find_wd(directory.path));
            if (node != directories_.end()) {
                auto file = node->second.files.find(name);
                if (file != node->second.files.end()) {
                    removed = file->second;
                    node->second.files.erase(file);
                    --root.info.file_count;
                    root.info.total_bytes -= removed;
                    publish_snapshot(root.info);
                }
            }
        }
        set_known_delta(event, root, -static_cast<std::int64_t>(removed));
        append_event(std::move(event));
    }

    void handle_paired_move(Root &root, const PendingMove &from, const std::string &path,
                            std::uint32_t to_mask, bool is_dir) {
        core::Event event =
            make_base_event(root.info.id,
                            parent_path(from.path) == parent_path(path) ? core::EventKind::RENAME
                                                                        : core::EventKind::MOVE,
                            path, from.raw_mask | to_mask, is_dir);
        event.old_path = from.path;
        event.cookie = from.cookie;
        event.observed_at_us = from.observed_at_us;
        event.monotonic_ns = from.received_ns;
        event.last_monotonic_ns = monotonic_ns();
        rename_index(root, from.path, path, is_dir);
        set_known_delta(event, root, 0);
        append_event(std::move(event));
    }

    void handle_moved_in(Root &root, const std::string &path, std::uint32_t mask, bool is_dir,
                         std::uint32_t cookie) {
        const std::uint64_t before = root.info.total_bytes;
        if (is_dir && root.info.mode == WatchMode::RECURSIVE) {
            scan_directory(root, path, false);
            publish_state(root.info, std::nullopt);
        } else if (!is_dir) {
            const int wd = find_wd(parent_path(path));
            struct stat status {};
            if (wd >= 0 && lstat(path.c_str(), &status) == 0 && S_ISREG(status.st_mode)) {
                const auto size = static_cast<std::uint64_t>(status.st_size);
                directories_.at(wd).files[base_name(path)] = size;
                root.info.total_bytes += size;
                ++root.info.file_count;
                publish_snapshot(root.info);
            }
        }
        core::Event event =
            make_base_event(root.info.id, core::EventKind::MOVED_IN, path, mask, is_dir);
        event.cookie = cookie;
        set_known_delta(event, root, signed_difference(root.info.total_bytes, before));
        append_event(std::move(event));
    }

    void expire_moves() {
        const auto now = monotonic_ns();
        std::vector<std::uint32_t> expired;
        for (const auto &[cookie, move] : pending_moves_) {
            if (now - move.received_ns >=
                static_cast<std::int64_t>(move_window_ms_) * 1'000'000LL) {
                auto root = roots_.find(move.root_id);
                if (root != roots_.end()) {
                    const std::uint64_t before = root->second.info.total_bytes;
                    if (move.is_dir) {
                        remove_subtree(root->second, move.path);
                    } else {
                        remove_file_by_path(root->second, move.path);
                    }
                    publish_snapshot(root->second.info);
                    core::Event event = make_base_event(move.root_id, core::EventKind::MOVED_OUT,
                                                        move.path, move.raw_mask, move.is_dir);
                    event.cookie = cookie;
                    event.observed_at_us = move.observed_at_us;
                    event.monotonic_ns = move.received_ns;
                    set_known_delta(event, root->second,
                                    signed_difference(root->second.info.total_bytes, before));
                    append_event(std::move(event));
                }
                expired.push_back(cookie);
            }
        }
        for (const auto cookie : expired) {
            pending_moves_.erase(cookie);
        }
    }

    void handle_overflow() {
        pending_moves_.clear();
        flush_batch(true);
        const std::uint64_t gap = next_gap_id_++;
        core::Event overflow = make_base_event(0, core::EventKind::OVERFLOW, "", 0, false);
        overflow.gap_id = gap;
        overflow.size_delta.reset();
        overflow.root_total.reset();
        append_event(std::move(overflow));
        flush_batch(true);
        for (auto &[id, root] : roots_) {
            static_cast<void>(id);
            root.info.state = WatchState::DEGRADED;
            root.info.reason = WatchReason::OVERFLOW;
            root.info.total_known = false;
            publish_state(root.info, core::make_error(core::ErrorCode::IO, "inotify overflow"));
            root.info.state = WatchState::RESYNCING;
            publish_state(root.info, std::nullopt);
        }
        for (auto &[id, root] : roots_) {
            static_cast<void>(id);
            const std::uint64_t before = root.info.total_bytes;
            clear_index(root);
            root.info.total_partial = false;
            scan_directory(root, root.info.path, false);
            root.info.total_known = true;
            core::Event done = make_base_event(root.info.id, core::EventKind::RESYNC_DONE,
                                               root.info.path, 0, false);
            done.gap_id = gap;
            set_known_delta(done, root, signed_difference(root.info.total_bytes, before));
            append_event(std::move(done));
            root.info.state = root.info.total_partial ? WatchState::DEGRADED : WatchState::ACTIVE;
            root.info.reason =
                root.info.total_partial ? WatchReason::WATCH_LIMIT : WatchReason::NONE;
            publish_state(root.info, std::nullopt);
        }
        flush_batch(true);
    }

    core::Event make_base_event(core::WatchId id, core::EventKind kind, const std::string &path,
                                std::uint32_t mask, bool is_dir) {
        core::Event event;
        event.seq = next_seq_++;
        event.observed_at_us = g_get_real_time();
        event.monotonic_ns = monotonic_ns();
        event.last_monotonic_ns = event.monotonic_ns;
        event.raw_mask = mask;
        event.kind = kind;
        event.watch_id = id;
        event.path = path;
        event.is_dir = is_dir;
        return event;
    }

    static core::EventStat stat_for(const struct stat &status) {
        core::EventStat result;
        result.size = static_cast<std::uint64_t>(status.st_size);
        result.mtime_ns = static_cast<std::int64_t>(status.st_mtim.tv_sec) * 1'000'000'000LL +
                          status.st_mtim.tv_nsec;
        result.mode = static_cast<std::uint32_t>(status.st_mode);
        return result;
    }

    static std::int64_t signed_difference(std::uint64_t value, std::uint64_t previous) {
        if (value >= previous) {
            return static_cast<std::int64_t>(std::min<std::uint64_t>(
                value - previous,
                static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())));
        }
        return -static_cast<std::int64_t>(std::min<std::uint64_t>(
            previous - value,
            static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())));
    }

    static void apply_size_change(Root &root, std::uint64_t previous, std::uint64_t value) {
        if (value >= previous) {
            root.info.total_bytes += value - previous;
        } else {
            root.info.total_bytes -= previous - value;
        }
    }

    static void set_known_delta(core::Event &event, const Root &root, std::int64_t delta) {
        if (!root.info.total_known) {
            event.size_delta.reset();
            event.root_total.reset();
            return;
        }
        event.size_delta = delta;
        event.root_total = root.info.total_bytes;
    }

    void append_event(core::Event event) {
        if (!batch_.empty() &&
            (event.kind == core::EventKind::MODIFY || event.kind == core::EventKind::ATTRIB)) {
            core::Event &previous = batch_.back();
            if (previous.kind == event.kind && previous.path == event.path) {
                previous.merged_count += event.merged_count;
                previous.last_monotonic_ns = event.last_monotonic_ns;
                previous.raw_mask |= event.raw_mask;
                previous.stat = event.stat;
                if (previous.size_delta && event.size_delta) {
                    previous.size_delta = *previous.size_delta + *event.size_delta;
                } else {
                    previous.size_delta.reset();
                }
                previous.root_total = event.root_total;
                return;
            }
        }
        if (batch_.empty()) {
            batch_started_ns_ = monotonic_ns();
        }
        batch_.push_back(std::move(event));
        if (batch_.size() >= 256) {
            flush_batch(false);
        }
    }

    void flush_if_due() {
        if (!batch_.empty() && monotonic_ns() - batch_started_ns_ >= 16'000'000LL) {
            flush_batch(false);
        }
    }

    void flush_batch(bool bypass_capacity) {
        if (batch_.empty()) {
            return;
        }
        if (!bypass_capacity && pending_events_.load() + batch_.size() > queue_capacity_) {
            batch_.clear();
            handle_overflow();
            return;
        }
        Batch output;
        output.reserve(batch_.size());
        for (auto &event : batch_) {
            output.push_back(core::make_event(std::move(event)));
        }
        batch_.clear();
        pending_events_.fetch_add(output.size());
        publish_message(std::move(output));
    }

    void publish_state(const WatchInfo &info, std::optional<core::Error> error) {
        publish_message(StateMessage{info, std::move(error), true});
    }

    void publish_snapshot(const WatchInfo &info) {
        publish_message(StateMessage{info, std::nullopt, false});
    }

    void publish_message(Message message) {
        {
            std::lock_guard<std::mutex> lock(delivery_mutex_);
            delivery_.push_back(std::move(message));
        }
        dispatcher_.emit();
    }

    void drain_messages() {
        std::deque<Message> messages;
        {
            std::lock_guard<std::mutex> lock(delivery_mutex_);
            messages.swap(delivery_);
        }
        for (auto &message : messages) {
            if (auto *state = std::get_if<StateMessage>(&message)) {
                main_roots_[state->info.id] = state->info;
                if (state->notify) {
                    state_signal_.emit(state->info, state->error);
                }
                if (state->info.state == WatchState::STOPPED) {
                    main_roots_.erase(state->info.id);
                }
            } else {
                auto &batch = std::get<Batch>(message);
                pending_events_.fetch_sub(batch.size());
                for (const auto &event : batch) {
                    auto root = main_roots_.find(event->watch_id);
                    if (root != main_roots_.end() && event->root_total.has_value()) {
                        root->second.total_bytes = event->root_total.value_or(0);
                        root->second.total_known = true;
                    }
                }
                event_signal_.emit(batch);
            }
        }
    }

    void stop_root(core::WatchId id, WatchReason reason, bool index_unknown) {
        auto found = roots_.find(id);
        if (found == roots_.end()) {
            return;
        }
        WatchInfo info = found->second.info;
        clear_root_watches(found->second);
        roots_.erase(found);
        info.state = WatchState::STOPPED;
        info.reason = reason;
        info.watch_count = 0;
        info.total_known = !index_unknown && reason == WatchReason::USER;
        publish_state(info, std::nullopt);
    }

    void clear_root_watches(Root &root) {
        const auto wds = root.wds;
        for (const int wd : wds) {
            inotify_rm_watch(inotify_fd_.get(), wd);
            directories_.erase(wd);
        }
        root.wds.clear();
        root.info.watch_count = 0;
    }

    void clear_index(Root &root) {
        clear_root_watches(root);
        root.info.total_bytes = 0;
        root.info.file_count = 0;
    }

    void erase_watch(int wd, Root &root) {
        directories_.erase(wd);
        root.wds.erase(wd);
        root.info.watch_count = static_cast<std::uint32_t>(root.wds.size());
        publish_state(root.info, std::nullopt);
    }

    int find_wd(const std::string &path) const {
        for (const auto &[wd, directory] : directories_) {
            if (directory.path == path) {
                return wd;
            }
        }
        return -1;
    }

    void remove_file_by_path(Root &root, const std::string &path) {
        const int wd = find_wd(parent_path(path));
        if (wd < 0) {
            return;
        }
        auto file = directories_.at(wd).files.find(base_name(path));
        if (file != directories_.at(wd).files.end()) {
            root.info.total_bytes -= file->second;
            --root.info.file_count;
            directories_.at(wd).files.erase(file);
        }
    }

    std::uint64_t remove_subtree(Root &root, const std::string &path) {
        std::vector<int> removed_wds;
        std::uint64_t removed = 0;
        for (const auto &[wd, directory] : directories_) {
            if (directory.root_id == root.info.id && core::is_ancestor(path, directory.path)) {
                removed_wds.push_back(wd);
                for (const auto &[name, size] : directory.files) {
                    static_cast<void>(name);
                    removed += size;
                    --root.info.file_count;
                }
            }
        }
        for (const int wd : removed_wds) {
            inotify_rm_watch(inotify_fd_.get(), wd);
            directories_.erase(wd);
            root.wds.erase(wd);
        }
        root.info.watch_count = static_cast<std::uint32_t>(root.wds.size());
        root.info.total_bytes -= removed;
        return removed;
    }

    void rename_index(Root &root, const std::string &old_path, const std::string &new_path,
                      bool is_dir) {
        if (!is_dir) {
            const int old_wd = find_wd(parent_path(old_path));
            const int new_wd = find_wd(parent_path(new_path));
            if (old_wd < 0 || new_wd < 0) {
                return;
            }
            auto file = directories_.at(old_wd).files.find(base_name(old_path));
            if (file != directories_.at(old_wd).files.end()) {
                directories_.at(new_wd).files[base_name(new_path)] = file->second;
                directories_.at(old_wd).files.erase(file);
            }
            return;
        }
        for (auto &[wd, directory] : directories_) {
            static_cast<void>(wd);
            if (directory.root_id == root.info.id && core::is_ancestor(old_path, directory.path)) {
                directory.path = new_path + directory.path.substr(old_path.size());
            }
        }
    }

    core::UniqueFd inotify_fd_;
    core::UniqueFd wake_fd_;
    Glib::Dispatcher dispatcher_;
    sigc::connection dispatcher_connection_;
    std::thread worker_;
    mutable std::unordered_map<core::WatchId, WatchInfo> main_roots_;
    core::WatchId next_watch_id_ = 1;
    std::uint32_t watch_limit_ = 0;
    std::mutex command_mutex_;
    std::deque<Command> commands_;
    std::mutex delivery_mutex_;
    std::deque<Message> delivery_;
    std::atomic<std::size_t> pending_events_{0};
    std::unordered_map<core::WatchId, Root> roots_;
    std::unordered_map<int, Directory> directories_;
    std::unordered_map<std::uint32_t, PendingMove> pending_moves_;
    std::vector<core::Event> batch_;
    std::int64_t batch_started_ns_ = 0;
    std::uint64_t next_seq_ = 1;
    std::uint64_t next_gap_id_ = 1;
    std::uint32_t queue_capacity_ = kDefaultQueueCapacity;
    std::uint32_t move_window_ms_ = 50;
};

Watcher::Watcher(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Watcher::~Watcher() = default;

core::Result<std::unique_ptr<Watcher>> Watcher::create() {
    const int inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (inotify_fd < 0) {
        return core::error_from_errno(errno, "inotify_init1", "inotify");
    }
    core::UniqueFd owned_inotify(inotify_fd);
    const int wake_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (wake_fd < 0) {
        return core::error_from_errno(errno, "eventfd", "mon watcher");
    }
    return std::make_unique<Watcher>(
        std::make_unique<Impl>(std::move(owned_inotify), core::UniqueFd(wake_fd)));
}

core::Result<core::WatchId> Watcher::add_root(const std::string &path, WatchMode mode) {
    return impl_->add_root(path, mode);
}
bool Watcher::remove_root(core::WatchId id) { return impl_->remove_root(id); }
std::vector<WatchInfo> Watcher::list_roots() const { return impl_->list_roots(); }
WatchUsage Watcher::usage() const { return impl_->usage(); }
core::WatchId Watcher::find_root(const std::string &path) const { return impl_->find_root(path); }
std::optional<RootTotal> Watcher::root_total(core::WatchId id) const {
    return impl_->root_total(id);
}
sigc::signal<void(const std::vector<core::EventPtr> &)> &Watcher::signal_events() {
    return impl_->event_signal_;
}
sigc::signal<void(const WatchInfo &, const std::optional<core::Error> &)> &Watcher::signal_state() {
    return impl_->state_signal_;
}

void testing_inject_overflow(Watcher &watcher) { watcher.impl_->inject_overflow(); }
void testing_set_queue_capacity(Watcher &watcher, std::uint32_t capacity) {
    watcher.impl_->set_queue_capacity(capacity);
}
void testing_set_move_window_ms(Watcher &watcher, std::uint32_t ms) {
    watcher.impl_->set_move_window_ms(ms);
}

namespace testing {

void inject_overflow(Watcher &watcher) { testing_inject_overflow(watcher); }
void set_queue_capacity(Watcher &watcher, std::uint32_t capacity) {
    testing_set_queue_capacity(watcher, capacity);
}
void set_move_window_ms(Watcher &watcher, std::uint32_t ms) {
    testing_set_move_window_ms(watcher, ms);
}

std::vector<core::Event> normalize_raw_events(const std::vector<std::uint8_t> &buffer,
                                              const std::vector<internal::RawWatch> &watches) {
    std::unordered_map<int, internal::RawWatch> by_wd;
    for (const auto &watch : watches) {
        by_wd.emplace(watch.wd, watch);
    }
    std::unordered_map<std::uint32_t, core::Event> moves;
    std::vector<core::Event> result;
    std::size_t offset = 0;
    while (offset + sizeof(inotify_event) <= buffer.size()) {
        const auto *raw = reinterpret_cast<const inotify_event *>(buffer.data() + offset);
        const std::size_t event_size = sizeof(inotify_event) + raw->len;
        if (event_size > buffer.size() - offset) {
            break;
        }
        if ((raw->mask & IN_Q_OVERFLOW) != 0U) {
            core::Event event;
            event.kind = core::EventKind::OVERFLOW;
            result.push_back(std::move(event));
            offset += event_size;
            continue;
        }
        auto watch = by_wd.find(raw->wd);
        if (watch == by_wd.end() || (raw->mask & IN_IGNORED) != 0U) {
            offset += event_size;
            continue;
        }
        core::Event event;
        event.raw_mask = raw->mask;
        event.watch_id = watch->second.watch_id;
        event.path = raw->len == 0 ? watch->second.path
                                   : join_path(watch->second.path, std::string(raw->name));
        event.cookie = raw->cookie;
        event.is_dir = (raw->mask & IN_ISDIR) != 0U;
        event.kind = kind_for_mask(raw->mask);
        if ((raw->mask & IN_MOVED_FROM) != 0U) {
            moves[raw->cookie] = std::move(event);
        } else if ((raw->mask & IN_MOVED_TO) != 0U) {
            auto from = moves.find(raw->cookie);
            if (from == moves.end()) {
                result.push_back(std::move(event));
            } else {
                event.old_path = from->second.path;
                event.raw_mask |= from->second.raw_mask;
                event.kind = parent_path(event.old_path) == parent_path(event.path)
                                 ? core::EventKind::RENAME
                                 : core::EventKind::MOVE;
                result.push_back(std::move(event));
                moves.erase(from);
            }
        } else {
            result.push_back(std::move(event));
        }
        offset += event_size;
    }
    for (auto &[cookie, event] : moves) {
        static_cast<void>(cookie);
        result.push_back(std::move(event));
    }
    return result;
}

} // namespace testing
} // namespace filescope::mon
