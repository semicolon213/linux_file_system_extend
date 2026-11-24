# 기술 설계 (내부)

이 문서는 내부 기술 설계다. 루트 `DESIGN.md`는 시각 디자인(Impeccable, 사용자 소유)으로 별개 문서다.

- 상태: R1 계약 제안(ARCH-001, 2026-11-24). 계층·모듈 배치·빌드 타깃은 docs/ARCHITECTURE.md, 결정 근거는 docs/adr/.
- 참조 표기: `DESIGN 인터페이스 §4`는 이 문서 "인터페이스" 부분의 §4를 뜻한다. 부분은 인터페이스, 이벤트, 스레드, 상태, 메모리, 데이터 모델, DB 스키마 순서다.
- 시그니처·수치를 바꾸려면 Coordinator 승인과 이 문서 갱신이 먼저다.

## 인터페이스 (R1 공개 API)

시그니처를 바꾸려면 Coordinator 승인과 이 부분 갱신이 먼저다.

### 인터페이스 §0 공통 규칙

- 함수 접두사: `fs_<모듈>_` (core는 `fs_`), 타입 접두사: `Fs`, 상수 접두사: `FS_<모듈>_`.
- 오류: 실패할 수 있는 함수는 마지막 인자 `GError **error`를 받고 `gboolean`(또는 NULL/0)으로 실패를 알린다. 도메인은 `FS_ERROR` 하나다(ADR-0002, ENGINEERING 오류). `error`는 NULL 허용.
- 경로: `const char *`는 Linux 원본 바이트 경로(NUL 종료, UTF-8 보장 없음)다. 공개 API의 경로 인자는 절대 경로여야 한다(`fs_path_validate_absolute`). 이름 인자(`name`)는 경로 구분자 없는 한 요소다. 화면 표시 문자열은 ui가 `g_filename_display_name()`으로 만든다.
- 소유권 표기: `(transfer none)` 빌려줌, `(transfer full)` 호출자가 해제, `(transfer container)` 배열만 해제. 표기가 없는 입력 포인터는 빌려줌이며 호출 동안만 유효하면 된다.
- 스레드 표기: `[main]` 메인 스레드 전용, `[any]` 어느 스레드든 가능. 공개 콜백은 모두 생성 시 기본 메인 컨텍스트(`g_main_context_default()`)에서 호출된다.
- 콜백 안에서 같은 객체의 free를 호출해도 된다(재진입 안전). 해제 함수가 반환한 뒤에는 그 객체의 콜백이 다시 호출되지 않는다.

### 인터페이스 §1 core (`src/core/`)

#### fs_error.h
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

#### fs_path.h (경로 검증, SEC-001/003)
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

/* SEC-003. 보호 규칙은 ENGINEERING 보안 §2. 위반이면 FS_ERROR_PROTECTED_PATH */
gboolean fs_path_check_protected(const char *path, FsPathOp op, GError **error); /* [any] */
/* SEC-001 사전 점검(UI 버튼 활성화용, 권고). 실제 판정은 작업 syscall 결과가 우선한다 */
gboolean fs_path_check_access(const char *path, FsPathOp op, GError **error);    /* [any] */
```

#### fs_types.h
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

#### fs_event.h
FsEvent 구조와 kind는 DESIGN 이벤트 §1~2가 정본이다. 함수:
```c
FsEvent *fs_event_ref(FsEvent *event);   /* [any] 원자적 */
void fs_event_unref(FsEvent *event);     /* [any] 원자적, NULL 허용 */
const char *fs_event_kind_to_string(FsEventKind kind); /* [any] "CREATE" 등, 정적 문자열 */

/* getter. FsEvent는 opaque이며 필드 의미는 DESIGN 이벤트 §1. 모두 [any], 문자열은 (transfer none)으로
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
/* 미상이면 FALSE, *delta=0 (DESIGN 이벤트 §8) */
gboolean fs_event_get_size_delta(const FsEvent *event, gint64 *delta);
/* 미상이면 FALSE, *total=0 */
gboolean fs_event_get_root_total(const FsEvent *event, guint64 *total);
```
FsEvent 생성(`FsEvent *fs_event_new_internal(const FsEvent *source)`: 깊은 복사, refcount=1, source는 borrowed)과 구조체 정의는 mon과 테스트만 쓰는 `core/fs_event_internal.h`에 둔다.

### 인터페이스 §2 job (`src/job/fs_job.h`)

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
- 상태 콜백 순서는 DESIGN 상태 §2의 전이만 따른다.

### 인터페이스 §3 fs (`src/fs/fs_ops.h`)

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

### 인터페이스 §4 mon (`src/mon/fs_mon.h`)

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
    gboolean total_partial;/* 감시 못 한 하위가 있어 합계가 일부만 (DESIGN 이벤트 §8) */
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
/* 루트 총용량(DESIGN 이벤트 §8). main 쪽 사본이며 이미 전달된 마지막 이벤트(또는 상태 콜백) 시점 값이다.
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

### 인터페이스 §5 nav (`src/nav/fs_nav.h`)

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

### 인터페이스 §6 dash (`src/dash/fs_dash.h`)

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

### 인터페이스 §7 ui (`src/ui/fs_ui.h`)

```c
/* app이 activate에서 호출. 모듈 객체(FsWatcher, FsJobQueue)는 ui가 만들고 소유한다.
 * 실패 → NULL + error (예: inotify 초기화 실패는 경고 띠로 표시하고 창은 연다: NULL 아님) */
GtkWidget *fs_ui_main_window_new(GtkApplication *app, GError **error);           /* [main] */
/* 종료 순서(DESIGN 스레드 §4) 수행. 창 destroy 전에 app이 호출 */
void fs_ui_shutdown(GtkWidget *main_window);                                     /* [main] */
```
- 시각 스타일은 루트 DESIGN.md(승인)와 docs/design/prototype/main-window.html을 따른다. ui 내부 파일 구성은 UI 작업이 정한다.
- 기존 `--smoke` 옵션(창 제목 출력 후 종료)은 유지한다.

## 이벤트 (R1)

정규화·병합 정책은 ADR-0003. 수치(시간창, 큐 크기)는 측정 전 가정이며 G8 결과로 조정한다(미결 E-1).

### 이벤트 §1 FsEvent (`src/core/fs_event.h`)

```c
typedef enum {
    FS_EVENT_CREATE,       /* MON-001  IN_CREATE */
    FS_EVENT_DELETE,       /* MON-002  IN_DELETE */
    FS_EVENT_MODIFY,       /* MON-003  IN_MODIFY */
    FS_EVENT_CLOSE_WRITE,  /* MON-006  IN_CLOSE_WRITE */
    FS_EVENT_ATTRIB,       /* MON-011  IN_ATTRIB */
    FS_EVENT_MOVED_OUT,    /* MON-008  짝 없는 IN_MOVED_FROM: 감시 범위 밖으로 나감 */
    FS_EVENT_MOVED_IN,     /* MON-009  짝 없는 IN_MOVED_TO: 감시 범위 밖에서 들어옴 */
    FS_EVENT_RENAME,       /* MON-010/028  짝 맞춘 이동, 부모 디렉터리 같음 */
    FS_EVENT_MOVE,         /* MON-028  짝 맞춘 이동, 부모 디렉터리 다름 */
    FS_EVENT_DELETE_SELF,  /* MON-012  감시 루트 자체 삭제 */
    FS_EVENT_MOVE_SELF,    /* MON-013  감시 루트 자체 이동 */
    FS_EVENT_UNMOUNT,      /* MON-014 */
    FS_EVENT_OVERFLOW,     /* MON-015/029  유실 구간 시작 */
    FS_EVENT_RESYNC_DONE,  /* MON-029  유실 구간 재탐색 완료 */
} FsEventKind;

typedef enum {
    FS_EVENT_FLAG_NONE      = 0,
    FS_EVENT_FLAG_SYNTHETIC = 1 << 0, /* 커널 이벤트가 아니라 동적 감시 등록 직후 스캔으로 만든 것 */
} FsEventFlags;

typedef enum { FS_ORIGIN_UNKNOWN = 0 } FsOriginHint; /* MON-032(P2)에서 확장. PID 아님 */

typedef struct {
    /* 읽기 전용 공개 필드. 생성 후 바뀌지 않는다 */
    guint64 seq;           /* FsWatcher 안에서 단조 증가, 1부터. 병합되면 첫 이벤트 값 */
    gint64 observed_at_us; /* g_get_real_time(), 첫 원시 이벤트 수신 시각 */
    gint64 monotonic_ns;   /* CLOCK_MONOTONIC, 첫 원시 이벤트 수신 시각 (지연 측정용) */
    gint64 last_monotonic_ns; /* 병합된 마지막 원시 이벤트 수신 시각. 병합 없으면 monotonic_ns */
    guint32 raw_mask;      /* 병합된 원시 mask의 OR. 합성 이벤트는 0 */
    FsEventKind kind;
    FsEventFlags flags;
    FsWatchId watch_id;    /* 감시 루트 ID (guint32). OVERFLOW/RESYNC_DONE은 0(전체) 또는 해당 루트 */
    char *path;            /* 절대 원본 바이트 경로. OVERFLOW/RESYNC_DONE은 루트 경로 또는 NULL */
    char *old_path;        /* RENAME/MOVE만. 그 외 NULL */
    guint32 cookie;        /* 짝 맞춘 이동과 MOVED_OUT/IN의 커널 cookie, 그 외 0 */
    gboolean is_dir;       /* IN_ISDIR */
    FsOriginHint origin_hint;
    guint64 gap_id;        /* OVERFLOW/RESYNC_DONE의 유실 구간 ID(1부터), 그 외 0 */
    guint32 merged_count;  /* 병합된 원시 이벤트 수, 최소 1 */
    /* 정규화 시점 lstat 결과 (watcher 스레드). 실패하거나 DELETE/MOVED_OUT이면 stat_valid=FALSE */
    gboolean stat_valid;
    guint64 size;
    gint64 mtime_ns;
    guint32 mode;
    /* 크기 색인(§8) 기준 값. 계산할 수 없으면 *_known=FALSE이고 값은 0 */
    gboolean size_delta_known;
    gint64 size_delta;     /* 이 이벤트로 바뀐 루트 총용량(바이트, 직전 색인 값 대비). 병합되면 합계 */
    gboolean root_total_known;
    guint64 root_total;    /* 이 이벤트 적용 직후 watch_id 루트의 총용량. 병합되면 마지막 값 */
} FsEvent;
```

구조체 정의는 `core/fs_event_internal.h`에만 둔다. 공개 헤더 `core/fs_event.h`에서 FsEvent는 opaque이고 위 필드는 DESIGN 인터페이스 §1의 getter로만 읽는다(PROGRESS 미결 사항 결정 기록, R1-T01 질의).

- 소유권: 이벤트는 불변이고 원자적 참조 카운트를 쓴다(`fs_event_ref/unref`). 구독 콜백이 받은 배열과 이벤트는 빌려준 것이다. 보관하려면 ref한다.
- stat 값은 이벤트보다 늦게 잰 값일 수 있다. ui는 크기 변동 표시에만 쓰고 정확한 상태가 필요하면 다시 읽는다.
- `seq`는 빠짐이 있을 수 있다(병합·유실). 유실 여부는 gap 이벤트로만 판단한다.

### 이벤트 §2 원시 mask와 kind 대응

등록 mask: `IN_CREATE | IN_DELETE | IN_MODIFY | IN_CLOSE_WRITE | IN_ATTRIB | IN_MOVED_FROM | IN_MOVED_TO | IN_DELETE_SELF | IN_MOVE_SELF | IN_DONT_FOLLOW | IN_ONLYDIR | IN_EXCL_UNLINK`. 읽기 계열(IN_ACCESS, IN_OPEN, IN_CLOSE_NOWRITE, MON-004/005/007)은 P1이며 R1은 등록하지 않는다. IN_UNMOUNT, IN_Q_OVERFLOW, IN_IGNORED는 커널이 항상 보낸다.

| 원시 | 조건 | 결과 |
|---|---|---|
| IN_CREATE | - | CREATE. IN_ISDIR이고 루트가 RECURSIVE면 §5 동적 감시 |
| IN_DELETE | - | DELETE |
| IN_MODIFY | - | MODIFY (§4 병합) |
| IN_CLOSE_WRITE | - | CLOSE_WRITE |
| IN_ATTRIB | 하위 항목 | ATTRIB (§4 병합) |
| IN_ATTRIB | 감시 디렉터리 자신(name 없음) | 부모 watch가 같은 변경을 보내므로 버림. 루트 자신이면 ATTRIB(path=루트) |
| IN_MOVED_FROM | - | §3 이동 짝 맞추기 대기 |
| IN_MOVED_TO | 대기 중 같은 cookie 있음 | RENAME 또는 MOVE |
| IN_MOVED_TO | 짝 없음 | MOVED_IN (디렉터리면 §5 등록) |
| IN_DELETE_SELF | 루트 | DELETE_SELF, 루트 STOPPED(ROOT_DELETED) |
| IN_DELETE_SELF | 하위 | 이벤트 없음(부모가 DELETE 보냄), registry 정리 |
| IN_MOVE_SELF | 루트 | MOVE_SELF, 루트 STOPPED(ROOT_MOVED). 새 경로는 알 수 없으므로 따라가지 않는다 |
| IN_MOVE_SELF | 하위 | 이벤트 없음(부모의 MOVED_FROM/TO로 처리) |
| IN_UNMOUNT | 루트 | UNMOUNT, 루트 STOPPED(UNMOUNTED) |
| IN_UNMOUNT | 하위 | UNMOUNT(path=그 디렉터리), 해당 하위 트리 registry 정리 |
| IN_IGNORED | 우리가 rm_watch했거나 앞선 DELETE_SELF/UNMOUNT 있음 | 이벤트 없음, wd 정리 |
| IN_IGNORED | 그 외, 루트 | 루트 ERROR(LOST) |
| IN_IGNORED | 그 외, 하위 | wd 정리 후 그 하위 디렉터리를 다시 등록 시도. 실패하면 루트 DEGRADED(LOST) |
| IN_Q_OVERFLOW | wd=-1 | §6 유실 처리 |
| 모르는 wd | - | 버림(제거된 watch의 늦은 이벤트), 디버그 로그만 |

### 이벤트 §3 MOVE cookie 매칭 (MON-008/009/010/028)

- IN_MOVED_FROM은 (cookie, 원래 경로, is_dir, 수신 monotonic)으로 대기 표에 넣는다.
- 같은 cookie의 IN_MOVED_TO가 오면 대기 항목을 빼서 짝 이벤트 하나를 만든다. `dirname(old_path) == dirname(path)`면 RENAME, 아니면 MOVE. 짝 이벤트의 seq·시각은 MOVED_FROM 기준이다.
- 시간창: 수신 후 50ms(`FS_MON_MOVE_WINDOW_MS`, 테스트에서 변경 가능) 안에 짝이 오지 않으면 MOVED_OUT으로 확정한다. 커널은 같은 rename의 두 이벤트를 보통 같은 read 버퍼에 연속으로 넣으므로 시간창은 버퍼 경계에 걸린 경우를 위한 여유다.
- 대기 중인 MOVED_FROM 뒤에 같은 cookie가 아닌 이벤트가 와도 순서를 지키기 위해 그 뒤 이벤트는 전달 배치에 그대로 들어간다. 짝 이벤트는 MOVED_FROM이 있던 위치에 놓인다.
- 디렉터리 이동 후 registry 처리:
  - RENAME/MOVE(범위 안): 해당 하위 트리의 wd 경로 접두사를 새 경로로 바꾼다. 새 부모가 RECURSIVE 루트 밖(다른 루트 포함)이면 MOVED_OUT과 MOVED_IN 처리와 같게 본다.
  - MOVED_OUT: 하위 트리의 watch를 모두 `inotify_rm_watch`하고 registry에서 뺀다.
  - MOVED_IN: §5 등록과 스캔.
- 감시 정지·overflow가 나면 대기 표를 비운다. 대기 항목은 MOVED_OUT으로 내보내지 않는다(재탐색이 상태를 맞춘다).

### 이벤트 §4 병합 (MON-027)

- 단위는 전달 배치(§7) 하나다. 배치를 넘어서 병합하지 않는다.
- 같은 배치 안에서 같은 (kind, path)인 MODIFY끼리, ATTRIB끼리만 병합한다. 먼저 온 이벤트 자리에 남기고 `merged_count`, `last_monotonic_ns`, `raw_mask`(OR), stat 값(마지막 값), `size_delta`(합계, 하나라도 미상이면 미상), `root_total`(마지막 값)을 갱신한다. 병합은 같은 루트의 다른 이벤트를 건너뛰어 뒤 위치로 옮기지 않으므로, 배치 순서대로 root_total을 따라가면 루트 총용량의 실제 경로와 같다(병합된 중간 값만 사라진다).
- 그 사이에 같은 path의 다른 kind(DELETE, MOVE, RENAME, CREATE, CLOSE_WRITE) 이벤트가 끼면 병합하지 않는다. 즉 순서가 바뀌는 병합은 없다.
- CREATE, DELETE, CLOSE_WRITE, 이동 계열, SELF 계열, OVERFLOW, RESYNC_DONE은 병합하지 않는다.

### 이벤트 §5 재귀·동적 감시 (MON-018/019)

- RECURSIVE 루트 등록: 루트에서 깊이 우선으로 디렉터리를 `openat(O_DIRECTORY|O_NOFOLLOW)`로 걷고 각 디렉터리에 watch를 건다. 심볼릭 링크는 따라가지 않는다. 루트와 `st_dev`가 다른 디렉터리(마운트 경계)는 등록하지 않는다(ADR-0003). 등록은 watcher 스레드가 한다.
- 비재귀 루트는 루트 디렉터리 하나만 감시한다.
- 동적 등록: RECURSIVE 루트 안에서 디렉터리 CREATE를 받으면 그 디렉터리와 하위를 등록한 뒤 바로 내용을 스캔해, 등록 전에 생긴 항목마다 CREATE(`FS_EVENT_FLAG_SYNTHETIC`, size_delta는 그 항목 크기)를 만든다. 이후 같은 항목의 커널 CREATE가 중복으로 오면 그대로 전달한다(소비자는 CREATE를 멱등 처리한다). 중복 CREATE의 size_delta는 색인에 이미 있는 크기 대비라 0이 되어 총용량이 두 번 늘지 않는다.
- 디렉터리 MOVED_IN은 하위를 등록하고 색인에 넣지만 하위 항목별 합성 이벤트는 만들지 않는다. MOVED_IN 이벤트 하나의 size_delta가 하위 전체 크기 합이다.
- watch 수 한도: `inotify_add_watch`가 ENOSPC면 그 디렉터리 아래는 등록을 멈추고 루트를 DEGRADED(WATCH_LIMIT)로 알린다(GError `FS_ERROR_WATCH_LIMIT`, 메시지에 미등록 디렉터리 수). 다른 디렉터리 감시는 계속한다. EACCES 디렉터리는 건너뛰고 수만 기록한다(DEGRADED로 바꾸지 않음, 미결 E-3).
- `inotify_add_watch`가 이미 등록된 wd를 돌려주면(같은 inode, 예: bind mount) 새 경로를 등록하지 않는다.
- 겹치는 루트(같거나 조상·자손)는 add_root가 거부한다(미결 U-3).

### 이벤트 §6 유실·overflow (MON-015/029)

유실 원인은 둘이다: 커널 IN_Q_OVERFLOW, 전달 큐 포화(§7). 처리는 같다.
1. 새 gap_id를 만든다. 대기 중인 MOVE 표를 비운다.
2. OVERFLOW 이벤트(watch_id=0, path=NULL, gap_id)를 내보낸다. 큐 포화로 이미 배치를 못 넣는 상태라면 이벤트를 버리는 대신 "유실 표시"만 세워 두고, 큐에 자리가 나면 가장 먼저 OVERFLOW를 넣는다. 유실을 조용히 넘기지 않는다.
3. 모든 ACTIVE 루트를 DEGRADED(OVERFLOW) → RESYNCING으로 알리고, 각 루트를 처음 등록처럼 다시 걷는다: 사라진 디렉터리의 watch 제거, 새 디렉터리 watch 추가. 재탐색 중 생긴 커널 이벤트는 그대로 전달한다.
4. 끝나면 RESYNC_DONE(gap_id 같음)을 내보내고 루트를 ACTIVE로 돌린다. 재탐색이 실패한 루트는 ERROR(IO).
5. mon은 파일 목록 스냅샷을 갖지 않는다. 소비자(ui)는 RESYNC_DONE을 받으면 열려 있는 감시 범위 디렉터리를 다시 읽어 모델과 맞춘다(NFR-013). 잃어버린 개별 이벤트는 복원 불가로 표시한다(R2 이력은 gap 구간을 기록).
6. 재탐색 중 다시 overflow가 나면 진행 중 재탐색을 멈추고 새 gap_id로 1부터 다시 한다.

### 이벤트 §7 전달 (MON-017)

- watcher 스레드는 정규화한 이벤트를 현재 배치에 모으고, 배치에 첫 이벤트가 들어온 뒤 16ms가 지나거나 256개가 차면 메인 컨텍스트로 보낸다(`g_main_context_invoke_full`, 기본 우선순위).
- 메인 스레드에 아직 전달되지 않은 이벤트 총수의 상한은 8192개다. 상한을 넘으면 그 배치를 버리고 §6 유실 처리를 한다.
- 메인 스레드는 배치 하나를 `events_cb` 한 번으로 넘긴다. 상태 변화는 그 배치보다 먼저 일어난 것이면 먼저 `state_cb`로 넘긴다(같은 큐로 순서 보장).
- 목표: 원시 이벤트 수신부터 events_cb 호출까지 P95 ≤ 50ms(NFR-002의 100ms 중 ui 몫 50ms를 남긴다). 측정 전 목표다.

### 이벤트 §8 크기 색인과 size_delta (변경량 1분봉, 목록 변동 열)

결정과 대안 비교는 ADR-0006. 메모리 예산은 DESIGN 메모리 §4.

- 색인: watcher 스레드가 감시 디렉터리 노드마다 `이름 → 크기` 표를 둔다(경로 전체를 키로 쓰지 않는다). 크기는 `lstat`의 `st_size`(논리 크기)이고 정규 파일만 더한다. 디렉터리·심볼릭 링크·특수 파일은 0이다. 루트 총용량은 색인 합계를 따로 들고 있어 조회가 O(1)이다.
- 범위: RECURSIVE 루트는 감시 중인 디렉터리 전체, 비재귀 루트는 루트 바로 아래 정규 파일만. 감시하지 못한 디렉터리(권한 없음, WATCH_LIMIT, 마운트 경계)는 빠지고 그 루트는 `total_partial=TRUE`(FsWatchInfo)다.
- 구축: 루트 등록·재탐색 때 재귀 등록과 같은 순회에서 `fstatat(dirfd, name, AT_SYMLINK_NOFOLLOW)`로 채운다. 등록이 끝나 ACTIVE가 되기 전(REGISTERING)에는 루트 총용량이 미상이고 그 사이 이벤트의 size_delta·root_total도 미상이다.
- 하드 링크는 경로마다 따로 더한다(중복 집계, 미결 E-7). 희소 파일은 논리 크기.

| kind | size_delta 계산 | 색인 갱신 |
|---|---|---|
| CREATE (파일) | 새 lstat 크기 − 색인 크기(없으면 0) | 추가·갱신 |
| CREATE (디렉터리) | 0 (하위 항목은 합성 CREATE가 각자 운반) | 노드 추가 |
| MODIFY, CLOSE_WRITE, ATTRIB | 새 lstat 크기 − 색인 크기 | 갱신 |
| DELETE | −색인 크기 | 삭제 |
| MOVED_OUT | −하위 전체 합(파일이면 그 크기) | 하위 노드 삭제 |
| MOVED_IN | +하위 전체 합(스캔 결과) | 하위 노드 추가 |
| RENAME, MOVE (같은 루트 안) | 0 | 노드·이름만 옮김 |
| DELETE_SELF, MOVE_SELF, UNMOUNT(루트) | 미상 | 루트 색인 폐기 |
| UNMOUNT(하위) | −하위 합 | 하위 삭제 |
| OVERFLOW | 미상 | 재탐색에서 재구축 |
| RESYNC_DONE | 재구축 후 총용량 − 유실 직전 총용량 | 재구축 결과로 교체 |

- 미상이 되는 경우: lstat 실패(사이에 삭제됨 등)면서 색인에도 없음, 루트가 REGISTERING, 루트 색인 폐기 뒤. 미상인 이벤트도 색인은 할 수 있는 만큼 갱신한다(lstat 실패한 MODIFY는 색인 값을 유지).
- 순서 보장: size_delta와 root_total은 watcher 스레드가 이벤트를 정규화하는 순서대로 계산한다. 같은 루트 이벤트를 수신 순으로 따라가면 `root_total[n] = root_total[n-1] + size_delta[n]`이 성립한다(둘 다 known일 때). 테스트 불변식으로 쓴다.
- 소비자: ui 파일 목록 변동 열(직전 이벤트 대비 증감, PROGRESS 미결 사항 U-4)과 dash 1분봉(ARCHITECTURE §7)은 이 두 값만 쓰고 파일을 다시 stat하지 않는다.

### 이벤트 §9 자체 이벤트

R1은 앱이 직접 한 파일 작업의 이벤트도 외부 이벤트와 똑같이 전달한다(origin_hint=UNKNOWN). 구분은 MON-032(P2). 내부 읽기로 인한 피드백은 읽기 계열 mask를 등록하지 않으므로 생기지 않는다.

## 스레드 (R1)

### 스레드 §1 스레드와 책임

| 스레드 | 수 | 소유 데이터 | 하는 일 |
|---|---|---|---|
| main (GTK) | 1 | 위젯, 모델, FsNavHistory, FsNavBookmarks, FsDash, FsWatcher·FsJobQueue 핸들 | UI, 모든 공개 콜백, 동기 파일 작업 |
| mon watcher | FsWatcher당 1 | inotify fd, wd registry, 크기 색인, MOVE 대기 표, 현재 배치 | poll, read, 정규화, 병합, 재귀 등록, 재탐색 |
| job worker | FsJobQueue당 1 (`GThreadPool`, max_threads=1, exclusive) | 실행 중 작업의 본문 데이터 | FsJobFunc 실행 |
| GIO 작업 스레드 | GLib 풀 | GTask 데이터 | nav 목록·자동완성 |

- GTK 위젯과 GtkTreeModel은 main에서만 만진다.
- 공개 콜백(이벤트, 감시 상태, 작업 상태·진행, GAsyncReadyCallback)은 모두 main 컨텍스트(`g_main_context_default()`)에서 호출된다.
- 모듈 객체는 만든 스레드(main)에서만 공개 함수를 부른다. 예외는 DESIGN 인터페이스에 `[any]`로 표시한 함수다.

### 스레드 §2 스레드 간 통신

| 방향 | 수단 |
|---|---|
| main → watcher | 명령 큐(`GAsyncQueue`: ADD_ROOT, REMOVE_ROOT, INJECT_OVERFLOW, STOP) + `eventfd` 깨우기. watcher는 `poll({inotify_fd, eventfd})`로 기다리며 시간창 만료까지 timeout을 둔다 |
| watcher → main | 배치·상태 변화를 담은 메시지를 `g_main_context_invoke_full`로 보냄. 미전달 이벤트 수는 원자 카운터로 세고 상한 8192(DESIGN 이벤트 §7) |
| main → worker | 작업 레코드를 잠금 아래 대기열에 넣고 `g_thread_pool_push` |
| worker → main | 상태·진행은 `g_main_context_invoke_full`. 진행은 작업당 최근 값 하나만 두고 10Hz로 덮어쓰기 |
| 취소 | `GCancellable` (작업마다 하나, nav GTask는 호출자가 넘긴 것) |

- registry와 MOVE 대기 표는 watcher 스레드만 만지므로 잠금이 없다. main의 조회 함수(`list_roots`, `get_usage`, `find_root`)는 watcher가 상태 메시지로 보낸 사본(main 소유)을 읽는다.

### 스레드 §3 잠금

| 잠금 | 보호 대상 | 순서 |
|---|---|---|
| `FsJobQueue.lock` (GMutex) | 대기열, 작업 표, 상태 값, 종료 플래그 | 1 |
| `FsJob.progress_lock` (GMutex) | 최근 진행 값, 항목 결과 배열 | 2 (queue.lock 안에서 잡을 수 있음, 반대는 금지) |
| `FsWatcher` 명령 큐 | GAsyncQueue 내부 잠금 | 다른 잠금과 같이 잡지 않음 |

- 잠금을 쥔 채 콜백 호출, GTK 호출, 파일 I/O, `g_main_context_invoke*` 호출을 하지 않는다.
- 새 잠금이 필요하면 이 표에 순서를 먼저 추가한다(Coordinator 승인).

### 스레드 §4 종료 순서

app의 `shutdown`(GApplication) 또는 창 닫기에서 `fs_ui_shutdown()`이 다음 순서로 수행한다.
1. ui가 새 사용자 명령을 막는다(작업 제출 버튼 비활성).
2. `fs_mon_watcher_free()`: STOP 명령 → 스레드 합류 → watch 해제 → inotify fd 닫기 → 아직 실행 안 된 invoke 메시지는 실행되더라도 "해제됨" 표시를 보고 아무것도 하지 않는다(메시지는 watcher 공유 상태를 ref로 잡는다).
3. `fs_job_queue_free()`: 새 제출 거부 → 실행 중 작업 취소 → worker 합류(`g_thread_pool_free(pool, FALSE, TRUE)`) → 임시 파일 정리는 작업 본문이 취소 경로에서 끝낸다 → 대기 작업 폐기.
4. (R2) DB flush·닫기.
5. 위젯 해제.

- 콜백 수명: UI 콜백이 객체 수명 뒤에 실행되지 않도록 GCancellable 취소(nav 비동기) 또는 free의 "해제됨" 표시(mon, job)로 막는다. nav 비동기 콜백은 취소되어도 호출되며(`FS_ERROR_CANCELLED`), 호출자는 이때 위젯을 만지지 않는다. free 반환 후 콜백은 없다.

트레이로 창 없이 감시를 계속하는 동작은 미결(U-1)이며, 정해지면 이 순서의 시작 시점만 바뀐다.

### 스레드 §5 검증

- G6(TSan): mon 통합 테스트(실제 이벤트 + overflow 주입 + 종료), job 테스트(진행 중 취소, 종료 중 제출)를 TSan 빌드에서 실행한다.
- 종료 경쟁(E10): 작업 실행 중·이벤트 폭주 중 free 호출 후 콜백이 오지 않음을 테스트로 확인한다.

## 상태 전이 (R1)

UI는 실제 상태 전이만 표시한다. 아래에 없는 전이는 버그다.

### 상태 §1 감시 루트 (FsWatchState)

```
          add_root
             │
       REGISTERING ──등록 실패(루트 open/watch 실패)──► ERROR(IO)
             │ 루트 watch 성공
             ├── 하위 일부 ENOSPC ──► DEGRADED(WATCH_LIMIT)
             ▼
          ACTIVE ◄──────────── RESYNCING ──재탐색 실패──► ERROR(IO)
             │                    ▲
             │ overflow/큐 포화   │ 즉시
             └──► DEGRADED(OVERFLOW)

 모든 상태 ──remove_root──► STOPPED(USER)
 ACTIVE/DEGRADED/RESYNCING ──루트 DELETE_SELF──► STOPPED(ROOT_DELETED)
                          ──루트 MOVE_SELF──► STOPPED(ROOT_MOVED)
                          ──루트 IN_UNMOUNT──► STOPPED(UNMOUNTED)
                          ──예상 못한 루트 IN_IGNORED──► ERROR(LOST)
```

| 상태 | 의미 | UI 표시 (루트 DESIGN.md States) |
|---|---|---|
| REGISTERING | 재귀 등록 중 | 감시 표시는 아직 감시 안 함 |
| ACTIVE | 정상 | 감시 중 점 |
| DEGRADED(WATCH_LIMIT) | 일부 하위 미감시, 나머지 동작 | 경고 띠(문구·스타일은 디자인 결정, 미결 U-5) |
| DEGRADED(OVERFLOW) → RESYNCING | 유실 후 재탐색 중 | 재탐색 완료 시 조용한 overflow 띠 |
| STOPPED(ROOT_DELETED) | 루트 사라짐 | 반전 띠 + 목록 stale |
| STOPPED(USER) | 사용자 해제 | 감시 안 함 점 |
| ERROR | 감시 불가 | 반전 띠 |

- STOPPED와 ERROR는 끝 상태다. 다시 감시하려면 add_root로 새 ID를 받는다.
- DEGRADED(WATCH_LIMIT)는 자동 회복하지 않는다(한도가 늘었는지 알 수 없음). overflow 재탐색이 일어나면 그때 다시 시도하고 결과로 상태를 정한다.
- 일시 정지(PAUSED, MON-022)는 P2라 R1에 없다(미결 U-2).

### 상태 §2 작업 (FsJobState)

```
 submit ─► QUEUED ──worker 시작──► RUNNING ──본문 TRUE──► DONE
             │                      │ ──본문 FALSE(취소 아님)──► FAILED
             │ cancel               │ ──취소 요청 후 본문 반환──► CANCELLED
             └────► CANCELLED        └ (PAUSED ↔ RUNNING: JOB-006/007, P1, R1 미구현)
```

| 상태 | 끝 상태 | 상태 콜백 error |
|---|---|---|
| QUEUED | 아니오 | NULL |
| RUNNING | 아니오 | NULL |
| PAUSED | 아니오 | R1에서는 나오지 않음 |
| DONE | 예 | NULL. 단 항목 중 skipped가 있을 수 있음 |
| FAILED | 예 | 대표 오류. 일부 항목만 실패하면 `FS_ERROR_PARTIAL`, 전부 같은 원인이면 그 코드 |
| CANCELLED | 예 | `FS_ERROR_CANCELLED`. 이미 끝난 항목 결과는 get_results로 볼 수 있다(JOB 공통 계약 "취소 시 부분 결과") |

- 판정 순서: 취소 요청이 있었고 본문이 FALSE + CANCELLED면 CANCELLED. 본문이 TRUE를 돌려도 항목 결과에 실패가 있으면 FAILED(PARTIAL).
- 취소 요청이 RUNNING 중에 오면 상태 콜백은 보내지 않고(취소 요청 중 표시는 ui 몫) 본문이 반환한 뒤 CANCELLED 하나만 보낸다.
- 치명 오류(NO_SPACE, CANCELLED, 원본 디렉터리 읽기 실패)는 남은 항목을 진행하지 않는다. 그 밖의 항목 오류는 기록하고 다음 항목으로 간다.

## 메모리 (R1)

함수별 소유권 표기는 인터페이스 부분이 정본이다.

### 메모리 §1 할당 계열

- 프로젝트 코드는 GLib 할당(`g_malloc`/`g_new0`/`g_strdup`/`g_free`)만 쓴다. `malloc`/`free`는 libc가 돌려준 메모리(`realpath`, `getline` 등)를 해제할 때만 짝으로 쓴다.
- 구조체 해제 함수는 `fs_<모듈>_<타입>_free`, NULL을 받아도 된다. 참조 카운트 타입(FsEvent)만 `ref/unref`.
- `g_autoptr`/`g_autofree`를 쓴다. 공개 타입마다 해당 헤더에 `G_DEFINE_AUTOPTR_CLEANUP_FUNC`를 둔다(FsEvent, FsEntry, FsWatcher, FsJobQueue, FsNavHistory, FsNavBookmarks, FsDash).

### 메모리 §2 소유권 요약

| 대상 | 규칙 |
|---|---|
| 공개 함수가 돌려준 `char *`, `char **`, `GPtrArray *` | transfer full. 배열은 free func가 설정되어 `g_ptr_array_unref` 한 번으로 해제 |
| 콜백 인자 (`const GPtrArray *events`, `const FsJobProgress *`, `const GError *`, `const FsWatchInfo *`) | transfer none, 콜백 동안만 유효 |
| FsEvent | 불변, 원자 refcount. 배치 배열이 1 ref를 갖고 콜백 반환 후 해제. 보관은 ref |
| 작업 `data` | submit 성공 시 큐가 인수, 끝난 뒤 main에서 `data_free`. submit 실패 시 호출자가 그대로 보유(큐는 해제하지 않음) |
| 입력 경로 배열 | 함수가 깊은 복사. 호출 후 호출자가 자유롭게 해제 |
| `GError` | 표준 GLib 규칙. 콜백의 `const GError *`는 빌려준 것이므로 보관하려면 `g_error_copy` |

### 메모리 §3 자원 해제 (오류 경로 포함)

- fd: `O_CLOEXEC`로 열고 모든 반환 경로에서 `close`(`g_autofd` 사용 가능, GLib ≥ 2.76이 아니면 직접 close; Ubuntu 22.04는 2.72라 직접 처리).
- `DIR *`: `closedir`. `fdopendir`에 넘긴 fd는 DIR이 소유한다.
- inotify: watcher free 시 wd를 모두 rm_watch할 필요는 없고 fd close로 정리되지만, registry 메모리는 모두 해제한다.
- 임시 파일: 복사·텍스트 저장 중 만든 `.filescope-*` 임시 파일은 실패·취소 경로에서 `unlinkat`한다(E06).
- GSource/invoke 메시지: watcher·job이 보낸 메시지는 공유 상태를 ref로 잡아 객체 해제 뒤 실행되어도 안전하다.

### 메모리 §4 크기와 한도

- 크기 계산은 `g_size_checked_add`/`g_uint64_checked_add` 등으로 overflow를 확인한다.
- 고정 크기 경로 버퍼를 쓰지 않는다. inotify read 버퍼만 고정 크기(최소 `sizeof(struct inotify_event) + NAME_MAX + 1`, 권장 64KiB)이며 이벤트 경계를 `len` 필드로 넘는다.
- 상한: 전달 큐 8192 이벤트, 이동 이력 100개, 텍스트 읽기 4MiB, 이벤트 로그 UI 500행(루트 DESIGN.md), dash 분 링 버퍼 60개(≥30), 지연 표본 1024개. job 대기열은 상한 없음(R1, 사용자 조작 단위라 작음; 미결 E-4).
- mon 크기 색인(DESIGN 이벤트 §8, ADR-0006) 예산: 정규 파일 10만 개(디렉터리 1만 개 가정) 기준 색인 + watch registry 합계 16MiB 이하. 산정: 파일 항목당 이름 할당(평균 이름 16B → 할당 단위 32B) + 해시 슬롯(해시·키·값, 적재율 고려 약 40B) + 크기 8B ≈ 80B → 8MiB, 디렉터리 노드(경로 조각, 표 머리, wd) 약 200B × 1만 ≈ 2MiB, 여유 6MiB. 경로 전체 문자열은 저장하지 않는다(필요할 때 노드 부모를 따라 조립). NFR-009(기본 RSS 150MiB)에 포함된다.
- 검증: R1-T04 G8에서 10만 파일 fixture를 재귀 등록하기 전후 RSS 차이를 기록한다(측정 전 PASS 금지). 예산 초과는 FAIL이 아니라 Coordinator 보고 후 E-1과 함께 조정.

### 메모리 §5 검증

G5: `make asan ubsan`(누수 포함, `scripts/lsan.supp`는 GLib/GTK 전역 할당만 억제). Valgrind는 선택.

## 데이터 모델

R1 메모리 모델은 확정 제안(ARCH-001), 영속 모델은 **R2에서 확정**(아래 DB 스키마).

### R1 (메모리)
| 모델 | 정의 위치 | 비고 |
|---|---|---|
| FsEntry | DESIGN 인터페이스 §1 fs_types.h | name(원본 바이트), type, target_type, size, mode, uid, gid, dev, ino, mtime_ns, is_mount_point, is_hidden. dev/inode는 영구 ID가 아니며 재사용될 수 있다 |
| FsEvent | DESIGN 이벤트 §1 | 불변, refcount |
| Job | DESIGN 인터페이스 §2 | id, state(DESIGN 상태 §2), 진행(bytes/items), 항목 결과 |
| Watch 루트 | DESIGN 인터페이스 §4 FsWatchInfo | id, path, flags, state, reason, watch_count, total_bytes·file_count·total_known·total_partial. wd·generation·크기 색인은 mon 내부 |
| 1분봉·세션 집계 | DESIGN 인터페이스 §6 FsDashCandle, FsDashCounts | dash 메모리 링 버퍼(60분). 영속 저장 없음 |
| 이동 이력 | FsNavHistory | 탭당, 최대 100 |
| 즐겨찾기 | FsNavBookmarks | 유일한 R1 영속 데이터(GKeyFile) |

화면 표시 문자열(display_name)은 모델에 두지 않고 ui가 그릴 때 만든다.

### R2 (영속, 초안)
Snapshot: id, path, hash, size, captured_at, completeness. 이벤트·작업 이력 저장. 변경 내용 패널과 되돌리기는 이 모델에 의존한다.

## DB 스키마 (SQLite)

상태: **R2에서 확정.** R1(탐색·파일 작업·감시·작업 큐)은 DB를 쓰지 않는다(ARCH-001 결정). R1의 영속 데이터는 즐겨찾기 파일 하나(`$XDG_CONFIG_HOME/filescope/bookmarks.ini`, DESIGN 인터페이스 §5)뿐이다.

의존 관계: PRODUCT.md의 "변경 내용 표시"(변경 내용 패널: 텍스트 줄 Diff, 이미지 전후 비교, 문서 블록 변경)와 되돌리기는 직전 버전 보관(HIS-001/005)이 전제다. 따라서 변경 내용 패널은 R2 이력과 이 스키마가 확정된 뒤에 구현한다. R1 UI는 패널 자리를 만들지 않거나 "R2 예정" 상태로 둔다(미결 U-1).

### R2 초안 (확정 아님)
테이블 events(seq PK, observed_at, kind, path BLOB, old_path BLOB, raw_mask, gap_id), snapshots(id PK, path BLOB, hash, size, captured_at, completeness), jobs(id PK, state, kind, error), settings(key PK,value), watches(id PK,path BLOB), file_index(path BLOB PK,mtime,size). events 시간/경로 인덱스. 스냅샷 본문은 별도 content-addressed 저장소. migration 버전 관리, prepared statement, WAL, 단일 writer, bounded busy retry. DB와 blob 간 장애 복구 및 orphan 정리 테스트 필수.

R2 확정 시 반영할 R1 계약: events 열은 DESIGN 이벤트 §1 FsEvent 필드(seq, observed_at_us, kind, path, old_path, raw_mask, gap_id, merged_count)를 따른다. gap 구간(OVERFLOW~RESYNC_DONE)은 "복원 불가" 표시의 근거로 저장한다.
