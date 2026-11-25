# 작업 목록

작업 지시서의 정본이다. 상태 흐름과 작업 지시서 양식은 harness/WORKFLOW.md, 요구사항 연결은 harness/PROGRESS.md 추적표를 따른다.

## 등록 규칙

- 작업마다 harness/WORKFLOW.md §7 "작업 지시서" 양식으로 이 문서에 절을 추가한다. 상태는 harness/WORKFLOW.md §2를 따른다. 첫 작업은 설계 계약 확정, 다음은 빌드 및 검증 명령 구현이다. 선행 계약 없는 기능 작업은 READY로 지정하지 않는다.
- 상태 값은 Coordinator만 바꾼다. 아래 요약 표와 각 절의 "상태" 줄을 함께 고친다.

## 공통 지시 (모든 작업)

- 언어·GUI: C++17 + gtkmm-3.0(ADR-0007). C++ 관용 규칙은 ADR-0008, 코딩 규칙은 docs/ENGINEERING.md, 공개 선언은 docs/DESIGN.md 인터페이스가 정본이다.
- 빌드·테스트는 WSL `Ubuntu-24.04`에서 `make <target>`으로 한다(AGENTS.md "명령").
- Codex는 문서(docs/, harness/, AGENTS.md, CLAUDE.md, README.md)를 고치지 않는다. 계약과 다르게 해야 하면 구현을 멈추지 말고 차이를 인수인계에 보고하거나, 진행할 수 없으면 BLOCKED로 보고한다.
- 커밋 메시지에 `Co-Authored-By` 줄을 넣지 않는다.
- C 구현 참고: 이전 C 코드는 커밋 `b0da73b`에 있다(`git show b0da73b:src/job/fs_job.c` 등). 동작 참고용이며 계약은 docs/DESIGN.md C++ 판이 정본이다. 옛 C 이름 대응은 DESIGN 인터페이스 §0 표.

## 상태 요약 (2026-11-24)

| 작업 | 상태 | 선행 |
|---|---|---|
| R0-001 빌드·테스트 하네스 (C) | 대체됨 → R0-002 | - |
| R0-002 C++ 전환 (빌드 하네스) | APPROVED (b7ca5d3, R1 QA PASS 8c469a9) | ADR-0007, 의존성 설치 |
| R1-T01 core C++ 포팅 | APPROVED (1bdb270, R1 QA PASS 8c469a9) | R0-002 |
| R1-T02 job C++ 포팅 | APPROVED (91ba40b, R1 QA PASS 8c469a9) | R1-T01 |
| R1-T03 fs | APPROVED (74eb5ae, R1 QA PASS 8c469a9) | R1-T01, R1-T02 |
| R1-T04 mon | APPROVED (4cc1116, R1 QA PASS 8c469a9) | R1-T01 |
| R1-T05 nav C++ 포팅 | APPROVED (2eab368, R1 QA PASS 8c469a9) | R1-T01 |
| R1-T06 ui | APPROVED (a148ce3, R1 QA PASS 8c469a9) | R1-T03, R1-T04, R1-T05, R1-T07 |
| R1-T07 dash | APPROVED (08424c7, R1 QA PASS 8c469a9) | R1-T01 |

의존 순서: R0-002 → R1-T01 → {R1-T02, R1-T04, R1-T05, R1-T07 병렬} → R1-T03(T02 필요) → R1-T06. 독립 QA는 R1 전체를 묶어 한 번 한다(R0-002 포함, PROGRESS 미결 사항 E-6).

파일 소유권: 루트 빌드 파일·scripts·tests/support·tests/integration/CMakeLists.txt·app 진입점은 R0-002, `src/<m>/**`·`tests/<m>/**`는 해당 모듈 작업, `tests/integration/test_<m>_*.cpp`는 해당 모듈 작업이 갖는다. 겹치지 않는다. 다른 작업의 파일을 바꿔야 하면 Coordinator에 보고한다.

## R0-001 빌드·테스트 하네스 (C, 대체됨)

- 상태: 대체됨(ADR-0007, 2026-11-24). C17 하네스(121ef2f)는 R0-002가 C++로 다시 만든다. 독립 QA 대상에서 빠진다.

## R0-002 C++ 전환 (빌드 하네스)

- Task ID / 요구사항 ID: R0-002 / 빌드·테스트 하네스 (ENGINEERING 빌드·의존성·코딩 규칙, TESTING §1 품질 게이트 G1~G7 골격), ADR-0007
- 상태: BLOCKED (선행 조건: 사용자가 WSL Ubuntu-24.04에 `libgtkmm-3.0-dev`를 설치했다고 확인. 확인되면 READY)
- 목표: C17 빌드 하네스를 C++17 + gtkmm-3.0으로 바꾼다. 루트 CMake(`LANGUAGES CXX`), 프리셋, Makefile 대상(이름·의미 그대로), clang-format/clang-tidy C++ 설정, 아키텍처 검사, 테스트 지원 코드, `Gtk::Application` 기반 앱 진입점(창 제목 FileScope, `--smoke`)과 Xvfb smoke.
- 제외 범위: core/job/fs/mon/nav/dash/ui 모듈 구현(각 포팅·구현 작업), 시각 디자인.
- 기준 브랜치 / SHA: semicolon213/design-and-r0 @ 이 계약 커밋
- 선행 작업: ADR-0007 승인(완료), `libgtkmm-3.0-dev` 설치(사용자, sudo)
- 허용 파일: `CMakeLists.txt`(루트, 단독 소유), `CMakePresets.json`, `Makefile`, `.clang-format`, `.clang-tidy`, `.gitignore`, `.github/workflows/**`, `scripts/**`, `src/main.c`(삭제) → `src/main.cpp`, `src/app_info.{c,h}`(삭제) → `src/app_info.{hpp,cpp}`, `tests/test_app_info.c`(삭제) → `tests/test_app_info.cpp`, `tests/test_architecture.cpp`(신규), `tests/support/**`, `tests/integration/CMakeLists.txt`. 삭제만 허용: `src/core/**`, `src/job/**`, `src/nav/**`, `tests/core/**`, `tests/job/**`, `tests/nav/**`(이전 C 모듈. C++ 빌드에서 컴파일되지 않으므로 지우고, 각 포팅 작업이 새로 만든다)
- 변경 금지: `docs/**`, `harness/**`, `AGENTS.md`, `CLAUDE.md`, `README.md`, `PRODUCT.md`, 루트 `DESIGN.md`, `.impeccable/**`, `docs/design/prototype/**`
- 참조 계약: ENGINEERING 빌드·의존성·코딩 규칙, ARCHITECTURE §1~4, TESTING §1, ADR-0004, ADR-0007, ADR-0008, DESIGN 인터페이스 §7(app·`--smoke`)
- 구현 메모:
  - 루트 CMake: `project(filescope VERSION 0.1.0 LANGUAGES CXX)`, C++17 필수·확장 없음, `CMAKE_EXPORT_COMPILE_COMMANDS ON`. pkg-config IMPORTED_TARGET: gtkmm-3.0, glibmm-2.4, giomm-2.4, sigc++-2.0, glib-2.0, gio-2.0, gtk+-3.0. `filescope_configure_target()`(`-Wall -Wextra -Wpedantic -Werror`, sanitizer, UBSan은 `-fno-sanitize-recover=undefined`)와 `filescope_add_test()`(TSan `setarch -R`) 유지. `foreach(m core job fs mon nav dash ui)`로 `src/<m>/CMakeLists.txt`, `tests/<m>/CMakeLists.txt`가 있을 때만 add_subdirectory. `TARGET filescope_ui`가 있으면 실행 파일에 링크.
  - app: `src/main.cpp`가 `Gtk::Application::create(kAppId)`, `--smoke` 옵션(창 제목을 표준 출력에 찍고 idle에서 종료), activate에서 제목 FileScope 창. ui 모듈이 생기기 전에는 main.cpp가 빈 `Gtk::ApplicationWindow`를 쓴다. `app_info.hpp`는 `namespace filescope`에 `kAppId = "io.github.filescope"`, `kAppName = "FileScope"`, `app_name()`, `app_version()`.
  - Makefile: 대상 이름과 의미 그대로(`build test format-check lint architecture asan ubsan tsan e2e integration stress benchmark verify`). `format-check`·`lint`는 `src`, `tests`의 `*.cpp`·`*.hpp` 전부. `integration`/`stress`/`benchmark`는 `ctest --preset debug -L <라벨> --no-tests=error` 유지(테스트 없으면 비0).
  - `.clang-format`: LLVM 기반, 들여쓰기 4, 100열, `Standard: c++17`. `.clang-tidy`: ENGINEERING 코딩 규칙의 검사 묶음, `WarningsAsErrors: '*'`, `HeaderFilterRegex: '^(src|tests)/'`.
  - `scripts/check-architecture.sh`: `*.cpp`·`*.hpp` 검사. ARCHITECTURE §1 표의 금지 include(모듈 디렉터리, `gtk/`, `gdk/`, `gtkmm`, `gdkmm`, core의 `sigc++`), `_internal.hpp`/`_testing.hpp` 규칙, `sqlite3.h`는 `src/his/`만.
  - `tests/test_architecture.cpp`: 임시 디렉터리 사본에 위반(예: `src/core`가 `mon/mon.hpp` 또는 `gtkmm.h` include)을 넣고 스크립트가 비0으로 끝나는지 확인(이전 `test_core_architecture.c`를 옮김).
  - `tests/support`: `test_support.{hpp,cpp}`, `namespace filescope::test`. 임시 디렉터리 생성·재귀 정리(RAII 클래스, 소멸 시 정리), HOME/XDG 임시 경로 설정·복원, "메인 루프를 돌리며 조건 대기(제한 시간)" 도우미(`bool wait_until(const std::function<bool()>&, unsigned timeout_ms)`). g_test와 함께 쓴다.
  - `scripts/lsan.supp`는 glibmm/gtkmm 전역 할당 항목만 추가할 수 있다(항목마다 이유 주석). `scripts/e2e-smoke.sh`는 그대로 쓰거나 C++ 실행 파일 경로만 맞춘다.
- 수용 조건 (Given/When/Then):
  - Given `libgtkmm-3.0-dev`가 설치된 Ubuntu 24.04, When `make build`, Then 종료 코드 0, 프로젝트 경고 0, 저장소에 `*.c`/`*.h` 소스 없음.
  - When `make test`, Then `app_info`와 `architecture` g_test가 실제로 실행되어 성공(라벨 `unit`).
  - When `make e2e`, Then Xvfb에서 `Gtk::Application` 앱이 뜨고 출력이 정확히 `FileScope`이며 종료 코드 0.
  - When `make format-check lint architecture`, Then 위반 0. 의도적 위반 사본에서는 `architecture`가 비0.
  - When `make asan ubsan tsan`, Then 별도 빌드 디렉터리에서 실행되어 오류 0.
  - When `make integration`, `make stress`, `make benchmark`(테스트 없음), Then 비0 종료(PASS로 처리하지 않음).
  - When `make verify`, Then 위 게이트가 차례로 통과.
- 필수 테스트 / 게이트: G1, G2, G4, G5, G6, G7(smoke)
- 예상 위험 / 차단 조건: `libgtkmm-3.0-dev` 미설치면 시작하지 않고 BLOCKED. sigc++ 2.10의 함수형 표기(`sigc::signal<void(int)>`) 컴파일 여부를 smoke 코드나 테스트 하나로 확인해 보고(DESIGN 인터페이스 §0). glibmm/gtkmm 헤더가 `-Wpedantic`·clang-tidy 경고를 내면 SYSTEM include로 막고, 검사를 끄는 경우 이유와 함께 보고. TSan과 glibmm 전역 초기화의 억제 필요 여부.
- 공통: WSL Ubuntu-24.04에서 빌드·테스트, Codex 문서 수정 금지, 커밋에 Co-Authored-By 금지.

## R1-T01 core C++ 포팅

- Task ID / 요구사항 ID: R1-T01 core C++ 포팅 / SEC-001, SEC-003(경로 검증·보호), 공통 기반(오류 모델, Event, Entry, UniqueFd)
- 상태: BACKLOG (R0-002 병합 후 READY). 이전 C 구현(a3b7e0f, 7d90a5e, 병합 1de4d68)은 R0-002에서 지워지고 이 작업이 C++로 다시 만든다.
- 목표: `src/core`(error, path, types, fd, event)를 DESIGN 인터페이스 §1, DESIGN 이벤트 §1대로 C++로 구현한다. 수용 조건은 C 판과 같다.
- 제외 범위: job/fs/mon/nav/dash/ui 구현, SEC-002(fs 작업 몫), 루트 빌드 파일(R0-002 소유), 시각 디자인.
- 기준 브랜치 / SHA: R0-002 통합 커밋
- 선행 작업: R0-002
- 허용 파일: `src/core/**`, `tests/core/**`
- 변경 금지: 루트 CMake·Makefile·CMakePresets·scripts, `tests/support/**`, `src/main.cpp`, `src/app_info.*`, `src/{job,fs,mon,nav,dash,ui}/**`, docs, harness, 디자인 파일
- 참조 계약: ARCHITECTURE §1~4, DESIGN 인터페이스 §0~1, DESIGN 이벤트 §1, DESIGN 메모리, ENGINEERING 오류·보안·코딩 규칙, ADR-0002, ADR-0004, ADR-0008
- 구현 메모: `src/core/CMakeLists.txt`가 `filescope_core`(STATIC, include `src`, glibmm-2.4·gio-2.0 링크)를 정의하고 `filescope_configure_target()`을 적용한다. `tests/core/CMakeLists.txt`는 테스트 파일마다 실행 파일을 만들고 `filescope_core`, `filescope_test_support`에 링크, 라벨 `unit`.
- 수용 조건 (Given/When/Then):
  - Given errno 표의 각 값, When `core::error_code_from_errno`, Then ENGINEERING 오류 §2 대응과 일치. `error_from_errno(ENOENT, "open", p)`의 message가 `"open: <p 표시형>: <strerror>"` 형식.
  - Given `Gio::Error(Gio::Error::NOT_SUPPORTED, "x")`(또는 `G_IO_ERROR_NOT_SUPPORTED`의 `Glib::Error`), When `core::error_from_glib`, Then NOT_SUPPORTED이고 원본 message 보존. ENGINEERING 오류 §1 대응표의 나머지 코드와 다른 도메인(→FAILED)도 확인.
  - Given `Result<int>`와 `Result<void>`, Then 성공·실패 생성, `ok()`, `value()`, `error()`가 계약대로 동작(실패 Result의 `value()`는 테스트하지 않음: abort 계약).
  - Given 이름 "", "a/b", ".", "..", 내부 NUL 포함 이름, 256바이트 이름, When `core::validate_name`, Then 각각 INVALID_PATH 또는 NAME_TOO_LONG. Given "notes.md", Then 성공.
  - Given 상대 경로·내부 NUL 경로·PATH_MAX 이상 경로, When `core::validate_absolute`, Then 실패와 해당 코드.
  - Given "/usr/bin/x"(WRITE), "/"(REMOVE), HOME 경로(REMOVE), "/run/user/<uid>/a"(WRITE), 임시 디렉터리 하위(REMOVE), When `core::check_protected`, Then 앞 셋 PROTECTED_PATH, 뒤 둘 성공. READ는 모두 성공.
  - Given "/a//b/./c/../d", When `core::canonicalize`, Then "/a/b/d". `core::is_ancestor("/a/b", "/a/bc")`는 false.
  - Given `UniqueFd`, Then 소멸·`reset`·이동 대입에서 fd가 정확히 한 번 닫힌다(`fcntl(F_GETFD)`로 확인), `release` 뒤에는 닫지 않는다.
  - Given `EventPtr` 1개를 두 스레드가 복사·해제 10만 회, Then TSan·ASan 오류 없음과 마지막 해제에서 소멸(소멸 감지는 `std::weak_ptr`).
  - `core::to_string(EventKind::RESYNC_DONE)` == "RESYNC_DONE" 등 모든 kind.
- 필수 테스트 / 게이트: G1, G2, G4, G5, G6(EventPtr 공유 테스트)
- 예상 위험 / 차단 조건: 루트 빌드와 support는 R0-002 소유이므로 필요하면 보고만 한다. HOME 보호 테스트는 HOME을 임시 경로로 바꿔 실행.
- 공통: WSL Ubuntu-24.04에서 빌드·테스트, Codex 문서 수정 금지, 커밋에 Co-Authored-By 금지.

## R1-T02 job C++ 포팅

- Task ID / 요구사항 ID: R1-T02 job C++ 포팅 / JOB-001, JOB-002, JOB-003, JOB-008
- 상태: BACKLOG (R1-T01 병합 후 READY). 이전 C 구현(87e7761, 병합 49daab4)을 C++로 다시 만든다.
- 목표: `src/job` `job::JobQueue`를 DESIGN 인터페이스 §2, DESIGN 상태 §2, DESIGN 스레드대로 구현한다(동시 실행 1, FIFO, 10Hz 진행, 취소, 항목 결과, `std::thread` worker, `Glib::Dispatcher` 전달, sigc++ 신호).
- 제외 범위: 일시정지·재개(JOB-006/007), 우선순위·동시성 설정(JOB-009/010), 이력(JOB-011), 재시도(JOB-012), 파일 작업 본문(fs 몫).
- 기준 브랜치 / SHA: R1-T01 통합 커밋
- 선행 작업: R1-T01
- 허용 파일: `src/job/**`, `tests/job/**`
- 변경 금지: 루트 CMake·Makefile·scripts, `tests/support/**`, 다른 모듈, docs, harness, 디자인 파일
- 참조 계약: DESIGN 인터페이스 §0·§2, DESIGN 상태 §2, DESIGN 스레드 §1~5, DESIGN 메모리, ENGINEERING 오류 §4, ADR-0008
- 수용 조건 (Given/When/Then):
  - Given 작업 A, B를 차례로 제출, When 메인 루프 실행, Then `signal_state` 순서 A:QUEUED, B:QUEUED, A:RUNNING, A:DONE, B:RUNNING, B:DONE (B는 A 종료 전 RUNNING이 되지 않음). QUEUED는 `submit` 반환 전에 동기로 온다.
  - Given 본문이 worker에서 실행 중, When 메인 루프 반복, Then 메인 컨텍스트가 막히지 않는다(본문 실행 중 idle 처리기가 돈다).
  - Given 100ms 동안 진행을 1만 번 보고하는 본문, Then `signal_progress`는 2회 이하 + 종료 전 마지막 1회이고 마지막 값이 최종 값과 같다. bytes_total=-1이면 그대로 전달.
  - Given QUEUED 작업, When cancel, Then 즉시 CANCELLED(error CANCELLED), 본문 미실행, 본문 캡처 소멸 1회(메인 스레드).
  - Given cancellable을 확인하는 RUNNING 본문, When cancel, Then CANCELLED 하나만 통지되고 그 전 항목 결과는 `results()`로 조회된다.
  - Given 항목 3개 중 1개 실패를 기록하고 성공을 반환한 본문, Then FAILED + PARTIAL.
  - Given 실행 중 작업과 대기 작업, When `shutdown()`(또는 소멸), Then 반환 후 신호 없음, 모든 본문 캡처가 메인 스레드에서 소멸, 누수 없음.
  - Given shutdown 진행 중(본문 캡처의 소멸자 안에서), When submit, Then Error CANCELLED이고 넘긴 본문은 그 호출 안에서 소멸.
  - Given 예외를 던지는 본문(계약 위반 방어), Then 프로그램이 끝나지 않고 FAILED.
- 필수 테스트 / 게이트: G1, G2, G4, G5, G6
- 예상 위험 / 차단 조건: Dispatcher를 main에서 만들고 소멸해야 하는 glibmm 규칙과 합류 순서(TSan 확인). sigc++ 신호를 worker에서 만지지 않기.
- 공통: WSL Ubuntu-24.04에서 빌드·테스트, Codex 문서 수정 금지, 커밋에 Co-Authored-By 금지.

## R1-T03 파일 작업 (fs)

- Task ID / 요구사항 ID: R1-T03 파일 작업 / FS-001, FS-002, FS-004, FS-005, FS-006, FS-007, FS-008, FS-009, FS-010, FS-012, FS-014, FS-018, SEC-002 (FS-003·011·013은 ui 몫, API 지원만)
- 상태: BACKLOG (R1-T01·T02 병합 후 READY). 이전 C 시도는 중단·폐기(ADR-0007). C++ 계약으로 새로 시작한다.
- 목표: `src/fs` 공개 API(DESIGN 인터페이스 §3)를 ADR-0005 규칙으로 구현한다: 동기 생성·이름 변경·휴지통·텍스트 읽기/저장, 작업 큐 기반 복사·이동·일괄 휴지통·영구 삭제, 충돌 정책, 확인(`Confirmation::CONFIRMED`).
- 제외 범위: 실행 취소(FS-019), 일괄 이름 변경(FS-015), 휴지통 관리 화면(R2), UI.
- 기준 브랜치 / SHA: R1-T02 통합 커밋
- 선행 작업: R1-T01, R1-T02
- 허용 파일: `src/fs/**`, `tests/fs/**`, `tests/integration/test_fs_*.cpp`
- 변경 금지: 루트 CMake·Makefile·scripts, `tests/support/**`, 다른 모듈(core 포함, core 변경이 필요하면 Coordinator에 보고), docs, harness, 디자인 파일
- 참조 계약: DESIGN 인터페이스 §0·§3, ENGINEERING 보안 §1~4, ENGINEERING 오류, DESIGN 메모리 §3, DESIGN 상태 §2, ADR-0005, ADR-0008
- 수용 조건 (Given/When/Then): 모든 경로는 `g_dir_make_tmp` 아래, HOME/XDG는 임시 경로.
  - FS-001/002: Given 빈 디렉터리, When `create_file` "a.txt" / `create_dir` "d", Then 생성되고 절대 경로 반환. 같은 이름 다시 → EXISTS, 기존 내용 불변. 쓰기 권한 없는 디렉터리 → PERMISSION_DENIED. 이름 "a/b" → INVALID_PATH, 파일 미생성.
  - FS-008: Given a.txt와 b.txt, When `rename` a.txt→b.txt, Then EXISTS, 두 파일 내용 불변.
  - FS-004/005 (E07): Given 텍스트 파일 읽기 후 외부에서 내용 변경, When `write_text`(expected=읽은 stamp), Then CONFLICT, 외부 내용 유지. 변경 없을 때 저장은 성공, 모드 유지, 디렉터리에 `.filescope-*` 잔여 없음. 4MiB 초과 파일 read → TOO_LARGE. expected 없음 + `Confirmation::NONE` → NOT_CONFIRMED.
  - FS-006: Given 임시 XDG_DATA_HOME, When `trash`, Then 원본 사라지고 휴지통 files 디렉터리에 존재. 휴지통 미지원 경로(재현 불가하면 NOT_RUN 사유 기록) → NOT_SUPPORTED이고 원본 유지.
  - FS-007/014: Given 파일 3개, When `delete_permanently`(NONE), Then Error NOT_CONFIRMED(제출 안 됨), 3개 유지. CONFIRMED면 DONE, 모두 삭제. 그중 1개가 쓰기 권한 없는 디렉터리 안이면 FAILED(PARTIAL), 나머지 삭제, 항목 결과 3개.
  - FS-009/012: Given 하위 디렉터리·심볼릭 링크·FIFO가 있는 트리, When `copy`, Then 구조·내용·권한 비트·mtime 일치, 링크는 링크로, FIFO는 항목 실패(NOT_SUPPORTED), 상태 FAILED(PARTIAL).
  - FS-010: Given 같은 장치 이동, Then rename으로 처리(ino 유지). 디렉터리를 자기 하위로 이동 → 제출 시 INVALID_PATH.
  - FS-018: Given 대상에 같은 이름 존재, When 정책 FAIL / SKIP / KEEP_BOTH / OVERWRITE(CONFIRMED), Then 각각 EXISTS 실패 / skipped / "a (2).txt" 생성 / 대상 교체. OVERWRITE에 CONFIRMED 없음 → NOT_CONFIRMED. 디렉터리→파일 덮어쓰기는 항목 실패.
  - 취소 (E06): Given 64MiB 파일 복사 중, When cancel, Then CANCELLED, 대상 디렉터리에 부분 파일·임시 파일 없음, 원본 유지.
  - 보호 경로: Given "/usr/x"에 `create_file` 또는 "/" `delete_permanently`(CONFIRMED), Then PROTECTED_PATH, 시스템에 쓰기 시도 없음.
  - EXDEV 이동: 테스트 환경에 다른 장치(tmpfs 등)가 있으면 통합 테스트, 없으면 NOT_RUN 사유 기록(PASS 아님).
- 필수 테스트 / 게이트: G1, G2, G3, G4, G5, G6
- 예상 위험 / 차단 조건: ENOSPC 재현(작은 tmpfs는 root 필요하므로 쓰기 실패 주입 지점을 내부 함수로 두고 단위 테스트, 실제 재현은 NOT_RUN 기록). 휴지통 GIO 동작이 XDG 경로·마운트에 따라 다름. 테스트가 root로 돌면 권한 테스트 skip 처리 확인. `Gio::File::trash`가 던지는 `Glib::Error`를 경계에서 잡기.
- 공통: WSL Ubuntu-24.04에서 빌드·테스트, Codex 문서 수정 금지, 커밋에 Co-Authored-By 금지.

## R1-T04 감시 (mon)

- Task ID / 요구사항 ID: R1-T04 감시 / MON-001, 002, 003, 006, 008, 009, 010, 011, 012, 013, 014, 015, 016, 018, 019, 020, 021, 027, 028, 029, DASH-002·U-4 데이터 원천(루트 크기 색인, size_delta/root_total) (MON-017은 전달 계약까지, 화면 반영은 R1-T06)
- 상태: BACKLOG (R1-T01 병합 후 READY). 이전 C 시도는 중단·폐기(ADR-0007). C++ 계약으로 새로 시작한다.
- 목표: `src/mon` `mon::Watcher`를 DESIGN 인터페이스 §4, DESIGN 이벤트 전체(§8 크기 색인 포함), DESIGN 상태 §1, DESIGN 스레드대로 구현한다. 루트별 크기 색인을 재귀 등록·재탐색 순회에서 만들고 이벤트마다 size_delta/root_total을 채우며 루트 총용량 조회를 제공한다(ADR-0006).
- 제외 범위: 읽기 계열 이벤트(MON-004/005/007), 일시정지(MON-022), 필터(MON-024~026), 자체 이벤트 구분(MON-032), 이력 저장(R2).
- 기준 브랜치 / SHA: R1-T01 통합 커밋
- 선행 작업: R1-T01
- 허용 파일: `src/mon/**`, `tests/mon/**`, `tests/integration/test_mon_*.cpp`
- 변경 금지: 루트 CMake·Makefile·scripts, `tests/support/**`, 다른 모듈(core 포함, Event 필드 변경이 필요하면 Coordinator에 보고), docs, harness, 디자인 파일
- 참조 계약: DESIGN 인터페이스 §0·§1·§4, DESIGN 이벤트 §1~9, DESIGN 상태 §1, DESIGN 스레드, DESIGN 메모리 §4(색인 예산), ADR-0003, ADR-0006, ADR-0008
- 수용 조건 (Given/When/Then): 외부 변경은 테스트가 syscall 또는 argv 기반 `Glib::spawn_sync`(`g_spawn_sync`)로 만들고, 대기는 조건 + 2초 제한.
  - E01: Given 비재귀 루트, When 파일 생성·쓰기·닫기·chmod·삭제, Then CREATE, MODIFY, CLOSE_WRITE, ATTRIB, DELETE가 이 순서로 오고 CLOSE_WRITE의 `stat->size`가 쓴 크기와 같다.
  - MON-027: Given 한 파일에 4KiB 쓰기 100회를 빠르게, Then MODIFY 이벤트 수 < 100이고 merged_count 합이 수신한 IN_MODIFY 수와 같다. 중간에 같은 경로의 다른 kind가 끼면 그 앞뒤 MODIFY는 합쳐지지 않는다(정규화 단위 테스트).
  - MON-010/028: Given 루트 안 같은 디렉터리 rename, Then RENAME 1개(old_path, path, cookie≠0). 다른 하위 디렉터리로 이동 → MOVE 1개. 루트 밖으로 이동 → 시간창(50ms) 뒤 MOVED_OUT. 밖에서 안으로 → MOVED_IN.
  - MON-018/019 (E02): Given 재귀 루트, When `mkdir -p a/b/c` 직후 c 안에 파일 생성, Then a, b, c 감시 등록(watch_count 증가)과 그 파일의 CREATE(합성 또는 커널) 수신. a를 루트 밖으로 이동하면 a 하위 watch 해제(watch_count 감소). 디렉터리 rename 뒤 그 안 파일 수정 이벤트의 path가 새 경로.
  - MON-012/013/014: Given 루트 삭제, Then DELETE_SELF 이벤트와 STOPPED(ROOT_DELETED). 루트 이동 → MOVE_SELF, STOPPED(ROOT_MOVED). 실제 UNMOUNT는 root가 필요하면 NOT_RUN 사유 기록, 정규화 단위 테스트(원시 이벤트 버퍼 입력)로 대체 검증.
  - MON-016: Given 하위 디렉터리 삭제, Then IN_IGNORED 후 registry에서 제거(watch_count 감소), 이벤트로 노출되지 않음. remove_root 뒤 늦은 이벤트는 전달되지 않음.
  - MON-015/029 (E03): Given `mon::testing::inject_overflow`, Then OVERFLOW(gap_id=n) → 상태 DEGRADED→RESYNCING→ACTIVE → RESYNC_DONE(gap_id=n). 주입 직전·직후 만든 디렉터리도 재탐색 뒤 감시된다. `set_queue_capacity(16)`로 줄이고 파일 1000개 생성 → OVERFLOW 최소 1회, OVERFLOW 없이 이벤트가 빠지는 경우 없음.
  - MON-020/021: Given 루트 2개, Then 각 이벤트의 watch_id가 맞음. 겹치는 경로 add_root → EXISTS. 없는 경로 → NOT_FOUND. remove_root → STOPPED(USER).
  - 크기 색인 (DESIGN 이벤트 §8): Given 파일 a(100B), b(200B), 하위 디렉터리 d 안 c(300B)가 있는 재귀 루트, When 등록 완료(ACTIVE), Then WatchInfo·`root_total()` = 600B, file_count 3, total_known true. REGISTERING 중 받은 이벤트는 size_delta·root_total이 nullopt.
  - size_delta: Given 위 루트, When a에 50B 덧붙임 → c 삭제 → 새 파일 e(10B) 생성·쓰기 → d를 루트 밖으로 이동 → 밖의 디렉터리(파일 2개 합 70B)를 안으로 이동, Then 각 이벤트의 size_delta가 +50, −300, (CREATE 0 또는 10, 이후 MODIFY/CLOSE_WRITE 합계 10), −(d 하위 합), +70이고 매 이벤트 root_total이 실제 `du --apparent-size` 상당 합(테스트가 lstat으로 직접 계산)과 같다.
  - 불변식: 같은 루트 이벤트를 수신 순으로 따라갈 때 둘 다 값이 있는 구간에서 `root_total[n] = root_total[n-1] + size_delta[n]`. 병합된 MODIFY의 size_delta는 원시 변화 합.
  - 중복 CREATE: Given `mkdir -p a/b` 직후 b 안 파일 생성으로 합성 CREATE와 커널 CREATE가 모두 온 경우, Then 총용량은 한 번만 늘어난다(두 번째 size_delta=0).
  - 디렉터리 rename: Given 하위 파일이 있는 디렉터리 rename, Then RENAME size_delta=0, 이후 그 안 파일 수정의 size_delta가 정확(색인 경로가 따라옴).
  - overflow: Given 합성 overflow 주입 전후로 파일 크기를 바꿈, Then OVERFLOW는 nullopt, RESYNC_DONE의 size_delta = 재구축 총용량 − 유실 직전 총용량, 그 뒤 root_total이 실제 합과 같다.
  - 미상: Given 루트 DELETE_SELF, Then 그 이벤트와 이후 `root_total()`은 nullopt.
  - G8 메모리: 10만 파일(디렉터리 1만) fixture 재귀 등록 전후 RSS 차이와 등록 소요 시간을 기록(DESIGN 메모리 §4 예산 16MiB, 측정 전 PASS 금지).
  - 수명 (E10): Given 이벤트 폭주 중, When Watcher 소멸, Then 반환 후 신호 없음, ASan·TSan 오류 없음.
  - 정규화 단위 테스트: 원시 `struct inotify_event` 버퍼(버퍼 경계에 걸친 이벤트, cookie 짝, 모르는 wd)를 넣어 기대 이벤트 열과 비교한다(입구는 `mon_testing.hpp`).
  - G8: `tests/integration/test_mon_stress_*.cpp`로 초당 1000 이벤트 10초, 최종 디렉터리 상태와 수신 이벤트로 재구성한 상태가 일치하거나 gap이 표시됨. 수치는 TESTING §5 기준 성능 형식으로 기록(측정 전 PASS 금지).
- 필수 테스트 / 게이트: G1, G2, G3, G4, G5, G6, G8
- 예상 위험 / 차단 조건: 실제 커널 overflow·UNMOUNT는 root 없이 재현이 어렵다(합성으로 검증, 실제는 NOT_RUN 기록). WSL에서 Windows 경로(/mnt/c)는 inotify가 동작하지 않으므로 Linux 파일 시스템 임시 경로에서만 테스트. 타이밍 의존 테스트는 조건 대기로 안정화. 전달 큐·Dispatcher 수명(합류 후 소멸).
- 공통: WSL Ubuntu-24.04에서 빌드·테스트, Codex 문서 수정 금지, 커밋에 Co-Authored-By 금지.

## R1-T05 nav C++ 포팅

- Task ID / 요구사항 ID: R1-T05 nav C++ 포팅 / NAV-001, NAV-002, NAV-003, NAV-004, NAV-005, NAV-010, NAV-013, NAV-014 (NAV-006 멀티 탭은 ui 몫)
- 상태: BACKLOG (R1-T01 병합 후 READY). 이전 C 구현(47f8824)을 C++로 다시 만든다.
- 목표: `src/nav` 공개 API(DESIGN 인터페이스 §5)를 구현한다: 비동기 디렉터리 목록(sigc++ 결과 슬롯), 이동 이력(`nav::History`), breadcrumb 분해, 자동완성, 홈, 즐겨찾기(`nav::Bookmarks`, `Glib::KeyFile`).
- 제외 범위: 위젯(ui 몫), 세션 복원(NAV-015), 최근 경로·파일(NAV-011/012), GTK 북마크 가져오기(미결 U-7).
- 기준 브랜치 / SHA: R1-T01 통합 커밋
- 선행 작업: R1-T01
- 허용 파일: `src/nav/**`, `tests/nav/**`
- 변경 금지: 루트 CMake·Makefile·scripts, `tests/support/**`, 다른 모듈, docs, harness, 디자인 파일
- 참조 계약: DESIGN 인터페이스 §0·§1(Entry)·§5, DESIGN 스레드 §1·§4, ENGINEERING 보안 §1, ENGINEERING 오류, SRS 탐색 공통 계약, ADR-0008
- 수용 조건 (Given/When/Then):
  - NAV-001/014: Given 파일 3개(숨김 1개), 디렉터리 1개, 심볼릭 링크 1개, 끊긴 링크 1개, When `list_dir_async`(show_hidden=false), Then 숨김 제외, 링크는 type=SYMLINK에 target_type 올바름, 끊긴 링크는 is_broken_link. show_hidden=true면 숨김 포함. "."과 ".."은 없음. 결과 슬롯은 메인 스레드에서 정확히 한 번 호출.
  - Given 권한 000 디렉터리, Then Error PERMISSION_DENIED. Given 이미 취소된 cancellable, Then CANCELLED.
  - Given 슬롯이 `sigc::trackable` 객체에 묶여 있고 완료 전에 그 객체가 소멸, Then 크래시·ASan 오류 없음(슬롯 호출 없음).
  - NAV-013: dirs_only=true면 디렉터리와 디렉터리를 가리키는 링크만.
  - NFR-003 준비: Given 1만 파일 fixture, When list_dir, Then 성공과 소요 시간 기록(목표 1초, 기록만 하고 판정은 G8).
  - NAV-002: push A, B, C → back=B, back=A, back=nullopt, forward=B, push D → forward 불가. 같은 경로 연속 push는 무시. 101번째 push에서 가장 오래된 항목 삭제. `parent("/")`=nullopt.
  - NAV-003: `breadcrumb("/home/u/p")` = {"/", "/home", "/home/u", "/home/u/p"}, `breadcrumb("/")` = {"/"}. 비 UTF-8 이름 바이트 보존.
  - NAV-004: Given 디렉터리 "proj", "prof", 파일 "pro.txt", When `complete_async("<tmp>/pro")`, Then 디렉터리 두 개만 이름순, max_results 준수.
  - NAV-010: Given 임시 XDG_CONFIG_HOME, When add 2개 → save → 다시 load, Then 같은 순서. 중복 add → EXISTS. 깨진 파일 load → Error(예외 아님), 파일 내용 불변.
  - NAV-005: 테스트 프로세스 시작 시 HOME을 임시 경로로 두면 `nav::home()`이 그 값.
- 필수 테스트 / 게이트: G1, G2, G4, G5
- 예상 위험 / 차단 조건: `Glib::get_home_dir`의 HOME 캐시 동작. 비 UTF-8 파일 이름 fixture 생성. `Glib::KeyFile`이 던지는 `Glib::Error`를 경계에서 잡기. giomm-2.4에 GTask 래퍼가 없으므로 C GTask 사용 시 결과 슬롯을 worker에서 만지지 않기.
- 공통: WSL Ubuntu-24.04에서 빌드·테스트, Codex 문서 수정 금지, 커밋에 Co-Authored-By 금지.

## R1-T06 메인 창 통합 (ui)

- Task ID / 요구사항 ID: R1-T06 메인 창 통합 / NAV-001~006, NAV-010, NAV-013, NAV-014, FS-001~014, FS-018, MON-017, JOB-003, JOB-008, SEC-002(확인 화면), DASH-001(세션 집계), DASH-002(변경량 1분봉), DASH-003(이벤트 로그), DASH-005(로그 칩·경로 거르기), 헤더 지표, UI P0(UI-001/005/007/011/012/015/016/018~022/025)의 화면 연결
- 상태: BACKLOG (R1-T03·T04·T05·T07 완료 후 착수. U-1/4/5/6/9 결정 완료, PROGRESS 미결 사항 결정 기록 참조)
- 목표: `src/ui`와 `src/main.cpp`로 gtkmm 메인 창(`ui::MainWindow`)을 만들고 nav/fs/mon/job/dash 공개 API를 연결한다. 외부 파일 변경이 새로고침 없이 목록·이벤트 로그·1분봉에 반영되고(MON-017), 파일 작업은 진행률·취소와 함께 수행된다.
- 시각 기준: 루트 DESIGN.md(승인)와 docs/design/prototype/main-window.html(하단 패널 [이벤트 로그 | 변경량 · 1분봉] 탭 포함). 루트 DESIGN.md "GTK3 구현 메모"의 위젯 대응(gtkmm 클래스로 읽기, DESIGN 인터페이스 §7), 색 토큰 CSS, 틱 보간, 모션 감소 규칙을 그대로 따른다. 색·간격·글꼴·레이아웃·아이콘을 새로 정하지 않는다. 시안에 없는 화면(U-6 목록)은 결정 U-6대로 루트 DESIGN.md 토큰·규칙 안에서 구현하고 코드에 `// design-pending: U-6` 표시를 달아 묶음 QA에서 화면을 확인받는다.
- 제외 범위: 변경 내용 패널(R2 이력 의존), 트레이·요약 창과 창 닫은 뒤 감시 유지(R2), 일시 정지(R2), 검색, 라이트 테마, 1분봉 시간 범위 변경(DASH-006, P1).
- 기준 브랜치 / SHA: R1-T03·T04·T05·T07 통합 커밋
- 선행 작업: R1-T03, R1-T04, R1-T05, R1-T07(dash) (R1-T02는 T03 경유)
- 허용 파일: `src/ui/**`, `src/main.cpp`, `tests/ui/**`, `scripts/e2e-*.sh`
- 변경 금지: 루트 CMake·Makefile·check-architecture.sh, 다른 모듈, `src/app_info.*`, docs, harness, `PRODUCT.md`, 루트 `DESIGN.md`, `.impeccable/**`, `docs/design/prototype/**`
- 참조 계약: DESIGN 인터페이스 전체(특히 §6 dash, §7 ui), DESIGN 스레드 §1·§4, DESIGN 상태(표시 규칙), DESIGN 이벤트 §6·§7·§8, ADR-0006, ADR-0008, ARCHITECTURE §7(dash 배치), ENGINEERING 보안 §3·§4, ENGINEERING 접근성, 루트 DESIGN.md Components·States·GTK3 구현 메모
- 수용 조건 (Given/When/Then): Xvfb, HOME·XDG 임시 경로, 테스트 fixture 안에서만.
  - NAV: Given 앱 시작, Then 홈 목록 표시. 폴더 더블클릭/Enter로 이동, Alt+←/→/↑로 뒤로/앞으로/상위, breadcrumb 항목 클릭으로 이동. 읽을 수 없는 폴더로 이동하면 기존 목록 유지 + 권한 오류 상태. Ctrl+T 새 탭, 탭마다 독립 이력. Ctrl+H 숨김 토글. 즐겨찾기 추가 후 재시작해도 사이드바에 남음. 트리 노드 펼치기는 비동기.
  - MON-017 (E01): Given 감시 루트 안 폴더를 보는 중, When 외부 `touch`·쓰기·`rm`·`mv`, Then 2초 안에 행 추가·크기 갱신·삭제 표시(취소선 후 제거)·이름 변경이 목록에 반영되고 루트 DESIGN.md Tick 규칙 적용, 모션 감소 설정이면 플래시 없음. 이벤트 수신→모델 반영 지연을 로그로 남김(NFR-002 측정 기록).
  - 변동 열 (U-4): Given 목록에 보이는 파일, When 외부에서 1.2KB 덧붙임 → 41KB 잘라냄, Then 변동 열이 이벤트 size_delta 그대로 `▲ +1.2 KB` → `▼ −41.0 KB`(직전 이벤트 대비, U+2212). 생성은 `● 신규`, 삭제는 `✕ 삭제`, 이동·속성은 보합 표기. size_delta가 nullopt면 수치를 표시하지 않고 accessible name에 "변동 알 수 없음"(표시 형식은 U-6 규칙, QA 화면 확인). ui는 이 값을 얻으려고 파일을 다시 stat하지 않는다.
  - DASH-003/005 이벤트 로그: Given 외부 변경, Then 로그 맨 위에 시각·이벤트·경로·값(size_delta) 행 추가, 최대 500행, 칩(전체/생성·삭제/수정/이동/속성)과 경로 거르기가 원본 기록을 바꾸지 않고 표시만 거름, 행 선택 시 목록에서 해당 파일 선택.
  - DASH-001 세션 집계·헤더 지표: Given 생성 2·수정 3·삭제 1 이벤트, Then 집계 숫자가 `Dash::counts()`와 같음. 헤더의 감시 경로 수, 최근 1분 이벤트 수(`events_last_minute`), 반영 지연 P95(ui가 모델 반영 직후 `record_latency`로 기록한 값)가 표시됨. 표본 없으면 지표는 비움.
  - DASH-002 변경량 1분봉: Given 하단 패널 "변경량 · 1분봉" 탭, Then 최근 30분 봉(`candles(30)`)을 `Gtk::DrawingArea` + `Cairo::Context`로 그림. 봉 색은 종가>시가 up, 종가<시가 down, 같으면 vol-mid(시안), 이벤트 없는 분은 봉 없음, 거래량 막대는 생성·수정·삭제를 쌓음, 오른쪽 축과 현재가 표시, 키보드 ←/→로 분 선택 시 시·고·저·종과 생성·수정·삭제 수를 툴팁과 accessible description으로 제공, Esc로 해제. 외부에서 파일을 키우고 줄이면 같은 분 봉의 고가·저가에 반영됨. level_known=false인 분은 봉을 그리지 않고 거래량만. "새 창으로"는 같은 Dash를 보는 별도 `Gtk::Window`를 열고 두 창이 함께 갱신됨. 탭 전환은 ←/→ 키로도 가능(시안).
  - 감시 상태: 감시하지 않는 폴더 → 조용한 띠 + "감시 시작"; 루트 삭제 → 반전 띠 + stale 목록 + "상위 폴더로 이동"; RESYNC_DONE → 조용한 overflow 띠와 목록 다시 읽기(E03).
  - FS: 새 파일, 새 폴더, 이름 변경(F2), 휴지통(Delete), 영구 삭제(Shift+Delete, 확인 화면 뒤 CONFIRMED), 복사·잘라내기·붙여넣기(Ctrl+C/X/V), 다중 선택, 기본 앱 열기. 붙여넣기 충돌은 `fs::find_conflicts` 결과로 정책을 고른 뒤 제출. 휴지통 미지원은 오류 표시, 자동 영구 삭제 없음. 텍스트 편집 저장 충돌은 경고, 무단 덮어쓰기 없음(E07).
  - JOB: 진행 중 작업의 진행률(미확정이면 indeterminate)과 취소 버튼, 취소 시 부분 결과 표시, 실행 중에도 목록 탐색 입력이 응답(NFR-015).
  - 접근성: 위 동작 모두 키보드로 가능, 보이는 포커스, 버튼·행 accessible name, 상태를 색 외 기호·문구로 구분.
  - 종료 (E10): 복사 작업·이벤트 폭주 중 창 닫기 → DESIGN 스레드 §4 순서로 정상 종료, ASan 누수 없음(lsan.supp 범위 밖).
  - 기존 `--smoke` 동작 유지(`make e2e` 통과).
- 필수 테스트 / 게이트: G1, G2, G3, G4, G5, G6, G7 (+ 실제 Ubuntu 수동 검증 기록: 캡처, 키보드 탐색, 빈·로딩·오류 상태)
- 예상 위험 / 차단 조건: 시안 없는 화면(U-6)과 R1 UI 범위(U-1) 미결. `Gtk::TreeView` 1만 행 갱신 성능. Xvfb와 실제 Wayland 차이. 글꼴·기호 렌더링은 실제 Ubuntu에서 재확인(루트 DESIGN.md §4). gtkmm 래퍼 없는 기능은 `gobj()`로 C API 사용.
- 공통: WSL Ubuntu-24.04에서 빌드·테스트, Codex 문서 수정 금지, 커밋에 Co-Authored-By 금지.

## R1-T07 dash 데이터 모델

- Task ID / 요구사항 ID: R1-T07 dash 데이터 모델 / DASH-001(세션 집계), DASH-002(변경량 1분봉), 헤더 지표(최근 1분 이벤트, 반영 지연 P95)
- 상태: BACKLOG (R1-T01 병합 후 READY). 이전 C 시도는 중단·폐기(ADR-0007). C++ 계약으로 새로 시작한다.
- 목표: `src/dash` `dash::Dash`를 DESIGN 인터페이스 §6대로 구현한다. GTK·파일 I/O·스레드 없이 시간을 인자로 받아 1분봉(시·고·저·종, 생성·수정·삭제 거래량, 링 버퍼 ≥30분)과 집계를 계산한다. 화면은 R1-T06.
- 제외 범위: 차트 그리기(ui), 시간 범위 변경(DASH-006), 영속 저장(R2), 이상 활동(DASH-015).
- 기준 브랜치 / SHA: R1-T01 통합 커밋
- 선행 작업: R1-T01
- 허용 파일: `src/dash/**`, `tests/dash/**`
- 변경 금지: 루트 CMake·Makefile·scripts, `tests/support/**`, 다른 모듈, docs, harness, 디자인 파일
- 참조 계약: DESIGN 인터페이스 §0·§1·§6, DESIGN 이벤트 §1·§8, ARCHITECTURE §7(dash 배치), ADR-0006, DESIGN 메모리 §4
- 수용 조건 (Given/When/Then): 테스트는 `core::make_event`로 만든 이벤트와 고정 시각을 쓴다.
  - 봉: Given 루트 1 기준값 1000B(분 0 시작), When 분 0 안에서 root_total 1200 → 900 → 1100 이벤트, Then 분 0 봉 = 시 1000, 고 1200, 저 900, 종 1100. 분 1 첫 이벤트 전 tick이면 분 1 시가 1100, has_events=false.
  - 다중 루트: Given 루트 A 500, B 300, When A 이벤트 root_total 600, Then 수준 900 (합). remove_root(B)는 수준 600으로 바뀌고 거래량은 그대로.
  - 거래량: Given CREATE 2(합성 1 포함), MODIFY 3(merged_count와 무관하게 3건), DELETE 1, CLOSE_WRITE 4, RENAME 1, Then creates 2, modifies 3, deletes 1. 세션 집계는 moves 1 포함.
  - 미상: Given root_total이 nullopt인 이벤트, Then 그 분 level_known=false, 거래량은 셈. 다음 값 있는 이벤트부터 수준 회복.
  - OVERFLOW가 있던 분은 has_gap=true.
  - 링 버퍼: capacity 10 요청 → 30으로 올림. 90분 흘린 뒤 `candles(100)`은 30개(가장 오래된 것부터), 메모리 증가 없음.
  - 시계 역행: 현재 분보다 과거 observed_at_us 이벤트는 현재 분에 들어감.
  - 지표: 최근 60초 경계(59.9초 포함, 60.1초 제외) 이벤트 수, 지연 표본 1..1024개 P95가 정렬 기준 값과 같음, 표본 없으면 nullopt.
- 필수 테스트 / 게이트: G1, G2, G4, G5
- 예상 위험 / 차단 조건: 분 경계는 UTC 60초 기준이라 30분 단위 시간대 오프셋 지역에서도 화면 분 표시와 맞는지 QA 확인.
- 공통: WSL Ubuntu-24.04에서 빌드·테스트, Codex 문서 수정 금지, 커밋에 Co-Authored-By 금지.
