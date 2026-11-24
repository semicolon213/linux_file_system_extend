# 스레드 모델 (R1)

상태: R1 계약 제안 (ARCH-001).

## 1. 스레드와 책임

| 스레드 | 수 | 소유 데이터 | 하는 일 |
|---|---|---|---|
| main (GTK) | 1 | 위젯, 모델, FsNavHistory, FsNavBookmarks, FsDash, FsWatcher·FsJobQueue 핸들 | UI, 모든 공개 콜백, 동기 파일 작업 |
| mon watcher | FsWatcher당 1 | inotify fd, wd registry, 크기 색인, MOVE 대기 표, 현재 배치 | poll, read, 정규화, 병합, 재귀 등록, 재탐색 |
| job worker | FsJobQueue당 1 (`GThreadPool`, max_threads=1, exclusive) | 실행 중 작업의 본문 데이터 | FsJobFunc 실행 |
| GIO 작업 스레드 | GLib 풀 | GTask 데이터 | nav 목록·자동완성 |

- GTK 위젯과 GtkTreeModel은 main에서만 만진다.
- 공개 콜백(이벤트, 감시 상태, 작업 상태·진행, GAsyncReadyCallback)은 모두 main 컨텍스트(`g_main_context_default()`)에서 호출된다.
- 모듈 객체는 만든 스레드(main)에서만 공개 함수를 부른다. 예외는 INTERFACE_SPEC에 `[any]`로 표시한 함수다.

## 2. 스레드 간 통신

| 방향 | 수단 |
|---|---|
| main → watcher | 명령 큐(`GAsyncQueue`: ADD_ROOT, REMOVE_ROOT, INJECT_OVERFLOW, STOP) + `eventfd` 깨우기. watcher는 `poll({inotify_fd, eventfd})`로 기다리며 시간창 만료까지 timeout을 둔다 |
| watcher → main | 배치·상태 변화를 담은 메시지를 `g_main_context_invoke_full`로 보냄. 미전달 이벤트 수는 원자 카운터로 세고 상한 8192(EVENT_SPEC §7) |
| main → worker | 작업 레코드를 잠금 아래 대기열에 넣고 `g_thread_pool_push` |
| worker → main | 상태·진행은 `g_main_context_invoke_full`. 진행은 작업당 최근 값 하나만 두고 10Hz로 덮어쓰기 |
| 취소 | `GCancellable` (작업마다 하나, nav GTask는 호출자가 넘긴 것) |

- registry와 MOVE 대기 표는 watcher 스레드만 만지므로 잠금이 없다. main의 조회 함수(`list_roots`, `get_usage`, `find_root`)는 watcher가 상태 메시지로 보낸 사본(main 소유)을 읽는다.

## 3. 잠금

| 잠금 | 보호 대상 | 순서 |
|---|---|---|
| `FsJobQueue.lock` (GMutex) | 대기열, 작업 표, 상태 값, 종료 플래그 | 1 |
| `FsJob.progress_lock` (GMutex) | 최근 진행 값, 항목 결과 배열 | 2 (queue.lock 안에서 잡을 수 있음, 반대는 금지) |
| `FsWatcher` 명령 큐 | GAsyncQueue 내부 잠금 | 다른 잠금과 같이 잡지 않음 |

- 잠금을 쥔 채 콜백 호출, GTK 호출, 파일 I/O, `g_main_context_invoke*` 호출을 하지 않는다.
- 새 잠금이 필요하면 이 표에 순서를 먼저 추가한다(Coordinator 승인).

## 4. 종료 순서

app의 `shutdown`(GApplication) 또는 창 닫기에서 `fs_ui_shutdown()`이 다음 순서로 수행한다.
1. ui가 새 사용자 명령을 막는다(작업 제출 버튼 비활성).
2. `fs_mon_watcher_free()`: STOP 명령 → 스레드 합류 → watch 해제 → inotify fd 닫기 → 아직 실행 안 된 invoke 메시지는 실행되더라도 "해제됨" 표시를 보고 아무것도 하지 않는다(메시지는 watcher 공유 상태를 ref로 잡는다).
3. `fs_job_queue_free()`: 새 제출 거부 → 실행 중 작업 취소 → worker 합류(`g_thread_pool_free(pool, FALSE, TRUE)`) → 임시 파일 정리는 작업 본문이 취소 경로에서 끝낸다 → 대기 작업 폐기.
4. (R2) DB flush·닫기.
5. 위젯 해제.

트레이로 창 없이 감시를 계속하는 동작은 미결(U-1)이며, 정해지면 이 순서의 시작 시점만 바뀐다.

## 5. 검증

- G6(TSan): mon 통합 테스트(실제 이벤트 + overflow 주입 + 종료), job 테스트(진행 중 취소, 종료 중 제출)를 TSan 빌드에서 실행한다.
- 종료 경쟁(E10): 작업 실행 중·이벤트 폭주 중 free 호출 후 콜백이 오지 않음을 테스트로 확인한다.
