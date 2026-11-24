# 상세 설계
파일 작업은 application command → job scheduler → filesystem adapter → 결과 이벤트 순서. watcher는 커널 이벤트 → normalizer → dispatcher → UI/history/index 순서. UI는 모델 갱신을 batch 처리한다. 초기 scan과 watch 등록 사이 변경은 재검증한다. 모듈별 내부 설계는 공개 계약 확정 후 작성한다.
