#include "core/fs_event_internal.h"

FsEvent *fs_event_new_internal(const FsEvent *source) {
    FsEvent *event;

    g_return_val_if_fail(source != NULL, NULL);

    event = g_memdup2(source, sizeof(*source));
    g_atomic_ref_count_init(&event->ref_count);
    event->path = g_strdup(source->path);
    event->old_path = g_strdup(source->old_path);
    return event;
}

FsEvent *fs_event_ref(FsEvent *event) {
    g_return_val_if_fail(event != NULL, NULL);

    g_atomic_ref_count_inc(&event->ref_count);
    return event;
}

void fs_event_unref(FsEvent *event) {
    if (event == NULL) {
        return;
    }
    if (g_atomic_ref_count_dec(&event->ref_count)) {
        g_free(event->path);
        g_free(event->old_path);
        g_free(event);
    }
}

const char *fs_event_kind_to_string(FsEventKind kind) {
    static const char *const names[] = {
        [FS_EVENT_CREATE] = "CREATE",       [FS_EVENT_DELETE] = "DELETE",
        [FS_EVENT_MODIFY] = "MODIFY",       [FS_EVENT_CLOSE_WRITE] = "CLOSE_WRITE",
        [FS_EVENT_ATTRIB] = "ATTRIB",       [FS_EVENT_MOVED_OUT] = "MOVED_OUT",
        [FS_EVENT_MOVED_IN] = "MOVED_IN",   [FS_EVENT_RENAME] = "RENAME",
        [FS_EVENT_MOVE] = "MOVE",           [FS_EVENT_DELETE_SELF] = "DELETE_SELF",
        [FS_EVENT_MOVE_SELF] = "MOVE_SELF", [FS_EVENT_UNMOUNT] = "UNMOUNT",
        [FS_EVENT_OVERFLOW] = "OVERFLOW",   [FS_EVENT_RESYNC_DONE] = "RESYNC_DONE",
    };

    if ((guint)kind >= G_N_ELEMENTS(names)) {
        return "UNKNOWN";
    }
    return names[kind];
}

#define FS_EVENT_GETTER(type, function, field, default_value)                                      \
    type function(const FsEvent *event) {                                                          \
        g_return_val_if_fail(event != NULL, default_value);                                        \
        return event->field;                                                                       \
    }

FS_EVENT_GETTER(guint64, fs_event_get_seq, seq, 0)
FS_EVENT_GETTER(gint64, fs_event_get_observed_at_us, observed_at_us, 0)
FS_EVENT_GETTER(gint64, fs_event_get_monotonic_ns, monotonic_ns, 0)
FS_EVENT_GETTER(gint64, fs_event_get_last_monotonic_ns, last_monotonic_ns, 0)
FS_EVENT_GETTER(guint32, fs_event_get_raw_mask, raw_mask, 0)
FS_EVENT_GETTER(FsEventKind, fs_event_get_kind, kind, FS_EVENT_CREATE)
FS_EVENT_GETTER(FsEventFlags, fs_event_get_flags, flags, FS_EVENT_FLAG_NONE)
FS_EVENT_GETTER(FsWatchId, fs_event_get_watch_id, watch_id, 0)
FS_EVENT_GETTER(const char *, fs_event_get_path, path, NULL)
FS_EVENT_GETTER(const char *, fs_event_get_old_path, old_path, NULL)
FS_EVENT_GETTER(guint32, fs_event_get_cookie, cookie, 0)
FS_EVENT_GETTER(gboolean, fs_event_is_dir, is_dir, FALSE)
FS_EVENT_GETTER(FsOriginHint, fs_event_get_origin_hint, origin_hint, FS_ORIGIN_UNKNOWN)
FS_EVENT_GETTER(guint64, fs_event_get_gap_id, gap_id, 0)
FS_EVENT_GETTER(guint32, fs_event_get_merged_count, merged_count, 0)

gboolean fs_event_get_stat(const FsEvent *event, guint64 *size, gint64 *mtime_ns, guint32 *mode) {
    g_return_val_if_fail(event != NULL, FALSE);

    if (size != NULL) {
        *size = event->stat_valid ? event->size : 0;
    }
    if (mtime_ns != NULL) {
        *mtime_ns = event->stat_valid ? event->mtime_ns : 0;
    }
    if (mode != NULL) {
        *mode = event->stat_valid ? event->mode : 0;
    }
    return event->stat_valid;
}

gboolean fs_event_get_size_delta(const FsEvent *event, gint64 *delta) {
    g_return_val_if_fail(event != NULL, FALSE);

    if (delta != NULL) {
        *delta = event->size_delta_known ? event->size_delta : 0;
    }
    return event->size_delta_known;
}

gboolean fs_event_get_root_total(const FsEvent *event, guint64 *total) {
    g_return_val_if_fail(event != NULL, FALSE);

    if (total != NULL) {
        *total = event->root_total_known ? event->root_total : 0;
    }
    return event->root_total_known;
}
