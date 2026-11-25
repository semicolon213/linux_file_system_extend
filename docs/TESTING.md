# 검증: 게이트, 완료 조건, 테스트

검증 문서는 이것 하나다. 강제는 `Makefile` 대상과 `scripts/`가 하며 이 문서는 무엇을 언제 돌리는지 정한다. 명령은 WSL `Ubuntu-24.04`에서 실행한다(AGENTS.md "명령").

## 1. 품질 게이트

| 게이트 | 명령 | 통과 기준 | 필수 대상 |
|---|---|---|---|
| G1 빌드 | `make build` | 종료 코드 0, 프로젝트 코드 경고 없음(경고=오류) | 모든 구현 작업 |
| G2 단위 | `make test` | 관련 테스트 전체 성공(라벨 `unit`) | 모든 구현 작업 |
| G3 통합 | `make integration` | 실제 임시 파일 시스템 시나리오 성공(라벨 `integration`) | 모든 구현 작업 |
| G4 스타일·의존성 | `make format-check lint architecture` | 위반 없음. `architecture`는 `scripts/check-architecture.sh`(모듈 금지 include, `_internal.hpp`/`_testing.hpp` 규칙, 비 ui 모듈의 gtkmm include 금지, `sqlite3.h`는 `src/his/`만, R2: libayatana-appindicator는 `src/ui/`만, diff·his·search·trash 금지 의존) | 모든 구현 작업 |
| G5 메모리 | `make asan ubsan` | sanitizer 오류 없음(누수 포함, `scripts/lsan.supp`는 GLib/GTK/glibmm/gtkmm 전역 할당만 억제). UBSan은 `-fno-sanitize-recover=undefined`로 첫 오류에서 테스트가 실패해야 한다 | C++ 코드 변경 |
| G6 동시성 | `make tsan` | 관련 모듈 경쟁 상태 없음 | 동시성 변경 |
| G7 GUI | `make e2e` | Xvfb 실행(`scripts/e2e-smoke.sh`), 사용자 시나리오 및 캡처 검증 | UI 변경 |
| G8 부하 | `make stress benchmark` | 합의된 기준 충족, 수치는 §5 형식으로 기록 | 성능·감시 변경 |

- `make verify`는 build, test, format-check, lint, architecture, asan, ubsan, tsan, e2e를 차례로 실행한다(G1, G2, G4~G7). G3·G8은 따로 실행한다.
- `integration`·`stress`·`benchmark`는 `ctest --preset debug -L <라벨> --no-tests=error`라 해당 라벨 테스트가 없으면 비0으로 끝난다. 이 경우 BLOCKED(또는 NOT_RUN)이며 PASS가 아니다. 해당 없음은 QA 근거와 함께 N/A로 적는다.
- 통합 시 적용 가능한 게이트 전체를 다시 실행한다. sanitizer는 별도 빌드에서 실행한다. 로그와 SHA를 보존한다.
- C++ sanitizer: 프리셋마다 `-fsanitize=<종류> -fno-omit-frame-pointer`를 컴파일과 링크 모두에 준다(libstdc++ 컨테이너 포함). UBSan은 `-fno-sanitize-recover=undefined`. TSan은 기존처럼 `setarch -R`로 실행한다. glibmm/gtkmm/sigc++는 계측되지 않은 시스템 라이브러리이므로, 그 안에서 난 TSan 보고를 억제해야 하면 `tests/tsan.supp`(Makefile tsan 타깃이 TSAN_OPTIONS로 적용)에 항목마다 이유를 적고 Coordinator 승인을 받는다(프로젝트 코드 보고는 억제 금지). 승인된 항목: `race:^g_queue_pop_tail$`(GLib 스레드 풀 큐 노드 해제 오탐, E-10, 8388c6c). 억제는 배포판 GLib(디버그 심볼 없음)에서 그대로 통과해야 한다. GUI LSan(E10)은 `scripts/lsan.supp`의 시스템 라이브러리 항목(fontconfig 캐시)만 억제한다. GLib 할당자를 거친 프로젝트 누수는 억제하지 않는다.

## 2. 완료 조건 (Definition of Done)

- 요구사항 ID와 수용 조건이 모두 연결됨.
- 정상·실패·경계 테스트 작성 및 실행.
- 메모리 소유권, 오류 반환, 취소 및 정리 경로 검토.
- 적용 가능한 Quality Gate 통과 및 증거 제출.
- UI 변경은 캡처, 키보드 탐색, 빈 화면·오류·로딩 검증 포함.
- 독립 QA가 동일 SHA를 PASS 판정.
- 통합 회귀 검사 성공 및 추적 문서 갱신.
미완료 항목이 있으면 DONE으로 이동하지 않는다.

## 3. 리뷰 체크리스트 (독립 QA)

- [ ] 요구사항과 diff 일치, 범위 외 변경 없음
- [ ] 실패 반환값(`core::Result`) 및 errno 처리, `Glib::Error`를 경계에서 잡음, 모듈·스레드 경계를 넘는 예외 없음
- [ ] RAII(앱 코드에 `new`/`delete`/`malloc` 없음), FD·watch·스레드 종료 경로 확인
- [ ] GTK 메인 스레드 규칙 및 큐 크기 제한
- [ ] 심볼릭 링크, 경로 탈출, 권한, 덮어쓰기 검증
- [ ] inotify 이동 cookie, overflow, 재귀 감시 누락 대응
- [ ] 외부 수정 충돌 및 취소 시 데이터 보존
- [ ] 실제 테스트 결과와 SHA 확인
- [ ] UI 상태·접근성·스크린샷 확인 (해당 시)
- [ ] 민감 정보 로그 노출 및 실제 사용자 파일 접근 없음
- [ ] 메모리·동시성: 소유권(값·unique_ptr·EventPtr), 잠금 순서와 잠금 중 금지 호출, 다른 스레드에서 sigc++·GTK 접근 없음, 소멸(shutdown) 반환 후 신호 없음(docs/DESIGN.md 메모리·스레드 부분)

## 4. 테스트 전략

GLib unit, 실제 임시 디렉터리 integration, Xvfb GUI smoke 및 실제 Ubuntu Wayland 수동 검증. 생성/수정/이동/삭제 외부 명령 테스트, overflow 강제 주입, 권한 오류, ENOSPC 모의, symlink loop, archive traversal, 취소 및 종료 경쟁. 실제 홈/시스템 경로 변경 금지. QA는 구현과 분리.

- 디렉터리·이름·라벨 규칙과 임시 경로·조건 대기 규칙은 docs/ARCHITECTURE.md §4.
- 함수별 최소 테스트(정상 1개 이상, 대표 실패 1개 이상, 권한 오류는 `chmod 000`, root면 skip)는 docs/ENGINEERING.md 오류 §6.
- 평가 시나리오 E01~E10은 harness/EVALS.md.

## 5. 성능 테스트와 기준 성능

1만/10만 파일 fixture, 초당 1000 이벤트, 10GB 복사(여유 용량 확인), 24시간 soak. monotonic clock으로 P50/P95/P99, RSS, CPU, queue depth, overflow 기록. NFR 목표와 비교. VM/호스트 사양 및 반복 횟수 기록.

기준 성능 기록 항목: SHA, Ubuntu, kernel, CPU, RAM, FS, fixture, 명령, 반복 횟수, 원시 결과, P95를 최초 실행 시 기록. 미측정은 NOT_RUN.

| 측정 | SHA | 환경 | 결과 |
| --- | --- | --- | --- |
| R2-T04 his 기록 부하(G8), 1회 | a0a85b1 | WSL Ubuntu-24.04, 디버그 빌드, ext4 임시 디렉터리 | 1,000건/초 10초 10,000건 전부 기록, 미기록 최대 100, 커밋 지연 P95 7.429ms(100건 단위 제출→flush, 단조 시계), 체크포인트 후 DB 1,466,368B, HIS_BACKLOG 0. 1MiB 블롭 100개 캡처 0.769초(130.12MiB/s) |
| R2-T05 기준 스냅샷 2만 파일, 작업자 보고 | 74fcebe | WSL Ubuntu-24.04, 디버그 빌드 | 46.381초(431개/초), 순회 중 load_history_async 응답 1.748ms(기준 1초) |
| R2-T08 1만 파일 재귀 검색, 작업자 보고 | 51cac30 | WSL Ubuntu-24.04, 디버그 빌드 | 2.018초, signal_hits 20묶음 |

## 6. 장애 주입

watch overflow synthetic injection 및 실제 부하 별도 검증; SQLite busy, 디스크 부족, 권한 거부, 파일 작업 중 외부 삭제, worker 취소, 강제 종료. 원본 보존과 gap 표시, 재동기화 확인.

## 7. 테스트 카탈로그

- docs/SRS.md의 기능 ID마다 테스트 ID `TC-<기능 ID>`가 하나 있다(394개, SRS 표 "완료 판정" 열).
- 각 TC의 기본 내용: 해당 기능의 정상 경로를 임시 fixture에서 실행하고 결과를 확인한다. 해당 기능에 적용되는 권한 오류, 취소, 외부 변경 및 모듈 공통 계약을 검증한다. 세부 입력/기대값은 구현 전 작업 명세에서 확정한다.
- 확정된 세부 Given/When/Then은 harness/TASKS.md 각 작업의 수용 조건이 정본이고, 요구사항·작업·테스트 연결은 harness/PROGRESS.md 추적표에 둔다.
- 현재 상태: 모든 TC가 NOT_RUN이다. 독립 QA가 실행해 PASS를 판정하면 추적표에 결과를 적는다.

## 8. R2 테스트 (제안, 2026-11-25)

R2 범위와 TC ID: TC-HIS-001, 005, 007, 008, 009, 011, 012, TC-MON-022, TC-TRASH-001, 002, 003, 005, 006, TC-SRC-001, 003, 004, TC-PRE-001, 003, TC-UI-012, 트레이·요약 창(기능 ID 없음, PRODUCT.md). 세부 Given/When/Then은 harness/TASKS.md R2 작업이 정본이다. 규칙은 §1~§7 그대로이고 아래만 더한다.

- 데이터 경로: his 테스트는 `Options::data_dir`를 `g_dir_make_tmp()` 아래로 준다. XDG_DATA_HOME도 임시 경로로 바꾼다(휴지통 테스트와 같음). 실제 `~/.local/share/filescope`를 만들지 않는다.
- DB 강제 종료 복구 (NFR-014): 테스트가 `fork()`한 자식에서 History를 열고 이벤트 기록·캡처를 반복하다가 부모가 SIGKILL한다(무작위 시점 20회). 또 `his_testing.hpp` 장애 지점(블롭 rename 뒤 커밋 전, 커밋 뒤 unlink 전)에서 자식이 `_exit`한다. 다시 연 뒤 `PRAGMA integrity_check`가 ok, blobs 행마다 파일 있음, 행 없는 블롭 파일 없음, tmp/ 비어 있음, ref_count가 versions 수와 같음을 확인한다.
- orphan 정리: 블롭 디렉터리에 행 없는 64자 hex 파일, tmp/ 잔여 파일, 행은 있는데 파일이 없는 블롭을 만들어 두고 열기 → 각각 삭제, 삭제, 행 정리. 64자 hex가 아닌 파일은 남는다.
- migration: user_version 0 DB → v1, 코드보다 큰 user_version → NOT_SUPPORTED이고 파일 바이트 불변. 손상 파일(무작위 바이트) → `history.db.corrupt-*`로 옮기고 새 DB.
- busy: 테스트가 별도 sqlite 연결로 `BEGIN EXCLUSIVE`를 쥔 채 기록 요청 → 재시도 뒤 DEGRADED, 잠금 해제 후 다음 기록 성공 → READY.
- 권한: data_dir를 0755로 만들어 두면 열 때 0700으로 고쳐짐. 생성 파일 모드 0600. 다른 소유자 디렉터리는 root가 필요하므로 NOT_RUN 사유 기록.
- diff 골든 테스트: `tests/diff/golden/<사례>.{before,after,expected}`. expected는 사람이 쓴 unified 형식(머리 `@@ -a,b +c,d @@`, 줄 기호)이고 구현 출력을 그대로 복사해 만들지 않는다. 사례: 빈 파일 양쪽, 한쪽만 빈 파일, 끝 줄바꿈 없음, CRLF, 같은 줄 반복, md 문서(제목·목록 수정), 비 UTF-8(→ BINARY). 성질 검사: 무작위 작은 입력 500쌍에서 결과를 before에 적용하면 after이고 added+deleted가 O(NM) LCS로 구한 최소값과 같다. 이미지 비교는 코드로 만든 RGBA 버퍼(한 칸만 바꿈, 크기 다름, 임계값 경계)로 검사하고 디코더를 쓰지 않는다.
- 트레이 테스트 한계: Xvfb와 `dbus-run-session`에는 StatusNotifier 호스트가 없어 AppIndicator가 표시줄에 연결되지 않는다. 그래서 (1) 메뉴 항목 목록을 만드는 순수 함수는 단위 테스트, (2) 연결 없음에서 창 닫기 = 종료(R1 동작 유지)는 ui-interaction, (3) 연결 있음 동작(창 닫아도 감시 유지, 메뉴로 다시 열기·종료)은 테스트용 `org.kde.StatusNotifierWatcher`를 같은 세션 버스에 띄워 재현하고, 재현이 안 되면 NOT_RUN 사유를 적고 실제 Ubuntu 수동 검증으로 넘긴다. 아이콘 모양과 Yaru 메뉴 표시는 수동 검증만 한다.
- 부하(G8): 초당 1000 이벤트 10초 동안 record_events → 미기록 이벤트 최대치, 기록 지연 P95, DB 크기, HIS_BACKLOG gap 발생 여부를 §5 형식으로 기록. 캡처 처리량(1MiB 파일 100개)과 기준 스냅샷 2만 파일 소요 시간도 기록한다. 측정 전 PASS 금지.
