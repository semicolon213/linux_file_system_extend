#pragma once

#include <functional>
#include <optional>
#include <string>

namespace filescope::test {

class TestEnvironment {
  public:
    TestEnvironment();
    ~TestEnvironment();

    TestEnvironment(const TestEnvironment &) = delete;
    TestEnvironment &operator=(const TestEnvironment &) = delete;
    TestEnvironment(TestEnvironment &&) = delete;
    TestEnvironment &operator=(TestEnvironment &&) = delete;

    const std::string &root() const noexcept;

  private:
    std::string root_;
    std::optional<std::string> old_home_;
    std::optional<std::string> old_config_home_;
    std::optional<std::string> old_data_home_;
};

bool wait_until(const std::function<bool()> &condition, unsigned timeout_ms);

} // namespace filescope::test
