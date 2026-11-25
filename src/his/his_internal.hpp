#pragma once

#include <functional>

#include "his/his.hpp"

namespace filescope::his {

namespace testing {
enum class FaultPoint : std::uint8_t;
}

namespace internal {

inline constexpr std::uint64_t max_content_bytes = 8ULL * 1024 * 1024;

struct Backlog {
    std::int64_t first;
    std::int64_t last;
};

struct Capture {
    std::optional<Version> version;
    std::optional<fs::FileStamp> stamp;
    bool changed = false;
};

bool excluded_tree(const std::string &path, const std::string &data_dir);

class Store {
  public:
    explicit Store(Options options);
    ~Store();

    core::Result<bool> open();
    core::Status recover(const std::function<void()> &between_steps);
    core::Status record(const std::vector<core::EventPtr> &events,
                        const std::vector<Backlog> &drops);
    core::Result<Capture> capture(const std::string &path, Origin origin,
                                  const std::function<void()> &after_copy = {},
                                  bool defer_retry = false,
                                  std::optional<std::uint64_t> baseline_bytes = std::nullopt);
    core::Status start_scan();
    core::Status note_pause(bool paused, std::int64_t at);
    core::Result<fs::FileStamp> restore(VersionId id, fs::Confirmation confirmation,
                                        const std::function<void(Version)> &added);
    core::Result<fs::FileStamp> recover_file(VersionId id);
    core::Result<FileHistory> load(const std::string &path);
    core::Result<std::vector<FileHistory>> deleted(const std::string &dir);
    core::Result<std::string> content(VersionId id);
    core::Status close();
    void set_fault(testing::FaultPoint point);

    bool corrupt() const;
    bool over_limit() const;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace internal
} // namespace filescope::his
