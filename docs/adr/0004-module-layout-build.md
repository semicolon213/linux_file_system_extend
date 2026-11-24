# ADR-0004 모듈 배치와 빌드 소유권

상태: 제안 (2026-11-24, ARCH-001)

## 결정
- `src/<모듈>/` 디렉터리마다 정적 라이브러리 하나(`filescope_<모듈>`). 공개 헤더는 `fs_<모듈>.h`(core는 여러 개), 내부 헤더는 `*_internal.h`.
- 의존: core ← job ← fs, core ← mon, core ← nav, ui → 전부, app → ui. 기능 모듈끼리는 fs→job만 허용(파일 작업이 작업 큐에 제출).
- 루트 `CMakeLists.txt`·`Makefile`·`scripts/check-architecture.sh`는 core 작업(R1-T01)이 단독 소유한다. 루트는 하위 `src/<m>/CMakeLists.txt`, `tests/<m>/CMakeLists.txt`가 있으면 add_subdirectory한다. 각 모듈 작업은 자기 하위 CMakeLists.txt만 고쳐 병렬 작업의 파일 충돌을 없앤다.
- check-architecture.sh가 금지 include(ARCHITECTURE §1 표)를 검사한다.

## 대안
- 단일 라이브러리: 경계 검사를 링크 단계에서 못 한다.
- 루트 CMake를 모든 작업이 수정: 병렬 worktree 충돌.

## 결과
새 모듈(R2 his 등)은 하위 CMakeLists.txt만 추가하면 루트가 수정 없이 포함한다. 단 실행 파일 링크 목록 변경은 루트 소유자 승인이 필요하다.
