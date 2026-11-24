# 데이터 모델
FileEntry: raw_path, display_name, dev, inode, size, mode, uid, gid, mtime. dev/inode는 영구 ID가 아니며 재사용 가능. Event: EVENT_SPEC 참조. Job: id, kind, state, bytes_done, total, error. Snapshot: id, path, hash, size, captured_at, completeness. Watch: id, path, wd, generation, status.
