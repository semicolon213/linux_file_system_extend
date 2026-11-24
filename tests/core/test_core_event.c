#include <glib.h>

#include "core/fs_event_internal.h"

static gpointer ref_worker(gpointer user_data) {
    FsEvent *event = user_data;

    for (guint i = 0; i < 100000; i++) {
        fs_event_unref(fs_event_ref(event));
    }
    return NULL;
}

static void test_event_copy_and_refcount(void) {
    FsEvent source = {.seq = 1,
                      .kind = FS_EVENT_RENAME,
                      .path = g_strdup("/tmp/new"),
                      .old_path = g_strdup("/tmp/old"),
                      .merged_count = 1};
    g_autoptr(FsEvent) event = fs_event_new_internal(&source);
    GThread *first;
    GThread *second;

    g_free(source.path);
    g_free(source.old_path);
    g_assert_cmpstr(event->path, ==, "/tmp/new");
    g_assert_cmpstr(event->old_path, ==, "/tmp/old");
    g_assert_cmpstr(fs_event_kind_to_string(event->kind), ==, "RENAME");

    first = g_thread_new("event-ref-1", ref_worker, event);
    second = g_thread_new("event-ref-2", ref_worker, event);
    g_thread_join(first);
    g_thread_join(second);
    fs_event_unref(NULL);
}

static void test_kind_strings(void) {
    g_assert_cmpstr(fs_event_kind_to_string(FS_EVENT_CREATE), ==, "CREATE");
    g_assert_cmpstr(fs_event_kind_to_string(FS_EVENT_RESYNC_DONE), ==, "RESYNC_DONE");
}

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/core/event/copy-refcount", test_event_copy_and_refcount);
    g_test_add_func("/core/event/kind-strings", test_kind_strings);
    return g_test_run();
}
