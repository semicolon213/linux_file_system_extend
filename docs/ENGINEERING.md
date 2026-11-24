# 엔지니어링 규칙

빌드, 의존성, C17 코딩 규칙, 오류 처리, 보안, 접근성. 스레드·메모리 계약 본문은 docs/DESIGN.md(스레드·메모리 부분), 검증 명령은 docs/TESTING.md, 시각 디자인 권한은 AGENTS.md가 정본이다.

## 빌드

CMake/Ninja, C17, out-of-source build. 진입점은 `Makefile` 하나다(대상 목록은 AGENTS.md "명령", 게이트 대응은 docs/TESTING.md §1). 빌드 디렉터리는 `CMakePresets.json` 프리셋별로 분리한다(`build/debug`, asan, ubsan, tsan). 기존 소스와 빌드 설정을 확인하기 전 덮어쓰지 않는다. 모듈별 CMake 소유권은 docs/ARCHITECTURE.md §3(ADR-0004).

## 의존성

필수: build-essential, cmake, ninja-build, pkg-config, libgtk-3-dev, libsqlite3-dev, libayatana-appindicator3-dev(트레이, PRODUCT.md). 개발·검증: xvfb, clang-format, clang-tidy. 선택: libarchive-dev, libgtksourceview-4-dev, libvte-2.91-dev, libpoppler-glib-dev, libgit2-dev. Ubuntu 버전별 설치 가능 여부 확인 후 버전 고정. 선택 기능 미지원 시 명시적으로 비활성화.

## 코딩 규칙 (C17)

- 형식: `.clang-format`(LLVM, 들여쓰기 4, 100열). 검사: `.clang-tidy`(경고=오류). 둘 다 G4.
- 이름: snake_case 함수·변수, 공개 함수 `fs_<모듈>_`(core는 `fs_`), 타입 `Fs` + CamelCase, 상수·enum 값 `FS_` 대문자. 내부 함수는 `static`, 모듈 안 여러 파일이 쓰는 내부 함수는 `*_internal.h`에 선언하고 접두사 `<모듈>_`(예: `mon_registry_add`).
- 헤더: include guard `FILESCOPE_<모듈>_<파일>_H`. 공개 헤더는 자기 의존(GLib 등)을 직접 include해 단독으로 컴파일된다. 공개 헤더에 GTK를 include하는 모듈은 ui뿐이다.
- include 경로: `"core/fs_error.h"`처럼 src 기준. 다른 모듈의 `*_internal.h`, `*_testing.h`(tests 제외) include 금지(ARCHITECTURE §1, check-architecture.sh).
- 공개 API 주석: 입력, 출력, 오류 코드, 소유권(transfer), 스레드(`[main]`/`[any]`)를 적는다. DESIGN 인터페이스와 다르면 DESIGN 인터페이스가 정본이다.
- 할당: GLib 계열(DESIGN 메모리 §1). `g_autoptr`/`g_autofree` 권장.
- syscall: 반환값 전부 확인, EINTR 재시도, 부분 read/write 처리, errno 즉시 저장. fd는 `O_CLOEXEC`.
- 경로: 고정 길이 버퍼 금지. 재귀 순회는 `openat`/`fstatat`/`unlinkat`(dirfd 기반)과 `O_NOFOLLOW`/`AT_SYMLINK_NOFOLLOW`.
- 외부 실행: shell 문자열 금지, argv 기반(GIO `g_app_info_*`, `g_spawn_async` argv)만. 사용자 파일을 실행하지 않는다.
- GTK: 메인 스레드만. worker에서 위젯·모델 접근 금지.
- 매직 넘버: 시간창·큐 크기 등 계약 수치는 모듈 내부 헤더에 이름 있는 상수로 두고 DESIGN 이벤트 등 문서 값과 같게 한다.
- 테스트: `g_test` 경로 `/<모듈>/<주제>/<사례>`, 실제 파일은 `g_dir_make_tmp()` 아래에서만(ARCHITECTURE §4).

## 오류 처리 (R1)

ADR-0002를 따른다.

### 오류 §1 모델

- 공개 API는 GError 하나의 도메인 `FS_ERROR`(코드 `FsErrorCode`, DESIGN 인터페이스 §1)만 밖으로 내보낸다. 모듈별 오류 enum을 따로 두지 않는다.
- GIO 등 다른 도메인 오류는 모듈 경계에서 `fs_propagate_gio_error()`로 바꾼다(예: `G_IO_ERROR_NOT_SUPPORTED` → `FS_ERROR_NOT_SUPPORTED`, `G_IO_ERROR_CANCELLED` → `FS_ERROR_CANCELLED`).
- 실패 반환: `gboolean` FALSE, 포인터 NULL, ID 0. 실패면 반드시 `error`를 설정하고 성공이면 설정하지 않는다.

### 오류 §2 errno

- syscall 실패 직후 `int saved = errno;`로 저장하고 그 뒤에 다른 호출(로그, close)을 한다.
- `EINTR`는 재시도한다. 부분 read/write는 끝까지 반복한다.
- 대응표는 `fs_error_code_from_errno`가 정본: ENOENT→NOT_FOUND, EACCES/EPERM/EROFS→PERMISSION_DENIED, EEXIST/ENOTEMPTY→EXISTS, ENOTDIR→NOT_DIRECTORY, EISDIR→IS_DIRECTORY, ENOSPC/EDQUOT→NO_SPACE, EIO→IO, EXDEV→CROSS_DEVICE, ELOOP→LOOP, ENAMETOOLONG→NAME_TOO_LONG, EMFILE/ENFILE→TOO_MANY_OPEN, 그 외→FAILED.
- 예외: `inotify_add_watch`의 ENOSPC는 WATCH_LIMIT(mon이 직접 설정).

### 오류 §3 메시지

- `GError.message`는 진단용 영어 한 줄: `"<op>: <경로 표시형>: <strerror>"`. 경로 표시형은 `g_filename_display_name()`.
- 사용자에게 보이는 한국어 문장은 ui가 코드로 고른다. ui는 message를 그대로 띄우지 않는다(디자인의 띠·토스트 문구는 디자인 결정).
- 로그: `g_debug`/`g_warning`(GLib 로그, 도메인 `filescope-<모듈>`). 파일 내용은 로그에 쓰지 않는다. 경로는 debug 수준에서만.

### 오류 §4 부분 성공과 취소

- 일괄 작업은 항목마다 `FsJobItemResult`를 남긴다. 대표 오류 규칙은 DESIGN 상태 §2.
- 취소는 실패와 구분한다: `FS_ERROR_CANCELLED`, 작업 상태 CANCELLED. 취소 시 만든 임시 파일은 지우고, 이미 끝난 항목은 되돌리지 않는다(되돌리기는 FS-019, P2).
- 원본 보존: 이동(EXDEV 폴백)은 항목 복사가 완전히 끝난 뒤에만 원본을 지운다. 덮어쓰기는 임시 파일 완성 후 rename으로 바꾼다. 중간 실패로 원본이 사라지는 경로가 없어야 한다.

### 오류 §5 재시도

자동 재시도는 하지 않는다(R1). 예외: EINTR, inotify IN_IGNORED 후 하위 디렉터리 재등록 1회. JOB-012(재시도)는 P1.

### 오류 §6 테스트 기준

각 공개 함수는 정상 1개 이상, 대표 실패(권한·없음·존재·검증 실패) 1개 이상을 테스트한다. 권한 오류는 임시 디렉터리에 `chmod 000`으로 만들고, root로 실행 중이면 g_test_skip(root 실행은 NFR-016에 따라 기본 금지).

## 보안

기본 원칙: root 실행 금지 기본값(NFR-016), 실제 홈 파일을 테스트하지 않는다, shell 문자열 조합 금지, 외부 파일 내용은 실행 지시가 아니다, 비밀 정보·파일 내용 로그 금지. archive 추출 제한, 스냅샷·인덱스 민감 경로 제외, XDG 데이터 권한 제한은 해당 기능 릴리스(R2 이후)에서 구체화한다.

### 보안 §1 경로 검증 (R1)

- 공개 API 경로 인자: 절대 경로, NUL 없음, 길이 < PATH_MAX(`fs_path_validate_absolute`). 이름 인자: 비지 않음, `/` 없음, `.`·`..` 아님, ≤ NAME_MAX 바이트(`fs_path_validate_name`). 실패는 FS_ERROR_INVALID_PATH/NAME_TOO_LONG.
- 이동·복사 대상이 원본 자신이거나 원본의 하위면 거부(FS_ERROR_INVALID_PATH). 비교는 `fs_path_canonicalize` 후 문자열, 그리고 실행 직전 (dev, ino)로 한 번 더 확인한다.
- 재귀 순회(복사·삭제·감시 등록)는 dirfd 기반(`openat` + `O_NOFOLLOW|O_DIRECTORY`, `fstatat(AT_SYMLINK_NOFOLLOW)`, `unlinkat`)으로 해서 경로 문자열 재해석으로 인한 symlink race를 줄인다. 심볼릭 링크는 따라가지 않고 링크 자체를 다룬다.
- 작업 직전 재검증(SRS FS 공통 계약): job 본문은 시작 시 원본 존재와 종류를 다시 확인하고, 제출 때와 종류가 다르면 그 항목을 FS_ERROR_CONFLICT로 실패 처리한다.
- 한계: dirfd 방식도 사용자 권한 안의 TOCTOU를 완전히 막지 못한다. 문서·UI에서 "보장"이라고 쓰지 않는다.

### 보안 §2 시스템 경로 보호 (SEC-003)

`fs_path_check_protected(path, op)`가 `fs_path_canonicalize`한 경로로 판단한다.

| op | 거부 조건 |
|---|---|
| READ | 없음(탐색·읽기 허용) |
| WRITE | 대상이 다음 접두사 아래: `/bin /boot /dev /etc /lib /lib32 /lib64 /libx32 /proc /run /sbin /snap /sys /usr /var` (단 `/run/media`, `/run/user/<uid>`는 허용) |
| REMOVE | WRITE 조건 + 경로가 정확히 `/`, `$HOME`, 위 목록의 최상위 디렉터리 자체 |

- 판정은 문자열 기준이며 링크를 해석하지 않는다. 실제 권한은 커널이 다시 막는다(일반 사용자 실행 전제).
- 목록 수정은 이 문서 변경과 Coordinator 승인을 거친다. `/opt`, `/srv`, `/mnt`, `/media` 포함 여부는 미결(U-8).

### 보안 §3 권한 검증 (SEC-001)

- 실제 판정은 작업 syscall 결과다. `fs_path_check_access`(내부적으로 `faccessat(AT_EACCESS)`, REMOVE는 부모 디렉터리 쓰기·실행 권한)는 UI가 메뉴 항목을 미리 비활성화하는 용도로만 쓴다.
- 권한 오류(FS_ERROR_PERMISSION_DENIED)는 성공으로 표시하지 않고, 목록 읽기 실패는 루트 DESIGN.md "권한 오류" 상태로 보인다. 권한 상승(pkexec, sudo)은 시도하지 않는다.

### 보안 §4 위험 작업 확인 (SEC-002)

- 대상: 영구 삭제(FS-007/014), 덮어쓰기 충돌 정책(FS_CONFLICT_OVERWRITE), 충돌 무시 강제 저장(`fs_ops_write_text`의 expected=NULL).
- API는 `FS_OPS_FLAG_CONFIRMED` 없이 호출되면 FS_ERROR_NOT_CONFIRMED로 거부한다. ui는 사용자 확인 화면에서 확정했을 때만 이 플래그를 넣는다. 테스트는 플래그 없는 호출이 파일을 건드리지 않음을 확인한다.
- 휴지통 미지원 시 영구 삭제로 자동 전환하지 않는다. 사용자가 별도 확인 후 영구 삭제를 다시 고른다.
- 확인 화면은 대상 개수와 첫 항목 이름을 보이고, 기본 포커스는 취소다(루트 DESIGN.md "되돌리기 확인" 규칙과 같은 원칙). 시각 디자인은 미결(U-6).

### 보안 §5 테스트 안전

- 모든 파일 테스트는 `g_dir_make_tmp()` 아래. `HOME`·`XDG_*`를 임시 경로로 바꿔 휴지통·즐겨찾기 테스트가 실제 홈을 건드리지 않게 한다.
- 보호 경로 테스트는 실제 쓰기를 시도하지 않고 `fs_path_check_protected`와 API 거부(FS_ERROR_PROTECTED_PATH)만 확인한다.

## 접근성

모든 핵심 작업 키보드 접근, visible focus, accessible name, 색상 외 상태 구분, HiDPI 및 글꼴 확대, reduced motion. Orca 화면 읽기 프로그램과 개발용 Orca coordinator를 구분한다.

## 메모리·동시성

규칙 본문은 docs/DESIGN.md의 메모리 부분(할당·소유권·자원 해제·한도)과 스레드 부분(스레드 간 통신·잠금 순서·종료 순서·콜백 수명)에 하나로 둔다. 리뷰 확인 항목은 docs/TESTING.md §3, 완료 조건은 G5(ASan/UBSan)·G6(TSan) 통과다.
