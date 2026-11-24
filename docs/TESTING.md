# 검증: 게이트, 완료 조건, 테스트

검증 문서는 이것 하나다. 강제는 `Makefile` 대상과 `scripts/`가 하며 이 문서는 무엇을 언제 돌리는지 정한다. 명령은 WSL `Ubuntu-24.04`에서 실행한다(AGENTS.md "명령").

## 1. 품질 게이트

| 게이트 | 명령 | 통과 기준 | 필수 대상 |
|---|---|---|---|
| G1 빌드 | `make build` | 종료 코드 0, 프로젝트 코드 경고 없음(경고=오류) | 모든 구현 작업 |
| G2 단위 | `make test` | 관련 테스트 전체 성공(라벨 `unit`) | 모든 구현 작업 |
| G3 통합 | `make integration` | 실제 임시 파일 시스템 시나리오 성공(라벨 `integration`) | 모든 구현 작업 |
| G4 스타일·의존성 | `make format-check lint architecture` | 위반 없음. `architecture`는 `scripts/check-architecture.sh`(모듈 금지 include, `_internal.h`/`_testing.h` 규칙, `sqlite3.h`는 `src/his/`만) | 모든 구현 작업 |
| G5 메모리 | `make asan ubsan` | sanitizer 오류 없음(누수 포함, `scripts/lsan.supp`는 GLib/GTK 전역 할당만 억제) | C 코드 변경 |
| G6 동시성 | `make tsan` | 관련 모듈 경쟁 상태 없음 | 동시성 변경 |
| G7 GUI | `make e2e` | Xvfb 실행(`scripts/e2e-smoke.sh`), 사용자 시나리오 및 캡처 검증 | UI 변경 |
| G8 부하 | `make stress benchmark` | 합의된 기준 충족, 수치는 §5 형식으로 기록 | 성능·감시 변경 |

- `make verify`는 build, test, format-check, lint, architecture, asan, ubsan, tsan, e2e를 차례로 실행한다(G1, G2, G4~G7). G3·G8은 따로 실행한다.
- `integration`·`stress`·`benchmark`는 `ctest --preset debug -L <라벨> --no-tests=error`라 해당 라벨 테스트가 없으면 비0으로 끝난다. 이 경우 BLOCKED(또는 NOT_RUN)이며 PASS가 아니다. 해당 없음은 QA 근거와 함께 N/A로 적는다.
- 통합 시 적용 가능한 게이트 전체를 다시 실행한다. sanitizer는 별도 빌드에서 실행한다. 로그와 SHA를 보존한다.

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
- [ ] 실패 반환값 및 errno/GError 처리
- [ ] 할당·해제, FD·watch·스레드 종료 경로 확인
- [ ] GTK 메인 스레드 규칙 및 큐 크기 제한
- [ ] 심볼릭 링크, 경로 탈출, 권한, 덮어쓰기 검증
- [ ] inotify 이동 cookie, overflow, 재귀 감시 누락 대응
- [ ] 외부 수정 충돌 및 취소 시 데이터 보존
- [ ] 실제 테스트 결과와 SHA 확인
- [ ] UI 상태·접근성·스크린샷 확인 (해당 시)
- [ ] 민감 정보 로그 노출 및 실제 사용자 파일 접근 없음
- [ ] 메모리·동시성: 소유권(transfer) 표기, 잠금 순서와 잠금 중 금지 호출, free 반환 후 콜백 없음(docs/DESIGN.md 메모리·스레드 부분)

## 4. 테스트 전략

GLib unit, 실제 임시 디렉터리 integration, Xvfb GUI smoke 및 실제 Ubuntu Wayland 수동 검증. 생성/수정/이동/삭제 외부 명령 테스트, overflow 강제 주입, 권한 오류, ENOSPC 모의, symlink loop, archive traversal, 취소 및 종료 경쟁. 실제 홈/시스템 경로 변경 금지. QA는 구현과 분리.

- 디렉터리·이름·라벨 규칙과 임시 경로·조건 대기 규칙은 docs/ARCHITECTURE.md §4.
- 함수별 최소 테스트(정상 1개 이상, 대표 실패 1개 이상, 권한 오류는 `chmod 000`, root면 skip)는 docs/ENGINEERING.md 오류 §6.
- 평가 시나리오 E01~E10은 harness/EVALS.md.

## 5. 성능 테스트와 기준 성능

1만/10만 파일 fixture, 초당 1000 이벤트, 10GB 복사(여유 용량 확인), 24시간 soak. monotonic clock으로 P50/P95/P99, RSS, CPU, queue depth, overflow 기록. NFR 목표와 비교. VM/호스트 사양 및 반복 횟수 기록.

기준 성능: 현재 측정 없음. SHA, Ubuntu, kernel, CPU, RAM, FS, fixture, 명령, 반복 횟수, 원시 결과, P95를 최초 실행 시 기록. 미측정은 NOT_RUN.

## 6. 장애 주입

watch overflow synthetic injection 및 실제 부하 별도 검증; SQLite busy, 디스크 부족, 권한 거부, 파일 작업 중 외부 삭제, worker 취소, 강제 종료. 원본 보존과 gap 표시, 재동기화 확인.

## 7. 테스트 카탈로그

- docs/SRS.md의 기능 ID마다 테스트 ID `TC-<기능 ID>`가 하나 있다(394개, SRS 표 "완료 판정" 열).
- 각 TC의 기본 내용: 해당 기능의 정상 경로를 임시 fixture에서 실행하고 결과를 확인한다. 해당 기능에 적용되는 권한 오류, 취소, 외부 변경 및 모듈 공통 계약을 검증한다. 세부 입력/기대값은 구현 전 작업 명세에서 확정한다.
- 확정된 세부 Given/When/Then은 harness/TASKS.md 각 작업의 수용 조건이 정본이고, 요구사항·작업·테스트 연결은 harness/PROGRESS.md 추적표에 둔다.
- 현재 상태: 모든 TC가 NOT_RUN이다. 독립 QA가 실행해 PASS를 판정하면 추적표에 결과를 적는다.
