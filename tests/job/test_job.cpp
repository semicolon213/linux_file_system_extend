#include "job/job.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <giomm/init.h>
#include <glib.h>
#include <glibmm/main.h>

namespace {

using filescope::core::ErrorCode;
using filescope::core::Result;
using filescope::job::ItemResult;
using filescope::job::Job;
using filescope::job::JobId;
using filescope::job::JobQueue;
using filescope::job::JobState;
using filescope::job::Progress;
using filescope::test::wait_until;

struct StateEvent {
    JobId id;
    JobState state;
    std::optional<ErrorCode> error;
};

class StateLog {
  public:
    explicit StateLog(JobQueue &queue)
        : connection_(queue.signal_state().connect(
              [this](JobId id, JobState state, const std::optional<filescope::core::Error> &error) {
                  events.push_back(
                      {id, state,
                       error.has_value() ? std::optional<ErrorCode>(error->code) : std::nullopt});
              })) {}

    std::vector<StateEvent> events;

  private:
    sigc::connection connection_;
};

void assert_state(const StateEvent &event, JobId id, JobState state) {
    g_assert_cmpuint(event.id, ==, id);
    g_assert_true(event.state == state);
}

void test_fifo_and_nonblocking() {
    JobQueue queue;
    StateLog log(queue);
    std::atomic<bool> release_first{false};
    std::atomic<unsigned> idle_count{0};

    const auto first = queue.submit("A", [&release_first](Job &) -> Result<void> {
        while (!release_first.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return {};
    });
    const auto second = queue.submit("B", [](Job &) -> Result<void> { return {}; });
    g_assert_true(first.ok());
    g_assert_true(second.ok());
    g_assert_cmpuint(log.events.size(), ==, 2);
    assert_state(log.events[0], first.value(), JobState::QUEUED);
    assert_state(log.events[1], second.value(), JobState::QUEUED);

    auto idle = Glib::signal_idle().connect([&idle_count] {
        ++idle_count;
        return true;
    });
    g_assert_true(wait_until(
        [&queue, &first] { return queue.state(first.value()) == JobState::RUNNING; }, 2000));
    g_assert_true(wait_until([&idle_count] { return idle_count.load() > 0; }, 2000));
    g_assert_cmpuint(idle_count.load(), >, 0);
    g_assert_true(queue.state(second.value()) == JobState::QUEUED);
    release_first = true;
    g_assert_true(wait_until([&log] { return log.events.size() == 6; }, 2000));
    idle.disconnect();

    const std::vector<std::pair<JobId, JobState>> expected = {
        {first.value(), JobState::QUEUED},   {second.value(), JobState::QUEUED},
        {first.value(), JobState::RUNNING},  {first.value(), JobState::DONE},
        {second.value(), JobState::RUNNING}, {second.value(), JobState::DONE},
    };
    for (std::size_t index = 0; index < expected.size(); ++index) {
        assert_state(log.events[index], expected[index].first, expected[index].second);
    }
}

void test_progress_throttled() {
    JobQueue queue;
    std::vector<Progress> progress;
    queue.signal_progress().connect(
        [&progress](JobId, const Progress &value) { progress.push_back(value); });

    const auto submitted = queue.submit("progress", [](Job &job) -> Result<void> {
        job.report_total(-1, -1);
        for (unsigned batch = 0; batch < 100; ++batch) {
            for (unsigned item = 0; item < 100; ++item) {
                const auto value = batch * 100 + item + 1;
                job.report_progress(value, value, "/tmp/item");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return {};
    });
    g_assert_true(submitted.ok());
    g_assert_true(wait_until(
        [&queue, &submitted] { return queue.state(submitted.value()) == JobState::DONE; }, 2000));
    g_assert_true(wait_until([&progress] { return !progress.empty(); }, 2000));
    while (Glib::MainContext::get_default()->pending()) {
        Glib::MainContext::get_default()->iteration(false);
    }

    g_assert_cmpuint(progress.size(), <=, 3);
    g_assert_cmpuint(progress.back().bytes_done, ==, 10000);
    g_assert_cmpint(progress.back().bytes_total, ==, -1);
    g_assert_cmpstr(progress.back().current_path.c_str(), ==, "/tmp/item");
}

struct DestructionRecord {
    DestructionRecord(std::atomic<unsigned> &count_value, std::thread::id &thread_value)
        : count(count_value), thread(thread_value) {}

    std::atomic<unsigned> &count;
    std::thread::id &thread;

    ~DestructionRecord() {
        thread = std::this_thread::get_id();
        ++count;
    }
};

void test_cancel_queued_and_running() {
    JobQueue queue;
    StateLog log(queue);
    std::atomic<bool> started{false};
    std::atomic<unsigned> destroyed{0};
    std::thread::id destruction_thread;
    const auto main_thread = std::this_thread::get_id();

    const auto running = queue.submit("running", [&started](Job &job) -> Result<void> {
        job.add_item_result(ItemResult{"/tmp/completed", std::nullopt, false});
        started = true;
        while (!job.is_cancelled()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return filescope::core::make_error(ErrorCode::CANCELLED, "cancelled");
    });
    auto token = std::make_shared<DestructionRecord>(destroyed, destruction_thread);
    const auto queued = queue.submit("queued", [token](Job &) -> Result<void> {
        g_assert_not_reached();
        return {};
    });
    token.reset();
    g_assert_true(running.ok());
    g_assert_true(queued.ok());
    g_assert_true(wait_until([&started] { return started.load(); }, 2000));

    g_assert_true(queue.cancel(queued.value()));
    g_assert_true(queue.state(queued.value()) == JobState::CANCELLED);
    g_assert_cmpuint(destroyed.load(), ==, 1);
    g_assert_true(destruction_thread == main_thread);
    g_assert_true(log.events.back().error == ErrorCode::CANCELLED);

    g_assert_true(queue.cancel(running.value()));
    g_assert_true(wait_until(
        [&log, &running] {
            return std::any_of(
                log.events.begin(), log.events.end(), [&running](const StateEvent &event) {
                    return event.id == running.value() && event.state == JobState::CANCELLED;
                });
        },
        2000));
    const auto results = queue.results(running.value());
    g_assert_cmpuint(results.size(), ==, 1);
    g_assert_cmpstr(results.front().path.c_str(), ==, "/tmp/completed");
}

void test_partial_results() {
    JobQueue queue;
    StateLog log(queue);
    const auto submitted = queue.submit("partial", [](Job &job) -> Result<void> {
        job.add_item_result(ItemResult{"/tmp/ok", std::nullopt, false});
        job.add_item_result(
            ItemResult{"/tmp/bad", filescope::core::make_error(ErrorCode::IO, "failed"), false});
        job.add_item_result(ItemResult{"/tmp/ok2", std::nullopt, false});
        return {};
    });
    g_assert_true(submitted.ok());
    g_assert_true(wait_until([&log] { return log.events.size() == 3; }, 2000));
    g_assert_true(queue.state(submitted.value()) == JobState::FAILED);
    g_assert_true(log.events.back().error == ErrorCode::PARTIAL);
    const auto results = queue.results(submitted.value());
    g_assert_cmpuint(results.size(), ==, 3);
    g_assert_true(results[1].error.has_value());
    const auto failed = results[1].error.value_or(
        filescope::core::make_error(ErrorCode::FAILED, "missing item error"));
    g_assert_true(failed.code == ErrorCode::IO);
}

struct ShutdownRecord {
    ShutdownRecord(JobQueue &queue_value, std::atomic<unsigned> &destroyed_value,
                   std::atomic<bool> &submit_rejected_value, std::thread::id &thread_value)
        : queue(queue_value), destroyed(destroyed_value), submit_rejected(submit_rejected_value),
          thread(thread_value) {}

    JobQueue &queue;
    std::atomic<unsigned> &destroyed;
    std::atomic<bool> &submit_rejected;
    std::thread::id &thread;

    ~ShutdownRecord() {
        thread = std::this_thread::get_id();
        ++destroyed;
        const auto result = queue.submit("rejected", [](Job &) -> Result<void> { return {}; });
        submit_rejected = !result.ok() && result.error().code == ErrorCode::CANCELLED;
    }
};

void test_shutdown_and_reentrant_submit() {
    JobQueue queue;
    StateLog log(queue);
    std::atomic<bool> started{false};
    std::atomic<unsigned> destroyed{0};
    std::atomic<bool> submit_rejected{false};
    std::thread::id destruction_thread;
    const auto main_thread = std::this_thread::get_id();

    auto running_token =
        std::make_shared<ShutdownRecord>(queue, destroyed, submit_rejected, destruction_thread);
    const auto running =
        queue.submit("running", [running_token, &started](Job &job) -> Result<void> {
            started = true;
            while (!job.is_cancelled()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return filescope::core::make_error(ErrorCode::CANCELLED, "cancelled");
        });
    running_token.reset();
    const auto waiting = queue.submit("waiting", [](Job &) -> Result<void> { return {}; });
    g_assert_true(running.ok());
    g_assert_true(waiting.ok());
    g_assert_true(wait_until([&started] { return started.load(); }, 2000));
    const auto signal_count = log.events.size();

    queue.shutdown();
    g_assert_cmpuint(destroyed.load(), ==, 1);
    g_assert_true(submit_rejected.load());
    g_assert_true(destruction_thread == main_thread);
    while (Glib::MainContext::get_default()->pending()) {
        Glib::MainContext::get_default()->iteration(false);
    }
    g_assert_cmpuint(log.events.size(), ==, signal_count);
    queue.shutdown();
}

void test_exception_becomes_failed() {
    JobQueue queue;
    StateLog log(queue);
    const auto submitted = queue.submit(
        "throws", [](Job &) -> Result<void> { throw std::runtime_error("broken body"); });
    g_assert_true(submitted.ok());
    g_assert_true(wait_until([&log] { return log.events.size() == 3; }, 2000));
    g_assert_true(queue.state(submitted.value()) == JobState::FAILED);
    g_assert_true(log.events.back().error == ErrorCode::FAILED);
}

} // namespace

int main(int argc, char **argv) {
    Gio::init();
    g_test_init(&argc, &argv, nullptr);
    g_test_add_func("/job/queue/fifo-nonblocking", test_fifo_and_nonblocking);
    g_test_add_func("/job/progress/throttled", test_progress_throttled);
    g_test_add_func("/job/cancel/queued-running", test_cancel_queued_and_running);
    g_test_add_func("/job/results/partial", test_partial_results);
    g_test_add_func("/job/shutdown/reentrant-submit", test_shutdown_and_reentrant_submit);
    g_test_add_func("/job/exception/failed", test_exception_becomes_failed);
    return g_test_run();
}
