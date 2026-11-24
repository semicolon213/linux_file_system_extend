# 상태 전이 (R1)

상태: R1 계약 제안 (ARCH-001). UI는 실제 상태 전이만 표시한다. 아래에 없는 전이는 버그다.

## 1. 감시 루트 (FsWatchState)

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

| 상태 | 의미 | UI 표시 (DESIGN.md States) |
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

## 2. 작업 (FsJobState)

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
