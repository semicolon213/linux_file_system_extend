#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <sigc++/sigc++.h>

#include "core/error.hpp"
#include "core/event.hpp"

namespace filescope::his {

using VersionId = std::int64_t;

enum class Origin : std::uint8_t { BASELINE, AUTO, PRE_RESTORE };

struct Version {
    VersionId id = 0;
    std::string path;
    std::uint32_t number = 0;
    std::int64_t captured_at_us = 0;
    Origin origin = Origin::AUTO;
    std::uint64_t size = 0;
    std::int64_t mtime_ns = 0;
    std::uint32_t mode = 0;
    std::string sha256;
};

struct FileHistory {
    std::string path;
    std::vector<Version> versions;
    std::optional<std::int64_t> deleted_at_us;
    bool gap_after_latest = false;
};

struct Limits {
    std::uint32_t max_versions_per_file = 20;
    std::uint64_t max_file_bytes = 8ULL * 1024 * 1024;
    std::uint64_t max_store_bytes = 512ULL * 1024 * 1024;
    std::uint64_t baseline_max_file_bytes = 1ULL * 1024 * 1024;
    std::uint32_t baseline_max_files = 20000;
    std::uint64_t baseline_max_bytes = 128ULL * 1024 * 1024;
    std::uint32_t capture_delay_ms = 300;
    std::uint64_t max_event_rows = 200000;
    std::uint32_t event_retention_days = 30;
    std::uint32_t max_pending_events = 50000;
};

struct Options {
    std::string data_dir;
    Limits limits;
};

enum class State : std::uint8_t { READY, DEGRADED, DISABLED };

using HistorySlot = sigc::slot<void(core::Result<FileHistory>)>;
using DeletedSlot = sigc::slot<void(core::Result<std::vector<FileHistory>>)>;
using ContentSlot = sigc::slot<void(core::Result<std::string>)>;

class History {
    class Impl;

  public:
    explicit History(std::unique_ptr<Impl> impl);
    static core::Result<std::unique_ptr<History>> open(Options options);
    ~History();
    History(const History &) = delete;
    History &operator=(const History &) = delete;

    void shutdown();
    State state() const;
    void record_events(const std::vector<core::EventPtr> &events);
    void load_history_async(const std::string &path, HistorySlot done);
    void list_deleted_async(const std::string &dir, DeletedSlot done);
    void read_content_async(VersionId id, ContentSlot done);

    sigc::signal<void(const Version &)> &signal_version_added();
    sigc::signal<void(State, const std::optional<core::Error> &)> &signal_state();

  private:
    std::unique_ptr<Impl> impl_;
    friend struct TestAccess;
};

} // namespace filescope::his
