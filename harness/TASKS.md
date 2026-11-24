# 작업 목록

작업 지시서의 정본이다. 상태 흐름과 작업 지시서 양식은 harness/WORKFLOW.md, 요구사항 연결은 harness/PROGRESS.md 추적표를 따른다.

## 등록 규칙

- 작업마다 harness/WORKFLOW.md §7 "작업 지시서" 양식으로 이 문서에 절을 추가한다. 상태는 harness/WORKFLOW.md §2를 따른다. 첫 작업은 설계 계약 확정, 다음은 빌드 및 검증 명령 구현이다. 선행 계약 없는 기능 작업은 READY로 지정하지 않는다.
- 상태 값은 Coordinator만 바꾼다. 아래 요약 표와 각 절의 "상태" 줄을 함께 고친다.

## 상태 요약 (2026-11-24)

| 작업 | 상태 | 선행 |
|---|---|---|
| R0-001 빌드·테스트 하네스 | VERIFY (121ef2f, 독립 QA는 R1 묶음) | ADR-0001 |
| R1-T01 core·빌드 골격 | VERIFY (병합, 독립 QA 대기) | ARCH-001 |
| R1-T02 job | VERIFY (병합, 독립 QA 대기) | R1-T01 |
| R1-T03 fs | IN_PROGRESS, 일시 중지(Codex 사용량 한도) | R1-T01, R1-T02 |
| R1-T04 mon | IN_PROGRESS, 일시 중지(Codex 사용량 한도) | R1-T01 |
| R1-T05 nav | VERIFY (병합, 독립 QA 대기) | R1-T01 |
| R1-T06 ui | BACKLOG | R1-T03, R1-T04, R1-T05, R1-T07 |
| R1-T07 dash | IN_PROGRESS, 일시 중지(Codex 사용량 한도) | R1-T01 |

독립 QA는 R1 전체를 묶어 한 번 한다(R0-001 포함, PROGRESS 미결 사항 E-6).

## R0 하네스 구축 (초기 개요)

선행: 기존 저장소 및 Ubuntu 환경 확인. 허용 범위: 승인된 빌드/테스트/스크립트 파일. CMake target, GLib 테스트, make 게이트, Xvfb smoke, sanitizer presets, CI를 구현한다. 빈 테스트나 미구현 게이트를 PASS로 처리하지 않는다. QA는 실제 명령과 SHA를 기록한다.

## R0-001 빌드·테스트 하네스

- Task ID / 요구사항 ID: R0-001 / 빌드·테스트 하네스 (ENGINEERING 빌드, TESTING §1 품질 게이트 G1~G7 골격)
- 상태: VERIFY (Codex 완료, 개발자 검증 통과. 독립 QA는 R1 묶음 QA에서)
- 목표: C++17/Qt 6 스캐폴드를 C17/GTK3 CMake/Ninja 프로젝트로 교체하고, `make build/test/format-check/lint/architecture/asan/ubsan/tsan/e2e` 진입점을 만든다. 빈 창을 띄우는 GTK3 앱(창 제목 FileScope), GLib 테스트 1개 이상, Xvfb smoke 1개.
- 제외 범위: 기능 모듈 구현, 시각 디자인 적용(루트 DESIGN.md는 UI 작업에서 적용).
- 기준 브랜치 / SHA: semicolon213/catshark @ 6d621d5 (main은 b563c1c, 디자인 커밋은 이 브랜치에만 있음)
- 선행 작업: ADR-0001 승인(완료), 의존성 설치(사용자).
- 허용 파일: CMakeLists.txt, CMakePresets.json, Makefile, src/main.c(신규), src/main.cpp·main_window.*(삭제), tests/, scripts/, .clang-format, .clang-tidy, README.md, .gitignore, .github/workflows/
- 변경 금지: docs/ (README 제외), harness/, PRODUCT.md, 루트 DESIGN.md, .impeccable/, docs/design/prototype/
- 참조 계약: ENGINEERING 빌드, ENGINEERING 의존성, ENGINEERING 코딩 규칙, TESTING §1 품질 게이트
- 수용 조건:
  - Given 의존성이 설치된 Ubuntu 24.04, When `make build`, Then 종료 코드 0, 프로젝트 경고 0.
  - When `make test`, Then GLib 테스트가 실제로 실행되어 성공.
  - When `make e2e`, Then Xvfb에서 앱이 뜨고 창 제목 FileScope를 확인 후 정상 종료.
  - When `make asan ubsan tsan`, Then 별도 빌드 디렉터리에서 실행되어 오류 0.
  - 아직 대상이 없는 게이트(integration, stress, benchmark)는 PASS가 아닌 명시적 "NOT_IMPLEMENTED" 메시지와 비0 종료.
- 필수 테스트 / 게이트: G1, G2, G4, G5, G7(smoke)
- 예상 위험: WSL의 GUI(Xvfb) 동작 차이, sanitizer와 GTK의 알려진 누수 억제 파일 필요.

## R1-T01 core·빌드 골격

- Task ID / 요구사항 ID: R1-T01 core·빌드 골격 / SEC-001, SEC-003(경로 검증·보호), 공통 기반(오류 도메인, FsEvent, FsEntry)
- 상태: VERIFY (Codex 구현 완료, `semicolon213/design-and-r0`에 병합, 개발자 게이트 통과. 독립 QA는 R1 묶음 QA에서) 커밋 a3b7e0f, 7d90a5e(병합 1de4d68)
- 목표: `src/core`(fs_error, fs_path, fs_types, fs_event)를 DESIGN 인터페이스 §1, DESIGN 이벤트 §1대로 구현하고, 모듈별 하위 CMake를 받아들이는 루트 빌드 골격과 테스트 지원 코드를 만든다.
- 제외 범위: job/fs/mon/nav/ui 구현, SEC-002(fs 작업 몫), 시각 디자인.
- 기준 브랜치 / SHA: semicolon213/design-and-r0 @ 승인 커밋(이 커밋)
- 선행 작업: ARCH-001 계약 승인(완료). R0-001 독립 QA는 사용자 지시로 R1 묶음 QA에 합침
- 허용 파일: `CMakeLists.txt`(루트, 단독 소유), `Makefile`, `CMakePresets.json`, `scripts/check-architecture.sh`, `src/core/**`, `tests/core/**`, `tests/support/**`, `tests/integration/CMakeLists.txt`
- 변경 금지: `src/main.c`, `src/app_info.*`, `src/{job,fs,mon,nav,ui}/**`, `docs/**`, `harness/**`, `PRODUCT.md`, 루트 `DESIGN.md`, `.impeccable/**`, `docs/design/prototype/**`
- 참조 계약: docs/ARCHITECTURE.md §1~4, DESIGN 인터페이스 §0~1, DESIGN 이벤트 §1, ENGINEERING 오류·보안·코딩 규칙, ADR-0002, ADR-0004
- 구현 메모:
  - 루트 CMake: `filescope_core` 추가. `src/<m>/CMakeLists.txt`, `tests/<m>/CMakeLists.txt`가 있으면 add_subdirectory(m = job, fs, mon, nav, ui). `TARGET filescope_ui`가 있으면 실행 파일에 링크. `filescope_configure_target()`을 하위에서 쓸 수 있게 유지.
  - `tests/integration/CMakeLists.txt`: `test_<m>_*.c`를 파일마다 실행 파일로 만들고 `filescope_<m>`, `filescope_test_support`에 링크. 이름에 `_stress_`가 있으면 라벨 `stress`, `_bench_`면 `benchmark`, 그 외 `integration`. 단위 테스트는 라벨 `unit`.
  - Makefile: `integration`/`stress`/`benchmark`를 `ctest --preset debug -L <라벨> --no-tests=error`로 바꾼다(테스트 없으면 비0 종료 유지). `format-check`·`lint`가 하위 디렉터리 파일까지 포함하게 한다.
  - check-architecture.sh: ARCHITECTURE §1 표의 금지 include와 `_internal.h`/`_testing.h` 규칙을 검사한다. 위반 사례로 스크립트 자체를 검증하는 테스트 1개(임시 디렉터리 사본 사용).
  - `tests/support`: 임시 디렉터리 생성·재귀 정리, HOME/XDG 임시 경로 설정, "메인 루프를 돌리며 조건 대기(제한 시간)" 도우미.
  - `core/fs_event_internal.h`: mon·테스트용 생성 함수.
- 수용 조건 (Given/When/Then):
  - Given errno 표의 각 값, When `fs_error_code_from_errno`, Then ENGINEERING 오류 §2 대응과 일치.
  - Given `G_IO_ERROR_NOT_SUPPORTED`, When `fs_propagate_gio_error`, Then FS_ERROR_NOT_SUPPORTED이고 원본 message 보존.
  - Given 이름 "", "a/b", ".", "..", 256바이트 이름, When `fs_path_validate_name`, Then 각각 INVALID_PATH 또는 NAME_TOO_LONG. Given "notes.md", Then TRUE.
  - Given 상대 경로·PATH_MAX 이상 경로, When `fs_path_validate_absolute`, Then FALSE와 해당 코드.
  - Given "/usr/bin/x"(WRITE), "/"(REMOVE), HOME 경로(REMOVE), "/run/user/<uid>/a"(WRITE), 임시 디렉터리 하위(REMOVE), When `fs_path_check_protected`, Then 앞 셋 PROTECTED_PATH, 뒤 둘 TRUE. READ는 모두 TRUE.
  - Given "/a//b/./c/../d", When `fs_path_canonicalize`, Then "/a/b/d". `fs_path_is_ancestor("/a/b", "/a/bc")`는 FALSE.
  - Given FsEvent 1개를 두 스레드가 ref/unref 10만 회, Then TSan·ASan 오류 없음과 최종 해제.
  - Given src/core가 `ui/` 또는 `mon/`을 include하는 임시 사본, When check-architecture.sh, Then 비0 종료.
  - When `make integration`(통합 테스트가 아직 없을 때), Then 비0 종료(PASS로 처리하지 않음).
- 필수 테스트 / 게이트: G1, G2, G4, G5, G6(refcount 테스트)
- 예상 위험 / 차단 조건: 루트 CMake 구조가 다른 모든 R1 작업의 선행이므로 지연 시 전체 차단. HOME 보호 테스트는 HOME을 임시 경로로 바꿔 실행.

## R1-T02 작업 큐 (job)

- Task ID / 요구사항 ID: R1-T02 작업 큐 / JOB-001, JOB-002, JOB-003, JOB-008
- 상태: VERIFY (Codex 구현 완료, `semicolon213/design-and-r0`에 병합, 개발자 게이트 통과. 독립 QA는 R1 묶음 QA에서) 커밋 87e7761(병합 49daab4)
- 목표: `src/job` FsJobQueue를 DESIGN 인터페이스 §2, DESIGN 상태 §2, DESIGN 스레드대로 구현한다(동시 실행 1, FIFO, 10Hz 진행, 취소, 항목 결과).
- 제외 범위: 일시정지·재개(JOB-006/007), 우선순위·동시성 설정(JOB-009/010), 이력(JOB-011), 재시도(JOB-012), 파일 작업 본문(fs 몫).
- 기준 브랜치 / SHA: R1-T01 통합 커밋
- 선행 작업: R1-T01
- 허용 파일: `src/job/**`, `tests/job/**`
- 변경 금지: 루트 CMake·Makefile·scripts, 다른 모듈, docs, harness, 디자인 파일
- 참조 계약: DESIGN 인터페이스 §0·§2, DESIGN 상태 §2, DESIGN 스레드 §2~4, DESIGN 메모리, ENGINEERING 오류 §4
- 수용 조건 (Given/When/Then):
  - Given 작업 A, B를 차례로 제출, When 메인 루프 실행, Then 상태 콜백 순서 A:QUEUED, B:QUEUED, A:RUNNING, A:DONE, B:RUNNING, B:DONE (B는 A 종료 전 RUNNING이 되지 않음).
  - Given 본문이 worker에서 실행 중, When 메인 루프 반복, Then 메인 컨텍스트가 막히지 않는다(본문 실행 중 idle 콜백이 돈다).
  - Given 100ms 동안 진행을 1만 번 보고하는 본문, Then 진행 콜백은 2회 이하 + 종료 전 마지막 1회이고 마지막 값이 최종 값과 같다. bytes_total=-1이면 그대로 전달.
  - Given QUEUED 작업, When cancel, Then 즉시 CANCELLED(FS_ERROR_CANCELLED), 본문 미실행, data_free 1회.
  - Given cancellable을 확인하는 RUNNING 본문, When cancel, Then CANCELLED 하나만 통지되고 그 전 항목 결과는 get_results로 조회된다.
  - Given 항목 3개 중 1개 실패를 기록하고 TRUE를 반환한 본문, Then FAILED + FS_ERROR_PARTIAL.
  - Given 실행 중 작업과 대기 작업, When fs_job_queue_free, Then 반환 후 콜백 없음, 모든 data_free 호출, 누수 없음.
  - Given free 진행 중, When submit, Then 0과 FS_ERROR_CANCELLED.
- 필수 테스트 / 게이트: G1, G2, G4, G5, G6
- 예상 위험 / 차단 조건: 종료 중 invoke 메시지 수명 문제(TSan으로 확인). `GThreadPool` exclusive 스레드 합류 순서.

## R1-T03 파일 작업 (fs)

- Task ID / 요구사항 ID: R1-T03 파일 작업 / FS-001, FS-002, FS-004, FS-005, FS-006, FS-007, FS-008, FS-009, FS-010, FS-012, FS-014, FS-018, SEC-002 (FS-003·011·013은 ui 몫, API 지원만)
- 상태: IN_PROGRESS (Codex, 일시 중지: Codex 사용량 한도. 재개 시 이어서 진행)
- 목표: `src/fs` 공개 API(DESIGN 인터페이스 §3)를 ADR-0005 규칙으로 구현한다: 동기 생성·이름 변경·휴지통·텍스트 읽기/저장, 작업 큐 기반 복사·이동·일괄 휴지통·영구 삭제, 충돌 정책, 확인 플래그.
- 제외 범위: 실행 취소(FS-019), 일괄 이름 변경(FS-015), 휴지통 관리 화면(R2), UI.
- 기준 브랜치 / SHA: R1-T02 통합 커밋
- 선행 작업: R1-T01, R1-T02
- 허용 파일: `src/fs/**`, `tests/fs/**`, `tests/integration/test_fs_*.c`
- 변경 금지: 루트 CMake·Makefile·scripts, 다른 모듈(core 포함, core 변경이 필요하면 Coordinator에 보고), docs, harness, 디자인 파일
- 참조 계약: DESIGN 인터페이스 §0·§3, ENGINEERING 보안 §1~4, ENGINEERING 오류, DESIGN 메모리 §3, DESIGN 상태 §2, ADR-0005
- 수용 조건 (Given/When/Then): 모든 경로는 g_dir_make_tmp 아래, HOME/XDG는 임시 경로.
  - FS-001/002: Given 빈 디렉터리, When create_file "a.txt" / create_dir "d", Then 생성되고 절대 경로 반환. 같은 이름 다시 → FS_ERROR_EXISTS, 기존 내용 불변. 쓰기 권한 없는 디렉터리 → PERMISSION_DENIED. 이름 "a/b" → INVALID_PATH, 파일 미생성.
  - FS-008: Given a.txt와 b.txt, When rename a.txt→b.txt, Then FS_ERROR_EXISTS, 두 파일 내용 불변.
  - FS-004/005 (E07): Given 텍스트 파일 읽기 후 외부에서 내용 변경, When write_text(expected=읽은 stamp), Then FS_ERROR_CONFLICT, 외부 내용 유지. 변경 없을 때 저장은 성공, 모드 유지, 디렉터리에 `.filescope-*` 잔여 없음. 4MiB 초과 파일 read → TOO_LARGE. expected=NULL에 CONFIRMED 없음 → NOT_CONFIRMED.
  - FS-006: Given 임시 XDG_DATA_HOME, When trash, Then 원본 사라지고 휴지통 files 디렉터리에 존재. 휴지통 미지원 경로(재현 불가하면 NOT_RUN 사유 기록) → NOT_SUPPORTED이고 원본 유지.
  - FS-007/014: Given 파일 3개, When delete(flags 없음), Then 0 + NOT_CONFIRMED, 3개 유지. CONFIRMED면 DONE, 모두 삭제. 그중 1개가 쓰기 권한 없는 디렉터리 안이면 FAILED(PARTIAL), 나머지 삭제, 항목 결과 3개.
  - FS-009/012: Given 하위 디렉터리·심볼릭 링크·FIFO가 있는 트리, When copy, Then 구조·내용·권한 비트·mtime 일치, 링크는 링크로, FIFO는 항목 실패(NOT_SUPPORTED), 상태 FAILED(PARTIAL).
  - FS-010: Given 같은 장치 이동, Then rename으로 처리(ino 유지). 디렉터리를 자기 하위로 이동 → 제출 시 INVALID_PATH.
  - FS-018: Given 대상에 같은 이름 존재, When 정책 FAIL / SKIP / KEEP_BOTH / OVERWRITE(CONFIRMED), Then 각각 EXISTS 실패 / skipped / "a (2).txt" 생성 / 대상 교체. OVERWRITE에 CONFIRMED 없음 → NOT_CONFIRMED. 디렉터리→파일 덮어쓰기는 항목 실패.
  - 취소 (E06): Given 64MiB 파일 복사 중, When cancel, Then CANCELLED, 대상 디렉터리에 부분 파일·임시 파일 없음, 원본 유지.
  - 보호 경로: Given "/usr/x"에 create_file 또는 "/" delete(CONFIRMED), Then PROTECTED_PATH, 시스템에 쓰기 시도 없음.
  - EXDEV 이동: 테스트 환경에 다른 장치(tmpfs 등)가 있으면 통합 테스트, 없으면 NOT_RUN 사유 기록(PASS 아님).
- 필수 테스트 / 게이트: G1, G2, G3, G4, G5, G6
- 예상 위험 / 차단 조건: ENOSPC 재현(작은 tmpfs는 root 필요하므로 쓰기 실패 주입 지점을 내부 함수로 두고 단위 테스트, 실제 재현은 NOT_RUN 기록). 휴지통 GIO 동작이 XDG 경로·마운트에 따라 다름. 테스트가 root로 돌면 권한 테스트 skip 처리 확인.

## R1-T04 감시 (mon)

- Task ID / 요구사항 ID: R1-T04 감시 / MON-001, 002, 003, 006, 008, 009, 010, 011, 012, 013, 014, 015, 016, 018, 019, 020, 021, 027, 028, 029, DASH-002·U-4 데이터 원천(루트 크기 색인, size_delta/root_total) (MON-017은 전달 계약까지, 화면 반영은 R1-T06)
- 상태: IN_PROGRESS (Codex, 일시 중지: Codex 사용량 한도. 재개 시 이어서 진행)
- 목표: `src/mon` FsWatcher를 DESIGN 인터페이스 §4, DESIGN 이벤트 전체(§8 크기 색인 포함), DESIGN 상태 §1, DESIGN 스레드대로 구현한다. 루트별 크기 색인을 재귀 등록·재탐색 순회에서 만들고 이벤트마다 size_delta/root_total을 채우며 루트 총용량 조회를 제공한다(ADR-0006).
- 제외 범위: 읽기 계열 이벤트(MON-004/005/007), 일시정지(MON-022), 필터(MON-024~026), 자체 이벤트 구분(MON-032), 이력 저장(R2).
- 기준 브랜치 / SHA: R1-T01 통합 커밋
- 선행 작업: R1-T01
- 허용 파일: `src/mon/**`, `tests/mon/**`, `tests/integration/test_mon_*.c`
- 변경 금지: 루트 CMake·Makefile·scripts, 다른 모듈(core 포함, FsEvent 필드 변경이 필요하면 Coordinator에 보고), docs, harness, 디자인 파일
- 참조 계약: DESIGN 인터페이스 §0·§1(FsEvent getter)·§4, DESIGN 이벤트 §1~9, DESIGN 상태 §1, DESIGN 스레드, DESIGN 메모리 §4(색인 예산), ADR-0003, ADR-0006
- 선행 조건 보충: FsEvent의 size_delta/root_total 필드와 getter가 core에 있어야 한다(PROGRESS 미결 사항 E-9). 없으면 BLOCKED로 보고한다.
- 수용 조건 (Given/When/Then): 외부 변경은 테스트가 syscall 또는 argv 기반 `g_spawn_sync`로 만들고, 대기는 조건 + 2초 제한.
  - E01: Given 비재귀 루트, When 파일 생성·쓰기·닫기·chmod·삭제, Then CREATE, MODIFY, CLOSE_WRITE, ATTRIB, DELETE가 이 순서로 오고 CLOSE_WRITE의 stat size가 쓴 크기와 같다.
  - MON-027: Given 한 파일에 4KiB 쓰기 100회를 빠르게, Then MODIFY 이벤트 수 < 100이고 merged_count 합이 수신한 IN_MODIFY 수와 같다. 중간에 같은 경로의 다른 kind가 끼면 그 앞뒤 MODIFY는 합쳐지지 않는다(정규화 단위 테스트).
  - MON-010/028: Given 루트 안 같은 디렉터리 rename, Then RENAME 1개(old_path, path, cookie≠0). 다른 하위 디렉터리로 이동 → MOVE 1개. 루트 밖으로 이동 → 시간창 뒤 MOVED_OUT. 밖에서 안으로 → MOVED_IN.
  - MON-018/019 (E02): Given 재귀 루트, When `mkdir -p a/b/c` 직후 c 안에 파일 생성, Then a, b, c 감시 등록(watch_count 증가)과 그 파일의 CREATE(합성 또는 커널) 수신. a를 루트 밖으로 이동하면 a 하위 watch 해제(watch_count 감소). 디렉터리 rename 뒤 그 안 파일 수정 이벤트의 path가 새 경로.
  - MON-012/013/014: Given 루트 삭제, Then DELETE_SELF 이벤트와 STOPPED(ROOT_DELETED). 루트 이동 → MOVE_SELF, STOPPED(ROOT_MOVED). 실제 UNMOUNT는 root가 필요하면 NOT_RUN 사유 기록, 정규화 단위 테스트(원시 이벤트 버퍼 입력)로 대체 검증.
  - MON-016: Given 하위 디렉터리 삭제, Then IN_IGNORED 후 registry에서 제거(watch_count 감소), 이벤트로 노출되지 않음. remove_root 뒤 늦은 이벤트는 전달되지 않음.
  - MON-015/029 (E03): Given `fs_mon_testing_inject_overflow`, Then OVERFLOW(gap_id=n) → 상태 DEGRADED→RESYNCING→ACTIVE → RESYNC_DONE(gap_id=n). 주입 직전·직후 만든 디렉터리도 재탐색 뒤 감시된다. 큐 용량을 16으로 줄이고 파일 1000개 생성 → OVERFLOW 최소 1회, OVERFLOW 없이 이벤트가 빠지는 경우 없음.
  - MON-020/021: Given 루트 2개, Then 각 이벤트의 watch_id가 맞음. 겹치는 경로 add_root → FS_ERROR_EXISTS. 없는 경로 → NOT_FOUND. remove_root → STOPPED(USER).
  - 크기 색인 (DESIGN 이벤트 §8): Given 파일 a(100B), b(200B), 하위 디렉터리 d 안 c(300B)가 있는 재귀 루트, When 등록 완료(ACTIVE), Then FsWatchInfo·get_root_total = 600B, file_count 3, total_known TRUE. REGISTERING 중 받은 이벤트는 size_delta·root_total 미상.
  - size_delta: Given 위 루트, When a에 50B 덧붙임 → c 삭제 → 새 파일 e(10B) 생성·쓰기 → d를 루트 밖으로 이동 → 밖의 디렉터리(파일 2개 합 70B)를 안으로 이동, Then 각 이벤트의 size_delta가 +50, −300, (CREATE 0 또는 10, 이후 MODIFY/CLOSE_WRITE 합계 10), −(d 하위 합), +70이고 매 이벤트 root_total이 실제 `du --apparent-size` 상당 합(테스트가 lstat으로 직접 계산)과 같다.
  - 불변식: 같은 루트 이벤트를 수신 순으로 따라갈 때 known 구간에서 `root_total[n] = root_total[n-1] + size_delta[n]`. 병합된 MODIFY의 size_delta는 원시 변화 합.
  - 중복 CREATE: Given `mkdir -p a/b` 직후 b 안 파일 생성으로 합성 CREATE와 커널 CREATE가 모두 온 경우, Then 총용량은 한 번만 늘어난다(두 번째 size_delta=0).
  - 디렉터리 rename: Given 하위 파일이 있는 디렉터리 rename, Then RENAME size_delta=0, 이후 그 안 파일 수정의 size_delta가 정확(색인 경로가 따라옴).
  - overflow: Given 합성 overflow 주입 전후로 파일 크기를 바꿈, Then OVERFLOW는 미상, RESYNC_DONE의 size_delta = 재구축 총용량 − 유실 직전 총용량, 그 뒤 root_total이 실제 합과 같다.
  - 미상: Given 루트 DELETE_SELF, Then 그 이벤트와 이후 get_root_total은 미상(FALSE).
  - G8 메모리: 10만 파일(디렉터리 1만) fixture 재귀 등록 전후 RSS 차이와 등록 소요 시간을 기록(DESIGN 메모리 §4 예산 16MiB, 측정 전 PASS 금지).
  - 수명 (E10): Given 이벤트 폭주 중, When fs_mon_watcher_free, Then 반환 후 콜백 없음, ASan·TSan 오류 없음.
  - 정규화 단위 테스트: 원시 `struct inotify_event` 버퍼(버퍼 경계에 걸친 이벤트, cookie 짝, 모르는 wd)를 넣어 기대 이벤트 열과 비교한다.
  - G8: `tests/integration/test_mon_stress_*.c`로 초당 1000 이벤트 10초, 최종 디렉터리 상태와 수신 이벤트로 재구성한 상태가 일치하거나 gap이 표시됨. 수치는 TESTING §5 기준 성능 형식으로 기록(측정 전 PASS 금지).
- 필수 테스트 / 게이트: G1, G2, G3, G4, G5, G6, G8
- 예상 위험 / 차단 조건: 실제 커널 overflow·UNMOUNT는 root 없이 재현이 어렵다(합성으로 검증, 실제는 NOT_RUN 기록). WSL에서 Windows 경로(/mnt/c)는 inotify가 동작하지 않으므로 Linux 파일 시스템 임시 경로에서만 테스트. 타이밍 의존 테스트는 조건 대기로 안정화.

## R1-T05 탐색 로직 (nav)

- Task ID / 요구사항 ID: R1-T05 탐색 로직 / NAV-001, NAV-002, NAV-003, NAV-004, NAV-005, NAV-010, NAV-013, NAV-014 (NAV-006 멀티 탭은 ui 몫)
- 상태: VERIFY (Codex 구현 완료, `semicolon213/design-and-r0`에 병합, 개발자 게이트 통과. 독립 QA는 R1 묶음 QA에서) 커밋 47f8824
- 목표: `src/nav` 공개 API(DESIGN 인터페이스 §5)를 구현한다: 비동기 디렉터리 목록, 이동 이력, breadcrumb 분해, 자동완성, 홈, 즐겨찾기 저장.
- 제외 범위: 위젯(ui 몫), 세션 복원(NAV-015), 최근 경로·파일(NAV-011/012), GTK 북마크 가져오기(미결 U-7).
- 기준 브랜치 / SHA: R1-T01 통합 커밋
- 선행 작업: R1-T01
- 허용 파일: `src/nav/**`, `tests/nav/**`
- 변경 금지: 루트 CMake·Makefile·scripts, 다른 모듈, docs, harness, 디자인 파일
- 참조 계약: DESIGN 인터페이스 §0·§1(FsEntry)·§5, ENGINEERING 보안 §1, ENGINEERING 오류, SRS 탐색 공통 계약
- 수용 조건 (Given/When/Then):
  - NAV-001/014: Given 파일 3개(숨김 1개), 디렉터리 1개, 심볼릭 링크 1개, 끊긴 링크 1개, When list_dir(show_hidden=FALSE), Then 숨김 제외, 링크는 type=SYMLINK에 target_type 올바름, 끊긴 링크는 is_broken_link. show_hidden=TRUE면 숨김 포함. "."과 ".."은 없음.
  - Given 권한 000 디렉터리, Then NULL + PERMISSION_DENIED. Given 이미 취소된 cancellable, Then CANCELLED.
  - NAV-013: dirs_only=TRUE면 디렉터리와 디렉터리를 가리키는 링크만.
  - NFR-003 준비: Given 1만 파일 fixture, When list_dir, Then 성공과 소요 시간 기록(목표 1초, 기록만 하고 판정은 G8).
  - NAV-002: push A, B, C → back=B, back=A, back=NULL, forward=B, push D → forward 불가. 같은 경로 연속 push는 무시. 101번째 push에서 가장 오래된 항목 삭제. parent("/")=NULL.
  - NAV-003: breadcrumb("/home/u/p") = ["/", "/home", "/home/u", "/home/u/p"], breadcrumb("/") = ["/"]. 비 UTF-8 이름 바이트 보존.
  - NAV-004: Given 디렉터리 "proj", "prof", 파일 "pro.txt", When complete("<tmp>/pro"), Then 디렉터리 두 개만 이름순, max_results 준수.
  - NAV-010: Given 임시 XDG_CONFIG_HOME, When add 2개 → save → 다시 load, Then 같은 순서. 중복 add → EXISTS. 깨진 파일 load → NULL + 오류, 파일 내용 불변.
  - NAV-005: 테스트 프로세스 시작 시 HOME을 임시 경로로 두면 fs_nav_home()이 그 값.
- 필수 테스트 / 게이트: G1, G2, G4, G5
- 예상 위험 / 차단 조건: g_get_home_dir의 HOME 캐시 동작. 비 UTF-8 파일 이름 fixture 생성.

## R1-T06 메인 창 통합 (ui)

- Task ID / 요구사항 ID: R1-T06 메인 창 통합 / NAV-001~006, NAV-010, NAV-013, NAV-014, FS-001~014, FS-018, MON-017, JOB-003, JOB-008, SEC-002(확인 화면), DASH-001(세션 집계), DASH-002(변경량 1분봉), DASH-003(이벤트 로그), DASH-005(로그 칩·경로 거르기), 헤더 지표, UI P0(UI-001/005/007/011/012/015/016/018~022/025)의 화면 연결
- 상태: BACKLOG (R1-T03·T04·T05·T07 완료 후 착수. U-1/4/5/6/9 결정 완료, PROGRESS 미결 사항 결정 기록 참조)
- 목표: `src/ui`와 `src/main.c`로 메인 창을 만들고 nav/fs/mon/job/dash 공개 API를 연결한다. 외부 파일 변경이 새로고침 없이 목록·이벤트 로그·1분봉에 반영되고(MON-017), 파일 작업은 진행률·취소와 함께 수행된다.
- 시각 기준: 루트 DESIGN.md(승인)와 docs/design/prototype/main-window.html(하단 패널 [이벤트 로그 | 변경량 · 1분봉] 탭 포함). 루트 DESIGN.md "GTK3 구현 메모"의 위젯 대응, 색 토큰 CSS, 틱 보간, 모션 감소 규칙을 그대로 따른다. 색·간격·글꼴·레이아웃·아이콘을 새로 정하지 않는다. 시안에 없는 화면(U-6 목록)은 결정 U-6대로 루트 DESIGN.md 토큰·규칙 안에서 구현하고 코드에 `/* design-pending: U-6 */` 표시를 달아 묶음 QA에서 화면을 확인받는다.
- 제외 범위: 변경 내용 패널(R2 이력 의존), 트레이·요약 창과 창 닫은 뒤 감시 유지(R2), 일시 정지(R2), 검색, 라이트 테마, 1분봉 시간 범위 변경(DASH-006, P1).
- 기준 브랜치 / SHA: R1-T03·T04·T05·T07 통합 커밋
- 선행 작업: R1-T03, R1-T04, R1-T05, R1-T07(dash) (R1-T02는 T03 경유)
- 허용 파일: `src/ui/**`, `src/main.c`, `tests/ui/**`, `scripts/e2e-*.sh`
- 변경 금지: 루트 CMake·Makefile·check-architecture.sh, 다른 모듈, `src/app_info.*`, docs, harness, `PRODUCT.md`, 루트 `DESIGN.md`, `.impeccable/**`, `docs/design/prototype/**`
- 참조 계약: DESIGN 인터페이스 전체(특히 §6 dash, §7 ui), DESIGN 스레드 §1·§4, DESIGN 상태(표시 규칙), DESIGN 이벤트 §6·§7·§8, ADR-0006, ARCHITECTURE §7(dash 배치), ENGINEERING 보안 §3·§4, ENGINEERING 접근성, 루트 DESIGN.md Components·States·GTK3 구현 메모
- 수용 조건 (Given/When/Then): Xvfb, HOME·XDG 임시 경로, 테스트 fixture 안에서만.
  - NAV: Given 앱 시작, Then 홈 목록 표시. 폴더 더블클릭/Enter로 이동, Alt+←/→/↑로 뒤로/앞으로/상위, breadcrumb 항목 클릭으로 이동. 읽을 수 없는 폴더로 이동하면 기존 목록 유지 + 권한 오류 상태. Ctrl+T 새 탭, 탭마다 독립 이력. Ctrl+H 숨김 토글. 즐겨찾기 추가 후 재시작해도 사이드바에 남음. 트리 노드 펼치기는 비동기.
  - MON-017 (E01): Given 감시 루트 안 폴더를 보는 중, When 외부 `touch`·쓰기·`rm`·`mv`, Then 2초 안에 행 추가·크기 갱신·삭제 표시(취소선 후 제거)·이름 변경이 목록에 반영되고 루트 DESIGN.md Tick 규칙 적용, 모션 감소 설정이면 플래시 없음. 이벤트 수신→모델 반영 지연을 로그로 남김(NFR-002 측정 기록).
  - 변동 열 (U-4): Given 목록에 보이는 파일, When 외부에서 1.2KB 덧붙임 → 41KB 잘라냄, Then 변동 열이 이벤트 size_delta 그대로 `▲ +1.2 KB` → `▼ −41.0 KB`(직전 이벤트 대비, U+2212). 생성은 `● 신규`, 삭제는 `✕ 삭제`, 이동·속성은 보합 표기. size_delta 미상이면 수치를 표시하지 않고 accessible name에 "변동 알 수 없음"(표시 형식은 U-6 규칙, QA 화면 확인). ui는 이 값을 얻으려고 파일을 다시 stat하지 않는다.
  - DASH-003/005 이벤트 로그: Given 외부 변경, Then 로그 맨 위에 시각·이벤트·경로·값(size_delta) 행 추가, 최대 500행, 칩(전체/생성·삭제/수정/이동/속성)과 경로 거르기가 원본 기록을 바꾸지 않고 표시만 거름, 행 선택 시 목록에서 해당 파일 선택.
  - DASH-001 세션 집계·헤더 지표: Given 생성 2·수정 3·삭제 1 이벤트, Then 집계 숫자가 fs_dash_get_counts와 같음. 헤더의 감시 경로 수, 최근 1분 이벤트 수(fs_dash_get_events_last_minute), 반영 지연 P95(ui가 모델 반영 직후 fs_dash_record_latency로 기록한 값)가 표시됨. 표본 없으면 지표는 비움.
  - DASH-002 변경량 1분봉: Given 하단 패널 "변경량 · 1분봉" 탭, Then 최근 30분 봉(fs_dash_get_candles(30))을 GtkDrawingArea + cairo로 그림. 봉 색은 종가>시가 up, 종가<시가 down, 같으면 vol-mid(시안), 이벤트 없는 분은 봉 없음, 거래량 막대는 생성·수정·삭제를 쌓음, 오른쪽 축과 현재가 표시, 키보드 ←/→로 분 선택 시 시·고·저·종과 생성·수정·삭제 수를 툴팁과 accessible description으로 제공, Esc로 해제. 외부에서 파일을 키우고 줄이면 같은 분 봉의 고가·저가에 반영됨. level_known=FALSE인 분은 봉을 그리지 않고 거래량만. "새 창으로"는 같은 FsDash를 보는 별도 GtkWindow를 열고 두 창이 함께 갱신됨. 탭 전환은 ←/→ 키로도 가능(시안).
  - 감시 상태: 감시하지 않는 폴더 → 조용한 띠 + "감시 시작"; 루트 삭제 → 반전 띠 + stale 목록 + "상위 폴더로 이동"; RESYNC_DONE → 조용한 overflow 띠와 목록 다시 읽기(E03).
  - FS: 새 파일, 새 폴더, 이름 변경(F2), 휴지통(Delete), 영구 삭제(Shift+Delete, 확인 화면 뒤 CONFIRMED), 복사·잘라내기·붙여넣기(Ctrl+C/X/V), 다중 선택, 기본 앱 열기. 붙여넣기 충돌은 fs_ops_find_conflicts 결과로 정책을 고른 뒤 제출. 휴지통 미지원은 오류 표시, 자동 영구 삭제 없음. 텍스트 편집 저장 충돌은 경고, 무단 덮어쓰기 없음(E07).
  - JOB: 진행 중 작업의 진행률(미확정이면 indeterminate)과 취소 버튼, 취소 시 부분 결과 표시, 실행 중에도 목록 탐색 입력이 응답(NFR-015).
  - 접근성: 위 동작 모두 키보드로 가능, 보이는 포커스, 버튼·행 accessible name, 상태를 색 외 기호·문구로 구분.
  - 종료 (E10): 복사 작업·이벤트 폭주 중 창 닫기 → DESIGN 스레드 §4 순서로 정상 종료, ASan 누수 없음(lsan.supp 범위 밖).
  - 기존 `--smoke` 동작 유지(`make e2e` 통과).
- 필수 테스트 / 게이트: G1, G2, G3, G4, G5, G6, G7 (+ 실제 Ubuntu 수동 검증 기록: 캡처, 키보드 탐색, 빈·로딩·오류 상태)
- 예상 위험 / 차단 조건: 시안 없는 화면(U-6)과 R1 UI 범위(U-1) 미결. GtkTreeView 1만 행 갱신 성능. Xvfb와 실제 Wayland 차이. 글꼴·기호 렌더링은 실제 Ubuntu에서 재확인(루트 DESIGN.md §4).

## R1-T07 dash 데이터 모델

- Task ID / 요구사항 ID: R1-T07 dash 데이터 모델 / DASH-001(세션 집계), DASH-002(변경량 1분봉), 헤더 지표(최근 1분 이벤트, 반영 지연 P95)
- 상태: IN_PROGRESS (Codex, 일시 중지: Codex 사용량 한도. 재개 시 이어서 진행)
- 목표: `src/dash` FsDash를 DESIGN 인터페이스 §6대로 구현한다. GTK·파일 I/O·스레드 없이 시간을 인자로 받아 1분봉(시·고·저·종, 생성·수정·삭제 거래량, 링 버퍼 ≥30분)과 집계를 계산한다. 화면은 R1-T06.
- 제외 범위: 차트 그리기(ui), 시간 범위 변경(DASH-006), 영속 저장(R2), 이상 활동(DASH-015).
- 기준 브랜치 / SHA: R1-T01 통합 커밋(FsEvent size_delta/root_total getter 포함, PROGRESS 미결 사항 E-9)
- 선행 작업: R1-T01 (루트 CMake의 add_subdirectory 목록에 dash 포함 필요, E-9)
- 허용 파일: `src/dash/**`, `tests/dash/**`
- 변경 금지: 루트 CMake·Makefile·scripts, 다른 모듈, docs, harness, 디자인 파일
- 참조 계약: DESIGN 인터페이스 §0·§1(FsEvent getter)·§6, DESIGN 이벤트 §1·§8, ARCHITECTURE §7(dash 배치), ADR-0006, DESIGN 메모리 §4
- 수용 조건 (Given/When/Then): 테스트는 `fs_event_new_internal`로 만든 이벤트와 고정 시각을 쓴다.
  - 봉: Given 루트 1 기준값 1000B(분 0 시작), When 분 0 안에서 root_total 1200 → 900 → 1100 이벤트, Then 분 0 봉 = 시 1000, 고 1200, 저 900, 종 1100. 분 1 첫 이벤트 전 tick이면 분 1 시가 1100, has_events=FALSE.
  - 다중 루트: Given 루트 A 500, B 300, When A 이벤트 root_total 600, Then 수준 900 (합). remove_root(B)는 수준 600으로 바뀌고 거래량은 그대로.
  - 거래량: Given CREATE 2(합성 1 포함), MODIFY 3(merged_count와 무관하게 3건), DELETE 1, CLOSE_WRITE 4, RENAME 1, Then creates 2, modifies 3, deletes 1. 세션 집계는 moves 1 포함.
  - 미상: Given root_total 미상 이벤트, Then 그 분 level_known=FALSE, 거래량은 셈. 다음 known 이벤트부터 수준 회복.
  - OVERFLOW가 있던 분은 has_gap=TRUE.
  - 링 버퍼: capacity 10 요청 → 30으로 올림. 90분 흘린 뒤 get_candles(100)은 30개(가장 오래된 것부터), 메모리 증가 없음.
  - 시계 역행: 현재 분보다 과거 observed_at_us 이벤트는 현재 분에 들어감.
  - 지표: 최근 60초 경계(59.9초 포함, 60.1초 제외) 이벤트 수, 지연 표본 1..1024개 P95가 정렬 기준 값과 같음, 표본 없으면 FALSE.
- 필수 테스트 / 게이트: G1, G2, G4, G5
- 예상 위험 / 차단 조건: R1-T01에 FsEvent 신규 필드·getter와 dash 하위 디렉터리 포함이 없으면 BLOCKED(E-9). 분 경계는 UTC 60초 기준이라 30분 단위 시간대 오프셋 지역에서도 화면 분 표시와 맞는지 QA 확인.
