# 아키텍처
Modular Monolith + Event-Driven + Layered. 단일 프로세스, GTK 메인 루프, inotify 전용 수신기, bounded queue, 작업 풀, SQLite 단일 writer. UI → application → module public API → infrastructure. 이벤트 버스는 관측 결과 전달용이며 명령 실행의 필수 경로가 아니다. CMake target 단위로 경계를 검사한다. GTK3/GLib/GIO, SQLite WAL, libarchive, Cairo, GtkSourceView4, VTE, Poppler, libgit2는 단계별 도입. 외부 서비스 불필요.
