#ifndef FILESCOPE_NAV_FS_NAV_H
#define FILESCOPE_NAV_FS_NAV_H

#include "core/fs_types.h"

#include <gio/gio.h>

G_BEGIN_DECLS

void fs_nav_list_dir_async(const char *path, gboolean show_hidden, gboolean dirs_only,
                           GCancellable *cancellable, GAsyncReadyCallback callback,
                           gpointer user_data);
GPtrArray *fs_nav_list_dir_finish(GAsyncResult *result, GError **error);

typedef struct FsNavHistory FsNavHistory;

FsNavHistory *fs_nav_history_new(const char *initial_path);
void fs_nav_history_free(FsNavHistory *history);
const char *fs_nav_history_current(const FsNavHistory *history);
void fs_nav_history_push(FsNavHistory *history, const char *path);
gboolean fs_nav_history_can_back(const FsNavHistory *history);
gboolean fs_nav_history_can_forward(const FsNavHistory *history);
const char *fs_nav_history_back(FsNavHistory *history);
const char *fs_nav_history_forward(FsNavHistory *history);
char *fs_nav_parent(const char *path);

char **fs_nav_breadcrumb(const char *path);

void fs_nav_complete_async(const char *input, gboolean show_hidden, guint max_results,
                           GCancellable *cancellable, GAsyncReadyCallback callback,
                           gpointer user_data);
char **fs_nav_complete_finish(GAsyncResult *result, GError **error);

const char *fs_nav_home(void);

typedef struct FsNavBookmarks FsNavBookmarks;

FsNavBookmarks *fs_nav_bookmarks_load(GError **error);
void fs_nav_bookmarks_free(FsNavBookmarks *bookmarks);
const char *const *fs_nav_bookmarks_list(const FsNavBookmarks *bookmarks);
gboolean fs_nav_bookmarks_add(FsNavBookmarks *bookmarks, const char *path, GError **error);
gboolean fs_nav_bookmarks_remove(FsNavBookmarks *bookmarks, const char *path);
gboolean fs_nav_bookmarks_save(FsNavBookmarks *bookmarks, GError **error);

G_END_DECLS

#endif
