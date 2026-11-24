# 공개 인터페이스 (R1)

상태: R1 계약 제안 (ARCH-001). 시그니처를 바꾸려면 Coordinator 승인과 이 문서 갱신이 먼저다.

## 0. 공통 규칙

- 함수 접두사: `fs_<모듈>_` (core는 `fs_`), 타입 접두사: `Fs`, 상수 접두사: `FS_<모듈>_`.
- 오류: 실패할 수 있는 함수는 마지막 인자 `GError **error`를 받고 `gboolean`(또는 NULL/0)으로 실패를 알린다. 도메인은 `FS_ERROR` 하나다(ADR-0002, ERROR_HANDLING.md). `error`는 NULL 허용.
- 경로: `const char *`는 Linux 원본 바이트 경로(NUL 종료, UTF-8 보장 없음)다. 공개 API의 경로 인자는 절대 경로여야 한다(`fs_path_validate_absolute`). 이름 인자(`name`)는 경로 구분자 없는 한 요소다. 화면 표시 문자열은 ui가 `g_filename_display_name()`으로 만든다.
- 소유권 표기: `(transfer none)` 빌려줌, `(transfer full)` 호출자가 해제, `(transfer container)` 배열만 해제. 표기가 없는 입력 포인터는 빌려줌이며 호출 동안만 유효하면 된다.
- 스레드 표기: `[main]` 메인 스레드 전용, `[any]` 어느 스레드든 가능. 공개 콜백은 모두 생성 시 기본 메인 컨텍스트(`g_main_context_default()`)에서 호출된다.
- 콜백 안에서 같은 객체의 free를 호출해도 된다(재진입 안전). 해제 함수가 반환한 뒤에는 그 객체의 콜백이 다시 호출되지 않는다.

## 1. core (`src/core/`)

### fs_error.h
```c
#define FS_ERROR (fs_error_quark())
GQuark fs_error_quark(void);

typedef enum {
    FS_ERROR_FAILED,            /* 분류 불가 (errno는 메시지에) */
    FS_ERROR_NOT_FOUND,         /* ENOENT */
    FS_ERROR_PERMISSION_DENIED, /* EACCES, EPERM, EROFS */
    FS_ERROR_EXISTS,            /* EEXIST, ENOTEMPTY(덮어쓰기 대상) */
    FS_ERROR_NOT_DIRECTORY,     /* ENOTDIR */
    FS_ERROR_IS_DIRECTORY,      /* EISDIR */
    FS_ERROR_NO_SPACE,          /* ENOSPC, EDQUOT (파일 쓰기) */
    FS_ERROR_IO,                /* EIO */
    FS_ERROR_CROSS_DEVICE,      /* EXDEV (fs 내부 폴백 판단용, 사용자에게 거의 노출 안 됨) */
    FS_ERROR_LOOP,              /* ELOOP, O_NOFOLLOW 위반 */
    FS_ERROR_NAME_TOO_LONG,     /* ENAMETOOLONG, NAME_MAX/PATH_MAX 초과 */
    FS_ERROR_INVALID_PATH,      /* 상대 경로, 빈 이름, '/', ".", "..", 자기 안으로 이동 */
    FS_ERROR_PROTECTED_PATH,    /* SEC-003 */
    FS_ERROR_NOT_CONFIRMED,     /* SEC-002: 위험 작업에 확인 플래그 없음 */
    FS_ERROR_NOT_SUPPORTED,     /* 휴지통 미지원, 특수 파일 복사 등 */
    FS_ERROR_CONFLICT,          /* 읽은 뒤 외부에서 바뀜 (FS-005 저장) */
    FS_ERROR_TOO_LARGE,         /* 텍스트 읽기 한도 초과 */
    FS_ERROR_CANCELLED,         /* 사용자 취소 */
    FS_ERROR_WATCH_LIMIT,       /* inotify_add_watch ENOSPC (max_user_watches) */
    FS_ERROR_TOO_MANY_OPEN,     /* EMFILE, ENFILE, inotify_init1 EMFILE */
    FS_ERROR_PARTIAL,           /* 일괄 작업 일부 실패 (항목별 결과 참조) */
} FsErrorCode;

FsErrorCode fs_error_code_from_errno(int errnum);                               /* [any] */
/* errno → 코드 변환 + 메시지 "<op>: <path 표시형>: <strerror>". 항상 FALSE 반환 */
gboolean fs_set_error_from_errno(GError **error, int errnum, const char *op, const char *path); /* [any] */
/* G_IO_ERROR 등 다른 도메인을 FS_ERROR로 변환. src는 소비(transfer full) */
void fs_propagate_gio_error(GError **dest, GError *src);                        /* [any] */
```

### fs_path.h (경로 검증, SEC-001/003)
```c
gboolean fs_path_validate_absolute(const char *path, GError **error);  /* [any] 절대·NUL 없음·길이 ≤ PATH_MAX-1 */
gboolean fs_path_validate_name(const char *name, GError **error);      /* [any] 비지 않음, '/' 없음, "."/".." 아님, ≤ NAME_MAX 바이트 */
char *fs_path_join(const char *dir, const char *name);                 /* [any] (transfer full) g_free */
/* 정규화: 중복 '/', ".", ".." 를 문자열로만 정리(링크는 해석 안 함). (transfer full) */
char *fs_path_canonicalize(const char *path);                          /* [any] */
gboolean fs_path_is_ancestor(const char *ancestor, const char *path);  /* [any] 같거나 하위면 TRUE, 정규화된 입력 기준 */

typedef enum {
    FS_PATH_OP_READ,     /* 탐색·읽기: 보호 경로에서도 허용 */
    FS_PATH_OP_WRITE,    /* 생성·이름 변경·저장·복사 대상 */
    FS_PATH_OP_REMOVE,   /* 삭제·휴지통·이동 원본 */
} FsPathOp;

/* SEC-003. 보호 규칙은 SECURITY.md §2. 위반이면 FS_ERROR_PROTECTED_PATH */
gboolean fs_path_check_protected(const char *path, FsPathOp op, GError **error); /* [any] */
/* SEC-001 사전 점검(UI 버튼 활성화용, 권고). 실제 판정은 작업 syscall 결과가 우선한다 */
gboolean fs_path_check_access(const char *path, FsPathOp op, GError **error);    /* [any] */
```

### fs_types.h
```c
typedef guint32 FsWatchId;   /* 감시 루트 ID, 0은 무효. mon이 발급하고 FsEvent가 참조 */

typedef enum { FS_FILE_REGULAR, FS_FILE_DIRECTORY, FS_FILE_SYMLINK, FS_FILE_OTHER } FsFileType;

typedef struct {
    char *name;            /* 원본 바이트 이름 */
    FsFileType type;       /* lstat 기준 */
    FsFileType target_type;/* 심볼릭 링크면 stat 기준 대상 종류, 끊긴 링크는 FS_FILE_OTHER */
    gboolean is_broken_link;
    gboolean is_mount_point;/* st_dev가 부모와 다름 */
    gboolean is_hidden;    /* 이름이 '.'로 시작 */
    guint64 size;          /* lstat st_size */
    guint32 mode, uid, gid;
    guint64 dev, ino;      /* 영구 ID 아님 */
    gint64 mtime_ns;       /* 1970 기준 ns */
} FsEntry;

void fs_entry_free(FsEntry *entry);   /* [any] NULL 허용 */
FsEntry *fs_entry_copy(const FsEntry *entry); /* [any] (transfer full) */
```

### fs_event.h
FsEvent 구조와 kind는 EVENT_SPEC.md §1~2가 정본이다. 함수:
```c
FsEvent *fs_event_ref(FsEvent *event);   /* [any] 원자적 */
void fs_event_unref(FsEvent *event);     /* [any] 원자적, NULL 허용 */
const char *fs_event_kind_to_string(FsEventKind kind); /* [any] "CREATE" 등, 정적 문자열 */

/* getter. FsEvent는 opaque이며 필드 의미는 EVENT_SPEC §1. 모두 [any], 문자열은 (transfer none)으로
 * 이벤트 수명 동안 유효 */
guint64 fs_event_get_seq(const FsEvent *event);
gint64 fs_event_get_observed_at_us(const FsEvent *event);
gint64 fs_event_get_monotonic_ns(const FsEvent *event);
gint64 fs_event_get_last_monotonic_ns(const FsEvent *event);
guint32 fs_event_get_raw_mask(const FsEvent *event);
FsEventKind fs_event_get_kind(const FsEvent *event);
FsEventFlags fs_event_get_flags(const FsEvent *event);
FsWatchId fs_event_get_watch_id(const FsEvent *event);
const char *fs_event_get_path(const FsEvent *event);      /* NULL 가능 */
const char *fs_event_get_old_path(const FsEvent *event);  /* NULL 가능 */
guint32 fs_event_get_cookie(const FsEvent *event);
gboolean fs_event_is_dir(const FsEvent *event);
FsOriginHint fs_event_get_origin_hint(const FsEvent *event);
guint64 fs_event_get_gap_id(const FsEvent *event);
guint32 fs_event_get_merged_count(const FsEvent *event);
/* stat_valid를 반환. 출력 포인터는 NULL 허용, FALSE면 출력값 0 */
gboolean fs_event_get_stat(const FsEvent *event, guint64 *size, gint64 *mtime_ns, guint32 *mode);
/* 미상이면 FALSE, *delta=0 (EVENT_SPEC §8) */
gboolean fs_event_get_size_delta(const FsEvent *event, gint64 *delta);
/* 미상이면 FALSE, *total=0 */
gboolean fs_event_get_root_total(const FsEvent *event, guint64 *total);
```
FsEvent 생성(`FsEvent *fs_event_new_internal(const FsEvent *source)`: 깊은 복사, refcount=1, source는 borrowed)과 구조체 정의는 mon과 테스트만 쓰는 `core/fs_event_internal.h`에 둔다.

## 2. job (`src/job/fs_job.h`)

```c
typedef struct FsJobQueue FsJobQueue;
typedef struct FsJob FsJob;
typedef guint64 FsJobId;              /* 0은 무효 */

typedef enum {
    FS_JOB_QUEUED, FS_JOB_RUNNING, FS_JOB_PAUSED /* R1 미사용, JOB-006 예약 */,
    FS_JOB_CANCELLED, FS_JOB_FAILED, FS_JOB_DONE,
} FsJobState;

typedef struct {
    guint64 bytes_done;
    gint64  bytes_total;   /* -1 = 미확정 → UI indeterminate */
    guint   items_done;
    gint    items_total;   /* -1 = 미확정 */
    const char *current_path; /* (transfer none) 콜백 동안만 유효 */
} FsJobProgress;

typedef struct {
    char *path;            /* 원본 경로 */
    GError *error;         /* NULL이면 성공, 건너뜀은 FS_ERROR_EXISTS 등 원인 + skipped=TRUE */
    gboolean skipped;
} FsJobItemResult;

/* worker 스레드에서 실행되는 본문. 진행 보고는 fs_job_report_*()로만 한다.
 * 취소는 g_cancellable_is_cancelled(fs_job_get_cancellable(job))로 확인한다.
 * 반환: TRUE 성공, FALSE이면 error 설정 (취소는 FS_ERROR_CANCELLED). */
typedef gboolean (*FsJobFunc)(FsJob *job, gpointer data, GError **error);

typedef void (*FsJobStateFunc)(FsJobQueue *queue, FsJobId id, FsJobState state,
                               const GError *error /* (transfer none) 종료 상태에서만 */,
                               gpointer user_data);
typedef void (*FsJobProgressFunc)(FsJobQueue *queue, FsJobId id, const FsJobProgress *progress,
                                  gpointer user_data);

FsJobQueue *fs_job_queue_new(FsJobStateFunc state_cb, FsJobProgressFunc progress_cb,
                             gpointer user_data);                               /* [main] */
/* 신규 제출 차단 → 실행 중 작업 취소 → worker 합류 → 대기 작업은 CANCELLED 통지 없이 폐기.
 * 반환 후 콜백 없음. NULL 허용 */
void fs_job_queue_free(FsJobQueue *queue);                                      /* [main] */

/* title: UI 표시용 UTF-8 (복사됨). data/data_free: 소유권 이전(transfer full), 작업 종료 후 worker
 * 아닌 메인 스레드에서 data_free 호출. 반환 0이면 error 설정 (종료 중이면 FS_ERROR_CANCELLED) */
FsJobId fs_job_queue_submit(FsJobQueue *queue, const char *title, FsJobFunc func,
                            gpointer data, GDestroyNotify data_free, GError **error); /* [main] */
/* 취소 요청. QUEUED면 즉시 CANCELLED, RUNNING이면 본문이 정리 후 CANCELLED. 이미 끝났으면 FALSE */
gboolean fs_job_queue_cancel(FsJobQueue *queue, FsJobId id);                   /* [main] */
FsJobState fs_job_queue_get_state(FsJobQueue *queue, FsJobId id);               /* [main] 모르는 id → FS_JOB_FAILED */
/* 종료된 작업의 항목별 결과. (transfer none), 다음 메인 루프 반복까지 또는 forget 전까지 유효 */
const GPtrArray *fs_job_queue_get_results(FsJobQueue *queue, FsJobId id);       /* [main] FsJobItemResult* */
void fs_job_queue_forget(FsJobQueue *queue, FsJobId id);                        /* [main] 종료 작업 기록 해제 */

/* FsJobFunc 안에서만 [worker] */
GCancellable *fs_job_get_cancellable(FsJob *job);                               /* (transfer none) */
void fs_job_report_total(FsJob *job, gint64 bytes_total, gint items_total);
void fs_job_report_progress(FsJob *job, guint64 bytes_done, guint items_done, const char *current_path);
/* 항목 결과 기록. error는 transfer full(NULL 허용) */
void fs_job_add_item_result(FsJob *job, const char *path, GError *error, gboolean skipped);
```
- 동시 실행 1개(R1). 제출 순서(FIFO)대로 실행한다. JOB-009/010(우선순위, 동시성 설정)은 P1.
- 진행 콜백은 작업당 최대 10Hz로 묶어 보내고, 종료 직전 마지막 값을 한 번 더 보낸다.
- 상태 콜백 순서는 STATE_MACHINE.md §2의 전이만 따른다.

## 3. fs (`src/fs/fs_ops.h`)

```c
typedef enum {
    FS_OPS_FLAG_NONE      = 0,
    FS_OPS_FLAG_CONFIRMED = 1 << 0, /* SEC-002: 영구 삭제·덮어쓰기에 필수. UI가 사용자 확인 후에만 설정 */
} FsOpsFlags;

typedef enum {
    FS_CONFLICT_FAIL,       /* 기본: 항목을 FS_ERROR_EXISTS로 실패 처리하고 다음 항목 진행 */
    FS_CONFLICT_SKIP,       /* 건너뜀(skipped=TRUE) */
    FS_CONFLICT_KEEP_BOTH,  /* "이름 (2).확장자" 형식의 비어 있는 이름 선택 */
    FS_CONFLICT_OVERWRITE,  /* FS_OPS_FLAG_CONFIRMED 필요. 디렉터리→파일, 파일→디렉터리 덮어쓰기는 금지 */
} FsConflictPolicy;

/* --- 동기 작업: 단일 syscall 수준. [any], R1 ui는 메인 스레드에서 호출 --- */
/* FS-001: O_CREAT|O_EXCL, 모드 0666 & ~umask. 반환 (transfer full) 생성된 절대 경로 */
char *fs_ops_create_file(const char *dir, const char *name, GError **error);
/* FS-002: mkdir 0777 & ~umask */
char *fs_ops_create_dir(const char *dir, const char *name, GError **error);
/* FS-008: renameat2(RENAME_NOREPLACE). 같은 디렉터리 안에서만. 대상 존재 → FS_ERROR_EXISTS */
char *fs_ops_rename(const char *path, const char *new_name, GError **error);
/* FS-006 단일 항목: g_file_trash. 미지원이면 FS_ERROR_NOT_SUPPORTED (영구 삭제로 자동 전환 금지) */
gboolean fs_ops_trash(const char *path, GCancellable *cancellable, GError **error);

/* 충돌 사전 조회: dest_dir 안에 sources의 basename과 같은 이름이 있는 sources 항목.
 * (transfer full) GPtrArray<char*> (free func g_free) */
GPtrArray *fs_ops_find_conflicts(const char *const *sources, const char *dest_dir, GError **error);

/* --- FS-004/005 텍스트 --- */
#define FS_OPS_TEXT_MAX_BYTES (4u * 1024u * 1024u)
typedef struct {
    guint64 dev, ino, size;
    gint64 mtime_ns;
} FsFileStamp;  /* 읽은 시점 식별. 저장 충돌 검사에 쓴다 */

/* 정규 파일만. 크기 > FS_OPS_TEXT_MAX_BYTES → FS_ERROR_TOO_LARGE. 내용은 바이트 그대로
 * (UTF-8 검사는 ui 책임). contents (transfer full) g_free */
gboolean fs_ops_read_text(const char *path, char **contents, gsize *length, FsFileStamp *stamp,
                          GError **error);                                         /* [any] */
/* 같은 디렉터리 임시 파일에 쓰고 fsync 후 rename으로 교체, 원래 모드 유지.
 * 현재 stamp가 expected와 다르면 쓰지 않고 FS_ERROR_CONFLICT. expected=NULL은 CONFIRMED 필요(강제 저장).
 * 성공 시 new_stamp 갱신(NULL 허용) */
gboolean fs_ops_write_text(const char *path, const char *contents, gsize length,
                           const FsFileStamp *expected, FsOpsFlags flags, FsFileStamp *new_stamp,
                           GError **error);                                        /* [any] */

/* --- 비동기 작업: job 큐에 제출. 반환 FsJobId, 0이면 error (검증 실패는 제출 전에 반환) [main] --- */
/* FS-009/012: 재귀 복사. 링크는 링크로 복사, 특수 파일은 항목 실패(NOT_SUPPORTED) */
FsJobId fs_ops_copy(FsJobQueue *queue, const char *const *sources, const char *dest_dir,
                    FsConflictPolicy policy, FsOpsFlags flags, GError **error);
/* FS-010/011/012: 같은 장치는 rename, EXDEV면 항목별 복사 완료 후 원본 삭제 */
FsJobId fs_ops_move(FsJobQueue *queue, const char *const *sources, const char *dest_dir,
                    FsConflictPolicy policy, FsOpsFlags flags, GError **error);
/* FS-006/014: 여러 항목 휴지통. 미지원 항목은 항목 실패로 남긴다 */
FsJobId fs_ops_trash_many(FsJobQueue *queue, const char *const *paths, GError **error);
/* FS-007/014: 영구 삭제. flags에 FS_OPS_FLAG_CONFIRMED 없으면 FS_ERROR_NOT_CONFIRMED */
FsJobId fs_ops_delete(FsJobQueue *queue, const char *const *paths, FsOpsFlags flags,
                      GError **error);
```
- `sources`/`paths`는 NULL 종료 배열이며 제출 시 깊은 복사한다.
- 모든 쓰기 작업은 시작 직전에 `fs_path_validate_absolute`, `fs_path_check_protected`를 다시 수행한다(SRS FS 공통 계약 "작업 전 대상 재검증").
- FS-003(기본 앱 열기)은 ui가 `g_app_info_launch_default_for_uri()`로 처리한다(디스플레이 문맥 필요). FS-013(다중 선택)과 FS-011 잘라내기 상태(앱 내부 클립보드)는 ui 책임이다.

## 4. mon (`src/mon/fs_mon.h`)

```c
typedef struct FsWatcher FsWatcher;
/* FsWatchId는 core/fs_types.h */

typedef enum {
    FS_WATCH_REGISTERING, FS_WATCH_ACTIVE, FS_WATCH_DEGRADED,
    FS_WATCH_RESYNCING, FS_WATCH_STOPPED, FS_WATCH_ERROR,
} FsWatchState;

typedef enum {
    FS_WATCH_REASON_NONE,
    FS_WATCH_REASON_USER,          /* remove_root */
    FS_WATCH_REASON_ROOT_DELETED,  /* 루트 DELETE_SELF */
    FS_WATCH_REASON_ROOT_MOVED,    /* 루트 MOVE_SELF */
    FS_WATCH_REASON_UNMOUNTED,     /* 루트 IN_UNMOUNT */
    FS_WATCH_REASON_OVERFLOW,      /* 커널 IN_Q_OVERFLOW 또는 전달 큐 포화 */
    FS_WATCH_REASON_WATCH_LIMIT,   /* 하위 감시 일부 등록 실패 */
    FS_WATCH_REASON_LOST,          /* 예상 못한 IN_IGNORED */
    FS_WATCH_REASON_IO,            /* 등록·재탐색 실패 */
} FsWatchReason;

typedef enum {
    FS_WATCH_FLAG_NONE = 0,
    FS_WATCH_FLAG_RECURSIVE = 1 << 0,  /* MON-018/019 */
} FsWatchFlags;

typedef struct {
    FsWatchId id;
    const char *path;      /* (transfer none) */
    FsWatchFlags flags;
    FsWatchState state;
    FsWatchReason reason;
    guint watch_count;     /* 이 루트가 쓰는 inotify watch 수 */
    gboolean total_known;  /* REGISTERING, STOPPED, ERROR, 재탐색 중이면 FALSE */
    gboolean total_partial;/* 감시 못 한 하위가 있어 합계가 일부만 (EVENT_SPEC §8) */
    guint64 total_bytes;   /* 루트 총용량(정규 파일 st_size 합) */
    guint64 file_count;    /* 색인된 정규 파일 수 */
} FsWatchInfo;

/* events: (transfer none) FsEvent* 배열, 콜백 동안만 유효. 보관하려면 fs_event_ref */
typedef void (*FsWatchEventsFunc)(FsWatcher *watcher, const GPtrArray *events, gpointer user_data);
/* error: (transfer none) DEGRADED/ERROR/STOPPED(IO)에서 원인, 그 외 NULL */
typedef void (*FsWatchStateFunc)(FsWatcher *watcher, const FsWatchInfo *info, const GError *error,
                                 gpointer user_data);

/* inotify_init1(IN_NONBLOCK|IN_CLOEXEC) 후 watcher 스레드 시작. 실패 → NULL + error */
FsWatcher *fs_mon_watcher_new(FsWatchEventsFunc events_cb, FsWatchStateFunc state_cb,
                              gpointer user_data, GError **error);              /* [main] */
/* 스레드 정지·합류, 모든 watch 해제, 미전달 배치 폐기. 반환 후 콜백 없음. NULL 허용 */
void fs_mon_watcher_free(FsWatcher *watcher);                                    /* [main] */

/* MON-020/021. 즉시 검사: 절대 경로, 존재하는 디렉터리(링크 따라가지 않음), 기존 루트와 겹치지
 * 않음(같거나 조상·자손이면 FS_ERROR_EXISTS). 통과하면 REGISTERING 상태로 ID 반환하고
 * 등록 결과는 state 콜백(ACTIVE/DEGRADED/ERROR)으로 알린다. 실패 → 0 + error */
FsWatchId fs_mon_watcher_add_root(FsWatcher *watcher, const char *path, FsWatchFlags flags,
                                  GError **error);                               /* [main] */
/* 감시 해제. STOPPED(USER) 상태 콜백 후 ID 무효. 모르는 ID면 FALSE */
gboolean fs_mon_watcher_remove_root(FsWatcher *watcher, FsWatchId id);           /* [main] */

/* 현재 루트 목록 (transfer full) GPtrArray<FsWatchInfo*>, 각 항목 path 포함 g_free 한 번으로 해제
 * (free func 설정됨) */
GPtrArray *fs_mon_watcher_list_roots(FsWatcher *watcher);                        /* [main] */
/* 상태 표시줄용 전체 watch 사용 수와 /proc/sys/fs/inotify/max_user_watches (읽기 실패 시 0) */
void fs_mon_watcher_get_usage(FsWatcher *watcher, guint *used, guint *limit);    /* [main] */
/* 경로가 어떤 루트의 감시 범위 안인지 (ui "감시하지 않는 폴더" 띠) */
FsWatchId fs_mon_watcher_find_root(FsWatcher *watcher, const char *path);        /* [main] */
/* 루트 총용량(EVENT_SPEC §8). main 쪽 사본이며 이미 전달된 마지막 이벤트(또는 상태 콜백) 시점 값이다.
 * 모르는 ID이거나 total_known=FALSE면 FALSE. 출력 포인터 NULL 허용 */
gboolean fs_mon_watcher_get_root_total(FsWatcher *watcher, FsWatchId id, guint64 *total_bytes,
                                       guint64 *file_count, gboolean *partial);  /* [main] */
```
- 루트 총용량이 정해지거나 크게 바뀌는 시점(REGISTERING→ACTIVE, RESYNC 완료)은 state 콜백의 FsWatchInfo로도 알린다. 이벤트별 변화는 FsEvent의 size_delta/root_total로만 알린다(별도 콜백 없음).

`src/mon/fs_mon_testing.h` (tests 전용, 라이브러리에 포함):
```c
void fs_mon_testing_inject_overflow(FsWatcher *watcher);          /* [main] IN_Q_OVERFLOW 합성 */
void fs_mon_testing_set_queue_capacity(FsWatcher *watcher, guint capacity); /* [main] 포화 재현 */
void fs_mon_testing_set_move_window_ms(FsWatcher *watcher, guint ms);       /* [main] */
```

## 5. nav (`src/nav/fs_nav.h`)

```c
/* NAV-001/013/014: 디렉터리 목록. GTask 스레드에서 openat+fdopendir+fstatat(AT_SYMLINK_NOFOLLOW).
 * 항목 개별 stat 실패는 건너뛰지 않고 type=FS_FILE_OTHER, size=0으로 남긴다.
 * dirs_only: 트리용. show_hidden=FALSE면 '.' 시작 이름 제외. "." ".."는 항상 제외 */
void fs_nav_list_dir_async(const char *path, gboolean show_hidden, gboolean dirs_only,
                           GCancellable *cancellable, GAsyncReadyCallback callback,
                           gpointer user_data);                                  /* [main] */
/* (transfer full) GPtrArray<FsEntry*> (free func fs_entry_free). 실패 → NULL + error
 * (권한 → FS_ERROR_PERMISSION_DENIED, 취소 → FS_ERROR_CANCELLED) */
GPtrArray *fs_nav_list_dir_finish(GAsyncResult *result, GError **error);         /* [main] */

/* NAV-002: 탭 하나의 이동 이력. 순수 메모리, 파일 접근 없음 */
typedef struct FsNavHistory FsNavHistory;
FsNavHistory *fs_nav_history_new(const char *initial_path);                     /* [any] */
void fs_nav_history_free(FsNavHistory *history);                                 /* [any] NULL 허용 */
const char *fs_nav_history_current(const FsNavHistory *history);                 /* (transfer none) */
/* 현재와 같으면 무시. 앞으로 이력 삭제. 최대 100개, 넘으면 가장 오래된 것 삭제 */
void fs_nav_history_push(FsNavHistory *history, const char *path);
gboolean fs_nav_history_can_back(const FsNavHistory *history);
gboolean fs_nav_history_can_forward(const FsNavHistory *history);
const char *fs_nav_history_back(FsNavHistory *history);     /* 불가하면 NULL */
const char *fs_nav_history_forward(FsNavHistory *history);  /* 불가하면 NULL */
/* 상위 경로 (transfer full), 루트 "/"면 NULL. 이력에 push하는 것은 호출자 */
char *fs_nav_parent(const char *path);                                           /* [any] */

/* NAV-003: "/home/u/p" → ["/", "/home", "/home/u", "/home/u/p"] (transfer full) GStrv */
char **fs_nav_breadcrumb(const char *path);                                      /* [any] */

/* NAV-004: 입력 문자열의 dirname을 읽어 basename 접두사로 시작하는 디렉터리 후보.
 * 결과는 절대 경로, 이름순, 최대 max_results. (transfer full) GStrv */
void fs_nav_complete_async(const char *input, gboolean show_hidden, guint max_results,
                           GCancellable *cancellable, GAsyncReadyCallback callback,
                           gpointer user_data);                                  /* [main] */
char **fs_nav_complete_finish(GAsyncResult *result, GError **error);             /* [main] */

/* NAV-005 */
const char *fs_nav_home(void);  /* g_get_home_dir(), (transfer none) */

/* NAV-010: 즐겨찾기. $XDG_CONFIG_HOME/filescope/bookmarks.ini (GKeyFile), 원자적 저장 */
typedef struct FsNavBookmarks FsNavBookmarks;
/* 파일 없음은 빈 목록, 파싱 실패는 NULL + error (원본 파일 덮어쓰지 않음) */
FsNavBookmarks *fs_nav_bookmarks_load(GError **error);                           /* [main] */
void fs_nav_bookmarks_free(FsNavBookmarks *bookmarks);
const char *const *fs_nav_bookmarks_list(const FsNavBookmarks *bookmarks);       /* (transfer none) NULL 종료 */
gboolean fs_nav_bookmarks_add(FsNavBookmarks *bookmarks, const char *path, GError **error); /* 중복이면 FS_ERROR_EXISTS */
gboolean fs_nav_bookmarks_remove(FsNavBookmarks *bookmarks, const char *path);   /* 없으면 FALSE */
gboolean fs_nav_bookmarks_save(FsNavBookmarks *bookmarks, GError **error);       /* g_file_set_contents */
```
- NAV-006(멀티 탭)은 ui가 탭마다 FsNavHistory 하나를 둔다. NAV 공통 계약 "경로 접근 실패 시 기존 탭 유지": ui는 list_dir가 성공한 뒤에만 history_push와 모델 교체를 한다.

## 6. dash (`src/dash/fs_dash.h`)

DASH-001(세션 집계), DASH-002(변경량 1분봉), DASH-003(이벤트 스트림 보관), 헤더 지표(최근 1분 이벤트, 반영 지연 P95)의 데이터 모델. GTK를 쓰지 않는 순수 GLib 모듈이며 화면은 ui가 그린다. 한 FsDash는 한 스레드(R1은 main)에서만 쓴다. 시간은 모두 호출자가 넘긴 값(테스트에서 고정 가능)을 쓴다.

```c
typedef struct FsDash FsDash;

typedef struct {
    gint64 minute_start_us;   /* 벽시계(g_get_real_time) 기준 분 시작, 로컬 시간대와 무관한 UTC 60초 경계 */
    gboolean has_events;      /* 이 분에 거래량이 있었나. FALSE인 분은 봉을 그리지 않는다(시안) */
    gboolean level_known;     /* 시가~종가가 정해졌나(감시 루트 총용량이 하나라도 미상이면 FALSE) */
    gboolean has_gap;         /* 이 분에 OVERFLOW가 있었다 (R1 화면 표시는 하지 않음, 데이터만) */
    guint64 open, high, low, close; /* 바이트. 모든 감시 루트 총용량의 합 */
    guint creates, modifies, deletes; /* 거래량: 정규화 이벤트 수 (CREATE·합성 CREATE / MODIFY / DELETE) */
} FsDashCandle;

typedef struct {
    guint64 creates, modifies, deletes, moves, attribs; /* 세션 누계 (DASH-001). moves = RENAME+MOVE+MOVED_IN+MOVED_OUT */
} FsDashCounts;

/* capacity_minutes: 보관할 분 수, 30 미만이면 30으로 올림. R1 ui는 60을 넘긴다 */
FsDash *fs_dash_new(guint capacity_minutes);
void fs_dash_free(FsDash *dash);                       /* NULL 허용 */

/* 루트 총용량 기준값. 루트 ACTIVE·RESYNC 완료 시 FsWatchInfo 값으로 호출. known=FALSE면 미상 처리 */
void fs_dash_set_root_total(FsDash *dash, FsWatchId id, gboolean known, guint64 total_bytes,
                            gint64 now_us);
void fs_dash_remove_root(FsDash *dash, FsWatchId id, gint64 now_us);
/* mon events 콜백의 배치를 그대로 넘긴다(transfer none). 각 이벤트의 observed_at_us로 분을 정하고
 * root_total로 해당 루트 수준을 갱신해 고가·저가를 이벤트 단위로 반영한다 */
void fs_dash_add_events(FsDash *dash, const GPtrArray *events);
/* 이벤트가 없어도 분이 넘어가게 한다. ui가 1초 주기 타이머로 호출 */
void fs_dash_tick(FsDash *dash, gint64 now_us);

/* 최근 n개 분(오래된 것부터, 현재 진행 중인 분 포함). (transfer full) GArray<FsDashCandle> */
GArray *fs_dash_get_candles(FsDash *dash, guint n);
void fs_dash_get_counts(FsDash *dash, FsDashCounts *out);
/* now_us 기준 최근 60초 이벤트 수 (헤더 지표) */
guint fs_dash_get_events_last_minute(FsDash *dash, gint64 now_us);
/* 반영 지연 표본(ui가 모델 반영 직후 monotonic 차이를 ns로 넘김). 최근 1024개로 P95 계산, 표본 없으면 FALSE */
void fs_dash_record_latency(FsDash *dash, gint64 latency_ns);
gboolean fs_dash_get_latency_p95_ns(FsDash *dash, gint64 *p95_ns);
```

봉 규칙(PRODUCT.md "변경량 1분봉"):
- 시가 = 그 분이 시작될 때의 총용량(= 직전 분 종가, 첫 분은 기준값), 고가/저가 = 그 분 안에서 관찰한 총용량의 최댓값/최솟값(시가 포함), 종가 = 그 분 마지막 총용량.
- 총용량 = 감시 루트별 마지막 root_total의 합. 루트 추가·제거·RESYNC는 수준을 바꾸지만 거래량은 늘리지 않는다.
- 이벤트의 root_total이 미상이면 그 루트 수준을 미상으로 두고, 해당 분의 level_known=FALSE. 거래량은 계속 센다.
- 이벤트 observed_at_us가 현재 분보다 과거(시계 역행)면 현재 분에 넣는다. 보관 분 수를 넘으면 가장 오래된 분을 버린다(링 버퍼).
- OVERFLOW가 있던 분은 has_gap=TRUE. 거래량은 받은 이벤트만 센다(유실분은 알 수 없음).

## 7. ui (`src/ui/fs_ui.h`)

```c
/* app이 activate에서 호출. 모듈 객체(FsWatcher, FsJobQueue)는 ui가 만들고 소유한다.
 * 실패 → NULL + error (예: inotify 초기화 실패는 경고 띠로 표시하고 창은 연다: NULL 아님) */
GtkWidget *fs_ui_main_window_new(GtkApplication *app, GError **error);           /* [main] */
/* 종료 순서(THREADING_MODEL.md §4) 수행. 창 destroy 전에 app이 호출 */
void fs_ui_shutdown(GtkWidget *main_window);                                     /* [main] */
```
- 시각 스타일은 DESIGN.md(승인)와 docs/design/prototype/main-window.html을 따른다. ui 내부 파일 구성은 UI 작업이 정한다.
- 기존 `--smoke` 옵션(창 제목 출력 후 종료)은 유지한다.
