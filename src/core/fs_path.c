#include "core/fs_path.h"

#include "core/fs_error.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>

static gboolean fs_path_set_validation_error(GError **error, FsErrorCode code,
                                             const char *message) {
    g_set_error_literal(error, FS_ERROR, code, message);
    return FALSE;
}

gboolean fs_path_validate_absolute(const char *path, GError **error) {
    if (path == NULL || path[0] != '/') {
        return fs_path_set_validation_error(error, FS_ERROR_INVALID_PATH, "path must be absolute");
    }
    if (strlen(path) >= PATH_MAX) {
        return fs_path_set_validation_error(error, FS_ERROR_NAME_TOO_LONG, "path exceeds PATH_MAX");
    }
    return TRUE;
}

gboolean fs_path_validate_name(const char *name, GError **error) {
    if (name == NULL || name[0] == '\0' || strchr(name, '/') != NULL || g_str_equal(name, ".") ||
        g_str_equal(name, "..")) {
        return fs_path_set_validation_error(error, FS_ERROR_INVALID_PATH,
                                            "name must be one non-special path component");
    }
    if (strlen(name) > NAME_MAX) {
        return fs_path_set_validation_error(error, FS_ERROR_NAME_TOO_LONG, "name exceeds NAME_MAX");
    }
    return TRUE;
}

char *fs_path_join(const char *dir, const char *name) { return g_build_filename(dir, name, NULL); }

char *fs_path_canonicalize(const char *path) {
    if (path == NULL) {
        return NULL;
    }
    return g_canonicalize_filename(path, NULL);
}

gboolean fs_path_is_ancestor(const char *ancestor, const char *path) {
    gsize length;

    if (ancestor == NULL || path == NULL) {
        return FALSE;
    }
    if (g_str_equal(ancestor, path) || g_str_equal(ancestor, "/")) {
        return TRUE;
    }
    length = strlen(ancestor);
    return g_str_has_prefix(path, ancestor) && ancestor[length - 1] != '/' && path[length] == '/';
}

static gboolean fs_path_has_component_prefix(const char *path, const char *prefix) {
    gsize length = strlen(prefix);

    return g_str_equal(path, prefix) || (g_str_has_prefix(path, prefix) && path[length] == '/');
}

static gboolean fs_path_is_run_exception(const char *path) {
    g_autofree char *user_path = g_strdup_printf("/run/user/%u", (guint)getuid());

    return fs_path_has_component_prefix(path, "/run/media") ||
           fs_path_has_component_prefix(path, user_path);
}

gboolean fs_path_check_protected(const char *path, FsPathOp op, GError **error) {
    static const char *const protected_paths[] = {
        "/bin",  "/boot", "/dev",  "/etc",  "/lib", "/lib32", "/lib64", "/libx32",
        "/proc", "/run",  "/sbin", "/snap", "/sys", "/usr",   "/var",
    };
    g_autofree char *canonical = NULL;
    const char *home;

    if (!fs_path_validate_absolute(path, error)) {
        return FALSE;
    }
    if (op == FS_PATH_OP_READ) {
        return TRUE;
    }

    canonical = fs_path_canonicalize(path);
    if (op == FS_PATH_OP_REMOVE) {
        home = g_getenv("HOME");
        if (home == NULL) {
            home = g_get_home_dir();
        }
        if (g_str_equal(canonical, "/") || (home != NULL && g_str_equal(canonical, home))) {
            return fs_path_set_validation_error(error, FS_ERROR_PROTECTED_PATH,
                                                "operation is blocked for a protected path");
        }
    }
    for (gsize i = 0; i < G_N_ELEMENTS(protected_paths); i++) {
        if (fs_path_has_component_prefix(canonical, protected_paths[i]) &&
            !(g_str_equal(protected_paths[i], "/run") && fs_path_is_run_exception(canonical))) {
            return fs_path_set_validation_error(error, FS_ERROR_PROTECTED_PATH,
                                                "operation is blocked for a protected path");
        }
    }
    return TRUE;
}

gboolean fs_path_check_access(const char *path, FsPathOp op, GError **error) {
    g_autofree char *parent = NULL;
    const char *checked_path = path;
    int mode = R_OK;

    if (!fs_path_validate_absolute(path, error)) {
        return FALSE;
    }
    if (op == FS_PATH_OP_REMOVE) {
        parent = g_path_get_dirname(path);
        checked_path = parent;
        mode = W_OK | X_OK;
    } else if (op == FS_PATH_OP_WRITE) {
        mode = W_OK;
        if (faccessat(AT_FDCWD, path, F_OK, AT_EACCESS) != 0 && errno == ENOENT) {
            parent = g_path_get_dirname(path);
            checked_path = parent;
            mode = W_OK | X_OK;
        }
    }
    if (faccessat(AT_FDCWD, checked_path, mode, AT_EACCESS) != 0) {
        int saved_errno = errno;

        return fs_set_error_from_errno(error, saved_errno, "access", checked_path);
    }
    return TRUE;
}
