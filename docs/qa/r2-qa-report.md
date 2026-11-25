# R2 묶음 독립 QA 보고서

- Task ID: task_9254e193a780 (R2-T00~T11, R2-F01)
- 검증 SHA: a5ed03f (`git reset --keep a5ed03f`, `git log --oneline -1` = `a5ed03f chore(harness): R2-T11·F01 병합, R2 묶음 QA 착수`)
- diff 범위: `git diff 8c469a9 a5ed03f -- src tests scripts Makefile CMakeLists.txt CMakePresets.json` (78파일, +12032/−313). 새 모듈 diff·his·search·trash, fs(install_file·remove_tree, N1·N2), mon(일시 정지), ui(change_panel·tray·검색·휴지통·미리보기)
- 환경: WSL2 Ubuntu 24.04.4 LTS, 커널 6.18.33.2-microsoft-standard-WSL2, CPU 6, g++ 13.3.0, 배포판 GLib `libglib2.0-0t64 2.80.0-6ubuntu3.10`, `libayatana-appindicator3-1 0.5.93-1build3`, `LD_LIBRARY_PATH` 미설정(logs/env.txt). 작업 트리 /mnt/c(drvfs), 테스트 임시 경로는 /tmp(ext4). 억제 파일 변경 없음.
- 증거 위치: `build/qa-r2/logs/`(summary.txt, env.txt, verify.log, integration.log, stress.log, benchmark.log, g8-verbose.log, ui-interaction.log, tsan-1..10.log, shutdown200.log, exp-*.log, gui-*.log, min2-*.log, min3.log), 실험 소스 `build/qa-r2/exp/`, 화면 `build/qa-r2/screens/`

## 판정: PASS

자동화로 검증할 수 있는 범위에서는 PASS입니다.
- 필수 명령(make verify, integration, stress, benchmark, ui-interaction, tsan 10회)이 모두 종료 코드 0이었습니다.
- 위험 항목 1~9를 직접 실험으로 확인했고 차단 수준 결함은 없습니다.

다만 아래 두 가지는 Coordinator가 결정해야 합니다.
- 발견 R2-N1(Medium): 트레이 호스트가 없을 때 libayatana-appindicator 안에서 잘못된 메모리 읽기가 납니다. FileScope 로직 없이 C 40줄로도 재현되는 라이브러리 결함입니다. 일반 빌드에서는 20회 중 크래시 0회였지만 가능성을 배제할 수는 없습니다. 회피 수정은 Codex에 배정할지 결정이 필요합니다.
- 실제 Ubuntu 데스크톱(GNOME 트레이, Wayland, 한국어 글꼴, 실제 편집기 저장)은 NOT_RUN입니다. R2-T09/T10/T11 수용 조건의 "실제 Ubuntu 수동 검증 기록"은 보고서 끝 체크리스트(R1 15항목 + R2 항목)로 사용자가 채워야 합니다. 그 기록을 받은 뒤 APPROVED로 올리기를 권합니다.

## 게이트별 명령 / 종료 코드 / 증거

| 게이트 | 명령 | 종료 코드 | 증거 |
|---|---|---|---|
| G1, G2, G4~G7 | `make verify` | 0 (1035초) | verify.log: 4개 프리셋 모두 `100% tests passed, 0 tests failed out of 27`, `architecture: PASS`, 프로젝트 `warning:` 0건. e2e event-to-model 지연 18.3~21.4ms |
| G3 | `make integration` | 0 | integration.log: 6/6(test_fs_operations, test_his_persistence, test_his_versions, test_mon_events, test_search_walk, test_trash_home) |
| G8 | `make stress` | 0 | g8-verbose.log: `stress operations=10000 elapsed_s=10.007 rate=999.3 gap=no`. his: `10000 events, 1000/s for 10 s; pending peak=100, batch commit P95=5.430 ms, checkpointed DB=1466368 bytes, HIS_BACKLOG=0`, 캡처 `100 x 1 MiB in 0.706 s (141.56 MiB/s)` |
| G8 | `make benchmark` | 0 | `benchmark files=100000 directories=10000 register_s=0.251 rss_delta=10096640`, his 기준 스냅샷 `20000 files in 34.312 s (582.88 files/s), history query 1.754 ms` |
| UI 동작 | `make ui-interaction` | 0 (101초) | ui-interaction.log의 PASS 20묶음(R1 13 + R2 7) |
| G6 반복 | `TSAN_OPTIONS=halt_on_error=1:suppressions=$W/tests/tsan.supp ctest --test-dir build/tsan --output-on-failure` ×10 | 10회 모두 0 | tsan-1..10.log, 각 27/27 |
| R2-F01 | TSan 빌드 `setarch -R test_his_versions -p /his/versions/shutdown` ×200 | 실패 0 | shutdown200.log `runs=200 failures=0` |
| R2-F01 | CMakePresets | — | 네 configure preset이 `base`를 상속하고 `base`에 `CMAKE_CXX_COMPILER=g++`. 모든 빌드 로그가 g++ 13.3.0 |

## 위험 항목별 확인 (Coordinator 요청 1~9)

| # | 항목 | 방법 | 결과 | 판정 |
|---|---|---|---|---|
| 1 | his 강제 종료·블롭·SQL·data_dir·restore·recover | 코드 검토(store.cpp 전체, his.cpp 전체) + `/his/integration/crash`(자식 SIGKILL 무작위 시점 20회 + AFTER_BLOB_RENAME·AFTER_COMMIT_BEFORE_UNLINK `_exit(77)`, 매번 integrity_check·foreign_key_check·ref_count·tmp 비움·블롭 행↔파일 1:1 확인), `/his/integration/orphans`, `/his/versions/restore-recover` 실행 통과 | 블롭 경로는 `blobs/<hex 앞 2자>/<sha256 hex>`(store.cpp:749-751). SQL은 상수 문자열 + `sqlite3_bind_blob64`/`bind_int64`만 쓰고 경로 문자열을 SQL에 조합하지 않음. 쓰기는 data_dir 아래 tmp/·blobs/·history.db(+wal/shm, corrupt 격리)뿐이고 예외는 의도된 restore·recover 대상 파일. restore는 CONFIRMED가 아니면 캡처 전에 NOT_CONFIRMED(store.cpp:475). PRE_RESTORE는 최신 sha256과 다를 때만 새 버전(store.cpp:989-995, DB §5.3). recover는 `install_file(expected=nullopt)` → 대상이 있으면 EXISTS, 트랜잭션 롤백으로 삭제 표시 유지 | PASS |
| 2 | 기준 스냅샷 2만 파일 중 조회 응답 | `exp_his_latency`: 2만 파일 scan_root 동안 50ms마다 load_history_async를 보내 전 구간 측정(lint 실행과 동시) | `captured=20000 secs=71.103 queries=1406 p50=1.91ms p95=4.01ms p99=7.65ms max=39.27ms`(exp-his-latency-1.log). 단독 실행 benchmark는 34.3초 | PASS(1초 기준) |
| 3 | /his/versions/shutdown 간헐 실패 | TSan 빌드 200회 반복 | 실패 0. 수정은 테스트 쪽(기준 스냅샷 완료 보장) | PASS |
| 4 | fs::install_file·remove_tree | 코드 검토(fs.cpp:1080-1228) + `/fs/install/*`, `/fs/remove-tree/*` 통과 | expected stamp(dev·ino·size·mtime_ns)를 쓰기 전과 rename 직전 두 번 비교해 다르면 CONFLICT. 원본은 `O_NOFOLLOW` + lstat/fstat dev·ino 대조. remove_tree는 NOT_CONFIRMED·보호 경로(`/`, HOME) 거부, dirfd 순회로 링크를 따라가지 않음(링크 대상 보존 테스트 있음) | PASS |
| 5 | trash | `exp_trash`(19개 검사) + `/trash/home/*` 통과 | files/의 디렉터리 링크 영구 삭제 시 링크만 삭제, 대상 보존. 원래 경로 부모가 `/usr/bin` 링크 → PROTECTED_PATH(/proc/self/fd 해석 검사). 절대 경로 `..`로 /usr → PROTECTED_PATH. 상대 `../../../etc` → info_missing, 복원 NOT_FOUND. `%00`, info 링크, info FIFO(멈추지 않음), 20000바이트 Path 줄, 머리 없음 → info_missing. DeletionDate 깨짐 → nullopt(복원 가능). id `../x`·`a/b`·`""` → INVALID_PATH. empty(NONE) 거부. 기존 이름에 복원 → EXISTS, 양쪽 불변. 쓰기 경로는 `$XDG_DATA_HOME/Trash`만 | PASS |
| 6 | search | `exp_search` | `/mnt` 검색이 `/mnt/c`(drvfs, 다른 st_dev)로 들어가지 않음(dirs_scanned=1). 20022개: 묶음 41개, 최대 500, 최소 간격 100.1ms(10Hz 이하), 루프 링크 미추적, 비 UTF-8 이름 대소문자 무시 일치. 진행 중 새 검색 → 이전 id의 done(CANCELLED)이 새 id 결과보다 먼저, 이후 이전 id hits 없음. 검색 중 소멸 → 신호 없음 | PASS |
| 7 | ui | ui-interaction(history·tray·r2 묶음) + 화면 캡처(screens/ 25장) + 코드 대조 | 검색 띠: 진행 중 "찾는 중… · N개 찾음"만, 완료 "N개 찾음 · 폴더 N개 · 접근 불가 N개", denied_more면 "100+"(ui.cpp:1218-1333), DESIGN.md R2 화면과 일치. 되돌리기 인라인 확인은 기본 포커스 취소·Esc 닫힘·파일 불변(테스트). 트레이 메뉴 항목·순서·문구가 DESIGN "Tray Menu"와 같음(tray.cpp:115-155, test_tray). 일시 정지 요약 창 집계·최근 변경·빈 상태 opacity 0.55(tray.cpp:393-395). 미리보기 4 MiB 한도·NUL·비 UTF-8 → "텍스트로 열 수 없음"·편집기 없음(fatal-criticals 아래). 저장 충돌 [취소](기본 포커스)/[다시 읽기]/[덮어쓰기…] + 2단계 확인 [돌아가기](포커스), 충돌 중 편집 상자 0.55 | PASS(기능). 시각은 아래 UI 절 |
| 8 | 알려진 환경 문제 | gui_san.sh, min2.c(순수 C AppIndicator) | GUI TSan: a5ed03f 35~42건, R1 기준 8c469a9도 31~32건. 범주가 같고 경쟁 접근이 모두 GLib/GIO/GObject/fontconfig/pango 안(gdbus·`[pango] fontconfig`·GIO 풀 스레드)이며 프로젝트 프레임은 할당 스택에만 나옴 → R1부터 있던 비계측 시스템 라이브러리 보고(회귀 아님). AppIndicator: 아래 R2-N1 | GUI TSan은 환경 문제로 판정. AppIndicator는 R2-N1 |
| 9 | 아키텍처 | check-architecture.sh PASS + include 직접 대조 | diff: core만. his: core, fs(fs.hpp가 job.hpp를 끌고 오지만 his는 job API를 쓰지 않음), glibmm, sigc++, sqlite3. search: core, gio/giomm/glibmm, sigc++. trash: core, fs, job, gio/glibmm. `sqlite3.h`는 src/his/store.cpp만, ayatana는 src/ui/tray.cpp만. 다른 모듈의 `_internal.hpp`/`_testing.hpp` include 없음. ARCHITECTURE 의존 표와 일치 | PASS |

## 발견 사항

차단 수준(Critical/Major)은 없습니다.

### R2-N1 [Medium] 트레이 호스트가 없으면 libayatana-appindicator가 잘못된 주소를 읽는다
- 위치: `src/ui/tray.cpp:162-169`(`app_indicator_new_with_path(..., "<exe 디렉터리>/tray-icons")`). 결함 자체는 시스템 라이브러리 `libayatana-appindicator3.so.1+0x7da4`(0.5.93-1build3)에 있습니다.
- 재현 명령:
  - 앱: `make asan` 빌드 후 `dbus-run-session -- env GDK_BACKEND=wayland WAYLAND_DISPLAY=wayland-0 XDG_RUNTIME_DIR=/mnt/wslg/runtime-dir ASAN_OPTIONS=detect_leaks=0 timeout -s INT 12 build/asan/filescope`(HOME·XDG는 임시 경로, `build/qa-r2/exp/gui_san.sh`). X11(Xvfb)에서도 같음.
  - 최소 재현: `build/qa-r2/exp/min2.c`를 `-fsanitize=address`로 빌드해 `min2_asan A`로 실행(`min2.sh`, `min3.sh`). FileScope 코드 없이 `app_indicator_new_with_path` + 메뉴 + ACTIVE만으로 재현됩니다.
- 기대값: 표시줄 호스트가 없으면 연결 없음으로 남고 메모리 오류가 없어야 합니다.
- 실제값:
  - FileScope ASan: Wayland 3/3, X11 1/1에서 시작 직후 `AddressSanitizer: SEGV on unknown address ... READ`. 스택은 main loop 콜백 → libayatana 내부입니다(gui-asan-*.log).
  - 최소 C 프로그램: ASan `A` 5/5 크래시. `app_indicator_new`(경로 없음)는 0회였습니다.
  - 일반 빌드: 최소 프로그램 10회, `MALLOC_PERTURB_`·`MALLOC_CHECK_` 10회, FileScope 디버그 빌드 Wayland 10회 모두 크래시 없음(rc 124 = timeout까지 생존).
- 제품 영향 판정: 확장 없는 GNOME(트레이 호스트 없음)은 기본 Ubuntu 구성 중 하나입니다. 일반 malloc에서는 읽은 위치가 매핑된 힙이라 지금은 크래시가 나지 않습니다. 그래도 범위 밖 읽기이므로 힙 배치에 따라 시작 시 크래시할 가능성이 남습니다. 확률은 낮지만 0은 아닙니다. ASan GUI 실행이 게이트에 없어 `make verify`로는 드러나지 않습니다.
- 권고: Codex. `app_indicator_new`로 만들고, 아이콘 검색 경로는 `gtk_icon_theme_append_search_path`(또는 설치 시 hicolor)로 주는 회피 방법을 검토합니다. 이 회피가 실제로 오류를 없애는지는 수정 후 같은 ASan 재현으로 확인해야 합니다(QA는 경로 없는 생성만 확인함). 억제 항목으로 덮는 것은 권하지 않습니다(SEGV는 억제 대상이 아님). 배포판 버그 보고 후보입니다.

### R2-N2 [Low] 기준 스냅샷 순회가 권한 외 오류 한 번에 조용히 끝난다
- 위치: `src/his/his.cpp:217-221, 489-490`(`std::filesystem::recursive_directory_iterator` + `skip_permission_denied`, `increment(error)`)
- 시나리오: libstdc++는 `increment(ec)`에서 EACCES 외 오류(예: 순회 중 하위 디렉터리가 지워져 ENOENT)가 나면 반복자를 end로 만듭니다. 그러면 해당 루트의 나머지 파일이 BASELINE 없이 끝납니다. 이후 쓰기 이벤트로 AUTO 버전은 생기므로 데이터 손실은 아니고, "보관된 버전 없음" 범위가 넓어지는 정도입니다.
- 재현: NOT_RUN(코드 검토)
- 수정 담당: Codex(R3 후보). Coordinator가 수용 여부를 결정합니다.

### 관찰 (판정 대상 아님)
- 휴지통 영구 삭제 확인은 `Gtk::Dialog::run()` 중첩 루프입니다(ui.cpp:1578-1581). DESIGN의 "이 화면만 모달"과 맞고 테스트도 통과했습니다.
- R1 N1~N3 해소 확인(R2-T00): `/fs/integration/copy-directory-overwrite`(N1), `/fs/integration/move-cross-device-conflict`(N2), ui "folders first, natural filename order and invalid UTF-8"(N3) 통과.

## 계약 확인

- his 스레드·종료: 명령 큐는 잠금 아래 deque이고 결과·신호는 Dispatcher로 메인에서 전달합니다. shutdown은 stopping_ → 합류 → disconnect → Dispatcher 해제 순서이고, dispatch 중이면 해제를 미룹니다. 슬롯은 메인 스레드 맵에서만 다룹니다. `/his/integration/shutdown`은 1000개 배치와 조회를 섞은 뒤 종료하며, 반환 후 전달 0과 HIS_BACKLOG gap을 확인합니다.
- his 미기록 상한: `max_pending_events`를 넘으면 배치를 버리고 HIS_BACKLOG gap을 기록합니다(his.cpp:161-169).
- search·trash: 비동기 결과는 메인(thread-default context)에서 한 번 전달됩니다. trash list는 nav와 같은 GThreadPool + GSource 방식입니다.
- §3 체크리스트: 새 코드에 `new`/`delete`/`malloc` 없음(diff grep). 경로를 로그에 남기는 g_message 없음. 테스트는 모두 mkdtemp·임시 XDG 아래에서 실행.
- 억제 파일: `tests/tsan.supp`, `scripts/lsan.supp` 변경 없음(diff 범위 밖).

## UI 화면 대조 (Xvfb 1600x1050, FILESCOPE_SCREENSHOT_DIR로 직접 캡처)

캡처는 `build/qa-r2/screens/`에 있습니다(history-*, tray-*, r2-search*, r2-trash*, r2-preview*, r2-editor-*).
- 검색(r2-search.png): 탭 "검색: notes", 진행·완료 띠, 확장자 칩(`.md 2`, `.txt 1`), 위치 열(루트 기준 상대 경로, mono dim), 일치 부분 굵게+밑줄(노랑 아님). DESIGN "R2 화면"과 구성이 같습니다.
- 되돌리기 확인(confirmation.png): 패널 안 인라인 상자, 취소에 포커스 링이 있고 모달이 아닙니다. 줄 Diff는 + / − 글자 기호와 초록·빨강 줄 바탕입니다.
- 환경 한계: Xvfb에 CJK 글꼴이 없어 한국어가 두부(□)로 나옵니다. 문구·글꼴·간격은 실제 Ubuntu에서 확인해야 합니다. 시각 디자인은 판정하거나 수정하지 않았습니다(디자인 권한).

## 요구사항별 결과

| 요구사항 / 작업 | 테스트(이번에 실행·통과) + QA 실험 | 판정 |
|---|---|---|
| R2-T00 (N1~N3) | copy-directory-overwrite, move-cross-device-conflict, ui 정렬 묶음 | PASS |
| R2-T01 빌드·의존 | architecture PASS, `/architecture/core/dependency-check`, include 대조 | PASS |
| R2-T02 fs 보조 API | /fs/install/*, /fs/remove-tree/* | PASS |
| R2-T03 diff (HIS-007/008/009 계산) | test_diff(골든 7종, 성질 500쌍, 이미지), 골든 expected가 사람이 쓴 unified 형식인지 파일 확인 | PASS |
| R2-T04 his 저장소 (HIS-001, NFR-014) | /his/open/*, /his/events/*, /his/capture/*, /his/query/*, /his/integration/{busy,shutdown,orphans,crash}, G8 ingest | PASS |
| R2-T05 자동 버전·복원 (HIS-005/011/012, E09) | /his/versions/*, G8 baseline + QA 지연 실험 | PASS. 실제 편집기 저장은 NOT_RUN |
| R2-T06 mon 일시 정지 (MON-022) | /mon/integration/pause-keeps-index 등 test_mon_*, stress | PASS |
| R2-T07 trash (TRASH-001/002/003/005/006) | /trash/home/* + exp_trash 19개 | PASS |
| R2-T08 search (SRC-001/003/004) | /search/*, /search/walk/* + exp_search 5개 | PASS |
| R2-T09 변경 내용 패널 (UI-012 포함) | history_interaction 3묶음 | PASS(Xvfb). 실제 Ubuntu 수동 검증 NOT_RUN |
| R2-T10 트레이·요약 창·일시 정지 | test_tray, tray_interaction(테스트용 StatusNotifierWatcher 연결 상태 포함) | PASS(Xvfb). R2-N1, 실제 GNOME 트레이 NOT_RUN |
| R2-T11 검색·휴지통·미리보기 (PRE-001/003) | r2_interaction 3묶음 | PASS(Xvfb). 실제 Ubuntu 수동 검증 NOT_RUN |
| R2-F01 | shutdown 200회 0 실패, preset g++ | PASS |

## 미실행 및 N/A 근거

| 항목 | 이유 |
|---|---|
| 실제 Ubuntu 데스크톱(GNOME 트레이·Yaru 메뉴·아이콘 모양, Wayland/X11 요약 창 위치, 한국어 글꼴, Orca 화면 읽기) | WSL Xvfb/WSLg, CJK 글꼴 없음, StatusNotifier 호스트 없음. 아래 체크리스트로 이관 |
| gedit·vim·VS Code 실제 저장의 자동 버전 | 편집기 없음. 체크리스트 이관 |
| 다른 소유자 data_dir(PERMISSION_DENIED) | root 필요 |
| 실제 UNMOUNT, 다른 마운트 휴지통(E-13) | root·특수 환경 필요. E-13은 R2 범위 밖 |
| ASan/TSan으로 연결된 트레이(호스트 있음) | 시간 제한으로 debug 빌드 tray_interaction만 실행 |
| R2-N2 | 코드 검토만 함 |

## 재검증 조건

1. R2-N1은 Coordinator가 수용하거나 Codex에 회피 수정을 배정합니다. 수정하면 `gui_san.sh`의 ASan Wayland·X11 재현과 tray_interaction을 다시 실행합니다.
2. 사용자가 아래 수동 체크리스트(R1 1~15, R2 16~30)를 실제 Ubuntu에서 수행하고 결과(캡처 포함)를 PROGRESS에 기록하면 R2-T09/T10/T11 수동 검증 조건이 닫힙니다.
3. a5ed03f 이후 코드가 바뀌면 이 판정은 무효입니다.

직접 수정한 내용: 없음(제품 코드·테스트·억제 파일 변경 없음). 이 보고서 파일만 추가했습니다.

## 실제 Ubuntu 수동 검증 체크리스트 (사용자 수행)

준비: `make build`. 테스트 폴더는 `T=$(mktemp -d); mkdir -p $T/a $T/b; echo hi > $T/a/x.txt`로 만들고, 앱은 `./build/debug/filescope`로 실행합니다(Wayland 세션, 실제 HOME 사용 가능. 파일 작업은 $T 안에서만).

| # | 동작 / 명령 | 기대 결과 |
|---|---|---|
| 1 | `echo $XDG_SESSION_TYPE` 확인 후 앱 실행 | `wayland`. 창이 뜨고 한국어 문구가 두부 없이 표시됨. 경고 대화상자 없음 |
| 2 | 주소창에 `$T/a` 입력 → Enter, Alt+↑, Alt+←, Alt+→ | 경로와 breadcrumb가 차례로 $T/a → $T → $T/a → $T로 바뀜 |
| 3 | Ctrl+T로 새 탭을 열고 $T/b로 이동한 뒤 첫 탭으로 돌아가 "뒤로" | 탭마다 이력이 따로 유지됨 |
| 4 | x.txt 선택 → Enter(또는 더블클릭) | 기본 텍스트 편집기가 열림(FS-003). 터미널 오류 없음 |
| 5 | 새 파일·새 폴더 만들기, 같은 이름으로 다시 만들기 | 생성됨. 중복이면 오류 문구가 나오고 기존 항목은 그대로 |
| 6 | x.txt Ctrl+C → $T/b에서 Ctrl+V 두 번 → 두 번째 대화상자에서 "둘 다 유지", 한 번 더 붙여넣고 "덮어쓰기" | `x (1).txt` 같은 새 이름이 생기고, 덮어쓰기는 확인 후 내용이 교체됨 |
| 7 | 큰 폴더 복사(`cp -r /usr/share/icons $T/big` 후 앱에서 복사·붙여넣기) 도중 목록 스크롤과 다른 폴더로 이동 | 진행률이 갱신되고 UI가 멈추지 않음(NFR-015). 취소하면 "작업을 취소했습니다 · 완료된 항목 N개" 띠 |
| 8 | 위 복사가 진행 중일 때 창 닫기 | 2초 안에 프로세스 종료(`pgrep filescope` 없음). 크래시 없음(E10). 트레이 연결 시에는 창만 숨김(29번 참고) |
| 9 | 파일 Delete → 시스템 휴지통 확인, Shift+Delete → 취소 → 다시 → "영구 삭제" | Delete는 휴지통(`~/.local/share/Trash/files`)으로 감. 취소하면 파일 유지, 확인하면 삭제 |
| 10 | $T/a에서 "감시 시작" 후 터미널에서 `echo more >> $T/a/x.txt; touch $T/a/n; rm $T/a/n` | 2초 안에 목록·로그에 ▲ 수정(빨강), ● 생성(빨강), ✕ 삭제(파랑). 기호와 색이 함께 표시됨 |
| 11 | 하단 "변경량 · 1분봉" 탭 → 차트 클릭 → ←/→/Esc, "새 창으로" | 툴팁에 시·고·저·종·생성·수정·삭제가 나오고 Esc로 닫힘. 새 창 차트가 메인과 함께 갱신됨 |
| 12 | 터미널에서 `rm -rf $T/a`(감시 중) | "⚠ 감시 폴더가 삭제되었습니다" 띠와 "상위 폴더로 이동" 버튼 |
| 13 | 설정 → 접근성 → "애니메이션 줄이기" 켠 뒤 띠·토스트 표시 | 애니메이션 없이 즉시 표시(모션 감소). 반영되지 않으면 기록 |
| 14 | Orca 화면 읽기 켜고(Super+Alt+S) Tab으로 주소창, 파일 목록, 로그 칩, 1분봉 차트 순회 | 각 요소 이름을 읽음. 포커스 외곽선은 노랑 |
| 15 | 빈 폴더와 권한 없는 폴더(`mkdir -m 000 $T/locked`)로 이동한 화면 캡처 | 빈 상태 안내 / 권한 오류 안내 문구 표시. 캡처를 PROGRESS 증거로 첨부 |
| 16 | `mkdir -p $T/w; printf 'a\nb\n' > $T/w/notes.md` 후 $T/w 감시 시작, notes.md 선택, `printf 'a\nc\nd\n' > $T/w/notes.md` | 2초 안에 패널 "+2 줄 추가 −1 줄 삭제", 통합 줄 Diff(+ / − 글자), `v1 → v2`. 한국어 문구와 mono 숫자 확인 |
| 17 | gedit, vim(`:w`), VS Code로 notes.md를 각각 한 번씩 저장 | 저장마다 AUTO 버전이 하나씩 생김(전체 이력에서 확인). `.goutputstream-*`, `.swp`, `4913` 같은 임시 파일은 버전 없음 |
| 18 | 패널 "되돌리기" → 확인 상자에서 Enter 바로 누르기 → 다시 열어 "되돌리기 확정" | 첫 Enter는 취소(파일 불변). 확정하면 내용 복원, 토스트, 새 버전 반영 |
| 19 | 터미널 `rm $T/w/notes.md` → 로그의 DELETE 행 선택 → "vN 복원" | 파일이 원래 위치에 생기고 목록에 나타남. 같은 이름이 있으면 오류 표시만, 덮어쓰지 않음 |
| 20 | PNG 두 버전(이미지 편집기로 일부 칠하기) 선택 → 나란히 / 겹쳐 밀기, ←/→ | 바뀐 영역 점선 상자, 슬라이더가 키보드로 움직임 |
| 21 | 머리줄 "변경 내용 패널" 토글로 숨긴 뒤 앱 재시작 | 숨김 상태 유지(UI-012) |
| 22 | `chmod 000 ~/.local/share/filescope` 후 앱 실행(끝나면 0700으로 되돌림) | 앱이 뜨고 "이력 꺼짐" 표시, R1 기능 정상 |
| 23 | GNOME에서 AppIndicator 확장을 끈 상태로 앱 실행·종료를 5번 | 크래시 없음(R2-N1 확인). 창 닫기 = 종료. 크래시가 나면 `journalctl -b | grep filescope` 기록 |
| 24 | AppIndicator 확장을 켠 상태로 실행 → 표시줄 아이콘과 메뉴 확인 | 단색 아이콘(감시 중 점), Yaru 메뉴, 항목 순서가 DESIGN "Tray Menu"와 같음. 정보 줄은 비활성 |
| 25 | 메뉴 "요약 열기" | "FileScope 요약" 창, 최근 1분 건수, 경로·범위, 세션 집계(0건 dim), 최근 변경 5행. X11에서는 화면 오른쪽 위, Wayland는 컴포지터 배치. Esc로 닫힘 |
| 26 | 요약 창 "일시 정지" → 감시 폴더에서 파일 변경 → "감시 재개" | 정지 아이콘, 집계·최근 변경 흐림, 조용한 띠. 일시 정지 중 변경은 목록·로그에 없고, 재개 후 그 파일의 새 버전이 패널에 보임 |
| 27 | 감시 경로 4개 이상 추가 후 메뉴·요약 창 | 메뉴는 3개 + "외 N개 경로 (요약에서 보기)", 요약 창은 "› 외 N개 경로 더 보기" 펼침·접기 |
| 28 | 요약 창 최근 변경 행 클릭 | 메인 창이 보이고 그 파일이 선택되며 패널이 열림 |
| 29 | 트레이 연결 상태에서 메인 창 닫기 → 외부에서 파일 변경 → 메뉴 "메인 창 열기" → "FileScope 종료" | 창만 숨고 감시 유지, 다시 열면 숨긴 동안의 변경이 목록·로그에 있음, 종료 시 프로세스 끝남 |
| 30 | 머리줄 검색 칸에 이름 일부 입력(큰 폴더에서), Esc / 결과 Enter / 사이드바 "휴지통" → 복원·영구 삭제·비우기, 4 MiB 넘는 파일·비 UTF-8 파일 미리보기, 편집 중 외부 수정 후 저장 | 검색 띠 진행·완료 수, Esc 취소, Enter로 폴더 이동·선택. 휴지통 개수 일치, 확인 모달 기본 포커스 취소. 미리보기 한도·"텍스트로 열 수 없음". 저장 충돌 3선택, 덮어쓰기는 2단계 확인 뒤에만 |
