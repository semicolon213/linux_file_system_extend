#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "core/event.hpp"

namespace filescope::dash {

struct Candle {
    std::int64_t minute_start_us = 0;
    bool has_events = false;
    bool level_known = false;
    bool has_gap = false;
    std::uint64_t open = 0;
    std::uint64_t high = 0;
    std::uint64_t low = 0;
    std::uint64_t close = 0;
    std::uint32_t creates = 0;
    std::uint32_t modifies = 0;
    std::uint32_t deletes = 0;
};

struct Counts {
    std::uint64_t creates = 0;
    std::uint64_t modifies = 0;
    std::uint64_t deletes = 0;
    std::uint64_t moves = 0;
    std::uint64_t attribs = 0;
};

class Dash {
  public:
    static constexpr std::uint32_t kMinCapacityMinutes = 30;
    static constexpr std::size_t kLatencySamples = 1024;

    explicit Dash(std::uint32_t capacity_minutes);

    void set_root_total(core::WatchId id, std::optional<std::uint64_t> total_bytes,
                        std::int64_t now_us);
    void remove_root(core::WatchId id, std::int64_t now_us);
    void add_events(const std::vector<core::EventPtr> &events);
    void tick(std::int64_t now_us);

    std::vector<Candle> candles(std::uint32_t n) const;
    Counts counts() const;
    std::uint32_t events_last_minute(std::int64_t now_us) const;
    void record_latency(std::int64_t latency_ns);
    std::optional<std::int64_t> latency_p95_ns() const;

  private:
    void advance_to(std::int64_t now_us);
    void update_level();
    std::optional<std::uint64_t> total() const;

    std::size_t capacity_;
    std::vector<Candle> candles_;
    std::vector<std::pair<core::WatchId, std::optional<std::uint64_t>>> roots_;
    Counts counts_;
    std::vector<std::int64_t> event_times_;
    std::int64_t latest_event_time_us_ = 0;
    std::vector<std::int64_t> latencies_;
    std::size_t next_latency_ = 0;
};

} // namespace filescope::dash
