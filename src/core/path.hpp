#pragma once

#include <cstdint>
#include <string>

#include "core/error.hpp"

namespace filescope::core {

// [any] Validates the public absolute-path contract.
Result<void> validate_absolute(const std::string &path);
// [any] Validates one non-special path component.
Result<void> validate_name(const std::string &name);
std::string path_join(const std::string &dir, const std::string &name);
// [any] Performs lexical normalization without resolving links.
std::string canonicalize(const std::string &path);
bool is_ancestor(const std::string &ancestor, const std::string &path) noexcept;

enum class PathOp : std::uint8_t { READ, WRITE, REMOVE };

// [any] Enforces SEC-003 without modifying the filesystem.
Result<void> check_protected(const std::string &path, PathOp op);
// [any] Performs the advisory SEC-001 access check.
Result<void> check_access(const std::string &path, PathOp op);

} // namespace filescope::core
