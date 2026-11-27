"""Folder-scoped session activity and file creation menus through GTK and AT-SPI."""
import os
from pathlib import Path
import sys

from history_interaction import capture, focused, label, select_file
from interaction import (X11, Xtst, click, close, display, find, key, named, replace_text,
                         row, row_index, run, wait)
from r2_interaction import focus_transient, navigate
from gi.repository import Atspi


def log_rows(app):
    table = find(app, "이벤트 로그 목록").get_table_iface()
    return [(table.get_accessible_at(i, 1).get_name(),
             table.get_accessible_at(i, 2).get_name())
            for i in range(table.get_n_rows())]


def counts(app):
    return tuple(int(Atspi.Text.get_text(find(app, f"{kind} 집계"), 0, -1))
                 for kind in ("● 생성", "▲ 수정", "✕ 삭제", "→ 이동", "◦ 속성"))


def check_scope(app, creates, moves=()):
    expected = {str(path) for path in creates}
    expected_moves = {str(path) for path in moves}
    actual = log_rows(app)
    assert {path for kind, path in actual if kind == "● CREATE"} == expected, actual
    assert {path for _, path in actual} == expected | expected_moves, actual
    assert counts(app) == (len(creates), 0, 0, len(moves), 0), counts(app)


def candle_values(app):
    click(find(app, "변경량 · 1분봉"))
    chart = find(app, "감시 폴더 총용량 1분봉 차트")
    assert chart.get_component_iface().grab_focus()
    wait("candle chart has keyboard focus", lambda: focused(chart))
    key("Right")
    description = wait("latest candle has accessible values",
                       lambda: text if ", 생성 " in (text := chart.get_description()) else None)
    click(find(app, "이벤트 로그"))
    return description.split(",", 1)[1]


def exercise_scope(app, home, data):
    a, b, sub, subc = home / "A", home / "B", home / "A/sub", home / "A/subc"
    marker = data / "private-event"
    navigate(app, data)
    assert marker.parent.is_dir()
    rate = label(app, "최근 1분 0").get_name()
    candle = candle_values(app)
    marker.write_text("history fixture")
    files = find(app, "파일 목록")
    wait("watcher sees history directory probe", lambda: row(files, marker.name))
    check_scope(app, [])
    assert label(app, "최근 1분 0").get_name() == rate
    assert candle_values(app) == candle

    navigate(app, data.parent)
    select_file(app, data.name)
    if not named(app, "변경 내용"):
        click(find(app, "변경 내용 패널"))
    label(app, "이 세션에서 하위 변경 없음")
    navigate(app, home)
    wait("home list is open", lambda: row(find(app, "파일 목록"), "A"))
    check_scope(app, [])

    paths = [a / "event-a", sub / "event-deep", b / "event-b", subc / "event-boundary"]
    for path in paths:
        path.mkdir()
    moved = b / "moved"
    (a / "move-source").rename(moved)
    wait("all four creates and move observed in home", lambda: counts(app) == (4, 0, 0, 1, 0))
    check_scope(app, paths, [moved])
    navigate(app, a)
    check_scope(app, [paths[0], paths[1], paths[3]], [moved])
    key("t", "Control_L")
    navigate(app, b)
    check_scope(app, [paths[2]], [moved])
    label(app, "최근 1분 5")
    click(find(app, "A", Atspi.Role.PAGE_TAB))
    check_scope(app, [paths[0], paths[1], paths[3]], [moved])
    navigate(app, sub)
    check_scope(app, [paths[1]])
    capture("f04-scoped-log")
    print("PASS history exclusion, scoped log/counts, path boundary, move and tab switch", flush=True)


def right_click(node, blank=False):
    bounds = node.get_component_iface().get_extents(Atspi.CoordType.SCREEN)
    assert bounds.width > 0 and bounds.height > (10 if blank else 0), node.get_name()
    y = bounds.y + (bounds.height - 10 if blank else bounds.height // 2)
    Xtst.XTestFakeMotionEvent(display, -1, bounds.x + bounds.width // 2, y, 0)
    Xtst.XTestFakeButtonEvent(display, 3, True, 0)
    Xtst.XTestFakeButtonEvent(display, 3, False, 0)
    X11.XSync(display, False)


def menu_item(app, name):
    return find(app, name, Atspi.Role.MENU_ITEM)


def new_entry(app, name, path):
    click(menu_item(app, name))
    dialog = find(app, name, Atspi.Role.DIALOG)
    entry = find(dialog, "", Atspi.Role.TEXT)
    replace_text(entry, path.name)
    with focus_transient():
        key("Return")
    wait(f"created {path.name}", path.exists)


def exercise_menus(app, home, outside):
    folder = home / "B"
    navigate(app, folder)
    files = select_file(app, "event-b")
    right_click(files, blank=True)
    menu_item(app, "새 파일")
    paste = menu_item(app, "붙여넣기")
    assert not paste.get_state_set().contains(Atspi.StateType.SENSITIVE)
    assert not named(app, "이름 변경", Atspi.Role.MENU_ITEM)
    assert not named(app, "이 폴더 감시 시작", Atspi.Role.MENU_ITEM)
    assert files.get_table_iface().is_row_selected(row_index(files, "event-b"))
    capture("f04-blank-menu")
    new_folder = folder / "menu-folder"
    new_entry(app, "새 폴더", new_folder)
    assert new_folder.is_dir()

    files = select_file(app, "event-b")
    right_click(row(files, "event-b"))
    menu_item(app, "기본 앱으로 열기")
    menu_item(app, "새 폴더")
    assert "F2" in menu_item(app, "이름 변경").get_action_iface().get_key_binding(0)
    capture("f04-row-menu")
    key("Escape")
    assert files.get_component_iface().grab_focus()
    wait("file list has focus", lambda: focused(files))
    key("F10", "Shift_L")
    menu_item(app, "텍스트 편집")
    key("Escape")
    wait("Escape returns focus to file list", lambda: focused(files))
    key("c", "Control_L")
    right_click(files, blank=True)
    assert menu_item(app, "붙여넣기").get_state_set().contains(Atspi.StateType.SENSITIVE)
    key("Escape")

    navigate(app, outside)
    wait("unwatched empty folder ready", lambda: named(app, "이 폴더는 비어 있습니다"))
    find(app, "감시 시작", Atspi.Role.PUSH_BUTTON)
    right_click(find(app, "이 폴더는 비어 있습니다"))
    menu_item(app, "이 폴더 감시 시작")
    key("Escape")
    files_button = find(app, "새 파일", Atspi.Role.PUSH_BUTTON)
    assert files_button.get_component_iface().grab_focus()
    key("Menu")
    assert not named(app, "이름 변경", Atspi.Role.MENU_ITEM)
    click(menu_item(app, "이 폴더 감시 시작"))
    wait("watch-start banner clears", lambda: not named(app, "감시 시작"))
    create = find(app, "새로 만들기")
    bounds = create.get_component_iface().get_extents(Atspi.CoordType.SCREEN)
    assert (bounds.width, bounds.height) == (26, 26), (bounds.width, bounds.height)
    click(create)
    menu_item(app, "새 폴더")
    capture("f04-header-menu")
    new_file = outside / "header-file.txt"
    new_entry(app, "새 파일", new_file)
    assert new_file.is_file()
    close(app)
    print("PASS blank/row menus, keyboard focus, clipboard sensitivity and both creation paths",
          flush=True)


if __name__ == "__main__":
    binary = str(Path(sys.argv[1]).resolve())
    fixture = Path(sys.argv[2])
    home = fixture / "home"
    outside = fixture / "unwatched"
    for path in (home / "A/sub", home / "A/subc", home / "B", outside):
        path.mkdir(parents=True)
    (home / "A/move-source").touch()
    share = home / ".local/share"
    share.mkdir(parents=True, mode=0o700)
    os.environ["XDG_DATA_HOME"] = str(share)
    data = share / "filescope"

    def scenario(app):
        exercise_scope(app, home, data)
        exercise_menus(app, home, outside)

    run(binary, scenario)
