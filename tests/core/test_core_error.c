#include <errno.h>
#include <string.h>

#include <glib.h>

#include "core/fs_error.h"

static void test_errno_mapping(void) {
    static const struct {
        int errnum;
        FsErrorCode code;
    } cases[] = {
        {ENOENT, FS_ERROR_NOT_FOUND},
        {EACCES, FS_ERROR_PERMISSION_DENIED},
        {EPERM, FS_ERROR_PERMISSION_DENIED},
        {EROFS, FS_ERROR_PERMISSION_DENIED},
        {EEXIST, FS_ERROR_EXISTS},
        {ENOTEMPTY, FS_ERROR_EXISTS},
        {ENOTDIR, FS_ERROR_NOT_DIRECTORY},
        {EISDIR, FS_ERROR_IS_DIRECTORY},
        {ENOSPC, FS_ERROR_NO_SPACE},
        {EDQUOT, FS_ERROR_NO_SPACE},
        {EIO, FS_ERROR_IO},
        {EXDEV, FS_ERROR_CROSS_DEVICE},
        {ELOOP, FS_ERROR_LOOP},
        {ENAMETOOLONG, FS_ERROR_NAME_TOO_LONG},
        {EMFILE, FS_ERROR_TOO_MANY_OPEN},
        {ENFILE, FS_ERROR_TOO_MANY_OPEN},
        {EINVAL, FS_ERROR_FAILED},
    };

    for (gsize i = 0; i < G_N_ELEMENTS(cases); i++) {
        g_assert_cmpint(fs_error_code_from_errno(cases[i].errnum), ==, cases[i].code);
    }
}

static void test_set_errno(void) {
    g_autoptr(GError) error = NULL;

    g_assert_false(fs_set_error_from_errno(&error, ENOENT, "open", "/missing"));
    g_assert_error(error, FS_ERROR, FS_ERROR_NOT_FOUND);
    g_assert_nonnull(strstr(error->message, "open: /missing:"));
}

static void test_propagate_gio(void) {
    g_autoptr(GError) error = NULL;

    fs_propagate_gio_error(
        &error, g_error_new_literal(G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "original message"));
    g_assert_error(error, FS_ERROR, FS_ERROR_NOT_SUPPORTED);
    g_assert_cmpstr(error->message, ==, "original message");
}

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/core/error/errno-mapping", test_errno_mapping);
    g_test_add_func("/core/error/set-errno", test_set_errno);
    g_test_add_func("/core/error/propagate-gio", test_propagate_gio);
    return g_test_run();
}
