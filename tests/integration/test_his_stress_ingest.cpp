#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#include <glib.h>
#include <glibmm/init.h>

#include "his/his_testing.hpp"
#include "test_support.hpp"

namespace {

using namespace filescope;
using namespace std::chrono_literals;
namespace disk = std::filesystem;

void measure_ingest() {
    test::TestEnvironment environment;
    const auto data = environment.root() + "/history";
    auto opened = his::History::open({data, {}});
    g_assert_true(opened.ok());
    auto history = std::move(opened).value();
    his::testing::flush(*history);

    constexpr int batch_size = 100;
    constexpr int batch_count = 100;
    const auto started = std::chrono::steady_clock::now();
    std::size_t sampled_peak = 0;
    std::vector<double> latencies;
    for (int i = 0; i < batch_count; ++i) {
        std::this_thread::sleep_until(started + (i + 1) * 100ms);
        std::vector<core::EventPtr> batch;
        batch.reserve(batch_size);
        for (int j = 0; j < batch_size; ++j) {
            core::Event event;
            event.seq = i * batch_size + j;
            event.observed_at_us = g_get_real_time();
            event.kind = core::EventKind::MODIFY;
            event.path = environment.root() + "/source-" + std::to_string(j);
            batch.push_back(core::make_event(std::move(event)));
        }
        const auto submitted = std::chrono::steady_clock::now();
        history->record_events(batch);
        sampled_peak = std::max(sampled_peak, his::testing::pending_events(*history));
        his::testing::flush(*history);
        latencies.push_back(
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - submitted)
                .count());
    }
    history->shutdown();
    std::sort(latencies.begin(), latencies.end());
    const auto database = data + "/history.db";
    const auto rows = his::testing::query(database, "SELECT (SELECT count(*) FROM events),"
                                                    "(SELECT count(*) FROM gaps WHERE kind=3)");
    g_assert_true(rows.ok());
    const his::testing::Rows expected{{"10000", "0"}};
    g_assert_true(rows.value() == expected);

    g_test_message("G8 ingress: %d events, %d/s for 10 s; sampled pending peak=%zu, "
                   "batch commit P95=%.3f ms, checkpointed DB=%ju bytes, HIS_BACKLOG=0",
                   batch_size * batch_count, batch_size * 10, sampled_peak, latencies[94],
                   disk::file_size(database));
}

void measure_capture() {
    test::TestEnvironment environment;
    const auto data = environment.root() + "/history";
    auto opened = his::History::open({data, {}});
    g_assert_true(opened.ok());
    auto history = std::move(opened).value();
    his::testing::flush(*history);

    constexpr int count = 100;
    std::string buffer(1024ULL * 1024, 'x');
    std::vector<std::string> paths;
    for (int i = 0; i < count; ++i) {
        buffer[0] = static_cast<char>(i);
        auto &path = paths.emplace_back(environment.root() + "/source-" + std::to_string(i));
        std::ofstream output(path, std::ios::binary);
        output << buffer;
        output.close();
        g_assert_true(output.good());
    }
    const auto started = std::chrono::steady_clock::now();
    for (const auto &path : paths) {
        his::testing::capture(*history, path);
    }
    his::testing::flush(*history);
    const auto seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    const auto rows =
        his::testing::query(data + "/history.db", "SELECT count(*),sum(size) FROM blobs");
    g_assert_true(rows.ok());
    const his::testing::Rows expected{{"100", "104857600"}};
    g_assert_true(rows.value() == expected);
    g_test_message("G8 capture: %d x 1 MiB unique blobs in %.3f s (%.2f MiB/s)", count, seconds,
                   count / seconds);
}

} // namespace

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, nullptr);
    Glib::init();
    g_test_add_func("/his/g8/ingest", measure_ingest);
    g_test_add_func("/his/g8/capture", measure_capture);
    return g_test_run();
}
