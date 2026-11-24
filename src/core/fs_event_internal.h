#ifndef FILESCOPE_CORE_FS_EVENT_INTERNAL_H
#define FILESCOPE_CORE_FS_EVENT_INTERNAL_H

#include "core/fs_event.h"

G_BEGIN_DECLS

struct FsEvent {
    gatomicrefcount ref_count;
    guint64 seq;
    gint64 observed_at_us;
    gint64 monotonic_ns;
    gint64 last_monotonic_ns;
    guint32 raw_mask;
    FsEventKind kind;
    FsEventFlags flags;
    FsWatchId watch_id;
    char *path;
    char *old_path;
    guint32 cookie;
    gboolean is_dir;
    FsOriginHint origin_hint;
    guint64 gap_id;
    guint32 merged_count;
    gboolean stat_valid;
    guint64 size;
    gint64 mtime_ns;
    guint32 mode;
};

FsEvent *fs_event_new_internal(const FsEvent *source);

G_END_DECLS

#endif
