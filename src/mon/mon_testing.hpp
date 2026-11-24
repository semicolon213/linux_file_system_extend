#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/event.hpp"
#include "mon/mon.hpp"
#include "mon/mon_internal.hpp"

namespace filescope::mon::testing {

using RawWatch = internal::RawWatch;

void inject_overflow(Watcher &watcher);
void set_queue_capacity(Watcher &watcher, std::uint32_t capacity);
void set_move_window_ms(Watcher &watcher, std::uint32_t ms);
std::vector<core::Event> normalize_raw_events(const std::vector<std::uint8_t> &buffer,
                                              const std::vector<RawWatch> &watches);

} // namespace filescope::mon::testing
