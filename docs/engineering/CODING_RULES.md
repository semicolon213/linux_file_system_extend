# C17 코딩 규칙

- 형식: `.clang-format`(LLVM, 들여쓰기 4, 100열). 검사: `.clang-tidy`(경고=오류). 둘 다 G4.
- 이름: snake_case 함수·변수, 공개 함수 `fs_<모듈>_`(core는 `fs_`), 타입 `Fs` + CamelCase, 상수·enum 값 `FS_` 대문자. 내부 함수는 `static`, 모듈 안 여러 파일이 쓰는 내부 함수는 `*_internal.h`에 선언하고 접두사 `<모듈>_`(예: `mon_registry_add`).
- 헤더: include guard `FILESCOPE_<모듈>_<파일>_H`. 공개 헤더는 자기 의존(GLib 등)을 직접 include해 단독으로 컴파일된다. 공개 헤더에 GTK를 include하는 모듈은 ui뿐이다.
- include 경로: `"core/fs_error.h"`처럼 src 기준. 다른 모듈의 `*_internal.h`, `*_testing.h`(tests 제외) include 금지(ARCHITECTURE §1, check-architecture.sh).
- 공개 API 주석: 입력, 출력, 오류 코드, 소유권(transfer), 스레드(`[main]`/`[any]`)를 적는다. INTERFACE_SPEC과 다르면 INTERFACE_SPEC이 정본이다.
- 할당: GLib 계열(MEMORY_MANAGEMENT §1). `g_autoptr`/`g_autofree` 권장.
- syscall: 반환값 전부 확인, EINTR 재시도, 부분 read/write 처리, errno 즉시 저장. fd는 `O_CLOEXEC`.
- 경로: 고정 길이 버퍼 금지. 재귀 순회는 `openat`/`fstatat`/`unlinkat`(dirfd 기반)과 `O_NOFOLLOW`/`AT_SYMLINK_NOFOLLOW`.
- 외부 실행: shell 문자열 금지, argv 기반(GIO `g_app_info_*`, `g_spawn_async` argv)만. 사용자 파일을 실행하지 않는다.
- GTK: 메인 스레드만. worker에서 위젯·모델 접근 금지.
- 매직 넘버: 시간창·큐 크기 등 계약 수치는 모듈 내부 헤더에 이름 있는 상수로 두고 EVENT_SPEC 등 문서 값과 같게 한다.
- 테스트: `g_test` 경로 `/<모듈>/<주제>/<사례>`, 실제 파일은 `g_dir_make_tmp()` 아래에서만(ARCHITECTURE §4).
