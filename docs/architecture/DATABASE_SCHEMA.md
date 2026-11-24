# SQLite 설계

상태: **R2에서 확정.** R1(탐색·파일 작업·감시·작업 큐)은 DB를 쓰지 않는다(ARCH-001 결정). R1의 영속 데이터는 즐겨찾기 파일 하나(`$XDG_CONFIG_HOME/filescope/bookmarks.ini`, INTERFACE_SPEC §5)뿐이다.

의존 관계: PRODUCT.md의 "변경 내용 표시"(변경 내용 패널: 텍스트 줄 Diff, 이미지 전후 비교, 문서 블록 변경)와 되돌리기는 직전 버전 보관(HIS-001/005)이 전제다. 따라서 변경 내용 패널은 R2 이력과 이 스키마가 확정된 뒤에 구현한다. R1 UI는 패널 자리를 만들지 않거나 "R2 예정" 상태로 둔다(미결 U-1).

## R2 초안 (확정 아님)
테이블 events(seq PK, observed_at, kind, path BLOB, old_path BLOB, raw_mask, gap_id), snapshots(id PK, path BLOB, hash, size, captured_at, completeness), jobs(id PK, state, kind, error), settings(key PK,value), watches(id PK,path BLOB), file_index(path BLOB PK,mtime,size). events 시간/경로 인덱스. 스냅샷 본문은 별도 content-addressed 저장소. migration 버전 관리, prepared statement, WAL, 단일 writer, bounded busy retry. DB와 blob 간 장애 복구 및 orphan 정리 테스트 필수.

R2 확정 시 반영할 R1 계약: events 열은 EVENT_SPEC.md §1 FsEvent 필드(seq, observed_at_us, kind, path, old_path, raw_mask, gap_id, merged_count)를 따른다. gap 구간(OVERFLOW~RESYNC_DONE)은 "복원 불가" 표시의 근거로 저장한다.
