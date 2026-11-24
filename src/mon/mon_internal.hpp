#pragma once

#include <string>

#include "core/types.hpp"

namespace filescope::mon::internal {

struct RawWatch {
    int wd = -1;
    core::WatchId watch_id = 0;
    std::string path;
};

} // namespace filescope::mon::internal
