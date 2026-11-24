#pragma once

#include "core/error.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <giomm/cancellable.h>
#include <glibmm/ustring.h>
#include <sigc++/sigc++.h>

namespace filescope::job {

using JobId = std::uint64_t;

enum class JobState : std::uint8_t { QUEUED, RUNNING, PAUSED, CANCELLED, FAILED, DONE };

struct Progress {
    std::uint64_t bytes_done = 0;
    std::int64_t bytes_total = -1;
    std::uint32_t items_done = 0;
    std::int32_t items_total = -1;
    std::string current_path;
};

struct ItemResult {
    std::string path;
    std::optional<core::Error> error;
    bool skipped = false;
};

namespace detail {
class JobQueueImpl;
struct JobData;
} // namespace detail

class Job {
  public:
    // [worker] Returns this job's shared cancellation token.
    const Glib::RefPtr<Gio::Cancellable> &cancellable() const noexcept;
    // [worker] Reports whether cancellation has been requested.
    bool is_cancelled() const noexcept;
    // [worker] Replaces the totals used by the next progress notification.
    void report_total(std::int64_t bytes_total, std::int32_t items_total);
    // [worker] Replaces the latest progress; current_path is copied.
    void report_progress(std::uint64_t bytes_done, std::uint32_t items_done,
                         const std::string &current_path);
    // [worker] Appends one owned item result.
    void add_item_result(ItemResult result);

  private:
    explicit Job(detail::JobData &data) noexcept;

    detail::JobData &data_;

    friend class detail::JobQueueImpl;
};

using JobBody = std::function<core::Result<void>(Job &job)>;

class JobQueue {
  public:
    // [main] Starts one FIFO worker.
    JobQueue();
    // [main] Calls shutdown().
    ~JobQueue();
    JobQueue(const JobQueue &) = delete;
    JobQueue &operator=(const JobQueue &) = delete;

    // [main] Rejects submissions, cancels work, joins the worker, and drops pending signals.
    void shutdown();
    // [main] Takes body on success; returns CANCELLED and destroys it on failure.
    core::Result<JobId> submit(Glib::ustring title, JobBody body);
    // [main] Cancels a known nonterminal job.
    bool cancel(JobId id);
    // [main] Returns FAILED for an unknown id.
    JobState state(JobId id) const;
    // [main] Returns a terminal job's item results, otherwise an empty vector.
    std::vector<ItemResult> results(JobId id) const;
    // [main] Releases a terminal job record.
    void forget(JobId id);

    // [main] Emitted for state transitions; error is present only for FAILED/CANCELLED.
    sigc::signal<void(JobId, JobState, const std::optional<core::Error> &)> &signal_state();
    // [main] Emitted at most 10 Hz plus once immediately before terminal state.
    sigc::signal<void(JobId, const Progress &)> &signal_progress();

  private:
    std::unique_ptr<detail::JobQueueImpl> impl_;
};

} // namespace filescope::job
