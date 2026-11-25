#include "his/his_internal.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <future>
#include <map>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <variant>

#include <glib.h>
#include <glibmm/dispatcher.h>
#include <glibmm/miscutils.h>
#include <sys/stat.h>

#include "core/path.hpp"

namespace filescope::his {

namespace {

namespace disk = std::filesystem;
using core::ErrorCode;
using internal::Backlog;
using internal::Store;
using Clock = std::chrono::steady_clock;

struct Batch {
    std::vector<core::EventPtr> events;
    std::optional<Backlog> drop;
};

struct Scan {
    disk::recursive_directory_iterator cursor;
    bool recursive;
    std::uint32_t files_left;
    std::uint64_t bytes_left;
};

template <typename Callback, typename... Args> void deliver(Callback &callback, Args &&...args) {
    try {
        callback(std::forward<Args>(args)...);
    } catch (const std::exception &error) {
        g_warning("History callback threw: %s", error.what());
    } catch (...) {
        g_warning("History callback threw");
    }
}

} // namespace

bool internal::excluded_tree(const std::string &path, const std::string &data_dir) {
    static const auto trash =
        core::canonicalize(core::path_join(Glib::get_user_data_dir(), "Trash"));
    if ((!data_dir.empty() && core::is_ancestor(data_dir, path)) ||
        core::is_ancestor(trash, path)) {
        return true;
    }
    constexpr std::array<std::string_view, 12> excluded{
        ".ssh", ".gnupg", ".password-store", ".pki",        "keyrings", ".git",
        ".hg",  ".svn",   "node_modules",    "__pycache__", ".cache",   ".venv"};
    std::string_view rest(path);
    while (!rest.empty()) {
        const auto slash = rest.find('/');
        const auto component = rest.substr(0, slash);
        if (std::find(excluded.begin(), excluded.end(), component) != excluded.end()) {
            return true;
        }
        if (slash == rest.npos) {
            break;
        }
        rest.remove_prefix(slash + 1);
    }
    return false;
}

bool is_excluded(const std::string &path, const std::string &data_dir) {
    const auto normalized = core::canonicalize(path);
    if (internal::excluded_tree(normalized,
                                data_dir.empty() ? data_dir : core::canonicalize(data_dir))) {
        return true;
    }
    const auto name = std::string_view(normalized).substr(normalized.find_last_of('/') + 1);
    constexpr std::array<std::string_view, 4> names{".env", ".netrc", ".pgpass", "4913"};
    constexpr std::array<std::string_view, 7> prefixes{
        ".env.", "id_rsa", "id_ed25519", "id_ecdsa", ".filescope-", ".#", ".goutputstream-"};
    constexpr std::array<std::string_view, 8> suffixes{".pem",  ".key", ".p12", ".pfx",
                                                       ".kdbx", ".swp", ".swx", "~"};
    return std::find(names.begin(), names.end(), name) != names.end() ||
           std::any_of(prefixes.begin(), prefixes.end(),
                       [name](auto prefix) { return name.substr(0, prefix.size()) == prefix; }) ||
           std::any_of(suffixes.begin(), suffixes.end(), [name](auto suffix) {
               return name.size() >= suffix.size() &&
                      name.substr(name.size() - suffix.size()) == suffix;
           });
}

class History::Impl {
    using Work = std::function<void(Store &)>;
    struct Read {
        Work work;
    };
    using Command = std::variant<Batch, Work, Read>;
    using Delivery = std::function<void(Impl &)>;
    using Slot = std::variant<HistorySlot, DeletedSlot, ContentSlot, RestoreSlot>;
    struct CaptureJob {
        std::string path;
        bool retry;
    };
    using Deadlines = std::multimap<Clock::time_point, CaptureJob>;

  public:
    explicit Impl(Options options)
        : capacity_(options.limits.max_pending_events), options_(options),
          store_(std::move(options)), dispatcher_(std::make_unique<Glib::Dispatcher>()) {
        connection_ = dispatcher_->connect(sigc::mem_fun(*this, &Impl::dispatch));
    }

    core::Status start() {
        std::promise<core::Status> started;
        auto status = started.get_future();
        worker_ = std::thread([this, &started] { run(started); });
        return status.get();
    }

    void shutdown() {
        {
            const std::lock_guard<std::mutex> lock(command_mutex_);
            if (stopping_) {
                return;
            }
            stopping_ = true;
        }
        wake_.notify_one();
        if (worker_.joinable()) {
            worker_.join();
        }
        connection_.disconnect();
        if (!dispatching_) {
            dispatcher_.reset();
        }
        slots_.clear();
        deliveries_.clear();
    }

    void record(const std::vector<core::EventPtr> &events) {
        if (events.empty()) {
            return;
        }
        {
            const std::lock_guard<std::mutex> lock(command_mutex_);
            if (stopping_) {
                return;
            }
            if (events.size() > capacity_ - pending_) {
                const auto now = g_get_real_time();
                auto *last = commands_.empty() ? nullptr : std::get_if<Batch>(&commands_.back());
                if (last && last->drop) {
                    last->drop->last = now;
                    return;
                }
                commands_.emplace_back(Batch{{}, Backlog{now, now}});
            } else {
                commands_.emplace_back(Batch{events, std::nullopt});
                pending_ += events.size();
            }
        }
        wake_.notify_one();
    }

    template <typename T, typename F>
    void request(sigc::slot<void(core::Result<T>)> done, F action) {
        if (stopping_) {
            return;
        }
        const auto id = ++request_id_;
        slots_.emplace(id, std::move(done));
        Work work = [this, id, action = std::move(action)](Store &store) {
            auto result = run_request<T>(store, action);
            post([id, result = std::move(result)](Impl &main) mutable {
                auto found = main.slots_.extract(id);
                auto &done = std::get<sigc::slot<void(core::Result<T>)>>(found.mapped());
                deliver(done, std::move(result));
            });
        };
        if constexpr (std::is_same_v<T, fs::FileStamp>) {
            enqueue(std::move(work));
        } else {
            enqueue(Read{std::move(work)});
        }
    }

    void scan_root(const std::string &root, bool recursive) {
        enqueue([this, root, recursive](Store &store) {
            if (!core::validate_absolute(root) || !ensure_recovery()) {
                return;
            }
            const auto path = core::canonicalize(root);
            const auto started = store.start_scan();
            if (!started) {
                write_failed(started.error(), std::nullopt);
                return;
            }
            if (internal::excluded_tree(path, options_.data_dir)) {
                return;
            }
            std::error_code error;
            if (!disk::is_directory(disk::symlink_status(path, error))) {
                return;
            }
            disk::recursive_directory_iterator cursor(
                path, disk::directory_options::skip_permission_denied, error);
            if (cursor != disk::recursive_directory_iterator{}) {
                scans_.push_back({std::move(cursor), recursive, options_.limits.baseline_max_files,
                                  options_.limits.baseline_max_bytes});
            }
        });
    }

    void note_pause(bool paused) {
        enqueue([this, paused, at = g_get_real_time()](Store &store) {
            if (!ensure_recovery()) {
                return;
            }
            const auto result = store.note_pause(paused, at);
            if (!result) {
                write_failed(result.error(), std::nullopt);
            } else {
                write_succeeded();
            }
        });
    }

    void restore(VersionId id, fs::Confirmation confirmation, RestoreSlot done) {
        request(std::move(done), [this, id, confirmation](Store &store) {
            auto result = store.restore(
                id, confirmation, [this](Version version) { emit_version(std::move(version)); });
            if (result) {
                write_succeeded();
            } else if (result.error().code == ErrorCode::NO_SPACE ||
                       result.error().code == ErrorCode::IO) {
                write_failed(result.error(), std::nullopt);
            }
            return result;
        });
    }

    void capture(const std::string &path, Origin origin, std::function<void()> after_copy) {
        enqueue([this, path, origin, after_copy = std::move(after_copy)](Store &) {
            if (!ensure_recovery()) {
                return;
            }
            finish_capture(store_.capture(path, origin, after_copy));
        });
    }

    void set_fault(testing::FaultPoint point) {
        enqueue([point](Store &store) { store.set_fault(point); });
    }

    void flush() {
        const auto finished = std::make_shared<std::promise<void>>();
        auto done = finished->get_future();
        if (enqueue([this, finished](Store &) {
                while (!deadlines_.empty()) {
                    run_capture();
                }
                finished->set_value();
            })) {
            done.get();
        }
    }

    std::size_t pending() {
        const std::lock_guard<std::mutex> lock(command_mutex_);
        return pending_;
    }

    State state() const { return state_; }
    sigc::signal<void(const Version &)> version_signal_;
    sigc::signal<void(State, const std::optional<core::Error> &)> state_signal_;

  private:
    template <typename T, typename F> core::Result<T> run_request(Store &store, const F &action) {
        if (disabled_) {
            return *disabled_;
        }
        if constexpr (std::is_same_v<T, fs::FileStamp>) {
            const auto recovered = ensure_recovery();
            if (!recovered) {
                return recovered.error();
            }
        }
        auto result = action(store);
        if (!result && store.corrupt()) {
            mark_disabled(result.error());
        }
        return result;
    }

    bool enqueue(Command command) {
        {
            const std::lock_guard<std::mutex> lock(command_mutex_);
            if (stopping_) {
                return false;
            }
            commands_.push_back(std::move(command));
        }
        wake_.notify_one();
        return true;
    }

    void run(std::promise<core::Status> &started) {
        bool opened = false;
        try {
            auto result = store_.open();
            if (!result) {
                static_cast<void>(store_.close());
                started.set_value(result.error());
                return;
            }
            if (result.value()) {
                set_state(State::READY, core::make_error(ErrorCode::IO, "history reset"));
            }
            started.set_value(core::Status());
            opened = true;
            static_cast<void>(ensure_recovery());
            run_commands();
        } catch (const std::exception &error) {
            const auto failure = core::make_error(ErrorCode::IO, error.what());
            if (!opened) {
                static_cast<void>(store_.close());
                started.set_value(failure);
                return;
            }
            mark_disabled(failure);
        } catch (...) {
            const auto failure =
                core::make_error(ErrorCode::IO, "Unexpected history worker failure");
            if (!opened) {
                static_cast<void>(store_.close());
                started.set_value(failure);
                return;
            }
            mark_disabled(failure);
        }
        const auto result = store_.close();
        if (!result) {
            g_message("History could not finish closing the database: %s",
                      result.error().message.c_str());
        }
    }

    void run_commands() {
        std::deque<Command> rest;
        for (;;) {
            std::optional<Command> command;
            {
                std::unique_lock<std::mutex> lock(command_mutex_);
                const auto ready = [this] { return stopping_ || !commands_.empty(); };
                if (scans_.empty()) {
                    if (deadlines_.empty()) {
                        wake_.wait(lock, ready);
                    } else {
                        wake_.wait_until(lock, deadlines_.begin()->first, ready);
                    }
                }
                if (stopping_) {
                    rest.swap(commands_);
                    pending_ = 0;
                    lock.unlock();
                    scans_.clear();
                    deadlines_.clear();
                    captures_.clear();
                    finish_batches(rest);
                    return;
                }
                if (!commands_.empty()) {
                    command.emplace(std::move(commands_.front()));
                    commands_.pop_front();
                }
            }
            if (command) {
                if (auto *batch = std::get_if<Batch>(&*command)) {
                    run_batch(*batch);
                    const std::lock_guard<std::mutex> done(command_mutex_);
                    pending_ -= batch->events.size();
                } else if (auto *read = std::get_if<Read>(&*command)) {
                    read->work(store_);
                } else {
                    std::get<Work> (*command)(store_);
                }
            }
            if (disabled_) {
                scans_.clear();
                deadlines_.clear();
                captures_.clear();
            } else if (!deadlines_.empty() && deadlines_.begin()->first <= Clock::now()) {
                run_capture();
            } else if (!scans_.empty()) {
                scan_step();
            }
        }
    }

    void cancel_capture(const std::string &path, bool directory) {
        if (path.empty()) {
            return;
        }
        if (directory) {
            for (auto it = captures_.begin(); it != captures_.end();) {
                if (core::is_ancestor(path, it->first)) {
                    deadlines_.erase(it->second);
                    it = captures_.erase(it);
                } else {
                    ++it;
                }
            }
        } else if (auto found = captures_.find(path); found != captures_.end()) {
            deadlines_.erase(found->second);
            captures_.erase(found);
        }
    }

    void schedule_capture(const std::string &path, bool retry = false) {
        cancel_capture(path, false);
        const auto due = Clock::now() + std::chrono::milliseconds(options_.limits.capture_delay_ms);
        captures_.emplace(path, deadlines_.emplace(due, CaptureJob{path, retry}));
    }

    void schedule_event(const core::Event &event) {
        using core::EventKind;
        switch (event.kind) {
        case EventKind::DELETE:
        case EventKind::MOVED_OUT:
            cancel_capture(event.path, event.is_dir);
            return;
        case EventKind::RENAME:
        case EventKind::MOVE:
            cancel_capture(event.old_path, event.is_dir);
            break;
        case EventKind::CREATE:
            if (!event.synthetic) {
                return;
            }
            break;
        case EventKind::MOVED_IN:
        case EventKind::CLOSE_WRITE:
            break;
        default:
            return;
        }
        if (!event.is_dir && !is_excluded(event.path, options_.data_dir)) {
            schedule_capture(event.path);
        }
    }

    void run_capture() {
        const auto entry = deadlines_.extract(deadlines_.begin());
        const auto &job = entry.mapped();
        captures_.erase(job.path);
        auto result = store_.capture(job.path, Origin::AUTO, {}, true);
        if (result && result.value().changed && !job.retry) {
            schedule_capture(job.path, true);
        }
        finish_capture(std::move(result));
    }

    void scan_step() {
        auto &scan = scans_.front();
        if (scan.cursor == disk::recursive_directory_iterator{} || scan.files_left == 0 ||
            scan.bytes_left == 0) {
            scans_.pop_front();
            return;
        }
        const auto path = scan.cursor->path().string();
        struct stat info {};
        const bool present = lstat(path.c_str(), &info) == 0;
        if (!scan.recursive || !present || !S_ISDIR(info.st_mode) ||
            internal::excluded_tree(path, options_.data_dir)) {
            scan.cursor.disable_recursion_pending();
        }
        std::error_code error;
        scan.cursor.increment(error);
        const auto per_file =
            std::min({options_.limits.baseline_max_file_bytes, options_.limits.max_file_bytes,
                      internal::max_content_bytes});
        if (!present || !S_ISREG(info.st_mode) ||
            static_cast<std::uint64_t>(info.st_size) > per_file) {
            return;
        }
        if (is_excluded(path, options_.data_dir)) {
            return;
        }
        if (static_cast<std::uint64_t>(info.st_size) > scan.bytes_left) {
            scans_.pop_front();
            return;
        }
        auto result = store_.capture(path, Origin::BASELINE, {}, false, scan.bytes_left);
        if (result) {
            const auto &stamp = result.value().stamp;
            if (stamp) {
                --scan.files_left;
                scan.bytes_left -= stamp->size;
            }
        }
        finish_capture(std::move(result));
    }

    void finish_capture(core::Result<internal::Capture> result) {
        if (!result) {
            write_failed(result.error(), std::nullopt);
            return;
        }
        auto &version = result.value().version;
        if (version) {
            emit_version(std::move(*version));
        }
        write_succeeded();
    }

    void emit_version(Version version) {
        post(
            [version = std::move(version)](Impl &main) { deliver(main.version_signal_, version); });
    }

    core::Status ensure_recovery() {
        if (disabled_) {
            return *disabled_;
        }
        if (recovered_) {
            return {};
        }
        auto result = store_.recover([this] { service_recovery_query(); });
        recovered_ =
            result.ok() || (store_.over_limit() && result.error().code == ErrorCode::NO_SPACE);
        if (!result) {
            write_failed(result.error(), std::nullopt);
        }
        return recovered_ ? core::Status{} : result;
    }

    void service_recovery_query() {
        Work work;
        {
            const std::lock_guard<std::mutex> lock(command_mutex_);
            auto *read =
                stopping_ || commands_.empty() ? nullptr : std::get_if<Read>(&commands_.front());
            if (!read) {
                return;
            }
            work = std::move(read->work);
            commands_.pop_front();
        }
        work(store_);
    }

    void run_batch(const Batch &batch) {
        if (!ensure_recovery()) {
            if (!disabled_) {
                defer_drop(batch.drop ? *batch.drop : lost_now());
            }
            return;
        }
        auto drops = deferred_drops_;
        if (batch.drop) {
            drops.push_back(*batch.drop);
        }
        const auto result = store_.record(batch.events, drops);
        if (!result) {
            write_failed(result.error(), batch.drop ? *batch.drop : lost_now());
            return;
        }
        deferred_drops_.clear();
        for (const auto &event : batch.events) {
            if (event) {
                schedule_event(*event);
            }
        }
        write_succeeded();
    }

    void finish_batches(std::deque<Command> &commands) {
        if (!ensure_recovery()) {
            return;
        }
        std::vector<core::EventPtr> events;
        auto drops = std::move(deferred_drops_);
        for (auto &command : commands) {
            if (auto *batch = std::get_if<Batch>(&command)) {
                if (batch->drop) {
                    drops.push_back(*batch->drop);
                } else {
                    events.insert(events.end(), std::make_move_iterator(batch->events.begin()),
                                  std::make_move_iterator(batch->events.end()));
                }
            }
        }
        if (!events.empty() || !drops.empty()) {
            const auto result = store_.record(events, drops);
            if (!result) {
                g_message("History could not commit pending events: %s",
                          result.error().message.c_str());
            }
        }
    }

    static Backlog lost_now() {
        const auto now = g_get_real_time();
        return {now, now};
    }

    void defer_drop(const Backlog &drop) {
        if (deferred_drops_.empty()) {
            deferred_drops_.push_back(drop);
        } else {
            deferred_drops_.front().first = std::min(deferred_drops_.front().first, drop.first);
            deferred_drops_.front().last = std::max(deferred_drops_.front().last, drop.last);
        }
    }

    void mark_disabled(const core::Error &error) {
        if (!disabled_) {
            disabled_ = error;
            set_state(State::DISABLED, error);
        }
    }

    void write_failed(const core::Error &error, std::optional<Backlog> drop) {
        if (store_.corrupt()) {
            mark_disabled(error);
            return;
        }
        if (drop) {
            defer_drop(*drop);
        }
        set_state(State::DEGRADED, error);
    }

    void write_succeeded() {
        if (store_.over_limit()) {
            if (worker_state_ == State::READY) {
                set_state(State::DEGRADED,
                          core::make_error(ErrorCode::NO_SPACE,
                                           "Latest history versions exceed the store limit"));
            }
        } else if (worker_state_ == State::DEGRADED) {
            set_state(State::READY, std::nullopt);
        }
    }

    void set_state(State state, std::optional<core::Error> error) {
        worker_state_ = state;
        post([state, error = std::move(error)](Impl &main) {
            main.state_ = state;
            deliver(main.state_signal_, state, error);
        });
    }

    void post(Delivery delivery) {
        bool notify = false;
        {
            const std::lock_guard<std::mutex> lock(delivery_mutex_);
            notify = deliveries_.empty();
            deliveries_.push_back(std::move(delivery));
        }
        if (notify) {
            dispatcher_->emit();
        }
    }

    void dispatch() {
        std::deque<Delivery> deliveries;
        {
            const std::lock_guard<std::mutex> lock(delivery_mutex_);
            deliveries.swap(deliveries_);
        }
        dispatching_ = true;
        for (auto &delivery : deliveries) {
            if (stopping_) {
                break;
            }
            delivery(*this);
        }
        dispatching_ = false;
    }

    const std::size_t capacity_;
    const Options options_;
    Store store_;
    std::mutex command_mutex_;
    std::condition_variable wake_;
    std::deque<Command> commands_;
    std::size_t pending_ = 0;
    std::deque<Scan> scans_;
    Deadlines deadlines_;
    std::unordered_map<std::string, Deadlines::iterator> captures_;

    std::mutex delivery_mutex_;
    std::deque<Delivery> deliveries_;
    std::unique_ptr<Glib::Dispatcher> dispatcher_;
    sigc::connection connection_;
    std::uint64_t request_id_ = 0;
    std::unordered_map<std::uint64_t, Slot> slots_;

    std::optional<core::Error> disabled_;
    std::vector<Backlog> deferred_drops_;
    std::thread worker_;

    State state_ = State::READY;
    State worker_state_ = State::READY;
    bool stopping_ = false;
    bool dispatching_ = false;
    bool recovered_ = false;
};

History::History(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
History::~History() { shutdown(); }

core::Result<std::unique_ptr<History>> History::open(Options options) {
    try {
        if (options.data_dir.empty()) {
            options.data_dir = Glib::get_user_data_dir() + "/filescope";
        }
        const auto valid = core::validate_absolute(options.data_dir);
        if (!valid) {
            return valid.error();
        }
        options.data_dir = core::canonicalize(options.data_dir);
        if (options.data_dir == "/") {
            return core::make_error(ErrorCode::INVALID_PATH, "Invalid history directory");
        }
        auto history = std::make_unique<History>(std::make_unique<Impl>(std::move(options)));
        const auto status = history->impl_->start();
        if (!status) {
            return status.error();
        }
        return history;
    } catch (const Glib::Error &error) {
        return core::error_from_glib(error);
    } catch (const std::exception &error) {
        return core::make_error(ErrorCode::IO, error.what());
    }
}

void History::shutdown() { impl_->shutdown(); }
State History::state() const { return impl_->state(); }
void History::record_events(const std::vector<core::EventPtr> &events) { impl_->record(events); }
void History::scan_root(const std::string &root, bool recursive) {
    impl_->scan_root(root, recursive);
}
void History::note_pause(bool paused) { impl_->note_pause(paused); }

void History::load_history_async(const std::string &path, HistorySlot done) {
    impl_->request(std::move(done), [path](Store &store) { return store.load(path); });
}
void History::list_deleted_async(const std::string &dir, DeletedSlot done) {
    impl_->request(std::move(done), [dir](Store &store) { return store.deleted(dir); });
}
void History::read_content_async(VersionId id, ContentSlot done) {
    impl_->request(std::move(done), [id](Store &store) { return store.content(id); });
}
void History::restore_async(VersionId id, fs::Confirmation confirmation, RestoreSlot done) {
    impl_->restore(id, confirmation, std::move(done));
}
void History::recover_async(VersionId id, RestoreSlot done) {
    impl_->request(std::move(done), [id](Store &store) { return store.recover_file(id); });
}

sigc::signal<void(const Version &)> &History::signal_version_added() {
    return impl_->version_signal_;
}
sigc::signal<void(State, const std::optional<core::Error> &)> &History::signal_state() {
    return impl_->state_signal_;
}

struct TestAccess {
    static History::Impl &get(History &history) { return *history.impl_; }
};

namespace testing {

void set_fault(History &history, FaultPoint point) { TestAccess::get(history).set_fault(point); }
void flush(History &history) { TestAccess::get(history).flush(); }
void capture(History &history, const std::string &path, Origin origin,
             std::function<void()> after_copy) {
    TestAccess::get(history).capture(path, origin, std::move(after_copy));
}
std::size_t pending_events(History &history) { return TestAccess::get(history).pending(); }

} // namespace testing
} // namespace filescope::his
