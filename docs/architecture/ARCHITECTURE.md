# 아키텍처

상태: R1 계약 제안 (ARCH-001, 2026-11-24). ADR-0001(승인), ADR-0002~0005(ARCH-001 승인 범위), ADR-0006(제안, 변경량 1분봉)을 따른다.

Modular Monolith + Event-Driven + Layered. 단일 프로세스, GTK 메인 루프, inotify 전용 수신 스레드, bounded 전달 큐, 작업 큐 worker. SQLite 단일 writer는 R2에서 도입한다(DATABASE_SCHEMA.md). 이벤트 전달은 관측 결과 전달용이며 명령 실행의 필수 경로가 아니다. 외부 서비스는 쓰지 않는다.

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

자세한 내용은 THREADING_MODEL.md. 오류 모델은 ERROR_HANDLING.md, 공개 시그니처는 INTERFACE_SPEC.md, 미결 사항(U-n, E-n)은 OPEN_QUESTIONS.md.

## 6. 단계별 도입

R1: core, job, fs, mon, nav, dash, ui. R2: his(SQLite, 스냅샷), src(검색), pre(미리보기), trash(휴지통 관리 화면). 변경 내용 패널과 Diff는 R2 이력에 의존한다. libarchive, GtkSourceView4, VTE, Poppler, libgit2는 해당 기능 릴리스에서 도입한다.
