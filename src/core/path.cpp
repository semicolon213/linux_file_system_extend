#include "core/path.hpp"

#include <array>
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <limits.h>
#include <string_view>
#include <unistd.h>
#include <vector>

namespace filescope::core {
namespace {

Result<void> validation_error(ErrorCode code, const char *message) {
    return make_error(code, message);
}

bool has_component_prefix(const std::string &path, std::string_view prefix) noexcept {
    return path == prefix ||
           (path.size() > prefix.size() && path.compare(0, prefix.size(), prefix) == 0 &&
            path[prefix.size()] == '/');
}

std::string parent_path(const std::string &path) {
    const auto slash = path.find_last_of('/');
    return slash == 0 ? "/" : path.substr(0, slash);
}

} // namespace

Result<void> validate_absolute(const std::string &path) {
    if (path.empty() || path.front() != '/' || path.find('\0') != std::string::npos) {
        return validation_error(ErrorCode::INVALID_PATH, "path must be absolute without NUL");
    }
    if (path.size() >= PATH_MAX) {
        return validation_error(ErrorCode::NAME_TOO_LONG, "path exceeds PATH_MAX");
    }
    return {};
}

Result<void> validate_name(const std::string &name) {
    if (name.empty() || name == "." || name == ".." || name.find('/') != std::string::npos ||
        name.find('\0') != std::string::npos) {
        return validation_error(ErrorCode::INVALID_PATH,
                                "name must be one non-special path component");
    }
    if (name.size() > NAME_MAX) {
        return validation_error(ErrorCode::NAME_TOO_LONG, "name exceeds NAME_MAX");
    }
    return {};
}

std::string path_join(const std::string &dir, const std::string &name) {
    return !dir.empty() && dir.back() == '/' ? dir + name : dir + '/' + name;
}

std::string canonicalize(const std::string &path) {
    const bool absolute = !path.empty() && path.front() == '/';
    std::vector<std::string_view> components;
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t end = path.find('/', start);
        const std::string_view component(path.data() + start,
                                         (end == std::string::npos ? path.size() : end) - start);
        if (!component.empty() && component != ".") {
            if (component == "..") {
                if (!components.empty() && components.back() != "..") {
                    components.pop_back();
                } else if (!absolute) {
                    components.push_back(component);
                }
            } else {
                components.push_back(component);
            }
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }

    std::string result = absolute ? "/" : "";
    for (const auto component : components) {
        if (!result.empty() && result.back() != '/') {
            result += '/';
        }
        result.append(component);
    }
    return result.empty() ? (absolute ? "/" : ".") : result;
}

bool is_ancestor(const std::string &ancestor, const std::string &path) noexcept {
    if (ancestor == path || ancestor == "/") {
        return true;
    }
    return path.size() > ancestor.size() && path.compare(0, ancestor.size(), ancestor) == 0 &&
           !ancestor.empty() && ancestor.back() != '/' && path[ancestor.size()] == '/';
}

Result<void> check_protected(const std::string &path, PathOp op) {
    static constexpr std::array<std::string_view, 15> protected_paths = {
        "/bin",  "/boot", "/dev",  "/etc",  "/lib", "/lib32", "/lib64", "/libx32",
        "/proc", "/run",  "/sbin", "/snap", "/sys", "/usr",   "/var",
    };
    if (const auto valid = validate_absolute(path); !valid) {
        return valid.error();
    }
    if (op == PathOp::READ) {
        return {};
    }

    const std::string normalized = canonicalize(path);
    if (op == PathOp::REMOVE) {
        const char *home = std::getenv("HOME");
        if (normalized == "/" || (home != nullptr && normalized == canonicalize(home))) {
            return validation_error(ErrorCode::PROTECTED_PATH,
                                    "operation is blocked for a protected path");
        }
    }

    const std::string run_user = "/run/user/" + std::to_string(getuid());
    for (const auto protected_path : protected_paths) {
        const bool run_exception =
            protected_path == "/run" && (has_component_prefix(normalized, "/run/media") ||
                                         has_component_prefix(normalized, run_user));
        if (has_component_prefix(normalized, protected_path) && !run_exception) {
            return validation_error(ErrorCode::PROTECTED_PATH,
                                    "operation is blocked for a protected path");
        }
    }
    return {};
}

Result<void> check_access(const std::string &path, PathOp op) {
    if (const auto valid = validate_absolute(path); !valid) {
        return valid.error();
    }

    std::string checked_path = path;
    int mode = R_OK;
    if (op == PathOp::REMOVE) {
        checked_path = parent_path(path);
        mode = W_OK | X_OK;
    } else if (op == PathOp::WRITE) {
        mode = W_OK;
        if (faccessat(AT_FDCWD, path.c_str(), F_OK, AT_EACCESS) != 0 && errno == ENOENT) {
            checked_path = parent_path(path);
            mode = W_OK | X_OK;
        }
    }
    if (faccessat(AT_FDCWD, checked_path.c_str(), mode, AT_EACCESS) != 0) {
        const int saved_errno = errno;
        return error_from_errno(saved_errno, "access", checked_path);
    }
    return {};
}

} // namespace filescope::core
