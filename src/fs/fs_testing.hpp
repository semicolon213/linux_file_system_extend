#pragma once

#include "core/error.hpp"

namespace filescope::fs::testing {

// Tests only: makes the next low-level file write fail with code.
void fail_next_write(core::ErrorCode code) noexcept;

} // namespace filescope::fs::testing
