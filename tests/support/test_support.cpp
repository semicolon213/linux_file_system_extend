#include "test_support.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <sys/stat.h>

#include <glib/gstdio.h>
#include <glibmm.h>

namespace filescope::test {
namespace {

std::optional<std::string> environment_value(const char *name) {
    const char *value = std::getenv(name);
    return value == nullptr ? std::nullopt : std::optional<std::string>(value);
}

void restore_environment(const char *name, const std::optional<std::string> &value) noexcept {
    if (value.has_value()) {
        g_setenv(name, value->c_str(), TRUE);
    } else {
        g_unsetenv(name);
    }
}

bool is_safe_temporary_path(const std::string &path) {
    const std::string canonical = Glib::canonicalize_filename(path);
    const std::string temporary = Glib::canonicalize_filename(Glib::get_tmp_dir());
    return canonical.size() > temporary.size() &&
           canonical.compare(0, temporary.size(), temporary) == 0 &&
           canonical[temporary.size()] == G_DIR_SEPARATOR;
}

bool remove_tree_internal(const std::string &path) noexcept {
    GStatBuf status{};
    if (g_lstat(path.c_str(), &status) != 0) {
        return errno == ENOENT;
    }
    if (!S_ISDIR(status.st_mode)) {
        return g_remove(path.c_str()) == 0;
    }

    GError *error = nullptr;
    std::unique_ptr<GDir, decltype(&g_dir_close)> directory(g_dir_open(path.c_str(), 0, &error),
                                                            g_dir_close);
    if (!directory) {
        g_clear_error(&error);
        return false;
    }
    while (const char *name = g_dir_read_name(directory.get())) {
        if (!remove_tree_internal(Glib::build_filename(path, name))) {
            return false;
        }
    }
    directory.reset();
    return g_rmdir(path.c_str()) == 0;
}

bool remove_tree(const std::string &path) noexcept {
    return !path.empty() && Glib::path_is_absolute(path) && is_safe_temporary_path(path) &&
           remove_tree_internal(path);
}

std::string make_temporary_directory() {
    GError *raw_error = nullptr;
    std::unique_ptr<char, decltype(&g_free)> path(
        g_dir_make_tmp("filescope-test-XXXXXX", &raw_error), g_free);
    std::unique_ptr<GError, decltype(&g_error_free)> error(raw_error, g_error_free);
    if (!path) {
        throw std::runtime_error(error ? error->message : "create temporary directory failed");
    }
    return path.get();
}

void make_directory(const std::string &path) {
    if (g_mkdir(path.c_str(), 0700) != 0) {
        const int saved_errno = errno;
        throw std::runtime_error("create test directory: " +
                                 std::string(std::strerror(saved_errno)));
    }
}

} // namespace

TestEnvironment::TestEnvironment()
    : root_(make_temporary_directory()), old_home_(environment_value("HOME")),
      old_config_home_(environment_value("XDG_CONFIG_HOME")),
      old_data_home_(environment_value("XDG_DATA_HOME")) {
    const std::string config = Glib::build_filename(root_, "config");
    const std::string data = Glib::build_filename(root_, "data");
    try {
        make_directory(config);
        make_directory(data);
    } catch (...) {
        remove_tree(root_);
        throw;
    }
    g_setenv("HOME", root_.c_str(), TRUE);
    g_setenv("XDG_CONFIG_HOME", config.c_str(), TRUE);
    g_setenv("XDG_DATA_HOME", data.c_str(), TRUE);
}

TestEnvironment::~TestEnvironment() {
    restore_environment("HOME", old_home_);
    restore_environment("XDG_CONFIG_HOME", old_config_home_);
    restore_environment("XDG_DATA_HOME", old_data_home_);
    if (!remove_tree(root_)) {
        g_warning("failed to remove test directory");
    }
}

const std::string &TestEnvironment::root() const noexcept { return root_; }

bool wait_until(const std::function<bool()> &condition, unsigned timeout_ms) {
    if (condition()) {
        return true;
    }
    const auto loop = Glib::MainLoop::create();
    bool matched = false;
    auto check = Glib::signal_timeout().connect(
        [&condition, &loop, &matched] {
            matched = condition();
            if (matched) {
                loop->quit();
            }
            return !matched;
        },
        1);
    auto timeout = Glib::signal_timeout().connect(
        [&loop] {
            loop->quit();
            return false;
        },
        std::max(timeout_ms, 1U));
    loop->run();
    check.disconnect();
    timeout.disconnect();
    return matched;
}

} // namespace filescope::test
