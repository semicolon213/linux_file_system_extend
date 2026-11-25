#include "his/his_internal.hpp"

#include <condition_variable>
#include <deque>
#include <future>
#include <thread>
#include <unordered_map>
#include <variant>

#include <glib.h>
#include <glibmm/dispatcher.h>
#include <glibmm/miscutils.h>

#include "core/path.hpp"

namespace filescope::his {

namespace {

using core::ErrorCode;
using internal::Backlog;
using internal::Store;

struct Batch {
    std::vector<core::EventPtr> events;
    std::optional<Backlog> drop;
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

class History::Impl {
    using Work = std::function<void(Store &)>;
    struct Read {
        Work work;
    };
    using Command = std::variant<Batch, Work, Read>;
    using Delivery = std::function<void(Impl &)>;
    using Slot = std::variant<HistorySlot, DeletedSlot, ContentSlot>;

  public:
    explicit Impl(Options options)
        : capacity_(options.limits.max_pending_events), store_(std::move(options)),
          dispatcher_(std::make_unique<Glib::Dispatcher>()) {
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
        enqueue(Read{[this, id, action = std::move(action)](Store &store) {
            auto result = disabled_ ? core::Result<T>(*disabled_) : action(store);
            if (!result && store.corrupt()) {
                mark_disabled(result.error());
            }
            post([id, result = std::move(result)](Impl &main) mutable {
                auto found = main.slots_.extract(id);
                auto &done = std::get<sigc::slot<void(core::Result<T>)>>(found.mapped());
                deliver(done, std::move(result));
            });
        }});
    }

    void capture(const std::string &path, Origin origin, std::function<void()> after_copy) {
        enqueue([this, path, origin, after_copy = std::move(after_copy)](Store &store) {
            if (!ensure_recovery()) {
                return;
            }
            auto result = store.capture(path, origin, after_copy);
            if (!result) {
                write_failed(result.error(), std::nullopt);
                return;
            }
            if (result.value()) {
                post([version = std::move(*result.value())](Impl &main) {
                    deliver(main.version_signal_, version);
                });
            }
            write_succeeded();
        });
    }

    void set_fault(testing::FaultPoint point) {
        enqueue([point](Store &store) { store.set_fault(point); });
    }

    void flush() {
        const auto finished = std::make_shared<std::promise<void>>();
        auto done = finished->get_future();
        if (enqueue([finished](Store &) { finished->set_value(); })) {
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
            ensure_recovery();
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
                wake_.wait(lock, [this] { return stopping_ || !commands_.empty(); });
                if (stopping_) {
                    rest.swap(commands_);
                    pending_ = 0;
                } else {
                    command.emplace(std::move(commands_.front()));
                    commands_.pop_front();
                }
            }
            if (!command) {
                finish_batches(rest);
                return;
            }

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
    }

    bool ensure_recovery() {
        if (disabled_) {
            return false;
        }
        if (!recovered_) {
            auto result = store_.recover([this] { service_recovery_query(); });
            recovered_ =
                result.ok() || (store_.over_limit() && result.error().code == ErrorCode::NO_SPACE);
            if (!result) {
                write_failed(result.error(), std::nullopt);
            }
        }
        return recovered_;
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
    Store store_;
    std::mutex command_mutex_;
    std::condition_variable wake_;
    std::deque<Command> commands_;
    std::size_t pending_ = 0;

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

void History::load_history_async(const std::string &path, HistorySlot done) {
    impl_->request(std::move(done), [path](Store &store) { return store.load(path); });
}
void History::list_deleted_async(const std::string &dir, DeletedSlot done) {
    impl_->request(std::move(done), [dir](Store &store) { return store.deleted(dir); });
}
void History::read_content_async(VersionId id, ContentSlot done) {
    impl_->request(std::move(done), [id](Store &store) { return store.content(id); });
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
