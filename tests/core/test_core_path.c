#include <limits.h>
#include <unistd.h>

#include <glib.h>

#include "core/fs_error.h"
#include "core/fs_path.h"
#include "test_support.h"

static void assert_validation_error(gboolean result, GError **error, FsErrorCode code) {
    g_assert_false(result);
    g_assert_error(*error, FS_ERROR, code);
}

static void test_validate_name(void) {
    static const char *const invalid[] = {"", "a/b", ".", ".."};

    for (gsize i = 0; i < G_N_ELEMENTS(invalid); i++) {
        g_autoptr(GError) error = NULL;

        assert_validation_error(fs_path_validate_name(invalid[i], &error), &error,
                                FS_ERROR_INVALID_PATH);
    }
    g_autofree char *long_name = g_strnfill(NAME_MAX + 1, 'x');
    g_autoptr(GError) error = NULL;

    assert_validation_error(fs_path_validate_name(long_name, &error), &error,
                            FS_ERROR_NAME_TOO_LONG);
    g_assert_true(fs_path_validate_name("notes.md", NULL));
}

static void test_validate_absolute(void) {
    g_autoptr(GError) relative_error = NULL;
    g_autoptr(GError) long_error = NULL;
    char *long_path = g_strnfill(PATH_MAX, 'x');

    assert_validation_error(fs_path_validate_absolute("relative", &relative_error), &relative_error,
                            FS_ERROR_INVALID_PATH);
    long_path[0] = '/';
    assert_validation_error(fs_path_validate_absolute(long_path, &long_error), &long_error,
                            FS_ERROR_NAME_TOO_LONG);
    g_free(long_path);
    g_assert_true(fs_path_validate_absolute("/tmp", NULL));
}

static void test_canonical_and_ancestor(void) {
    g_autofree char *canonical = fs_path_canonicalize("/a//b/./c/../d");

    g_assert_cmpstr(canonical, ==, "/a/b/d");
    g_assert_true(fs_path_is_ancestor("/a/b", "/a/b/d"));
    g_assert_true(fs_path_is_ancestor("/a/b", "/a/b"));
    g_assert_false(fs_path_is_ancestor("/a/b", "/a/bc"));
}

static void test_protected_paths(void) {
    g_autoptr(FsTestEnvironment) environment = NULL;
    g_autoptr(GError) error = NULL;
    g_autofree char *run_user = NULL;
    g_autofree char *temporary_child = NULL;

    environment = fs_test_environment_new(&error);
    g_assert_no_error(error);
    g_assert_nonnull(environment);
    run_user = g_strdup_printf("/run/user/%u/a", (guint)getuid());
    temporary_child =
        g_build_filename(fs_test_environment_get_root(environment), "removable", NULL);

    g_assert_false(fs_path_check_protected("/usr/bin/x", FS_PATH_OP_WRITE, &error));
    g_assert_error(error, FS_ERROR, FS_ERROR_PROTECTED_PATH);
    g_clear_error(&error);
    g_assert_false(fs_path_check_protected("/", FS_PATH_OP_REMOVE, &error));
    g_assert_error(error, FS_ERROR, FS_ERROR_PROTECTED_PATH);
    g_clear_error(&error);
    g_assert_false(fs_path_check_protected(fs_test_environment_get_root(environment),
                                           FS_PATH_OP_REMOVE, &error));
    g_assert_error(error, FS_ERROR, FS_ERROR_PROTECTED_PATH);
    g_clear_error(&error);
    g_assert_true(fs_path_check_protected(run_user, FS_PATH_OP_WRITE, &error));
    g_assert_no_error(error);
    g_assert_true(fs_path_check_protected(temporary_child, FS_PATH_OP_REMOVE, &error));
    g_assert_no_error(error);
    g_assert_true(fs_path_check_protected("/usr/bin/x", FS_PATH_OP_READ, &error));
    g_assert_no_error(error);
}

static void test_access(void) {
    g_autoptr(FsTestEnvironment) environment = NULL;
    g_autoptr(GError) error = NULL;
    g_autofree char *new_file = NULL;

    environment = fs_test_environment_new(&error);
    g_assert_no_error(error);
    new_file = g_build_filename(fs_test_environment_get_root(environment), "new", NULL);
    g_assert_true(fs_path_check_access(new_file, FS_PATH_OP_WRITE, &error));
    g_assert_no_error(error);
    g_assert_true(fs_path_check_access(new_file, FS_PATH_OP_REMOVE, &error));
    g_assert_no_error(error);
}

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/core/path/validate-name", test_validate_name);
    g_test_add_func("/core/path/validate-absolute", test_validate_absolute);
    g_test_add_func("/core/path/canonical-ancestor", test_canonical_and_ancestor);
    g_test_add_func("/core/path/protected", test_protected_paths);
    g_test_add_func("/core/path/access", test_access);
    return g_test_run();
}
