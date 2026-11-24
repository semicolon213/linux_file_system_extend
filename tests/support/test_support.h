#ifndef FILESCOPE_TESTS_SUPPORT_TEST_SUPPORT_H
#define FILESCOPE_TESTS_SUPPORT_TEST_SUPPORT_H

#include <glib.h>

G_BEGIN_DECLS

typedef struct FsTestEnvironment FsTestEnvironment;
typedef gboolean (*FsTestConditionFunc)(gpointer user_data);

char *fs_test_make_tmp_dir(GError **error);
gboolean fs_test_remove_tree(const char *path, GError **error);
FsTestEnvironment *fs_test_environment_new(GError **error);
void fs_test_environment_free(FsTestEnvironment *environment);
const char *fs_test_environment_get_root(FsTestEnvironment *environment);
gboolean fs_test_wait_until(FsTestConditionFunc condition, gpointer user_data, guint timeout_ms);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FsTestEnvironment, fs_test_environment_free)

G_END_DECLS

#endif
