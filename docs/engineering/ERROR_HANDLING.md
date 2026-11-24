# 오류 처리
GError 또는 모듈 오류 enum을 공개 API마다 통일. errno 즉시 저장. 사용자 메시지와 진단 로그 분리. EACCES, ENOSPC, ENOENT, EXDEV, EIO, 취소를 구분. 부분 성공은 개별 파일 결과를 남긴다. 자동 재시도는 멱등 작업만 제한적으로 수행.
