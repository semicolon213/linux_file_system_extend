#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "core/types.hpp"

namespace filescope::core {

enum class EventKind : std::uint8_t {
    CREATE,
    DELETE,
    MODIFY,
    CLOSE_WRITE,
    ATTRIB,
    MOVED_OUT,
    MOVED_IN,
    RENAME,
    MOVE,
    DELETE_SELF,
    MOVE_SELF,
    UNMOUNT,
    OVERFLOW,
    RESYNC_DONE,
};

enum class OriginHint : std::uint8_t { UNKNOWN };

struct EventStat {
    std::uint64_t size = 0;
    std::int64_t mtime_ns = 0;
    std::uint32_t mode = 0;
};

struct Event {
    std::uint64_t seq = 0;
    std::int64_t observed_at_us = 0;
    std::int64_t monotonic_ns = 0;
    std::int64_t last_monotonic_ns = 0;
    std::uint32_t raw_mask = 0;
    EventKind kind = EventKind::CREATE;
    bool synthetic = false;
    WatchId watch_id = 0;
    std::string path;
    std::string old_path;
    std::uint32_t cookie = 0;
    bool is_dir = false;
    OriginHint origin_hint = OriginHint::UNKNOWN;
    std::uint64_t gap_id = 0;
    std::uint32_t merged_count = 1;
    std::optional<EventStat> stat;
    std::optional<std::int64_t> size_delta;
    std::optional<std::uint64_t> root_total;
};

using EventPtr = std::shared_ptr<const Event>;

// [any] Moves an event value into immutable shared ownership.
EventPtr make_event(Event event);
// [any] Returns a static contract name for the event kind.
const char *to_string(EventKind kind) noexcept;

} // namespace filescope::core
