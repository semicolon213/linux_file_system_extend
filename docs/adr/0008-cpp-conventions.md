# ADR-0008 C++ 관용 규칙: 오류, 콜백, 스레드 전달, 배치, 테스트

상태: 승인 범위 제안 (2026-11-24, Architect, ADR-0007 후속). ADR-0002(오류 모델)의 표현 방식(GError)을 대체한다. 오류 코드 이름과 의미는 그대로다. ADR-0004의 헤더 이름 규칙(`fs_<m>.h`)을 `<m>/<m>.hpp`로 바꾼다.

## 결정

1. 오류: `filescope::core::Error { ErrorCode code; std::string message; }`와 `core::Result<T>`(값 또는 Error), `core::Status = Result<void>`. `ErrorCode`는 기존 `FsErrorCode` 값을 같은 순서·의미로 옮긴 `enum class`다. 모듈 경계와 스레드 경계를 넘는 예외는 없다. glibmm/giomm이 던지는 `Glib::Error`는 그 호출을 감싼 함수 안에서 잡아 `core::error_from_glib()`로 바꾼다. 작업 본문·watcher 스레드에서 새어 나온 예외는 스레드 진입 함수가 잡는다.
2. 콜백: 메인 스레드에서 호출되는 공개 콜백은 모두 sigc++(`sigc::signal`, 1회성 비동기 결과는 `sigc::slot`)다. 다른 스레드에서 실행되는 함수(작업 본문)는 `std::function`이다.
3. 스레드 → 메인 전달: 객체마다 `Glib::Dispatcher` 하나와 잠금 아래 메시지 큐(`std::deque`) 하나. 생산 스레드는 큐에 넣고 잠금 밖에서 `emit()`, 메인은 핸들러에서 큐를 모두 비운다. 큐가 하나라서 상태 변화와 이벤트 배치의 순서가 유지된다.
4. 소유권: RAII. 앱 코드에 `new`/`delete`, `malloc`/`free`가 없다. 단독 소유 `std::unique_ptr`, 불변 공유 이벤트 `std::shared_ptr<const core::Event>`(`core::EventPtr`), GObject는 `Glib::RefPtr`, 위젯 자식은 `Gtk::make_managed`. fd는 `core::UniqueFd`.
5. 경로: 원본 바이트 경로·이름은 `std::string`(UTF-8 보장 없음). 화면 문자열은 `Glib::ustring`이며 ui가 `Glib::filename_display_name()`으로 만든다. 공개 API는 경로를 `Glib::ustring`으로 받지 않는다.
6. 배치: 디렉터리는 `src/<모듈>/` 그대로(`include/`로 옮기지 않음). 공개 헤더 `src/<m>/<m>.hpp`(core는 `error.hpp`, `path.hpp`, `types.hpp`, `event.hpp`, `fd.hpp`), 내부 헤더 `*_internal.hpp`, 테스트 전용 헤더 `*_testing.hpp`, 소스 `.cpp`. 이름 공간 `filescope::core|job|fs|mon|nav|dash|ui`.
7. 테스트: GLib `g_test`를 C++에서 그대로 쓴다. 테스트 파일은 `.cpp`, 경로 이름·라벨 규칙은 그대로다.

## 근거와 대안
- 오류 `std::error_code`: 코드만 담고 진단 메시지(`"<op>: <경로>: <strerror>"`)를 담지 못해 ADR-0002의 "errno 원문을 message에 남긴다"를 잃는다. 기각.
- 오류를 예외로: 작업 본문(worker)과 watcher 스레드 경계, sigc++ 신호 경계에서 예외가 새면 프로그램이 끝나고, 실패가 정상 흐름인 파일 작업(EXISTS, PERMISSION_DENIED)을 예외로 다루면 호출부가 try 투성이가 된다. 기각.
- `Glib::Error`를 값으로 사용: 예외 클래스를 값으로 쓰는 모양이 어색하고 message가 `Glib::ustring`이라 UTF-8을 강제한다. 기각. C++17에는 `std::expected`가 없으므로 `Result<T>`는 `std::variant` 위의 작은 클래스로 core에 둔다.
- 콜백을 전부 `std::function`: gtkmm 위젯이 `sigc::trackable`이라 sigc++ 슬롯은 위젯이 사라지면 자동으로 끊긴다. 이 성질로 "객체 해제 뒤 UI 콜백 금지" 규칙(DESIGN 스레드 §4)을 대부분 공짜로 얻는다. 단 sigc++는 스레드 안전하지 않으므로 다른 스레드에서 호출되는 함수에는 쓰지 않는다.
- 전달에 `Glib::MainContext::invoke`·`Glib::signal_idle()`: glibmm 문서상 다른 스레드에서 sigc++ 슬롯을 연결·복사하는 것은 안전하지 않다. `Glib::Dispatcher::emit()`은 다른 스레드에서 불러도 되는 glibmm의 공식 수단이다.
- `include/`로 공개 헤더 분리: 설치·배포할 라이브러리가 아니라서 이득이 없고 ADR-0004의 모듈 디렉터리 소유권(작업별 허용 파일)이 둘로 쪼개진다. 기각.
- GoogleTest·Catch2: 새 의존성이 필요하고 기존 테스트 구조(경로 이름, 라벨, `tests/support`)를 다시 짜야 한다. `g_test`는 C++에서 문제없이 동작한다. 기각.

## 결과
- 옛 C 이름과 새 C++ 이름 대응은 docs/DESIGN.md 인터페이스 §0 표가 정본이다. ADR 본문과 harness 기록의 옛 이름(`fs_mon_testing.h`, `fs_event_new_internal` 등)은 그 표로 읽는다.
- `scripts/check-architecture.sh`는 `.cpp`/`.hpp`와 gtkmm include(`gtkmm`, `gdkmm`)를 검사하도록 R0-002에서 바뀐다.
