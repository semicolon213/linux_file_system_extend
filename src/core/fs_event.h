#ifndef FILESCOPE_CORE_FS_EVENT_H
#define FILESCOPE_CORE_FS_EVENT_H

#include <glib.h>

#include "core/fs_types.h"

G_BEGIN_DECLS

typedef enum {
    FS_EVENT_CREATE,
    FS_EVENT_DELETE,
    FS_EVENT_MODIFY,
    FS_EVENT_CLOSE_WRITE,
    FS_EVENT_ATTRIB,
    FS_EVENT_MOVED_OUT,
    FS_EVENT_MOVED_IN,
    FS_EVENT_RENAME,
    FS_EVENT_MOVE,
    FS_EVENT_DELETE_SELF,
    FS_EVENT_MOVE_SELF,
    FS_EVENT_UNMOUNT,
    FS_EVENT_OVERFLOW,
    FS_EVENT_RESYNC_DONE,
} FsEventKind;

typedef enum {
    FS_EVENT_FLAG_NONE = 0,
    FS_EVENT_FLAG_SYNTHETIC = 1 << 0,
} FsEventFlags;

typedef enum { FS_ORIGIN_UNKNOWN = 0 } FsOriginHint;

typedef struct FsEvent FsEvent;

/* All functions are [any]. ref returns the same event; unref accepts NULL. */
FsEvent *fs_event_ref(FsEvent *event);
void fs_event_unref(FsEvent *event);
/* Returns a static event-kind name. */
const char *fs_event_kind_to_string(FsEventKind kind);
/* Getters borrow event; returned strings remain valid for the event lifetime. */
guint64 fs_event_get_seq(const FsEvent *event);
gint64 fs_event_get_observed_at_us(const FsEvent *event);
gint64 fs_event_get_monotonic_ns(const FsEvent *event);
gint64 fs_event_get_last_monotonic_ns(const FsEvent *event);
guint32 fs_event_get_raw_mask(const FsEvent *event);
FsEventKind fs_event_get_kind(const FsEvent *event);
FsEventFlags fs_event_get_flags(const FsEvent *event);
FsWatchId fs_event_get_watch_id(const FsEvent *event);
const char *fs_event_get_path(const FsEvent *event);
const char *fs_event_get_old_path(const FsEvent *event);
guint32 fs_event_get_cookie(const FsEvent *event);
gboolean fs_event_is_dir(const FsEvent *event);
FsOriginHint fs_event_get_origin_hint(const FsEvent *event);
guint64 fs_event_get_gap_id(const FsEvent *event);
guint32 fs_event_get_merged_count(const FsEvent *event);
/* Returns stat validity; nullable outputs are zeroed when the stat is unknown. */
gboolean fs_event_get_stat(const FsEvent *event, guint64 *size, gint64 *mtime_ns, guint32 *mode);
/* Return whether the value is known; nullable output is zeroed when unknown. */
gboolean fs_event_get_size_delta(const FsEvent *event, gint64 *delta);
gboolean fs_event_get_root_total(const FsEvent *event, guint64 *total);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FsEvent, fs_event_unref)

G_END_DECLS

#endif
