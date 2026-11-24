# 아키텍처

상태: R1 계약 제안 (ARCH-001, 2026-11-24). ADR-0001(승인), ADR-0002~0005(ARCH-001 승인 범위), ADR-0006(제안, 변경량 1분봉)을 따른다.

Modular Monolith + Event-Driven + Layered. 단일 프로세스, GTK 메인 루프, inotify 전용 수신 스레드, bounded 전달 큐, 작업 큐 worker. SQLite 단일 writer는 R2에서 도입한다(DESIGN DB 스키마). 이벤트 전달은 관측 결과 전달용이며 명령 실행의 필수 경로가 아니다. 외부 서비스는 쓰지 않는다.

## 0. 처리 흐름

파일 작업은 application command → job scheduler → filesystem adapter → 결과 이벤트 순서. watcher는 커널 이벤트 → normalizer → dispatcher → UI/history/index 순서. UI는 모델 갱신을 batch 처리한다. 초기 scan과 watch 등록 사이 변경은 재검증한다. 모듈별 내부 설계는 공개 계약 확정 후 작성한다.

## 1. 계층과 의존 방향

```
app (src/main.c)            조립, 수명 관리, 종료 순서
  └─ ui (src/ui)            GTK 위젯, 모델, 사용자 확인
       ├─ nav (src/nav)     목록 읽기, 이력, 경로 분해, 자동완성, 즐겨찾기
       ├─ fs  (src/fs)      파일 작업 (생성·이름 변경·복사·이동·삭제·휴지통·텍스트 저장)
       │    └─ job (src/job)  작업 큐, 진행률, 취소
       ├─ mon (src/mon)     inotify 감시, 이벤트 정규화·병합·전달, 루트별 크기 색인
       ├─ dash (src/dash)   세션 집계, 변경량 1분봉 링 버퍼, 헤더 지표 (GTK 없음)
       └─ core (src/core)   오류 도메인, 경로 검증, 이벤트 타입, 공통 타입
```

규칙 (scripts/check-architecture.sh로 검사, G4):

| 모듈 | 의존 가능 | 금지 |
|---|---|---|
| core | GLib | GTK, 다른 모든 모듈 |
| dash | core, GLib | GTK, job, fs, mon, nav, ui |
| job | core, GLib/GIO | GTK, fs, mon, nav, dash, ui |
| fs | core, job, GLib/GIO | GTK, mon, nav, dash, ui |
| mon | core, GLib | GTK, fs, job, nav, dash, ui |
| nav | core, GLib/GIO | GTK, fs, job, mon, dash, ui |
| ui | core, job, fs, mon, nav, dash, GTK3 | 모듈 내부 헤더 |
| app | 모든 공개 헤더 | 모듈 내부 헤더 |

- 다른 모듈의 공개 헤더(`src/<m>/fs_<m>.h`, core는 `src/core/*.h` 전부)만 include한다. 내부 헤더는 `src/<m>/*_internal.h`로 이름 짓고 같은 모듈 안에서만 쓴다.
- `src/mon/fs_mon_testing.h`처럼 `_testing.h`로 끝나는 헤더는 tests/에서만 include한다.
- 순환 의존 금지. mon은 UI를 호출하지 않는다(콜백으로만 전달).
- 기존 `src/app_info.{c,h}`(`filescope_info`)는 그대로 둔다. app만 사용한다.
- SQLite(`sqlite3.h`)는 `src/his/`(R2)만 include한다. UI와 다른 모듈은 DB에 직접 접근하지 않고 his 공개 헤더를 거친다(check-architecture.sh 검사).

## 2. 디렉터리 배치

```
src/
  main.c, app_info.{c,h}         app (기존)
  core/  fs_error.{h,c}  fs_path.{h,c}  fs_event.{h,c}  fs_types.h  CMakeLists.txt
  job/   fs_job.h  job_queue.c  job_internal.h  CMakeLists.txt
  fs/    fs_ops.h  ops_*.c  ops_internal.h  CMakeLists.txt
  mon/   fs_mon.h  fs_mon_testing.h  watcher.c  normalizer.c  registry.c  mon_internal.h  CMakeLists.txt
  nav/   fs_nav.h  listing.c  history.c  bookmarks.c  complete.c  CMakeLists.txt
  dash/  fs_dash.h  dash.c  CMakeLists.txt
  ui/    fs_ui.h  (창·모델·위젯 파일은 UI 작업이 정함)  CMakeLists.txt
tests/
  test_app_info.c                기존
  core/ job/ fs/ mon/ nav/ dash/ ui/  단위 테스트 (모듈별 CMakeLists.txt)
  integration/                   실제 임시 파일 시스템 시나리오 (파일 이름 접두사로 소유 구분: fs_*, mon_*)
  support/                       공용 fixture 도우미 (core 작업 소유)
```

## 3. 빌드 타깃

| 타깃 | 종류 | 소스 | 링크 |
|---|---|---|---|
| filescope_info | STATIC (기존) | src/app_info.c | - |
| filescope_core | STATIC | src/core/*.c | GLib |
| filescope_job | STATIC | src/job/*.c | core, GIO |
| filescope_fs | STATIC | src/fs/*.c | core, job, GIO |
| filescope_mon | STATIC | src/mon/*.c | core, GLib, Threads |
| filescope_nav | STATIC | src/nav/*.c | core, GIO |
| filescope_dash | STATIC | src/dash/*.c | core, GLib |
| filescope_ui | STATIC | src/ui/*.c | core, job, fs, mon, nav, dash, GTK3 |
| filescope | 실행 파일 | src/main.c | info, ui |
| filescope_test_support | STATIC (테스트 전용) | tests/support/*.c | GLib |

- 모든 타깃은 루트 `filescope_configure_target()`(경고=오류, sanitizer)을 적용한다.
- `target_include_directories(<lib> PUBLIC ${PROJECT_SOURCE_DIR}/src)`로 include 경로는 `"core/fs_error.h"`, `"mon/fs_mon.h"` 형식이다.
- 루트 `CMakeLists.txt`는 단일 소유자(R1-T01)다. 루트는 `src/<m>/CMakeLists.txt`, `tests/<m>/CMakeLists.txt`가 있을 때만 `add_subdirectory`하고(m = job, fs, mon, nav, dash, ui), `filescope_ui` 타깃이 있으면 실행 파일에 링크한다. 각 모듈 작업은 자기 하위 CMakeLists.txt만 소유한다(ADR-0004).

## 4. 테스트 디렉터리 규칙

- 단위 테스트: `tests/<m>/test_<m>_<주제>.c`, GLib `g_test`, 테스트 경로 이름은 `/<m>/<주제>/<사례>`. ctest 이름 `<m>_<주제>`, 라벨 `unit`.
- 통합 테스트: `tests/integration/test_<m>_<주제>.c`, ctest 라벨 `integration`. `make integration`은 `ctest --preset debug -L integration --no-tests=error`(R1-T01이 구현).
- UI 테스트: `tests/ui/`(Xvfb, 라벨 `gui`), `make e2e`가 실행한다.
- 모든 파일 테스트는 `g_dir_make_tmp()`로 만든 경로 안에서만 쓰고 끝나면 그 경로만 지운다. 실제 홈·시스템 경로를 바꾸지 않는다. `HOME`, `XDG_CONFIG_HOME`, `XDG_DATA_HOME`은 테스트 프로세스에서 임시 경로로 바꾼다(휴지통 테스트 포함).
- 감시 테스트는 이벤트 대기에 고정 sleep 대신 메인 루프를 돌리며 조건+제한 시간(기본 2초)으로 기다린다(`tests/support`).

## 5. 런타임 구성

| 스레드 | 소유 | 하는 일 |
|---|---|---|
| main | GTK, app | 위젯, 모든 공개 콜백 수신, 작은 동기 파일 작업 |
| mon watcher (FsWatcher당 1) | mon | inotify read, 정규화, 병합, 배치 전달 |
| job worker (1) | job | 대기열 작업 실행 |
| GIO 작업 스레드 | nav | 디렉터리 목록·자동완성 (GTask) |

자세한 내용은 DESIGN 스레드. 오류 모델은 ENGINEERING 오류, 공개 시그니처는 DESIGN 인터페이스, 미결 사항(U-n, E-n)은 PROGRESS 미결 사항.

## 6. 단계별 도입

R1: core, job, fs, mon, nav, dash, ui. R2: his(SQLite, 스냅샷), src(검색), pre(미리보기), trash(휴지통 관리 화면). 변경 내용 패널과 Diff는 R2 이력에 의존한다. libarchive, GtkSourceView4, VTE, Poppler, libgit2는 해당 기능 릴리스에서 도입한다.

## 7. 모듈 경계 (전체 모듈)

모든 모듈은 공개 헤더를 통해서만 호출하며 내부 구조체와 DB 연결을 공유하지 않는다. 공통 core는 기능 모듈에 의존하지 않는다. watcher는 UI를 호출하지 않는다. 순환 의존 금지. 모듈 배치·의존은 §1(ADR-0004)이 정본이며 fs는 job 공개 헤더에 의존할 수 있다.

| 모듈 | 책임 |
|---|---|
| nav | 탐색 |
| fs | 파일 작업 |
| mon | 감시 |
| dash | 대시보드 |
| his | 이력 |
| src | 검색 |
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
- 의존: dash → core만. mon을 직접 구독하지 않고, ui가 mon 이벤트 배치를 받아 `fs_dash_add_events`로 넘긴다. dash는 GTK·파일 I/O·스레드를 쓰지 않는다(main 단일 스레드 객체).
- ui는 dash 결과를 그리기만 한다(차트는 GtkDrawingArea + cairo, 스타일은 루트 DESIGN.md와 시안).

## 8. ADR과 옛 문서 이름

결정 기록은 `docs/adr/`(ADR-0001~0006)에 있다. ADR 본문은 작성 당시 문서 이름을 그대로 쓰므로 아래 표로 새 위치를 찾는다.

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
