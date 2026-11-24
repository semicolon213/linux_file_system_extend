#include "test_support.h"

#include <errno.h>
#include <glib/gstdio.h>
#include <sys/stat.h>

struct FsTestEnvironment {
    char *root;
    char *old_home;
    char *old_config_home;
    char *old_data_home;
};

char *fs_test_make_tmp_dir(GError **error) {
    return g_dir_make_tmp("filescope-test-XXXXXX", error);
}

static gboolean fs_test_path_is_safe_tmp(const char *path) {
    g_autofree char *canonical = g_canonicalize_filename(path, NULL);
    g_autofree char *tmp = g_canonicalize_filename(g_get_tmp_dir(), NULL);
    gsize length = strlen(tmp);

    return !g_str_equal(canonical, tmp) && g_str_has_prefix(canonical, tmp) &&
           canonical[length] == G_DIR_SEPARATOR;
}

static gboolean fs_test_remove_tree_internal(const char *path, GError **error) {
    GStatBuf stat_buffer;

    if (g_lstat(path, &stat_buffer) != 0) {
        if (errno == ENOENT) {
            return TRUE;
        }
        int saved_errno = errno;
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(saved_errno), "lstat %s: %s", path,
                    g_strerror(saved_errno));
        return FALSE;
    }
    if (S_ISDIR(stat_buffer.st_mode)) {
        g_autoptr(GDir) directory = g_dir_open(path, 0, error);
        const char *name;

        if (directory == NULL) {
            return FALSE;
        }
        while ((name = g_dir_read_name(directory)) != NULL) {
            g_autofree char *child = g_build_filename(path, name, NULL);

            if (!fs_test_remove_tree_internal(child, error)) {
                return FALSE;
            }
        }
        if (g_rmdir(path) != 0) {
            int saved_errno = errno;
            g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(saved_errno), "rmdir %s: %s",
                        path, g_strerror(saved_errno));
            return FALSE;
        }
    } else if (g_remove(path) != 0) {
        int saved_errno = errno;
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(saved_errno), "remove %s: %s",
                    path, g_strerror(saved_errno));
        return FALSE;
    }
    return TRUE;
}

gboolean fs_test_remove_tree(const char *path, GError **error) {
    if (path == NULL || !g_path_is_absolute(path) || !fs_test_path_is_safe_tmp(path)) {
        g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
                            "refusing to remove a path outside the temporary directory");
        return FALSE;
    }
    return fs_test_remove_tree_internal(path, error);
}

static void fs_test_restore_environment(const char *name, const char *value) {
    if (value != NULL) {
        g_setenv(name, value, TRUE);
    } else {
        g_unsetenv(name);
    }
}

FsTestEnvironment *fs_test_environment_new(GError **error) {
    FsTestEnvironment *environment = g_new0(FsTestEnvironment, 1);
    g_autofree char *config = NULL;
    g_autofree char *data = NULL;

    environment->old_home = g_strdup(g_getenv("HOME"));
    environment->old_config_home = g_strdup(g_getenv("XDG_CONFIG_HOME"));
    environment->old_data_home = g_strdup(g_getenv("XDG_DATA_HOME"));
    environment->root = fs_test_make_tmp_dir(error);
    if (environment->root == NULL) {
        fs_test_environment_free(environment);
        return NULL;
    }
    config = g_build_filename(environment->root, "config", NULL);
    data = g_build_filename(environment->root, "data", NULL);
    if (g_mkdir(config, 0700) != 0 || g_mkdir(data, 0700) != 0) {
        int saved_errno = errno;
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(saved_errno),
                    "create test XDG directory: %s", g_strerror(saved_errno));
        fs_test_environment_free(environment);
        return NULL;
    }
    g_setenv("HOME", environment->root, TRUE);
    g_setenv("XDG_CONFIG_HOME", config, TRUE);
    g_setenv("XDG_DATA_HOME", data, TRUE);
    return environment;
}

void fs_test_environment_free(FsTestEnvironment *environment) {
    if (environment == NULL) {
        return;
    }
    fs_test_restore_environment("HOME", environment->old_home);
    fs_test_restore_environment("XDG_CONFIG_HOME", environment->old_config_home);
    fs_test_restore_environment("XDG_DATA_HOME", environment->old_data_home);
    if (environment->root != NULL) {
        fs_test_remove_tree(environment->root, NULL);
    }
    g_free(environment->root);
    g_free(environment->old_home);
    g_free(environment->old_config_home);
    g_free(environment->old_data_home);
    g_free(environment);
}

const char *fs_test_environment_get_root(FsTestEnvironment *environment) {
    g_return_val_if_fail(environment != NULL, NULL);
    return environment->root;
}

typedef struct {
    GMainLoop *loop;
    FsTestConditionFunc condition;
    gpointer user_data;
    gboolean matched;
} FsTestWait;

static gboolean fs_test_wait_check(gpointer user_data) {
    FsTestWait *wait = user_data;

    wait->matched = wait->condition(wait->user_data);
    if (wait->matched) {
        g_main_loop_quit(wait->loop);
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

static gboolean fs_test_wait_timeout(gpointer user_data) {
    FsTestWait *wait = user_data;

    g_main_loop_quit(wait->loop);
    return G_SOURCE_REMOVE;
}

gboolean fs_test_wait_until(FsTestConditionFunc condition, gpointer user_data, guint timeout_ms) {
    g_autoptr(GMainLoop) loop = NULL;
    FsTestWait wait;
    guint check_source;
    guint timeout_source;

    g_return_val_if_fail(condition != NULL, FALSE);
    if (condition(user_data)) {
        return TRUE;
    }
    loop = g_main_loop_new(NULL, FALSE);
    wait = (FsTestWait){.loop = loop, .condition = condition, .user_data = user_data};
    check_source = g_timeout_add(1, fs_test_wait_check, &wait);
    timeout_source = g_timeout_add(MAX(timeout_ms, 1), fs_test_wait_timeout, &wait);
    g_main_loop_run(loop);
    if (g_main_context_find_source_by_id(NULL, check_source) != NULL) {
        g_source_remove(check_source);
    }
    if (g_main_context_find_source_by_id(NULL, timeout_source) != NULL) {
        g_source_remove(timeout_source);
    }
    return wait.matched;
}
