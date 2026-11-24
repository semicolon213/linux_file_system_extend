# 데이터 모델

상태: R1 메모리 모델은 확정 제안(ARCH-001), 영속 모델은 **R2에서 확정**(DATABASE_SCHEMA.md).

## R1 (메모리)
| 모델 | 정의 위치 | 비고 |
|---|---|---|
| FsEntry | INTERFACE_SPEC §1 fs_types.h | name(원본 바이트), type, target_type, size, mode, uid, gid, dev, ino, mtime_ns, is_mount_point, is_hidden. dev/inode는 영구 ID가 아니며 재사용될 수 있다 |
| FsEvent | EVENT_SPEC §1 | 불변, refcount |
| Job | INTERFACE_SPEC §2 | id, state(STATE_MACHINE §2), 진행(bytes/items), 항목 결과 |
| Watch 루트 | INTERFACE_SPEC §4 FsWatchInfo | id, path, flags, state, reason, watch_count. wd·generation은 mon 내부 |
| 이동 이력 | FsNavHistory | 탭당, 최대 100 |
| 즐겨찾기 | FsNavBookmarks | 유일한 R1 영속 데이터(GKeyFile) |

화면 표시 문자열(display_name)은 모델에 두지 않고 ui가 그릴 때 만든다.

## R2 (영속, 초안)
Snapshot: id, path, hash, size, captured_at, completeness. 이벤트·작업 이력 저장. 변경 내용 패널과 되돌리기는 이 모델에 의존한다.
