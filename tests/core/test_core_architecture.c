#include <glib.h>
#include <glib/gstdio.h>

#include "test_support.h"

static gboolean run_architecture_check(const char *root) {
    const char *argv[] = {"bash", "scripts/check-architecture.sh", NULL};
    g_autofree char *old_directory = g_get_current_dir();
    g_autoptr(GError) error = NULL;
    gint status = 0;

    g_assert_cmpint(g_chdir(root), ==, 0);
    g_assert_true(g_spawn_sync(NULL, (char **)argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL,
                               NULL, &status, &error));
    g_assert_no_error(error);
    g_assert_cmpint(g_chdir(old_directory), ==, 0);
    return g_spawn_check_wait_status(status, NULL);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): local test helper is unambiguous.
static void write_source(const char *root, const char *include) {
    g_autofree char *source = g_build_filename(root, "src", "core", "sample.c", NULL);
    g_autofree char *contents = g_strdup_printf("#include \"%s\"\n", include);
    g_autoptr(GError) error = NULL;

    g_assert_true(g_file_set_contents(source, contents, -1, &error));
    g_assert_no_error(error);
}

static void test_architecture_rejects_core_dependencies(void) {
    g_autoptr(FsTestEnvironment) environment = NULL;
    g_autoptr(GError) error = NULL;
    g_autofree char *scripts = NULL;
    g_autofree char *core = NULL;
    g_autofree char *source_script = NULL;
    g_autofree char *target_script = NULL;
    g_autofree char *script_contents = NULL;
    gsize script_length = 0;
    const char *root;

    environment = fs_test_environment_new(&error);
    g_assert_no_error(error);
    root = fs_test_environment_get_root(environment);
    scripts = g_build_filename(root, "scripts", NULL);
    core = g_build_filename(root, "src", "core", NULL);
    g_assert_cmpint(g_mkdir_with_parents(scripts, 0700), ==, 0);
    g_assert_cmpint(g_mkdir_with_parents(core, 0700), ==, 0);

    source_script =
        g_build_filename(FILESCOPE_SOURCE_DIR, "scripts", "check-architecture.sh", NULL);
    target_script = g_build_filename(scripts, "check-architecture.sh", NULL);
    g_assert_true(g_file_get_contents(source_script, &script_contents, &script_length, &error));
    g_assert_no_error(error);
    g_assert_true(
        g_file_set_contents(target_script, script_contents, (gssize)script_length, &error));
    g_assert_no_error(error);

    write_source(root, "ui/fs_ui.h");
    g_assert_false(run_architecture_check(root));
    write_source(root, "mon/fs_mon.h");
    g_assert_false(run_architecture_check(root));
    write_source(root, "dash/fs_dash.h");
    g_assert_false(run_architecture_check(root));
    write_source(root, "core/fs_error.h");
    g_assert_true(run_architecture_check(root));
}

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/core/architecture/dependency-check",
                    test_architecture_rejects_core_dependencies);
    return g_test_run();
}
