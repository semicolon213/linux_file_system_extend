#include "core/error.hpp"

#include <cerrno>
#include <cstring>

#include <gio/gio.h>
#include <glibmm/convert.h>

namespace filescope::core {
namespace {

ErrorCode error_code_from_gio(int code) noexcept {
    switch (code) {
    case G_IO_ERROR_NOT_FOUND:
        return ErrorCode::NOT_FOUND;
    case G_IO_ERROR_PERMISSION_DENIED:
    case G_IO_ERROR_READ_ONLY:
        return ErrorCode::PERMISSION_DENIED;
    case G_IO_ERROR_EXISTS:
    case G_IO_ERROR_NOT_EMPTY:
        return ErrorCode::EXISTS;
    case G_IO_ERROR_NOT_DIRECTORY:
        return ErrorCode::NOT_DIRECTORY;
    case G_IO_ERROR_IS_DIRECTORY:
        return ErrorCode::IS_DIRECTORY;
    case G_IO_ERROR_NO_SPACE:
        return ErrorCode::NO_SPACE;
    case G_IO_ERROR_NOT_SUPPORTED:
        return ErrorCode::NOT_SUPPORTED;
    case G_IO_ERROR_CANCELLED:
        return ErrorCode::CANCELLED;
    case G_IO_ERROR_TOO_MANY_OPEN_FILES:
        return ErrorCode::TOO_MANY_OPEN;
    case G_IO_ERROR_FILENAME_TOO_LONG:
        return ErrorCode::NAME_TOO_LONG;
    case G_IO_ERROR_INVALID_FILENAME:
        return ErrorCode::INVALID_PATH;
    default:
        return ErrorCode::FAILED;
    }
}

} // namespace

ErrorCode error_code_from_errno(int errnum) noexcept {
    switch (errnum) {
    case ENOENT:
        return ErrorCode::NOT_FOUND;
    case EACCES:
    case EPERM:
    case EROFS:
        return ErrorCode::PERMISSION_DENIED;
    case EEXIST:
    case ENOTEMPTY:
        return ErrorCode::EXISTS;
    case ENOTDIR:
        return ErrorCode::NOT_DIRECTORY;
    case EISDIR:
        return ErrorCode::IS_DIRECTORY;
    case ENOSPC:
    case EDQUOT:
        return ErrorCode::NO_SPACE;
    case EIO:
        return ErrorCode::IO;
    case EXDEV:
        return ErrorCode::CROSS_DEVICE;
    case ELOOP:
        return ErrorCode::LOOP;
    case ENAMETOOLONG:
        return ErrorCode::NAME_TOO_LONG;
    case EMFILE:
    case ENFILE:
        return ErrorCode::TOO_MANY_OPEN;
    default:
        return ErrorCode::FAILED;
    }
}

Error error_from_errno(int errnum, std::string_view op, const std::string &path) {
    return {error_code_from_errno(errnum), std::string(op) + ": " +
                                               Glib::filename_display_name(path) + ": " +
                                               std::strerror(errnum)};
}

Error error_from_glib(const Glib::Error &error) {
    const ErrorCode code =
        error.domain() == G_IO_ERROR ? error_code_from_gio(error.code()) : ErrorCode::FAILED;
    return {code, error.what()};
}

Error make_error(ErrorCode code, std::string message) { return {code, std::move(message)}; }

} // namespace filescope::core
