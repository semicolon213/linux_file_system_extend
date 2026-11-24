#ifndef FILESCOPE_CORE_FS_ERROR_H
#define FILESCOPE_CORE_FS_ERROR_H

#include <gio/gio.h>

G_BEGIN_DECLS

#define FS_ERROR (fs_error_quark())

typedef enum {
    FS_ERROR_FAILED,
    FS_ERROR_NOT_FOUND,
    FS_ERROR_PERMISSION_DENIED,
    FS_ERROR_EXISTS,
    FS_ERROR_NOT_DIRECTORY,
    FS_ERROR_IS_DIRECTORY,
    FS_ERROR_NO_SPACE,
    FS_ERROR_IO,
    FS_ERROR_CROSS_DEVICE,
    FS_ERROR_LOOP,
    FS_ERROR_NAME_TOO_LONG,
    FS_ERROR_INVALID_PATH,
    FS_ERROR_PROTECTED_PATH,
    FS_ERROR_NOT_CONFIRMED,
    FS_ERROR_NOT_SUPPORTED,
    FS_ERROR_CONFLICT,
    FS_ERROR_TOO_LARGE,
    FS_ERROR_CANCELLED,
    FS_ERROR_WATCH_LIMIT,
    FS_ERROR_TOO_MANY_OPEN,
    FS_ERROR_PARTIAL,
} FsErrorCode;

GQuark fs_error_quark(void);
FsErrorCode fs_error_code_from_errno(int errnum);
gboolean fs_set_error_from_errno(GError **error, int errnum, const char *op, const char *path);
void fs_propagate_gio_error(GError **dest, GError *src);

G_END_DECLS

#endif
