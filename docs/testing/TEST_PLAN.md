# 테스트 전략
GLib unit, 실제 임시 디렉터리 integration, Xvfb GUI smoke 및 실제 Ubuntu Wayland 수동 검증. 생성/수정/이동/삭제 외부 명령 테스트, overflow 강제 주입, 권한 오류, ENOSPC 모의, symlink loop, archive traversal, 취소 및 종료 경쟁. 실제 홈/시스템 경로 변경 금지. QA는 구현과 분리.
