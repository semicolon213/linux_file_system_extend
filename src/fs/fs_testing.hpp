#pragma once

#include "core/error.hpp"

namespace filescope::job {
class Job;
}

namespace filescope::fs::testing {

// Tests only: makes the next low-level file write fail with code.
void fail_next_write(core::ErrorCode code) noexcept;

// Invoked on the worker before each regular-file chunk is written, until cleared.
using CopyWriteHook = void (*)(job::Job &);
void set_copy_write_hook(CopyWriteHook hook) noexcept;

// Invoked once on the worker after a successful EXDEV copy, before removing the source.
using SourceRemovalHook = void (*)(const std::string &);
void set_source_removal_hook(SourceRemovalHook hook) noexcept;
void force_next_cross_device_move() noexcept;

} // namespace filescope::fs::testing
