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
                      .observed_at_us = 2,
                      .monotonic_ns = 3,
                      .last_monotonic_ns = 4,
                      .raw_mask = 5,
                      .kind = FS_EVENT_RENAME,
                      .flags = FS_EVENT_FLAG_SYNTHETIC,
                      .watch_id = 6,
                      .path = g_strdup("/tmp/new"),
                      .old_path = g_strdup("/tmp/old"),
                      .cookie = 7,
                      .is_dir = TRUE,
                      .origin_hint = FS_ORIGIN_UNKNOWN,
                      .gap_id = 8,
                      .merged_count = 9,
                      .stat_valid = TRUE,
                      .size = 10,
                      .mtime_ns = 11,
                      .mode = 12,
                      .size_delta_known = TRUE,
                      .size_delta = -13,
                      .root_total_known = TRUE,
                      .root_total = 14};
    g_autoptr(FsEvent) event = fs_event_new_internal(&source);
    FsEvent unknown_source = {0};
    g_autoptr(FsEvent) unknown = fs_event_new_internal(&unknown_source);
    GThread *first;
    GThread *second;
    guint64 size = 99;
    gint64 mtime_ns = 99;
    guint32 mode = 99;
    gint64 delta = 99;
    guint64 total = 99;

    g_free(source.path);
    g_free(source.old_path);
    g_assert_cmpuint(fs_event_get_seq(event), ==, 1);
    g_assert_cmpint(fs_event_get_observed_at_us(event), ==, 2);
    g_assert_cmpint(fs_event_get_monotonic_ns(event), ==, 3);
    g_assert_cmpint(fs_event_get_last_monotonic_ns(event), ==, 4);
    g_assert_cmpuint(fs_event_get_raw_mask(event), ==, 5);
    g_assert_cmpint(fs_event_get_kind(event), ==, FS_EVENT_RENAME);
    g_assert_cmpint(fs_event_get_flags(event), ==, FS_EVENT_FLAG_SYNTHETIC);
    g_assert_cmpuint(fs_event_get_watch_id(event), ==, 6);
    g_assert_cmpstr(fs_event_get_path(event), ==, "/tmp/new");
    g_assert_cmpstr(fs_event_get_old_path(event), ==, "/tmp/old");
    g_assert_cmpuint(fs_event_get_cookie(event), ==, 7);
    g_assert_true(fs_event_is_dir(event));
    g_assert_cmpint(fs_event_get_origin_hint(event), ==, FS_ORIGIN_UNKNOWN);
    g_assert_cmpuint(fs_event_get_gap_id(event), ==, 8);
    g_assert_cmpuint(fs_event_get_merged_count(event), ==, 9);
    g_assert_true(fs_event_get_stat(event, &size, &mtime_ns, &mode));
    g_assert_cmpuint(size, ==, 10);
    g_assert_cmpint(mtime_ns, ==, 11);
    g_assert_cmpuint(mode, ==, 12);
    g_assert_true(fs_event_get_size_delta(event, &delta));
    g_assert_cmpint(delta, ==, -13);
    g_assert_true(fs_event_get_root_total(event, &total));
    g_assert_cmpuint(total, ==, 14);
    g_assert_false(fs_event_get_stat(unknown, &size, &mtime_ns, &mode));
    g_assert_cmpuint(size, ==, 0);
    g_assert_cmpint(mtime_ns, ==, 0);
    g_assert_cmpuint(mode, ==, 0);
    g_assert_false(fs_event_get_size_delta(unknown, &delta));
    g_assert_cmpint(delta, ==, 0);
    g_assert_false(fs_event_get_root_total(unknown, &total));
    g_assert_cmpuint(total, ==, 0);
    g_assert_cmpstr(fs_event_kind_to_string(fs_event_get_kind(event)), ==, "RENAME");

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
