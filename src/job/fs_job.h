#ifndef FILESCOPE_JOB_FS_JOB_H
#define FILESCOPE_JOB_FS_JOB_H

#include <gio/gio.h>

G_BEGIN_DECLS

typedef struct FsJobQueue FsJobQueue;
typedef struct FsJob FsJob;
typedef guint64 FsJobId;

typedef enum {
    FS_JOB_QUEUED,
    FS_JOB_RUNNING,
    FS_JOB_PAUSED,
    FS_JOB_CANCELLED,
    FS_JOB_FAILED,
    FS_JOB_DONE,
} FsJobState;

typedef struct {
    guint64 bytes_done;
    gint64 bytes_total;
    guint items_done;
    gint items_total;
    const char *current_path;
} FsJobProgress;

typedef struct {
    char *path;
    GError *error;
    gboolean skipped;
} FsJobItemResult;

typedef gboolean (*FsJobFunc)(FsJob *job, gpointer data, GError **error);
typedef void (*FsJobStateFunc)(FsJobQueue *queue, FsJobId id, FsJobState state, const GError *error,
                               gpointer user_data);
typedef void (*FsJobProgressFunc)(FsJobQueue *queue, FsJobId id, const FsJobProgress *progress,
                                  gpointer user_data);

/* [main] Creates a FIFO queue whose callbacks run on the default main context. */
FsJobQueue *fs_job_queue_new(FsJobStateFunc state_cb, FsJobProgressFunc progress_cb,
                             gpointer user_data);
/* [main] Cancels work, joins the worker, and guarantees no callbacks after return. */
void fs_job_queue_free(FsJobQueue *queue);

/* [main] Transfers data on success; failure returns 0 and leaves data with the caller. */
FsJobId fs_job_queue_submit(FsJobQueue *queue, const char *title, FsJobFunc func, gpointer data,
                            GDestroyNotify data_free, GError **error);
/* [main] Requests cancellation; a queued job becomes CANCELLED immediately. */
gboolean fs_job_queue_cancel(FsJobQueue *queue, FsJobId id);
/* [main] Returns FS_JOB_FAILED for an unknown id. */
FsJobState fs_job_queue_get_state(FsJobQueue *queue, FsJobId id);
/* [main] Returns borrowed item results for a terminal job, or NULL. */
const GPtrArray *fs_job_queue_get_results(FsJobQueue *queue, FsJobId id);
/* [main] Releases a terminal job record. */
void fs_job_queue_forget(FsJobQueue *queue, FsJobId id);

/* [worker] The returned cancellable is borrowed. */
GCancellable *fs_job_get_cancellable(FsJob *job);
/* [worker] Replaces the latest totals for the next throttled progress callback. */
void fs_job_report_total(FsJob *job, gint64 bytes_total, gint items_total);
/* [worker] Replaces the latest progress; current_path is copied. */
void fs_job_report_progress(FsJob *job, guint64 bytes_done, guint items_done,
                            const char *current_path);
/* [worker] Copies path and consumes error, which may be NULL. */
void fs_job_add_item_result(FsJob *job, const char *path, GError *error, gboolean skipped);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FsJobQueue, fs_job_queue_free)

G_END_DECLS

#endif
