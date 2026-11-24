#pragma once

namespace filescope {

inline constexpr char kAppId[] = "io.github.filescope";
inline constexpr char kAppName[] = "FileScope";

const char *app_name() noexcept;
const char *app_version() noexcept;

} // namespace filescope
