# 진행 상태

Coordinator가 갱신한다. 작업별 ID, 담당, 기준 SHA, 상태, 차단 사유, QA 증거를 기록한다. 작업 상세 상태는 harness/TASKS.md 상태 요약.

## 1. 현재 상태 (2026-11-24)

| 단계 | 상태 |
|---|---|
| 요구사항·설계 계약 | R1 계약 승인(ARCH-001, 8615d98). 사용자 결정 U-1~U-9 기록 완료(§3 결정 기록). C++17 + gtkmm-3.0 판으로 갱신(ADR-0007 승인, ADR-0008 제안, 2026-11-24) |
| 디자인 (메인 창, 트레이) | APPROVED (사용자 확정, 커밋 6d621d5). 루트 PRODUCT.md·DESIGN.md, docs/design/prototype. 언어 변경과 무관하게 유효 |
| 빌드·테스트 하네스 | R0-002 C++ 전환 VERIFY (b7ca5d3) |
| R1-T01 core, R1-T02 job, R1-T05 nav, R1-T07 dash, R1-T04 mon | VERIFY (1bdb270, 91ba40b, 2eab368, 08424c7, 4cc1116). 개발자 게이트 통과, 묶음 QA 대기 |
| R1-T03 fs | VERIFY (74eb5ae). 병합 후 게이트 0, tsan 5회 0 |
| R1-T06 ui | IN_PROGRESS (Codex, 기준 f25513f). E-10 tsan 안정화 작업 병행 |
| UI 및 고급 기능 (R2~) | PLANNED |

### 이력

- 2026-11-24: 제품 맥락(PRODUCT.md)과 메인 창·트레이 디자인 확정. ADR-0001 승인. R0-001은 개발 패키지 미설치로 BLOCKED 등록(98cc49b) → READY, Codex 배정(6a0c810) → VERIFY(121ef2f, cec5a7f).
- 2026-11-24: ARCH-001 계약 제안(8615d98)과 승인, R1-T01 배정(6c680c3). 사용자 결정 기록(7a8a406), 변경량 1분봉 추가(64ad2b4~6f64a98), E-7·E-9 결정(48cb551).
- 2026-11-24: R1-T01(1de4d68), R1-T05(47f8824), R1-T02(49daab4) 병합.
- 2026-11-24: 문서를 핵심 문서 체계로 통합(AGENTS.md 목차, docs/ 6개, harness/ 4개). 옛 문서 이름 대응은 docs/ARCHITECTURE.md §8.
- 2026-11-24: 구현 언어를 C++17 + gtkmm-3.0으로 전환(사용자 결정, 수업 요구사항). ADR-0007·0008, docs/DESIGN.md 인터페이스 C++ 판, ARCHITECTURE·ENGINEERING·TESTING 갱신. 작업 재편: R0-002 신설(BLOCKED, gtkmm 설치 대기), R1-T01/T02/T05는 C++ 포팅, R1-T03/T04/T07의 C 시도는 중단·폐기 후 BACKLOG.
- 2026-11-24: C++ 판 R0-002, R1-T01/T02/T05/T07 병합(VERIFY). R1-T04 mon 병합(4cc1116, 병합 후 build/test/integration/format-check/lint/architecture/asan/ubsan 0; tsan은 nav 테스트 1회 GLib 내부 경합 보고 후 3회 재실행 모두 0, E-10).
- 2026-11-24: R1-T03 fs 병합(74eb5ae, 작업자 보고는 tsan의 nav 간헐 실패(E-10)로 failed, 병합 후 Coordinator 재실행 게이트 0·tsan 5회 0). R1-T06 ui 착수, E-10 수정 작업 병행.
- 2026-11-25: Codex 모델 gpt-6.2-sol 전환(사용자). R1-FIX-01 tsan 안정화 병합(7252b74), 병합 후 게이트 0·tsan 10회 0.

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

### R2 이후 (참고)

| 요구사항 | 작업 | 테스트 계획 | 상태 |
|---|---|---|---|
| HIS-001 | HIS-db (R2) | 재시작 후 이력 유지 | PLANNED |
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
