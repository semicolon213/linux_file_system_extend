#include <glib.h>

#include "core/fs_types.h"

static void test_entry_copy(void) {
    FsEntry source = {.name = g_strdup("entry"),
                      .type = FS_FILE_SYMLINK,
                      .target_type = FS_FILE_DIRECTORY,
                      .size = 42,
                      .ino = 7};
    g_autoptr(FsEntry) copy = fs_entry_copy(&source);

    g_assert_nonnull(copy);
    g_assert_cmpstr(copy->name, ==, source.name);
    g_assert_true(copy->name != source.name);
    g_assert_cmpint(copy->type, ==, FS_FILE_SYMLINK);
    g_assert_cmpuint(copy->size, ==, 42);
    g_free(source.name);
    g_assert_null(fs_entry_copy(NULL));
    fs_entry_free(NULL);
}

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/core/types/entry-copy", test_entry_copy);
    return g_test_run();
}
