# Worktree 규칙
기준 브랜치는 실제 존재하는 커밋이어야 한다. `git rev-parse --verify refs/heads/main`으로 확인하고 main이 없으면 Coordinator가 실제 기준 브랜치를 지정한다. 최초 커밋 없는 저장소는 초기화 후 작업한다.
작업별 `feature/<task-id>-<name>` 브랜치와 독립 worktree를 사용한다. 예: `git worktree add -b feature/mon-001-watcher ../fs-mon-001 main`.
작업별 허용 경로를 명시하고 공통 헤더·CMake·migration은 단일 소유자가 관리한다. 빌드 디렉터리와 테스트 DB를 worktree별로 분리한다. 다른 worktree 수정, 강제 삭제, reset --hard, force push 금지. 병합 전 기준 브랜치 변경을 확인하고 충돌은 Coordinator에게 보고한다. 병합 및 검증 후 깨끗한 worktree만 제거한다.
