#pragma once

namespace filescope::core {

// Exclusive fd ownership; destruction closes a held descriptor.
class UniqueFd {
  public:
    UniqueFd() noexcept = default;
    explicit UniqueFd(int fd) noexcept;
    UniqueFd(UniqueFd &&other) noexcept;
    UniqueFd &operator=(UniqueFd &&other) noexcept;
    UniqueFd(const UniqueFd &) = delete;
    UniqueFd &operator=(const UniqueFd &) = delete;
    ~UniqueFd();

    int get() const noexcept;
    int release() noexcept;
    void reset(int fd = -1) noexcept;
    explicit operator bool() const noexcept;

  private:
    int fd_ = -1;
};

} // namespace filescope::core
