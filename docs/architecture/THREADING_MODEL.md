# 스레드 모델
GTK main thread만 위젯 접근. watcher는 nonblocking inotify와 종료 신호를 poll; dispatcher는 bounded queue 소비; GThreadPool은 I/O; DB writer는 별도 연결 소유. UI 전달은 g_main_context_invoke 또는 idle source. 종료 순서: 신규 작업 차단 → watcher 종료 → worker 취소/join → DB flush → UI 해제. 잠금 중 콜백 호출 금지.
