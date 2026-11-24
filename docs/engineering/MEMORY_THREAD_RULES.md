# 메모리 및 동시성 공통 계약

세부 계약: docs/architecture/MEMORY_MANAGEMENT.md(소유권), THREADING_MODEL.md(스레드·잠금·종료), EVENT_SPEC.md §7(전달 큐). 아래는 구현·리뷰 체크용 요약이다.

1. 공개 API는 transfer full/none/container를 명시한다. 표기 없는 입력 포인터는 빌려준 것.
2. 스레드를 건너는 이벤트는 불변 FsEvent + 원자 refcount. 배치 배열은 전달 후 해제되고, 보관하려는 소비자가 ref한다.
3. 작업 `data`는 submit 성공 시 큐가 인수하고 main에서 해제한다. 실패 시 호출자 소유 그대로.
4. 잠금 순서: `FsJobQueue.lock` → `FsJob.progress_lock`. mon registry는 watcher 스레드 전용이라 잠금 없음. 새 잠금은 THREADING_MODEL §3에 먼저 추가.
5. 잠금 중 콜백·GTK 호출·파일 I/O·`g_main_context_invoke*` 금지.
6. 큐 상한과 포화 정책: mon 전달 큐 8192(넘치면 유실 gap + 재탐색, 조용히 버리지 않음). job 대기열은 상한 없음(R1, 사용자 조작 단위라 작음; 미결 E-4).
7. 종료 순서: 신규 작업 차단 → 감시 중지(watcher 합류) → worker 취소·합류 → 큐 정리 → (R2) DB 종료 → 위젯 해제. free 반환 후 콜백 없음.
8. UI 콜백이 객체 수명 뒤에 실행되지 않도록 GCancellable 취소(nav 비동기) 또는 free의 "해제됨" 표시(mon, job)로 막는다. nav 비동기 콜백은 취소되어도 호출되며(`FS_ERROR_CANCELLED`), 호출자는 이때 위젯을 만지지 않는다.
9. G5(ASan/UBSan), G6(TSan) 통과가 완료 조건이다.
