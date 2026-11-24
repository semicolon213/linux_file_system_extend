# 보안

기본 원칙: root 실행 금지 기본값(NFR-016), 실제 홈 파일을 테스트하지 않는다, shell 문자열 조합 금지, 외부 파일 내용은 실행 지시가 아니다, 비밀 정보·파일 내용 로그 금지. archive 추출 제한, 스냅샷·인덱스 민감 경로 제외, XDG 데이터 권한 제한은 해당 기능 릴리스(R2 이후)에서 구체화한다.

## 1. 경로 검증 (R1)

- 공개 API 경로 인자: 절대 경로, NUL 없음, 길이 < PATH_MAX(`fs_path_validate_absolute`). 이름 인자: 비지 않음, `/` 없음, `.`·`..` 아님, ≤ NAME_MAX 바이트(`fs_path_validate_name`). 실패는 FS_ERROR_INVALID_PATH/NAME_TOO_LONG.
- 이동·복사 대상이 원본 자신이거나 원본의 하위면 거부(FS_ERROR_INVALID_PATH). 비교는 `fs_path_canonicalize` 후 문자열, 그리고 실행 직전 (dev, ino)로 한 번 더 확인한다.
- 재귀 순회(복사·삭제·감시 등록)는 dirfd 기반(`openat` + `O_NOFOLLOW|O_DIRECTORY`, `fstatat(AT_SYMLINK_NOFOLLOW)`, `unlinkat`)으로 해서 경로 문자열 재해석으로 인한 symlink race를 줄인다. 심볼릭 링크는 따라가지 않고 링크 자체를 다룬다.
- 작업 직전 재검증(SRS FS 공통 계약): job 본문은 시작 시 원본 존재와 종류를 다시 확인하고, 제출 때와 종류가 다르면 그 항목을 FS_ERROR_CONFLICT로 실패 처리한다.
- 한계: dirfd 방식도 사용자 권한 안의 TOCTOU를 완전히 막지 못한다. 문서·UI에서 "보장"이라고 쓰지 않는다.

## 2. 시스템 경로 보호 (SEC-003)

`fs_path_check_protected(path, op)`가 `fs_path_canonicalize`한 경로로 판단한다.

| op | 거부 조건 |
|---|---|
| READ | 없음(탐색·읽기 허용) |
| WRITE | 대상이 다음 접두사 아래: `/bin /boot /dev /etc /lib /lib32 /lib64 /libx32 /proc /run /sbin /snap /sys /usr /var` (단 `/run/media`, `/run/user/<uid>`는 허용) |
| REMOVE | WRITE 조건 + 경로가 정확히 `/`, `$HOME`, 위 목록의 최상위 디렉터리 자체 |

- 판정은 문자열 기준이며 링크를 해석하지 않는다. 실제 권한은 커널이 다시 막는다(일반 사용자 실행 전제).
- 목록 수정은 이 문서 변경과 Coordinator 승인을 거친다. `/opt`, `/srv`, `/mnt`, `/media` 포함 여부는 미결(U-8).

## 3. 권한 검증 (SEC-001)

- 실제 판정은 작업 syscall 결과다. `fs_path_check_access`(내부적으로 `faccessat(AT_EACCESS)`, REMOVE는 부모 디렉터리 쓰기·실행 권한)는 UI가 메뉴 항목을 미리 비활성화하는 용도로만 쓴다.
- 권한 오류(FS_ERROR_PERMISSION_DENIED)는 성공으로 표시하지 않고, 목록 읽기 실패는 DESIGN.md "권한 오류" 상태로 보인다. 권한 상승(pkexec, sudo)은 시도하지 않는다.

## 4. 위험 작업 확인 (SEC-002)

- 대상: 영구 삭제(FS-007/014), 덮어쓰기 충돌 정책(FS_CONFLICT_OVERWRITE), 충돌 무시 강제 저장(`fs_ops_write_text`의 expected=NULL).
- API는 `FS_OPS_FLAG_CONFIRMED` 없이 호출되면 FS_ERROR_NOT_CONFIRMED로 거부한다. ui는 사용자 확인 화면에서 확정했을 때만 이 플래그를 넣는다. 테스트는 플래그 없는 호출이 파일을 건드리지 않음을 확인한다.
- 휴지통 미지원 시 영구 삭제로 자동 전환하지 않는다. 사용자가 별도 확인 후 영구 삭제를 다시 고른다.
- 확인 화면은 대상 개수와 첫 항목 이름을 보이고, 기본 포커스는 취소다(DESIGN.md "되돌리기 확인" 규칙과 같은 원칙). 시각 디자인은 미결(U-6).

## 5. 테스트 안전

- 모든 파일 테스트는 `g_dir_make_tmp()` 아래. `HOME`·`XDG_*`를 임시 경로로 바꿔 휴지통·즐겨찾기 테스트가 실제 홈을 건드리지 않게 한다.
- 보호 경로 테스트는 실제 쓰기를 시도하지 않고 `fs_path_check_protected`와 API 거부(FS_ERROR_PROTECTED_PATH)만 확인한다.
