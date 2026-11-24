# 오류 처리 (R1)

상태: R1 계약 제안 (ARCH-001, ADR-0002).

## 1. 모델

- 공개 API는 GError 하나의 도메인 `FS_ERROR`(코드 `FsErrorCode`, INTERFACE_SPEC §1)만 밖으로 내보낸다. 모듈별 오류 enum을 따로 두지 않는다.
- GIO 등 다른 도메인 오류는 모듈 경계에서 `fs_propagate_gio_error()`로 바꾼다(예: `G_IO_ERROR_NOT_SUPPORTED` → `FS_ERROR_NOT_SUPPORTED`, `G_IO_ERROR_CANCELLED` → `FS_ERROR_CANCELLED`).
- 실패 반환: `gboolean` FALSE, 포인터 NULL, ID 0. 실패면 반드시 `error`를 설정하고 성공이면 설정하지 않는다.

## 2. errno

- syscall 실패 직후 `int saved = errno;`로 저장하고 그 뒤에 다른 호출(로그, close)을 한다.
- `EINTR`는 재시도한다. 부분 read/write는 끝까지 반복한다.
- 대응표는 `fs_error_code_from_errno`가 정본: ENOENT→NOT_FOUND, EACCES/EPERM/EROFS→PERMISSION_DENIED, EEXIST/ENOTEMPTY→EXISTS, ENOTDIR→NOT_DIRECTORY, EISDIR→IS_DIRECTORY, ENOSPC/EDQUOT→NO_SPACE, EIO→IO, EXDEV→CROSS_DEVICE, ELOOP→LOOP, ENAMETOOLONG→NAME_TOO_LONG, EMFILE/ENFILE→TOO_MANY_OPEN, 그 외→FAILED.
- 예외: `inotify_add_watch`의 ENOSPC는 WATCH_LIMIT(mon이 직접 설정).

## 3. 메시지

- `GError.message`는 진단용 영어 한 줄: `"<op>: <경로 표시형>: <strerror>"`. 경로 표시형은 `g_filename_display_name()`.
- 사용자에게 보이는 한국어 문장은 ui가 코드로 고른다. ui는 message를 그대로 띄우지 않는다(디자인의 띠·토스트 문구는 디자인 결정).
- 로그: `g_debug`/`g_warning`(GLib 로그, 도메인 `filescope-<모듈>`). 파일 내용은 로그에 쓰지 않는다. 경로는 debug 수준에서만.

## 4. 부분 성공과 취소

- 일괄 작업은 항목마다 `FsJobItemResult`를 남긴다. 대표 오류 규칙은 STATE_MACHINE.md §2.
- 취소는 실패와 구분한다: `FS_ERROR_CANCELLED`, 작업 상태 CANCELLED. 취소 시 만든 임시 파일은 지우고, 이미 끝난 항목은 되돌리지 않는다(되돌리기는 FS-019, P2).
- 원본 보존: 이동(EXDEV 폴백)은 항목 복사가 완전히 끝난 뒤에만 원본을 지운다. 덮어쓰기는 임시 파일 완성 후 rename으로 바꾼다. 중간 실패로 원본이 사라지는 경로가 없어야 한다.

## 5. 재시도

자동 재시도는 하지 않는다(R1). 예외: EINTR, inotify IN_IGNORED 후 하위 디렉터리 재등록 1회. JOB-012(재시도)는 P1.

## 6. 테스트 기준

각 공개 함수는 정상 1개 이상, 대표 실패(권한·없음·존재·검증 실패) 1개 이상을 테스트한다. 권한 오류는 임시 디렉터리에 `chmod 000`으로 만들고, root로 실행 중이면 g_test_skip(root 실행은 NFR-016에 따라 기본 금지).
