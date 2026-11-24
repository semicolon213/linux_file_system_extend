#ifndef FILESCOPE_CORE_FS_TYPES_H
#define FILESCOPE_CORE_FS_TYPES_H

#include <glib.h>

G_BEGIN_DECLS

typedef guint32 FsWatchId;

typedef enum {
    FS_FILE_REGULAR,
    FS_FILE_DIRECTORY,
    FS_FILE_SYMLINK,
    FS_FILE_OTHER,
} FsFileType;

typedef struct {
    char *name;
    FsFileType type;
    FsFileType target_type;
    gboolean is_broken_link;
    gboolean is_mount_point;
    gboolean is_hidden;
    guint64 size;
    guint32 mode;
    guint32 uid;
    guint32 gid;
    guint64 dev;
    guint64 ino;
    gint64 mtime_ns;
} FsEntry;

void fs_entry_free(FsEntry *entry);
FsEntry *fs_entry_copy(const FsEntry *entry);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FsEntry, fs_entry_free)

G_END_DECLS

#endif
