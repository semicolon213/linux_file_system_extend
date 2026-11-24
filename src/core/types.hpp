#pragma once

#include <cstdint>
#include <string>

namespace filescope::core {

using WatchId = std::uint32_t;

enum class FileType : std::uint8_t { REGULAR, DIRECTORY, SYMLINK, OTHER };

struct Entry {
    std::string name;
    FileType type = FileType::OTHER;
    FileType target_type = FileType::OTHER;
    bool is_broken_link = false;
    bool is_mount_point = false;
    bool is_hidden = false;
    std::uint64_t size = 0;
    std::uint32_t mode = 0;
    std::uint32_t uid = 0;
    std::uint32_t gid = 0;
    std::uint64_t dev = 0;
    std::uint64_t ino = 0;
    std::int64_t mtime_ns = 0;
};

} // namespace filescope::core
