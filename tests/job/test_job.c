#include "core/fs_error.h"
#include "job/fs_job.h"

// NOLINTBEGIN(bugprone-reserved-identifier): sanitizer runtime API name is reserved.
const char *__tsan_default_suppressions(void);

const char *__tsan_default_suppressions(void) {
    return "race:g_queue_pop_tail\n"
           "race:g_rw_lock_reader_lock\n"
           "race:g_rw_lock_writer_lock\n";
}
// NOLINTEND(bugprone-reserved-identifier)

typedef struct {
    GMainLoop *loop;
    GArray *states;
    guint progress_count;
    guint64 last_bytes;
    gint64 last_total;
    guint idle_count;
    guint freed;
    FsJobQueue *submit_on_free;
    gboolean submit_rejected;
    gint last_error_code;
} Fixture;

typedef struct {
    Fixture *fixture;
    guint marker;
    guint wait_ms;
    gboolean cancellable;
    gboolean partial;
} Work;

static gboolean rejected_work(FsJob *job, gpointer data, GError **error) {
    (void)job;
    (void)data;
    (void)error;
    return TRUE;
}

static void work_free(gpointer data) {
    Work *work = data;
    work->fixture->freed++;
    if (work->fixture->submit_on_free != NULL) {
        g_autoptr(GError) error = NULL;
        gpointer rejected = g_malloc(1);
        FsJobId id = fs_job_queue_submit(work->fixture->submit_on_free, "rejected", rejected_work,
                                         rejected, g_free, &error);
        work->fixture->submit_rejected =
            id == 0 && g_error_matches(error, FS_ERROR, FS_ERROR_CANCELLED);
        g_free(rejected);
        work->fixture->submit_on_free = NULL;
    }
    g_free(work);
}

static gboolean run_work(FsJob *job, gpointer data, GError **error) {
    Work *work = data;
    for (guint i = 0; i < work->wait_ms; i++) {
        fs_job_report_progress(job, i + 1, i + 1, "/tmp/item");
        if (work->marker != 0 && i == 0) {
            fs_job_add_item_result(job, "/tmp/completed", NULL, FALSE);
        }
        if (work->cancellable && g_cancellable_is_cancelled(fs_job_get_cancellable(job))) {
            g_set_error(error, FS_ERROR, FS_ERROR_CANCELLED, "cancelled");
            return FALSE;
        }
        g_usleep(1000);
    }
    if (work->partial) {
        fs_job_add_item_result(job, "/tmp/ok", NULL, FALSE);
        fs_job_add_item_result(job, "/tmp/bad", g_error_new(FS_ERROR, FS_ERROR_IO, "failed"),
                               FALSE);
        fs_job_add_item_result(job, "/tmp/ok2", NULL, FALSE);
    }
    return TRUE;
}

static gboolean run_progress_storm(FsJob *job, gpointer data, GError **error) {
    (void)data;
    (void)error;
    fs_job_report_total(job, -1, -1);
    for (guint i = 0; i < 10000; i++) {
        fs_job_report_progress(job, i + 1, i + 1, "/tmp/item");
        if (i % 100 == 0) {
            g_usleep(1000);
        }
    }
    return TRUE;
}

static void state_cb(FsJobQueue *queue, FsJobId id, FsJobState state, const GError *error,
                     gpointer user_data) {
    (void)queue;
    (void)error;
    Fixture *fixture = user_data;
    guint64 event = id * 10 + (guint64)state;
    g_array_append_val(fixture->states, event);
    if (error != NULL) {
        fixture->last_error_code = error->code;
    }
    if (state >= FS_JOB_CANCELLED && fixture->loop != NULL) {
        g_main_loop_quit(fixture->loop);
    }
}

static void progress_cb(FsJobQueue *queue, FsJobId id, const FsJobProgress *progress,
                        gpointer user_data) {
    (void)queue;
    (void)id;
    Fixture *fixture = user_data;
    fixture->progress_count++;
    fixture->last_bytes = progress->bytes_done;
    fixture->last_total = progress->bytes_total;
}

static gboolean idle_cb(gpointer data) {
    Fixture *fixture = data;
    fixture->idle_count++;
    return G_SOURCE_CONTINUE;
}

static void fixture_init(Fixture *fixture) {
    fixture->states = g_array_new(FALSE, FALSE, sizeof(guint64));
}

static void fixture_clear(Fixture *fixture) {
    g_clear_pointer(&fixture->loop, g_main_loop_unref);
    g_array_unref(fixture->states);
}

static Work *work_new(Fixture *fixture, guint wait_ms) {
    Work *work = g_new0(Work, 1);
    work->fixture = fixture;
    work->wait_ms = wait_ms;
    return work;
}

static void test_fifo_and_nonblocking(void) {
    Fixture fixture = {0};
    fixture_init(&fixture);
    g_autoptr(FsJobQueue) queue = fs_job_queue_new(state_cb, progress_cb, &fixture);
    g_assert_nonnull(queue);
    g_assert_cmpuint(
        fs_job_queue_submit(queue, "A", run_work, work_new(&fixture, 30), work_free, NULL), ==, 1);
    g_assert_cmpuint(
        fs_job_queue_submit(queue, "B", run_work, work_new(&fixture, 1), work_free, NULL), ==, 2);
    guint idle_id = g_idle_add(idle_cb, &fixture);
    fixture.loop = g_main_loop_new(NULL, FALSE);
    while (fixture.states->len < 6) {
        g_main_loop_run(fixture.loop);
    }
    g_source_remove(idle_id);

    const guint64 expected[] = {10, 20, 11, 15, 21, 25};
    g_assert_cmpuint(fixture.states->len, ==, G_N_ELEMENTS(expected));
    for (guint i = 0; i < G_N_ELEMENTS(expected); i++) {
        g_assert_cmpuint(g_array_index(fixture.states, guint64, i), ==, expected[i]);
    }
    g_assert_cmpuint(fixture.idle_count, >, 0);
    g_assert_cmpuint(fixture.freed, ==, 2);
    fixture_clear(&fixture);
}

static void test_progress_throttled(void) {
    Fixture fixture = {0};
    fixture_init(&fixture);
    g_autoptr(FsJobQueue) queue = fs_job_queue_new(state_cb, progress_cb, &fixture);
    fixture.loop = g_main_loop_new(NULL, FALSE);
    FsJobId id = fs_job_queue_submit(queue, "progress", run_progress_storm, NULL, NULL, NULL);
    g_assert_cmpuint(id, !=, 0);
    g_main_loop_run(fixture.loop);
    g_assert_cmpuint(fixture.progress_count, <=, 3);
    g_assert_cmpuint(fixture.last_bytes, ==, 10000);
    g_assert_cmpint(fixture.last_total, ==, -1);
    fixture_clear(&fixture);
}

static void test_cancel_queued(void) {
    Fixture fixture = {0};
    fixture_init(&fixture);
    g_autoptr(FsJobQueue) queue = fs_job_queue_new(state_cb, NULL, &fixture);
    Work *first = work_new(&fixture, 100);
    first->cancellable = TRUE;
    first->marker = 1;
    FsJobId first_id = fs_job_queue_submit(queue, "first", run_work, first, work_free, NULL);
    while (fs_job_queue_get_state(queue, first_id) == FS_JOB_QUEUED) {
        g_usleep(100);
    }
    g_usleep(2000);
    FsJobId second_id =
        fs_job_queue_submit(queue, "second", run_work, work_new(&fixture, 1), work_free, NULL);
    g_assert_true(fs_job_queue_cancel(queue, second_id));
    g_assert_cmpint(fs_job_queue_get_state(queue, second_id), ==, FS_JOB_CANCELLED);
    g_assert_cmpuint(fixture.freed, ==, 1);
    g_assert_cmpint(fixture.last_error_code, ==, FS_ERROR_CANCELLED);
    g_assert_true(fs_job_queue_cancel(queue, first_id));
    fixture.loop = g_main_loop_new(NULL, FALSE);
    while (fs_job_queue_get_state(queue, first_id) < FS_JOB_CANCELLED) {
        g_main_context_iteration(NULL, TRUE);
    }
    while (fixture.freed < 2) {
        g_main_context_iteration(NULL, TRUE);
    }
    const GPtrArray *results = fs_job_queue_get_results(queue, first_id);
    g_assert_nonnull(results);
    g_assert_cmpuint(results->len, ==, 1);
    fixture_clear(&fixture);
}

static void test_partial_results(void) {
    Fixture fixture = {0};
    fixture_init(&fixture);
    g_autoptr(FsJobQueue) queue = fs_job_queue_new(state_cb, NULL, &fixture);
    Work *work = work_new(&fixture, 0);
    work->partial = TRUE;
    fixture.loop = g_main_loop_new(NULL, FALSE);
    FsJobId id = fs_job_queue_submit(queue, "partial", run_work, work, work_free, NULL);
    g_main_loop_run(fixture.loop);
    g_assert_cmpint(fs_job_queue_get_state(queue, id), ==, FS_JOB_FAILED);
    g_assert_cmpint(fixture.last_error_code, ==, FS_ERROR_PARTIAL);
    const GPtrArray *results = fs_job_queue_get_results(queue, id);
    g_assert_nonnull(results);
    g_assert_cmpuint(results->len, ==, 3);
    const FsJobItemResult *failed = g_ptr_array_index((GPtrArray *)results, 1);
    g_assert_error(failed->error, FS_ERROR, FS_ERROR_IO);
    fixture_clear(&fixture);
}

static void test_free_and_submit_during_free(void) {
    Fixture fixture = {0};
    fixture_init(&fixture);
    FsJobQueue *queue = fs_job_queue_new(state_cb, NULL, &fixture);
    Work *running = work_new(&fixture, 100);
    running->cancellable = TRUE;
    fixture.submit_on_free = queue;
    fs_job_queue_submit(queue, "running", run_work, running, work_free, NULL);
    fs_job_queue_submit(queue, "waiting", run_work, work_new(&fixture, 1), work_free, NULL);
    fs_job_queue_free(queue);
    g_assert_cmpuint(fixture.freed, ==, 2);
    g_assert_true(fixture.submit_rejected);
    while (g_main_context_pending(NULL)) {
        g_main_context_iteration(NULL, FALSE);
    }
    g_assert_cmpuint(fixture.states->len, ==, 2);
    fixture_clear(&fixture);
}

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/job/queue/fifo-nonblocking", test_fifo_and_nonblocking);
    g_test_add_func("/job/progress/throttled", test_progress_throttled);
    g_test_add_func("/job/cancel/queued-running", test_cancel_queued);
    g_test_add_func("/job/results/partial", test_partial_results);
    g_test_add_func("/job/shutdown/free", test_free_and_submit_during_free);
    return g_test_run();
}
