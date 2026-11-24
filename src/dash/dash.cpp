#include "dash/dash.hpp"

#include <algorithm>
#include <limits>

namespace filescope::dash {
namespace {

constexpr std::int64_t kMinuteUs = 60'000'000;

std::int64_t minute_start(std::int64_t time_us) {
    const auto remainder = time_us % kMinuteUs;
    return time_us - remainder - (remainder < 0 ? kMinuteUs : 0);
}

} // namespace

Dash::Dash(std::uint32_t capacity_minutes)
    : capacity_(std::max(capacity_minutes, kMinCapacityMinutes)) {
    candles_.reserve(capacity_);
    latencies_.reserve(kLatencySamples);
}

void Dash::set_root_total(core::WatchId id, std::optional<std::uint64_t> total_bytes,
                          std::int64_t now_us) {
    const bool first_value = candles_.empty();
    if (!first_value) {
        advance_to(now_us);
    }
    const auto root = std::find_if(roots_.begin(), roots_.end(),
                                   [id](const auto &entry) { return entry.first == id; });
    if (root == roots_.end()) {
        roots_.emplace_back(id, total_bytes);
    } else {
        root->second = total_bytes;
    }
    if (first_value) {
        advance_to(now_us);
    }
    update_level();
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) -- Contract fixes this order.
void Dash::remove_root(core::WatchId id, std::int64_t now_us) {
    advance_to(now_us);
    roots_.erase(std::remove_if(roots_.begin(), roots_.end(),
                                [id](const auto &entry) { return entry.first == id; }),
                 roots_.end());
    update_level();
}

void Dash::add_events(const std::vector<core::EventPtr> &events) {
    for (const auto &event : events) {
        if (!event) {
            continue;
        }
        advance_to(event->observed_at_us);

        latest_event_time_us_ = std::max(latest_event_time_us_, event->observed_at_us);
        event_times_.push_back(event->observed_at_us);
        const auto cutoff = latest_event_time_us_ - kMinuteUs;
        event_times_.erase(std::remove_if(event_times_.begin(), event_times_.end(),
                                          [cutoff](auto time) { return time < cutoff; }),
                           event_times_.end());

        auto &candle = candles_.back();
        switch (event->kind) {
        case core::EventKind::CREATE:
            ++counts_.creates;
            ++candle.creates;
            candle.has_events = true;
            break;
        case core::EventKind::MODIFY:
            ++counts_.modifies;
            ++candle.modifies;
            candle.has_events = true;
            break;
        case core::EventKind::DELETE:
            ++counts_.deletes;
            ++candle.deletes;
            candle.has_events = true;
            break;
        case core::EventKind::RENAME:
        case core::EventKind::MOVE:
        case core::EventKind::MOVED_IN:
        case core::EventKind::MOVED_OUT:
            ++counts_.moves;
            break;
        case core::EventKind::ATTRIB:
            ++counts_.attribs;
            break;
        case core::EventKind::OVERFLOW:
            candle.has_gap = true;
            break;
        default:
            break;
        }

        if (event->watch_id != 0) {
            const auto root = std::find_if(roots_.begin(), roots_.end(), [&](const auto &entry) {
                return entry.first == event->watch_id;
            });
            if (root == roots_.end()) {
                roots_.emplace_back(event->watch_id, event->root_total);
            } else {
                root->second = event->root_total;
            }
        }
        if (!event->root_total) {
            candle.level_known = false;
        }
        update_level();
    }
}

void Dash::tick(std::int64_t now_us) { advance_to(now_us); }

std::vector<Candle> Dash::candles(std::uint32_t n) const {
    const auto count = std::min<std::size_t>(n, candles_.size());
    return {candles_.end() - static_cast<std::ptrdiff_t>(count), candles_.end()};
}

Counts Dash::counts() const { return counts_; }

std::uint32_t Dash::events_last_minute(std::int64_t now_us) const {
    const auto cutoff = now_us - kMinuteUs;
    return static_cast<std::uint32_t>(
        std::count_if(event_times_.begin(), event_times_.end(),
                      [cutoff, now_us](auto time) { return time >= cutoff && time <= now_us; }));
}

void Dash::record_latency(std::int64_t latency_ns) {
    if (latencies_.size() < kLatencySamples) {
        latencies_.push_back(latency_ns);
        return;
    }
    latencies_[next_latency_] = latency_ns;
    next_latency_ = (next_latency_ + 1) % kLatencySamples;
}

std::optional<std::int64_t> Dash::latency_p95_ns() const {
    if (latencies_.empty()) {
        return std::nullopt;
    }
    auto sorted = latencies_;
    std::sort(sorted.begin(), sorted.end());
    const auto rank = (95 * sorted.size() + 99) / 100;
    return sorted[rank - 1];
}

void Dash::advance_to(std::int64_t now_us) {
    const auto target = minute_start(now_us);
    if (candles_.empty()) {
        Candle candle;
        candle.minute_start_us = target;
        if (const auto value = total()) {
            candle.level_known = true;
            candle.open = candle.high = candle.low = candle.close = *value;
        }
        candles_.push_back(candle);
        return;
    }
    if (target <= candles_.back().minute_start_us) {
        return;
    }

    auto first = candles_.back().minute_start_us + kMinuteUs;
    const auto gap =
        static_cast<std::uint64_t>((target - candles_.back().minute_start_us) / kMinuteUs);
    if (gap >= capacity_) {
        candles_.clear();
        first = target - static_cast<std::int64_t>(capacity_ - 1) * kMinuteUs;
    }
    for (auto minute = first; minute <= target; minute += kMinuteUs) {
        Candle candle;
        candle.minute_start_us = minute;
        if (const auto value = total()) {
            candle.level_known = true;
            candle.open = candle.high = candle.low = candle.close = *value;
        }
        candles_.push_back(candle);
        if (candles_.size() > capacity_) {
            candles_.erase(candles_.begin());
        }
    }
}

void Dash::update_level() {
    const auto value = total();
    if (!value || candles_.empty()) {
        if (!candles_.empty()) {
            candles_.back().level_known = false;
        }
        return;
    }
    auto &candle = candles_.back();
    if (!candle.level_known) {
        return;
    }
    candle.high = std::max(candle.high, *value);
    candle.low = std::min(candle.low, *value);
    candle.close = *value;
}

std::optional<std::uint64_t> Dash::total() const {
    std::uint64_t result = 0;
    for (const auto &[id, value] : roots_) {
        static_cast<void>(id);
        if (!value || *value > std::numeric_limits<std::uint64_t>::max() - result) {
            return std::nullopt;
        }
        result += *value;
    }
    return result;
}

} // namespace filescope::dash
