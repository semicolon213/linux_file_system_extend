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

FsEvent *fs_event_ref(FsEvent *event);
void fs_event_unref(FsEvent *event);
const char *fs_event_kind_to_string(FsEventKind kind);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FsEvent, fs_event_unref)

G_END_DECLS

#endif
