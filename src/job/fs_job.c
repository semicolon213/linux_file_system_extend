#include "job/fs_job.h"

#include "core/fs_error.h"

#define FS_JOB_PROGRESS_INTERVAL_MS 100

// NOLINTBEGIN(bugprone-reserved-identifier): sanitizer runtime API names are reserved.
extern void __tsan_acquire(void *address) __attribute__((weak));
extern void __tsan_release(void *address) __attribute__((weak));
extern void __tsan_ignore_thread_begin(void) __attribute__((weak));
extern void __tsan_ignore_thread_end(void) __attribute__((weak));
// NOLINTEND(bugprone-reserved-identifier)

static void job_mutex_lock(GMutex *mutex) {
    g_mutex_lock(mutex);
    if (__tsan_acquire != NULL) {
        __tsan_acquire(mutex);
    }
}

static void job_mutex_unlock(GMutex *mutex) {
    if (__tsan_release != NULL) {
        __tsan_release(mutex);
    }
    g_mutex_unlock(mutex);
}

#define g_mutex_lock job_mutex_lock
#define g_mutex_unlock job_mutex_unlock

struct FsJobQueue {
    gint ref_count;
    GMutex lock;
    GHashTable *jobs;
    GThreadPool *pool;
    FsJobId next_id;
    gboolean shutting_down;
    FsJobStateFunc state_cb;
    FsJobProgressFunc progress_cb;
    gpointer user_data;
};

struct FsJob {
    gint ref_count;
    FsJobQueue *queue;
    FsJobId id;
    char *title;
    FsJobFunc func;
    gpointer data;
    GDestroyNotify data_free;
    GCancellable *cancellable;
    FsJobState state;
    GError *terminal_error;
    GMutex progress_lock;
    FsJobProgress progress;
    char *current_path;
    GPtrArray *results;
    gboolean progress_dirty;
    gboolean progress_pending;
    gboolean terminal;
};

typedef struct {
    FsJobQueue *queue;
    FsJobId id;
    FsJobState state;
} StateMessage;

typedef struct {
    FsJob *job;
    gboolean final;
} ProgressMessage;

typedef struct {
    GDestroyNotify notify;
    gpointer data;
} DataCleanup;

static FsJobQueue *queue_ref(FsJobQueue *queue) {
    g_atomic_int_inc(&queue->ref_count);
    return queue;
}

static void queue_unref(FsJobQueue *queue) {
    if (!g_atomic_int_dec_and_test(&queue->ref_count)) {
        return;
    }
    g_hash_table_unref(queue->jobs);
    g_mutex_clear(&queue->lock);
    g_free(queue);
}

static FsJob *job_ref(FsJob *job) {
    g_atomic_int_inc(&job->ref_count);
    return job;
}

static void item_result_free(gpointer data) {
    FsJobItemResult *result = data;
    g_free(result->path);
    g_clear_error(&result->error);
    g_free(result);
}

static void job_unref(gpointer data) {
    FsJob *job = data;
    if (!g_atomic_int_dec_and_test(&job->ref_count)) {
        return;
    }
    if (job->data_free != NULL) {
        job->data_free(job->data);
    }
    g_clear_object(&job->cancellable);
    g_clear_error(&job->terminal_error);
    g_ptr_array_unref(job->results);
    g_free(job->current_path);
    g_free(job->title);
    g_mutex_clear(&job->progress_lock);
    queue_unref(job->queue);
    g_free(job);
}

static FsJob *lookup_job(FsJobQueue *queue, FsJobId id) {
    return g_hash_table_lookup(queue->jobs, &id);
}

static void attach_idle_source(GSourceFunc callback, gpointer data, GDestroyNotify destroy) {
    if (__tsan_release != NULL) {
        __tsan_release(data);
        __tsan_ignore_thread_begin();
    }
    GSource *source = g_idle_source_new();
    g_source_set_callback(source, callback, data, destroy);
    g_source_attach(source, g_main_context_default());
    g_source_unref(source);
    if (__tsan_ignore_thread_end != NULL) {
        __tsan_ignore_thread_end();
    }
}

static gboolean state_message_dispatch(gpointer data) {
    if (__tsan_acquire != NULL) {
        __tsan_acquire(data);
    }
    StateMessage *message = data;
    FsJobQueue *queue = message->queue;
    const GError *error = NULL;

    g_mutex_lock(&queue->lock);
    FsJob *job = lookup_job(queue, message->id);
    gboolean deliver = !queue->shutting_down && job != NULL;
    if (deliver && message->state >= FS_JOB_CANCELLED) {
        error = job->terminal_error;
        if (job->data_free != NULL) {
            GDestroyNotify data_free = job->data_free;
            gpointer job_data = job->data;
            job->data_free = NULL;
            job->data = NULL;
            g_mutex_unlock(&queue->lock);
            data_free(job_data);
            g_mutex_lock(&queue->lock);
            job = lookup_job(queue, message->id);
            deliver = !queue->shutting_down && job != NULL;
            error = deliver ? job->terminal_error : NULL;
        }
    }
    FsJob *callback_job = deliver ? job_ref(job) : NULL;
    FsJobStateFunc callback = deliver ? queue->state_cb : NULL;
    gpointer user_data = queue->user_data;
    g_mutex_unlock(&queue->lock);

    if (callback != NULL) {
        callback(queue, message->id, message->state, error, user_data);
    }
    if (callback_job != NULL) {
        job_unref(callback_job);
    }
    return G_SOURCE_REMOVE;
}

static void state_message_free(gpointer data) {
    StateMessage *message = data;
    queue_unref(message->queue);
    g_free(message);
}

static void post_state(FsJob *job, FsJobState state) {
    StateMessage *message = g_new0(StateMessage, 1);
    message->queue = queue_ref(job->queue);
    message->id = job->id;
    message->state = state;
    attach_idle_source(state_message_dispatch, message, state_message_free);
}

static gboolean progress_message_dispatch(gpointer data) {
    if (__tsan_acquire != NULL) {
        __tsan_acquire(data);
    }
    ProgressMessage *message = data;
    FsJob *job = message->job;
    FsJobQueue *queue = job->queue;
    FsJobProgress progress;
    g_autofree char *path = NULL;

    g_mutex_lock(&job->progress_lock);
    job->progress_pending = FALSE;
    job->progress_dirty = FALSE;
    progress = job->progress;
    path = g_strdup(job->current_path);
    progress.current_path = path;
    g_mutex_unlock(&job->progress_lock);

    g_mutex_lock(&queue->lock);
    gboolean deliver = !queue->shutting_down && lookup_job(queue, job->id) == job &&
                       (message->final || !job->terminal);
    FsJobProgressFunc callback = deliver ? queue->progress_cb : NULL;
    gpointer user_data = queue->user_data;
    g_mutex_unlock(&queue->lock);
    if (callback != NULL) {
        callback(queue, job->id, &progress, user_data);
    }
    return G_SOURCE_REMOVE;
}

static void progress_message_free(gpointer data) {
    ProgressMessage *message = data;
    job_unref(message->job);
    g_free(message);
}

static void schedule_progress(FsJob *job, gboolean final) {
    g_mutex_lock(&job->progress_lock);
    if ((!job->progress_dirty && !final) || (job->progress_pending && !final)) {
        g_mutex_unlock(&job->progress_lock);
        return;
    }
    job->progress_pending = TRUE;
    g_mutex_unlock(&job->progress_lock);

    ProgressMessage *message = g_new0(ProgressMessage, 1);
    message->job = job_ref(job);
    message->final = final;
    if (final) {
        attach_idle_source(progress_message_dispatch, message, progress_message_free);
    } else {
        if (__tsan_release != NULL) {
            __tsan_release(message);
            __tsan_ignore_thread_begin();
        }
        g_timeout_add_full(G_PRIORITY_DEFAULT, FS_JOB_PROGRESS_INTERVAL_MS,
                           progress_message_dispatch, message, progress_message_free);
        if (__tsan_ignore_thread_end != NULL) {
            __tsan_ignore_thread_end();
        }
    }
}

static gboolean has_failed_item(FsJob *job) {
    gboolean failed = FALSE;
    g_mutex_lock(&job->progress_lock);
    for (guint i = 0; i < job->results->len; i++) {
        const FsJobItemResult *result = g_ptr_array_index(job->results, i);
        if (result->error != NULL && !result->skipped) {
            failed = TRUE;
            break;
        }
    }
    g_mutex_unlock(&job->progress_lock);
    return failed;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): GFunc fixes this signature.
static void worker_run(gpointer data, gpointer user_data) {
    (void)user_data;
    if (__tsan_acquire != NULL) {
        __tsan_acquire(data);
    }
    FsJob *job = data;
    FsJobQueue *queue = job->queue;
    g_autoptr(GError) error = NULL;

    g_mutex_lock(&queue->lock);
    if (queue->shutting_down || job->state != FS_JOB_QUEUED) {
        g_mutex_unlock(&queue->lock);
        job_unref(job);
        return;
    }
    job->state = FS_JOB_RUNNING;
    g_mutex_unlock(&queue->lock);
    post_state(job, FS_JOB_RUNNING);

    gboolean succeeded = job->func(job, job->data, &error);
    gboolean cancelled = g_cancellable_is_cancelled(job->cancellable);
    gboolean partial = succeeded && has_failed_item(job);

    g_mutex_lock(&queue->lock);
    if (cancelled) {
        job->state = FS_JOB_CANCELLED;
        job->terminal_error = g_error_new(FS_ERROR, FS_ERROR_CANCELLED, "Job cancelled");
    } else if (!succeeded || partial) {
        job->state = FS_JOB_FAILED;
        if (partial) {
            job->terminal_error = g_error_new(FS_ERROR, FS_ERROR_PARTIAL, "Job partially failed");
        } else if (error != NULL && error->domain == FS_ERROR) {
            job->terminal_error = error;
            error = NULL;
        } else {
            job->terminal_error = g_error_new(FS_ERROR, FS_ERROR_FAILED, "Job failed");
        }
    } else {
        job->state = FS_JOB_DONE;
    }
    job->terminal = TRUE;
    FsJobState terminal_state = job->state;
    g_mutex_unlock(&queue->lock);

    schedule_progress(job, TRUE);
    post_state(job, terminal_state);
    job_unref(job);
}

FsJobQueue *fs_job_queue_new(FsJobStateFunc state_cb, FsJobProgressFunc progress_cb,
                             gpointer user_data) {
    (void)fs_error_quark();
    FsJobQueue *queue = g_new0(FsJobQueue, 1);
    queue->ref_count = 1;
    g_mutex_init(&queue->lock);
    queue->jobs = g_hash_table_new_full(g_int64_hash, g_int64_equal, g_free, job_unref);
    queue->next_id = 1;
    queue->state_cb = state_cb;
    queue->progress_cb = progress_cb;
    queue->user_data = user_data;

    g_autoptr(GError) error = NULL;
    queue->pool = g_thread_pool_new(worker_run, NULL, 1, TRUE, &error);
    if (queue->pool == NULL) {
        queue_unref(queue);
        return NULL;
    }
    return queue;
}

void fs_job_queue_free(FsJobQueue *queue) {
    if (queue == NULL) {
        return;
    }

    g_mutex_lock(&queue->lock);
    if (queue->shutting_down) {
        g_mutex_unlock(&queue->lock);
        return;
    }
    queue->shutting_down = TRUE;
    GHashTableIter iter;
    gpointer value;
    g_hash_table_iter_init(&iter, queue->jobs);
    while (g_hash_table_iter_next(&iter, NULL, &value)) {
        FsJob *job = value;
        g_cancellable_cancel(job->cancellable);
    }
    g_mutex_unlock(&queue->lock);

    g_thread_pool_free(queue->pool, FALSE, TRUE);
    queue->pool = NULL;

    g_autoptr(GPtrArray) cleanups = g_ptr_array_new_with_free_func(g_free);
    g_mutex_lock(&queue->lock);
    g_hash_table_iter_init(&iter, queue->jobs);
    while (g_hash_table_iter_next(&iter, NULL, &value)) {
        FsJob *job = value;
        if (job->data_free != NULL) {
            DataCleanup *cleanup = g_new(DataCleanup, 1);
            cleanup->notify = job->data_free;
            cleanup->data = job->data;
            job->data_free = NULL;
            job->data = NULL;
            g_ptr_array_add(cleanups, cleanup);
        }
    }
    g_mutex_unlock(&queue->lock);
    for (guint i = 0; i < cleanups->len; i++) {
        DataCleanup *cleanup = g_ptr_array_index(cleanups, i);
        cleanup->notify(cleanup->data);
    }
    g_hash_table_remove_all(queue->jobs);
    queue_unref(queue);
}

FsJobId fs_job_queue_submit(FsJobQueue *queue, const char *title, FsJobFunc func, gpointer data,
                            GDestroyNotify data_free, GError **error) {
    g_return_val_if_fail(queue != NULL, 0);
    g_return_val_if_fail(func != NULL, 0);

    g_mutex_lock(&queue->lock);
    if (queue->shutting_down) {
        g_mutex_unlock(&queue->lock);
        g_set_error(error, FS_ERROR, FS_ERROR_CANCELLED, "Job queue is shutting down");
        return 0;
    }

    FsJob *job = g_new0(FsJob, 1);
    job->ref_count = 1;
    job->queue = queue_ref(queue);
    job->id = queue->next_id++;
    job->title = g_strdup(title);
    job->func = func;
    job->data = data;
    job->data_free = data_free;
    job->cancellable = g_cancellable_new();
    job->state = FS_JOB_QUEUED;
    g_mutex_init(&job->progress_lock);
    job->progress.bytes_total = -1;
    job->progress.items_total = -1;
    job->results = g_ptr_array_new_with_free_func(item_result_free);

    FsJobId *key = g_new(FsJobId, 1);
    *key = job->id;
    g_hash_table_insert(queue->jobs, key, job);
    job_ref(job);
    if (__tsan_release != NULL) {
        __tsan_release(job);
    }
    g_autoptr(GError) push_error = NULL;
    gboolean pushed = g_thread_pool_push(queue->pool, job, &push_error);
    if (!pushed) {
        job->data = NULL;
        job->data_free = NULL;
        g_hash_table_remove(queue->jobs, &job->id);
        job_unref(job);
        g_mutex_unlock(&queue->lock);
        GError *owned_error = push_error;
        push_error = NULL;
        fs_propagate_gio_error(error, owned_error);
        return 0;
    }
    FsJobId id = job->id;
    FsJobStateFunc callback = queue->state_cb;
    gpointer user_data = queue->user_data;
    queue_ref(queue);
    g_mutex_unlock(&queue->lock);

    if (callback != NULL) {
        callback(queue, id, FS_JOB_QUEUED, NULL, user_data);
    }
    queue_unref(queue);
    return id;
}

gboolean fs_job_queue_cancel(FsJobQueue *queue, FsJobId id) {
    g_return_val_if_fail(queue != NULL, FALSE);

    g_mutex_lock(&queue->lock);
    FsJob *job = lookup_job(queue, id);
    if (job == NULL || job->terminal || queue->shutting_down) {
        g_mutex_unlock(&queue->lock);
        return FALSE;
    }
    g_cancellable_cancel(job->cancellable);
    if (job->state == FS_JOB_RUNNING) {
        g_mutex_unlock(&queue->lock);
        return TRUE;
    }

    job->state = FS_JOB_CANCELLED;
    job->terminal = TRUE;
    job->terminal_error = g_error_new(FS_ERROR, FS_ERROR_CANCELLED, "Job cancelled");
    GDestroyNotify data_free = job->data_free;
    gpointer job_data = job->data;
    job->data_free = NULL;
    job->data = NULL;
    FsJobStateFunc callback = queue->state_cb;
    gpointer user_data = queue->user_data;
    const GError *terminal_error = job->terminal_error;
    job_ref(job);
    queue_ref(queue);
    g_mutex_unlock(&queue->lock);

    if (data_free != NULL) {
        data_free(job_data);
    }
    if (callback != NULL) {
        callback(queue, id, FS_JOB_CANCELLED, terminal_error, user_data);
    }
    job_unref(job);
    queue_unref(queue);
    return TRUE;
}

FsJobState fs_job_queue_get_state(FsJobQueue *queue, FsJobId id) {
    g_return_val_if_fail(queue != NULL, FS_JOB_FAILED);
    g_mutex_lock(&queue->lock);
    FsJob *job = lookup_job(queue, id);
    FsJobState state = job != NULL ? job->state : FS_JOB_FAILED;
    g_mutex_unlock(&queue->lock);
    return state;
}

const GPtrArray *fs_job_queue_get_results(FsJobQueue *queue, FsJobId id) {
    g_return_val_if_fail(queue != NULL, NULL);
    g_mutex_lock(&queue->lock);
    FsJob *job = lookup_job(queue, id);
    const GPtrArray *results = job != NULL && job->terminal ? job->results : NULL;
    g_mutex_unlock(&queue->lock);
    return results;
}

void fs_job_queue_forget(FsJobQueue *queue, FsJobId id) {
    g_return_if_fail(queue != NULL);
    g_mutex_lock(&queue->lock);
    FsJob *job = lookup_job(queue, id);
    GDestroyNotify data_free = NULL;
    gpointer job_data = NULL;
    if (job != NULL && job->terminal) {
        data_free = job->data_free;
        job_data = job->data;
        job->data_free = NULL;
        job->data = NULL;
        g_hash_table_remove(queue->jobs, &id);
    }
    g_mutex_unlock(&queue->lock);
    if (data_free != NULL) {
        data_free(job_data);
    }
}

GCancellable *fs_job_get_cancellable(FsJob *job) {
    g_return_val_if_fail(job != NULL, NULL);
    return job->cancellable;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): public contract fixes the order.
void fs_job_report_total(FsJob *job, gint64 bytes_total, gint items_total) {
    g_return_if_fail(job != NULL);
    g_mutex_lock(&job->progress_lock);
    job->progress.bytes_total = bytes_total;
    job->progress.items_total = items_total;
    job->progress_dirty = TRUE;
    g_mutex_unlock(&job->progress_lock);
    schedule_progress(job, FALSE);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): public contract fixes the order.
void fs_job_report_progress(FsJob *job, guint64 bytes_done, guint items_done,
                            const char *current_path) {
    g_return_if_fail(job != NULL);
    g_mutex_lock(&job->progress_lock);
    job->progress.bytes_done = bytes_done;
    job->progress.items_done = items_done;
    g_free(job->current_path);
    job->current_path = g_strdup(current_path);
    job->progress_dirty = TRUE;
    g_mutex_unlock(&job->progress_lock);
    schedule_progress(job, FALSE);
}

void fs_job_add_item_result(FsJob *job, const char *path, GError *error, gboolean skipped) {
    g_return_if_fail(job != NULL);
    FsJobItemResult *result = g_new0(FsJobItemResult, 1);
    result->path = g_strdup(path);
    result->error = error;
    result->skipped = skipped;
    g_mutex_lock(&job->progress_lock);
    g_ptr_array_add(job->results, result);
    g_mutex_unlock(&job->progress_lock);
}
