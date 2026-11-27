"""Drive the installed GTK app with X11 input and observe it through AT-SPI."""
import ctypes as ct
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
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
JOB_ITEMS = 20000


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
    breadcrumb = find(app, "현재 경로")

    def path_is(path):
        wait(f"breadcrumb shows {path}",
             lambda: named(breadcrumb, path.name, Atspi.Role.PUSH_BUTTON))

    def navigate(path):
        key("l", "Control_L")
        address = find(app, "주소 입력 및 폴더 자동완성")
        wait("address takes keyboard focus",
             lambda: address.get_state_set().contains(Atspi.StateType.FOCUSED))
        replace_text(address, path)
        key("Return")
        path_is(path)
        assert not named(app, "주소 입력 및 폴더 자동완성")

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
    key("Return")
    wait("Enter cancels the conflict dialog", lambda: not named(app, "같은 이름의 항목"))
    assert not (second / "copy2.txt").exists()
    key("v", "Control_L")
    find(app, "같은 이름의 항목")
    button("건너뛰기")
    button("덮어쓰기")
    button("둘 다 유지")
    click(button("건너뛰기"))
    wait("second selected file copied", lambda: (second / "copy2.txt").is_file())
    assert (second / "copy.txt").read_text() == "existing"
    assert (first / "copy2.txt").is_file()

    for confirm in (False, True):
        focus(files())
        key("v", "Control_L")
        find(app, "같은 이름의 항목")
        click(button("덮어쓰기"))
        find(app, "2개 항목을 덮어씁니다.")
        find(app, "덮어쓴 파일은 감시 중일 때만 이력에 남습니다")
        cancel = button("취소")
        assert cancel.get_state_set().contains(Atspi.StateType.IS_DEFAULT)
        assert (second / "copy.txt").read_text() == "existing"
        if confirm:
            click(button("덮어쓰기"))
            wait("confirmed overwrite replaces conflict",
                 lambda: (second / "copy.txt").read_text() == "source")
        else:
            key("Return")
        wait("overwrite confirmation closes", lambda: not named(app, "2개 항목을 덮어씁니다."))
    print("PASS two-step overwrite and safe Enter defaults", flush=True)

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
    wait("editor is writable", lambda: editor.get_state_set().contains(Atspi.StateType.EDITABLE))
    (second / "move.txt").write_text("external update")
    replace_text(editor, "editor update")
    click(button("저장 (Ctrl+S)"))
    find(app, "편집하는 동안 다른 프로그램이 이 파일을 바꿨습니다")
    click(button("취소"))
    wait("conflict prompt dismissed", lambda: not named(app, "다시 읽기"))
    assert (second / "move.txt").read_text() == "external update"
    click(button("편집 취소"))
    print("PASS external edit conflict", flush=True)

    scroll_folder = fixture / "unwatched-scroll"
    scroll_folder.mkdir()
    for i in range(400):
        (scroll_folder / f"item-{i:03d}").touch()
    navigate(scroll_folder)
    button("감시 시작")
    table = files()
    model = table.get_table_iface()
    wait("all scroll fixture rows are listed", lambda: model.get_n_rows() == 400)
    scrollbar = next(node for node in descendants(table.get_parent())
                     if node.get_role() == Atspi.Role.SCROLL_BAR
                     and node.get_state_set().contains(Atspi.StateType.VERTICAL))
    focus(table)
    key("End")
    key("Up", "Shift_L")
    wait("two rows selected at the bottom",
         lambda: model.is_row_selected(398) and model.is_row_selected(399))
    offset = wait("list scrolled to bottom",
                  lambda: offset if (offset := Atspi.Value.get_current_value(scrollbar)) > 1000
                  else None)

    (scroll_folder / "aaa-reload-marker").touch()
    wait("external file triggers a reload", lambda: model.get_n_rows() == 401)
    assert model.get_accessible_at(0, 0).get_name() == "aaa-reload-marker"
    wait(f"scroll position stays at {offset} after reload",
         lambda: abs(Atspi.Value.get_current_value(scrollbar) - offset) < 1)
    assert model.get_selected_rows() == [399, 400], model.get_selected_rows()
    key("Up")
    wait("Up moves from the restored cursor", lambda: model.get_selected_rows() == [398])
    print("PASS unwatched refresh preserves scroll position, selection and keyboard cursor", flush=True)

    navigate(fixture / "job-origin")
    select("job-source")
    key("c", "Control_L")
    job_target = fixture / "job-target"
    job_target.mkdir()
    for name in ("existing", "queued-cancel", "queued-trash", "warmup"):
        (job_target / name).touch()
    navigate(job_target)
    file("existing")
    table = files()
    (job_target / "reload-marker").touch()
    wait("external file reaches the unwatched list", lambda: row(table, "reload-marker"),
         timeout=1)
    assert not row(find(app, "이벤트 로그 목록"), str(job_target / "reload-marker"), 2)
    select("warmup")
    key("Delete")
    wait("warmup job reaches Trash", lambda: (fixture / "data/Trash/files/warmup").is_file())
    find(app, "파일 작업을 완료했습니다")
    focus(files())
    key("v", "Control_L")
    progress = wait("copy progress bar appears", lambda: next(
        (n for n in descendants(app) if n.get_role() == Atspi.Role.PROGRESS_BAR
         and showing(n)), None))
    select("queued-cancel")
    key("Delete")
    click(button("대기 1개"))
    click(button("휴지통으로 이동 취소"))
    wait("queued job leaves the waiting list", lambda: not named(app, "대기 1개"))
    assert (job_target / "queued-cancel").is_file()
    queued_result = find(app, "작업을 취소했습니다 · 완료된 항목 0개")
    click(button("작업 결과 닫기"))
    wait("queued cancellation result is dismissed", lambda: not showing(queued_result))

    select("queued-trash")
    key("Delete")
    button("대기 1개")
    assert (job_target / "queued-trash").is_file()
    assert named(app, "파일 작업을 완료했습니다")
    click(find(progress.get_parent(), "취소", Atspi.Role.PUSH_BUTTON))
    result = wait("cancellation result survives the reload", lambda: next(
        (n for n in descendants(app) if n.get_role() == Atspi.Role.LABEL
         and n.get_name().startswith("작업을 취소했습니다 · 완료된 항목 ") and showing(n)),
        None), timeout=1)
    wait("queued Trash runs after copy cancellation",
         lambda: (fixture / "data/Trash/files/queued-trash").is_file())
    assert not named(app, "대기 1개")
    assert not (job_target / "queued-trash").exists()
    assert not list(job_target.rglob(".filescope-*"))
    file("reload-marker")
    find(app, "이 폴더를 감시하지 않습니다")
    button("감시 시작")
    assert button("작업 결과 닫기").get_action_iface().do_action(0)
    wait("cancellation result can be dismissed", lambda: not showing(result))
    find(app, "이 폴더를 감시하지 않습니다")
    assert sum(1 for _ in (job_target / "job-source").glob("*")) < JOB_ITEMS
    assert sum(1 for _ in (fixture / "job-origin/job-source").iterdir()) == JOB_ITEMS
    print("PASS unwatched refresh, queued cancellation, 20000-file copy cancellation within 1s "
          "and queued Trash", flush=True)

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

    def watch_count(count):
        return next((node for node in descendants(app) if showing(node)
                     and node.get_name().startswith(f"감시 경로 {count}   ")), None)

    def stop_watch(action, marker):
        wait("watch header shows both roots", lambda: watch_count(2))
        action()
        wait("watch header drops a root", lambda: watch_count(1), timeout=1)
        find(app, "감시를 중지했습니다")
        path = watched / marker
        path.touch()
        file(marker)
        assert not row(log, str(path), 2)
        assert row(log, str(first_file), 2)
        click(button("감시 시작"))
        button("감시 중지")

    stop_watch(lambda: click(button("감시 중지")), "unlogged-after-button")

    def stop_from_blank_menu():
        bounds = files().get_component_iface().get_extents(Atspi.CoordType.SCREEN)
        Xtst.XTestFakeMotionEvent(display, -1, bounds.x + bounds.width // 2,
                                 bounds.y + bounds.height - 10, 0)
        Xtst.XTestFakeButtonEvent(display, 3, True, 0)
        Xtst.XTestFakeButtonEvent(display, 3, False, 0)
        X11.XSync(display, False)
        click(find(app, "이 폴더 감시 중지", Atspi.Role.MENU_ITEM))

    stop_watch(stop_from_blank_menu, "unlogged-after-menu")
    (watched / "unlogged-after-button").unlink()
    (watched / "unlogged-after-menu").unlink()
    print("PASS stop watching via folder band and blank menu, preserving earlier log entries",
          flush=True)

    force_overflow(app, first_file, second_file)
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

    sorting = fixture / "sorting"
    sorting.mkdir()
    for name in ("b", "A"):
        (sorting / name).mkdir()
    for name in ("file-1", "file-2", "file-10", "File-3", os.fsdecode(b"z-\xff")):
        (sorting / name).touch()
    navigate(sorting)
    table = files().get_table_iface()
    wait("seven sorted rows including invalid UTF-8", lambda: table.get_n_rows() == 7)
    actual = [table.get_accessible_at(i, 0).get_name() for i in range(7)]
    assert actual == ["A", "b", "file-1", "file-2", "File-3", "file-10", "z-\ufffd"], actual
    print("PASS folders first, natural filename order and invalid UTF-8", flush=True)

    close(app)


def force_overflow(app, first_file, second_file):
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


def exercise_trash_failure(app, source, data_home):
    files = find(app, "파일 목록")
    cell = wait("cross-device source is listed", lambda: row(files, source.name))
    click(cell)
    key("Delete")
    find(app, "⚠ 휴지통 이동 실패 · 1개 항목 · 홈 휴지통으로 옮길 수 없습니다")
    assert source.is_file()
    assert not (data_home / "Trash/files" / source.name).exists()
    delete = find(app, "영구 삭제…", Atspi.Role.PUSH_BUTTON)
    click(delete)
    find(app, "영구 삭제하시겠습니까?")
    key("Return")
    wait("permanent delete confirmation closes", lambda: not named(app, "영구 삭제하시겠습니까?"))
    assert source.is_file()
    click(delete)
    find(app, "영구 삭제하시겠습니까?")
    click(find(app, "영구 삭제", Atspi.Role.PUSH_BUTTON))
    wait("only explicit permanent delete removes the failed item", lambda: not source.exists())
    close(app)
    print("PASS cross-device Trash failure band, separate permanent delete confirmation",
          flush=True)


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
        (job_source / f"item-{i:04d}").write_bytes(b"copy fixture\n")

    run(binary, lambda app: exercise(app, home, fixture))

    def restart(app):
        click(find(app, f"☆  {second}"))
        breadcrumb = find(app, "현재 경로")
        find(breadcrumb, second.name, Atspi.Role.PUSH_BUTTON)
        close(app)

    run(binary, restart)
    print("PASS favorite persists and window close exits", flush=True)
    source = home / "cross-device.txt"
    source.touch()
    with tempfile.TemporaryDirectory(dir="/dev/shm") as tmp:
        data_home = Path(tmp)
        assert data_home.stat().st_dev != source.stat().st_dev
        previous_data_home = os.environ["XDG_DATA_HOME"]
        try:
            os.environ["XDG_DATA_HOME"] = tmp
            run(binary, lambda app: exercise_trash_failure(app, source, data_home))
        finally:
            os.environ["XDG_DATA_HOME"] = previous_data_home


if __name__ == "__main__":
    main()
