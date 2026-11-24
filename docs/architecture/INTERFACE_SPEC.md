# 공개 인터페이스 초안
공통 타입은 opaque handle과 명시적 오류 반환을 사용한다. 아래 이름은 승인 전 제안 계약이다.
```c
typedef struct FsApp FsApp;
typedef struct FsEvent FsEvent;
typedef struct FsJob FsJob;
typedef struct FsWatcher FsWatcher;
```
watch_add/remove는 경로와 오류를 반환; job_submit은 job ID 반환; job_cancel은 취소 요청; event_subscribe/unsubscribe는 구독 토큰 관리. 문자열은 GLib 소유권 규칙을 명시하고 Linux 경로 원본 바이트와 UI 표시 문자열을 분리한다. 구체 시그니처와 테스트는 R0에서 승인 후 고정한다.
