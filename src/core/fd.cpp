#include "core/fd.hpp"

#include <unistd.h>

namespace filescope::core {

UniqueFd::UniqueFd(int fd) noexcept : fd_(fd) {}

UniqueFd::UniqueFd(UniqueFd &&other) noexcept : fd_(other.release()) {}

UniqueFd &UniqueFd::operator=(UniqueFd &&other) noexcept {
    if (this != &other) {
        reset(other.release());
    }
    return *this;
}

UniqueFd::~UniqueFd() { reset(); }

int UniqueFd::get() const noexcept { return fd_; }

int UniqueFd::release() noexcept {
    const int released = fd_;
    fd_ = -1;
    return released;
}

void UniqueFd::reset(int fd) noexcept {
    if (fd_ == fd) {
        return;
    }
    if (fd_ >= 0) {
        close(fd_);
    }
    fd_ = fd;
}

UniqueFd::operator bool() const noexcept { return fd_ >= 0; }

} // namespace filescope::core
