"""Drive the installed GTK app with X11 input and observe it through AT-SPI."""
import ctypes as ct
import os
from pathlib import Path
import signal
import subprocess
import sys
import time

import gi

gi.require_version("Atspi", "2.0")
from gi.repository import Atspi, GLib

X11 = ct.CDLL("libX11.so.6")
Xtst = ct.CDLL("libXtst.so.6")
X11.XOpenDisplay.argtypes = [ct.c_char_p]
X11.XOpenDisplay.restype = ct.c_void_p
X11.XStringToKeysym.argtypes = [ct.c_char_p]
X11.XStringToKeysym.restype = ct.c_ulong
X11.XKeysymToKeycode.argtypes = [ct.c_void_p, ct.c_ulong]
X11.XKeysymToKeycode.restype = ct.c_ubyte
X11.XSync.argtypes = [ct.c_void_p, ct.c_int]
Xtst.XTestFakeKeyEvent.argtypes = [ct.c_void_p, ct.c_uint, ct.c_int, ct.c_ulong]
Xtst.XTestFakeButtonEvent.argtypes = Xtst.XTestFakeKeyEvent.argtypes
Xtst.XTestFakeMotionEvent.argtypes = [ct.c_void_p, ct.c_int, ct.c_int, ct.c_int, ct.c_ulong]
display = X11.XOpenDisplay(None)
assert display, "Xvfb display is unavailable"
JOB_ITEMS = 16384


def key(symbol, *modifiers):
    codes = [X11.XKeysymToKeycode(display, X11.XStringToKeysym(name.encode()))
             for name in (*modifiers, symbol)]
    assert all(codes), (symbol, modifiers)
    for code in codes:
        Xtst.XTestFakeKeyEvent(display, code, True, 0)
    for code in reversed(codes):
        Xtst.XTestFakeKeyEvent(display, code, False, 0)
    X11.XSync(display, False)


def click(node):
    bounds = node.get_component_iface().get_extents(Atspi.CoordType.SCREEN)
    assert bounds.width > 0 and bounds.height > 0, node.get_name()
    Xtst.XTestFakeMotionEvent(display, -1, bounds.x + bounds.width // 2,
                             bounds.y + bounds.height // 2, 0)
    Xtst.XTestFakeButtonEvent(display, 1, True, 0)
    Xtst.XTestFakeButtonEvent(display, 1, False, 0)
    X11.XSync(display, False)


def wait(message, predicate, timeout=8):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            result = predicate()
            if result:
                return result
        except GLib.Error:
            # The accessible can disappear while GTK replaces a view or dialog.
            pass
        time.sleep(0.05)
    raise AssertionError(message)


def showing(node):
    return node.get_state_set().contains(Atspi.StateType.SHOWING)


def descendants(node):
    for i in range(node.get_child_count()):
        child = node.get_child_at_index(i)
        if child is not None:
            yield child
            # The table interface looks up rows directly.
            if child.get_table_iface() is None:
                yield from descendants(child)


def named(root, name, role=None):
    return next((node for node in descendants(root)
                 if node.get_name() == name and showing(node)
                 and (role is None or node.get_role() == role)), None)


def find(root, name, role=None):
    return wait(f"visible {name!r}", lambda: named(root, name, role))


def row_index(table, value, column=0):
    model = table.get_table_iface()
    for i in range(model.get_n_rows()):
        cell = model.get_accessible_at(i, column)
        if cell is not None and cell.get_name() == value:
            return i
    return None


def row(table, value, column=0):
    index = row_index(table, value, column)
    return None if index is None else table.get_table_iface().get_accessible_at(index, column)


def replace_text(node, value):
    assert node.get_editable_text_iface().set_text_contents(str(value))


def exercise(app, home, fixture):
    address = find(app, "주소 입력 및 폴더 자동완성")
    breadcrumb = find(app, "현재 경로")

    def path_is(path):
        try:
            wait(f"address becomes {path}",
                 lambda: Atspi.Text.get_text(address, 0, -1) == str(path)
                 and named(breadcrumb, path.name, Atspi.Role.PUSH_BUTTON))
        except AssertionError as error:
            raise AssertionError(f"{error}, actual {Atspi.Text.get_text(address, 0, -1)}") from error

    def navigate(path):
        click(address)
        wait("address takes keyboard focus",
             lambda: address.get_state_set().contains(Atspi.StateType.FOCUSED))
        replace_text(address, path)
        key("Return")
        path_is(path)
        key("Escape")

    def files():
        return find(app, "파일 목록")

    def file(name):
        return wait(f"file row {name}", lambda: row(files(), name))

    def button(name):
        return find(app, name, Atspi.Role.PUSH_BUTTON)

    def focus(node):
        assert node.get_component_iface().grab_focus()

    def select(name):
        table = files()
        cell = file(name)
        table.get_selection_iface().clear_selection()
        assert table.get_table_iface().add_row_selection(row_index(table, name)), name
        focus(table)
        assert cell.get_state_set().contains(Atspi.StateType.SELECTED), name

    first = home / "first"
    second = home / "second"
    navigate(first)
    navigate(second)
    key("Left", "Alt_L")
    path_is(first)
    key("Right", "Alt_L")
    path_is(second)
    key("Up", "Alt_L")
    path_is(home)
    navigate(first)
    click(button(home.name))
    path_is(home)
    print("PASS Alt navigation and breadcrumb", flush=True)

    navigate(first)
    key("t", "Control_L")
    wait("second tab appears", lambda: sum(node.get_role() == Atspi.Role.PAGE_TAB
                                          for node in descendants(app)) == 2)
    navigate(second)
    click(button("뒤로"))
    path_is(first)
    click(button("앞으로"))
    path_is(second)
    click(find(app, first.name, Atspi.Role.PAGE_TAB))
    path_is(first)
    click(button(home.name))
    path_is(home)
    click(find(app, second.name, Atspi.Role.PAGE_TAB))
    path_is(second)
    print("PASS independent tab histories", flush=True)

    assert row(files(), ".hidden") is None
    key("h", "Control_L")
    file(".hidden")
    key("h", "Control_L")
    wait("hidden row disappears", lambda: row(files(), ".hidden") is None)
    focus(files())
    key("d", "Control_L")
    find(app, f"☆  {second}")
    print("PASS hidden toggle and favorite creation", flush=True)

    select("rename.txt")
    key("F2")
    dialog = find(app, "이름 변경")
    entry = wait("rename entry", lambda: next((n for n in descendants(dialog)
                                                if n.get_editable_text_iface()), None))
    replace_text(entry, "renamed.txt")
    click(button("확인"))
    wait("rename on disk", lambda: (second / "renamed.txt").is_file())
    assert not (second / "rename.txt").exists()
    select("renamed.txt")
    key("Delete")
    wait("trash contains renamed.txt",
         lambda: (fixture / "data/Trash/files/renamed.txt").is_file())
    assert not (second / "renamed.txt").exists()

    select("permanent.txt")
    key("Delete", "Shift_L")
    find(app, "영구 삭제하시겠습니까?")
    click(button("취소"))
    assert (second / "permanent.txt").is_file()
    select("permanent.txt")
    key("Delete", "Shift_L")
    click(button("영구 삭제"))
    wait("confirmed permanent deletion", lambda: not (second / "permanent.txt").exists())
    print("PASS rename, Trash, permanent-delete cancellation and confirmation", flush=True)

    navigate(first)
    table = files()
    model = table.get_table_iface()
    for name in ("copy.txt", "copy2.txt"):
        assert model.add_row_selection(row_index(table, name)), name
    assert sum(model.is_row_selected(i) for i in range(model.get_n_rows())) == 2
    focus(table)
    key("c", "Control_L")
    navigate(second)
    focus(files())
    key("v", "Control_L")
    find(app, "같은 이름의 항목")
    button("건너뛰기")
    button("덮어쓰기")
    button("둘 다 유지")
    click(button("건너뛰기"))
    wait("second selected file copied", lambda: (second / "copy2.txt").is_file())
    assert (second / "copy.txt").read_text() == "existing"
    assert (first / "copy2.txt").is_file()

    navigate(first)
    select("move.txt")
    key("x", "Control_L")
    navigate(second)
    focus(files())
    key("v", "Control_L")
    wait("cut/paste moves file", lambda: (second / "move.txt").is_file()
         and not (first / "move.txt").exists())
    print("PASS multiselection, copy/skip conflict and cut/paste", flush=True)

    select("move.txt")
    key("e", "Control_L")
    editor = find(app, "파일 내용")
    (second / "move.txt").write_text("external update")
    replace_text(editor, "editor update")
    click(button("저장"))
    find(app, "⚠ 파일이 바뀌었거나 저장할 수 없습니다")
    assert (second / "move.txt").read_text() == "external update"
    print("PASS external edit conflict", flush=True)

    navigate(fixture / "job-origin")
    select("job-source")
    key("c", "Control_L")
    job_target = fixture / "job-target"
    job_target.mkdir()
    (job_target / "existing").touch()
    navigate(job_target)
    focus(files())
    key("v", "Control_L")
    progress = wait("copy progress bar appears", lambda: next(
        (n for n in descendants(app) if n.get_role() == Atspi.Role.PROGRESS_BAR
         and showing(n)), None))
    cancel = find(progress.get_parent(), "취소", Atspi.Role.PUSH_BUTTON)
    assert cancel.get_action_iface().do_action(0)
    wait("copy progress bar hides after cancellation", lambda: not showing(progress))
    assert sum(1 for _ in (job_target / "job-source").glob("*")) < JOB_ITEMS
    assert sum(1 for _ in (fixture / "job-origin/job-source").iterdir()) == JOB_ITEMS
    print("PASS copy progress and cancellation", flush=True)

    navigate(second)
    log = find(app, "이벤트 로그 목록")
    log_model = log.get_table_iface()
    log_filter = find(app, "경로 거르기")
    created = second / "filter-target"
    created.touch()
    wait("create logged", lambda: row(log, str(created), 2))
    first_minute = int(time.time()) // 60
    log_filter.get_editable_text_iface().set_text_contents(created.name)
    wait("only the target path remains", lambda: log_model.get_n_rows() > 0
         and all(log_model.get_accessible_at(i, 2).get_name() == str(created)
                 for i in range(log_model.get_n_rows())))
    click(find(app, "속성", Atspi.Role.TOGGLE_BUTTON))
    wait("attribute chip excludes creation", lambda: log_model.get_n_rows() == 0)
    click(find(app, "생성·삭제", Atspi.Role.TOGGLE_BUTTON))
    click(wait("target CREATE row", lambda: row(log, str(created), 2)))
    wait("log row selects corresponding file", lambda: file(created.name).get_state_set()
         .contains(Atspi.StateType.SELECTED))
    replace_text(log_filter, "")
    click(find(app, "전체", Atspi.Role.TOGGLE_BUTTON))
    for i in range(520):
        (second / f"burst-{i:03d}").touch()
    wait("log reaches its 500-row cap", lambda: log_model.get_n_rows() == 500
         and row(log, str(second / "burst-519"), 2))
    print("PASS log path/chip filters, file selection and cap", flush=True)

    # Put events in distinct real minutes so both arrow directions must move selection.
    next_minute = (first_minute + 1) * 60
    while time.time() < next_minute:
        time.sleep(min(0.5, max(0, next_minute - time.time())))
    next_event = second / "next-minute"
    next_event.touch()
    wait("second minute change reaches the log", lambda: row(log, str(next_event), 2))
    click(find(app, "변경량 · 1분봉"))
    chart = find(app, "감시 폴더 총용량 1분봉 차트")
    focus(chart)
    fields = (", 시 ", ", 고 ", ", 저 ", ", 종 ", ", 생성 ", ", 수정 ", ", 삭제 ")

    def candle_time():
        description = chart.get_description()
        return description.split(",", 1)[0] if all(part in description for part in fields) else ""

    key("Right")
    latest = wait("latest candle has full accessible description", candle_time)
    key("Left")
    wait("Left selects an earlier candle", lambda: candle_time() and candle_time() != latest)
    key("Right")
    wait("Right returns to the latest candle", lambda: candle_time() == latest)
    key("Escape")
    wait("Escape clears description", lambda: not chart.get_description())
    print("PASS candle keyboard and accessible description", flush=True)

    # An unwatched sibling keeps root removal independent from the home fixture.
    watched = fixture / "watch-me"
    watched.mkdir()
    first_file, second_file = watched / "alive", watched / "alternate"
    first_file.touch()
    second_file.touch()
    navigate(watched)
    find(app, "이 폴더를 감시하지 않습니다")
    assert button("감시 시작").get_action_iface().do_action(0)
    wait("watch-start banner clears", lambda: named(app, "감시 시작") is None)
    click(find(app, "이벤트 로그"))
    log = find(app, "이벤트 로그 목록")
    first_file.chmod(0o600)
    wait("new watch delivers an event", lambda: row(log, str(first_file), 2))

    # Alternate inodes while the app is stopped so inotify cannot coalesce the events.
    capacity = int(Path("/proc/sys/fs/inotify/max_queued_events").read_text())
    pid = app.get_process_id()
    os.kill(pid, signal.SIGSTOP)
    try:
        _, status = os.waitpid(pid, os.WUNTRACED)
        assert os.WIFSTOPPED(status), "FileScope did not stop"
        for i in range(capacity // 2 + 512):
            mode = 0o600 if i % 2 else 0o644
            first_file.chmod(mode)
            second_file.chmod(mode)
    finally:
        os.kill(pid, signal.SIGCONT)
    wait("RESYNC_DONE banner", lambda: next(
        (n for n in descendants(app) if n.get_role() == Atspi.Role.LABEL
         and n.get_name().startswith("대기열이 넘쳐 다시 검사해 맞췄습니다") and showing(n)),
        None), timeout=30)
    print("PASS RESYNC_DONE banner after real inotify overflow", flush=True)

    second_file.unlink()
    first_file.unlink()
    watched.rmdir()
    find(app, "⚠ 감시 폴더가 삭제되었습니다")
    button("상위 폴더로 이동")
    print("PASS watch-start and root-deletion bands", flush=True)

    close(app)


def close(app):
    frame = find(app, "FileScope", Atspi.Role.FRAME)
    control = wait("window close control", lambda: next(
        (n for n in descendants(frame) if showing(n)
         and n.get_role() == Atspi.Role.PUSH_BUTTON and n.get_name() in ("Close", "닫기")), None))
    click(control)


def run(binary, scenario):
    with subprocess.Popen([binary]) as process:
        try:
            Atspi.init()
            desktop = Atspi.get_desktop(0)
            app = wait("FileScope accessible application", lambda: next(
                (n for n in descendants(desktop) if n.get_role() == Atspi.Role.APPLICATION
                 and n.get_process_id() == process.pid), None))
            scenario(app)
            assert process.wait(timeout=8) == 0
        finally:
            if process.poll() is None:
                process.kill()


def main():
    binary = str(Path(sys.argv[1]).resolve())
    fixture = Path(sys.argv[2])
    home = fixture / "home"
    assert home == Path(os.environ["HOME"]), "run through tests/ui/run.sh"
    first = home / "first"
    second = home / "second"
    first.mkdir()
    second.mkdir()
    for name in ("copy.txt", "copy2.txt", "move.txt"):
        (first / name).write_text("source")
    for name in (".hidden", "rename.txt", "permanent.txt", "copy.txt"):
        (second / name).write_text("existing")
    job_source = fixture / "job-origin/job-source"
    job_source.mkdir(parents=True)
    for i in range(JOB_ITEMS):
        (job_source / f"item-{i:04d}").touch()

    run(binary, lambda app: exercise(app, home, fixture))

    def restart(app):
        click(find(app, f"☆  {second}"))
        address = find(app, "주소 입력 및 폴더 자동완성")
        wait("persisted favorite opens second",
             lambda: Atspi.Text.get_text(address, 0, -1) == str(second))
        close(app)

    run(binary, restart)
    print("PASS favorite persists and window close exits", flush=True)


if __name__ == "__main__":
    main()
