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
