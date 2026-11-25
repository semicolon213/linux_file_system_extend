#pragma once

#include <functional>

#include "his/his.hpp"

namespace filescope::his {

namespace testing {
enum class FaultPoint : std::uint8_t;
}

namespace internal {

struct Backlog {
    std::int64_t first;
    std::int64_t last;
};

class Store {
  public:
    explicit Store(Options options);
    ~Store();

    core::Result<bool> open();
    core::Status recover(const std::function<void()> &between_steps);
    core::Status record(const std::vector<core::EventPtr> &events,
                        const std::vector<Backlog> &drops);
    core::Result<std::optional<Version>> capture(const std::string &path, Origin origin,
                                                 const std::function<void()> &after_copy);
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
