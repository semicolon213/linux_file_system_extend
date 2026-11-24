#include "core/error.hpp"

#include <array>
#include <cerrno>
#include <cstring>
#include <string>
#include <utility>

#include <gio/gio.h>
#include <glib.h>

namespace {

using filescope::core::ErrorCode;

void test_errno_mapping() {
    static constexpr std::array cases = {
        std::pair{ENOENT, ErrorCode::NOT_FOUND},
        std::pair{EACCES, ErrorCode::PERMISSION_DENIED},
        std::pair{EPERM, ErrorCode::PERMISSION_DENIED},
        std::pair{EROFS, ErrorCode::PERMISSION_DENIED},
        std::pair{EEXIST, ErrorCode::EXISTS},
        std::pair{ENOTEMPTY, ErrorCode::EXISTS},
        std::pair{ENOTDIR, ErrorCode::NOT_DIRECTORY},
        std::pair{EISDIR, ErrorCode::IS_DIRECTORY},
        std::pair{ENOSPC, ErrorCode::NO_SPACE},
        std::pair{EDQUOT, ErrorCode::NO_SPACE},
        std::pair{EIO, ErrorCode::IO},
        std::pair{EXDEV, ErrorCode::CROSS_DEVICE},
        std::pair{ELOOP, ErrorCode::LOOP},
        std::pair{ENAMETOOLONG, ErrorCode::NAME_TOO_LONG},
        std::pair{EMFILE, ErrorCode::TOO_MANY_OPEN},
        std::pair{ENFILE, ErrorCode::TOO_MANY_OPEN},
        std::pair{EINVAL, ErrorCode::FAILED},
    };
    for (const auto &[number, expected] : cases) {
        g_assert_true(filescope::core::error_code_from_errno(number) == expected);
    }

    const auto error = filescope::core::error_from_errno(ENOENT, "open", "/missing");
    g_assert_true(error.code == ErrorCode::NOT_FOUND);
    const std::string expected = "open: /missing: " + std::string(std::strerror(ENOENT));
    g_assert_cmpstr(error.message.c_str(), ==, expected.c_str());
}

void test_glib_mapping() {
    static constexpr std::array cases = {
        std::pair{G_IO_ERROR_NOT_FOUND, ErrorCode::NOT_FOUND},
        std::pair{G_IO_ERROR_PERMISSION_DENIED, ErrorCode::PERMISSION_DENIED},
        std::pair{G_IO_ERROR_READ_ONLY, ErrorCode::PERMISSION_DENIED},
        std::pair{G_IO_ERROR_EXISTS, ErrorCode::EXISTS},
        std::pair{G_IO_ERROR_NOT_EMPTY, ErrorCode::EXISTS},
        std::pair{G_IO_ERROR_NOT_DIRECTORY, ErrorCode::NOT_DIRECTORY},
        std::pair{G_IO_ERROR_IS_DIRECTORY, ErrorCode::IS_DIRECTORY},
        std::pair{G_IO_ERROR_NO_SPACE, ErrorCode::NO_SPACE},
        std::pair{G_IO_ERROR_NOT_SUPPORTED, ErrorCode::NOT_SUPPORTED},
        std::pair{G_IO_ERROR_CANCELLED, ErrorCode::CANCELLED},
        std::pair{G_IO_ERROR_TOO_MANY_OPEN_FILES, ErrorCode::TOO_MANY_OPEN},
        std::pair{G_IO_ERROR_FILENAME_TOO_LONG, ErrorCode::NAME_TOO_LONG},
        std::pair{G_IO_ERROR_INVALID_FILENAME, ErrorCode::INVALID_PATH},
    };
    for (const auto &[number, expected] : cases) {
        const Glib::Error source(G_IO_ERROR, number, "original message");
        const auto error = filescope::core::error_from_glib(source);
        g_assert_true(error.code == expected);
        g_assert_cmpstr(error.message.c_str(), ==, "original message");
    }

    const Glib::Error other(g_file_error_quark(), G_FILE_ERROR_FAILED, "other domain");
    const auto error = filescope::core::error_from_glib(other);
    g_assert_true(error.code == ErrorCode::FAILED);
    g_assert_cmpstr(error.message.c_str(), ==, "other domain");
}

void test_result() {
    filescope::core::Result<int> success = 42;
    g_assert_true(success.ok());
    g_assert_true(static_cast<bool>(success));
    g_assert_cmpint(success.value(), ==, 42);
    success.value() = 7;
    g_assert_cmpint(std::move(success).value(), ==, 7);

    filescope::core::Result<int> failure = filescope::core::make_error(ErrorCode::IO, "failed");
    g_assert_false(failure.ok());
    g_assert_true(failure.error().code == ErrorCode::IO);

    const filescope::core::Result<void> void_success;
    g_assert_true(void_success.ok());
    const filescope::core::Result<void> void_failure =
        filescope::core::make_error(ErrorCode::CANCELLED, "cancelled");
    g_assert_false(void_failure.ok());
    g_assert_true(void_failure.error().code == ErrorCode::CANCELLED);
}

} // namespace

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, nullptr);
    g_test_add_func("/core/error/errno-mapping", test_errno_mapping);
    g_test_add_func("/core/error/glib-mapping", test_glib_mapping);
    g_test_add_func("/core/error/result", test_result);
    return g_test_run();
}
