#include "dash/dash.hpp"

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include <glib.h>

namespace {

constexpr std::int64_t kMinuteUs = 60'000'000;

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) -- Mirrors Event field order.
filescope::core::EventPtr event(filescope::core::EventKind kind, std::int64_t time_us,
                                filescope::core::WatchId watch_id,
                                std::optional<std::uint64_t> root_total, bool synthetic = false,
                                std::uint32_t merged_count = 1) {
    filescope::core::Event value;
    value.kind = kind;
    value.observed_at_us = time_us;
    value.watch_id = watch_id;
    value.root_total = root_total;
    value.synthetic = synthetic;
    value.merged_count = merged_count;
    return filescope::core::make_event(std::move(value));
}

void test_candle() {
    filescope::dash::Dash dash(30);
    dash.set_root_total(1, 1000, 0);
    dash.add_events({event(filescope::core::EventKind::MODIFY, 1, 1, 1200),
                     event(filescope::core::EventKind::MODIFY, 2, 1, 900),
                     event(filescope::core::EventKind::MODIFY, 3, 1, 1100)});

    auto candles = dash.candles(1);
    g_assert_cmpuint(candles[0].open, ==, 1000);
    g_assert_cmpuint(candles[0].high, ==, 1200);
    g_assert_cmpuint(candles[0].low, ==, 900);
    g_assert_cmpuint(candles[0].close, ==, 1100);

    dash.tick(kMinuteUs);
    candles = dash.candles(1);
    g_assert_cmpuint(candles[0].open, ==, 1100);
    g_assert_false(candles[0].has_events);
}

void test_roots_and_volume() {
    filescope::dash::Dash dash(30);
    dash.set_root_total(1, 500, 0);
    dash.set_root_total(2, 300, 0);
    dash.add_events({event(filescope::core::EventKind::MODIFY, 1, 1, 600)});
    g_assert_cmpuint(dash.candles(1)[0].close, ==, 900);
    dash.remove_root(2, 2);
    const auto candle = dash.candles(1)[0];
    g_assert_cmpuint(candle.close, ==, 600);
    g_assert_cmpuint(candle.modifies, ==, 1);
}

void test_counts() {
    filescope::dash::Dash dash(30);
    dash.set_root_total(1, 0, 0);
    std::vector<filescope::core::EventPtr> events;
    for (unsigned i = 0; i < 2; ++i) {
        events.push_back(event(filescope::core::EventKind::CREATE, i + 1, 1, 0, i == 1));
    }
    for (unsigned i = 0; i < 3; ++i) {
        events.push_back(event(filescope::core::EventKind::MODIFY, i + 3, 1, 0, false, 10));
    }
    events.push_back(event(filescope::core::EventKind::DELETE, 6, 1, 0));
    for (unsigned i = 0; i < 4; ++i) {
        events.push_back(event(filescope::core::EventKind::CLOSE_WRITE, i + 7, 1, 0));
    }
    events.push_back(event(filescope::core::EventKind::RENAME, 11, 1, 0));
    dash.add_events(events);

    const auto candle = dash.candles(1)[0];
    g_assert_cmpuint(candle.creates, ==, 2);
    g_assert_cmpuint(candle.modifies, ==, 3);
    g_assert_cmpuint(candle.deletes, ==, 1);
    const auto counts = dash.counts();
    g_assert_cmpuint(counts.creates, ==, 2);
    g_assert_cmpuint(counts.modifies, ==, 3);
    g_assert_cmpuint(counts.deletes, ==, 1);
    g_assert_cmpuint(counts.moves, ==, 1);
}

void test_unknown_and_gap() {
    filescope::dash::Dash dash(30);
    dash.set_root_total(1, 100, 0);
    dash.add_events({event(filescope::core::EventKind::MODIFY, 1, 1, std::nullopt),
                     event(filescope::core::EventKind::MODIFY, 2, 1, 120),
                     event(filescope::core::EventKind::OVERFLOW, 3, 0, std::nullopt)});
    const auto first = dash.candles(1)[0];
    g_assert_false(first.level_known);
    g_assert_true(first.has_gap);

    dash.tick(kMinuteUs);
    const auto next = dash.candles(1)[0];
    g_assert_true(next.level_known);
    g_assert_cmpuint(next.open, ==, 120);
}

void test_ring_and_clock_regression() {
    filescope::dash::Dash dash(10);
    dash.set_root_total(1, 100, 0);
    dash.tick(90 * kMinuteUs);
    auto candles = dash.candles(100);
    g_assert_cmpuint(candles.size(), ==, 30);
    g_assert_cmpint(candles.front().minute_start_us, ==, 61 * kMinuteUs);
    g_assert_cmpint(candles.back().minute_start_us, ==, 90 * kMinuteUs);

    dash.add_events({event(filescope::core::EventKind::CREATE, 89 * kMinuteUs, 1, 100)});
    candles = dash.candles(1);
    g_assert_cmpint(candles[0].minute_start_us, ==, 90 * kMinuteUs);
    g_assert_cmpuint(candles[0].creates, ==, 1);
}

void test_metrics() {
    filescope::dash::Dash dash(30);
    constexpr std::int64_t now = 120 * kMinuteUs;
    dash.set_root_total(1, 0, now - kMinuteUs);
    dash.add_events({event(filescope::core::EventKind::CREATE, now - 59'900'000, 1, 0),
                     event(filescope::core::EventKind::CREATE, now - 60'100'000, 1, 0)});
    g_assert_cmpuint(dash.events_last_minute(now), ==, 1);
    g_assert_false(dash.latency_p95_ns().has_value());

    for (std::int64_t value = 1; value <= 1024; ++value) {
        dash.record_latency(value);
    }
    g_assert_cmpint(dash.latency_p95_ns().value_or(-1), ==, 973);
    dash.record_latency(1025);
    g_assert_cmpint(dash.latency_p95_ns().value_or(-1), ==, 974);
}

} // namespace

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, nullptr);
    g_test_add_func("/dash/candle", test_candle);
    g_test_add_func("/dash/roots-and-volume", test_roots_and_volume);
    g_test_add_func("/dash/counts", test_counts);
    g_test_add_func("/dash/unknown-and-gap", test_unknown_and_gap);
    g_test_add_func("/dash/ring-and-clock-regression", test_ring_and_clock_regression);
    g_test_add_func("/dash/metrics", test_metrics);
    return g_test_run();
}
