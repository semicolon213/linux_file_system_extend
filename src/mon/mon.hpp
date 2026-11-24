#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <sigc++/sigc++.h>

#include "core/error.hpp"
#include "core/event.hpp"

namespace filescope::mon {

enum class WatchState : std::uint8_t { REGISTERING, ACTIVE, DEGRADED, RESYNCING, STOPPED, ERROR };
enum class WatchReason : std::uint8_t {
    NONE,
    USER,
    ROOT_DELETED,
    ROOT_MOVED,
    UNMOUNTED,
    OVERFLOW,
    WATCH_LIMIT,
    LOST,
    IO,
};
enum class WatchMode : std::uint8_t { NON_RECURSIVE, RECURSIVE };

struct WatchInfo {
    core::WatchId id = 0;
    std::string path;
    WatchMode mode = WatchMode::NON_RECURSIVE;
    WatchState state = WatchState::REGISTERING;
    WatchReason reason = WatchReason::NONE;
    std::uint32_t watch_count = 0;
    bool total_known = false;
    bool total_partial = false;
    std::uint64_t total_bytes = 0;
    std::uint64_t file_count = 0;
};

struct RootTotal {
    std::uint64_t total_bytes = 0;
    std::uint64_t file_count = 0;
    bool partial = false;
};

struct WatchUsage {
    std::uint32_t used = 0;
    std::uint32_t limit = 0;
};

class Watcher {
    class Impl;

  public:
    static core::Result<std::unique_ptr<Watcher>> create();
    ~Watcher();
    Watcher(const Watcher &) = delete;
    Watcher &operator=(const Watcher &) = delete;

    core::Result<core::WatchId> add_root(const std::string &path, WatchMode mode);
    bool remove_root(core::WatchId id);
    std::vector<WatchInfo> list_roots() const;
    WatchUsage usage() const;
    core::WatchId find_root(const std::string &path) const;
    std::optional<RootTotal> root_total(core::WatchId id) const;
    sigc::signal<void(const std::vector<core::EventPtr> &)> &signal_events();
    sigc::signal<void(const WatchInfo &, const std::optional<core::Error> &)> &signal_state();

    explicit Watcher(std::unique_ptr<Impl> impl);

  private:
    std::unique_ptr<Impl> impl_;

    friend void testing_inject_overflow(Watcher &);
    friend void testing_set_queue_capacity(Watcher &, std::uint32_t);
    friend void testing_set_move_window_ms(Watcher &, std::uint32_t);
};

} // namespace filescope::mon
