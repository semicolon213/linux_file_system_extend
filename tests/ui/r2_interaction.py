"""Search, home trash and the text editor through GTK's accessible interface."""
from contextlib import contextmanager
import ctypes as ct
import os
from pathlib import Path
import sys

from history_interaction import capture, focused, label, select_file
from interaction import X11, click, close, display, find, key, named, replace_text, row, run, wait
from gi.repository import Atspi


@contextmanager
def focus_transient():
    # Xvfb runs without a WM to transfer X input focus to a transient dialog.
    window = ct.c_ulong
    window_ptr = ct.POINTER(window)
    X11.XDefaultRootWindow.argtypes = [ct.c_void_p]
    X11.XDefaultRootWindow.restype = window
    X11.XQueryTree.argtypes = [ct.c_void_p, window, window_ptr, window_ptr,
                             ct.POINTER(window_ptr), ct.POINTER(ct.c_uint)]
    X11.XGetTransientForHint.argtypes = [ct.c_void_p, window, window_ptr]
    X11.XSetInputFocus.argtypes = [ct.c_void_p, window, ct.c_int, ct.c_ulong]
    X11.XFree.argtypes = [ct.c_void_p]
    root = X11.XDefaultRootWindow(display)
    children, count, parent, ignored = window_ptr(), ct.c_uint(), window(), window()
    assert X11.XQueryTree(display, root, ct.byref(ignored), ct.byref(parent),
                          ct.byref(children), ct.byref(count))
    try:
        dialogs = [(child, parent.value) for child in children[:count.value]
                   if X11.XGetTransientForHint(display, child, ct.byref(parent))]
    finally:
        X11.XFree(children)
    assert dialogs, "no X11 transient window"
    dialog, parent = dialogs[-1]  # XQueryTree orders windows from bottom to top.
    X11.XSetInputFocus(display, dialog, 2, 0)  # RevertToParent, CurrentTime
    X11.XSync(display, False)
    try:
        yield
    finally:
        X11.XSetInputFocus(display, parent, 2, 0)
        X11.XSync(display, False)


def navigate(app, path):
    key("l", "Control_L")
    address = find(app, "주소 입력 및 폴더 자동완성")
    replace_text(address, path)
    key("Return")
    find(find(app, "현재 경로"), path.name, Atspi.Role.PUSH_BUTTON)


def exercise_search(app, root):
    navigate(app, root / "empty")
    label(app, "이 폴더는 비어 있습니다")
    navigate(app, root)
    click(find(app, "경로 표시줄 편집"))
    address = find(app, "주소 입력 및 폴더 자동완성")
    assert Atspi.Text.get_text(address, 0, -1) == str(root)
    key("Escape")
    wait("Esc closes address mode", lambda: not named(app, "주소 입력 및 폴더 자동완성"))
    find(find(app, "현재 경로"), root.name, Atspi.Role.PUSH_BUTTON)

    key("f", "Control_L")
    search = find(app, "이 폴더에서 검색")
    wait("Ctrl+F focuses folder search", lambda: focused(search))
    replace_text(search, "notes")
    label(app, "3개 찾음 · 폴더")
    label(app, "접근 불가 1개")
    results = find(app, "검색 결과")
    assert results.get_table_iface().get_n_rows() == 3
    assert row(results, "docs", 1)
    assert row(results, "notes.txt")
    capture("r2-search")
    click(find(app, ".md 2"))
    label(app, "2개 찾음 · 폴더")
    assert results.get_table_iface().get_n_rows() == 2
    assert not row(results, "notes.txt")
    click(find(app, "거르기 해제"))
    label(app, "3개 찾음 · 폴더")
    click(row(results, "notes.md"))
    key("Return")
    find(find(app, "현재 경로"), "docs", Atspi.Role.PUSH_BUTTON)
    files = find(app, "파일 목록")
    selected = wait("result item selected in its folder", lambda: row(files, "notes.md"))
    wait("search result Enter selects notes.md", lambda:
         selected.get_state_set().contains(Atspi.StateType.SELECTED))

    replace_text(find(app, "이 폴더에서 검색"), "no-such-name")
    label(app, "검색 결과가 없습니다")
    label(app, "건너뛴 폴더 0개")
    capture("r2-search-empty")
    key("Escape")
    find(app, "파일 목록")
    assert not named(app, "검색 결과")
    print("PASS recursive search, denied count, extension filter, Enter, empty and Escape")


def exercise_trash(app, root):
    navigate(app, root)
    trash = Path(os.environ["XDG_DATA_HOME"]) / "Trash" / "files"
    click(find(app, "휴지통 0"))
    label(app, "휴지통이 비어 있습니다")
    assert not find(app, "휴지통 비우기…").get_state_set().contains(Atspi.StateType.SENSITIVE)
    key("Escape")
    first = root / "restore-me.txt"
    second = root / "delete-me.txt"
    for item in (first, second):
        files = select_file(app, item.name)
        assert files.get_component_iface().grab_focus()
        key("Delete")
        wait(f"{item.name} in the system trash", lambda: (trash / item.name).exists())
    click(find(app, "휴지통 2"))
    table = find(app, "휴지통 항목")
    label(app, "2개 ·")
    assert row(table, first.name, 1)
    assert row(table, str(root), 2)
    assert table.get_table_iface().get_accessible_at(0, 3).get_name() != "—"
    assert table.get_table_iface().get_accessible_at(0, 4).get_name() != "—"
    capture("r2-trash")

    click(row(table, first.name, 1))
    label(app, "1개 선택")
    find(app, "원래 위치로 복원")
    first.write_text("external occupant")
    click(find(app, "복원"))
    label(app, "원래 위치에 같은 이름의 항목")
    find(app, "원래 폴더 열기")
    assert first.read_text() == "external occupant"
    assert (trash / first.name).read_text() == "first"
    capture("r2-trash-conflict")
    first.unlink()
    click(row(table, first.name, 1))
    click(find(app, "원래 위치로 복원"))
    wait("restored bytes at original location", lambda: first.exists() and first.read_text() == "first")
    find(app, "휴지통 1")
    wait("restored row removed", lambda: not row(table, first.name, 1))

    click(row(table, second.name, 1))
    click(find(app, "영구 삭제…"))
    label(app, "삭제하면 되돌릴 수 없습니다")
    with focus_transient():
        wait("Cancel focused on permanent deletion", lambda: focused(find(app, "취소")))
        key("Escape")
        wait("confirmation dismissed", lambda: not named(app, "1개 영구 삭제"))
    assert (trash / second.name).read_text() == "second"
    click(find(app, "영구 삭제…"))
    click(find(app, "1개 영구 삭제"))
    label(app, "휴지통이 비어 있습니다")
    assert not (trash / second.name).exists()

    key("Escape")
    for name in ("empty-1.txt", "empty-2.txt"):
        files = select_file(app, name)
        assert files.get_component_iface().grab_focus()
        key("Delete")
        wait(f"{name} in trash", lambda: (trash / name).exists())
    click(find(app, "휴지통 2"))
    click(find(app, "휴지통 비우기…"))
    find(app, "2개 영구 삭제")
    with focus_transient():
        wait("Cancel focused on empty trash", lambda: focused(find(app, "취소")))
        capture("r2-trash-confirm")
        key("Return")
        wait("empty confirmation dismissed", lambda: not named(app, "2개 영구 삭제"))
    assert len(list(trash.iterdir())) == 2
    click(find(app, "휴지통 비우기…"))
    click(find(app, "2개 영구 삭제"))
    label(app, "휴지통이 비어 있습니다")
    find(app, "휴지통 0")
    assert not list(trash.iterdir())
    capture("r2-trash-empty")
    key("Escape")
    print("PASS system trash, counts, restore conflict, restore, cancel, delete and empty")


def exercise_editor(app, root):
    note = root / "editing.md"
    select_file(app, note.name)
    label(app, "새 파일 · 이전 버전 없음")
    click(find(app, "미리보기"))
    label(app, "읽기 전용 · UTF-8")
    editor = find(app, "파일 내용")
    assert Atspi.Text.get_text(editor, 0, -1) == "baseline\n"
    assert not editor.get_state_set().contains(Atspi.StateType.EDITABLE)
    capture("r2-preview")
    key("F4")
    wait("F4 enables editor", lambda: editor.get_state_set().contains(Atspi.StateType.EDITABLE))
    replace_text(editor, "saved\n")
    key("s", "Control_L")
    wait("save writes content", lambda: note.read_text() == "saved\n")
    label(app, "읽기 전용 · UTF-8")
    click(find(app, "변경 내용", Atspi.Role.PUSH_BUTTON))
    label(app, "v1 → v2")

    files = select_file(app, note.name)
    assert files.get_component_iface().grab_focus()
    key("e", "Control_L")
    editor = find(app, "파일 내용")
    wait("R1 edit shortcut remains", lambda: editor.get_state_set().contains(Atspi.StateType.EDITABLE))
    replace_text(editor, "local draft\n")
    note.write_text("external\n")
    key("s", "Control_L")
    label(app, "편집하는 동안 다른 프로그램이 이 파일을 바꿨습니다")
    wait("conflict defaults to Cancel", lambda: focused(find(app, "취소")))
    assert note.read_text() == "external\n"
    capture("r2-editor-conflict")
    key("Escape")
    wait("Escape cancels conflict", lambda: not named(app, "다시 읽기"))
    assert note.read_text() == "external\n"
    assert Atspi.Text.get_text(editor, 0, -1) == "local draft\n"
    click(find(app, "저장 (Ctrl+S)"))
    click(find(app, "다시 읽기"))
    label(app, "읽기 전용 · UTF-8")
    assert Atspi.Text.get_text(editor, 0, -1) == "external\n"

    click(find(app, "편집 (F4)"))
    wait("F4 button enables editor", lambda: editor.get_state_set().contains(Atspi.StateType.EDITABLE))
    replace_text(editor, "confirmed overwrite\n")
    assert Atspi.Text.get_text(editor, 0, -1) == "confirmed overwrite\n"
    note.write_text("new disk content\n")
    click(find(app, "저장 (Ctrl+S)"))
    click(find(app, "덮어쓰기…"))
    assert note.read_text() == "new disk content\n"
    wait("second confirmation focuses Back", lambda: focused(find(app, "돌아가기")))
    click(find(app, "돌아가기"))
    assert note.read_text() == "new disk content\n"
    click(find(app, "덮어쓰기…"))
    confirm = find(app, "덮어쓰기 확정")
    capture("r2-editor-overwrite")
    click(confirm)
    try:
        wait("explicit overwrite now changes disk", lambda: note.read_text() == "confirmed overwrite\n")
    except AssertionError:
        capture("r2-editor-overwrite-failed")
        print(repr(note.read_text()), repr(Atspi.Text.get_text(editor, 0, -1)), flush=True)
        raise
    label(app, "읽기 전용 · UTF-8")

    for name, notice in (("large.txt", "미리보기 한도(4 MiB)"),
                         ("nul.txt", "텍스트로 열 수 없음"),
                         ("invalid.txt", "텍스트로 열 수 없음")):
        select_file(app, name)
        click(find(app, "미리보기"))
        label(app, notice)
        assert not named(app, "파일 내용")
        find(app, "기본 프로그램으로 열기")
    capture("r2-preview-invalid")
    print("PASS read-only UTF-8, invalid/large files, save/history, conflict cancel, reload and explicit overwrite")


def main():
    binary = str(Path(sys.argv[1]).resolve())
    home = Path(sys.argv[2]) / "home"
    assert home == Path(os.environ["HOME"]), "run through tests/ui/run.sh"
    root = home / "a"
    (root / "docs").mkdir(parents=True)
    (root / "empty").mkdir()
    denied = root / "locked"
    denied.mkdir()
    (denied / "notes-denied.txt").touch()
    assert os.getuid() != 0, "permission-denied fixture requires non-root"
    denied.chmod(0)
    try:
        (root / "docs/notes.md").write_text("docs")
        (root / "notes-root.md").write_text("root")
        (root / "notes.txt").write_text("text")
        for name, contents in (("restore-me.txt", "first"), ("delete-me.txt", "second"),
                               ("empty-1.txt", "one"), ("empty-2.txt", "two"),
                               ("editing.md", "baseline\n")):
            (root / name).write_text(contents)
        (root / "nul.txt").write_bytes(b"a\x00b")
        (root / "invalid.txt").write_bytes(b"\xff")
        with (root / "large.txt").open("wb") as large:
            large.truncate(4 * 1024 * 1024 + 1)

        def scenario(app):
            exercise_search(app, root)
            exercise_trash(app, root)
            exercise_editor(app, root)
            close(app)

        run(binary, scenario)
    finally:
        denied.chmod(0o700)


if __name__ == "__main__":
    main()
