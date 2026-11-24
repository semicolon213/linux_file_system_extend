#include "nav/fs_nav.h"

#include "core/fs_error.h"
#include "core/fs_path.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <glib/gstdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define FS_NAV_HISTORY_LIMIT 100

typedef struct {
    char *path;
    gboolean show_hidden;
    gboolean dirs_only;
} FsNavListRequest;

typedef struct {
    char *input;
    gboolean show_hidden;
    guint max_results;
} FsNavCompleteRequest;

struct FsNavHistory {
    GPtrArray *paths;
    guint current;
};

struct FsNavBookmarks {
    char **paths;
    gsize length;
};

static FsFileType fs_nav_mode_type(mode_t mode) {
    if (S_ISREG(mode)) {
        return FS_FILE_REGULAR;
    }
    if (S_ISDIR(mode)) {
        return FS_FILE_DIRECTORY;
    }
    if (S_ISLNK(mode)) {
        return FS_FILE_SYMLINK;
    }
    return FS_FILE_OTHER;
}

static gboolean fs_nav_set_cancelled(GError **error) {
    g_set_error_literal(error, FS_ERROR, FS_ERROR_CANCELLED, "operation cancelled");
    return FALSE;
}

static int fs_nav_fstat(int fd, struct stat *stat_buffer) {
    int result;

    do {
        result = fstat(fd, stat_buffer);
    } while (result < 0 && errno == EINTR);
    return result;
}

static int fs_nav_fstatat(int fd, const char *name, struct stat *stat_buffer, int flags) {
    int result;

    do {
        result = fstatat(fd, name, stat_buffer, flags);
    } while (result < 0 && errno == EINTR);
    return result;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): mirrors the public contract.
static GPtrArray *fs_nav_list_dir(const char *path, gboolean show_hidden, gboolean dirs_only,
                                  GCancellable *cancellable, GError **error) {
    g_autoptr(GPtrArray) entries = g_ptr_array_new_with_free_func((GDestroyNotify)fs_entry_free);
    struct stat parent_stat;
    DIR *directory;
    int fd;

    if (!fs_path_validate_absolute(path, error)) {
        return NULL;
    }
    if (cancellable != NULL && g_cancellable_is_cancelled(cancellable)) {
        fs_nav_set_cancelled(error);
        return NULL;
    }
    do {
        fd = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    } while (fd < 0 && errno == EINTR);
    if (fd < 0) {
        int saved_errno = errno;

        fs_set_error_from_errno(error, saved_errno, "open directory", path);
        return NULL;
    }
    if (fs_nav_fstat(fd, &parent_stat) != 0) {
        int saved_errno = errno;

        close(fd);
        fs_set_error_from_errno(error, saved_errno, "stat directory", path);
        return NULL;
    }
    directory = fdopendir(fd);
    if (directory == NULL) {
        int saved_errno = errno;

        close(fd);
        fs_set_error_from_errno(error, saved_errno, "read directory", path);
        return NULL;
    }

    for (;;) {
        struct dirent *item;
        struct stat item_stat;
        struct stat target_stat;
        FsEntry *entry;

        if (cancellable != NULL && g_cancellable_is_cancelled(cancellable)) {
            closedir(directory);
            fs_nav_set_cancelled(error);
            return NULL;
        }
        errno = 0;
        item = readdir(directory);
        if (item == NULL) {
            if (errno == EINTR) {
                continue;
            }
            if (errno != 0) {
                int saved_errno = errno;

                closedir(directory);
                fs_set_error_from_errno(error, saved_errno, "read directory", path);
                return NULL;
            }
            break;
        }
        if (g_str_equal(item->d_name, ".") || g_str_equal(item->d_name, "..") ||
            (!show_hidden && item->d_name[0] == '.')) {
            continue;
        }
        entry = g_new0(FsEntry, 1);
        entry->name = g_strdup(item->d_name);
        entry->type = FS_FILE_OTHER;
        entry->target_type = FS_FILE_OTHER;
        entry->is_hidden = item->d_name[0] == '.';
        if (fs_nav_fstatat(fd, item->d_name, &item_stat, AT_SYMLINK_NOFOLLOW) == 0) {
            entry->type = fs_nav_mode_type(item_stat.st_mode);
            entry->target_type = entry->type;
            entry->is_mount_point = item_stat.st_dev != parent_stat.st_dev;
            entry->size = (guint64)item_stat.st_size;
            entry->mode = (guint32)item_stat.st_mode;
            entry->uid = (guint32)item_stat.st_uid;
            entry->gid = (guint32)item_stat.st_gid;
            entry->dev = (guint64)item_stat.st_dev;
            entry->ino = (guint64)item_stat.st_ino;
            entry->mtime_ns = (gint64)item_stat.st_mtim.tv_sec * G_GINT64_CONSTANT(1000000000) +
                              item_stat.st_mtim.tv_nsec;
            if (entry->type == FS_FILE_SYMLINK) {
                if (fs_nav_fstatat(fd, item->d_name, &target_stat, 0) == 0) {
                    entry->target_type = fs_nav_mode_type(target_stat.st_mode);
                } else if (errno == ENOENT || errno == ENOTDIR) {
                    entry->is_broken_link = TRUE;
                    entry->target_type = FS_FILE_OTHER;
                }
            }
        }
        if (dirs_only && entry->type != FS_FILE_DIRECTORY &&
            !(entry->type == FS_FILE_SYMLINK && entry->target_type == FS_FILE_DIRECTORY)) {
            fs_entry_free(entry);
            continue;
        }
        g_ptr_array_add(entries, entry);
    }
    closedir(directory);
    GPtrArray *result = entries;

    entries = NULL;
    return result;
}

static void fs_nav_list_request_free(FsNavListRequest *request) {
    g_free(request->path);
    g_free(request);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): GTask fixes this callback signature.
static void fs_nav_list_worker(GTask *task, gpointer source_object, gpointer task_data,
                               GCancellable *cancellable) {
    FsNavListRequest *request = task_data;
    g_autoptr(GError) error = NULL;
    GPtrArray *entries;

    (void)source_object;
    entries = fs_nav_list_dir(request->path, request->show_hidden, request->dirs_only, cancellable,
                              &error);
    if (entries == NULL) {
        GError *task_error = error;

        error = NULL;
        g_task_return_error(task, task_error);
    } else {
        g_task_return_pointer(task, entries, (GDestroyNotify)g_ptr_array_unref);
    }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): public contract fixes the order.
void fs_nav_list_dir_async(const char *path, gboolean show_hidden, gboolean dirs_only,
                           GCancellable *cancellable, GAsyncReadyCallback callback,
                           gpointer user_data) {
    g_autoptr(GTask) task = g_task_new(NULL, cancellable, callback, user_data);
    FsNavListRequest *request = g_new0(FsNavListRequest, 1);

    request->path = g_strdup(path);
    request->show_hidden = show_hidden;
    request->dirs_only = dirs_only;
    g_task_set_task_data(task, request, (GDestroyNotify)fs_nav_list_request_free);
    g_task_run_in_thread(task, fs_nav_list_worker);
}

static gpointer fs_nav_propagate_pointer(GAsyncResult *result, GError **error) {
    g_autoptr(GError) local_error = NULL;
    gpointer value = g_task_propagate_pointer(G_TASK(result), &local_error);

    if (local_error != NULL) {
        GError *propagated = local_error;

        local_error = NULL;
        if (propagated->domain == FS_ERROR) {
            g_propagate_error(error, propagated);
        } else {
            fs_propagate_gio_error(error, propagated);
        }
    }
    return value;
}

GPtrArray *fs_nav_list_dir_finish(GAsyncResult *result, GError **error) {
    g_return_val_if_fail(G_IS_TASK(result), NULL);
    return fs_nav_propagate_pointer(result, error);
}

FsNavHistory *fs_nav_history_new(const char *initial_path) {
    FsNavHistory *history = g_new0(FsNavHistory, 1);

    history->paths = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(history->paths, g_strdup(initial_path));
    return history;
}

void fs_nav_history_free(FsNavHistory *history) {
    if (history == NULL) {
        return;
    }
    g_ptr_array_unref(history->paths);
    g_free(history);
}

const char *fs_nav_history_current(const FsNavHistory *history) {
    g_return_val_if_fail(history != NULL, NULL);
    return g_ptr_array_index(history->paths, history->current);
}

void fs_nav_history_push(FsNavHistory *history, const char *path) {
    g_return_if_fail(history != NULL);
    g_return_if_fail(path != NULL);
    if (g_str_equal(fs_nav_history_current(history), path)) {
        return;
    }
    while (history->paths->len > history->current + 1) {
        g_ptr_array_remove_index(history->paths, history->paths->len - 1);
    }
    g_ptr_array_add(history->paths, g_strdup(path));
    if (history->paths->len > FS_NAV_HISTORY_LIMIT) {
        g_ptr_array_remove_index(history->paths, 0);
    }
    history->current = history->paths->len - 1;
}

gboolean fs_nav_history_can_back(const FsNavHistory *history) {
    return history != NULL && history->current > 0;
}

gboolean fs_nav_history_can_forward(const FsNavHistory *history) {
    return history != NULL && history->current + 1 < history->paths->len;
}

const char *fs_nav_history_back(FsNavHistory *history) {
    if (!fs_nav_history_can_back(history)) {
        return NULL;
    }
    history->current--;
    return fs_nav_history_current(history);
}

const char *fs_nav_history_forward(FsNavHistory *history) {
    if (!fs_nav_history_can_forward(history)) {
        return NULL;
    }
    history->current++;
    return fs_nav_history_current(history);
}

char *fs_nav_parent(const char *path) {
    g_autofree char *canonical = fs_path_canonicalize(path);

    if (canonical == NULL || g_str_equal(canonical, "/")) {
        return NULL;
    }
    return g_path_get_dirname(canonical);
}

char **fs_nav_breadcrumb(const char *path) {
    g_autofree char *canonical = fs_path_canonicalize(path);
    g_autoptr(GPtrArray) result = g_ptr_array_new_with_free_func(g_free);

    if (canonical == NULL || canonical[0] != '/') {
        return NULL;
    }
    g_ptr_array_add(result, g_strdup("/"));
    for (char *slash = canonical + 1; *slash != '\0'; slash++) {
        if (*slash == '/') {
            g_ptr_array_add(result, g_strndup(canonical, (gsize)(slash - canonical)));
        }
    }
    if (!g_str_equal(canonical, "/")) {
        g_ptr_array_add(result, g_strdup(canonical));
    }
    g_ptr_array_add(result, NULL);
    GPtrArray *result_array = result;

    result = NULL;
    return (char **)g_ptr_array_free(result_array, FALSE);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): GCompareFunc fixes the signature.
static gint fs_nav_compare_strings(gconstpointer left, gconstpointer right) {
    const char *const *left_string = (const char *const *)left;
    const char *const *right_string = (const char *const *)right;

    return strcmp(*left_string, *right_string);
}

static void fs_nav_complete_request_free(FsNavCompleteRequest *request) {
    g_free(request->input);
    g_free(request);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): GTask fixes this callback signature.
static void fs_nav_complete_worker(GTask *task, gpointer source_object, gpointer task_data,
                                   GCancellable *cancellable) {
    FsNavCompleteRequest *request = task_data;
    g_autoptr(GError) error = NULL;
    g_autofree char *directory = NULL;
    g_autofree char *prefix = NULL;
    g_autoptr(GPtrArray) entries = NULL;
    g_autoptr(GPtrArray) matches = g_ptr_array_new_with_free_func(g_free);
    const char *separator;

    (void)source_object;
    if (!fs_path_validate_absolute(request->input, &error)) {
        GError *task_error = error;

        error = NULL;
        g_task_return_error(task, task_error);
        return;
    }
    separator = strrchr(request->input, '/');
    directory = separator == request->input
                    ? g_strdup("/")
                    : g_strndup(request->input, (gsize)(separator - request->input));
    prefix = g_strdup(separator + 1);
    entries = fs_nav_list_dir(directory, request->show_hidden, TRUE, cancellable, &error);
    if (entries == NULL) {
        GError *task_error = error;

        error = NULL;
        g_task_return_error(task, task_error);
        return;
    }
    for (guint i = 0; i < entries->len; i++) {
        const FsEntry *entry = g_ptr_array_index(entries, i);

        if (g_str_has_prefix(entry->name, prefix)) {
            g_ptr_array_add(matches, fs_path_join(directory, entry->name));
        }
    }
    g_ptr_array_sort(matches, fs_nav_compare_strings);
    if (matches->len > request->max_results) {
        g_ptr_array_set_size(matches, (gint)request->max_results);
    }
    g_ptr_array_add(matches, NULL);
    GPtrArray *matches_array = matches;

    matches = NULL;
    g_task_return_pointer(task, (gpointer)g_ptr_array_free(matches_array, FALSE),
                          (GDestroyNotify)g_strfreev);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): public contract fixes the order.
void fs_nav_complete_async(const char *input, gboolean show_hidden, guint max_results,
                           GCancellable *cancellable, GAsyncReadyCallback callback,
                           gpointer user_data) {
    g_autoptr(GTask) task = g_task_new(NULL, cancellable, callback, user_data);
    FsNavCompleteRequest *request = g_new0(FsNavCompleteRequest, 1);

    request->input = g_strdup(input);
    request->show_hidden = show_hidden;
    request->max_results = max_results;
    g_task_set_task_data(task, request, (GDestroyNotify)fs_nav_complete_request_free);
    g_task_run_in_thread(task, fs_nav_complete_worker);
}

char **fs_nav_complete_finish(GAsyncResult *result, GError **error) {
    g_return_val_if_fail(G_IS_TASK(result), NULL);
    return (char **)fs_nav_propagate_pointer(result, error);
}

const char *fs_nav_home(void) { return g_get_home_dir(); }

static char *fs_nav_bookmarks_path(void) {
    return g_build_filename(g_get_user_config_dir(), "filescope", "bookmarks.ini", NULL);
}

FsNavBookmarks *fs_nav_bookmarks_load(GError **error) {
    g_autofree char *path = fs_nav_bookmarks_path();
    g_autoptr(GKeyFile) key_file = g_key_file_new();
    g_autoptr(GError) local_error = NULL;
    FsNavBookmarks *bookmarks = g_new0(FsNavBookmarks, 1);

    if (!g_key_file_load_from_file(key_file, path, G_KEY_FILE_NONE, &local_error)) {
        if (g_error_matches(local_error, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
            bookmarks->paths = g_new0(char *, 1);
            return bookmarks;
        }
        GError *load_error = local_error;

        local_error = NULL;
        fs_propagate_gio_error(error, load_error);
        g_free(bookmarks);
        return NULL;
    }
    bookmarks->paths = g_key_file_get_string_list(key_file, "Bookmarks", "paths",
                                                  &bookmarks->length, &local_error);
    if (bookmarks->paths == NULL) {
        GError *load_error = local_error;

        local_error = NULL;
        fs_propagate_gio_error(error, load_error);
        g_free(bookmarks);
        return NULL;
    }
    for (gsize i = 0; i < bookmarks->length; i++) {
        if (!fs_path_validate_absolute(bookmarks->paths[i], error)) {
            fs_nav_bookmarks_free(bookmarks);
            return NULL;
        }
    }
    return bookmarks;
}

void fs_nav_bookmarks_free(FsNavBookmarks *bookmarks) {
    if (bookmarks == NULL) {
        return;
    }
    g_strfreev(bookmarks->paths);
    g_free(bookmarks);
}

const char *const *fs_nav_bookmarks_list(const FsNavBookmarks *bookmarks) {
    g_return_val_if_fail(bookmarks != NULL, NULL);
    return (const char *const *)bookmarks->paths;
}

gboolean fs_nav_bookmarks_add(FsNavBookmarks *bookmarks, const char *path, GError **error) {
    g_return_val_if_fail(bookmarks != NULL, FALSE);
    if (!fs_path_validate_absolute(path, error)) {
        return FALSE;
    }
    for (gsize i = 0; i < bookmarks->length; i++) {
        if (g_str_equal(bookmarks->paths[i], path)) {
            g_set_error_literal(error, FS_ERROR, FS_ERROR_EXISTS, "bookmark already exists");
            return FALSE;
        }
    }
    bookmarks->paths =
        (char **)g_realloc_n((gpointer)bookmarks->paths, bookmarks->length + 2, sizeof(char *));
    bookmarks->paths[bookmarks->length++] = g_strdup(path);
    bookmarks->paths[bookmarks->length] = NULL;
    return TRUE;
}

gboolean fs_nav_bookmarks_remove(FsNavBookmarks *bookmarks, const char *path) {
    g_return_val_if_fail(bookmarks != NULL, FALSE);
    for (gsize i = 0; i < bookmarks->length; i++) {
        if (g_str_equal(bookmarks->paths[i], path)) {
            g_free(bookmarks->paths[i]);
            for (gsize j = i; j < bookmarks->length; j++) {
                bookmarks->paths[j] = bookmarks->paths[j + 1];
            }
            bookmarks->length--;
            return TRUE;
        }
    }
    return FALSE;
}

gboolean fs_nav_bookmarks_save(FsNavBookmarks *bookmarks, GError **error) {
    g_autofree char *path = fs_nav_bookmarks_path();
    g_autofree char *directory = g_path_get_dirname(path);
    g_autoptr(GKeyFile) key_file = g_key_file_new();
    g_autofree char *contents = NULL;
    g_autoptr(GError) local_error = NULL;
    gsize length;

    g_return_val_if_fail(bookmarks != NULL, FALSE);
    if (g_mkdir_with_parents(directory, 0700) != 0) {
        int saved_errno = errno;

        return fs_set_error_from_errno(error, saved_errno, "create config directory", directory);
    }
    g_key_file_set_string_list(key_file, "Bookmarks", "paths",
                               (const char *const *)bookmarks->paths, bookmarks->length);
    contents = g_key_file_to_data(key_file, &length, NULL);
    if (!g_file_set_contents(path, contents, (gssize)length, &local_error)) {
        GError *save_error = local_error;

        local_error = NULL;
        fs_propagate_gio_error(error, save_error);
        return FALSE;
    }
    return TRUE;
}
