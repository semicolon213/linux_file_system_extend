# 진행 상태

Coordinator가 갱신한다. 작업별 ID, 담당, 기준 SHA, 상태, 차단 사유, QA 증거를 기록한다. 작업 상세 상태는 harness/TASKS.md 상태 요약.

## 1. 현재 상태 (2026-11-25)

| 단계 | 상태 |
|---|---|
| 요구사항·설계 계약 | R1 계약 승인(ARCH-001, 8615d98). 사용자 결정 U-1~U-9 기록 완료(§3 결정 기록). C++17 + gtkmm-3.0 판으로 갱신(ADR-0007 승인, ADR-0008 제안, 2026-11-24) |
| 디자인 (메인 창, 트레이) | APPROVED (사용자 확정, 커밋 6d621d5). 루트 PRODUCT.md·DESIGN.md, docs/design/prototype. 언어 변경과 무관하게 유효 |
| 빌드·테스트 하네스 | R0-002 C++ 전환 APPROVED (b7ca5d3) |
| R0-002, R1-T01~T07 | APPROVED (R1 묶음 QA PASS, 8c469a9, docs/qa/r1-qa-report.md). 실제 Ubuntu 수동 검증(보고서 끝 체크리스트 15항목)은 사용자 수행 대기 |
| R2 계약 | 승인 (2026-11-25, Coordinator, ad49d0b): DESIGN R2 추가·DB 스키마, ARCHITECTURE R2 모듈, ADR-0009(제안), TESTING §8, TASKS R2-T00~T11. |
| R2 작업 | R2-T00~T11·F01 R2 묶음 QA PASS(3e49268, docs/qa/r2-qa-report.md). R2-F02(N1 Medium) 진행. APPROVED는 F02 병합과 사용자 실제 Ubuntu 수동 점검(보고서 끝 1~30항목) 뒤 |
| R3~ | PLANNED |

### 이력

- 2026-11-24: 제품 맥락(PRODUCT.md)과 메인 창·트레이 디자인 확정. ADR-0001 승인. R0-001은 개발 패키지 미설치로 BLOCKED 등록(98cc49b) → READY, Codex 배정(6a0c810) → VERIFY(121ef2f, cec5a7f).
- 2026-11-24: ARCH-001 계약 제안(8615d98)과 승인, R1-T01 배정(6c680c3). 사용자 결정 기록(7a8a406), 변경량 1분봉 추가(64ad2b4~6f64a98), E-7·E-9 결정(48cb551).
- 2026-11-24: R1-T01(1de4d68), R1-T05(47f8824), R1-T02(49daab4) 병합.
- 2026-11-24: 문서를 핵심 문서 체계로 통합(AGENTS.md 목차, docs/ 6개, harness/ 4개). 옛 문서 이름 대응은 docs/ARCHITECTURE.md §8.
- 2026-11-24: 구현 언어를 C++17 + gtkmm-3.0으로 전환(사용자 결정, 수업 요구사항). ADR-0007·0008, docs/DESIGN.md 인터페이스 C++ 판, ARCHITECTURE·ENGINEERING·TESTING 갱신. 작업 재편: R0-002 신설(BLOCKED, gtkmm 설치 대기), R1-T01/T02/T05는 C++ 포팅, R1-T03/T04/T07의 C 시도는 중단·폐기 후 BACKLOG.
- 2026-11-24: C++ 판 R0-002, R1-T01/T02/T05/T07 병합(VERIFY). R1-T04 mon 병합(4cc1116, 병합 후 build/test/integration/format-check/lint/architecture/asan/ubsan 0; tsan은 nav 테스트 1회 GLib 내부 경합 보고 후 3회 재실행 모두 0, E-10).
- 2026-11-24: R1-T03 fs 병합(74eb5ae, 작업자 보고는 tsan의 nav 간헐 실패(E-10)로 failed, 병합 후 Coordinator 재실행 게이트 0·tsan 5회 0). R1-T06 ui 착수, E-10 수정 작업 병행.
- 2026-11-25: Codex 모델 gpt-6.2-sol 전환(사용자). R1-FIX-01 tsan 안정화 병합(7252b74), 병합 후 게이트 0·tsan 10회 0.
- 2026-11-25: R1-T06 ui 병합(a148ce3, fast-forward). GUI LSan의 fontconfig 종료 시 캐시 보고는 최소 GTK 창에서도 재현되어 scripts/lsan.supp에 leak:libfontconfig.so.1 승인(Coordinator). 병합 후 make verify 0. R1 구현 완료, 묶음 QA 착수.
- 2026-11-25: R1 묶음 독립 QA(a8d89fa) FAIL. 게이트는 모두 0. F1 Critical: EXDEV 이동 폴백이 복사 실패 원본까지 삭제(데이터 손실, fs.cpp:437-443·522-526). F2: lsan.supp GLib 패턴이 프로젝트 누수를 가림. F3: R1-T06 UI 동작 테스트·수동 검증 기록 없음. F4 재귀 TOCTOU, F5 OVERWRITE 이동 대상 선삭제, F6 tsan 억제 범위, F7 job 종료 UAF 가능성, F8 화면(빈 띠 60px, 1분봉 축 라벨 겹침, 0건 dim). 수정 배정: R1-FIX-02(fs·job·억제), R1-FIX-03(UI 테스트·화면). 세션 집계 0건 dim은 메인 창에도 적용(Coordinator 결정). 실제 Ubuntu 수동 검증은 사용자 환경 필요.
- 2026-11-25: R1-FIX-03(791367c, UI 동작 테스트 make ui-interaction 12묶음, F8 화면, UI 누수) 병합. R1-FIX-02 첫 제출(9aefa0d)은 디버그 심볼 GLib 전제로 기본 환경 tsan 10/10 실패라 반려, 재제출(8388c6c: nav 완료를 GTask 밖으로, tsan.supp는 race:^g_queue_pop_tail$ 하나, lsan.supp는 fontconfig만) 병합. 병합 후 verify·integration·stress·benchmark·ui-interaction 0, 기본 GLib tsan 10회 0. E-10 억제 범위 축소로 F2·F6 해소. e2e 간헐 실패(빈 smoke 출력)와 취소 문구 덮어쓰기는 FIX-03 후속.
- 2026-11-25: R1-FIX-03 후속(1408171: e2e 전용 D-Bus로 간헐 실패 해소, 실패 단계·stderr 출력, 미감시 띠와 취소 결과 띠 동시 표시) 병합. 병합 후 make verify 3회 0, ui-interaction 0. R1 재QA 착수.
- 2026-11-25: R1 재QA PASS(8c469a9). F1~F8 해소 재현 확인. 새 발견 Low: N1 copy+OVERWRITE 디렉터리 대상 선삭제(fs.cpp:473), N2 EXDEV 이동 후 원본 검사가 최상위만(fs.cpp:614-623), N3 목록 사전순 정렬 → R2 착수 전 정리 작업으로 배정 예정. R0-002·R1 APPROVED.
- 2026-11-25: R2 범위 사용자 결정(§3 결정 기록 "R2 범위"). R2 계약 제안: DESIGN 인터페이스 §3·§4·§7 R2 추가와 §8 his·§9 diff·§10 search·§11 trash, 이벤트 §10 이력 연결, DB 스키마 확정안, ARCHITECTURE R2 모듈, ADR-0009(제안), TESTING §8, ENGINEERING 보안 §6, PRD 릴리스 줄. 작업 R2-T00~T11 등록(N1~N3은 R2-T00). 패키지 확인: libsqlite3-dev 3.45.1, libayatana-appindicator3-dev 0.5.93 설치됨(WSL Ubuntu-24.04, dpkg -s).
- 2026-11-25: R2 계약·ADR-0009 승인(Coordinator). U-10 사용자 결정: 시안 없는 R2 화면(검색 결과, 휴지통 목록, 버전 목록, 미리보기·저장 충돌, 이력 끔)은 Impeccable로 시안을 먼저 확정한 뒤 구현. U-11 사용자 결정: 머리줄 칸은 시안대로 "이 폴더에서 검색", 주소 입력은 Ctrl+L·경로 표시줄 편집으로 이동. R2-T00·T01 Codex 착수.
- 2026-11-25: U-10 R2 화면 시안 확정(사용자 승인): docs/design/prototype/r2-screens.html, 루트 DESIGN.md "R2 화면" 절.
- 2026-11-25: R2-T01(4849475), R2-T00(7e980f5), R2-T06(82a7c77) 병합. 병합 상태 게이트 직접 재실행: verify·integration·stress·ui-interaction 0, tsan 10/10(배포판 GLib). 컴파일러 GCC(g++) 고정 결정(사용자): ENGINEERING 빌드 절, CMakePresets 고정은 다음 빌드 파일 작업에서.
- 2026-11-25: R2-T08(51cac30), R2-T02(e81de30), R2-T03(35111c2), R2-T04(a0a85b1) 병합. 병합 상태 게이트 직접 재실행: verify·integration·stress·ui-interaction 0, tsan 10/10. G8 his 기록 부하 수치는 TESTING §5. ADR-0009 블롭 경로 예시를 DESIGN DB §1에 맞춤. 미실행: 타 UID 권한 검사(R2-T04), 기준 스냅샷 2만 파일(R2-T05). Orca run을 run_b93fd9e78a9c로 새로 만듦.
- 2026-11-25: R2-T07(264d259), R2-T05(74fcebe) 병합. 병합 상태 게이트 직접 재실행(각각): verify·integration·stress·benchmark·ui-interaction 0, tsan 10/10. HIS-011 수용 조건을 DB §5.3 중복 생략에 맞춤(37a152b). 미검증: gedit·VS Code 실제 저장의 자동 버전(R2 QA·수동 점검 대상). TASKS 요약 표 R2 상태 갱신.
- 2026-11-25: R2-T09(38c4cad) 병합. 병합 상태 게이트 직접 재실행: verify·integration·stress·benchmark·ui-interaction 0, tsan 10/10. 작업자 보고: GUI TSan(앱 실행) 종료 1, 배포판 GLib gdbus 스레드 malloc/free 경쟁이 기준 2a81905에서도 재현(억제 미추가, QA 기록 대상). 한글 글꼴 데스크톱 시각 확인 미실행.
- 2026-11-25: R2-T10(cde40b1) 병합. 병합 상태 게이트 직접 재실행: verify·integration·stress·benchmark·ui-interaction 0, tsan 10/10. 작업자 보고: test_his_versions /his/versions/shutdown 간헐 실패(약 1/33, 기준 17411e6 재현) → R2-F01 등록(GCC preset 고정 포함). 실제 GNOME 트레이·Wayland/X11 위치 확인과 GUI TSan(GLib gdbus 보고) NOT_RUN.
- 2026-11-26: R2-F01(f6b1cd2: his 종료 테스트 수정, CMakePresets g++ 고정), R2-T11(c08cce1) 병합. 병합 상태 게이트 직접 재실행(각각): verify·integration·stress·benchmark·ui-interaction 0, tsan 10/10. 루트 DESIGN.md 검색 진행 띠를 search 계약(진행 중 찾은 수만, 완료 시 폴더·접근 불가 수)에 맞춤. 작업자 보고 위험: 트레이 호스트 없는 Wayland ASan에서 libayatana-appindicator 읽기 오류(기준 재현) → R2 QA 판정 대상. R2 묶음 독립 QA 착수.
- 2026-11-26: R2 묶음 독립 QA PASS(a5ed03f, 보고서 3e49268). 게이트 전부 0, tsan 10/10, shutdown 200회 0 실패, 기준 스냅샷 2만 파일 34.3초·조회 1.75ms. 발견: N1 Medium(트레이 호스트 없을 때 libayatana-appindicator 범위 밖 읽기, ASan 10/10 크래시, 일반 빌드 0/20) → R2-F02 배정. N2 Low(기준 스냅샷 순회가 EACCES 외 오류에 조용히 종료, 데이터 손실 없음) → R3 후보로 수용. GUI TSan 보고는 R1부터 있던 시스템 라이브러리 보고로 판정(회귀 아님).

## 2. 요구사항 추적표

아래는 계획 항목이며 구현 또는 테스트 통과를 의미하지 않는다. 테스트 계획의 세부 Given/When/Then은 작업 지시서(harness/TASKS.md)가 정본이고, TC ID는 docs/TESTING.md §7를 따른다.

작업: R1-T01 core(002), R1-T02 job(003), R1-T03 fs(004), R1-T04 mon(005), R1-T05 nav(006), R1-T06 ui(007), R1-T07 dash(008).

### R1 P0

| 요구사항 | 작업 | 테스트 계획 | 상태 |
|---|---|---|---|
| NAV-001 | R1-T05, R1-T06 | TC-NAV-001: list_dir 항목·링크·권한 오류·취소, 화면 이동 | PLANNED |
| NAV-002 | R1-T05, R1-T06 | TC-NAV-002: 이력 back/forward/상한, Alt+←/→/↑ | PLANNED |
| NAV-003 | R1-T05, R1-T06 | TC-NAV-003: breadcrumb 분해, 비 UTF-8 보존, 클릭 이동 | PLANNED |
| NAV-004 | R1-T05, R1-T06 | TC-NAV-004: 디렉터리 후보 이름순·상한 (화면은 U-6) | PLANNED |
| NAV-005 | R1-T05, R1-T06 | TC-NAV-005: HOME 임시 경로 | PLANNED |
| NAV-006 | R1-T06 | TC-NAV-006: 탭별 독립 이력 | PLANNED |
| NAV-010 | R1-T05, R1-T06 | TC-NAV-010: 저장·재로드·중복·깨진 파일 | PLANNED |
| NAV-013 | R1-T05, R1-T06 | TC-NAV-013: dirs_only, 비동기 펼치기 | PLANNED |
| NAV-014 | R1-T05, R1-T06 | TC-NAV-014: show_hidden 토글 | PLANNED |
| FS-001 | R1-T03, R1-T06 | TC-FS-001: 생성·중복·권한 오류·잘못된 이름 | PLANNED |
| FS-002 | R1-T03, R1-T06 | TC-FS-002: 폴더 생성·중복 | PLANNED |
| FS-003 | R1-T06 | TC-FS-003: 기본 앱 실행 요청(argv, shell 없음) | PLANNED |
| FS-004 | R1-T03 | TC-FS-004: 읽기, 4MiB 한도 | PLANNED |
| FS-005 | R1-T03, R1-T06 | TC-FS-005: stamp 충돌(E07), 원자 저장, 모드 유지 | PLANNED |
| FS-006 | R1-T03, R1-T06 | TC-FS-006: 임시 XDG 휴지통, 미지원 시 자동 영구 삭제 없음 | PLANNED |
| FS-007 | R1-T03, R1-T06 | TC-FS-007: CONFIRMED 없으면 거부, 확인 후 삭제 | PLANNED |
| FS-008 | R1-T03, R1-T06 | TC-FS-008: NOREPLACE 충돌 | PLANNED |
| FS-009 | R1-T03, R1-T06 | TC-FS-009: 트리 복사, 링크·FIFO, 취소 정리(E06) | PLANNED |
| FS-010 | R1-T03, R1-T06 | TC-FS-010: 같은 장치 rename, EXDEV 폴백, 자기 하위 거부 | PLANNED |
| FS-011 | R1-T06 | TC-FS-011: 잘라내기 후 붙여넣기 = 이동 | PLANNED |
| FS-012 | R1-T03, R1-T06 | TC-FS-012: 붙여넣기 = 복사/이동 제출 | PLANNED |
| FS-013 | R1-T06 | TC-FS-013: 다중 선택 일괄 작업 | PLANNED |
| FS-014 | R1-T03, R1-T06 | TC-FS-014: 일괄 삭제 부분 실패(PARTIAL) | PLANNED |
| FS-018 | R1-T03, R1-T06 | TC-FS-018: FAIL/SKIP/KEEP_BOTH/OVERWRITE | PLANNED |
| MON-001 | R1-T04 | TC-MON-001: CREATE (E01) | PLANNED |
| MON-002 | R1-T04 | TC-MON-002: DELETE (E01) | PLANNED |
| MON-003 | R1-T04 | TC-MON-003: MODIFY (E01) | PLANNED |
| MON-006 | R1-T04 | TC-MON-006: CLOSE_WRITE, stat size | PLANNED |
| MON-008 | R1-T04 | TC-MON-008: 밖으로 이동 → MOVED_OUT | PLANNED |
| MON-009 | R1-T04 | TC-MON-009: 안으로 이동 → MOVED_IN | PLANNED |
| MON-010 | R1-T04 | TC-MON-010: 같은 디렉터리 RENAME | PLANNED |
| MON-011 | R1-T04 | TC-MON-011: ATTRIB | PLANNED |
| MON-012 | R1-T04, R1-T06 | TC-MON-012: 루트 DELETE_SELF → STOPPED, stale 표시 | PLANNED |
| MON-013 | R1-T04 | TC-MON-013: 루트 MOVE_SELF → STOPPED | PLANNED |
| MON-014 | R1-T04 | TC-MON-014: UNMOUNT 정규화 단위 테스트(실제는 E-2) | PLANNED |
| MON-015 | R1-T04 | TC-MON-015: 합성 overflow → OVERFLOW 이벤트 | PLANNED |
| MON-016 | R1-T04 | TC-MON-016: IN_IGNORED registry 정리, 비노출 | PLANNED |
| MON-017 | R1-T04, R1-T06 | TC-MON-017: 외부 변경 → 목록 반영 2초 안, 지연 기록 | PLANNED |
| MON-018 | R1-T04 | TC-MON-018: 재귀 등록, 링크·마운트 경계 미추적 (E02) | PLANNED |
| MON-019 | R1-T04 | TC-MON-019: mkdir -p 직후 파일 CREATE(합성) | PLANNED |
| MON-020 | R1-T04 | TC-MON-020: 루트 2개 watch_id 구분 | PLANNED |
| MON-021 | R1-T04, R1-T06 | TC-MON-021: 등록·해제·겹침 거부, "감시 시작" 띠 | PLANNED |
| MON-027 | R1-T04 | TC-MON-027: 배치 내 MODIFY 병합, 순서 보존 | PLANNED |
| MON-028 | R1-T04 | TC-MON-028: cookie 짝·시간창 만료 | PLANNED |
| MON-029 | R1-T04, R1-T06 | TC-MON-029: gap → 재탐색 → RESYNC_DONE, 큐 포화 (E03) | PLANNED |
| JOB-001 | R1-T02 | TC-JOB-001: FIFO, 동시 1개 | PLANNED (C 구현은 C++ 포팅 대상, ADR-0007) |
| JOB-002 | R1-T02, R1-T06 | TC-JOB-002: 메인 루프 비차단, 실행 중 UI 입력(NFR-015) | PLANNED |
| JOB-003 | R1-T02, R1-T06 | TC-JOB-003: 10Hz 진행, indeterminate | PLANNED |
| JOB-008 | R1-T02, R1-T06 | TC-JOB-008: QUEUED/RUNNING 취소, 부분 결과 | PLANNED |
| SEC-001 | R1-T01, R1-T06 | TC-SEC-001: 권한 사전 점검은 권고, 실제 syscall 오류 표시 | PLANNED |
| SEC-002 | R1-T03, R1-T06 | TC-SEC-002: CONFIRMED 없는 위험 작업 거부 | PLANNED |
| SEC-003 | R1-T01, R1-T03 | TC-SEC-003: 보호 경로 거부(쓰기 시도 없음) | PLANNED |
| DASH-001 | R1-T07, R1-T06 | TC-DASH-001: 세션 집계·헤더 지표(최근 1분, 반영 지연 P95) | PLANNED |
| DASH-002 | R1-T04, R1-T07, R1-T06 | TC-DASH-002: 루트 크기 색인·size_delta/root_total 불변식(T04), 1분봉 시·고·저·종·거래량·링 버퍼(T07), 하단 패널 탭·새 창·키보드 툴팁(T06). P1을 사용자 결정으로 R1에 포함 | PLANNED |
| DASH-003 | R1-T06 | TC-DASH-003: 이벤트 로그 500행, 행 선택 시 목록 선택 | PLANNED |
| DASH-005 | R1-T06 | TC-DASH-005: 칩·경로 거르기가 원본을 바꾸지 않음 | PLANNED |

### R2 (데모 핵심, 2026-11-25 사용자 결정)

| 요구사항 | 작업 | 테스트 계획 | 상태 |
|---|---|---|---|
| HIS-001 | R2-T04, R2-T09 | TC-HIS-001: events·gaps 기록, 재열기 후 유지, 강제 종료 후 integrity_check(NFR-014), orphan 정리 | PLANNED |
| HIS-005 | R2-T04, R2-T05 | TC-HIS-005: 쓰기 완료·편집기 rename 저장 → 자동 버전, 300ms 합치기, 같은 내용 무시, 제외 경로, 기준 스냅샷, 보관 한도 | PLANNED |
| HIS-007 | R2-T03, R2-T04, R2-T09 | TC-HIS-007: 버전 내용 조회, 전체 이력에서 고른 버전과 최신 비교 | PLANNED |
| HIS-008 | R2-T03, R2-T09 | TC-HIS-008: Myers 줄 Diff 골든·성질 검사(md 포함), 패널 통합 줄 Diff | PLANNED |
| HIS-009 | R2-T03, R2-T09 | TC-HIS-009: 격자 이미지 비교 영역, 패널 나란히·겹쳐 밀기 | PLANNED |
| HIS-011 | R2-T02, R2-T05, R2-T09 | TC-HIS-011: CONFIRMED 필요, PRE_RESTORE 보관 후 install_file, 인라인 확인 | PLANNED |
| HIS-012 | R2-T02, R2-T05, R2-T09 | TC-HIS-012: 삭제 표시, 원래 경로 복구, 같은 이름 있으면 EXISTS, 버전 없으면 복구 불가(E09) | PLANNED |
| MON-022 | R2-T06, R2-T10 | TC-MON-022: PAUSED 전이, 일시 정지 중 이벤트 없음·색인 유지, 재개 후 총용량 일치, 트레이·요약 창 표시 | PLANNED |
| UI-012 | R2-T09 | TC-UI-012: 변경 내용 패널 토글, 재시작 후 유지 | PLANNED |
| 트레이·요약 창 (기능 ID 없음, PRODUCT.md) | R2-T10 | 메뉴 모델 단위 테스트, 요약 창 지표가 dash와 같음, 창 닫아도 감시 유지(연결 재현 또는 수동 검증, E-14) | PLANNED |
| SRC-001 | R2-T08, R2-T11 | TC-SRC-001: 이름 부분 일치(casefold), 숨김, 비 UTF-8 | PLANNED |
| SRC-003 | R2-T08, R2-T11 | TC-SRC-003: 재귀·비재귀, 링크 미추적, 접근 불가 기록, 취소 | PLANNED |
| SRC-004 | R2-T08, R2-T11 | TC-SRC-004: 확장자 거르기 | PLANNED |
| TRASH-001 | R2-T07, R2-T11 | TC-TRASH-001: 홈 휴지통(XDG_DATA_HOME/Trash) 읽기, 미지원 시 자동 영구 삭제 없음 | PLANNED |
| TRASH-002 | R2-T07, R2-T11 | TC-TRASH-002: 목록(원래 경로·삭제 시각·크기), info 없음·깨짐 처리 | PLANNED |
| TRASH-003 | R2-T07, R2-T11 | TC-TRASH-003: 원위치 복원, 충돌 EXISTS, 부모 없음 NOT_FOUND | PLANNED |
| TRASH-005 | R2-T02, R2-T07, R2-T11 | TC-TRASH-005: 비우기 CONFIRMED 필요 | PLANNED |
| TRASH-006 | R2-T02, R2-T07, R2-T11 | TC-TRASH-006: 선택 삭제 CONFIRMED 필요 | PLANNED |
| PRE-001 | R2-T11 | TC-PRE-001: 읽기 전용 미리보기, 4MiB 한도, 비 UTF-8 거부 | PLANNED |
| PRE-003 | R2-T11 (R1 FS-005 편집기 재사용) | TC-PRE-003: 저장, 외부 변경 충돌 선택(E07) | PLANNED |
| R1 QA N1~N3 | R2-T00 | copy OVERWRITE 대상 보존, EXDEV 원본 삭제는 복사한 항목만, 자연 순서 정렬 | PLANNED |

### R3 이후 (참고)

| 요구사항 | 작업 | 테스트 계획 | 상태 |
|---|---|---|---|
| HIS·SRC·PRE·TRASH의 나머지 P1·P2, docx·odt 내부 비교 | R3 | R2 결정으로 미룸 | PLANNED |
| ARC-016 | ARC-safe | 경로 탈출·링크 차단 | PLANNED |

추가 열(구현 SHA, 테스트 경로, 증거 경로, QA 판정)은 통합 시 Integration이 채운다. UI P0와 CFG-001 행은 U-9 결정 범위로 R1-T06 수용 조건에서 추적한다. 변동 열(U-4)은 MON-017 행과 R1-T06 수용 조건에서 추적한다.

## 3. 미결 사항과 결정 기록 (ARCH-001 기준)

계약 문서에서 `미결 U-n`/`E-n`으로 참조한다. U는 사용자 결정이 필요한 것, E는 기술 확인(측정·환경)으로 닫을 수 있는 것이다. 닫으면 결정과 날짜를 적고 관련 문서를 고친다.

### 사용자 결정 필요

| ID | 질문 | 영향 | 현재 계약의 기본값 |
|---|---|---|---|
| U-1 | R1 화면 범위: 이벤트 로그·세션 집계·헤더 지표(DASH-001/003/005는 P0지만 PRD §3 릴리스에 릴리스 미배정), 트레이·요약 창(PRODUCT.md 요구, 기능 ID 없음), 창을 닫아도 감시 유지 동작을 R1에 넣는가 | R1-T06 범위, DESIGN 스레드 §4 종료 시점 | R1 제외, 변경 내용 패널은 R2(이력 의존) |
| U-2 | 트레이·요약 창의 "일시 정지"는 MON-022(P2)다. 트레이를 넣는다면 일시 정지도 같은 릴리스로 당기는가 | mon 상태 머신에 PAUSED 추가 | R1에 없음 |
| U-3 | 겹치는 감시 루트(같음, 조상, 자손) 등록을 거부해도 되는가 | add_root 동작 | 거부(FS_ERROR_EXISTS), ADR-0003 |
| U-4 | 파일 목록 "변동" 열의 기준: 직전 이벤트 대비인가, 감시(세션) 시작 대비인가 | ui 모델 | 미정. 루트 DESIGN.md는 표시 형식만 정함 |
| U-5 | DEGRADED(WATCH_LIMIT, 일부 하위 미감시)와 ERROR 상태의 띠 문구·형태 | ui 상태 표시 | 시안 없음 |
| U-6 | 시안 없는 R1 화면: 작업 진행률·취소, 붙여넣기 충돌 정책 선택, 영구 삭제 확인, 텍스트 편집(FS-005), 새 이름 입력(생성·이름 변경), 주소 입력·자동완성(NAV-004) | R1-T06 | GTK 기본 위젯, 스타일 없음, `design-pending` 표시 |
| U-7 | 시스템 클립보드(Nautilus 호환 복사·붙여넣기)와 GTK 북마크(~/.config/gtk-3.0/bookmarks) 연동 여부 | fs/ui, nav | 앱 내부 클립보드, 자체 bookmarks.ini |
| U-8 | 보호 경로 목록에 /opt, /srv, /mnt, /media를 넣는가, /var 전체 보호가 맞는가(/var/www 개발 사용) | ENGINEERING 보안 §2 | 표의 목록 |
| U-9 | PRD §3 릴리스에 릴리스가 배정되지 않은 P0 ID(UI-001/005/007/011/012/015/016/018~022/025, DASH-001/003/005, CFG-001)의 릴리스 배정 | PROGRESS 추적표, R1-T06 범위 | R1-T06이 NAV/FS/MON/JOB 시연에 필요한 UI만 다룸 |
| U-10 | 시안 없는 R2 화면: 검색 결과 목록과 확장자 거르기 입력, 휴지통 목록, 전체 이력(버전 목록), 텍스트 미리보기·저장 충돌 선택, 이력 끔 표시, 트레이 감시 경로가 3개를 넘을 때 | R2-T09·T10·T11 | 해결: 시안 확정(사용자 승인, 2026-11-25) → docs/design/prototype/r2-screens.html, DESIGN.md "R2 화면" |
| U-11 | 머리줄 검색 칸: R1은 주소 입력·자동완성(NAV-004)으로 쓴다. 시안은 "이 폴더에서 검색"이다 | R2-T11, NAV-004 화면 | 결정: 시안대로 검색 칸, 주소는 Ctrl+L(사용자, 2026-11-25) |

### 기술 확인

| ID | 내용 | 닫는 방법 |
|---|---|---|
| E-1 | MOVE 시간창 50ms, 배치 16ms/256개, 전달 큐 8192, 이벤트 전달 P95 50ms 목표는 측정 전 가정 | R1-T04 G8 결과로 조정, DESIGN 이벤트 갱신 |
| E-2 | 실제 커널 IN_Q_OVERFLOW·IN_UNMOUNT는 root 없이 재현이 어렵다 | 합성 주입으로 계약 검증, 실제는 수동 시연 환경에서 관찰 기록 |
| E-3 | 재귀 등록 중 권한 없는 하위 디렉터리를 DEGRADED로 알릴지 | R1-T04 구현 중 UI 표시 필요성과 함께 Coordinator 결정 |
| E-4 | 작업 대기열 상한(현재 없음) | 사용 패턴 확인 후 JOB-010(P1)에서 결정 |
| E-5 | 텍스트 읽기 한도 4MiB의 근거 | FS-005 편집 화면 성능 확인 후 조정 |
| E-6 | R0-001(C)은 대체됨. R0-002(C++)의 독립 QA | R1 묶음 QA에서 처리 |

### 결정 기록 (2026-11-24, 사용자)

- U-1: R1에 메인 창의 이벤트 로그·세션 집계·헤더 지표(DASH-001/003/005)를 포함한다. 트레이·요약 창·창 닫아도 감시 유지·변경 내용 패널은 R2.
- U-2: 일시 정지(MON-022)는 트레이와 함께 R2.
- U-3: 기본값 승인(겹치는 감시 루트 거부, ADR-0003).
- U-4: 변동 열은 직전 이벤트 대비 증감.
- U-5: 권한 부족·WATCH_LIMIT·ERROR는 루트 DESIGN.md 반전 띠(진행 중 경고), 해결된 알림은 quiet 띠.
- U-6: 시안 없는 R1 화면은 새 시안 없이 루트 DESIGN.md 토큰·규칙으로 구현하고, 묶음 QA에서 화면을 확인한다.
- U-7: 기본값 승인(앱 내부 클립보드, 자체 bookmarks.ini).
- U-8: 기본값 승인(ENGINEERING 보안 표의 보호 경로).
- U-9: R1-T06은 U-1 범위의 DASH-001/003/005와 메인 창 시연에 필요한 UI P0(UI-001/005/007/011/012/015/016/018~022/025)를 다룬다. CFG-001은 설정 저장이 필요한 범위(즐겨찾기·창 상태)만.
- 계약 보완(R1-T01 질의, C판. C++ 판은 `core::make_event`로 대체, ADR-0008): `FsEvent *fs_event_new_internal(const FsEvent *source);` 내부 헤더 전용, 깊은 복사, refcount=1, source는 borrowed. 공개 API의 FsEvent는 opaque.
- 변경량 1분봉 (2026-11-24, 사용자, R1): PRODUCT.md·main-window.html 하단 패널 탭. 계약 반영: mon 루트 크기 색인과 이벤트 size_delta/root_total(DESIGN 이벤트 §8), 루트 총용량 조회(DESIGN 인터페이스 §4), dash 모듈(DESIGN 인터페이스 §6, ARCHITECTURE §7), ADR-0006. U-4 변동 열은 이벤트 size_delta를 쓴다.
- 독립 QA 단위 (2026-11-24, 사용자): 독립 QA는 작업마다 하지 않고 큰 작업 단위(R1 전체)로 묶어 한 번 한다. R0-001도 R1 묶음 QA에 포함한다(E-6).
- 작업 분담 (2026-11-24, 사용자): Codex가 코드와 문서 수정을 모두 구현하고, Claude 메인 세션은 배정·리뷰·병합만 한다. R1-T03·T04·T07은 Codex 사용량 한도로 일시 중지.
- 작업 분담 변경 (2026-11-24, 사용자): 문서 수정은 다시 Claude 메인 세션이 맡는다. Codex는 코드만 구현한다.

### 기술 확인 (1분봉 추가분)

| ID | 내용 | 닫는 방법 |
|---|---|---|
| E-7 | 하드 링크는 경로마다 총용량에 더해진다(중복 집계). 디스크 모듈 공통 계약의 "하드 링크 중복 집계 방지"와 다르다 | 결정(2026-11-24 Coordinator): R1 시연 범위에서 허용. R2 이력·디스크 작업에서 (dev, ino) 집계로 교체 |
| E-8 | 10만 파일 색인 16MiB 예산과 등록 시간은 측정 전 가정 | R1-T04 G8 기록: 10만 파일/1만 디렉터리 등록 0.247s, RSS +9.7MiB, 10초 1만 작업 gap 없음(WSL, 4cc1116) |
| E-10 | tsan에서 nav 테스트가 간헐적으로 GLib 스레드 풀 내부(g_queue_pop_tail) 경합을 보고. 비계측 libglib 오탐 추정 | 해결(7252b74): 억제 없이 재현 후 비계측 GLib 동기화 오탐으로 판정, tests/tsan.supp(called_from_lib:libglib) + nav GTask 동기화 표시. 병합 후 tsan 10회 0. 남은 위험: GLib 직접 호출 인터셉터의 실제 경합도 가려짐(묶음 QA 확인) |
| E-9 | R1-T01(진행 중)의 FsEvent에 size_delta/root_total 필드·getter, 루트 CMake에 dash 하위 디렉터리 추가가 필요하다 | 결정: 진행 중인 R1-T01에 보완 전달(2026-11-24) |

### 결정 기록 (2026-11-24, 사용자): 구현 언어 C++17 + gtkmm-3.0

- 결정: 수업 요구사항으로 구현 언어를 C++17로 바꾼다. GUI 툴킷은 GTK3를 유지하고 C++ 바인딩 gtkmm-3.0(glibmm, giomm, sigc++)을 쓴다. ADR-0007(승인), ADR-0001 상태 줄에 반영.
- 유지: 루트 DESIGN.md의 시각 디자인과 GTK3 구현 메모(gtkmm 클래스로 읽음), docs/DESIGN.md의 모든 수치와 동작(이벤트 필드, size_delta/root_total, MOVE 시간창 50ms, 배치 16ms/256개, 전달 큐 8192, 작업 상태, 오류 코드, 보호 경로, 탐색 동작, 1분봉 규칙), 작업별 수용 조건.
- C++ 관용 규칙(Architect, ADR-0008): 오류는 `core::Error` + `core::Result<T>`(예외는 모듈·스레드 경계를 넘지 않음), 메인 스레드 콜백은 sigc++·다른 스레드 함수는 `std::function`, 스레드→메인 전달은 `Glib::Dispatcher`, RAII(`new`/`delete` 금지), 경로는 `std::string` 원본 바이트·화면은 `Glib::ustring`, 배치는 `src/<m>/` 유지(`.hpp`/`.cpp`), 테스트는 GLib `g_test` 유지.
- 작업 재편: R0-001(C 하네스)은 R0-002로 대체. R1-T01/T02/T05는 C++ 포팅(수용 조건 동일). R1-T03/T04/T07의 C 시도(일시 중지 상태)는 중단하고 결과를 버린다. 새 C++ 계약으로 BACKLOG에서 다시 시작한다.
- 선행 조건(BLOCKED): R0-002는 사용자가 WSL Ubuntu-24.04에 `sudo apt install libgtkmm-3.0-dev`를 실행했다고 확인한 뒤에만 READY가 된다(에이전트는 sudo를 쓰지 않는다).
- 해소(2026-11-24): 사용자가 설치함. gtkmm-3.0 3.24.9, glibmm 2.66.7, sigc++ 2.12.1 확인. ADR-0008은 Coordinator 승인.

### 결정 기록 (2026-11-25, 사용자): R2 범위

- R2는 데모 핵심만 한다: HIS-001(SQLite 이력), HIS-005(변경 시 자동 버전), HIS-007/008/009(버전 비교, md를 포함한 텍스트 줄 Diff, 이미지 전후 비교), HIS-011(버전 복원), HIS-012(삭제 파일 복구), 메인 창 변경 내용 패널(UI-012 패널 접기), 트레이 아이콘·네이티브 메뉴·요약 창·일시 정지(MON-022)·메인 창을 닫아도 감시 유지(U-1·U-2에서 R2로 미룬 것), 휴지통 화면 TRASH-001/002/003/005/006, 검색 P0 SRC-001/003/004, 미리보기·편집 P0 PRE-001/003(R1 FS-005 편집기를 다시 쓰고 겹치는 구현을 만들지 않음).
- docx·odt 내부 텍스트 블록·첨부 이미지 비교는 R3(libarchive와 함께). R2 Diff는 텍스트(줄 단위, md 포함)와 이미지 전후 비교다. 바이너리는 "바이너리 변경, 크기 증감"만 보인다.
- 나머지 HIS·SRC·PRE·TRASH의 P1·P2는 R3. docs/PRD.md §3 반영.
- R1 QA Low N1(copy + OVERWRITE 디렉터리 대상 선삭제), N2(EXDEV 이동 뒤 최상위만 검사), N3(사전순 정렬)는 R2 시작 정리 작업 R2-T00으로 한다.
- 계약 반영(Coordinator, 제안): DESIGN 인터페이스 §3·§4·§7 R2 추가와 §8~§11, 이벤트 §10, 스레드·상태·메모리 R2 항목, DB 스키마, ARCHITECTURE R2 모듈(검색 모듈 디렉터리는 `src/search`, pre 모듈은 R3로 미룸), ADR-0009(제안), TESTING §8, ENGINEERING 보안 §6. 휴지통은 홈 휴지통을 규격대로 직접 읽는다(gvfsd-trash 의존 없음, E-13).

### 기술 확인 (R2)

| ID | 내용 | 닫는 방법 |
|---|---|---|
| E-11 | 보관 한도(경로별 20개, 파일 8MiB, 전체 512MiB, 이벤트 20만 행·30일)는 측정 전 가정 | R2-T04·T05 G8 기록과 실제 데모 저장소 크기로 조정, DESIGN DB §6 갱신 |
| E-12 | 기준 스냅샷 한도(파일 1MiB, 루트당 2만 개·128MiB)와 캡처 지연 300ms, 미기록 이벤트 상한 5만은 측정 전 가정 | R2-T05 G8(2만 파일 순회 시간, 캡처 처리량)과 편집기 저장 수동 검증 후 조정 |
| E-13 | 휴지통 목록은 홈 휴지통($XDG_DATA_HOME/Trash)만. 다른 마운트의 .Trash-$uid에 버린 항목은 보이지 않는다 | R3에서 마운트별 휴지통 또는 trash:/// 사용을 다시 결정 |
| E-14 | Xvfb·dbus-run-session에는 StatusNotifier 호스트가 없어 트레이 연결 동작을 자동으로 재현하기 어렵다 | R2-T10에서 테스트용 StatusNotifierWatcher로 재현 시도, 안 되면 NOT_RUN과 실제 Ubuntu 수동 검증 |
| E-15 | 편집기마다 저장 방식(직접 쓰기, 임시 파일 rename, 백업 파일)이 달라 자동 버전 시점이 다를 수 있다 | R2-T05 통합 테스트 두 방식 + gedit·vim·VS Code 수동 확인 |
