"""R2 history behavior through the public GTK and AT-SPI interfaces."""
import hashlib
import os
from pathlib import Path
import struct
import sys
import time
import zlib

from interaction import click, close, descendants, find, key, named, row, row_index, run, showing, wait
from gi.repository import Atspi


def label_has(app, fragment):
    return next((node for node in descendants(app)
                 if node.get_role() == Atspi.Role.LABEL and showing(node)
                 and fragment in node.get_name()), None)


def label(app, fragment, timeout=8):
    return wait(f"history text contains {fragment!r}", lambda: label_has(app, fragment), timeout)


def select_file(app, name):
    files = find(app, "파일 목록")
    index, = wait(f"history fixture {name}",
                  lambda: (i,) if (i := row_index(files, name)) is not None else None)
    files.get_selection_iface().clear_selection()
    assert files.get_table_iface().add_row_selection(index), name
    return files


def log_delete(app, path):
    log = find(app, "이벤트 로그 목록")

    def delete_cell():
        table = log.get_table_iface()
        for i in range(table.get_n_rows()):
            if (table.get_accessible_at(i, 1).get_name() == "✕ DELETE"
                    and table.get_accessible_at(i, 2).get_name() == str(path)):
                return table.get_accessible_at(i, 2)
        return None

    click(wait(f"DELETE row for {path.name}", delete_cell))


def focused(node):
    return node.get_state_set().contains(Atspi.StateType.FOCUSED)


def diff_text(app):
    view = named(app, "통합 줄 Diff")
    return Atspi.Text.get_text(view, 0, -1) if view else ""


def capture(name):
    destination = os.environ.get("FILESCOPE_SCREENSHOT_DIR")
    if not destination:
        return
    import gi
    gi.require_version("Gdk", "3.0")
    from gi.repository import Gdk
    root = Gdk.get_default_root_window()
    image = Gdk.pixbuf_get_from_window(root, 0, 0, root.get_width(), root.get_height())
    Path(destination).mkdir(parents=True, exist_ok=True)
    image.savev(str(Path(destination) / f"history-{name}.png"), "png", [], [])


def png(path, changed=False):
    def chunk(kind, value):
        return (struct.pack(">I", len(value)) + kind + value
                + struct.pack(">I", zlib.crc32(kind + value)))

    width, height = 96, 48
    rows = bytearray()
    for y in range(height):
        rows.append(0)
        for x in range(width):
            edit = changed and (x < 16 or x >= 80) and y < 16
            rows.extend((240, 50, 30) if edit else (60, 120, 160))
    path.write_bytes(b"\x89PNG\r\n\x1a\n"
                     + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


def exercise(app, home):
    note = home / "history-notes.md"
    select_file(app, note.name)
    label(app, "새 파일 · 이전 버전 없음 · +5 줄 추가")
    wait("baseline lines marked as additions", lambda: "+ alpha" in diff_text(app))
    assert "+ omit-two" in diff_text(app)
    capture("new")

    note.write_text("alpha\nkeep\nbeta\nnew line\n")
    label(app, "+1 줄 추가", 2)
    label(app, "−2 줄 삭제", 2)
    label(app, "v1 → v2")
    text = wait("unified Diff rendered", lambda: diff_text(app))
    assert "    2       − omit-one" in text and "    4 + new line" in text, text
    capture("text")

    note.write_text("alpha\nkeep\nbeta\nnew line\nthird\n")
    label(app, "v2 → v3")
    click(find(app, "전체 이력"))
    versions = find(app, "전체 이력 버전 목록")
    assert row(versions, "v3 현재")
    capture("versions")
    click(row(versions, "v1"))
    key("Return")
    label(app, "v1 → v3")
    before = note.read_bytes()
    click(find(app, "v1으로 되돌리기"))
    wait("cancel focused by default", lambda: focused(find(app, "취소")))
    capture("confirmation")
    key("Escape")
    wait("Escape dismisses confirmation", lambda: not named(app, "되돌리기 확정"))
    assert note.read_bytes() == before
    click(find(app, "v1으로 되돌리기"))
    click(find(app, "되돌리기 확정"))
    wait("restore writes chosen version", lambda: b"omit-two" in note.read_bytes())
    label(app, "파일을 되돌렸습니다")
    label(app, "v3 → v4")

    picture = home / "history.png"
    select_file(app, picture.name)
    label(app, "96 × 48")
    png(picture, True)
    label(app, "바뀐 영역 2곳")
    diagram = find(app, "이미지 비교")
    assert diagram.get_description() == "바뀐 영역 점선 상자 2개"
    capture("image-pair")
    overlay = find(app, "겹쳐 밀기")
    assert overlay.get_component_iface().grab_focus()
    wait("overlay mode has keyboard focus", lambda: focused(overlay))
    key("space")
    slider = find(app, "겹쳐 밀기 위치")
    assert slider.get_component_iface().grab_focus()
    before = Atspi.Value.get_current_value(slider)
    key("Right")
    wait("Right moves overlay", lambda: Atspi.Value.get_current_value(slider) > before)
    key("Left")
    wait("Left moves overlay", lambda: Atspi.Value.get_current_value(slider) == before)
    capture("image-overlay")

    binary = home / "history.docx"
    select_file(app, binary.name)
    label(app, "바이너리 변경")
    binary.write_bytes(b"PK\x03\x04\x00new contents")
    label(app, hashlib.sha256(binary.read_bytes()).hexdigest())
    label(app, "바이너리 변경")
    label(app, "▲ +")
    assert named(app, "통합 줄 Diff") is None
    capture("binary")

    note.unlink()
    log_delete(app, note)
    label(app, "✕ 삭제")
    restore = find(app, "v4 복원").get_name()
    capture("deleted")
    click(find(app, "전체 이력"))
    versions = find(app, "전체 이력 버전 목록")
    click(row(versions, "v1"))
    key("Return")
    label(app, "v1 → v4")
    label(app, "내용이 같습니다")
    note.write_text("existing")
    click(find(app, restore))
    label(app, "같은 이름의 파일이 이미 있습니다")
    assert note.read_text() == "existing"
    note.unlink()
    log_delete(app, note)
    click(find(app, "v4 복원"))
    wait("recover recreates removed file", lambda: note.exists() and b"omit-two" in note.read_bytes())
    label(app, "파일을 복원했습니다")
    assert wait("recovered row in file list", lambda: row(find(app, "파일 목록"), note.name))

    transient = home / "history-transient"
    transient.symlink_to(note.name)
    transient.unlink()
    log_delete(app, transient)
    label(app, "복구할 버전이 없습니다")
    assert named(app, "v1 복원") is None
    capture("no-version")

    directory = home / "history-dir"
    select_file(app, directory.name)
    label(app, "이 세션에서 하위 변경 없음")
    child = directory / "child.txt"
    child.write_text("child content\n")
    changes = find(app, "폴더 하위 변경")
    wait("folder uses session event rows", lambda: row(changes, str(child), 2))

    slow = home / "history-slow.txt"
    select_file(app, slow.name)
    label(app, "+50000 줄 추가")
    slow.write_text("new line\n" * 50000)
    label(app, "v1 → v2")
    if label_has(app, "비교하는 중…"):
        capture("loading")
    start = time.monotonic()
    files = select_file(app, picture.name)
    assert files.get_component_iface().grab_focus()
    key("Down")
    wait("file list keeps keyboard focus during comparison", lambda: focused(files), 2)
    select_file(app, binary.name)
    label(app, "바이너리 변경", 2)
    assert time.monotonic() - start < 2
    assert named(app, "통합 줄 Diff") is None

    toggle = find(app, "변경 내용 패널")
    assert toggle.get_component_iface().grab_focus()
    key("space")
    wait("keyboard hides panel", lambda: not named(app, "변경 내용"))
    close(app)
    print("PASS history text, versions, restore/recover, image, binary, folder and responsiveness",
          flush=True)


def run_history(binary, home):
    (home / "history-notes.md").write_text("alpha\nomit-one\nkeep\nomit-two\nbeta\n")
    (home / "history.docx").write_bytes(b"PK\x03\x04\x00old")
    (home / "history-dir").mkdir()
    (home / "history-slow.txt").write_text("old line\n" * 50000)
    png(home / "history.png")
    run(binary, lambda app: exercise(app, home))

    gtk_config = Path(os.environ["XDG_CONFIG_HOME"]) / "gtk-3.0"
    gtk_config.mkdir()
    (gtk_config / "settings.ini").write_text("[Settings]\ngtk-enable-animations=false\n")

    def restart(app):
        toggle = find(app, "변경 내용 패널")
        assert not toggle.get_state_set().contains(Atspi.StateType.CHECKED)
        assert not named(app, "변경 내용")
        click(toggle)
        find(app, "변경 내용")
        select_file(app, "history.png")
        label(app, "최신 버전 뒤에 감시하지 못한 구간이 있습니다")
        slow = home / "history-slow.txt"
        select_file(app, slow.name)
        label(app, "v1 → v2")
        log = find(app, "이벤트 로그 목록").get_table_iface()
        closing = home / "history-closing.txt"
        closing.write_text("capture at shutdown\n")

        def capture_queued():
            # The file emits both MODIFY and CLOSE_WRITE with this display label.
            return sum(log.get_accessible_at(i, 1).get_name() == "▲ MODIFY"
                       and log.get_accessible_at(i, 2).get_name() == str(closing)
                       for i in range(log.get_n_rows())) >= 2

        wait("capture event submitted before close", capture_queued, 2)
        close(app)

    run(binary, restart)
    print("PASS history panel persistence and close during comparison/capture", flush=True)

    assert os.geteuid() != 0, "mode 000 verification requires an unprivileged user"
    data = Path(os.environ["XDG_DATA_HOME"]) / "filescope"
    data.chmod(0)

    def disabled(app):
        label(app, "이력 꺼짐")
        select_file(app, "history-notes.md")
        label(app, "보관된 버전 없음")
        label(app, str(data))
        marker = home / "history-disabled-marker.txt"
        marker.write_text("still watching")
        wait("R1 monitoring continues with history disabled",
             lambda: row(find(app, "파일 목록"), marker.name))
        capture("disabled")
        close(app)

    try:
        run(binary, disabled)
    finally:
        data.chmod(0o700)
    print("PASS history open failure keeps window and R1 watch working", flush=True)


if __name__ == "__main__":
    run_history(str(Path(sys.argv[1]).resolve()), Path(os.environ["HOME"]))
