# 비기능 요구사항
목표값이며 측정 결과가 아니다. Ubuntu 버전, CPU, RAM, 파일 시스템, VM 여부를 기록한다.
- NFR-001 시작 2초 이내; NFR-002 이벤트 UI 반영 P95 100ms 이내(일반 부하).
- NFR-003 1만 파일 목록 1초 목표; NFR-004 10만 파일 탐색 중 UI 응답 유지.
- NFR-005 초당 1000 이벤트 부하; NFR-006 10GB 복사; NFR-007 인덱스 검색 300ms 목표.
- NFR-008 유휴 CPU 1% 미만; NFR-009 기본 RSS 150MB 이하 목표; NFR-010 24시간 감시.
- NFR-011 ASan/UBSan 오류 0; NFR-012 TSan 관련 경쟁 0; NFR-013 overflow 후 상태 일치.
- NFR-014 강제 종료 후 SQLite integrity_check 성공; NFR-015 장시간 작업 중 UI 입력 유지.
- NFR-016 root 없이 실행; NFR-017 오프라인 핵심 기능; NFR-018 Ubuntu LTS 호환.
- NFR-019 키보드 접근; NFR-020 핵심 모듈 커버리지 85% 목표.
