# R1 묶음 독립 재QA 보고서

- Task ID: task_e240384df1e7 (R0-002 + R1-T01~T07 + R1-FIX-01~03, 이전 판정 FAIL a8d89fa 이후 재검증)
- 검증 SHA: 8c469a9 (`git reset --keep 8c469a9`, `git log --oneline -1` = `8c469a9 chore(harness): R1-FIX-03 후속 병합, R1 재QA 착수`)
- diff 범위: `git diff b73c1b1 8c469a9 -- src tests scripts Makefile CMakeLists.txt CMakePresets.json` (95파일, +10030/−3134). 이전 QA 이후 수정분: `git diff a8d89fa 8c469a9`(12파일: fs.cpp, job.cpp, nav.cpp, ui.cpp, lsan.supp, tsan.supp, e2e-smoke.sh, Makefile, tests/ui/*, test_fs_operations.cpp, test_nav.cpp)
- 환경: WSL2 Ubuntu 24.04.4 LTS, 커널 6.18.33.2-microsoft-standard-WSL2, CPU 6, g++ 13.3.0, 배포판 GLib `libglib2.0-0t64 2.80.0-6ubuntu3.10`, `LD_LIBRARY_PATH` 미설정(logs/env.txt). 작업 트리는 /mnt/c(drvfs). 실험 빌드는 `git archive`로 뽑은 사본을 /tmp에 풀어 따로 수행했습니다(작업 트리 변경·커밋 없음).
- 증거 위치: `build/qa-r1b/logs/`(summary.txt, env.txt, verify.log, integration.log, stress.log, benchmark.log, ui-interaction.log, tsan-1..10.log, g8-verbose.log, xdev.log, exp-new.log, exp-old.log, toctou.log), 실험 소스 `build/qa-r1b/exp/`, 화면 `build/qa-r1b/screens/`

## 판정: PASS

자동화로 검증할 수 있는 범위에서는 PASS입니다.
- 게이트 G1~G8과 `make ui-interaction`이 모두 종료 코드 0이었고, tsan은 10회 모두 0이었습니다.
- 이전 FAIL 사유 F1~F8을 같은 재현 방법으로 다시 확인했고 모두 해소됐습니다. 수정 전 SHA(a8d89fa)에 같은 실험을 돌려 실험이 결함을 잡아낸다는 것도 함께 확인했습니다.

다만 실제 Ubuntu 데스크톱(Wayland, 한국어 글꼴, 화면 읽기)과 자동 테스트가 없는 일부 UI 동작은 NOT_RUN입니다(아래 "미실행" 표). R1-T06 수용 조건의 "실제 Ubuntu 수동 검증 기록"은 보고서 끝 체크리스트로 사용자가 채워야 합니다. Coordinator는 그 기록을 받은 뒤 APPROVED로 올리기를 권합니다.

## 게이트별 명령 / 종료 코드 / 증거

| 게이트 | 명령 | 종료 코드 | 증거 |
|---|---|---|---|
| G1, G2, G4~G7 | `make verify` | 0 | verify.log: build·test·format-check·lint·architecture·asan·ubsan·tsan·e2e를 거침. 4개 프리셋 모두 `100% tests passed, 0 tests failed out of 17`, `architecture: PASS`, 컴파일러 `warning:` 0건. e2e 스크린샷 4장, event-to-model 지연 16.4~22.7ms(기준 2초) |
| G3 | `make integration` | 0 | integration.log |
| G8 | `make stress` | 0 | `stress operations=10000 elapsed_s=10.006 rate=999.4 gap=no`(g8-verbose.log) |
| G8 | `make benchmark` | 0 | `benchmark files=100000 directories=10000 register_s=0.255 rss_delta=10158080`(g8-verbose.log) |
| UI 동작 | `make ui-interaction` | 0 | ui-interaction.log의 PASS 12묶음(아래 요구사항 표 참조) |
| G6 반복 | `TSAN_OPTIONS=halt_on_error=1:suppressions=$W/tests/tsan.supp ctest --test-dir build/tsan --output-on-failure` ×10 | 10회 모두 0 | tsan-1..10.log 모두 17/17. 배포판 GLib, LD_LIBRARY_PATH 없음 |
| EXDEV 통합 | `test_fs_operations --verbose -p /fs/integration/move-cross-device` | 0 | xdev.log: skip 없이 실행됨(pipe NOT_SUPPORTED, 상위 PARTIAL 보고) |

## 이전 발견 사항 재확인 (F1~F8)

| ID | 재현 방법 | 8c469a9 결과 | a8d89fa 대조 | 판정 |
|---|---|---|---|---|
| F1 EXDEV 데이터 손실 (Critical) | `exp/test_fs_qaexdev.cpp`: /tmp(dev 2096)의 `tree/{ok.txt, pipe(FIFO), secret.txt(000)}`를 /mnt/c(dev 70)로 `fs::move(FAIL, NONE)` | `state=4(FAILED)`. item 오류는 secret PERMISSION_DENIED, pipe NOT_SUPPORTED, tree PARTIAL. **`src_tree_exists=1 fifo_in_src=1 secret_in_src=1`**, rc=0 | `src_tree_exists=0 ... secret_in_src=0`, rc=1(손실 재현) | 해소 |
| F2 LSan 억제 과다 | `exp/test_job_qaleak.cpp`(malloc 4096 + g_strdup 57)를 asan으로 빌드하고 `LSAN_OPTIONS=suppressions=scripts/lsan.supp`로 실행 | 두 누수 모두 `Direct leak` 보고(g_strdup 스택 포함), `SUMMARY: 4153 byte(s) leaked in 2 allocation(s)`, rc=1 | (이전 QA: 57B 억제됨) | 해소. lsan.supp에는 `leak:libfontconfig.so.1` 하나만 남음 |
| F3 UI 동작 테스트 부재 | `make ui-interaction` | 12묶음 PASS, rc=0 | — | 대부분 해소. 남은 항목은 미실행 표에 정리 |
| F4 재귀 TOCTOU | `exp/test_fs_qatoctou.cpp`: `victim/a`(파일 3000개) 영구 삭제 중 공격 스레드가 `a`의 mtime 변화를 감지하면 `a`를 stash로 옮기고 `a → outside` 심볼릭 링크로 바꿈. outside의 같은 이름 파일 3000개가 남는지 확인 | 15+30회 모두 `lost=0`, 작업 FAILED(CONFLICT) | 3회 중 1회 `outside_left=1 lost=2999`(rc=1). 이후 30회는 0(경합이라 비결정적) | 해소. 코드도 `openat/fstatat(AT_SYMLINK_NOFOLLOW)/unlinkat`과 dev/ino 재검사로 바뀜(fs.cpp:221-283, 460-530) |
| F5 OVERWRITE 이동 대상 선삭제 | `exp/test_fs_qaovw.cpp`: 원본 디렉터리를 0500으로 막고 파일·디렉터리를 각각 `move(OVERWRITE, CONFIRMED)` | 파일과 디렉터리 모두 FAILED, 대상 `old` 보존, `dest_lost=0` | 파일·디렉터리 대상 모두 사라짐, `dest_lost=2` | 해소(파일은 renameat 원자 교체, 디렉터리는 staging 백업 후 복원. fs.cpp:553-625) |
| F6 TSan 억제 과다 | `exp/test_job_qarace.cpp` plain/glib 모드를 현재 tests/tsan.supp로 실행(배포판 GLib) | plain: `data race ... test_job_qarace.cpp:12` rc=66. **glib: `data race ... in g_strdup` rc=66**(이전에는 억제됨) | — | 해소. 억제는 `race:^g_queue_pop_tail$` 하나 |
| F7 job 종료 UAF | 코드 검토 | `const JobId id = data.id;`를 잠금 전에 복사하고, 잠금을 푼 뒤에는 `data`에 접근하지 않음(job.cpp:260-280) | — | 해소 |
| F8 화면 | Xvfb 캡처(아래 UI 절) | 빈 띠 없음, 축 라벨 겹침 없음, 0건 dim | — | 해소 |

## 새 발견 사항

차단 수준(Critical/Major)은 없습니다. 아래는 Low이며 기록만 합니다.

### N1 [Low] 복사(copy) + OVERWRITE가 디렉터리 대상을 먼저 지운다
- 위치: `src/fs/fs.cpp:473-477`(`copy_directory`의 `if (replace) remove_entry(target)`)
- 시나리오: 같은 이름 디렉터리를 OVERWRITE(CONFIRMED)로 복사할 때, 대상을 지운 뒤 복사가 중간에 실패(권한, 디스크 가득 등)하면 원래 대상은 사라지고 부분 복사본만 남습니다. 원본은 보존되므로 E06 "원본 보존" 위반은 아닙니다. 다만 F5에서 이동에 적용한 staging 백업 방식이 복사에는 적용되지 않았습니다.
- 재현: NOT_RUN(코드 검토만)
- 기대값: 사용자가 덮어쓰기를 확인했더라도, 실패하면 원래 대상을 복원하는 편이 일관됩니다.
- 수정 담당: Codex(필요하면 R2). Coordinator가 위험을 수용할지 결정합니다.

### N2 [Low] EXDEV 이동 뒤 원본 변경 검사가 최상위 stamp만 본다
- 위치: `src/fs/fs.cpp:614-623`
- 시나리오: 장치 간 복사 중 원본의 하위 디렉터리에 새 파일이 생기면, 최상위 mtime은 바뀌지 않아 검사를 통과합니다. 이어지는 `remove_entry(source)`가 복사되지 않은 새 파일까지 지웁니다. 다른 프로세스가 동시에 쓰는 경합에서만 생기는 문제입니다.
- 재현: NOT_RUN(코드 검토)
- 수정 담당: Codex(R2 후보)

### N3 [Low, UI] 파일 목록 기본 정렬이 사전순이다
- 위치: screens/main-longpath.png에서 file-1, file-10, file-11 … file-2 순서
- R1 요구사항 위반은 아니며 관찰로만 기록합니다(디자인·UX 결정 사항).

## 계약 확인

- 모듈 경계: `scripts/check-architecture.sh` PASS(verify.log), `/architecture/core/dependency-check` 통과. 비 ui 모듈에 gtkmm include가 없음을 확인했습니다.
- ADR-0008:
  - nav 비동기는 GTask를 제거하고 `GThreadPool` + 호출자 thread-default context에 붙인 `GSource`로 완료를 전달합니다(nav.cpp:34-86). 완료 콜백은 호출 스레드에서 실행되며, `/nav/list-dir-thread-default` 테스트가 통과했습니다.
  - job은 Dispatcher와 잠금 아래 deque를 쓰고 emit은 잠금 밖에서 합니다(변경 없음).
  - ui는 스레드를 만들지 않습니다.
  - 예외 경계는 이전 QA와 같습니다(`bad_alloc`만 다시 던짐).
- mon size_delta/root_total 불변식: 이번 수정 범위 밖이라 코드 변경이 없고, `/mon/integration/basic-events-size`가 통과했습니다.
- dash 1분봉:
  - 분 버킷은 UTC epoch 60초 내림이고 표시는 localtime입니다.
  - @1700000040을 `TZ=... date`로 확인한 결과 UTC 22:14:00, Asia/Kolkata(+0530) 03:44:00, Asia/Kathmandu(+0545) 03:59:00, Australia/Adelaide(+1030) 08:44:00으로 모두 :00초였습니다(exp-new.log). 30분·45분 오프셋에서도 분 경계가 맞습니다.
  - 시간대 전용 자동 테스트는 여전히 없습니다(Low, 기록만).
- fs 위험 작업:
  - `/fs/integration/move-delete`: CONFIRMED가 없으면 `delete_permanently`를 거부하고 파일을 보존합니다.
  - `/fs/conflicts-protection`: 보호 경로를 거부합니다.
  - UI 테스트는 Shift+Delete 취소 후 파일 보존, 확인 후 삭제를 검증합니다.
- 억제 파일:
  - `tests/tsan.supp`는 `race:^g_queue_pop_tail$` 하나입니다. 배포판 GLib(디버그 심볼 없음)에서 tsan 10/10이 0이었습니다.
  - 의도적 경합은 plain과 GLib 경유 모두 보고됐습니다.
  - `scripts/lsan.supp`는 fontconfig 하나이며, GLib 할당자를 거친 의도적 누수도 보고됐습니다.

## UI 화면 대조 (Xvfb 1600x1050 캡처, 직접 실행)

캡처 파일:
- `screens/main-default.png`, `main-chart.png`, `main-empty.png`, `main-error.png`: 이번 `make verify` e2e에서 생성
- `screens/main-longpath.png`: `build/qa-r1b/longpath.sh`로 4단 한국어 긴 경로를 만들어 캡처

대조 기준은 루트 DESIGN.md입니다. 시안 HTML은 브라우저 렌더 비교 대신 토큰으로 대조했습니다.

- 색 토큰: `src/ui/ui.cpp` CSS의 hex 15개가 모두 DESIGN.md 정의 안에 있습니다(bg #10151c, up #f0525a, down #4d9bff, sel #f5c84b 등).
- 색 의미와 기호:
  - 세션 집계는 생성 1(빨강 ●), 수정 2(빨강 ▲), 삭제 1(파랑 ✕)이고, 이동·속성은 dim입니다. 0건은 모두 dim입니다(main-default). Coordinator 결정과 일치합니다.
  - 삭제 행은 취소선과 "✕ DELETE"로 표시되고, 이동은 "→ MOVE"입니다.
  - 탭 밑줄은 노랑(sel)입니다.
- 하단 패널 탭은 [이벤트 로그 | 변경량 · 1분봉]이고, 칩 행과 경로 거르기 입력이 있습니다. 차트 캡처에서 1분봉 탭이 활성 상태입니다.
- F8 수정 확인:
  - 파일 목록과 하단 패널 사이의 빈 띠가 사라졌습니다.
  - 축 단위가 KB(41.2/27.9/14.5 KB)로 바뀌었습니다.
  - 현재가 라벨 "1.2 KB"는 박스로 표시되고 눈금 라벨과 겹치지 않습니다.
- 긴 경로: breadcrumb는 앞쪽 구성 요소를 생략하고 마지막 3개만 보여 주며 넘치지 않습니다. 주소 입력란은 잘림 처리됩니다.
- 띠 표시: 감시 시작 띠, 루트 삭제 띠, RESYNC_DONE 띠, 취소 결과 띠는 `make ui-interaction`이 AT-SPI로 문구와 표시 상태를 검증했습니다. 띠의 색과 모양은 캡처하지 않았습니다(NOT_RUN).
- 환경 한계: Xvfb에 CJK 글꼴이 없어(`fc-list :lang=ko` 0개) 한국어가 두부(□)로 나옵니다. 문구와 글꼴은 실제 Ubuntu에서 확인해야 합니다.
- 구현자가 알린 행 밀도·시세색 차이는 판정하지 않습니다(디자인 권한). 시각 수정도 하지 않았습니다.

## 요구사항별 결과 (PROGRESS §2 R1)

| 요구사항 | 테스트(이번에 실행·통과) | 판정 |
|---|---|---|
| NAV-001/003/004/005/010/013/014 | /nav/list-dir, list-dir-thread-default, list-dir-failures, history-parent-breadcrumb, complete, home, bookmarks, disconnected-slot, list-dir-stress. UI: "Alt navigation and breadcrumb", "hidden toggle and favorite creation", "favorite persists" | PASS |
| NAV-002 | /nav/history-parent-breadcrumb + UI Alt+←/→/↑ | PASS |
| NAV-006 | UI "independent tab histories"(Ctrl+T, 탭별 뒤로/앞으로) | PASS |
| FS-001/002/008 | /fs/create-rename, /fs/create-permission-denied. UI F2 이름 변경 | PASS(API·이름 변경). 새 파일·새 폴더 UI는 NOT_RUN |
| FS-003 | 없음(ui.cpp:1437 `launch_default_for_uri`) | NOT_RUN |
| FS-004/005 | /fs/text/save-conflict, too-large, write-failure-cleanup. UI "external edit conflict" | PASS |
| FS-006 | /fs/integration/trash + UI Delete → 임시 XDG Trash | PASS(지원 경로). 미지원 경로는 NOT_RUN |
| FS-007/014, SEC-002 | /fs/integration/move-delete, delete-partial. UI Shift+Delete 취소·확인 | PASS |
| FS-009/012 | /fs/integration/copy-tree, cancel. UI 복사 진행률·취소 | PASS |
| FS-010 | /fs/integration/move-delete, move-overwrite, move-cross-device + QA 실험 F1·F5 | PASS |
| FS-011/013 | UI "multiselection, copy/skip conflict and cut/paste" | PASS |
| FS-018 | /fs/integration/conflicts(4정책 API). UI 대화상자 버튼 3개 존재, SKIP 실행 | PASS(API). KEEP_BOTH/OVERWRITE의 UI 실행은 NOT_RUN |
| SEC-001/003 | /core/path/protected, /core/path/access, /fs/conflicts-protection | PASS |
| MON-001/002/003/006/011 | /mon/integration/basic-events-size | PASS |
| MON-008/009/010/018/019/028 | recursive-moves-overflow-remove, /mon/normalize/cookie-unknown-boundary | PASS |
| MON-012/013 | root-terminal-events-destruction + UI 루트 삭제 띠 | PASS |
| MON-014 | /mon/normalize/unpaired-overflow(합성) | PASS(합성). 실제 UNMOUNT는 NOT_RUN(E-2) |
| MON-015/016/029 | 합성 overflow 테스트 + UI "RESYNC_DONE banner after real inotify overflow"(실제 커널 overflow) | PASS |
| MON-017 | e2e 지연 16.4~22.7ms | PASS |
| MON-020/021/027 | multiple-roots-merge-moved-in-queue-gap, /mon/api/validation-overlap + UI 감시 시작 띠 | PASS |
| JOB-001/002/003/008 | /job/* 6개 + UI 진행률·취소·부분 결과 띠 | PASS. NFR-015(작업 중 입력 응답) 측정은 NOT_RUN |
| DASH-001 | /dash/counts, /dash/metrics + 캡처 헤더(P95 22ms, 세션 집계) | PASS |
| DASH-002 | /dash/candle, roots-and-volume, unknown-and-gap, ring-and-clock-regression + UI 1분봉 ←/→/Esc와 accessible description | PASS. "새 창으로"(ui.cpp:946)는 NOT_RUN |
| DASH-003/005 | UI "log path/chip filters, file selection and cap"(500행 상한, 행 → 목록 선택) | PASS |
| R0-002 | /filescope/app-info, /architecture/core/dependency-check, make 타깃 전부 rc 0 | PASS |
| R1-FIX-01~03 | tsan 10/10, 억제 실험, ui-interaction | PASS |

## 미실행 및 N/A 근거

| 항목 | 이유 |
|---|---|
| 실제 Ubuntu 데스크톱·Wayland·한국어 글꼴·화면 읽기(Orca) | WSL Xvfb 환경, CJK 글꼴 없음. 아래 체크리스트로 이관 |
| FS-003 기본 앱 열기 | 자동 테스트 없음, 데스크톱 연결 필요 |
| 새 파일·새 폴더 UI, KEEP_BOTH/OVERWRITE UI 실행, "새 창으로" 1분봉 동기 갱신 | ui-interaction에 없음 |
| 모션 감소 설정 반영, 띠의 색과 모양 | ui.cpp에서 gtk-enable-animations 참조를 찾지 못함. 수동 확인 필요 |
| 작업 중 창 닫기(E10) 정상 종료, NFR-015 응답성 수치 | 자동 테스트 없음(ui-interaction은 유휴 상태 닫기만 검증) |
| 실제 UNMOUNT(E-2), 휴지통 미지원 경로 | root 또는 특수 환경 필요 |
| N1, N2 | 코드 검토만 함 |

## 재검증 조건

1. 사용자가 아래 수동 체크리스트를 실제 Ubuntu에서 수행하고 결과(캡처 포함)를 PROGRESS에 기록하면 R1-T06 수용 조건이 닫힙니다.
2. N1·N2는 Coordinator가 수용하거나 R2 작업으로 배정합니다.
3. 8c469a9 이후 코드가 바뀌면 이 판정은 무효입니다.

직접 수정한 내용: 없음. 문서 수정과 커밋도 없습니다.

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
| 8 | 위 복사가 진행 중일 때 창 닫기 | 2초 안에 프로세스 종료(`pgrep filescope` 없음). 크래시 없음(E10) |
| 9 | 파일 Delete → 시스템 휴지통 확인, Shift+Delete → 취소 → 다시 → "영구 삭제" | Delete는 휴지통(`~/.local/share/Trash/files`)으로 감. 취소하면 파일 유지, 확인하면 삭제 |
| 10 | $T/a에서 "감시 시작" 후 터미널에서 `echo more >> $T/a/x.txt; touch $T/a/n; rm $T/a/n` | 2초 안에 목록·로그에 ▲ 수정(빨강), ● 생성(빨강), ✕ 삭제(파랑). 기호와 색이 함께 표시됨 |
| 11 | 하단 "변경량 · 1분봉" 탭 → 차트 클릭 → ←/→/Esc, "새 창으로" | 툴팁에 시·고·저·종·생성·수정·삭제가 나오고 Esc로 닫힘. 새 창 차트가 메인과 함께 갱신됨 |
| 12 | 터미널에서 `rm -rf $T/a`(감시 중) | "⚠ 감시 폴더가 삭제되었습니다" 띠와 "상위 폴더로 이동" 버튼 |
| 13 | 설정 → 접근성 → "애니메이션 줄이기" 켠 뒤 띠·토스트 표시 | 애니메이션 없이 즉시 표시(모션 감소). 반영되지 않으면 기록 |
| 14 | Orca 화면 읽기 켜고(Super+Alt+S) Tab으로 주소창, 파일 목록, 로그 칩, 1분봉 차트 순회 | 각 요소 이름을 읽음("주소 입력 및 폴더 자동완성", "파일 목록", "이벤트 로그 목록", "감시 폴더 총용량 1분봉 차트"). 포커스 외곽선은 노랑 |
| 15 | 빈 폴더와 권한 없는 폴더(`mkdir -m 000 $T/locked`)로 이동한 화면 캡처 | 빈 상태 안내 / 권한 오류 안내 문구 표시. 캡처를 PROGRESS 증거로 첨부 |
