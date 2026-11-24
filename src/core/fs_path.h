#ifndef FILESCOPE_CORE_FS_PATH_H
#define FILESCOPE_CORE_FS_PATH_H

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
    FS_PATH_OP_READ,
    FS_PATH_OP_WRITE,
    FS_PATH_OP_REMOVE,
} FsPathOp;

/* [any] Validates an absolute path shorter than PATH_MAX; sets FS_ERROR on failure. */
gboolean fs_path_validate_absolute(const char *path, GError **error);
/* [any] Validates one non-special component up to NAME_MAX; sets FS_ERROR on failure. */
gboolean fs_path_validate_name(const char *name, GError **error);
/* [any] Returns a newly allocated joined path; free with g_free(). */
char *fs_path_join(const char *dir, const char *name);
/* [any] Returns a newly allocated lexical normalization without resolving links. */
char *fs_path_canonicalize(const char *path);
/* [any] Returns TRUE when normalized path equals or is below normalized ancestor. */
gboolean fs_path_is_ancestor(const char *ancestor, const char *path);
/* [any] Enforces SEC-003 and sets FS_ERROR_PROTECTED_PATH when blocked. */
gboolean fs_path_check_protected(const char *path, FsPathOp op, GError **error);
/* [any] Performs the advisory SEC-001 access check and sets FS_ERROR on failure. */
gboolean fs_path_check_access(const char *path, FsPathOp op, GError **error);

G_END_DECLS

#endif
