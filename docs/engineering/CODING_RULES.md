# C17 코딩 규칙
snake_case 함수·변수, 모듈 접두사, 헤더 include guard, 내부 함수 static. 공개 API는 입력·출력·오류·소유권·스레드 제약을 명시한다. malloc/free와 GLib 할당 계열을 일관되게 사용한다. 모든 syscall 반환값 확인, 부분 read/write와 EINTR 처리. 경로 길이 고정 버퍼 가정 금지. shell 문자열 조합 대신 argv 기반 실행. 사용자 파일을 실행하여 미리보기하지 않는다. GTK 접근은 메인 스레드만 허용한다. 모듈 내부 헤더를 다른 모듈에서 직접 참조하지 않는다.
