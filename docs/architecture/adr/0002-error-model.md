# ADR-0002 오류 모델: GError 단일 도메인 FS_ERROR

상태: 제안 (2026-11-24, ARCH-001)

## 맥락
ERROR_HANDLING 초안은 "GError 또는 모듈 오류 enum"을 열어 두었다. GTK/GIO 콜백, GTask, 작업 큐 결과가 모두 GError를 쓴다.

## 결정
- 공개 API는 GError만 쓰고 도메인은 `FS_ERROR` 하나, 코드는 `FsErrorCode`(INTERFACE_SPEC §1).
- 다른 도메인(G_IO_ERROR 등)은 모듈 경계에서 FS_ERROR로 바꾼다. ui는 한 가지 코드 표로 사용자 문구를 고른다.
- 위험 작업은 `FS_OPS_FLAG_CONFIRMED` 없이는 FS_ERROR_NOT_CONFIRMED로 거부한다(SEC-002를 API 수준에서 강제).

## 대안
- 모듈별 enum + errno 반환: GTask/콜백과 맞지 않고 메시지 전달이 어렵다.
- G_IO_ERROR 그대로 사용: PROTECTED_PATH, WATCH_LIMIT, CONFLICT 같은 제품 고유 코드를 표현하지 못한다.

## 결과
코드 추가는 core 소유자(INTERFACE_SPEC 변경)만 한다. errno 원문은 message에 남아 진단 정보 손실이 없다.
