# 아키텍처

상태: R1 계약 제안 (ARCH-001, 2026-11-24). ADR-0001(승인), ADR-0002~0005(ARCH-001 승인 범위), ADR-0006(제안, 변경량 1분봉), ADR-0007(승인, C++17 + gtkmm-3.0), ADR-0008(C++ 관용 규칙)을 따른다. R2 모듈(his, diff, search, trash)과 트레이는 R2 계약 제안(2026-11-25, ADR-0009)이다.

Modular Monolith + Event-Driven + Layered. 단일 프로세스, GTK 메인 루프, inotify 전용 수신 스레드, bounded 전달 큐, 작업 큐 worker. SQLite 단일 writer는 R2에서 도입한다(DESIGN DB 스키마). 이벤트 전달은 관측 결과 전달용이며 명령 실행의 필수 경로가 아니다. 외부 서비스는 쓰지 않는다. 구현 언어는 C++17, GUI는 gtkmm-3.0(GTK3)이다.

## 0. 처리 흐름

파일 작업은 application command → job scheduler → filesystem adapter → 결과 이벤트 순서. watcher는 커널 이벤트 → normalizer → dispatcher → UI/history/index 순서. UI는 모델 갱신을 batch 처리한다. 초기 scan과 watch 등록 사이 변경은 재검증한다. 모듈별 내부 설계는 공개 계약 확정 후 작성한다.

## 1. 계층과 의존 방향

```
app (src/main.cpp)          조립, 수명 관리, 종료 순서
  └─ ui (src/ui)            gtkmm 위젯, 모델, 사용자 확인
       ├─ nav (src/nav)     목록 읽기, 이력, 경로 분해, 자동완성, 즐겨찾기
       ├─ fs  (src/fs)      파일 작업 (생성·이름 변경·복사·이동·삭제·휴지통·텍스트 저장)
       │    └─ job (src/job)  작업 큐, 진행률, 취소
       ├─ mon (src/mon)     inotify 감시, 이벤트 정규화·병합·전달, 루트별 크기 색인
       ├─ dash (src/dash)   세션 집계, 변경량 1분봉 링 버퍼, 헤더 지표 (GTK 없음)
       ├─ his (src/his)     R2: SQLite 이력, 블롭 저장소, 자동 버전, 복원·복구 (fs 사용)
       ├─ diff (src/diff)   R2: 줄 Diff, 이미지 비교, 내용 종류 판별 (순수 함수, GTK 없음)
       ├─ search (src/search) R2: 이름·확장자 재귀 검색 (기능 ID SRC)
       ├─ trash (src/trash) R2: 홈 휴지통 목록·복원·삭제 (fs, job 사용)
       └─ core (src/core)   오류 모델, 경로 검증, 이벤트 타입, 공통 타입
```

규칙 (scripts/check-architecture.sh로 검사, G4). "GLib"은 C GLib/GIO와 C++ 바인딩 glibmm/giomm, sigc++를 함께 뜻한다. "GTK"는 gtk/gdk C 헤더와 gtkmm/gdkmm을 함께 뜻한다.

| 모듈 | 의존 가능 | 금지 |
|---|---|---|
| core | GLib(glibmm, GIO 오류 상수) | GTK, sigc++ 신호, 다른 모든 모듈 |
| dash | core, GLib | GTK, job, fs, mon, nav, ui |
| job | core, GLib/GIO(giomm), sigc++ | GTK, fs, mon, nav, dash, ui |
| fs | core, job, GLib/GIO(giomm) | GTK, mon, nav, dash, ui |
| mon | core, GLib(glibmm), sigc++ | GTK, fs, job, nav, dash, ui |
| nav | core, GLib/GIO(giomm), sigc++ | GTK, fs, job, mon, dash, ui |
| diff (R2) | core, GLib | GTK(gdk-pixbuf 포함), job, fs, mon, nav, dash, his, search, trash, ui |
| his (R2) | core, fs, GLib/GIO(giomm), sigc++, SQLite(`sqlite3.h`) | GTK, job, mon, nav, dash, diff, search, trash, ui |
| search (R2) | core, GLib/GIO(giomm), sigc++ | GTK, job, fs, mon, nav, dash, his, diff, trash, ui |
| trash (R2) | core, job, fs, GLib/GIO(giomm), sigc++ | GTK, mon, nav, dash, his, diff, search, ui |
| ui | core, job, fs, mon, nav, dash, his, diff, search, trash, GTK3(gtkmm-3.0), libayatana-appindicator3 | 모듈 내부 헤더 |
| app | 모든 공개 헤더 | 모듈 내부 헤더 |

- 다른 모듈의 공개 헤더(`src/<m>/<m>.hpp`, core는 `src/core/*.hpp` 중 `_internal.hpp`가 아닌 것)만 include한다. 내부 헤더는 `src/<m>/*_internal.hpp`로 이름 짓고 같은 모듈 안에서만 쓴다.
- `src/mon/mon_testing.hpp`처럼 `_testing.hpp`로 끝나는 헤더는 tests/에서만 include한다.
- 순환 의존 금지. mon은 UI를 호출하지 않는다(신호로만 전달).
- 기존 `src/app_info`(`filescope_info`)는 `app_info.hpp/.cpp`로 옮긴다(R0-002). app만 사용한다.
- SQLite(`sqlite3.h`)는 `src/his/`(R2)만 include한다. UI와 다른 모듈은 DB에 직접 접근하지 않고 his 공개 헤더를 거친다(check-architecture.sh 검사).
- (R2) libayatana-appindicator3(`libayatana-appindicator/`)는 `src/ui/`만 include한다. diff는 이미지를 디코드하지 않는다(ui가 `Gdk::Pixbuf`로 디코드해 RGBA 버퍼를 넘긴다).
- (R2) mon과 his는 서로 모른다. ui가 메인 스레드에서 mon 신호를 받아 his를 부른다(DESIGN 이벤트 §10). 사용자 파일 쓰기는 fs 함수만 한다(his 복원, trash 삭제 포함).

## 2. 디렉터리 배치

공개 헤더를 `include/`로 따로 빼지 않는다(ADR-0008: 설치할 라이브러리가 아니고, 모듈 디렉터리 하나가 작업 하나의 허용 파일이 되는 ADR-0004 소유권을 유지한다).

```
src/
  main.cpp, app_info.{hpp,cpp}   app
  core/  error.{hpp,cpp}  path.{hpp,cpp}  types.hpp  event.{hpp,cpp}  fd.{hpp,cpp}  CMakeLists.txt
  job/   job.hpp  job_queue.cpp  job_internal.hpp  CMakeLists.txt
  fs/    fs.hpp  ops_*.cpp  ops_internal.hpp  CMakeLists.txt
  mon/   mon.hpp  mon_testing.hpp  watcher.cpp  normalizer.cpp  registry.cpp  mon_internal.hpp  CMakeLists.txt
  nav/   nav.hpp  listing.cpp  history.cpp  bookmarks.cpp  complete.cpp  CMakeLists.txt
  dash/  dash.hpp  dash.cpp  CMakeLists.txt
  diff/  diff.hpp  (R2)  CMakeLists.txt
  his/   his.hpp  his_testing.hpp  (R2, DB·블롭·캡처 파일 나눔은 작업이 정함)  CMakeLists.txt
  search/ search.hpp  (R2)  CMakeLists.txt
  trash/ trash.hpp  (R2)  CMakeLists.txt
  ui/    ui.hpp  (창·모델·위젯 파일은 UI 작업이 정함)  CMakeLists.txt
tests/
  test_app_info.cpp              app_info
  core/ job/ fs/ mon/ nav/ dash/ diff/ his/ search/ trash/ ui/  단위 테스트 (모듈별 CMakeLists.txt)
  integration/                   실제 임시 파일 시스템 시나리오 (파일 이름 접두사로 소유 구분: fs_*, mon_*, his_*, search_*, trash_*)
  support/                       공용 fixture 도우미 test_support.{hpp,cpp} (R0-002 소유)
```

모듈 안의 `.cpp` 파일 나눔은 예시이며 구현 작업이 정한다. 공개 헤더 이름과 위치는 고정이다.

## 3. 빌드 타깃

모든 타깃은 C++17(`CMAKE_CXX_STANDARD 17`, 확장 없음)이다. C 소스는 없다.

| 타깃 | 종류 | 소스 | 링크 (pkg-config) |
|---|---|---|---|
| filescope_info | STATIC | src/app_info.cpp | - |
| filescope_core | STATIC | src/core/*.cpp | glibmm-2.4, gio-2.0 |
| filescope_job | STATIC | src/job/*.cpp | core, giomm-2.4, sigc++-2.0, Threads |
| filescope_fs | STATIC | src/fs/*.cpp | core, job, giomm-2.4 |
| filescope_mon | STATIC | src/mon/*.cpp | core, glibmm-2.4, sigc++-2.0, Threads |
| filescope_nav | STATIC | src/nav/*.cpp | core, giomm-2.4, sigc++-2.0 |
| filescope_dash | STATIC | src/dash/*.cpp | core |
| filescope_diff (R2) | STATIC | src/diff/*.cpp | core |
| filescope_his (R2) | STATIC | src/his/*.cpp | core, fs, giomm-2.4, sigc++-2.0, sqlite3, Threads |
| filescope_search (R2) | STATIC | src/search/*.cpp | core, giomm-2.4, sigc++-2.0, Threads |
| filescope_trash (R2) | STATIC | src/trash/*.cpp | core, job, fs, giomm-2.4, sigc++-2.0 |
| filescope_ui | STATIC | src/ui/*.cpp | core, job, fs, mon, nav, dash, gtkmm-3.0, R2: diff, his, search, trash, ayatana-appindicator3-0.1 |
| filescope | 실행 파일 | src/main.cpp | info, gtkmm-3.0, (있으면) ui |
| filescope_test_support | STATIC (테스트 전용) | tests/support/*.cpp | glibmm-2.4, giomm-2.4 |

- 모든 타깃은 루트 `filescope_configure_target()`(경고=오류, sanitizer)을 적용한다.
- `target_include_directories(<lib> PUBLIC ${PROJECT_SOURCE_DIR}/src)`로 include 경로는 `"core/error.hpp"`, `"mon/mon.hpp"` 형식이다.
- 루트 `CMakeLists.txt`·`Makefile`·`scripts/`·`tests/support/`는 단일 소유자(R0-002, 이후 변경은 Coordinator 배정)다. 루트는 `src/<m>/CMakeLists.txt`, `tests/<m>/CMakeLists.txt`가 있을 때만 `add_subdirectory`하고(m = core, job, fs, mon, nav, dash, R2의 diff, his, search, trash, 그리고 ui. 의존 순서대로), `filescope_ui` 타깃이 있으면 실행 파일에 링크한다. 각 모듈 작업은 자기 하위 CMakeLists.txt만 소유한다(ADR-0004). R2 루트 변경(모듈 목록, `pkg_check_modules` sqlite3·ayatana-appindicator3-0.1, check-architecture.sh 규칙)은 R2-T01이 맡는다.

## 4. 테스트 디렉터리 규칙

- 단위 테스트: `tests/<m>/test_<m>_<주제>.cpp`, GLib `g_test`(C++에서 그대로 사용, ADR-0008), 테스트 경로 이름은 `/<m>/<주제>/<사례>`. ctest 이름 `<m>_<주제>`, 라벨 `unit`.
- 통합 테스트: `tests/integration/test_<m>_<주제>.cpp`, ctest 라벨 `integration`. `make integration`은 `ctest --preset debug -L integration --no-tests=error`.
- UI 테스트: `tests/ui/`(Xvfb, 라벨 `gui`), `make e2e`가 실행한다.
- 모든 파일 테스트는 `g_dir_make_tmp()`(또는 `Glib::dir_make_tmp()`)로 만든 경로 안에서만 쓰고 끝나면 그 경로만 지운다. 실제 홈·시스템 경로를 바꾸지 않는다. `HOME`, `XDG_CONFIG_HOME`, `XDG_DATA_HOME`은 테스트 프로세스에서 임시 경로로 바꾼다(휴지통 테스트 포함).
- 감시 테스트는 이벤트 대기에 고정 sleep 대신 메인 루프를 돌리며 조건+제한 시간(기본 2초)으로 기다린다(`tests/support`).

## 5. 런타임 구성

| 스레드 | 소유 | 하는 일 |
|---|---|---|
| main | GTK, app | 위젯, 모든 공개 신호 수신, 작은 동기 파일 작업 |
| mon watcher (Watcher당 1, `std::thread`) | mon | inotify read, 정규화, 병합, 배치 전달(`Glib::Dispatcher`) |
| job worker (1, `std::thread`) | job | 대기열 작업 실행 |
| GIO 작업 스레드 | nav, trash, ui | 디렉터리 목록·자동완성 (GTask), R2: 휴지통 목록, 변경 내용 패널 계산 |
| his (R2, 1, `std::thread`) | his | SQLite 단일 연결, 이벤트 기록, 캡처, 정리, 조회, 복원 |
| search worker (R2, 1, `std::thread`) | search | 디렉터리 순회 검색 |

자세한 내용은 DESIGN 스레드. 오류 모델은 ENGINEERING 오류, 공개 선언은 DESIGN 인터페이스, 미결 사항(U-n, E-n)은 PROGRESS 미결 사항.

## 6. 단계별 도입

R1: core, job, fs, mon, nav, dash, ui. R2: his(SQLite, 자동 버전·복원), diff(줄 Diff·이미지 비교), search(기능 ID SRC, 디렉터리 이름은 `search`), trash(휴지통 관리), ui 트레이·요약 창·변경 내용 패널. pre 모듈은 R3로 미룬다(R2의 PRE-001/003은 fs::read_text/write_text와 ui 편집기로 충분하다). 변경 내용 패널과 Diff는 R2 이력에 의존한다. libarchive, GtkSourceView4, VTE, Poppler, libgit2는 해당 기능 릴리스에서 도입한다.

## 7. 모듈 경계 (전체 모듈)

모든 모듈은 공개 헤더를 통해서만 호출하며 내부 구조체와 DB 연결을 공유하지 않는다. 공통 core는 기능 모듈에 의존하지 않는다. watcher는 UI를 호출하지 않는다. 순환 의존 금지. 모듈 배치·의존은 §1(ADR-0004)이 정본이며 fs는 job 공개 헤더에 의존할 수 있다.

| 모듈 | 책임 |
|---|---|
| nav | 탐색 |
| fs | 파일 작업 |
| mon | 감시 |
| dash | 대시보드 |
| his | 이력 |
| diff | 비교 계산 (R2, 이력·미리보기 공용) |
| src | 검색 (디렉터리 `src/search`) |
| pre | 미리보기 |
| meta | 속성 |
| disk | 디스크 |
| trash | 휴지통 |
| arc | 압축 |
| job | 작업 큐 |
| dev | 개발 도구 |
| sec | 보안 |
| auto | 자동화 |
| back | 백업 |
| sys | 장치 |
| ui | UI/UX |
| cfg | 설정 |

### R1 dash 배치 결정 (2026-11-24, 변경량 1분봉 추가, ADR-0006)
- 1분봉 집계(링 버퍼 30분 이상, R1은 60분), 세션 집계(DASH-001), 최근 1분 이벤트 수·반영 지연 P95(헤더 지표)는 ui가 아니라 `src/dash`(`filescope_dash`)에 둔다.
- 이유: 메인 창 하단 패널과 "새 창으로" 떼어 낸 차트 창이 같은 데이터를 봐야 하므로 위젯 밖에 모델이 있어야 하고, 봉 계산(시·고·저·종, 분 경계, 링 버퍼)은 GTK·Xvfb 없이 시간을 고정해 단위 테스트해야 한다.
- 의존: dash → core만. mon을 직접 구독하지 않고, ui가 mon `signal_events` 배치를 받아 `dash::Dash::add_events`로 넘긴다. dash는 GTK·파일 I/O·스레드를 쓰지 않는다(main 단일 스레드 객체).
- ui는 dash 결과를 그리기만 한다(차트는 `Gtk::DrawingArea` + `Cairo::Context`, 스타일은 루트 DESIGN.md와 시안).

## 8. ADR과 옛 문서 이름

결정 기록은 `docs/adr/`(ADR-0001~0009)에 있다. ADR 본문은 작성 당시 문서 이름을 그대로 쓰므로 아래 표로 새 위치를 찾는다. ADR-0002~0006의 C 함수·타입 이름은 DESIGN 인터페이스 §0 대응표로 C++ 이름을 찾는다.

| 옛 이름 | 새 위치 |
|---|---|
| INTERFACE_SPEC, EVENT_SPEC, THREADING_MODEL, STATE_MACHINE, MEMORY_MANAGEMENT, DATA_MODEL, DATABASE_SCHEMA, docs/architecture/DESIGN.md | docs/DESIGN.md 인터페이스·이벤트·스레드·상태·메모리·데이터 모델·DB 스키마 부분 (§0 처리 흐름은 이 문서) |
| ARCHITECTURE, MODULE_BOUNDARIES | 이 문서 (§7) |
| ERROR_HANDLING, SECURITY, CODING_RULES, BUILD, DEPENDENCIES, MEMORY_THREAD_RULES, ACCESSIBILITY, DESIGN_OWNERSHIP | docs/ENGINEERING.md (디자인 권한은 AGENTS.md) |
| PRD, ROADMAP / SRS, FEATURE_SPEC, NFR | docs/PRD.md / docs/SRS.md |
| TEST_PLAN, TEST_CASES, PERFORMANCE_TEST, BENCHMARK_BASELINE, FAULT_INJECTION, QUALITY_GATES, DEFINITION_OF_DONE, REVIEW_CHECKLIST | docs/TESTING.md |
| OPEN_QUESTIONS, TRACEABILITY | harness/PROGRESS.md |
| WORKFLOW, AGENT_ROLES, WORKTREE_RULES, FAILURE_POLICY, CONTEXT_MAP, prompts, templates | harness/WORKFLOW.md (CONTEXT_MAP은 AGENTS.md 표) |
| tasks/*.md | harness/TASKS.md |
| EVAL_SPEC | harness/EVALS.md |
