#ifndef FILESCOPE_CORE_FS_PATH_H
#define FILESCOPE_CORE_FS_PATH_H

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
    FS_PATH_OP_READ,
    FS_PATH_OP_WRITE,
    FS_PATH_OP_REMOVE,
} FsPathOp;

gboolean fs_path_validate_absolute(const char *path, GError **error);
gboolean fs_path_validate_name(const char *name, GError **error);
char *fs_path_join(const char *dir, const char *name);
char *fs_path_canonicalize(const char *path);
gboolean fs_path_is_ancestor(const char *ancestor, const char *path);
gboolean fs_path_check_protected(const char *path, FsPathOp op, GError **error);
gboolean fs_path_check_access(const char *path, FsPathOp op, GError **error);

G_END_DECLS

#endif
