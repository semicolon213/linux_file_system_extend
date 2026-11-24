# 요구사항 추적표
아래는 계획 항목이며 구현 또는 테스트 통과를 의미하지 않는다. 테스트 계획의 세부 Given/When/Then은 작업 지시서(harness/tasks/)가 정본이고, TC ID는 docs/testing/TEST_CASES.md를 따른다.

작업: R1-T01 core(002), R1-T02 job(003), R1-T03 fs(004), R1-T04 mon(005), R1-T05 nav(006), R1-T06 ui(007).

## R1 P0

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
| JOB-001 | R1-T02 | TC-JOB-001: FIFO, 동시 1개 | PLANNED |
| JOB-002 | R1-T02, R1-T06 | TC-JOB-002: 메인 루프 비차단, 실행 중 UI 입력(NFR-015) | PLANNED |
| JOB-003 | R1-T02, R1-T06 | TC-JOB-003: 10Hz 진행, indeterminate | PLANNED |
| JOB-008 | R1-T02, R1-T06 | TC-JOB-008: QUEUED/RUNNING 취소, 부분 결과 | PLANNED |
| SEC-001 | R1-T01, R1-T06 | TC-SEC-001: 권한 사전 점검은 권고, 실제 syscall 오류 표시 | PLANNED |
| SEC-002 | R1-T03, R1-T06 | TC-SEC-002: CONFIRMED 없는 위험 작업 거부 | PLANNED |
| SEC-003 | R1-T01, R1-T03 | TC-SEC-003: 보호 경로 거부(쓰기 시도 없음) | PLANNED |

## R2 이후 (참고)

| 요구사항 | 작업 | 테스트 계획 | 상태 |
|---|---|---|---|
| HIS-001 | HIS-db (R2) | 재시작 후 이력 유지 | PLANNED |
| ARC-016 | ARC-safe | 경로 탈출·링크 차단 | PLANNED |

추가 열(구현 SHA, 테스트 경로, 증거 경로, QA 판정)은 통합 시 Integration이 채운다. ROADMAP 미배정 P0(UI, DASH, CFG)는 OPEN_QUESTIONS U-9 결정 후 등록한다.
