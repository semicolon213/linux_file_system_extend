# 엔지니어링 규칙

빌드, 의존성, C++17 코딩 규칙, 오류 처리, 보안, 접근성. 스레드·메모리 계약 본문은 docs/DESIGN.md(스레드·메모리 부분), 검증 명령은 docs/TESTING.md, 시각 디자인 권한은 AGENTS.md가 정본이다. 언어 결정은 ADR-0007, C++ 관용 규칙의 근거는 ADR-0008.

## 빌드

CMake/Ninja, C++17, out-of-source build. 루트 `project(filescope ... LANGUAGES CXX)`이며 C 언어는 켜지 않는다(C 소스가 없다. C 라이브러리 헤더는 C++에서 그대로 include한다). `CMAKE_CXX_STANDARD 17`, `CMAKE_CXX_STANDARD_REQUIRED ON`, `CMAKE_CXX_EXTENSIONS OFF`. 의존 라이브러리는 `pkg_check_modules(... IMPORTED_TARGET ...)`로 찾는다(gtkmm-3.0, glibmm-2.4, giomm-2.4, sigc++-2.0, gio-2.0, glib-2.0). 컴파일 경고: `-Wall -Wextra -Wpedantic -Werror`(프로젝트 코드). 외부 헤더 경고는 pkg-config include를 SYSTEM으로 넣어 막는다(경고를 끄는 pragma로 숨기지 않는다).

진입점은 `Makefile` 하나다(대상 목록은 AGENTS.md "명령", 게이트 대응은 docs/TESTING.md §1). 빌드 디렉터리는 `CMakePresets.json` 프리셋별로 분리한다(`build/debug`, asan, ubsan, tsan). 기존 소스와 빌드 설정을 확인하기 전 덮어쓰지 않는다. 모듈별 CMake 소유권은 docs/ARCHITECTURE.md §3(ADR-0004).

## 의존성

필수: build-essential(g++), cmake, ninja-build, pkg-config, libgtk-3-dev, libgtkmm-3.0-dev(glibmm-2.4, giomm, cairomm, libsigc++-2.0 개발 패키지를 함께 설치), libsqlite3-dev, libayatana-appindicator3-dev(트레이, PRODUCT.md). 개발·검증: xvfb, clang-format, clang-tidy. 선택: libarchive-dev, libgtksourceview-4-dev, libvte-2.91-dev, libpoppler-glib-dev, libgit2-dev. Ubuntu 22.04/24.04 모두 기본 저장소에 gtkmm 3.24가 있다(`Gtk::make_managed` 사용 가능). Ubuntu 버전별 설치 가능 여부 확인 후 버전 고정. 선택 기능 미지원 시 명시적으로 비활성화. 새 C++ 라이브러리(Boost, GoogleTest, fmt 등)는 ADR 없이 추가하지 않는다.

설치 명령(사용자가 직접, `sudo` 필요):

```
sudo apt install build-essential cmake ninja-build pkg-config libgtk-3-dev libgtkmm-3.0-dev libsqlite3-dev libayatana-appindicator3-dev xvfb clang-format clang-tidy
```

## 코딩 규칙 (C++17)

- 형식: `.clang-format`(LLVM 기반, 들여쓰기 4, 100열, `Standard: c++17`). 검사: `.clang-tidy`(경고=오류). 둘 다 G4. `make format-check`·`make lint`는 `src/`, `tests/`의 `*.cpp`, `*.hpp`를 모두 대상으로 한다.
- clang-tidy 검사 묶음: `clang-analyzer-*`, `bugprone-*`, `performance-*`, `portability-*`, `modernize-use-nullptr`, `modernize-use-override`, `cppcoreguidelines-no-malloc`, `cppcoreguidelines-owning-memory`. 특정 검사가 glibmm/gtkmm 관용구와 맞지 않으면 `.clang-tidy`에서 그 검사 하나만 끄고 이유를 주석으로 남긴 뒤 Coordinator에 보고한다. 코드 안 `NOLINT`는 줄 단위로만, 이유와 함께.
- 이름: 이름 공간 `filescope::<모듈>`. 타입 `CamelCase`, 함수·변수·멤버 함수 `snake_case`, private 멤버 `snake_case_`(끝 밑줄), 상수 `kCamelCase`, `enum class` 값은 문서 용어와 같은 대문자(`ErrorCode::NOT_FOUND`). 파일 안에서만 쓰는 함수·타입은 이름 없는 이름 공간에 둔다. 모듈 안 여러 파일이 쓰는 내부 선언은 `*_internal.hpp`.
- 헤더: `#pragma once`. 공개 헤더는 자기 의존(glibmm 등)을 직접 include해 단독으로 컴파일된다. 공개 헤더에 gtkmm/GTK를 include하는 모듈은 ui뿐이다. 헤더에 `using namespace` 금지.
- include 경로: `"core/error.hpp"`처럼 src 기준. 다른 모듈의 `*_internal.hpp`, `*_testing.hpp`(tests 제외) include 금지(ARCHITECTURE §1, check-architecture.sh).
- 공개 API 주석: 입력, 출력, 오류 코드, 소유권(값·참조·unique_ptr·EventPtr로 드러나지 않는 것만), 스레드(`[main]`/`[any]`/`[worker]`)를 적는다. DESIGN 인터페이스와 다르면 DESIGN 인터페이스가 정본이다.
- 예외: 모듈 경계(공개 함수, 신호 처리기, 슬롯, 스레드 진입 함수)를 넘는 예외는 없다. 실패는 `core::Result<T>`로 돌려준다(오류 §1). 예외를 제어 흐름으로 쓰지 않는다. `-fno-exceptions`는 쓰지 않는다(glibmm이 예외를 던진다).
- 소유권: RAII만. 앱 코드에 `new`/`delete`, `malloc`/`free`, 소유 raw 포인터가 없다(`std::make_unique`, `std::make_shared`, `Gtk::make_managed`, `Glib::RefPtr`, `core::UniqueFd`). 비소유 관찰은 참조 또는 raw 포인터(널 가능할 때)로 한다. 규칙 본문은 DESIGN 메모리 §1.
- const 정확성: 바꾸지 않는 멤버 함수는 `const`, 읽기 전용 인자는 `const T&`(작은 값 타입은 값), 지역 변수도 바꾸지 않으면 `const`. 이벤트는 `std::shared_ptr<const Event>`라 공유 후 바뀌지 않는다.
- 특수 멤버: 자원을 가진 클래스는 복사·이동을 명시한다(`= delete` 또는 정의). 그 외는 rule of zero.
- 형 변환: C 스타일 캐스트 금지(`static_cast` 등). GLib C API 경계의 `reinterpret_cast`는 그 경계 함수 안에만 둔다.
- 정수: 크기·바이트는 `std::uint64_t`/`std::int64_t`, 개수는 `std::size_t`. 부호 섞인 비교는 경고=오류로 막힌다.
- syscall: 반환값 전부 확인, EINTR 재시도, 부분 read/write 처리, errno 즉시 저장. fd는 `O_CLOEXEC`이고 받자마자 `core::UniqueFd`로 감싼다.
- 경로: 원본 바이트는 `std::string`, 화면 문자열은 `Glib::ustring`(ui가 `Glib::filename_display_name()`으로 만듦). 두 타입을 암시적으로 바꾸지 않는다. 고정 길이 버퍼 금지. 재귀 순회는 `openat`/`fstatat`/`unlinkat`(dirfd 기반)과 `O_NOFOLLOW`/`AT_SYMLINK_NOFOLLOW`. `std::filesystem`은 쓰지 않는다(링크·dirfd·errno 제어가 부족하고 오류를 예외로 던진다).
- 외부 실행: shell 문자열 금지, argv 기반(`Gio::AppInfo`, `Glib::spawn_async` argv)만. 사용자 파일을 실행하지 않는다.
- 스레드: `std::thread`, `std::mutex`, `std::condition_variable`, `std::atomic`. 스레드 → 메인 전달은 `Glib::Dispatcher`만(DESIGN 스레드 §2). GTK·sigc++는 메인 스레드만. worker에서 위젯·모델·신호 접근 금지.
- 콜백: 메인 스레드 콜백은 sigc++(`sigc::signal`, `sigc::slot`), 다른 스레드에서 실행되는 함수는 `std::function`(DESIGN 인터페이스 §0). ui 처리기는 `sigc::mem_fun`으로 `sigc::trackable` 객체에 묶는다. 람다에 `this`를 잡아 신호에 연결할 때는 연결(`sigc::connection`)을 객체가 들고 소멸 때 끊는다.
- 매직 넘버: 시간창·큐 크기 등 계약 수치는 이름 있는 `constexpr` 상수로 두고 DESIGN 이벤트 등 문서 값과 같게 한다.
- 테스트: `g_test`를 C++에서 그대로 쓴다. 경로 `/<모듈>/<주제>/<사례>`, 실제 파일은 `g_dir_make_tmp()` 아래에서만(ARCHITECTURE §4). 테스트 코드도 위 규칙(RAII, `new` 금지)을 따른다.

## 오류 처리 (R1)

ADR-0002(코드와 원칙), ADR-0008(C++ 표현)을 따른다.

### 오류 §1 모델

- 공개 API는 `core::Error{code, message}` 하나만 밖으로 내보낸다. 코드는 `core::ErrorCode`(DESIGN 인터페이스 §1, C판 `FsErrorCode`와 같은 이름·의미). 모듈별 오류 enum을 따로 두지 않는다. 실패할 수 있는 함수는 `core::Result<T>`를 돌려준다.
- glibmm/giomm이 던지는 `Glib::Error`(및 `Gio::Error`, `Glib::FileError`, `Glib::KeyFileError`)는 그 호출을 감싼 함수 안에서 `catch (const Glib::Error &e)`로 잡아 `core::error_from_glib(e)`로 바꾼다. 대응(C판 `fs_propagate_gio_error`와 같음): `G_IO_ERROR`의 NOT_FOUND→NOT_FOUND, PERMISSION_DENIED·READ_ONLY→PERMISSION_DENIED, EXISTS·NOT_EMPTY→EXISTS, NOT_DIRECTORY→NOT_DIRECTORY, IS_DIRECTORY→IS_DIRECTORY, NO_SPACE→NO_SPACE, NOT_SUPPORTED→NOT_SUPPORTED, CANCELLED→CANCELLED, TOO_MANY_OPEN_FILES→TOO_MANY_OPEN, FILENAME_TOO_LONG→NAME_TOO_LONG, INVALID_FILENAME→INVALID_PATH, 그 외 코드와 다른 도메인→FAILED. message는 원본을 보존한다.
- 실패 반환: `Result`가 Error를 담는다. "없음"이 실패가 아닌 조회(`find_root`, `root_total`, `back`)는 0, `std::nullopt`, `false`로 알리고 Error를 쓰지 않는다.
- 예외가 모듈 밖으로 새면 계약 위반이다. 스레드 진입 함수와 신호 처리기는 마지막 방어로 `catch (...)`를 두고 FAILED/IO로 바꾸며 `g_warning`으로 기록한다. `std::bad_alloc`은 잡지 않는다(DESIGN 메모리 §1).

### 오류 §2 errno

- syscall 실패 직후 `const int saved = errno;`로 저장하고 그 뒤에 다른 호출(로그, close, 소멸자)을 한다. RAII 소멸자가 errno를 바꿀 수 있으므로 저장이 먼저다.
- `EINTR`는 재시도한다. 부분 read/write는 끝까지 반복한다.
- 대응표는 `core::error_code_from_errno`가 정본: ENOENT→NOT_FOUND, EACCES/EPERM/EROFS→PERMISSION_DENIED, EEXIST/ENOTEMPTY→EXISTS, ENOTDIR→NOT_DIRECTORY, EISDIR→IS_DIRECTORY, ENOSPC/EDQUOT→NO_SPACE, EIO→IO, EXDEV→CROSS_DEVICE, ELOOP→LOOP, ENAMETOOLONG→NAME_TOO_LONG, EMFILE/ENFILE→TOO_MANY_OPEN, 그 외→FAILED.
- 예외: `inotify_add_watch`의 ENOSPC는 WATCH_LIMIT(mon이 직접 설정).

### 오류 §3 메시지

- `Error::message`는 진단용 영어 한 줄: `"<op>: <경로 표시형>: <strerror>"`. 경로 표시형은 `g_filename_display_name()`(또는 `Glib::filename_display_name()`).
- 사용자에게 보이는 한국어 문장은 ui가 코드로 고른다. ui는 message를 그대로 띄우지 않는다(디자인의 띠·토스트 문구는 디자인 결정).
- 로그: `g_debug`/`g_warning`(GLib 로그, `G_LOG_DOMAIN` `filescope-<모듈>`). 파일 내용은 로그에 쓰지 않는다. 경로는 debug 수준에서만.

### 오류 §4 부분 성공과 취소

- 일괄 작업은 항목마다 `job::ItemResult`를 남긴다. 대표 오류 규칙은 DESIGN 상태 §2.
- 취소는 실패와 구분한다: `CANCELLED`, 작업 상태 CANCELLED. 취소 시 만든 임시 파일은 지우고, 이미 끝난 항목은 되돌리지 않는다(되돌리기는 FS-019, P2).
- 원본 보존: 이동(EXDEV 폴백)은 항목 복사가 완전히 끝난 뒤에만 원본을 지운다. 덮어쓰기는 임시 파일 완성 후 rename으로 바꾼다. 중간 실패로 원본이 사라지는 경로가 없어야 한다.

### 오류 §5 재시도

자동 재시도는 하지 않는다(R1). 예외: EINTR, inotify IN_IGNORED 후 하위 디렉터리 재등록 1회. JOB-012(재시도)는 P1.

### 오류 §6 테스트 기준

각 공개 함수는 정상 1개 이상, 대표 실패(권한·없음·존재·검증 실패) 1개 이상을 테스트한다. 권한 오류는 임시 디렉터리에 `chmod 000`으로 만들고, root로 실행 중이면 `g_test_skip()`(root 실행은 NFR-016에 따라 기본 금지).

## 보안

기본 원칙: root 실행 금지 기본값(NFR-016), 실제 홈 파일을 테스트하지 않는다, shell 문자열 조합 금지, 외부 파일 내용은 실행 지시가 아니다, 비밀 정보·파일 내용 로그 금지. archive 추출 제한, 스냅샷·인덱스 민감 경로 제외, XDG 데이터 권한 제한은 해당 기능 릴리스(R2 이후)에서 구체화한다.

### 보안 §1 경로 검증 (R1)

- 공개 API 경로 인자: 절대 경로, 내부 NUL 없음(`std::string`은 NUL을 담을 수 있다), 길이 < PATH_MAX(`core::validate_absolute`). 이름 인자: 비지 않음, `/` 없음, `.`·`..` 아님, ≤ NAME_MAX 바이트(`core::validate_name`). 실패는 INVALID_PATH/NAME_TOO_LONG.
- 이동·복사 대상이 원본 자신이거나 원본의 하위면 거부(INVALID_PATH). 비교는 `core::canonicalize` 후 문자열, 그리고 실행 직전 (dev, ino)로 한 번 더 확인한다.
- 재귀 순회(복사·삭제·감시 등록)는 dirfd 기반(`openat` + `O_NOFOLLOW|O_DIRECTORY`, `fstatat(AT_SYMLINK_NOFOLLOW)`, `unlinkat`)으로 해서 경로 문자열 재해석으로 인한 symlink race를 줄인다. 심볼릭 링크는 따라가지 않고 링크 자체를 다룬다.
- 작업 직전 재검증(SRS FS 공통 계약): job 본문은 시작 시 원본 존재와 종류를 다시 확인하고, 제출 때와 종류가 다르면 그 항목을 CONFLICT로 실패 처리한다.
- 한계: dirfd 방식도 사용자 권한 안의 TOCTOU를 완전히 막지 못한다. 문서·UI에서 "보장"이라고 쓰지 않는다.

### 보안 §2 시스템 경로 보호 (SEC-003)

`core::check_protected(path, op)`가 `core::canonicalize`한 경로로 판단한다.

| op | 거부 조건 |
|---|---|
| READ | 없음(탐색·읽기 허용) |
| WRITE | 대상이 다음 접두사 아래: `/bin /boot /dev /etc /lib /lib32 /lib64 /libx32 /proc /run /sbin /snap /sys /usr /var` (단 `/run/media`, `/run/user/<uid>`는 허용) |
| REMOVE | WRITE 조건 + 경로가 정확히 `/`, `$HOME`, 위 목록의 최상위 디렉터리 자체 |

- 판정은 문자열 기준이며 링크를 해석하지 않는다. 실제 권한은 커널이 다시 막는다(일반 사용자 실행 전제).
- 목록 수정은 이 문서 변경과 Coordinator 승인을 거친다. `/opt`, `/srv`, `/mnt`, `/media` 포함 여부는 미결(U-8).

### 보안 §3 권한 검증 (SEC-001)

- 실제 판정은 작업 syscall 결과다. `core::check_access`(내부적으로 `faccessat(AT_EACCESS)`, REMOVE는 부모 디렉터리 쓰기·실행 권한)는 UI가 메뉴 항목을 미리 비활성화하는 용도로만 쓴다.
- 권한 오류(PERMISSION_DENIED)는 성공으로 표시하지 않고, 목록 읽기 실패는 루트 DESIGN.md "권한 오류" 상태로 보인다. 권한 상승(pkexec, sudo)은 시도하지 않는다.

### 보안 §4 위험 작업 확인 (SEC-002)

- 대상: 영구 삭제(FS-007/014), 덮어쓰기 충돌 정책(`ConflictPolicy::OVERWRITE`), 충돌 무시 강제 저장(`fs::write_text`의 expected 없음).
- API는 `fs::Confirmation::CONFIRMED` 없이 호출되면 NOT_CONFIRMED로 거부한다. ui는 사용자 확인 화면에서 확정했을 때만 CONFIRMED를 넘긴다. 테스트는 CONFIRMED 없는 호출이 파일을 건드리지 않음을 확인한다.
- 휴지통 미지원 시 영구 삭제로 자동 전환하지 않는다. 사용자가 별도 확인 후 영구 삭제를 다시 고른다.
- 확인 화면은 대상 개수와 첫 항목 이름을 보이고, 기본 포커스는 취소다(루트 DESIGN.md "되돌리기 확인" 규칙과 같은 원칙). 시각 디자인은 미결(U-6).

### 보안 §5 테스트 안전

- 모든 파일 테스트는 `g_dir_make_tmp()` 아래. `HOME`·`XDG_*`를 임시 경로로 바꿔 휴지통·즐겨찾기 테스트가 실제 홈을 건드리지 않게 한다.
- 보호 경로 테스트는 실제 쓰기를 시도하지 않고 `core::check_protected`와 API 거부(PROTECTED_PATH)만 확인한다.

### 보안 §6 이력 저장 (R2)

- 이력 데이터는 `$XDG_DATA_HOME/filescope`(0700, 파일 0600)에만 둔다. 민감·소음 경로 제외 목록과 권한 처리는 DESIGN DB 스키마 §1·§8이 정본이다.
- 파일 내용(블롭)은 로그·이벤트 행에 넣지 않는다. 경로는 DB에 남는다(이벤트 기록이 목적).
- 복원·복구도 사용자 파일 쓰기 규칙(보호 경로, 원자 교체, stamp 충돌)을 그대로 따른다(`fs::install_file`, DESIGN 인터페이스 §3 R2 추가).

## 접근성

모든 핵심 작업 키보드 접근, visible focus, accessible name, 색상 외 상태 구분, HiDPI 및 글꼴 확대, reduced motion. Orca 화면 읽기 프로그램과 개발용 Orca coordinator를 구분한다.

## 메모리·동시성

규칙 본문은 docs/DESIGN.md의 메모리 부분(할당·소유권·자원 해제·한도)과 스레드 부분(스레드 간 통신·잠금 순서·종료 순서·콜백 수명)에 하나로 둔다. 리뷰 확인 항목은 docs/TESTING.md §3, 완료 조건은 G5(ASan/UBSan)·G6(TSan) 통과다.
