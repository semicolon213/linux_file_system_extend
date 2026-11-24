# 이벤트 계약 (R1)

상태: R1 계약 제안 (ARCH-001). 정규화·병합 정책은 ADR-0003. 수치(시간창, 큐 크기)는 측정 전 가정이며 G8 결과로 조정한다(미결 E-1).

## 1. FsEvent (`src/core/fs_event.h`)

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
} FsEvent;
```

- 소유권: 이벤트는 불변이고 원자적 참조 카운트를 쓴다(`fs_event_ref/unref`). 구독 콜백이 받은 배열과 이벤트는 빌려준 것이다. 보관하려면 ref한다.
- stat 값은 이벤트보다 늦게 잰 값일 수 있다. ui는 크기 변동 표시에만 쓰고 정확한 상태가 필요하면 다시 읽는다.
- `seq`는 빠짐이 있을 수 있다(병합·유실). 유실 여부는 gap 이벤트로만 판단한다.

## 2. 원시 mask와 kind 대응

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

## 3. MOVE cookie 매칭 (MON-008/009/010/028)

- IN_MOVED_FROM은 (cookie, 원래 경로, is_dir, 수신 monotonic)으로 대기 표에 넣는다.
- 같은 cookie의 IN_MOVED_TO가 오면 대기 항목을 빼서 짝 이벤트 하나를 만든다. `dirname(old_path) == dirname(path)`면 RENAME, 아니면 MOVE. 짝 이벤트의 seq·시각은 MOVED_FROM 기준이다.
- 시간창: 수신 후 50ms(`FS_MON_MOVE_WINDOW_MS`, 테스트에서 변경 가능) 안에 짝이 오지 않으면 MOVED_OUT으로 확정한다. 커널은 같은 rename의 두 이벤트를 보통 같은 read 버퍼에 연속으로 넣으므로 시간창은 버퍼 경계에 걸린 경우를 위한 여유다.
- 대기 중인 MOVED_FROM 뒤에 같은 cookie가 아닌 이벤트가 와도 순서를 지키기 위해 그 뒤 이벤트는 전달 배치에 그대로 들어간다. 짝 이벤트는 MOVED_FROM이 있던 위치에 놓인다.
- 디렉터리 이동 후 registry 처리:
  - RENAME/MOVE(범위 안): 해당 하위 트리의 wd 경로 접두사를 새 경로로 바꾼다. 새 부모가 RECURSIVE 루트 밖(다른 루트 포함)이면 MOVED_OUT과 MOVED_IN 처리와 같게 본다.
  - MOVED_OUT: 하위 트리의 watch를 모두 `inotify_rm_watch`하고 registry에서 뺀다.
  - MOVED_IN: §5 등록과 스캔.
- 감시 정지·overflow가 나면 대기 표를 비운다. 대기 항목은 MOVED_OUT으로 내보내지 않는다(재탐색이 상태를 맞춘다).

## 4. 병합 (MON-027)

- 단위는 전달 배치(§7) 하나다. 배치를 넘어서 병합하지 않는다.
- 같은 배치 안에서 같은 (kind, path)인 MODIFY끼리, ATTRIB끼리만 병합한다. 먼저 온 이벤트 자리에 남기고 `merged_count`, `last_monotonic_ns`, `raw_mask`(OR), stat 값(마지막 값)을 갱신한다.
- 그 사이에 같은 path의 다른 kind(DELETE, MOVE, RENAME, CREATE, CLOSE_WRITE) 이벤트가 끼면 병합하지 않는다. 즉 순서가 바뀌는 병합은 없다.
- CREATE, DELETE, CLOSE_WRITE, 이동 계열, SELF 계열, OVERFLOW, RESYNC_DONE은 병합하지 않는다.

## 5. 재귀·동적 감시 (MON-018/019)

- RECURSIVE 루트 등록: 루트에서 깊이 우선으로 디렉터리를 `openat(O_DIRECTORY|O_NOFOLLOW)`로 걷고 각 디렉터리에 watch를 건다. 심볼릭 링크는 따라가지 않는다. 루트와 `st_dev`가 다른 디렉터리(마운트 경계)는 등록하지 않는다(ADR-0003). 등록은 watcher 스레드가 한다.
- 비재귀 루트는 루트 디렉터리 하나만 감시한다.
- 동적 등록: RECURSIVE 루트 안에서 디렉터리 CREATE/MOVED_IN을 받으면 그 디렉터리와 하위를 등록한 뒤 바로 내용을 스캔해, 등록 전에 생긴 항목마다 CREATE(`FS_EVENT_FLAG_SYNTHETIC`)를 만든다. 이후 같은 항목의 커널 CREATE가 중복으로 오면 그대로 전달한다(소비자는 CREATE를 멱등 처리한다).
- watch 수 한도: `inotify_add_watch`가 ENOSPC면 그 디렉터리 아래는 등록을 멈추고 루트를 DEGRADED(WATCH_LIMIT)로 알린다(GError `FS_ERROR_WATCH_LIMIT`, 메시지에 미등록 디렉터리 수). 다른 디렉터리 감시는 계속한다. EACCES 디렉터리는 건너뛰고 수만 기록한다(DEGRADED로 바꾸지 않음, 미결 E-3).
- `inotify_add_watch`가 이미 등록된 wd를 돌려주면(같은 inode, 예: bind mount) 새 경로를 등록하지 않는다.
- 겹치는 루트(같거나 조상·자손)는 add_root가 거부한다(미결 U-3).

## 6. 유실·overflow (MON-015/029)

유실 원인은 둘이다: 커널 IN_Q_OVERFLOW, 전달 큐 포화(§7). 처리는 같다.
1. 새 gap_id를 만든다. 대기 중인 MOVE 표를 비운다.
2. OVERFLOW 이벤트(watch_id=0, path=NULL, gap_id)를 내보낸다. 큐 포화로 이미 배치를 못 넣는 상태라면 이벤트를 버리는 대신 "유실 표시"만 세워 두고, 큐에 자리가 나면 가장 먼저 OVERFLOW를 넣는다. 유실을 조용히 넘기지 않는다.
3. 모든 ACTIVE 루트를 DEGRADED(OVERFLOW) → RESYNCING으로 알리고, 각 루트를 처음 등록처럼 다시 걷는다: 사라진 디렉터리의 watch 제거, 새 디렉터리 watch 추가. 재탐색 중 생긴 커널 이벤트는 그대로 전달한다.
4. 끝나면 RESYNC_DONE(gap_id 같음)을 내보내고 루트를 ACTIVE로 돌린다. 재탐색이 실패한 루트는 ERROR(IO).
5. mon은 파일 목록 스냅샷을 갖지 않는다. 소비자(ui)는 RESYNC_DONE을 받으면 열려 있는 감시 범위 디렉터리를 다시 읽어 모델과 맞춘다(NFR-013). 잃어버린 개별 이벤트는 복원 불가로 표시한다(R2 이력은 gap 구간을 기록).
6. 재탐색 중 다시 overflow가 나면 진행 중 재탐색을 멈추고 새 gap_id로 1부터 다시 한다.

## 7. 전달 (MON-017)

- watcher 스레드는 정규화한 이벤트를 현재 배치에 모으고, 배치에 첫 이벤트가 들어온 뒤 16ms가 지나거나 256개가 차면 메인 컨텍스트로 보낸다(`g_main_context_invoke_full`, 기본 우선순위).
- 메인 스레드에 아직 전달되지 않은 이벤트 총수의 상한은 8192개다. 상한을 넘으면 그 배치를 버리고 §6 유실 처리를 한다.
- 메인 스레드는 배치 하나를 `events_cb` 한 번으로 넘긴다. 상태 변화는 그 배치보다 먼저 일어난 것이면 먼저 `state_cb`로 넘긴다(같은 큐로 순서 보장).
- 목표: 원시 이벤트 수신부터 events_cb 호출까지 P95 ≤ 50ms(NFR-002의 100ms 중 ui 몫 50ms를 남긴다). 측정 전 목표다.

## 8. 자체 이벤트

R1은 앱이 직접 한 파일 작업의 이벤트도 외부 이벤트와 똑같이 전달한다(origin_hint=UNKNOWN). 구분은 MON-032(P2). 내부 읽기로 인한 피드백은 읽기 계열 mask를 등록하지 않으므로 생기지 않는다.
