#include "nav/fs_nav.h"

#include "core/fs_error.h"
#include "test_support.h"

#include <errno.h>
#include <fcntl.h>
#include <glib/gstdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    GMainLoop *loop;
    GAsyncResult *result;
} AsyncResult;

static void async_result_ready(GObject *source, GAsyncResult *result, gpointer user_data) {
    AsyncResult *async = user_data;

    (void)source;
    async->result = g_object_ref(result);
    g_main_loop_quit(async->loop);
}

static GPtrArray *list_dir(const char *path, gboolean show_hidden, gboolean dirs_only,
                           GCancellable *cancellable, GError **error) {
    g_autoptr(GMainLoop) loop = g_main_loop_new(NULL, FALSE);
    AsyncResult async = {.loop = loop};
    GPtrArray *entries;

    fs_nav_list_dir_async(path, show_hidden, dirs_only, cancellable, async_result_ready, &async);
    g_main_loop_run(loop);
    entries = fs_nav_list_dir_finish(async.result, error);
    g_object_unref(async.result);
    return entries;
}

static char **complete(const char *input, gboolean show_hidden, guint max_results, GError **error) {
    g_autoptr(GMainLoop) loop = g_main_loop_new(NULL, FALSE);
    AsyncResult async = {.loop = loop};
    char **matches;

    fs_nav_complete_async(input, show_hidden, max_results, NULL, async_result_ready, &async);
    g_main_loop_run(loop);
    matches = fs_nav_complete_finish(async.result, error);
    g_object_unref(async.result);
    return matches;
}

static FsEntry *find_entry(GPtrArray *entries, const char *name) {
    for (guint i = 0; i < entries->len; i++) {
        FsEntry *entry = g_ptr_array_index(entries, i);

        if (g_str_equal(entry->name, name)) {
            return entry;
        }
    }
    return NULL;
}

static void write_fixture(const char *path) {
    g_autoptr(GError) error = NULL;

    g_assert_true(g_file_set_contents(path, "x", 1, &error));
    g_assert_no_error(error);
}

static void test_list_dir(void) {
    g_autoptr(GError) error = NULL;
    g_autoptr(FsTestEnvironment) environment = fs_test_environment_new(&error);
    const char *root;
    g_autofree char *visible = NULL;
    g_autofree char *second = NULL;
    g_autofree char *hidden = NULL;
    g_autofree char *directory = NULL;
    g_autofree char *link = NULL;
    g_autofree char *broken = NULL;
    g_autofree char *fixture = NULL;
    const char invalid_name[] = {'b', 'a', 'd', (char)0xff, '\0'};
    g_autofree char *invalid = NULL;
    g_autoptr(GPtrArray) entries = NULL;
    FsEntry *entry;

    g_assert_no_error(error);
    root = fs_test_environment_get_root(environment);
    fixture = g_build_filename(root, "fixture", NULL);
    g_assert_cmpint(g_mkdir(fixture, 0700), ==, 0);
    visible = g_build_filename(fixture, "a.txt", NULL);
    second = g_build_filename(fixture, "b.txt", NULL);
    hidden = g_build_filename(fixture, ".hidden", NULL);
    directory = g_build_filename(fixture, "dir", NULL);
    link = g_build_filename(fixture, "dir-link", NULL);
    broken = g_build_filename(fixture, "broken", NULL);
    invalid = g_build_filename(fixture, invalid_name, NULL);
    write_fixture(visible);
    write_fixture(second);
    write_fixture(hidden);
    g_assert_cmpint(g_mkdir(directory, 0700), ==, 0);
    g_assert_cmpint(symlink("dir", link), ==, 0);
    g_assert_cmpint(symlink("missing", broken), ==, 0);
    int invalid_fd = g_open(invalid, O_CREAT | O_WRONLY | O_EXCL, 0600);

    g_assert_cmpint(invalid_fd, >=, 0);
    g_assert_cmpint(close(invalid_fd), ==, 0);

    entries = list_dir(fixture, FALSE, FALSE, NULL, &error);
    g_assert_no_error(error);
    g_assert_nonnull(entries);
    g_assert_cmpuint(entries->len, ==, 6);
    g_assert_null(find_entry(entries, "."));
    g_assert_null(find_entry(entries, ".."));
    g_assert_null(find_entry(entries, ".hidden"));
    entry = find_entry(entries, "dir-link");
    g_assert_nonnull(entry);
    g_assert_cmpint(entry->type, ==, FS_FILE_SYMLINK);
    g_assert_cmpint(entry->target_type, ==, FS_FILE_DIRECTORY);
    g_assert_false(entry->is_broken_link);
    entry = find_entry(entries, "broken");
    g_assert_nonnull(entry);
    g_assert_cmpint(entry->type, ==, FS_FILE_SYMLINK);
    g_assert_true(entry->is_broken_link);
    entry = find_entry(entries, invalid_name);
    g_assert_nonnull(entry);
    g_assert_cmpmem(entry->name, sizeof(invalid_name), invalid_name, sizeof(invalid_name));

    g_clear_pointer(&entries, g_ptr_array_unref);
    entries = list_dir(fixture, TRUE, FALSE, NULL, &error);
    g_assert_no_error(error);
    g_assert_cmpuint(entries->len, ==, 7);
    g_assert_true(find_entry(entries, ".hidden")->is_hidden);

    g_clear_pointer(&entries, g_ptr_array_unref);
    entries = list_dir(fixture, TRUE, TRUE, NULL, &error);
    g_assert_no_error(error);
    g_assert_cmpuint(entries->len, ==, 2);
    g_assert_nonnull(find_entry(entries, "dir"));
    g_assert_nonnull(find_entry(entries, "dir-link"));
}

static void test_list_dir_cancelled(void) {
    g_autoptr(GError) error = NULL;
    g_autoptr(FsTestEnvironment) environment = fs_test_environment_new(&error);
    g_autoptr(GCancellable) cancellable = g_cancellable_new();
    g_autoptr(GPtrArray) entries = NULL;

    g_assert_no_error(error);
    g_cancellable_cancel(cancellable);
    entries =
        list_dir(fs_test_environment_get_root(environment), FALSE, FALSE, cancellable, &error);
    g_assert_null(entries);
    g_assert_error(error, FS_ERROR, FS_ERROR_CANCELLED);
    g_clear_error(&error);
    entries = list_dir("relative", FALSE, FALSE, NULL, &error);
    g_assert_null(entries);
    g_assert_error(error, FS_ERROR, FS_ERROR_INVALID_PATH);
}

static void test_list_dir_permission(void) {
    g_autoptr(GError) error = NULL;
    g_autoptr(FsTestEnvironment) environment = fs_test_environment_new(&error);
    g_autofree char *directory = NULL;
    g_autoptr(GPtrArray) entries = NULL;

    g_assert_no_error(error);
    if (geteuid() == 0) {
        g_test_skip("permission behavior is not meaningful as root");
        return;
    }
    directory = g_build_filename(fs_test_environment_get_root(environment), "denied", NULL);
    g_assert_cmpint(g_mkdir(directory, 0700), ==, 0);
    g_assert_cmpint(g_chmod(directory, 0000), ==, 0);
    entries = list_dir(directory, FALSE, FALSE, NULL, &error);
    g_assert_null(entries);
    g_assert_error(error, FS_ERROR, FS_ERROR_PERMISSION_DENIED);
    g_assert_cmpint(g_chmod(directory, 0700), ==, 0);
}

static void test_list_dir_stress(void) {
    g_autoptr(GError) error = NULL;
    g_autoptr(FsTestEnvironment) environment = fs_test_environment_new(&error);
    const char *root;
    g_autofree char *fixture = NULL;
    gint64 started;
    g_autoptr(GPtrArray) entries = NULL;

    g_assert_no_error(error);
    root = fs_test_environment_get_root(environment);
    fixture = g_build_filename(root, "fixture", NULL);
    g_assert_cmpint(g_mkdir(fixture, 0700), ==, 0);
    for (guint i = 0; i < 10000; i++) {
        g_autofree char *name = g_strdup_printf("entry-%05u", i);
        g_autofree char *path = g_build_filename(fixture, name, NULL);
        int fd = g_open(path, O_CREAT | O_WRONLY | O_EXCL, 0600);

        g_assert_cmpint(fd, >=, 0);
        g_assert_cmpint(close(fd), ==, 0);
    }
    started = g_get_monotonic_time();
    entries = list_dir(fixture, FALSE, FALSE, NULL, &error);
    g_test_message("listed 10000 entries in %.3f seconds",
                   (double)(g_get_monotonic_time() - started) / 1000000.0);
    g_assert_no_error(error);
    g_assert_cmpuint(entries->len, ==, 10000);
}

static void test_history_and_parent(void) {
    FsNavHistory *history = fs_nav_history_new("/A");
    g_autofree char *parent = NULL;

    fs_nav_history_push(history, "/B");
    fs_nav_history_push(history, "/C");
    g_assert_cmpstr(fs_nav_history_back(history), ==, "/B");
    g_assert_cmpstr(fs_nav_history_back(history), ==, "/A");
    g_assert_null(fs_nav_history_back(history));
    g_assert_cmpstr(fs_nav_history_forward(history), ==, "/B");
    fs_nav_history_push(history, "/D");
    g_assert_false(fs_nav_history_can_forward(history));
    fs_nav_history_push(history, "/D");
    g_assert_cmpstr(fs_nav_history_current(history), ==, "/D");
    fs_nav_history_free(history);

    history = fs_nav_history_new("/0");
    for (guint i = 1; i <= 100; i++) {
        g_autofree char *path = g_strdup_printf("/%u", i);

        fs_nav_history_push(history, path);
    }
    for (guint i = 0; i < 99; i++) {
        g_assert_nonnull(fs_nav_history_back(history));
    }
    g_assert_cmpstr(fs_nav_history_current(history), ==, "/1");
    g_assert_null(fs_nav_history_back(history));
    fs_nav_history_free(history);

    parent = fs_nav_parent("/home/user/");
    g_assert_cmpstr(parent, ==, "/home");
    g_assert_null(fs_nav_parent("/"));
}

static void test_breadcrumb(void) {
    g_auto(GStrv) crumbs = fs_nav_breadcrumb("/home/u/p");
    const char invalid_path[] = {'/', 'x', '/', (char)0xff, '\0'};
    g_auto(GStrv) invalid_crumbs = fs_nav_breadcrumb(invalid_path);

    g_assert_cmpuint(g_strv_length(crumbs), ==, 4);
    g_assert_cmpstr(crumbs[0], ==, "/");
    g_assert_cmpstr(crumbs[1], ==, "/home");
    g_assert_cmpstr(crumbs[2], ==, "/home/u");
    g_assert_cmpstr(crumbs[3], ==, "/home/u/p");
    g_assert_cmpuint(g_strv_length(invalid_crumbs), ==, 3);
    g_assert_cmpmem(invalid_crumbs[2], 4, invalid_path, 4);
}

static void test_complete(void) {
    g_autoptr(GError) error = NULL;
    g_autoptr(FsTestEnvironment) environment = fs_test_environment_new(&error);
    const char *root;
    g_autofree char *proj = NULL;
    g_autofree char *prof = NULL;
    g_autofree char *file = NULL;
    g_autofree char *input = NULL;
    g_auto(GStrv) matches = NULL;

    g_assert_no_error(error);
    root = fs_test_environment_get_root(environment);
    proj = g_build_filename(root, "proj", NULL);
    prof = g_build_filename(root, "prof", NULL);
    file = g_build_filename(root, "pro.txt", NULL);
    input = g_build_filename(root, "pro", NULL);
    g_assert_cmpint(g_mkdir(proj, 0700), ==, 0);
    g_assert_cmpint(g_mkdir(prof, 0700), ==, 0);
    write_fixture(file);

    matches = complete(input, FALSE, 10, &error);
    g_assert_no_error(error);
    g_assert_cmpuint(g_strv_length(matches), ==, 2);
    g_assert_cmpstr(matches[0], ==, prof);
    g_assert_cmpstr(matches[1], ==, proj);
    g_clear_pointer(&matches, g_strfreev);
    matches = complete(input, FALSE, 1, &error);
    g_assert_no_error(error);
    g_assert_cmpuint(g_strv_length(matches), ==, 1);
    g_assert_cmpstr(matches[0], ==, prof);
    g_clear_pointer(&matches, g_strfreev);
    matches = complete("relative", FALSE, 1, &error);
    g_assert_null(matches);
    g_assert_error(error, FS_ERROR, FS_ERROR_INVALID_PATH);
}

static void test_home_subprocess(void) {
    if (g_test_subprocess()) {
        g_assert_cmpstr(fs_nav_home(), ==, g_getenv("HOME"));
        return;
    }
    g_setenv("HOME", "/tmp/filescope-home-test", TRUE);
    g_test_trap_subprocess(NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
    g_test_trap_assert_passed();
}

static void test_bookmarks(void) {
    g_autoptr(GError) error = NULL;
    g_autoptr(FsTestEnvironment) environment = fs_test_environment_new(&error);
    FsNavBookmarks *bookmarks;
    const char *const *paths;
    g_autofree char *config_path = NULL;
    g_autofree char *contents = NULL;

    g_assert_no_error(error);
    bookmarks = fs_nav_bookmarks_load(&error);
    g_assert_no_error(error);
    g_assert_nonnull(bookmarks);
    g_assert_true(fs_nav_bookmarks_add(bookmarks, "/one", &error));
    g_assert_true(fs_nav_bookmarks_add(bookmarks, "/two", &error));
    g_assert_false(fs_nav_bookmarks_add(bookmarks, "/one", &error));
    g_assert_error(error, FS_ERROR, FS_ERROR_EXISTS);
    g_clear_error(&error);
    g_assert_false(fs_nav_bookmarks_add(bookmarks, "relative", &error));
    g_assert_error(error, FS_ERROR, FS_ERROR_INVALID_PATH);
    g_clear_error(&error);
    g_assert_true(fs_nav_bookmarks_save(bookmarks, &error));
    g_assert_no_error(error);
    fs_nav_bookmarks_free(bookmarks);

    bookmarks = fs_nav_bookmarks_load(&error);
    g_assert_no_error(error);
    paths = fs_nav_bookmarks_list(bookmarks);
    g_assert_cmpstr(paths[0], ==, "/one");
    g_assert_cmpstr(paths[1], ==, "/two");
    g_assert_null(paths[2]);
    g_assert_true(fs_nav_bookmarks_remove(bookmarks, "/one"));
    g_assert_false(fs_nav_bookmarks_remove(bookmarks, "/missing"));
    fs_nav_bookmarks_free(bookmarks);

    config_path = g_build_filename(g_getenv("XDG_CONFIG_HOME"), "filescope", "bookmarks.ini", NULL);
    g_assert_true(g_file_set_contents(config_path, "broken [", -1, &error));
    g_assert_no_error(error);
    bookmarks = fs_nav_bookmarks_load(&error);
    g_assert_null(bookmarks);
    g_assert_error(error, FS_ERROR, FS_ERROR_FAILED);
    g_clear_error(&error);
    g_assert_true(g_file_get_contents(config_path, &contents, NULL, &error));
    g_assert_no_error(error);
    g_assert_cmpstr(contents, ==, "broken [");
}

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/nav/list-dir", test_list_dir);
    g_test_add_func("/nav/list-dir-cancelled", test_list_dir_cancelled);
    g_test_add_func("/nav/list-dir-permission", test_list_dir_permission);
    g_test_add_func("/nav/list-dir-stress", test_list_dir_stress);
    g_test_add_func("/nav/history-parent", test_history_and_parent);
    g_test_add_func("/nav/breadcrumb", test_breadcrumb);
    g_test_add_func("/nav/complete", test_complete);
    g_test_add_func("/nav/home", test_home_subprocess);
    g_test_add_func("/nav/bookmarks", test_bookmarks);
    return g_test_run();
}
