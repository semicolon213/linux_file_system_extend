#include "core/fs_error.h"

#include <errno.h>
#include <string.h>

G_DEFINE_QUARK(fs - error - quark, fs_error)

FsErrorCode fs_error_code_from_errno(int errnum) {
    switch (errnum) {
    case ENOENT:
        return FS_ERROR_NOT_FOUND;
    case EACCES:
    case EPERM:
    case EROFS:
        return FS_ERROR_PERMISSION_DENIED;
    case EEXIST:
    case ENOTEMPTY:
        return FS_ERROR_EXISTS;
    case ENOTDIR:
        return FS_ERROR_NOT_DIRECTORY;
    case EISDIR:
        return FS_ERROR_IS_DIRECTORY;
    case ENOSPC:
    case EDQUOT:
        return FS_ERROR_NO_SPACE;
    case EIO:
        return FS_ERROR_IO;
    case EXDEV:
        return FS_ERROR_CROSS_DEVICE;
    case ELOOP:
        return FS_ERROR_LOOP;
    case ENAMETOOLONG:
        return FS_ERROR_NAME_TOO_LONG;
    case EMFILE:
    case ENFILE:
        return FS_ERROR_TOO_MANY_OPEN;
    default:
        return FS_ERROR_FAILED;
    }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): public contract fixes the order.
gboolean fs_set_error_from_errno(GError **error, int errnum, const char *op, const char *path) {
    g_autofree char *display_path = g_filename_display_name(path != NULL ? path : "");

    g_set_error(error, FS_ERROR, fs_error_code_from_errno(errnum), "%s: %s: %s",
                op != NULL ? op : "operation", display_path, g_strerror(errnum));
    return FALSE;
}

static FsErrorCode fs_error_code_from_gio(GIOErrorEnum code) {
    switch (code) {
    case G_IO_ERROR_NOT_FOUND:
        return FS_ERROR_NOT_FOUND;
    case G_IO_ERROR_PERMISSION_DENIED:
    case G_IO_ERROR_READ_ONLY:
        return FS_ERROR_PERMISSION_DENIED;
    case G_IO_ERROR_EXISTS:
    case G_IO_ERROR_NOT_EMPTY:
        return FS_ERROR_EXISTS;
    case G_IO_ERROR_NOT_DIRECTORY:
        return FS_ERROR_NOT_DIRECTORY;
    case G_IO_ERROR_IS_DIRECTORY:
        return FS_ERROR_IS_DIRECTORY;
    case G_IO_ERROR_NO_SPACE:
        return FS_ERROR_NO_SPACE;
    case G_IO_ERROR_NOT_SUPPORTED:
        return FS_ERROR_NOT_SUPPORTED;
    case G_IO_ERROR_CANCELLED:
        return FS_ERROR_CANCELLED;
    case G_IO_ERROR_TOO_MANY_OPEN_FILES:
        return FS_ERROR_TOO_MANY_OPEN;
    case G_IO_ERROR_FILENAME_TOO_LONG:
        return FS_ERROR_NAME_TOO_LONG;
    case G_IO_ERROR_INVALID_FILENAME:
        return FS_ERROR_INVALID_PATH;
    default:
        return FS_ERROR_FAILED;
    }
}

void fs_propagate_gio_error(GError **dest, GError *src) {
    FsErrorCode code;

    if (src == NULL) {
        return;
    }
    code = src->domain == G_IO_ERROR ? fs_error_code_from_gio((GIOErrorEnum)src->code)
                                     : FS_ERROR_FAILED;
    if (dest != NULL) {
        g_set_error_literal(dest, FS_ERROR, code, src->message);
    }
    g_error_free(src);
}
