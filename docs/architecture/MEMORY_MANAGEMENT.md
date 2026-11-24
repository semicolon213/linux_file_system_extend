# 메모리 계약 (R1)

상태: R1 계약 제안 (ARCH-001). 함수별 소유권 표기는 INTERFACE_SPEC.md가 정본이다.

## 1. 할당 계열

- 프로젝트 코드는 GLib 할당(`g_malloc`/`g_new0`/`g_strdup`/`g_free`)만 쓴다. `malloc`/`free`는 libc가 돌려준 메모리(`realpath`, `getline` 등)를 해제할 때만 짝으로 쓴다.
- 구조체 해제 함수는 `fs_<모듈>_<타입>_free`, NULL을 받아도 된다. 참조 카운트 타입(FsEvent)만 `ref/unref`.
- `g_autoptr`/`g_autofree`를 쓴다. 공개 타입마다 해당 헤더에 `G_DEFINE_AUTOPTR_CLEANUP_FUNC`를 둔다(FsEvent, FsEntry, FsWatcher, FsJobQueue, FsNavHistory, FsNavBookmarks).

## 2. 소유권 요약

| 대상 | 규칙 |
|---|---|
| 공개 함수가 돌려준 `char *`, `char **`, `GPtrArray *` | transfer full. 배열은 free func가 설정되어 `g_ptr_array_unref` 한 번으로 해제 |
| 콜백 인자 (`const GPtrArray *events`, `const FsJobProgress *`, `const GError *`, `const FsWatchInfo *`) | transfer none, 콜백 동안만 유효 |
| FsEvent | 불변, 원자 refcount. 배치 배열이 1 ref를 갖고 콜백 반환 후 해제. 보관은 ref |
| 작업 `data` | submit 성공 시 큐가 인수, 끝난 뒤 main에서 `data_free`. submit 실패 시 호출자가 그대로 보유(큐는 해제하지 않음) |
| 입력 경로 배열 | 함수가 깊은 복사. 호출 후 호출자가 자유롭게 해제 |
| `GError` | 표준 GLib 규칙. 콜백의 `const GError *`는 빌려준 것이므로 보관하려면 `g_error_copy` |

## 3. 자원 해제 (오류 경로 포함)

- fd: `O_CLOEXEC`로 열고 모든 반환 경로에서 `close`(`g_autofd` 사용 가능, GLib ≥ 2.76이 아니면 직접 close; Ubuntu 22.04는 2.72라 직접 처리).
- `DIR *`: `closedir`. `fdopendir`에 넘긴 fd는 DIR이 소유한다.
- inotify: watcher free 시 wd를 모두 rm_watch할 필요는 없고 fd close로 정리되지만, registry 메모리는 모두 해제한다.
- 임시 파일: 복사·텍스트 저장 중 만든 `.filescope-*` 임시 파일은 실패·취소 경로에서 `unlinkat`한다(E06).
- GSource/invoke 메시지: watcher·job이 보낸 메시지는 공유 상태를 ref로 잡아 객체 해제 뒤 실행되어도 안전하다.

## 4. 크기와 한도

- 크기 계산은 `g_size_checked_add`/`g_uint64_checked_add` 등으로 overflow를 확인한다.
- 고정 크기 경로 버퍼를 쓰지 않는다. inotify read 버퍼만 고정 크기(최소 `sizeof(struct inotify_event) + NAME_MAX + 1`, 권장 64KiB)이며 이벤트 경계를 `len` 필드로 넘는다.
- 상한: 전달 큐 8192 이벤트, 이동 이력 100개, 텍스트 읽기 4MiB, 이벤트 로그 UI 500행(DESIGN.md).

## 5. 검증

G5: `make asan ubsan`(누수 포함, `scripts/lsan.supp`는 GLib/GTK 전역 할당만 억제). Valgrind는 선택.
