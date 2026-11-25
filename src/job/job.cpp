#include "job/job.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <exception>
#include <mutex>
#include <new>
#include <thread>
#include <unordered_map>
#include <utility>

#include <glib.h>
#include <glibmm/dispatcher.h>

namespace filescope::job {
namespace detail {

using Clock = std::chrono::steady_clock;
constexpr auto kProgressInterval = std::chrono::milliseconds(100);

struct JobData {
    JobData(JobQueueImpl &queue_value, JobId id_value, JobBody body_value)
        : queue(queue_value), id(id_value), body(std::move(body_value)) {}

    JobQueueImpl &queue;
    JobId id;
    JobBody body;
    Glib::RefPtr<Gio::Cancellable> cancellable = Gio::Cancellable::create();
    JobState state = JobState::QUEUED;
    std::optional<core::Error> terminal_error;
    bool cancel_requested = false;
    mutable std::mutex progress_mutex;
    Progress progress;
    std::vector<ItemResult> results;
    bool progress_pending = false;
    Clock::time_point next_progress = Clock::now() + kProgressInterval;
};

enum class MessageKind : std::uint8_t { STATE, PROGRESS };

struct Message {
    MessageKind kind = MessageKind::STATE;
    JobId id = 0;
    JobState state = JobState::FAILED;
    std::optional<core::Error> error;
    Progress progress;
    bool terminal = false;
};

class JobQueueImpl {
  public:
    JobQueueImpl() {
        dispatcher_connection_ =
            dispatcher_.connect(sigc::mem_fun(*this, &JobQueueImpl::dispatch_messages));
        worker_ = std::thread(&JobQueueImpl::worker_loop, this);
    }

    ~JobQueueImpl() { shutdown(); }

    void shutdown() {
        std::vector<Glib::RefPtr<Gio::Cancellable>> cancellables;
        {
            const std::lock_guard<std::mutex> lock(queue_mutex_);
            if (shutting_down_) {
                return;
            }
            shutting_down_ = true;
            shutting_down_flag_.store(true, std::memory_order_release);
            for (const auto &entry : jobs_) {
                entry.second->cancel_requested = true;
                cancellables.push_back(entry.second->cancellable);
            }
        }
        for (const auto &cancellable : cancellables) {
            cancellable->cancel();
        }
        queue_condition_.notify_one();
        if (worker_.joinable()) {
            worker_.join();
        }

        std::vector<JobBody> bodies;
        {
            const std::lock_guard<std::mutex> lock(queue_mutex_);
            bodies.reserve(jobs_.size());
            for (auto &entry : jobs_) {
                bodies.push_back(std::move(entry.second->body));
            }
        }
        bodies.clear();
        {
            const std::lock_guard<std::mutex> lock(queue_mutex_);
            jobs_.clear();
            waiting_.clear();
        }
        {
            const std::lock_guard<std::mutex> lock(delivery_mutex_);
            messages_.clear();
        }
    }

    core::Result<JobId> submit(JobBody body) {
        JobId id = 0;
        {
            const std::lock_guard<std::mutex> lock(queue_mutex_);
            if (shutting_down_) {
                return core::make_error(core::ErrorCode::CANCELLED, "Job queue is shutting down");
            }
            id = next_id_++;
            if (next_id_ == 0) {
                next_id_ = 1;
            }
            auto data = std::make_unique<JobData>(*this, id, std::move(body));
            jobs_.emplace(id, std::move(data));
            waiting_.push_back(id);
        }

        emit_state(id, JobState::QUEUED, std::nullopt);
        queue_condition_.notify_one();
        return id;
    }

    bool cancel(JobId id) {
        JobData *data = nullptr;
        bool queued = false;
        {
            const std::lock_guard<std::mutex> lock(queue_mutex_);
            const auto found = jobs_.find(id);
            if (found == jobs_.end() || is_terminal(found->second->state) || shutting_down_) {
                return false;
            }
            data = found->second.get();
            data->cancel_requested = true;
            queued = data->state == JobState::QUEUED;
            if (queued) {
                waiting_.erase(std::remove(waiting_.begin(), waiting_.end(), id), waiting_.end());
                data->state = JobState::CANCELLED;
                data->terminal_error =
                    core::make_error(core::ErrorCode::CANCELLED, "Job cancelled");
            }
        }
        data->cancellable->cancel();
        if (!queued) {
            return true;
        }

        const auto terminal_error = data->terminal_error;
        data->body = {};
        emit_state(id, JobState::CANCELLED, terminal_error);
        return true;
    }

    JobState state(JobId id) const {
        const std::lock_guard<std::mutex> lock(queue_mutex_);
        const auto found = jobs_.find(id);
        return found == jobs_.end() ? JobState::FAILED : found->second->state;
    }

    std::vector<ItemResult> results(JobId id) const {
        const std::lock_guard<std::mutex> queue_lock(queue_mutex_);
        const auto found = jobs_.find(id);
        if (found == jobs_.end() || !is_terminal(found->second->state)) {
            return {};
        }
        const std::lock_guard<std::mutex> progress_lock(found->second->progress_mutex);
        return found->second->results;
    }

    void forget(JobId id) {
        std::unique_ptr<JobData> forgotten;
        {
            const std::lock_guard<std::mutex> lock(queue_mutex_);
            const auto found = jobs_.find(id);
            if (found == jobs_.end() || !is_terminal(found->second->state)) {
                return;
            }
            forgotten = std::move(found->second);
            jobs_.erase(found);
        }
    }

    sigc::signal<void(JobId, JobState, const std::optional<core::Error> &)> &signal_state() {
        return state_signal_;
    }

    sigc::signal<void(JobId, const Progress &)> &signal_progress() { return progress_signal_; }

    void schedule_progress(JobData &data) {
        std::optional<Progress> progress;
        {
            const std::lock_guard<std::mutex> lock(data.progress_mutex);
            const auto now = Clock::now();
            if (data.progress_pending || now < data.next_progress) {
                return;
            }
            data.progress_pending = true;
            data.next_progress = now + kProgressInterval;
            progress = data.progress;
        }
        post_progress(data.id, std::move(progress).value());
    }

  private:
    static bool is_terminal(JobState state) noexcept {
        return state == JobState::CANCELLED || state == JobState::FAILED || state == JobState::DONE;
    }

    void worker_loop() {
        while (true) {
            JobData *data = nullptr;
            {
                std::unique_lock<std::mutex> lock(queue_mutex_);
                queue_condition_.wait(lock, [this] { return shutting_down_ || !waiting_.empty(); });
                if (shutting_down_) {
                    return;
                }
                const JobId id = waiting_.front();
                waiting_.pop_front();
                const auto found = jobs_.find(id);
                if (found == jobs_.end() || found->second->state != JobState::QUEUED) {
                    continue;
                }
                data = found->second.get();
                data->state = JobState::RUNNING;
            }
            post_state(data->id, JobState::RUNNING, std::nullopt);
            run_body(*data);
        }
    }

    void run_body(JobData &data) {
        core::Result<void> result;
        try {
            Job job(data);
            result = data.body(job);
        } catch (const std::bad_alloc &) {
            throw;
        } catch (const std::exception &error) {
            result = core::make_error(core::ErrorCode::FAILED,
                                      std::string("Job body threw: ") + error.what());
        } catch (...) {
            result = core::make_error(core::ErrorCode::FAILED, "Job body threw");
        }

        Progress final_progress;
        bool has_failed_item = false;
        {
            const std::lock_guard<std::mutex> lock(data.progress_mutex);
            final_progress = data.progress;
            data.progress_pending = true;
            has_failed_item =
                std::any_of(data.results.begin(), data.results.end(), [](const ItemResult &item) {
                    return item.error.has_value() && !item.skipped;
                });
        }

        const JobId id = data.id;
        JobState terminal_state = JobState::DONE;
        std::optional<core::Error> terminal_error;
        {
            const std::lock_guard<std::mutex> lock(queue_mutex_);
            if (data.cancel_requested) {
                terminal_state = JobState::CANCELLED;
                terminal_error = core::make_error(core::ErrorCode::CANCELLED, "Job cancelled");
            } else if (!result.ok()) {
                terminal_state = JobState::FAILED;
                terminal_error = result.error();
            } else if (has_failed_item) {
                terminal_state = JobState::FAILED;
                terminal_error = core::make_error(core::ErrorCode::PARTIAL, "Job partially failed");
            }
            data.state = terminal_state;
            data.terminal_error = terminal_error;
        }

        post_progress(id, std::move(final_progress));
        post_state(id, terminal_state, terminal_error, true);
    }

    void post_state(JobId id, JobState state, std::optional<core::Error> error,
                    bool terminal = false) {
        Message message;
        message.id = id;
        message.state = state;
        message.error = std::move(error);
        message.terminal = terminal;
        post_message(std::move(message));
    }

    void post_progress(JobId id, Progress progress) {
        Message message;
        message.kind = MessageKind::PROGRESS;
        message.id = id;
        message.progress = std::move(progress);
        post_message(std::move(message));
    }

    void post_message(Message message) {
        if (shutting_down_flag_.load(std::memory_order_acquire)) {
            return;
        }
        {
            const std::lock_guard<std::mutex> lock(delivery_mutex_);
            if (shutting_down_flag_.load(std::memory_order_relaxed)) {
                return;
            }
            messages_.push_back(std::move(message));
        }
        dispatcher_.emit();
    }

    void dispatch_messages() {
        std::deque<Message> messages;
        {
            const std::lock_guard<std::mutex> lock(delivery_mutex_);
            messages.swap(messages_);
        }
        if (shutting_down_flag_.load(std::memory_order_acquire)) {
            return;
        }

        for (auto &message : messages) {
            JobData *data = nullptr;
            {
                const std::lock_guard<std::mutex> lock(queue_mutex_);
                const auto found = jobs_.find(message.id);
                if (found != jobs_.end()) {
                    data = found->second.get();
                }
            }
            if (data == nullptr) {
                continue;
            }
            if (message.kind == MessageKind::PROGRESS) {
                {
                    const std::lock_guard<std::mutex> lock(data->progress_mutex);
                    data->progress_pending = false;
                }
                emit_progress(message.id, message.progress);
            } else {
                if (message.terminal) {
                    data->body = {};
                }
                emit_state(message.id, message.state, message.error);
            }
        }
    }

    void emit_state(JobId id, JobState state, const std::optional<core::Error> &error) noexcept {
        try {
            state_signal_.emit(id, state, error);
        } catch (const std::exception &exception) {
            g_warning("Job state handler threw: %s", exception.what());
        } catch (...) {
            g_warning("Job state handler threw");
        }
    }

    void emit_progress(JobId id, const Progress &progress) noexcept {
        try {
            progress_signal_.emit(id, progress);
        } catch (const std::exception &exception) {
            g_warning("Job progress handler threw: %s", exception.what());
        } catch (...) {
            g_warning("Job progress handler threw");
        }
    }

    mutable std::mutex queue_mutex_;
    std::condition_variable queue_condition_;
    std::unordered_map<JobId, std::unique_ptr<JobData>> jobs_;
    std::deque<JobId> waiting_;
    JobId next_id_ = 1;
    bool shutting_down_ = false;
    std::atomic<bool> shutting_down_flag_{false};
    std::thread worker_;

    std::mutex delivery_mutex_;
    std::deque<Message> messages_;
    Glib::Dispatcher dispatcher_;
    sigc::connection dispatcher_connection_;
    sigc::signal<void(JobId, JobState, const std::optional<core::Error> &)> state_signal_;
    sigc::signal<void(JobId, const Progress &)> progress_signal_;
};

} // namespace detail

Job::Job(detail::JobData &data) noexcept : data_(data) {}

const Glib::RefPtr<Gio::Cancellable> &Job::cancellable() const noexcept {
    return data_.cancellable;
}

bool Job::is_cancelled() const noexcept { return data_.cancellable->is_cancelled(); }

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): public contract fixes the order.
void Job::report_total(std::int64_t bytes_total, std::int32_t items_total) {
    {
        const std::lock_guard<std::mutex> lock(data_.progress_mutex);
        data_.progress.bytes_total = bytes_total;
        data_.progress.items_total = items_total;
    }
    data_.queue.schedule_progress(data_);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): public contract fixes the order.
void Job::report_progress(std::uint64_t bytes_done, std::uint32_t items_done,
                          const std::string &current_path) {
    {
        const std::lock_guard<std::mutex> lock(data_.progress_mutex);
        data_.progress.bytes_done = bytes_done;
        data_.progress.items_done = items_done;
        data_.progress.current_path = current_path;
    }
    data_.queue.schedule_progress(data_);
}

void Job::add_item_result(ItemResult result) {
    const std::lock_guard<std::mutex> lock(data_.progress_mutex);
    data_.results.push_back(std::move(result));
}

JobQueue::JobQueue() : impl_(std::make_unique<detail::JobQueueImpl>()) {}

JobQueue::~JobQueue() { shutdown(); }

void JobQueue::shutdown() { impl_->shutdown(); }

// NOLINTNEXTLINE(performance-unnecessary-value-param): public contract takes title by value.
core::Result<JobId> JobQueue::submit(Glib::ustring title, JobBody body) {
    (void)title;
    return impl_->submit(std::move(body));
}

bool JobQueue::cancel(JobId id) { return impl_->cancel(id); }

JobState JobQueue::state(JobId id) const { return impl_->state(id); }

std::vector<ItemResult> JobQueue::results(JobId id) const { return impl_->results(id); }

void JobQueue::forget(JobId id) { impl_->forget(id); }

sigc::signal<void(JobId, JobState, const std::optional<core::Error> &)> &JobQueue::signal_state() {
    return impl_->signal_state();
}

sigc::signal<void(JobId, const Progress &)> &JobQueue::signal_progress() {
    return impl_->signal_progress();
}

} // namespace filescope::job
