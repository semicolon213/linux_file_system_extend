# 소프트웨어 요구사항 명세 (SRS)

기능 ID·우선순위·완료 판정과 비기능 목표의 정본이다. 모두 계획 상태이며 구현 여부와 무관하다(진행 상태는 harness/PROGRESS.md). 아래는 초기 계약이며 작업 착수 전 구체 입력·출력·경계 조건을 작업 지시서(harness/TASKS.md)에 확정한다.

- 우선순위: P0 핵심, P1 완성도, P2 고급, P3 실험.
- 모든 기능 ID에 공통으로 적용하는 요구: 사용자가 해당 기능을 실행하거나 관련 이벤트가 발생하면 그 기능의 결과를 화면 또는 기록에 반영한다. 해당 모듈 공통 계약(각 절 첫 줄)을 적용하며 미지원·실패·취소를 성공과 구분한다.
- 완료 판정은 표의 TC ID이며 테스트 카탈로그 규칙은 docs/TESTING.md §7이다.

## 1. 기능 요구사항

### 탐색 (NAV)

공통 계약: 경로 접근 실패 시 기존 탭 유지; 심볼릭 링크와 마운트 경계 표시.

| ID | 기능 | 우선순위 | 완료 판정 |
|---|---|---|---|
| NAV-001 | 디렉터리 탐색 | P0 | TC-NAV-001 |
| NAV-002 | 뒤로/앞으로/상위 이동 | P0 | TC-NAV-002 |
| NAV-003 | Breadcrumb | P0 | TC-NAV-003 |
| NAV-004 | 주소 자동완성 | P0 | TC-NAV-004 |
| NAV-005 | 홈 | P0 | TC-NAV-005 |
| NAV-006 | 멀티 탭 | P0 | TC-NAV-006 |
| NAV-007 | 듀얼 패널 | P1 | TC-NAV-007 |
| NAV-008 | 분할 화면 | P1 | TC-NAV-008 |
| NAV-009 | 새 창 | P1 | TC-NAV-009 |
| NAV-010 | 즐겨찾기 | P0 | TC-NAV-010 |
| NAV-011 | 최근 경로 | P1 | TC-NAV-011 |
| NAV-012 | 최근 파일 | P1 | TC-NAV-012 |
| NAV-013 | 트리 | P0 | TC-NAV-013 |
| NAV-014 | 숨김 파일 | P0 | TC-NAV-014 |
| NAV-015 | 세션 복원 | P1 | TC-NAV-015 |
| NAV-016 | 워크스페이스 | P2 | TC-NAV-016 |
| NAV-017 | 경로 별칭 | P2 | TC-NAV-017 |
| NAV-018 | 링크 대상 이동 | P2 | TC-NAV-018 |
| NAV-019 | 마운트 탐색 | P2 | TC-NAV-019 |
| NAV-020 | SFTP/SMB | P3 | TC-NAV-020 |

### 파일 작업 (FS)

공통 계약: 충돌 정책 명시, 작업 전 대상 재검증, 실패 시 원본 보존; 영구 삭제는 확인 필수.

| ID | 기능 | 우선순위 | 완료 판정 |
|---|---|---|---|
| FS-001 | 파일 생성 | P0 | TC-FS-001 |
| FS-002 | 폴더 생성 | P0 | TC-FS-002 |
| FS-003 | 기본 앱 열기 | P0 | TC-FS-003 |
| FS-004 | 내용 읽기 | P0 | TC-FS-004 |
| FS-005 | 텍스트 수정 | P0 | TC-FS-005 |
| FS-006 | 휴지통 이동 | P0 | TC-FS-006 |
| FS-007 | 영구 삭제 | P0 | TC-FS-007 |
| FS-008 | 이름 변경 | P0 | TC-FS-008 |
| FS-009 | 복사 | P0 | TC-FS-009 |
| FS-010 | 이동 | P0 | TC-FS-010 |
| FS-011 | 잘라내기 | P0 | TC-FS-011 |
| FS-012 | 붙여넣기 | P0 | TC-FS-012 |
| FS-013 | 다중 선택 | P0 | TC-FS-013 |
| FS-014 | 일괄 삭제 | P0 | TC-FS-014 |
| FS-015 | 일괄 이름 변경 | P1 | TC-FS-015 |
| FS-016 | 복제 | P2 | TC-FS-016 |
| FS-017 | 드래그 앤 드롭 | P2 | TC-FS-017 |
| FS-018 | 충돌 처리 | P0 | TC-FS-018 |
| FS-019 | 실행 취소 | P2 | TC-FS-019 |
| FS-020 | 다시 실행 | P2 | TC-FS-020 |
| FS-021 | 템플릿 | P2 | TC-FS-021 |
| FS-022 | 링크 생성 | P2 | TC-FS-022 |
| FS-023 | 연결 앱 선택 | P2 | TC-FS-023 |
| FS-024 | 경로 복사 | P2 | TC-FS-024 |
| FS-025 | 분할 및 병합 | P2 | TC-FS-025 |
| FS-026 | 희소 파일 복사 | P2 | TC-FS-026 |

### 감시 (MON)

공통 계약: raw mask와 정규화 이벤트 분리; cookie 매칭 만료 및 overflow 재탐색; 읽기 감시는 선택 사항.

| ID | 기능 | 우선순위 | 완료 판정 |
|---|---|---|---|
| MON-001 | CREATE | P0 | TC-MON-001 |
| MON-002 | DELETE | P0 | TC-MON-002 |
| MON-003 | MODIFY | P0 | TC-MON-003 |
| MON-004 | ACCESS | P1 | TC-MON-004 |
| MON-005 | OPEN | P1 | TC-MON-005 |
| MON-006 | CLOSE_WRITE | P0 | TC-MON-006 |
| MON-007 | CLOSE_NOWRITE | P1 | TC-MON-007 |
| MON-008 | MOVE_FROM | P0 | TC-MON-008 |
| MON-009 | MOVE_TO | P0 | TC-MON-009 |
| MON-010 | RENAME | P0 | TC-MON-010 |
| MON-011 | ATTRIB | P0 | TC-MON-011 |
| MON-012 | DELETE_SELF | P0 | TC-MON-012 |
| MON-013 | MOVE_SELF | P0 | TC-MON-013 |
| MON-014 | UNMOUNT | P0 | TC-MON-014 |
| MON-015 | OVERFLOW | P0 | TC-MON-015 |
| MON-016 | IGNORED | P0 | TC-MON-016 |
| MON-017 | UI 동기화 | P0 | TC-MON-017 |
| MON-018 | 재귀 감시 | P0 | TC-MON-018 |
| MON-019 | 동적 감시 | P0 | TC-MON-019 |
| MON-020 | 다중 경로 | P0 | TC-MON-020 |
| MON-021 | 감시 등록/해제 | P0 | TC-MON-021 |
| MON-022 | 일시정지 | P2 | TC-MON-022 |
| MON-023 | 프로필 | P2 | TC-MON-023 |
| MON-024 | 제외 경로 | P2 | TC-MON-024 |
| MON-025 | 확장자 필터 | P2 | TC-MON-025 |
| MON-026 | 패턴 필터 | P2 | TC-MON-026 |
| MON-027 | 이벤트 병합 | P0 | TC-MON-027 |
| MON-028 | cookie 연결 | P0 | TC-MON-028 |
| MON-029 | 유실 대응 | P0 | TC-MON-029 |
| MON-030 | 자원 조회 | P2 | TC-MON-030 |
| MON-031 | 상태 진단 | P2 | TC-MON-031 |
| MON-032 | 자체 이벤트 추정 | P2 | TC-MON-032 |
| MON-033 | 이벤트 재생 | P2 | TC-MON-033 |
| MON-034 | 알림 규칙 | P2 | TC-MON-034 |
| MON-035 | 백그라운드 감시 | P2 | TC-MON-035 |
| MON-036 | 프로세스 연계 | P3 | TC-MON-036 |

### 대시보드 (DASH)

공통 계약: 수신 시각 기준 집계; 유실 구간 표시; 필터는 원본 기록을 변경하지 않는다.

| ID | 기능 | 우선순위 | 완료 판정 |
|---|---|---|---|
| DASH-001 | 카운터 | P0 | TC-DASH-001 |
| DASH-002 | 실시간 그래프 | P1 | TC-DASH-002 |
| DASH-003 | 이벤트 스트림 | P0 | TC-DASH-003 |
| DASH-004 | 검색 | P1 | TC-DASH-004 |
| DASH-005 | 필터 | P0 | TC-DASH-005 |
| DASH-006 | 시간 범위 | P1 | TC-DASH-006 |
| DASH-007 | 활동 디렉터리 순위 | P1 | TC-DASH-007 |
| DASH-008 | 활동 파일 순위 | P1 | TC-DASH-008 |
| DASH-009 | 확장자 통계 | P1 | TC-DASH-009 |
| DASH-010 | 히트맵 | P1 | TC-DASH-010 |
| DASH-011 | 지연 측정 | P1 | TC-DASH-011 |
| DASH-012 | 큐 상태 | P1 | TC-DASH-012 |
| DASH-013 | CSV/JSON/TXT 내보내기 | P1 | TC-DASH-013 |
| DASH-014 | 위젯 배치 | P1 | TC-DASH-014 |
| DASH-015 | 이상 활동 | P1 | TC-DASH-015 |
| DASH-016 | 세션 비교 | P2 | TC-DASH-016 |

### 이력 (HIS)

공통 계약: 초기 및 쓰기 완료 스냅샷만 복원 가능; 저장 중 변경 감지 시 재시도 또는 불완전 표시.

| ID | 기능 | 우선순위 | 완료 판정 |
|---|---|---|---|
| HIS-001 | SQLite 이력 | P0 | TC-HIS-001 |
| HIS-002 | 파일 타임라인 | P1 | TC-HIS-002 |
| HIS-003 | 디렉터리 타임라인 | P1 | TC-HIS-003 |
| HIS-004 | 스냅샷 | P1 | TC-HIS-004 |
| HIS-005 | 자동 버전 | P1 | TC-HIS-005 |
| HIS-006 | 체크포인트 | P1 | TC-HIS-006 |
| HIS-007 | 버전 비교 | P1 | TC-HIS-007 |
| HIS-008 | 텍스트 Diff | P1 | TC-HIS-008 |
| HIS-009 | 이미지 비교 | P1 | TC-HIS-009 |
| HIS-010 | 바이너리 비교 | P1 | TC-HIS-010 |
| HIS-011 | 버전 복원 | P1 | TC-HIS-011 |
| HIS-012 | 삭제 복구 | P1 | TC-HIS-012 |
| HIS-013 | 디렉터리 스냅샷 | P1 | TC-HIS-013 |
| HIS-014 | 디렉터리 복원 | P1 | TC-HIS-014 |
| HIS-015 | 이름 추적 | P1 | TC-HIS-015 |
| HIS-016 | 중복 제거 | P2 | TC-HIS-016 |
| HIS-017 | 보관 정책 | P2 | TC-HIS-017 |
| HIS-018 | 버전 보호 | P2 | TC-HIS-018 |
| HIS-019 | 메모 | P2 | TC-HIS-019 |
| HIS-020 | 이력 백업 | P2 | TC-HIS-020 |
| HIS-021 | 시점 탐색 | P2 | TC-HIS-021 |

### 검색 (SRC)

공통 계약: 취소 가능한 비동기 검색; 접근 불가 경로 표시; 인덱스 지연 표시.

| ID | 기능 | 우선순위 | 완료 판정 |
|---|---|---|---|
| SRC-001 | 파일명 | P0 | TC-SRC-001 |
| SRC-002 | 실시간 검색 | P1 | TC-SRC-002 |
| SRC-003 | 재귀 검색 | P0 | TC-SRC-003 |
| SRC-004 | 확장자 | P0 | TC-SRC-004 |
| SRC-005 | 내용 | P1 | TC-SRC-005 |
| SRC-006 | 정규표현식 | P1 | TC-SRC-006 |
| SRC-007 | Glob | P1 | TC-SRC-007 |
| SRC-008 | 크기 | P1 | TC-SRC-008 |
| SRC-009 | 날짜 | P1 | TC-SRC-009 |
| SRC-010 | 권한 | P1 | TC-SRC-010 |
| SRC-011 | 소유자 | P1 | TC-SRC-011 |
| SRC-012 | 복합 조건 | P1 | TC-SRC-012 |
| SRC-013 | 인덱스 | P1 | TC-SRC-013 |
| SRC-014 | FTS5 | P1 | TC-SRC-014 |
| SRC-015 | 이벤트 동기화 | P1 | TC-SRC-015 |
| SRC-016 | 조건 저장 | P2 | TC-SRC-016 |
| SRC-017 | 결과 미리보기 | P2 | TC-SRC-017 |
| SRC-018 | 퍼지 검색 | P2 | TC-SRC-018 |
| SRC-019 | 스마트 폴더 | P2 | TC-SRC-019 |
| SRC-020 | 태그 | P2 | TC-SRC-020 |
| SRC-021 | 자연어 검색 | P3 | TC-SRC-021 |

### 미리보기 (PRE)

공통 계약: 크기 제한 및 취소; 외부 변경 시 저장 충돌 확인; 문서 내 스크립트 실행 금지.

| ID | 기능 | 우선순위 | 완료 판정 |
|---|---|---|---|
| PRE-001 | 텍스트 | P0 | TC-PRE-001 |
| PRE-002 | 코드 강조 | P1 | TC-PRE-002 |
| PRE-003 | 텍스트 편집 | P0 | TC-PRE-003 |
| PRE-004 | 이미지 | P1 | TC-PRE-004 |
| PRE-005 | PDF | P1 | TC-PRE-005 |
| PRE-006 | Markdown | P1 | TC-PRE-006 |
| PRE-007 | JSON | P1 | TC-PRE-007 |
| PRE-008 | XML | P1 | TC-PRE-008 |
| PRE-009 | CSV | P1 | TC-PRE-009 |
| PRE-010 | HEX 조회 | P1 | TC-PRE-010 |
| PRE-011 | HEX 편집 | P3 | TC-PRE-011 |
| PRE-012 | 오디오 | P1 | TC-PRE-012 |
| PRE-013 | 동영상 | P1 | TC-PRE-013 |
| PRE-014 | 압축 탐색 | P1 | TC-PRE-014 |
| PRE-015 | Quick Look | P1 | TC-PRE-015 |
| PRE-016 | 썸네일 | P2 | TC-PRE-016 |
| PRE-017 | 대용량 뷰어 | P2 | TC-PRE-017 |
| PRE-018 | 로그 뷰어 | P2 | TC-PRE-018 |
| PRE-019 | 인코딩 | P2 | TC-PRE-019 |
| PRE-020 | 줄바꿈 | P2 | TC-PRE-020 |
| PRE-021 | 자동 저장 | P2 | TC-PRE-021 |
| PRE-022 | 외부 수정 충돌 | P2 | TC-PRE-022 |
| PRE-023 | 검색 및 치환 | P2 | TC-PRE-023 |
| PRE-024 | 미니맵 | P2 | TC-PRE-024 |

### 속성 (META)

공통 계약: lstat/stat 구분; 미지원 속성은 N/A; 권한 변경 실패는 성공으로 표시하지 않는다.

| ID | 기능 | 우선순위 | 완료 판정 |
|---|---|---|---|
| META-001 | 기본 속성 | P0 | TC-META-001 |
| META-002 | Inode | P0 | TC-META-002 |
| META-003 | Device ID | P1 | TC-META-003 |
| META-004 | 시간 정보 | P0 | TC-META-004 |
| META-005 | 소유자 | P0 | TC-META-005 |
| META-006 | 권한 | P0 | TC-META-006 |
| META-007 | chmod | P1 | TC-META-007 |
| META-008 | chown | P1 | TC-META-008 |
| META-009 | 재귀 권한 | P1 | TC-META-009 |
| META-010 | 특수 권한 | P1 | TC-META-010 |
| META-011 | 하드 링크 | P1 | TC-META-011 |
| META-012 | 심볼릭 링크 | P1 | TC-META-012 |
| META-013 | MIME | P1 | TC-META-013 |
| META-014 | xattr | P1 | TC-META-014 |
| META-015 | ACL | P1 | TC-META-015 |
| META-016 | 해시 | P2 | TC-META-016 |
| META-017 | 해시 검증 | P2 | TC-META-017 |
| META-018 | EXIF | P2 | TC-META-018 |
| META-019 | ELF | P2 | TC-META-019 |
| META-020 | Magic Number | P2 | TC-META-020 |
| META-021 | 태그 | P2 | TC-META-021 |
| META-022 | 메모 | P2 | TC-META-022 |
| META-023 | 파일 플래그 | P2 | TC-META-023 |

### 디스크 (DISK)

공통 계약: 논리 크기와 할당 크기 구분; 하드 링크 중복 집계 방지; 기본적으로 마운트 경계 유지.

| ID | 기능 | 우선순위 | 완료 판정 |
|---|---|---|---|
| DISK-001 | 사용량 | P0 | TC-DISK-001 |
| DISK-002 | 파티션 | P1 | TC-DISK-002 |
| DISK-003 | 디렉터리 용량 | P1 | TC-DISK-003 |
| DISK-004 | Treemap | P1 | TC-DISK-004 |
| DISK-005 | Sunburst | P1 | TC-DISK-005 |
| DISK-006 | 확장자 분석 | P1 | TC-DISK-006 |
| DISK-007 | 대용량 파일 | P1 | TC-DISK-007 |
| DISK-008 | 대용량 폴더 | P1 | TC-DISK-008 |
| DISK-009 | 오래된 파일 | P1 | TC-DISK-009 |
| DISK-010 | 빈 파일 | P1 | TC-DISK-010 |
| DISK-011 | 빈 폴더 | P1 | TC-DISK-011 |
| DISK-012 | 중복 | P1 | TC-DISK-012 |
| DISK-013 | 유사 이미지 | P3 | TC-DISK-013 |
| DISK-014 | 끊어진 링크 | P1 | TC-DISK-014 |
| DISK-015 | 캐시 분석 | P1 | TC-DISK-015 |
| DISK-016 | 용량 추적 | P2 | TC-DISK-016 |
| DISK-017 | 용량 경고 | P2 | TC-DISK-017 |
| DISK-018 | Inode 사용량 | P2 | TC-DISK-018 |
| DISK-019 | I/O 통계 | P2 | TC-DISK-019 |
| DISK-020 | 실제 할당량 | P2 | TC-DISK-020 |
| DISK-021 | 보고서 | P2 | TC-DISK-021 |
| DISK-022 | 정리 후보 | P2 | TC-DISK-022 |
| DISK-023 | 정리 시뮬레이션 | P2 | TC-DISK-023 |

### 휴지통 (TRASH)

공통 계약: freedesktop 휴지통 규격 및 GIO 사용; 지원 불가 시 영구 삭제로 자동 전환 금지.

| ID | 기능 | 우선순위 | 완료 판정 |
|---|---|---|---|
| TRASH-001 | 시스템 휴지통 | P0 | TC-TRASH-001 |
| TRASH-002 | 목록 | P0 | TC-TRASH-002 |
| TRASH-003 | 원위치 복원 | P0 | TC-TRASH-003 |
| TRASH-004 | 지정 위치 복원 | P1 | TC-TRASH-004 |
| TRASH-005 | 비우기 | P0 | TC-TRASH-005 |
| TRASH-006 | 선택 삭제 | P0 | TC-TRASH-006 |
| TRASH-007 | 삭제 시간 | P1 | TC-TRASH-007 |
| TRASH-008 | 검색 | P1 | TC-TRASH-008 |
| TRASH-009 | 자동 정리 | P1 | TC-TRASH-009 |
| TRASH-010 | 용량 제한 | P1 | TC-TRASH-010 |
| TRASH-011 | 복원 충돌 | P1 | TC-TRASH-011 |
| TRASH-012 | 보호 파일 | P1 | TC-TRASH-012 |

### 압축 (ARC)

공통 계약: 절대 경로, .. 탈출, 위험 링크 차단; 추출 용량 제한 및 취소 정리.

| ID | 기능 | 우선순위 | 완료 판정 |
|---|---|---|---|
| ARC-001 | ZIP 생성 | P0 | TC-ARC-001 |
| ARC-002 | ZIP 해제 | P0 | TC-ARC-002 |
| ARC-003 | TAR | P1 | TC-ARC-003 |
| ARC-004 | GZIP | P1 | TC-ARC-004 |
| ARC-005 | BZIP2 | P1 | TC-ARC-005 |
| ARC-006 | XZ | P1 | TC-ARC-006 |
| ARC-007 | ZSTD | P1 | TC-ARC-007 |
| ARC-008 | 7Z | P1 | TC-ARC-008 |
| ARC-009 | 압축률 | P1 | TC-ARC-009 |
| ARC-010 | 암호화 | P1 | TC-ARC-010 |
| ARC-011 | 내부 탐색 | P1 | TC-ARC-011 |
| ARC-012 | 선택 추출 | P1 | TC-ARC-012 |
| ARC-013 | 무결성 | P1 | TC-ARC-013 |
| ARC-014 | 진행률 | P1 | TC-ARC-014 |
| ARC-015 | 취소 | P1 | TC-ARC-015 |
| ARC-016 | 안전한 추출 | P0 | TC-ARC-016 |

### 작업 큐 (JOB)

공통 계약: 작업 상태와 오류 기록; 취소 시 부분 결과 표시; 진행률 미확정이면 indeterminate.

| ID | 기능 | 우선순위 | 완료 판정 |
|---|---|---|---|
| JOB-001 | 대기열 | P0 | TC-JOB-001 |
| JOB-002 | 백그라운드 | P0 | TC-JOB-002 |
| JOB-003 | 진행률 | P0 | TC-JOB-003 |
| JOB-004 | 속도 | P1 | TC-JOB-004 |
| JOB-005 | ETA | P1 | TC-JOB-005 |
| JOB-006 | 일시정지 | P1 | TC-JOB-006 |
| JOB-007 | 재개 | P1 | TC-JOB-007 |
| JOB-008 | 취소 | P0 | TC-JOB-008 |
| JOB-009 | 우선순위 | P1 | TC-JOB-009 |
| JOB-010 | 동시성 제한 | P1 | TC-JOB-010 |
| JOB-011 | 이력 | P1 | TC-JOB-011 |
| JOB-012 | 재시도 | P1 | TC-JOB-012 |
| JOB-013 | 부분 실패 | P1 | TC-JOB-013 |
| JOB-014 | 해시 검증 | P1 | TC-JOB-014 |
| JOB-015 | 예약 | P3 | TC-JOB-015 |
| JOB-016 | 완료 알림 | P2 | TC-JOB-016 |

### 개발 도구 (DEV)

공통 계약: 명령은 argv로 실행; 명시적 사용자 실행만 허용; 신뢰하지 않는 ELF를 실행하지 않는다.

| ID | 기능 | 우선순위 | 완료 판정 |
|---|---|---|---|
| DEV-001 | 터미널 | P1 | TC-DEV-001 |
| DEV-002 | 경로 연동 | P1 | TC-DEV-002 |
| DEV-003 | 멀티 터미널 | P1 | TC-DEV-003 |
| DEV-004 | Git 감지 | P1 | TC-DEV-004 |
| DEV-005 | Git 상태 | P1 | TC-DEV-005 |
| DEV-006 | 브랜치 | P1 | TC-DEV-006 |
| DEV-007 | Diff | P1 | TC-DEV-007 |
| DEV-008 | Log | P1 | TC-DEV-008 |
| DEV-009 | Worktree 조회 | P1 | TC-DEV-009 |
| DEV-010 | Worktree 생성 | P1 | TC-DEV-010 |
| DEV-011 | gitignore | P1 | TC-DEV-011 |
| DEV-012 | IDE 연동 | P1 | TC-DEV-012 |
| DEV-013 | 프로젝트 감지 | P1 | TC-DEV-013 |
| DEV-014 | 빌드 실행 | P1 | TC-DEV-014 |
| DEV-015 | 로그 Tail | P1 | TC-DEV-015 |
| DEV-016 | 로그 강조 | P2 | TC-DEV-016 |
| DEV-017 | 로그 필터 | P2 | TC-DEV-017 |
| DEV-018 | 환경 변수 | P2 | TC-DEV-018 |
| DEV-019 | 파일 비교 | P2 | TC-DEV-019 |
| DEV-020 | 폴더 비교 | P2 | TC-DEV-020 |
| DEV-021 | 동기화 | P2 | TC-DEV-021 |
| DEV-022 | 열린 FD | P2 | TC-DEV-022 |
| DEV-023 | 프로세스 정보 | P2 | TC-DEV-023 |
| DEV-024 | ELF 분석 | P2 | TC-DEV-024 |
| DEV-025 | 공유 라이브러리 | P2 | TC-DEV-025 |
| DEV-026 | Unix Socket | P2 | TC-DEV-026 |
| DEV-027 | 사용자 명령 | P2 | TC-DEV-027 |
| DEV-028 | 시스템 호출 관찰 | P3 | TC-DEV-028 |

### 보안 (SEC)

공통 계약: 일반 사용자 권한 기본; 경고는 탐지 보장 아님; 비밀 정보 로그 금지.

| ID | 기능 | 우선순위 | 완료 판정 |
|---|---|---|---|
| SEC-001 | 권한 검증 | P0 | TC-SEC-001 |
| SEC-002 | 위험 작업 확인 | P0 | TC-SEC-002 |
| SEC-003 | 시스템 경로 보호 | P0 | TC-SEC-003 |
| SEC-004 | 권한 분리 | P1 | TC-SEC-004 |
| SEC-005 | 해시 기준점 | P1 | TC-SEC-005 |
| SEC-006 | 무결성 감시 | P1 | TC-SEC-006 |
| SEC-007 | 권한 변경 알림 | P1 | TC-SEC-007 |
| SEC-008 | 대량 삭제 경고 | P1 | TC-SEC-008 |
| SEC-009 | 대량 수정 경고 | P1 | TC-SEC-009 |
| SEC-010 | 실행 파일 변경 | P1 | TC-SEC-010 |
| SEC-011 | 위험 권한 | P1 | TC-SEC-011 |
| SEC-012 | SUID 탐색 | P1 | TC-SEC-012 |
| SEC-013 | 암호화 | P3 | TC-SEC-013 |
| SEC-014 | 감사 로그 | P1 | TC-SEC-014 |
| SEC-015 | 민감 경로 제외 | P1 | TC-SEC-015 |
| SEC-016 | ClamAV 연동 | P3 | TC-SEC-016 |

### 자동화 (AUTO)

공통 계약: 기본 비활성; dry-run, 반복 제한, 작업별 승인; 자동 영구 삭제 금지.

| ID | 기능 | 우선순위 | 완료 판정 |
|---|---|---|---|
| AUTO-001 | 규칙 생성 | P2 | TC-AUTO-001 |
| AUTO-002 | 이벤트 트리거 | P2 | TC-AUTO-002 |
| AUTO-003 | 시간 트리거 | P3 | TC-AUTO-003 |
| AUTO-004 | 확장자 조건 | P2 | TC-AUTO-004 |
| AUTO-005 | 파일명 조건 | P2 | TC-AUTO-005 |
| AUTO-006 | 크기 조건 | P2 | TC-AUTO-006 |
| AUTO-007 | 복합 조건 | P2 | TC-AUTO-007 |
| AUTO-008 | 이동 | P2 | TC-AUTO-008 |
| AUTO-009 | 복사 | P2 | TC-AUTO-009 |
| AUTO-010 | 이름 변경 | P2 | TC-AUTO-010 |
| AUTO-011 | 압축 | P2 | TC-AUTO-011 |
| AUTO-012 | 태그 | P2 | TC-AUTO-012 |
| AUTO-013 | 알림 | P2 | TC-AUTO-013 |
| AUTO-014 | 스크립트 | P3 | TC-AUTO-014 |
| AUTO-015 | 시뮬레이션 | P2 | TC-AUTO-015 |
| AUTO-016 | 이력 | P2 | TC-AUTO-016 |
| AUTO-017 | 순환 방지 | P2 | TC-AUTO-017 |
| AUTO-018 | 템플릿 | P2 | TC-AUTO-018 |

### 백업 (BACK)

공통 계약: 복원 전 충돌 확인; 해시 검증; 원본과 대상 중첩 방지.

| ID | 기능 | 우선순위 | 완료 판정 |
|---|---|---|---|
| BACK-001 | 수동 백업 | P2 | TC-BACK-001 |
| BACK-002 | 증분 | P2 | TC-BACK-002 |
| BACK-003 | 예약 | P3 | TC-BACK-003 |
| BACK-004 | 실시간 | P2 | TC-BACK-004 |
| BACK-005 | 단방향 | P2 | TC-BACK-005 |
| BACK-006 | 양방향 | P3 | TC-BACK-006 |
| BACK-007 | 충돌 | P3 | TC-BACK-007 |
| BACK-008 | 제외 설정 | P2 | TC-BACK-008 |
| BACK-009 | 무결성 | P2 | TC-BACK-009 |
| BACK-010 | 복원 | P2 | TC-BACK-010 |
| BACK-011 | 외장 장치 | P2 | TC-BACK-011 |
| BACK-012 | 시뮬레이션 | P2 | TC-BACK-012 |

### 장치 (SYS)

공통 계약: UDisks2/Polkit 정책 준수; 장치 제거 실패 표시; 포맷 기능 제외.

| ID | 기능 | 우선순위 | 완료 판정 |
|---|---|---|---|
| SYS-001 | 장치 조회 | P1 | TC-SYS-001 |
| SYS-002 | 파일 시스템 | P1 | TC-SYS-002 |
| SYS-003 | 마운트 정보 | P1 | TC-SYS-003 |
| SYS-004 | 연결 감지 | P1 | TC-SYS-004 |
| SYS-005 | 마운트 | P1 | TC-SYS-005 |
| SYS-006 | 안전 제거 | P1 | TC-SYS-006 |
| SYS-007 | 장치 정보 | P1 | TC-SYS-007 |
| SYS-008 | 자원 표시 | P1 | TC-SYS-008 |
| SYS-009 | FD 조회 | P1 | TC-SYS-009 |
| SYS-010 | inotify 제한 | P1 | TC-SYS-010 |
| SYS-011 | 감시 진단 | P1 | TC-SYS-011 |
| SYS-012 | SMART | P3 | TC-SYS-012 |

### UI/UX (UI)

공통 계약: 키보드 포커스 유지; 색상 외 텍스트 표시; 비동기 오류와 빈 상태 제공.

| ID | 기능 | 우선순위 | 완료 판정 |
|---|---|---|---|
| UI-001 | 다크 | P0 | TC-UI-001 |
| UI-002 | 라이트 | P1 | TC-UI-002 |
| UI-003 | 시스템 테마 | P1 | TC-UI-003 |
| UI-004 | 강조색 | P1 | TC-UI-004 |
| UI-005 | 리스트 | P0 | TC-UI-005 |
| UI-006 | 그리드 | P1 | TC-UI-006 |
| UI-007 | 상세 | P0 | TC-UI-007 |
| UI-008 | 컬럼 설정 | P1 | TC-UI-008 |
| UI-009 | 그룹화 | P1 | TC-UI-009 |
| UI-010 | 다중 정렬 | P1 | TC-UI-010 |
| UI-011 | 패널 크기 | P0 | TC-UI-011 |
| UI-012 | 패널 접기 | P0 | TC-UI-012 |
| UI-013 | Command Palette | P1 | TC-UI-013 |
| UI-014 | Quick Switcher | P1 | TC-UI-014 |
| UI-015 | 컨텍스트 메뉴 | P0 | TC-UI-015 |
| UI-016 | Toast | P0 | TC-UI-016 |
| UI-017 | 알림 센터 | P2 | TC-UI-017 |
| UI-018 | 상태 표시줄 | P0 | TC-UI-018 |
| UI-019 | 로딩 | P0 | TC-UI-019 |
| UI-020 | 빈 상태 | P0 | TC-UI-020 |
| UI-021 | 오류 상태 | P0 | TC-UI-021 |
| UI-022 | 변경 강조 | P0 | TC-UI-022 |
| UI-023 | 삭제 전환 | P2 | TC-UI-023 |
| UI-024 | 애니메이션 | P2 | TC-UI-024 |
| UI-025 | 단축키 | P0 | TC-UI-025 |
| UI-026 | 키 변경 | P2 | TC-UI-026 |
| UI-027 | 키보드 탐색 | P2 | TC-UI-027 |
| UI-028 | HiDPI | P2 | TC-UI-028 |
| UI-029 | 글꼴 | P2 | TC-UI-029 |
| UI-030 | 아이콘 크기 | P2 | TC-UI-030 |
| UI-031 | 밀도 | P2 | TC-UI-031 |
| UI-032 | 접근성 | P2 | TC-UI-032 |
| UI-033 | 다국어 | P2 | TC-UI-033 |
| UI-034 | 전체 화면 | P2 | TC-UI-034 |
| UI-035 | 레이아웃 저장 | P2 | TC-UI-035 |
| UI-036 | 온보딩 | P2 | TC-UI-036 |
| UI-037 | 도움말 | P2 | TC-UI-037 |
| UI-038 | 모션 감소 | P2 | TC-UI-038 |

### 설정 (CFG)

공통 계약: XDG 경로 사용; 원자적 설정 저장; 잘못된 설정은 안전 기본값 및 오류 표시.

| ID | 기능 | 우선순위 | 완료 판정 |
|---|---|---|---|
| CFG-001 | 저장 | P0 | TC-CFG-001 |
| CFG-002 | 초기화 | P1 | TC-CFG-002 |
| CFG-003 | 내보내기 | P1 | TC-CFG-003 |
| CFG-004 | 가져오기 | P1 | TC-CFG-004 |
| CFG-005 | 시작 경로 | P1 | TC-CFG-005 |
| CFG-006 | 자동 시작 | P1 | TC-CFG-006 |
| CFG-007 | 알림 | P1 | TC-CFG-007 |
| CFG-008 | 캐시 | P1 | TC-CFG-008 |
| CFG-009 | 용량 제한 | P1 | TC-CFG-009 |
| CFG-010 | 로그 수준 | P1 | TC-CFG-010 |
| CFG-011 | 진단 내보내기 | P1 | TC-CFG-011 |
| CFG-012 | 안전 모드 | P1 | TC-CFG-012 |
| CFG-013 | 플러그인 | P3 | TC-CFG-013 |
| CFG-014 | 기능 플래그 | P1 | TC-CFG-014 |
| CFG-015 | 업데이트 확인 | P1 | TC-CFG-015 |
| CFG-016 | 개인정보 삭제 | P2 | TC-CFG-016 |

## 2. 비기능 요구사항

NFR 수치는 목표값이며 측정 결과가 아니다. 측정 시 Ubuntu 버전, CPU, RAM, 파일 시스템, VM 여부를 기록한다(docs/TESTING.md §5).

| ID | 목표 |
|---|---|
| NFR-001 | 시작 2초 이내 |
| NFR-002 | 이벤트 UI 반영 P95 100ms 이내(일반 부하) |
| NFR-003 | 1만 파일 목록 1초 목표 |
| NFR-004 | 10만 파일 탐색 중 UI 응답 유지 |
| NFR-005 | 초당 1000 이벤트 부하 |
| NFR-006 | 10GB 복사 |
| NFR-007 | 인덱스 검색 300ms 목표 |
| NFR-008 | 유휴 CPU 1% 미만 |
| NFR-009 | 기본 RSS 150MB 이하 목표 |
| NFR-010 | 24시간 감시 |
| NFR-011 | ASan/UBSan 오류 0 |
| NFR-012 | TSan 관련 경쟁 0 |
| NFR-013 | overflow 후 상태 일치 |
| NFR-014 | 강제 종료 후 SQLite integrity_check 성공 |
| NFR-015 | 장시간 작업 중 UI 입력 유지 |
| NFR-016 | root 없이 실행 |
| NFR-017 | 오프라인 핵심 기능 |
| NFR-018 | Ubuntu LTS 호환 |
| NFR-019 | 키보드 접근 |
| NFR-020 | 핵심 모듈 커버리지 85% 목표 |
