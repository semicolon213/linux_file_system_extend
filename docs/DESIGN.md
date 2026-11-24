# 기술 설계 (내부)

이 문서는 내부 기술 설계다. 루트 `DESIGN.md`는 시각 디자인(Impeccable, 사용자 소유)으로 별개 문서다.

- 상태: R1 계약 제안(ARCH-001, 2026-11-24), C++17 + gtkmm-3.0 판(ADR-0007, ADR-0008, 2026-11-24). 계층·모듈 배치·빌드 타깃은 docs/ARCHITECTURE.md, 결정 근거는 docs/adr/.
- 참조 표기: `DESIGN 인터페이스 §4`는 이 문서 "인터페이스" 부분의 §4를 뜻한다. 부분은 인터페이스, 이벤트, 스레드, 상태, 메모리, 데이터 모델, DB 스키마 순서다.
- 시그니처·수치를 바꾸려면 Coordinator 승인과 이 문서 갱신이 먼저다.

## 인터페이스 (R1 공개 API)

시그니처를 바꾸려면 Coordinator 승인과 이 부분 갱신이 먼저다. 아래 선언은 헤더에 들어갈 공개 부분이며, private 멤버와 구현 세부(pimpl 여부 등)는 구현 작업이 정한다.

### 인터페이스 §0 공통 규칙

- 언어: C++17. 이름 공간 `filescope::<모듈>`(core, job, fs, mon, nav, dash, ui). 공개 헤더 `src/<m>/<m>.hpp`(core는 여러 개), 내부 헤더 `*_internal.hpp`, 테스트 전용 헤더 `*_testing.hpp`(ADR-0008).
- 이름: 타입 `CamelCase`, 함수·변수 `snake_case`, `enum class` 값은 문서 용어와 같은 대문자(`ErrorCode::NOT_FOUND`, `JobState::QUEUED`, `EventKind::CREATE`). 이 덕분에 문서의 `PARTIAL`, `NOT_CONFIRMED`, `ACTIVE` 같은 표기가 코드 이름과 같다.
- 오류: 실패할 수 있는 함수는 `core::Result<T>`(성공 값 없음은 `core::Result<void>`)를 돌려준다. 실패는 `core::Error{code, message}` 하나로 표현하고 다른 오류 타입은 밖으로 내보내지 않는다(ADR-0002 원칙, ADR-0008 표현). 공개 함수와 신호 슬롯 밖으로 예외를 내보내지 않는다. glibmm/giomm의 `Glib::Error`는 그 호출을 감싼 함수에서 잡아 `core::error_from_glib()`로 바꾼다. 실패할 수 없는 함수는 `noexcept`를 붙일 수 있다(메모리 부족은 예외 아님, 아래 메모리 §1).
- 경로: `std::string`은 Linux 원본 바이트 경로(UTF-8 보장 없음, 내부 NUL 불허)다. 공개 API의 경로 인자는 절대 경로여야 한다(`core::validate_absolute`). 이름 인자(`name`)는 경로 구분자 없는 한 요소다. 화면 표시 문자열(`Glib::ustring`)은 ui가 `Glib::filename_display_name()`으로 만든다. 공개 API는 경로를 `Glib::ustring`으로 받거나 돌려주지 않는다. "없음"은 `std::optional`로 표현한다. 단 빈 문자열이 유효한 값이 될 수 없는 경로 필드(Event의 path·old_path, Progress::current_path)는 빈 문자열이 "없음"이다.
- 소유권: 반환 값은 값(`std::string`, `std::vector`, 구조체)이나 `std::unique_ptr`로 호출자에게 넘어간다. 입력 참조(`const T&`)는 호출 동안만 빌린다. 이벤트는 불변 공유 `core::EventPtr`(`std::shared_ptr<const core::Event>`). GObject는 `Glib::RefPtr`. 공개 API에 소유 raw 포인터가 없다. 스레드나 신호를 가진 객체(JobQueue, Watcher, MainWindow)는 복사·이동 불가다. History, Bookmarks, Dash는 일반 값 타입이다.
- 스레드 표기: `[main]` 메인 스레드 전용, `[any]` 어느 스레드든 가능, `[worker]` 작업 본문 안에서만. 공개 신호·슬롯은 모두 기본 메인 컨텍스트(메인 스레드)에서 호출된다.
- 콜백: 메인 스레드에서 호출되는 공개 콜백은 sigc++다. 여러 구독자가 받는 통지는 `sigc::signal<void(Args...)>`(접근자 `signal_<이름>()`), 1회성 비동기 결과는 `sigc::slot<void(Args...)>` 인자다. 다른 스레드에서 실행되는 함수(작업 본문)만 `std::function`이다(ADR-0008). ui 객체는 `sigc::trackable`(gtkmm 위젯은 이미 그렇다)에 `sigc::mem_fun`으로 연결해, 위젯이 먼저 사라지면 연결이 자동으로 끊기게 한다.
- 재진입: 신호 처리기 안에서 같은 객체의 공개 함수(cancel, remove_root 등)를 불러도 된다. 모듈 객체의 소멸자(또는 `shutdown()`)가 반환한 뒤에는 그 객체의 신호가 다시 호출되지 않는다. 신호 처리기 안에서 그 객체를 소멸시키지 않는다(다음 메인 루프 반복으로 미룬다).
- sigc++ 표기: libsigc++ 2.10의 함수형 표기 `sigc::signal<void(Args...)>`를 쓴다. 2.x 구표기 `sigc::signal<void, Args...>`와 같은 뜻이다. 대상 Ubuntu의 sigc++에서 함수형 표기가 컴파일되지 않으면 구표기로 바꾸고 Coordinator에 보고한다.

옛 C 이름과의 대응 (ADR, harness 기록의 옛 이름을 읽을 때):

| C (ADR-0002~0006, 이전 작업 기록) | C++ |
|---|---|
| `GError` + `FS_ERROR` 도메인, `FsErrorCode` `FS_ERROR_X` | `core::Error`, `core::ErrorCode::X` |
| `gboolean f(..., GError **error)` | `core::Result<void> f(...)` |
| `fs_set_error_from_errno`, `fs_propagate_gio_error` | `core::error_from_errno`, `core::error_from_glib` |
| `fs_path_*` | `core::validate_absolute`, `core::validate_name`, `core::path_join`, `core::canonicalize`, `core::is_ancestor`, `core::check_protected`, `core::check_access` |
| `FsEntry`, `FsFileType`, `FsWatchId` | `core::Entry`, `core::FileType`, `core::WatchId` |
| `FsEvent` + `fs_event_ref/unref` + getter, `fs_event_new_internal` | `core::Event`(공개 불변 구조체) + `core::EventPtr`, `core::make_event` |
| `FsJobQueue`, `FsJob`, `FsJobFunc` + `data`/`data_free` | `job::JobQueue`, `job::Job`, `job::JobBody`(`std::function`, 캡처가 data) |
| `fs_ops_*`, `FS_OPS_FLAG_CONFIRMED` | `fs::*`, `fs::Confirmation::CONFIRMED` |
| `FsWatcher`, `fs_mon_testing.h` | `mon::Watcher`, `mon/mon_testing.hpp` |
| `FsNavHistory`, `FsNavBookmarks`, `fs_nav_*` | `nav::History`, `nav::Bookmarks`, `nav::*` |
| `FsDash`, `FsDashCandle`, `FsDashCounts` | `dash::Dash`, `dash::Candle`, `dash::Counts` |
| `fs_ui_main_window_new`, `fs_ui_shutdown` | `ui::MainWindow::create`, `ui::MainWindow::shutdown` |
| `[main]` 콜백 + `user_data` | sigc++ 신호·슬롯 |
| `g_main_context_invoke_full` (스레드 → 메인) | `Glib::Dispatcher` + 잠금 아래 메시지 큐 |

### 인터페이스 §1 core (`src/core/`)

core는 GLib/glibmm(+ 오류 변환용 GIO 상수)만 쓴다. GTK·gtkmm·sigc++ 신호는 쓰지 않는다.

#### core/error.hpp
```cpp
namespace filescope::core {

enum class ErrorCode {
    FAILED,            // 분류 불가 (errno는 message에)
    NOT_FOUND,         // ENOENT
    PERMISSION_DENIED, // EACCES, EPERM, EROFS
    EXISTS,            // EEXIST, ENOTEMPTY(덮어쓰기 대상)
    NOT_DIRECTORY,     // ENOTDIR
    IS_DIRECTORY,      // EISDIR
    NO_SPACE,          // ENOSPC, EDQUOT (파일 쓰기)
    IO,                // EIO
    CROSS_DEVICE,      // EXDEV (fs 내부 폴백 판단용, 사용자에게 거의 노출 안 됨)
    LOOP,              // ELOOP, O_NOFOLLOW 위반
    NAME_TOO_LONG,     // ENAMETOOLONG, NAME_MAX/PATH_MAX 초과
    INVALID_PATH,      // 상대 경로, 빈 이름, '/', ".", "..", 내부 NUL, 자기 안으로 이동
    PROTECTED_PATH,    // SEC-003
    NOT_CONFIRMED,     // SEC-002: 위험 작업에 확인 없음
    NOT_SUPPORTED,     // 휴지통 미지원, 특수 파일 복사 등
    CONFLICT,          // 읽은 뒤 외부에서 바뀜 (FS-005 저장)
    TOO_LARGE,         // 텍스트 읽기 한도 초과
    CANCELLED,         // 사용자 취소
    WATCH_LIMIT,       // inotify_add_watch ENOSPC (max_user_watches)
    TOO_MANY_OPEN,     // EMFILE, ENFILE, inotify_init1 EMFILE
    PARTIAL,           // 일괄 작업 일부 실패 (항목별 결과 참조)
};

struct Error {
    ErrorCode code = ErrorCode::FAILED;
    std::string message; // 진단용 영어 한 줄 "<op>: <경로 표시형>: <strerror>" (ENGINEERING 오류 §3)
};

// 성공 값 T 또는 Error. C++17에 std::expected가 없어 core가 std::variant 위에 둔다.
template <typename T> class [[nodiscard]] Result {
  public:
    Result(T value);       // 암시적: return value; 로 성공
    Result(Error error);   // 암시적: return error; 로 실패
    bool ok() const noexcept;
    explicit operator bool() const noexcept; // ok()
    const T &value() const &;  // ok()가 아니면 계약 위반 → std::abort()
    T &value() &;
    T &&value() &&;
    const Error &error() const &; // ok()이면 계약 위반 → std::abort()
};
template <> class [[nodiscard]] Result<void> {
  public:
    Result() noexcept;     // 성공
    Result(Error error);
    bool ok() const noexcept;
    explicit operator bool() const noexcept;
    const Error &error() const &;
};

ErrorCode error_code_from_errno(int errnum) noexcept;                                  // [any]
// errno → 코드 + message "<op>: <path 표시형>: <strerror>"
Error error_from_errno(int errnum, std::string_view op, const std::string &path);       // [any]
// Glib::Error(G_IO_ERROR, G_FILE_ERROR, G_KEY_FILE_ERROR 등) → Error. 원본 message 보존
// G_IO_ERROR_NOT_SUPPORTED → NOT_SUPPORTED, G_IO_ERROR_CANCELLED → CANCELLED, 그 외 대응은 ENGINEERING 오류 §1
Error error_from_glib(const Glib::Error &error);                                        // [any]
Error make_error(ErrorCode code, std::string message);                                  // [any]

} // namespace filescope::core
```

#### core/path.hpp (경로 검증, SEC-001/003)
```cpp
namespace filescope::core {

Result<void> validate_absolute(const std::string &path); // [any] 절대·내부 NUL 없음·길이 ≤ PATH_MAX-1
Result<void> validate_name(const std::string &name);     // [any] 비지 않음, '/'·NUL 없음, "."/".." 아님, ≤ NAME_MAX 바이트
std::string path_join(const std::string &dir, const std::string &name); // [any]
// 정규화: 중복 '/', ".", ".." 를 문자열로만 정리(링크는 해석 안 함)
std::string canonicalize(const std::string &path);                       // [any]
bool is_ancestor(const std::string &ancestor, const std::string &path) noexcept; // [any] 같거나 하위면 true, 정규화된 입력 기준

enum class PathOp {
    READ,   // 탐색·읽기: 보호 경로에서도 허용
    WRITE,  // 생성·이름 변경·저장·복사 대상
    REMOVE, // 삭제·휴지통·이동 원본
};

// SEC-003. 보호 규칙은 ENGINEERING 보안 §2. 위반이면 PROTECTED_PATH
Result<void> check_protected(const std::string &path, PathOp op); // [any]
// SEC-001 사전 점검(UI 버튼 활성화용, 권고). 실제 판정은 작업 syscall 결과가 우선한다
Result<void> check_access(const std::string &path, PathOp op);    // [any]

} // namespace filescope::core
```

#### core/types.hpp
```cpp
namespace filescope::core {

using WatchId = std::uint32_t; // 감시 루트 ID, 0은 무효. mon이 발급하고 Event가 참조

enum class FileType { REGULAR, DIRECTORY, SYMLINK, OTHER };

struct Entry {
    std::string name;                     // 원본 바이트 이름
    FileType type = FileType::OTHER;      // lstat 기준
    FileType target_type = FileType::OTHER; // 심볼릭 링크면 stat 기준 대상 종류, 끊긴 링크는 OTHER
    bool is_broken_link = false;
    bool is_mount_point = false;          // st_dev가 부모와 다름
    bool is_hidden = false;               // 이름이 '.'로 시작
    std::uint64_t size = 0;               // lstat st_size
    std::uint32_t mode = 0, uid = 0, gid = 0;
    std::uint64_t dev = 0, ino = 0;       // 영구 ID 아님
    std::int64_t mtime_ns = 0;            // 1970 기준 ns
};

} // namespace filescope::core
```
Entry는 값 타입이다(복사·이동 자유, 해제 함수 없음).

#### core/fd.hpp
```cpp
namespace filescope::core {

// 파일 디스크립터 단독 소유. 소멸 시 close (EINTR 재시도 없음, Linux 규칙)
class UniqueFd {
  public:
    UniqueFd() noexcept = default;
    explicit UniqueFd(int fd) noexcept;
    UniqueFd(UniqueFd &&other) noexcept;
    UniqueFd &operator=(UniqueFd &&other) noexcept;
    UniqueFd(const UniqueFd &) = delete;
    UniqueFd &operator=(const UniqueFd &) = delete;
    ~UniqueFd();
    int get() const noexcept;        // 없으면 -1
    int release() noexcept;          // 소유 포기
    void reset(int fd = -1) noexcept;
    explicit operator bool() const noexcept;
};

} // namespace filescope::core
```
fs, mon, nav가 같은 RAII를 따로 만들지 않도록 core에 둔다. `DIR *`는 `std::unique_ptr<DIR, decltype(&closedir)>`로 감싼다(공개 타입 아님).

#### core/event.hpp
Event 구조와 kind는 DESIGN 이벤트 §1~2가 정본이다. Event는 공개 불변 구조체이며 getter와 별도 내부 헤더가 없다(C판의 `fs_event_internal.h`, getter, ref/unref는 `std::shared_ptr<const Event>`로 대체).
```cpp
namespace filescope::core {

using EventPtr = std::shared_ptr<const Event>;

// [any] Event 값을 옮겨 불변 공유 이벤트를 만든다. mon과 테스트(dash 테스트 포함)가 쓴다
EventPtr make_event(Event event);
const char *to_string(EventKind kind) noexcept; // [any] "CREATE" 등, 정적 문자열

} // namespace filescope::core
```

### 인터페이스 §2 job (`src/job/job.hpp`)

```cpp
namespace filescope::job {

using JobId = std::uint64_t; // 0은 무효

enum class JobState {
    QUEUED, RUNNING, PAUSED /* R1 미사용, JOB-006 예약 */,
    CANCELLED, FAILED, DONE,
};

struct Progress {
    std::uint64_t bytes_done = 0;
    std::int64_t bytes_total = -1;  // -1 = 미확정 → UI indeterminate
    std::uint32_t items_done = 0;
    std::int32_t items_total = -1;  // -1 = 미확정
    std::string current_path;       // 원본 경로, 없으면 빈 문자열
};

struct ItemResult {
    std::string path;                 // 원본 경로
    std::optional<core::Error> error; // 없으면 성공. 건너뜀은 EXISTS 등 원인 + skipped=true
    bool skipped = false;
};

class Job; // 작업 본문이 받는 worker 쪽 손잡이

// worker 스레드에서 실행되는 본문. 진행 보고는 Job의 report_*()로만 한다.
// 취소는 job.is_cancelled() 또는 job.cancellable()로 확인한다.
// 반환: 성공이면 Result<void>{}, 실패면 Error (취소는 CANCELLED).
// 본문이 필요한 데이터는 람다 캡처로 소유한다(C판의 data/data_free). 캡처는 이동 가능해야 하고,
// 복사할 수 없는 자원은 std::shared_ptr로 감싼다(std::function은 복사 가능 객체만 받는다).
using JobBody = std::function<core::Result<void>(Job &job)>;

class Job {
  public:
    const Glib::RefPtr<Gio::Cancellable> &cancellable() const noexcept; // [worker]
    bool is_cancelled() const noexcept;                                  // [worker]
    void report_total(std::int64_t bytes_total, std::int32_t items_total); // [worker]
    // 최근 진행 값을 덮어쓴다. current_path는 복사된다
    void report_progress(std::uint64_t bytes_done, std::uint32_t items_done,
                         const std::string &current_path);                 // [worker]
    void add_item_result(ItemResult result);                              // [worker]
};

class JobQueue {
  public:
    JobQueue();                 // [main] worker 스레드 1개 시작
    ~JobQueue();                // [main] shutdown()을 부른다
    JobQueue(const JobQueue &) = delete;
    JobQueue &operator=(const JobQueue &) = delete;

    // [main] 신규 제출 차단 → 실행 중 작업 취소 → worker 합류 → 대기 작업은 CANCELLED 통지 없이 폐기.
    // 모든 본문(JobBody와 캡처)은 이 안에서 메인 스레드에서 소멸한다. 반환 후 신호 없음. 두 번 불러도 된다.
    void shutdown();

    // [main] title: UI 표시용 UTF-8. 성공하면 body는 큐가 소유하고, 작업이 끝난 뒤 메인 스레드에서 소멸한다.
    // 실패(shutdown 중 → CANCELLED)하면 body는 이 호출 안에서 소멸한다.
    // QUEUED 상태 신호는 반환 전에 이 호출 안에서 동기로 보낸다.
    core::Result<JobId> submit(Glib::ustring title, JobBody body);
    // [main] 취소 요청. QUEUED면 즉시 CANCELLED, RUNNING이면 본문이 정리 후 CANCELLED. 이미 끝났거나 모르는 id면 false
    bool cancel(JobId id);
    JobState state(JobId id) const;                     // [main] 모르는 id → FAILED
    // [main] 끝난 작업의 항목별 결과 사본. 모르는 id이거나 끝나지 않았으면 빈 vector
    std::vector<ItemResult> results(JobId id) const;
    void forget(JobId id);                              // [main] 끝난 작업 기록 해제

    // [main에서 호출됨] error는 끝 상태(FAILED, CANCELLED)에서만 값이 있다
    sigc::signal<void(JobId, JobState, const std::optional<core::Error> &)> &signal_state();
    // [main에서 호출됨] progress는 호출 동안만 유효
    sigc::signal<void(JobId, const Progress &)> &signal_progress();
};

} // namespace filescope::job
```
- 동시 실행 1개(R1). 제출 순서(FIFO)대로 실행한다. JOB-009/010(우선순위, 동시성 설정)은 P1.
- 진행 신호는 작업당 최대 10Hz로 묶어 보내고, 종료 직전 마지막 값을 한 번 더 보낸다.
- 상태 신호 순서는 DESIGN 상태 §2의 전이만 따른다.
- 본문이 예외를 내보내면 worker 진입 함수가 잡아 FAILED(`FAILED`, message에 `what()`)로 처리한다. 예외를 계약 위반으로 보고 테스트에서 쓰지 않는다.

### 인터페이스 §3 fs (`src/fs/fs.hpp`)

```cpp
namespace filescope::fs {

// SEC-002: 영구 삭제·덮어쓰기·강제 저장에 필수. UI가 사용자 확인 후에만 CONFIRMED를 넘긴다
enum class Confirmation { NONE, CONFIRMED };

enum class ConflictPolicy {
    FAIL,      // 기본: 항목을 EXISTS로 실패 처리하고 다음 항목 진행
    SKIP,      // 건너뜀(skipped=true)
    KEEP_BOTH, // "이름 (2).확장자" 형식의 비어 있는 이름 선택
    OVERWRITE, // CONFIRMED 필요. 디렉터리→파일, 파일→디렉터리 덮어쓰기는 금지
};

// --- 동기 작업: 단일 syscall 수준. [any], R1 ui는 메인 스레드에서 호출 ---
// FS-001: O_CREAT|O_EXCL, 모드 0666 & ~umask. 반환: 생성된 절대 경로
core::Result<std::string> create_file(const std::string &dir, const std::string &name);
// FS-002: mkdir 0777 & ~umask
core::Result<std::string> create_dir(const std::string &dir, const std::string &name);
// FS-008: renameat2(RENAME_NOREPLACE). 같은 디렉터리 안에서만. 대상 존재 → EXISTS
core::Result<std::string> rename(const std::string &path, const std::string &new_name);
// FS-006 단일 항목: Gio::File::trash. 미지원이면 NOT_SUPPORTED (영구 삭제로 자동 전환 금지)
core::Result<void> trash(const std::string &path,
                         const Glib::RefPtr<Gio::Cancellable> &cancellable = {});

// 충돌 사전 조회: dest_dir 안에 sources의 basename과 같은 이름이 있는 sources 항목
core::Result<std::vector<std::string>> find_conflicts(const std::vector<std::string> &sources,
                                                      const std::string &dest_dir);

// --- FS-004/005 텍스트 ---
inline constexpr std::size_t kTextMaxBytes = 4u * 1024u * 1024u;

struct FileStamp { // 읽은 시점 식별. 저장 충돌 검사에 쓴다
    std::uint64_t dev = 0, ino = 0, size = 0;
    std::int64_t mtime_ns = 0;
    bool operator==(const FileStamp &other) const noexcept;
    bool operator!=(const FileStamp &other) const noexcept;
};

struct TextFile {
    std::string contents; // 바이트 그대로 (UTF-8 검사는 ui 책임)
    FileStamp stamp;
};

// 정규 파일만. 크기 > kTextMaxBytes → TOO_LARGE
core::Result<TextFile> read_text(const std::string &path);                       // [any]
// 같은 디렉터리 임시 파일에 쓰고 fsync 후 rename으로 교체, 원래 모드 유지.
// 현재 stamp가 expected와 다르면 쓰지 않고 CONFLICT. expected 없음(강제 저장)은 CONFIRMED 필요.
// 성공하면 새 stamp
core::Result<FileStamp> write_text(const std::string &path, const std::string &contents,
                                   const std::optional<FileStamp> &expected,
                                   Confirmation confirmation);                   // [any]

// --- 비동기 작업: job 큐에 제출. 검증 실패는 제출 전에 Error로 반환 [main] ---
// FS-009/012: 재귀 복사. 링크는 링크로 복사, 특수 파일은 항목 실패(NOT_SUPPORTED)
core::Result<job::JobId> copy(job::JobQueue &queue, const std::vector<std::string> &sources,
                              const std::string &dest_dir, ConflictPolicy policy,
                              Confirmation confirmation);
// FS-010/011/012: 같은 장치는 rename, EXDEV면 항목별 복사 완료 후 원본 삭제
core::Result<job::JobId> move(job::JobQueue &queue, const std::vector<std::string> &sources,
                              const std::string &dest_dir, ConflictPolicy policy,
                              Confirmation confirmation);
// FS-006/014: 여러 항목 휴지통. 미지원 항목은 항목 실패로 남긴다
core::Result<job::JobId> trash_many(job::JobQueue &queue, const std::vector<std::string> &paths);
// FS-007/014: 영구 삭제. CONFIRMED 아니면 NOT_CONFIRMED
core::Result<job::JobId> delete_permanently(job::JobQueue &queue,
                                            const std::vector<std::string> &paths,
                                            Confirmation confirmation);

} // namespace filescope::fs
```
- `sources`/`paths`는 제출 시 본문 캡처로 복사한다. 호출 뒤 호출자 vector를 바꿔도 된다.
- 모든 쓰기 작업은 시작 직전에 `core::validate_absolute`, `core::check_protected`를 다시 수행한다(SRS FS 공통 계약 "작업 전 대상 재검증").
- FS-003(기본 앱 열기)은 ui가 `Gio::AppInfo::launch_default_for_uri()`로 처리한다(디스플레이 문맥 필요). FS-013(다중 선택)과 FS-011 잘라내기 상태(앱 내부 클립보드)는 ui 책임이다.

### 인터페이스 §4 mon (`src/mon/mon.hpp`)

```cpp
namespace filescope::mon {

enum class WatchState { REGISTERING, ACTIVE, DEGRADED, RESYNCING, STOPPED, ERROR };

enum class WatchReason {
    NONE,
    USER,         // remove_root
    ROOT_DELETED, // 루트 DELETE_SELF
    ROOT_MOVED,   // 루트 MOVE_SELF
    UNMOUNTED,    // 루트 IN_UNMOUNT
    OVERFLOW,     // 커널 IN_Q_OVERFLOW 또는 전달 큐 포화
    WATCH_LIMIT,  // 하위 감시 일부 등록 실패
    LOST,         // 예상 못한 IN_IGNORED
    IO,           // 등록·재탐색 실패
};

enum class WatchMode { NON_RECURSIVE, RECURSIVE /* MON-018/019 */ };

struct WatchInfo {
    core::WatchId id = 0;
    std::string path;
    WatchMode mode = WatchMode::NON_RECURSIVE;
    WatchState state = WatchState::REGISTERING;
    WatchReason reason = WatchReason::NONE;
    std::uint32_t watch_count = 0; // 이 루트가 쓰는 inotify watch 수
    bool total_known = false;      // REGISTERING, STOPPED, ERROR, 재탐색 중이면 false
    bool total_partial = false;    // 감시 못 한 하위가 있어 합계가 일부만 (DESIGN 이벤트 §8)
    std::uint64_t total_bytes = 0; // 루트 총용량(정규 파일 st_size 합)
    std::uint64_t file_count = 0;  // 색인된 정규 파일 수
};

struct RootTotal {
    std::uint64_t total_bytes = 0;
    std::uint64_t file_count = 0;
    bool partial = false;
};

struct WatchUsage {
    std::uint32_t used = 0;  // 전체 watch 사용 수
    std::uint32_t limit = 0; // /proc/sys/fs/inotify/max_user_watches, 읽기 실패 시 0
};

class Watcher {
  public:
    // [main] inotify_init1(IN_NONBLOCK|IN_CLOEXEC) 후 watcher 스레드 시작. 실패 → Error
    // (EMFILE → TOO_MANY_OPEN). 생성자는 실패를 알릴 수 없으므로 팩터리로 만든다
    static core::Result<std::unique_ptr<Watcher>> create();
    // [main] 스레드 정지·합류, 모든 watch 해제, 미전달 배치 폐기. 반환 후 신호 없음
    ~Watcher();
    Watcher(const Watcher &) = delete;
    Watcher &operator=(const Watcher &) = delete;

    // [main] MON-020/021. 즉시 검사: 절대 경로, 존재하는 디렉터리(링크 따라가지 않음), 기존 루트와 겹치지
    // 않음(같거나 조상·자손이면 EXISTS). 통과하면 REGISTERING 상태로 ID 반환하고 등록 결과는
    // signal_state(ACTIVE/DEGRADED/ERROR)로 알린다
    core::Result<core::WatchId> add_root(const std::string &path, WatchMode mode);
    // [main] 감시 해제. STOPPED(USER) 상태 신호 후 ID 무효. 모르는 ID면 false
    bool remove_root(core::WatchId id);

    std::vector<WatchInfo> list_roots() const;               // [main] 현재 루트 사본
    WatchUsage usage() const;                                // [main] 상태 표시줄용
    // [main] 경로가 어떤 루트의 감시 범위 안인지 (ui "감시하지 않는 폴더" 띠). 없으면 0
    core::WatchId find_root(const std::string &path) const;
    // [main] 루트 총용량(DESIGN 이벤트 §8). main 쪽 사본이며 이미 전달된 마지막 이벤트(또는 상태 신호)
    // 시점 값이다. 모르는 ID이거나 total_known=false면 nullopt
    std::optional<RootTotal> root_total(core::WatchId id) const;

    // [main에서 호출됨] 배치 하나. vector는 호출 동안만 유효, 이벤트를 보관하려면 EventPtr를 복사한다
    sigc::signal<void(const std::vector<core::EventPtr> &)> &signal_events();
    // [main에서 호출됨] error: DEGRADED/ERROR/STOPPED(IO)에서 원인, 그 외 nullopt
    sigc::signal<void(const WatchInfo &, const std::optional<core::Error> &)> &signal_state();
};

} // namespace filescope::mon
```
- 루트 총용량이 정해지거나 크게 바뀌는 시점(REGISTERING→ACTIVE, RESYNC 완료)은 상태 신호의 WatchInfo로도 알린다. 이벤트별 변화는 Event의 size_delta/root_total로만 알린다(별도 신호 없음).

`src/mon/mon_testing.hpp` (tests 전용, 라이브러리에 포함):
```cpp
namespace filescope::mon::testing {
void inject_overflow(Watcher &watcher);                                  // [main] IN_Q_OVERFLOW 합성
void set_queue_capacity(Watcher &watcher, std::uint32_t capacity);       // [main] 포화 재현
void set_move_window_ms(Watcher &watcher, std::uint32_t ms);             // [main]
// [any] 정규화 단위 테스트용: 원시 struct inotify_event 버퍼를 넣고 기대 이벤트 열과 비교하는 입구는
// R1-T04가 이 헤더에 정한다(공개 API 아님)
}
```

### 인터페이스 §5 nav (`src/nav/nav.hpp`)

```cpp
namespace filescope::nav {

// 1회성 결과 슬롯. 메인 스레드에서 정확히 한 번 호출된다(취소되어도 CANCELLED로 호출).
// 슬롯이 끊긴 sigc::trackable(사라진 위젯)에 묶여 있었다면 호출은 아무 일도 하지 않는다
using ListDirSlot = sigc::slot<void(core::Result<std::vector<core::Entry>>)>;
using CompleteSlot = sigc::slot<void(core::Result<std::vector<std::string>>)>;

// [main] NAV-001/013/014: 디렉터리 목록. GIO 스레드 풀에서 openat+fdopendir+fstatat(AT_SYMLINK_NOFOLLOW).
// 항목 개별 stat 실패는 건너뛰지 않고 type=OTHER, size=0으로 남긴다.
// dirs_only: 트리용. show_hidden=false면 '.' 시작 이름 제외. "." ".."는 항상 제외.
// 실패: 권한 → PERMISSION_DENIED, 취소 → CANCELLED. slot은 worker에서 만지지 않는다
void list_dir_async(const std::string &path, bool show_hidden, bool dirs_only,
                    const Glib::RefPtr<Gio::Cancellable> &cancellable, ListDirSlot done);

// NAV-002: 탭 하나의 이동 이력. 순수 메모리, 파일 접근 없음. [any] (한 객체는 한 스레드에서만)
class History {
  public:
    static constexpr std::size_t kMaxEntries = 100;
    explicit History(std::string initial_path);
    const std::string &current() const noexcept;
    // 현재와 같으면 무시. 앞으로 이력 삭제. 최대 kMaxEntries, 넘으면 가장 오래된 것 삭제
    void push(const std::string &path);
    bool can_back() const noexcept;
    bool can_forward() const noexcept;
    std::optional<std::string> back();    // 불가하면 nullopt
    std::optional<std::string> forward(); // 불가하면 nullopt
};

// [any] 상위 경로, 루트 "/"면 nullopt. 이력에 push하는 것은 호출자
std::optional<std::string> parent(const std::string &path);

// [any] NAV-003: "/home/u/p" → {"/", "/home", "/home/u", "/home/u/p"}
std::vector<std::string> breadcrumb(const std::string &path);

// [main] NAV-004: 입력 문자열의 dirname을 읽어 basename 접두사로 시작하는 디렉터리 후보.
// 결과는 절대 경로, 이름순, 최대 max_results
void complete_async(const std::string &input, bool show_hidden, std::size_t max_results,
                    const Glib::RefPtr<Gio::Cancellable> &cancellable, CompleteSlot done);

// [any] NAV-005: Glib::get_home_dir() (HOME 환경 변수 우선, 테스트에서 임시 경로)
std::string home();

// NAV-010: 즐겨찾기. $XDG_CONFIG_HOME/filescope/bookmarks.ini (Glib::KeyFile), 원자적 저장. [main]
class Bookmarks {
  public:
    // 파일 없음은 빈 목록, 파싱 실패는 Error (원본 파일 덮어쓰지 않음)
    static core::Result<Bookmarks> load();
    const std::vector<std::string> &list() const noexcept;
    core::Result<void> add(const std::string &path); // 중복이면 EXISTS
    bool remove(const std::string &path);            // 없으면 false
    core::Result<void> save() const;                 // Glib::file_set_contents (원자적)
};

} // namespace filescope::nav
```
- NAV-006(멀티 탭)은 ui가 탭마다 History 하나를 둔다. NAV 공통 계약 "경로 접근 실패 시 기존 탭 유지": ui는 list_dir가 성공한 뒤에만 push와 모델 교체를 한다.
- 구현 메모: giomm-2.4에는 GTask 래퍼가 없으므로 nav는 C API `g_task_run_in_thread`를 내부에서 써도 된다. 결과 슬롯은 메인 스레드에서만 복사·호출한다.

### 인터페이스 §6 dash (`src/dash/dash.hpp`)

DASH-001(세션 집계), DASH-002(변경량 1분봉), DASH-003(이벤트 스트림 보관), 헤더 지표(최근 1분 이벤트, 반영 지연 P95)의 데이터 모델. GTK·파일 I/O·스레드를 쓰지 않는 순수 C++ 모듈이며 화면은 ui가 그린다. 한 Dash는 한 스레드(R1은 main)에서만 쓴다. 시간은 모두 호출자가 넘긴 값(테스트에서 고정 가능)을 쓴다.

```cpp
namespace filescope::dash {

struct Candle {
    std::int64_t minute_start_us = 0; // 벽시계(g_get_real_time) 기준 분 시작, 로컬 시간대와 무관한 UTC 60초 경계
    bool has_events = false;   // 이 분에 거래량이 있었나. false인 분은 봉을 그리지 않는다(시안)
    bool level_known = false;  // 시가~종가가 정해졌나(감시 루트 총용량이 하나라도 미상이면 false)
    bool has_gap = false;      // 이 분에 OVERFLOW가 있었다 (R1 화면 표시는 하지 않음, 데이터만)
    std::uint64_t open = 0, high = 0, low = 0, close = 0; // 바이트. 모든 감시 루트 총용량의 합
    std::uint32_t creates = 0, modifies = 0, deletes = 0; // 거래량: 정규화 이벤트 수 (CREATE·합성 CREATE / MODIFY / DELETE)
};

struct Counts { // 세션 누계 (DASH-001). moves = RENAME+MOVE+MOVED_IN+MOVED_OUT
    std::uint64_t creates = 0, modifies = 0, deletes = 0, moves = 0, attribs = 0;
};

class Dash {
  public:
    static constexpr std::uint32_t kMinCapacityMinutes = 30;
    static constexpr std::size_t kLatencySamples = 1024;
    // capacity_minutes: 보관할 분 수, 30 미만이면 30으로 올림. R1 ui는 60을 넘긴다
    explicit Dash(std::uint32_t capacity_minutes);

    // 루트 총용량 기준값. 루트 ACTIVE·RESYNC 완료 시 WatchInfo 값으로 호출. nullopt면 미상 처리
    void set_root_total(core::WatchId id, std::optional<std::uint64_t> total_bytes,
                        std::int64_t now_us);
    void remove_root(core::WatchId id, std::int64_t now_us);
    // mon signal_events의 배치를 그대로 넘긴다. 각 이벤트의 observed_at_us로 분을 정하고
    // root_total로 해당 루트 수준을 갱신해 고가·저가를 이벤트 단위로 반영한다
    void add_events(const std::vector<core::EventPtr> &events);
    // 이벤트가 없어도 분이 넘어가게 한다. ui가 1초 주기 타이머로 호출
    void tick(std::int64_t now_us);

    // 최근 n개 분(오래된 것부터, 현재 진행 중인 분 포함)
    std::vector<Candle> candles(std::uint32_t n) const;
    Counts counts() const;
    // now_us 기준 최근 60초 이벤트 수 (헤더 지표)
    std::uint32_t events_last_minute(std::int64_t now_us) const;
    // 반영 지연 표본(ui가 모델 반영 직후 monotonic 차이를 ns로 넘김). 최근 1024개로 P95 계산
    void record_latency(std::int64_t latency_ns);
    std::optional<std::int64_t> latency_p95_ns() const; // 표본 없으면 nullopt
};

} // namespace filescope::dash
```

봉 규칙(PRODUCT.md "변경량 1분봉"):
- 시가 = 그 분이 시작될 때의 총용량(= 직전 분 종가, 첫 분은 기준값), 고가/저가 = 그 분 안에서 관찰한 총용량의 최댓값/최솟값(시가 포함), 종가 = 그 분 마지막 총용량.
- 총용량 = 감시 루트별 마지막 root_total의 합. 루트 추가·제거·RESYNC는 수준을 바꾸지만 거래량은 늘리지 않는다.
- 이벤트의 root_total이 미상이면 그 루트 수준을 미상으로 두고, 해당 분의 level_known=false. 거래량은 계속 센다.
- 이벤트 observed_at_us가 현재 분보다 과거(시계 역행)면 현재 분에 넣는다. 보관 분 수를 넘으면 가장 오래된 분을 버린다(링 버퍼).
- OVERFLOW가 있던 분은 has_gap=true. 거래량은 받은 이벤트만 센다(유실분은 알 수 없음).

### 인터페이스 §7 ui (`src/ui/ui.hpp`)

```cpp
namespace filescope::ui {

class MainWindow : public Gtk::ApplicationWindow {
  public:
    // [main] app이 activate에서 호출. 모듈 객체(mon::Watcher, job::JobQueue, dash::Dash)는 창이 만들고 소유한다.
    // inotify 초기화 실패는 오류가 아니다: 경고 띠로 표시하고 창은 연다. Error는 창 자체를 만들 수 없을 때만
    static core::Result<std::unique_ptr<MainWindow>> create(const Glib::RefPtr<Gtk::Application> &app);
    ~MainWindow() override;
    // [main] 종료 순서(DESIGN 스레드 §4) 수행. 창 소멸 전에 app이 호출. 두 번 불러도 된다
    void shutdown();
};

} // namespace filescope::ui
```
- app(`src/main.cpp`)은 `Gtk::Application::create(kAppId)`로 앱을 만들고, `std::unique_ptr<ui::MainWindow>`를 소유한다. `signal_shutdown`에서 `shutdown()` 뒤 창을 해제한다. 위젯 자식은 `Gtk::make_managed`로 만든다.
- 시각 스타일은 루트 DESIGN.md(승인)와 docs/design/prototype/main-window.html을 따른다. ui 내부 파일 구성은 UI 작업이 정한다.
- 루트 DESIGN.md "GTK3 구현 메모"의 C 이름은 gtkmm 대응으로 읽는다: `GtkTreeView`/`GtkListStore`/`GtkTreeStore` → `Gtk::TreeView`/`Gtk::ListStore`/`Gtk::TreeStore`, `GtkDrawingArea` + cairo → `Gtk::DrawingArea::on_draw(const Cairo::RefPtr<Cairo::Context>&)`, `g_signal_connect_after(..."draw"...)` → `signal_draw().connect(slot, /*after=*/true)`, `GtkStack`/`GtkStackSwitcher` → `Gtk::Stack`/`Gtk::StackSwitcher`, CSS는 `Gtk::CssProvider`. gtkmm 래퍼가 없는 것(libayatana-appindicator3, `gtk_tree_view_get_background_area` 같은 일부 함수)은 `gobj()`로 C API를 직접 부른다.
- 기존 `--smoke` 옵션(창 제목 출력 후 종료)은 유지한다.

## 이벤트 (R1)

정규화·병합 정책은 ADR-0003. 수치(시간창, 큐 크기)는 측정 전 가정이며 G8 결과로 조정한다(미결 E-1).

### 이벤트 §1 Event (`src/core/event.hpp`)

```cpp
namespace filescope::core {

enum class EventKind {
    CREATE,      // MON-001  IN_CREATE
    DELETE,      // MON-002  IN_DELETE
    MODIFY,      // MON-003  IN_MODIFY
    CLOSE_WRITE, // MON-006  IN_CLOSE_WRITE
    ATTRIB,      // MON-011  IN_ATTRIB
    MOVED_OUT,   // MON-008  짝 없는 IN_MOVED_FROM: 감시 범위 밖으로 나감
    MOVED_IN,    // MON-009  짝 없는 IN_MOVED_TO: 감시 범위 밖에서 들어옴
    RENAME,      // MON-010/028  짝 맞춘 이동, 부모 디렉터리 같음
    MOVE,        // MON-028  짝 맞춘 이동, 부모 디렉터리 다름
    DELETE_SELF, // MON-012  감시 루트 자체 삭제
    MOVE_SELF,   // MON-013  감시 루트 자체 이동
    UNMOUNT,     // MON-014
    OVERFLOW,    // MON-015/029  유실 구간 시작
    RESYNC_DONE, // MON-029  유실 구간 재탐색 완료
};

enum class OriginHint { UNKNOWN }; // MON-032(P2)에서 확장. PID 아님

struct EventStat { // 정규화 시점 lstat 결과 (watcher 스레드)
    std::uint64_t size = 0;
    std::int64_t mtime_ns = 0;
    std::uint32_t mode = 0;
};

struct Event {
    std::uint64_t seq = 0;              // Watcher 안에서 단조 증가, 1부터. 병합되면 첫 이벤트 값
    std::int64_t observed_at_us = 0;    // g_get_real_time(), 첫 원시 이벤트 수신 시각
    std::int64_t monotonic_ns = 0;      // CLOCK_MONOTONIC, 첫 원시 이벤트 수신 시각 (지연 측정용)
    std::int64_t last_monotonic_ns = 0; // 병합된 마지막 원시 이벤트 수신 시각. 병합 없으면 monotonic_ns
    std::uint32_t raw_mask = 0;         // 병합된 원시 mask의 OR. 합성 이벤트는 0
    EventKind kind = EventKind::CREATE;
    bool synthetic = false;             // 커널 이벤트가 아니라 동적 감시 등록 직후 스캔으로 만든 것 (C판 FS_EVENT_FLAG_SYNTHETIC)
    WatchId watch_id = 0;               // 감시 루트 ID. OVERFLOW/RESYNC_DONE은 0(전체) 또는 해당 루트
    std::string path;                   // 절대 원본 바이트 경로. OVERFLOW/RESYNC_DONE은 루트 경로 또는 빈 문자열(없음)
    std::string old_path;               // RENAME/MOVE만. 그 외 빈 문자열
    std::uint32_t cookie = 0;           // 짝 맞춘 이동과 MOVED_OUT/IN의 커널 cookie, 그 외 0
    bool is_dir = false;                // IN_ISDIR
    OriginHint origin_hint = OriginHint::UNKNOWN;
    std::uint64_t gap_id = 0;           // OVERFLOW/RESYNC_DONE의 유실 구간 ID(1부터), 그 외 0
    std::uint32_t merged_count = 1;     // 병합된 원시 이벤트 수, 최소 1
    std::optional<EventStat> stat;      // lstat 실패하거나 DELETE/MOVED_OUT이면 nullopt (C판 stat_valid=FALSE)
    // 크기 색인(§8) 기준 값. 계산할 수 없으면 nullopt (C판 *_known=FALSE)
    std::optional<std::int64_t> size_delta;  // 이 이벤트로 바뀐 루트 총용량(바이트, 직전 색인 값 대비). 병합되면 합계
    std::optional<std::uint64_t> root_total; // 이 이벤트 적용 직후 watch_id 루트의 총용량. 병합되면 마지막 값
};

using EventPtr = std::shared_ptr<const Event>;

} // namespace filescope::core
```

- 소유권: 이벤트는 만든 뒤 바뀌지 않는다. `core::make_event()`가 `std::shared_ptr<const Event>`로 감싸고, 참조 수는 `shared_ptr`의 원자 카운터가 관리한다(C판 ref/unref 대체). 신호가 넘긴 vector는 호출 동안만 유효하고, 이벤트를 보관하려면 `EventPtr`를 복사한다. 여러 스레드가 같은 Event를 읽는 것은 안전하다(const).
- mon은 병합할 때 이미 공유된 Event를 고치지 않는다. 배치를 확정하기 전까지는 `Event` 값(공유 전)으로 병합하고, 배치를 보낼 때 `make_event`로 감싼다.
- stat 값은 이벤트보다 늦게 잰 값일 수 있다. ui는 크기 변동 표시에만 쓰고 정확한 상태가 필요하면 다시 읽는다.
- `seq`는 빠짐이 있을 수 있다(병합·유실). 유실 여부는 gap 이벤트로만 판단한다.
- stat 값은 이벤트보다 늦게 잰 값일 수 있다. ui는 크기 변동 표시에만 쓰고 정확한 상태가 필요하면 다시 읽는다.
- `seq`는 빠짐이 있을 수 있다(병합·유실). 유실 여부는 gap 이벤트로만 판단한다.

### 이벤트 §2 원시 mask와 kind 대응

등록 mask: `IN_CREATE | IN_DELETE | IN_MODIFY | IN_CLOSE_WRITE | IN_ATTRIB | IN_MOVED_FROM | IN_MOVED_TO | IN_DELETE_SELF | IN_MOVE_SELF | IN_DONT_FOLLOW | IN_ONLYDIR | IN_EXCL_UNLINK`. 읽기 계열(IN_ACCESS, IN_OPEN, IN_CLOSE_NOWRITE, MON-004/005/007)은 P1이며 R1은 등록하지 않는다. IN_UNMOUNT, IN_Q_OVERFLOW, IN_IGNORED는 커널이 항상 보낸다.

| 원시 | 조건 | 결과 |
|---|---|---|
| IN_CREATE | - | CREATE. IN_ISDIR이고 루트가 RECURSIVE면 §5 동적 감시 |
| IN_DELETE | - | DELETE |
| IN_MODIFY | - | MODIFY (§4 병합) |
| IN_CLOSE_WRITE | - | CLOSE_WRITE |
| IN_ATTRIB | 하위 항목 | ATTRIB (§4 병합) |
| IN_ATTRIB | 감시 디렉터리 자신(name 없음) | 부모 watch가 같은 변경을 보내므로 버림. 루트 자신이면 ATTRIB(path=루트) |
| IN_MOVED_FROM | - | §3 이동 짝 맞추기 대기 |
| IN_MOVED_TO | 대기 중 같은 cookie 있음 | RENAME 또는 MOVE |
| IN_MOVED_TO | 짝 없음 | MOVED_IN (디렉터리면 §5 등록) |
| IN_DELETE_SELF | 루트 | DELETE_SELF, 루트 STOPPED(ROOT_DELETED) |
| IN_DELETE_SELF | 하위 | 이벤트 없음(부모가 DELETE 보냄), registry 정리 |
| IN_MOVE_SELF | 루트 | MOVE_SELF, 루트 STOPPED(ROOT_MOVED). 새 경로는 알 수 없으므로 따라가지 않는다 |
| IN_MOVE_SELF | 하위 | 이벤트 없음(부모의 MOVED_FROM/TO로 처리) |
| IN_UNMOUNT | 루트 | UNMOUNT, 루트 STOPPED(UNMOUNTED) |
| IN_UNMOUNT | 하위 | UNMOUNT(path=그 디렉터리), 해당 하위 트리 registry 정리 |
| IN_IGNORED | 우리가 rm_watch했거나 앞선 DELETE_SELF/UNMOUNT 있음 | 이벤트 없음, wd 정리 |
| IN_IGNORED | 그 외, 루트 | 루트 ERROR(LOST) |
| IN_IGNORED | 그 외, 하위 | wd 정리 후 그 하위 디렉터리를 다시 등록 시도. 실패하면 루트 DEGRADED(LOST) |
| IN_Q_OVERFLOW | wd=-1 | §6 유실 처리 |
| 모르는 wd | - | 버림(제거된 watch의 늦은 이벤트), 디버그 로그만 |

### 이벤트 §3 MOVE cookie 매칭 (MON-008/009/010/028)

- IN_MOVED_FROM은 (cookie, 원래 경로, is_dir, 수신 monotonic)으로 대기 표에 넣는다.
- 같은 cookie의 IN_MOVED_TO가 오면 대기 항목을 빼서 짝 이벤트 하나를 만든다. `dirname(old_path) == dirname(path)`면 RENAME, 아니면 MOVE. 짝 이벤트의 seq·시각은 MOVED_FROM 기준이다.
- 시간창: 수신 후 50ms(`FS_MON_MOVE_WINDOW_MS`, 테스트에서 변경 가능) 안에 짝이 오지 않으면 MOVED_OUT으로 확정한다. 커널은 같은 rename의 두 이벤트를 보통 같은 read 버퍼에 연속으로 넣으므로 시간창은 버퍼 경계에 걸린 경우를 위한 여유다.
- 대기 중인 MOVED_FROM 뒤에 같은 cookie가 아닌 이벤트가 와도 순서를 지키기 위해 그 뒤 이벤트는 전달 배치에 그대로 들어간다. 짝 이벤트는 MOVED_FROM이 있던 위치에 놓인다.
- 디렉터리 이동 후 registry 처리:
  - RENAME/MOVE(범위 안): 해당 하위 트리의 wd 경로 접두사를 새 경로로 바꾼다. 새 부모가 RECURSIVE 루트 밖(다른 루트 포함)이면 MOVED_OUT과 MOVED_IN 처리와 같게 본다.
  - MOVED_OUT: 하위 트리의 watch를 모두 `inotify_rm_watch`하고 registry에서 뺀다.
  - MOVED_IN: §5 등록과 스캔.
- 감시 정지·overflow가 나면 대기 표를 비운다. 대기 항목은 MOVED_OUT으로 내보내지 않는다(재탐색이 상태를 맞춘다).

### 이벤트 §4 병합 (MON-027)

- 단위는 전달 배치(§7) 하나다. 배치를 넘어서 병합하지 않는다.
- 같은 배치 안에서 같은 (kind, path)인 MODIFY끼리, ATTRIB끼리만 병합한다. 먼저 온 이벤트 자리에 남기고 `merged_count`, `last_monotonic_ns`, `raw_mask`(OR), stat 값(마지막 값), `size_delta`(합계, 하나라도 미상이면 미상), `root_total`(마지막 값)을 갱신한다. 병합은 같은 루트의 다른 이벤트를 건너뛰어 뒤 위치로 옮기지 않으므로, 배치 순서대로 root_total을 따라가면 루트 총용량의 실제 경로와 같다(병합된 중간 값만 사라진다).
- 그 사이에 같은 path의 다른 kind(DELETE, MOVE, RENAME, CREATE, CLOSE_WRITE) 이벤트가 끼면 병합하지 않는다. 즉 순서가 바뀌는 병합은 없다.
- CREATE, DELETE, CLOSE_WRITE, 이동 계열, SELF 계열, OVERFLOW, RESYNC_DONE은 병합하지 않는다.

### 이벤트 §5 재귀·동적 감시 (MON-018/019)

- RECURSIVE 루트 등록: 루트에서 깊이 우선으로 디렉터리를 `openat(O_DIRECTORY|O_NOFOLLOW)`로 걷고 각 디렉터리에 watch를 건다. 심볼릭 링크는 따라가지 않는다. 루트와 `st_dev`가 다른 디렉터리(마운트 경계)는 등록하지 않는다(ADR-0003). 등록은 watcher 스레드가 한다.
- 비재귀 루트는 루트 디렉터리 하나만 감시한다.
- 동적 등록: RECURSIVE 루트 안에서 디렉터리 CREATE를 받으면 그 디렉터리와 하위를 등록한 뒤 바로 내용을 스캔해, 등록 전에 생긴 항목마다 CREATE(`FS_EVENT_FLAG_SYNTHETIC`, size_delta는 그 항목 크기)를 만든다. 이후 같은 항목의 커널 CREATE가 중복으로 오면 그대로 전달한다(소비자는 CREATE를 멱등 처리한다). 중복 CREATE의 size_delta는 색인에 이미 있는 크기 대비라 0이 되어 총용량이 두 번 늘지 않는다.
- 디렉터리 MOVED_IN은 하위를 등록하고 색인에 넣지만 하위 항목별 합성 이벤트는 만들지 않는다. MOVED_IN 이벤트 하나의 size_delta가 하위 전체 크기 합이다.
- watch 수 한도: `inotify_add_watch`가 ENOSPC면 그 디렉터리 아래는 등록을 멈추고 루트를 DEGRADED(WATCH_LIMIT)로 알린다(상태 신호의 Error `WATCH_LIMIT`, message에 미등록 디렉터리 수). 다른 디렉터리 감시는 계속한다. EACCES 디렉터리는 건너뛰고 수만 기록한다(DEGRADED로 바꾸지 않음, 미결 E-3).
- `inotify_add_watch`가 이미 등록된 wd를 돌려주면(같은 inode, 예: bind mount) 새 경로를 등록하지 않는다.
- 겹치는 루트(같거나 조상·자손)는 add_root가 거부한다(미결 U-3).

### 이벤트 §6 유실·overflow (MON-015/029)

유실 원인은 둘이다: 커널 IN_Q_OVERFLOW, 전달 큐 포화(§7). 처리는 같다.
1. 새 gap_id를 만든다. 대기 중인 MOVE 표를 비운다.
2. OVERFLOW 이벤트(watch_id=0, path 없음, gap_id)를 내보낸다. 큐 포화로 이미 배치를 못 넣는 상태라면 이벤트를 버리는 대신 "유실 표시"만 세워 두고, 큐에 자리가 나면 가장 먼저 OVERFLOW를 넣는다. 유실을 조용히 넘기지 않는다.
3. 모든 ACTIVE 루트를 DEGRADED(OVERFLOW) → RESYNCING으로 알리고, 각 루트를 처음 등록처럼 다시 걷는다: 사라진 디렉터리의 watch 제거, 새 디렉터리 watch 추가. 재탐색 중 생긴 커널 이벤트는 그대로 전달한다.
4. 끝나면 RESYNC_DONE(gap_id 같음)을 내보내고 루트를 ACTIVE로 돌린다. 재탐색이 실패한 루트는 ERROR(IO).
5. mon은 파일 목록 스냅샷을 갖지 않는다. 소비자(ui)는 RESYNC_DONE을 받으면 열려 있는 감시 범위 디렉터리를 다시 읽어 모델과 맞춘다(NFR-013). 잃어버린 개별 이벤트는 복원 불가로 표시한다(R2 이력은 gap 구간을 기록).
6. 재탐색 중 다시 overflow가 나면 진행 중 재탐색을 멈추고 새 gap_id로 1부터 다시 한다.

### 이벤트 §7 전달 (MON-017)

- watcher 스레드는 정규화한 이벤트를 현재 배치에 모으고, 배치에 첫 이벤트가 들어온 뒤 16ms가 지나거나 256개가 차면 메인 컨텍스트로 보낸다(`Glib::Dispatcher`, 스레드 §2).
- 메인 스레드에 아직 전달되지 않은 이벤트 총수의 상한은 8192개다. 상한을 넘으면 그 배치를 버리고 §6 유실 처리를 한다.
- 메인 스레드는 배치 하나를 `signal_events` 한 번으로 넘긴다. 상태 변화는 그 배치보다 먼저 일어난 것이면 먼저 `signal_state`로 넘긴다(같은 큐로 순서 보장).
- 목표: 원시 이벤트 수신부터 signal_events 호출까지 P95 ≤ 50ms(NFR-002의 100ms 중 ui 몫 50ms를 남긴다). 측정 전 목표다.

### 이벤트 §8 크기 색인과 size_delta (변경량 1분봉, 목록 변동 열)

결정과 대안 비교는 ADR-0006. 메모리 예산은 DESIGN 메모리 §4.

- 색인: watcher 스레드가 감시 디렉터리 노드마다 `이름 → 크기` 표를 둔다(경로 전체를 키로 쓰지 않는다). 크기는 `lstat`의 `st_size`(논리 크기)이고 정규 파일만 더한다. 디렉터리·심볼릭 링크·특수 파일은 0이다. 루트 총용량은 색인 합계를 따로 들고 있어 조회가 O(1)이다.
- 범위: RECURSIVE 루트는 감시 중인 디렉터리 전체, 비재귀 루트는 루트 바로 아래 정규 파일만. 감시하지 못한 디렉터리(권한 없음, WATCH_LIMIT, 마운트 경계)는 빠지고 그 루트는 `total_partial=true`(WatchInfo)다.
- 구축: 루트 등록·재탐색 때 재귀 등록과 같은 순회에서 `fstatat(dirfd, name, AT_SYMLINK_NOFOLLOW)`로 채운다. 등록이 끝나 ACTIVE가 되기 전(REGISTERING)에는 루트 총용량이 미상이고 그 사이 이벤트의 size_delta·root_total도 미상이다.
- 하드 링크는 경로마다 따로 더한다(중복 집계, 미결 E-7). 희소 파일은 논리 크기.

| kind | size_delta 계산 | 색인 갱신 |
|---|---|---|
| CREATE (파일) | 새 lstat 크기 − 색인 크기(없으면 0) | 추가·갱신 |
| CREATE (디렉터리) | 0 (하위 항목은 합성 CREATE가 각자 운반) | 노드 추가 |
| MODIFY, CLOSE_WRITE, ATTRIB | 새 lstat 크기 − 색인 크기 | 갱신 |
| DELETE | −색인 크기 | 삭제 |
| MOVED_OUT | −하위 전체 합(파일이면 그 크기) | 하위 노드 삭제 |
| MOVED_IN | +하위 전체 합(스캔 결과) | 하위 노드 추가 |
| RENAME, MOVE (같은 루트 안) | 0 | 노드·이름만 옮김 |
| DELETE_SELF, MOVE_SELF, UNMOUNT(루트) | 미상 | 루트 색인 폐기 |
| UNMOUNT(하위) | −하위 합 | 하위 삭제 |
| OVERFLOW | 미상 | 재탐색에서 재구축 |
| RESYNC_DONE | 재구축 후 총용량 − 유실 직전 총용량 | 재구축 결과로 교체 |

- 미상이 되는 경우: lstat 실패(사이에 삭제됨 등)면서 색인에도 없음, 루트가 REGISTERING, 루트 색인 폐기 뒤. 미상인 이벤트도 색인은 할 수 있는 만큼 갱신한다(lstat 실패한 MODIFY는 색인 값을 유지).
- 순서 보장: size_delta와 root_total은 watcher 스레드가 이벤트를 정규화하는 순서대로 계산한다. 같은 루트 이벤트를 수신 순으로 따라가면 `root_total[n] = root_total[n-1] + size_delta[n]`이 성립한다(둘 다 known일 때). 테스트 불변식으로 쓴다.
- 소비자: ui 파일 목록 변동 열(직전 이벤트 대비 증감, PROGRESS 미결 사항 U-4)과 dash 1분봉(ARCHITECTURE §7)은 이 두 값만 쓰고 파일을 다시 stat하지 않는다.

### 이벤트 §9 자체 이벤트

R1은 앱이 직접 한 파일 작업의 이벤트도 외부 이벤트와 똑같이 전달한다(origin_hint=UNKNOWN). 구분은 MON-032(P2). 내부 읽기로 인한 피드백은 읽기 계열 mask를 등록하지 않으므로 생기지 않는다.

## 스레드 (R1)

### 스레드 §1 스레드와 책임

| 스레드 | 수 | 소유 데이터 | 하는 일 |
|---|---|---|---|
| main (GTK) | 1 | 위젯, 모델, nav::History, nav::Bookmarks, dash::Dash, mon::Watcher·job::JobQueue 객체 | UI, 모든 공개 신호·슬롯, 동기 파일 작업 |
| mon watcher | Watcher당 1 (`std::thread`) | inotify fd, wd registry, 크기 색인, MOVE 대기 표, 현재 배치 | poll, read, 정규화, 병합, 재귀 등록, 재탐색 |
| job worker | JobQueue당 1 (`std::thread`, 대기열은 `std::condition_variable`로 깨움) | 실행 중 작업의 본문 데이터 | JobBody 실행 |
| GIO 작업 스레드 | GLib 풀 | GTask 데이터 | nav 목록·자동완성 |

- GTK 위젯과 `Gtk::TreeModel`은 main에서만 만진다.
- 공개 신호·슬롯(이벤트, 감시 상태, 작업 상태·진행, nav 결과 슬롯)은 모두 main(기본 메인 컨텍스트)에서 호출된다. sigc++ 객체(signal, slot, connection)는 main에서만 만들고 복사하고 호출한다. 다른 스레드는 sigc++를 만지지 않는다.
- 모듈 객체는 만든 스레드(main)에서만 공개 함수를 부른다. 예외는 DESIGN 인터페이스에 `[any]`로 표시한 함수다.
- 모든 스레드 진입 함수(watcher 루프, worker 루프)는 예외를 잡는다. 새어 나온 예외는 해당 루트 ERROR(IO) 또는 작업 FAILED로 바꾸고 스레드를 끝내지 않는다. 메인 스레드 신호 처리기(ui)도 예외를 내보내지 않는다(glibmm은 처리기 예외를 `Glib::exception_handlers`로 보내고 계속 돌지만, 그 경로를 정상 흐름으로 쓰지 않는다).

### 스레드 §2 스레드 간 통신

| 방향 | 수단 |
|---|---|
| main → watcher | 명령 큐(`std::mutex` + `std::deque`: ADD_ROOT, REMOVE_ROOT, INJECT_OVERFLOW, STOP) + `eventfd` 깨우기. watcher는 `poll({inotify_fd, eventfd})`로 기다리며 시간창 만료까지 timeout을 둔다 |
| watcher → main | 배치·상태 변화를 담은 메시지를 전달 큐(`std::mutex` + `std::deque`)에 넣고 잠금 밖에서 `Glib::Dispatcher::emit()`. main의 dispatcher 처리기가 큐를 모두 꺼내 순서대로 신호를 보낸다. 미전달 이벤트 수는 `std::atomic` 카운터로 세고 상한 8192(DESIGN 이벤트 §7) |
| main → worker | 작업 레코드를 잠금 아래 대기열에 넣고 `std::condition_variable::notify_one` |
| worker → main | 상태·진행 메시지를 전달 큐에 넣고 `Glib::Dispatcher::emit()`. 진행은 작업당 최근 값 하나만 두고 10Hz로 덮어쓰기 |
| 취소 | `Glib::RefPtr<Gio::Cancellable>` (작업마다 하나, nav는 호출자가 넘긴 것) |

- `Glib::Dispatcher`는 객체(Watcher, JobQueue)마다 하나이며 main에서 만들고 main에서 소멸한다(glibmm 규칙). `emit()`만 다른 스레드에서 부른다.
- 상태와 이벤트 배치가 같은 전달 큐를 지나므로 순서가 유지된다(DESIGN 이벤트 §7).
- `Glib::MainContext::invoke`, `Glib::signal_idle()`/`signal_timeout()`은 다른 스레드에서 쓰지 않는다(sigc++ 슬롯을 다른 스레드에서 만들게 된다).
- registry와 MOVE 대기 표는 watcher 스레드만 만지므로 잠금이 없다. main의 조회 함수(`list_roots`, `usage`, `find_root`, `root_total`)는 watcher가 상태 메시지로 보낸 사본(main 소유)을 읽는다.

### 스레드 §3 잠금

| 잠금 (`std::mutex`) | 보호 대상 | 순서 |
|---|---|---|
| `JobQueue` 큐 잠금 | 대기열, 작업 표, 상태 값, 종료 플래그 | 1 |
| `Job` 진행 잠금 | 최근 진행 값, 항목 결과 vector | 2 (큐 잠금 안에서 잡을 수 있음, 반대는 금지) |
| `Watcher` 명령 큐 잠금 | 명령 deque | 다른 잠금과 같이 잡지 않음 |
| 전달 큐 잠금 (Watcher, JobQueue 각각) | 메인으로 갈 메시지 deque | 말단: 이 잠금을 쥔 채 다른 잠금을 잡지 않음 |

- 잠금은 `std::lock_guard`/`std::unique_lock`으로만 잡는다(수동 lock/unlock 금지).
- 잠금을 쥔 채 신호 호출, GTK 호출, 파일 I/O, `Glib::Dispatcher::emit()`을 하지 않는다.
- 새 잠금이 필요하면 이 표에 순서를 먼저 추가한다(Coordinator 승인).

### 스레드 §4 종료 순서

app의 `signal_shutdown`(Gio::Application) 또는 창 닫기에서 `ui::MainWindow::shutdown()`이 다음 순서로 수행한다.
1. ui가 새 사용자 명령을 막는다(작업 제출 버튼 비활성).
2. `mon::Watcher` 소멸(`std::unique_ptr` reset): STOP 명령 → 스레드 합류 → watch 해제 → inotify fd 닫기 → 전달 큐에 남은 메시지 폐기 → Dispatcher 소멸. 합류 뒤 Dispatcher를 없애므로 이미 emit된 알림이 있어도 처리기가 불리지 않는다.
3. `job::JobQueue::shutdown()`: 새 제출 거부 → 실행 중 작업 취소 → worker 합류(`std::thread::join`) → 임시 파일 정리는 작업 본문이 취소 경로에서 끝낸다 → 대기 작업 폐기 → 모든 본문을 main에서 소멸 → 전달 큐 폐기.
4. (R2) DB flush·닫기.
5. 위젯 해제.

- 콜백 수명: UI 처리기가 객체 수명 뒤에 실행되지 않도록 sigc++ 자동 연결 해제(ui 객체는 `sigc::trackable`), nav 비동기는 `Gio::Cancellable` 취소, mon·job은 "합류 후 Dispatcher 소멸"로 막는다. nav 결과 슬롯은 취소되어도 `CANCELLED`로 호출되며(대상 위젯이 이미 사라졌으면 끊긴 슬롯이라 아무 일도 없다), 호출자는 CANCELLED일 때 위젯을 만지지 않는다. 소멸자·shutdown 반환 후 신호는 없다.

트레이로 창 없이 감시를 계속하는 동작은 미결(U-1)이며, 정해지면 이 순서의 시작 시점만 바뀐다.

### 스레드 §5 검증

- G6(TSan): mon 통합 테스트(실제 이벤트 + overflow 주입 + 종료), job 테스트(진행 중 취소, 종료 중 제출)를 TSan 빌드에서 실행한다.
- 종료 경쟁(E10): 작업 실행 중·이벤트 폭주 중 소멸(shutdown) 후 신호가 오지 않음을 테스트로 확인한다.

## 상태 전이 (R1)

UI는 실제 상태 전이만 표시한다. 아래에 없는 전이는 버그다.

### 상태 §1 감시 루트 (mon::WatchState)

```
          add_root
             │
       REGISTERING ──등록 실패(루트 open/watch 실패)──► ERROR(IO)
             │ 루트 watch 성공
             ├── 하위 일부 ENOSPC ──► DEGRADED(WATCH_LIMIT)
             ▼
          ACTIVE ◄──────────── RESYNCING ──재탐색 실패──► ERROR(IO)
             │                    ▲
             │ overflow/큐 포화   │ 즉시
             └──► DEGRADED(OVERFLOW)

 모든 상태 ──remove_root──► STOPPED(USER)
 ACTIVE/DEGRADED/RESYNCING ──루트 DELETE_SELF──► STOPPED(ROOT_DELETED)
                          ──루트 MOVE_SELF──► STOPPED(ROOT_MOVED)
                          ──루트 IN_UNMOUNT──► STOPPED(UNMOUNTED)
                          ──예상 못한 루트 IN_IGNORED──► ERROR(LOST)
```

| 상태 | 의미 | UI 표시 (루트 DESIGN.md States) |
|---|---|---|
| REGISTERING | 재귀 등록 중 | 감시 표시는 아직 감시 안 함 |
| ACTIVE | 정상 | 감시 중 점 |
| DEGRADED(WATCH_LIMIT) | 일부 하위 미감시, 나머지 동작 | 경고 띠(문구·스타일은 디자인 결정, 미결 U-5) |
| DEGRADED(OVERFLOW) → RESYNCING | 유실 후 재탐색 중 | 재탐색 완료 시 조용한 overflow 띠 |
| STOPPED(ROOT_DELETED) | 루트 사라짐 | 반전 띠 + 목록 stale |
| STOPPED(USER) | 사용자 해제 | 감시 안 함 점 |
| ERROR | 감시 불가 | 반전 띠 |

- STOPPED와 ERROR는 끝 상태다. 다시 감시하려면 add_root로 새 ID를 받는다.
- DEGRADED(WATCH_LIMIT)는 자동 회복하지 않는다(한도가 늘었는지 알 수 없음). overflow 재탐색이 일어나면 그때 다시 시도하고 결과로 상태를 정한다.
- 일시 정지(PAUSED, MON-022)는 P2라 R1에 없다(미결 U-2).

### 상태 §2 작업 (JobState)

```
 submit ─► QUEUED ──worker 시작──► RUNNING ──본문 성공──► DONE
             │                      │ ──본문 Error(취소 아님)──► FAILED
             │ cancel               │ ──취소 요청 후 본문 반환──► CANCELLED
             └────► CANCELLED        └ (PAUSED ↔ RUNNING: JOB-006/007, P1, R1 미구현)
```

| 상태 | 끝 상태 | 상태 신호 error |
|---|---|---|
| QUEUED | 아니오 | nullopt |
| RUNNING | 아니오 | nullopt |
| PAUSED | 아니오 | R1에서는 나오지 않음 |
| DONE | 예 | nullopt. 단 항목 중 skipped가 있을 수 있음 |
| FAILED | 예 | 대표 오류. 일부 항목만 실패하면 `PARTIAL`, 전부 같은 원인이면 그 코드 |
| CANCELLED | 예 | `CANCELLED`. 이미 끝난 항목 결과는 `results()`로 볼 수 있다(JOB 공통 계약 "취소 시 부분 결과") |

- 판정 순서: 취소 요청이 있었고 본문이 CANCELLED Error를 돌려주면 CANCELLED. 본문이 성공을 돌려도 항목 결과에 실패가 있으면 FAILED(PARTIAL).
- 취소 요청이 RUNNING 중에 오면 상태 신호는 보내지 않고(취소 요청 중 표시는 ui 몫) 본문이 반환한 뒤 CANCELLED 하나만 보낸다.
- 치명 오류(NO_SPACE, CANCELLED, 원본 디렉터리 읽기 실패)는 남은 항목을 진행하지 않는다. 그 밖의 항목 오류는 기록하고 다음 항목으로 간다.

## 메모리 (R1)

함수별 소유권은 인터페이스 부분의 선언(값, 참조, `std::unique_ptr`, `EventPtr`)이 정본이다.

### 메모리 §1 할당과 RAII

- 자원은 모두 RAII 객체가 소유한다. 앱 코드(src/, tests/)에 `new`/`delete`, `malloc`/`free`, `g_malloc`/`g_free` 직접 호출이 없다. 단독 소유는 `std::make_unique`, 공유는 `std::make_shared`(이벤트만), 위젯 자식은 `Gtk::make_managed`(gtkmm 3.24).
- C API가 돌려준 메모리는 받는 즉시 소유 객체로 감싼다: `g_free` 대상은 `std::unique_ptr<char, decltype(&g_free)>` 또는 `Glib::convert_return_gchar_ptr_to_stdstring`, `GError*`는 `Glib::Error`(소유 인수) 후 `core::error_from_glib`, libc `realpath` 등은 `std::unique_ptr<char, decltype(&free)>`.
- GObject는 `Glib::RefPtr`(gtkmm 래퍼) 또는 C 객체를 `Glib::wrap(ptr, /*take_copy=*/false)`로 받는다. `g_object_unref` 직접 호출 없음.
- 메모리 부족(`std::bad_alloc`)은 잡지 않는다. GLib의 `g_malloc`처럼 프로그램을 끝낸다. 예외 금지 규칙(ENGINEERING 오류 §1)의 유일한 예외다.

### 메모리 §2 소유권 요약

| 대상 | 규칙 |
|---|---|
| 공개 함수 반환 (`std::string`, `std::vector`, 구조체, `Result<T>`) | 값으로 호출자에게 넘어감 |
| 팩터리 반환 (`Watcher::create`, `MainWindow::create`) | `std::unique_ptr`, 호출자 단독 소유 |
| 신호 인자 (`const std::vector<EventPtr>&`, `const Progress&`, `const std::optional<Error>&`, `const WatchInfo&`) | 빌려줌, 호출 동안만 유효. 보관하려면 복사 |
| Event | 불변, `std::shared_ptr<const Event>`. 전달 큐 메시지가 배치 vector를 소유하고 신호 반환 후 해제. 보관은 EventPtr 복사 |
| 작업 본문 (`JobBody`와 캡처) | submit 성공 시 큐가 소유, 끝난 뒤 main에서 소멸. submit 실패 시 submit 안에서 소멸 |
| 입력 경로 (`const std::string&`, `const std::vector<std::string>&`) | 호출 동안만 빌림. 비동기 작업은 필요한 만큼 복사해 둔다 |
| `core::Error` | 값 타입. 복사·이동 자유 |

### 메모리 §3 자원 해제 (오류 경로 포함)

- fd: `O_CLOEXEC`로 열고 받는 즉시 `core::UniqueFd`로 감싼다. 모든 반환 경로에서 소멸자가 닫는다.
- `DIR *`: `std::unique_ptr<DIR, decltype(&closedir)>`. `fdopendir`에 넘긴 fd는 DIR이 소유하므로 `UniqueFd::release()` 후 넘긴다.
- inotify: Watcher 소멸 시 wd를 모두 rm_watch할 필요는 없고 fd close로 정리되지만, registry 메모리는 모두 해제한다.
- 임시 파일: 복사·텍스트 저장 중 만든 `.filescope-*` 임시 파일은 실패·취소 경로에서 `unlinkat`한다(E06). 작은 RAII 가드(성공 시 dismiss)로 구현해도 된다.
- 전달 큐 메시지: 합류 후 Dispatcher와 함께 폐기되므로 객체 소멸 뒤 실행되는 메시지가 없다(스레드 §4).

### 메모리 §4 크기와 한도

- 크기 계산은 `__builtin_add_overflow`(또는 GLib `g_uint64_checked_add`)로 overflow를 확인한다.
- 고정 크기 경로 버퍼를 쓰지 않는다. inotify read 버퍼만 고정 크기(최소 `sizeof(struct inotify_event) + NAME_MAX + 1`, 권장 64KiB, `alignas(struct inotify_event)`)이며 이벤트 경계를 `len` 필드로 넘는다.
- 상한: 전달 큐 8192 이벤트, 이동 이력 100개, 텍스트 읽기 4MiB, 이벤트 로그 UI 500행(루트 DESIGN.md), dash 분 링 버퍼 60개(≥30), 지연 표본 1024개. job 대기열은 상한 없음(R1, 사용자 조작 단위라 작음; 미결 E-4).
- mon 크기 색인(DESIGN 이벤트 §8, ADR-0006) 예산: 정규 파일 10만 개(디렉터리 1만 개 가정) 기준 색인 + watch registry 합계 16MiB 이하. 산정: 파일 항목당 이름 할당(평균 이름 16B → 할당 단위 32B, `std::string` SSO는 15B까지라 대부분 힙) + 해시 슬롯(`std::unordered_map` 노드·버킷, 약 40B) + 크기 8B ≈ 80B → 8MiB, 디렉터리 노드(경로 조각, 표 머리, wd) 약 200B × 1만 ≈ 2MiB, 여유 6MiB. 경로 전체 문자열은 저장하지 않는다(필요할 때 노드 부모를 따라 조립). NFR-009(기본 RSS 150MiB)에 포함된다.
- 검증: R1-T04 G8에서 10만 파일 fixture를 재귀 등록하기 전후 RSS 차이를 기록한다(측정 전 PASS 금지). 예산 초과는 FAIL이 아니라 Coordinator 보고 후 E-1과 함께 조정.

### 메모리 §5 검증

G5: `make asan ubsan`(누수 포함, `scripts/lsan.supp`는 GLib/GTK/glibmm/gtkmm 전역 할당만 억제). Valgrind는 선택.

## 데이터 모델

R1 메모리 모델은 확정 제안(ARCH-001), 영속 모델은 **R2에서 확정**(아래 DB 스키마).

### R1 (메모리)
| 모델 | 정의 위치 | 비고 |
|---|---|---|
| core::Entry | DESIGN 인터페이스 §1 core/types.hpp | name(원본 바이트), type, target_type, size, mode, uid, gid, dev, ino, mtime_ns, is_mount_point, is_hidden. dev/inode는 영구 ID가 아니며 재사용될 수 있다 |
| core::Event | DESIGN 이벤트 §1 | 불변, `std::shared_ptr<const Event>` |
| Job | DESIGN 인터페이스 §2 | id, state(DESIGN 상태 §2), 진행(bytes/items), 항목 결과 |
| Watch 루트 | DESIGN 인터페이스 §4 mon::WatchInfo | id, path, mode, state, reason, watch_count, total_bytes·file_count·total_known·total_partial. wd·generation·크기 색인은 mon 내부 |
| 1분봉·세션 집계 | DESIGN 인터페이스 §6 dash::Candle, dash::Counts | dash 메모리 링 버퍼(60분). 영속 저장 없음 |
| 이동 이력 | nav::History | 탭당, 최대 100 |
| 즐겨찾기 | nav::Bookmarks | 유일한 R1 영속 데이터(Glib::KeyFile) |

화면 표시 문자열(display_name)은 모델에 두지 않고 ui가 그릴 때 만든다.

### R2 (영속, 초안)
Snapshot: id, path, hash, size, captured_at, completeness. 이벤트·작업 이력 저장. 변경 내용 패널과 되돌리기는 이 모델에 의존한다.

## DB 스키마 (SQLite)

상태: **R2에서 확정.** R1(탐색·파일 작업·감시·작업 큐)은 DB를 쓰지 않는다(ARCH-001 결정). R1의 영속 데이터는 즐겨찾기 파일 하나(`$XDG_CONFIG_HOME/filescope/bookmarks.ini`, DESIGN 인터페이스 §5)뿐이다.

의존 관계: PRODUCT.md의 "변경 내용 표시"(변경 내용 패널: 텍스트 줄 Diff, 이미지 전후 비교, 문서 블록 변경)와 되돌리기는 직전 버전 보관(HIS-001/005)이 전제다. 따라서 변경 내용 패널은 R2 이력과 이 스키마가 확정된 뒤에 구현한다. R1 UI는 패널 자리를 만들지 않거나 "R2 예정" 상태로 둔다(미결 U-1).

### R2 초안 (확정 아님)
테이블 events(seq PK, observed_at, kind, path BLOB, old_path BLOB, raw_mask, gap_id), snapshots(id PK, path BLOB, hash, size, captured_at, completeness), jobs(id PK, state, kind, error), settings(key PK,value), watches(id PK,path BLOB), file_index(path BLOB PK,mtime,size). events 시간/경로 인덱스. 스냅샷 본문은 별도 content-addressed 저장소. migration 버전 관리, prepared statement, WAL, 단일 writer, bounded busy retry. DB와 blob 간 장애 복구 및 orphan 정리 테스트 필수.

R2 확정 시 반영할 R1 계약: events 열은 DESIGN 이벤트 §1 Event 필드(seq, observed_at_us, kind, path, old_path, raw_mask, gap_id, merged_count)를 따른다. gap 구간(OVERFLOW~RESYNC_DONE)은 "복원 불가" 표시의 근거로 저장한다.
