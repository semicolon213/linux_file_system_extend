"""Exercise the real StatusNotifier item and native menu over the session bus."""
import ctypes as ct
from pathlib import Path
import sqlite3
import sys
import time

import gi

from history_interaction import capture, diff_text
from interaction import (X11, click, close, descendants, display, find, force_overflow,
                         key, named, row, run, showing, wait)

gi.require_version("GdkPixbuf", "2.0")
from gi.repository import Atspi, GdkPixbuf, Gio, GLib


WATCHER = "org.kde.StatusNotifierWatcher"
ITEM = "org.kde.StatusNotifierItem"
MENU = "com.canonical.dbusmenu"
WATCHER_XML = f"""
<node><interface name="{WATCHER}">
  <method name="RegisterStatusNotifierItem"><arg type="s" direction="in"/></method>
  <property name="IsStatusNotifierHostRegistered" type="b" access="read"/>
  <property name="RegisteredStatusNotifierItems" type="as" access="read"/>
  <property name="ProtocolVersion" type="i" access="read"/>
</interface></node>
"""


class StatusHost:
    def __init__(self):
        self.bus = Gio.bus_get_sync(Gio.BusType.SESSION)
        self.item = None
        self.bus.call_sync(
            "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
            "RequestName", GLib.Variant("(su)", (WATCHER, 0)), None,
            Gio.DBusCallFlags.NONE, 2000, None,
        )
        self.bus.register_object(
            "/StatusNotifierWatcher", Gio.DBusNodeInfo.new_for_xml(WATCHER_XML).interfaces[0],
            self.register, self.property, None,
        )

    def register(self, _conn, sender, _path, _iface, _method, params, invocation):
        address, = params.unpack()
        self.item = (sender, address) if address.startswith("/") else (address, "/StatusNotifierItem")
        invocation.return_value(GLib.Variant("()", ()))

    def property(self, _conn, _sender, _path, _iface, name):
        return {
            "IsStatusNotifierHostRegistered": GLib.Variant("b", True),
            "RegisteredStatusNotifierItems": GLib.Variant("as", []),
            "ProtocolVersion": GLib.Variant("i", 0),
        }[name]

    def registered(self):
        context = GLib.MainContext.default()
        while context.iteration(False):
            pass
        return self.item

    def call(self, path, interface, method, arguments):
        return self.bus.call_sync(
            self.item[0], path, interface, method, arguments, None,
            Gio.DBusCallFlags.NONE, 2000, None,
        ).unpack()

    def props(self):
        return self.call(self.item[1], "org.freedesktop.DBus.Properties", "GetAll",
                         GLib.Variant("(s)", (ITEM,)))[0]

    def layout(self):
        _, (_, _, rows) = self.call(
            self.props()["Menu"], MENU, "GetLayout", GLib.Variant("(iias)", (0, 1, []))
        )
        return rows

    def activate(self, name):
        item = wait(name, lambda: next(
            ((item_id, props) for item_id, props, _ in self.layout() if props.get("label") == name),
            None,
        ))
        assert item[1].get("enabled", True), name
        self.call(self.props()["Menu"], MENU, "Event",
                  GLib.Variant("(isvu)", (item[0], "clicked", GLib.Variant("s", ""), 0)))


def notice(frame, text):
    return next((child for child in descendants(frame)
                 if showing(child) and text in child.get_name()), None)


def focus_window(node):
    # Bare Xvfb has no window manager to honor present() requests for keyboard focus.
    click(node)
    X11.XDefaultRootWindow.argtypes = [ct.c_void_p]
    X11.XDefaultRootWindow.restype = ct.c_ulong
    X11.XQueryPointer.argtypes = [ct.c_void_p, ct.c_ulong, *([ct.POINTER(ct.c_ulong)] * 2),
                                 *([ct.POINTER(ct.c_int)] * 4), ct.POINTER(ct.c_uint)]
    X11.XSetInputFocus.argtypes = [ct.c_void_p, ct.c_ulong, ct.c_int, ct.c_ulong]
    root, child = ct.c_ulong(), ct.c_ulong()
    coords = [ct.c_int() for _ in range(4)]
    mask = ct.c_uint()
    assert X11.XQueryPointer(display, X11.XDefaultRootWindow(display), ct.byref(root),
                            ct.byref(child), *(ct.byref(v) for v in coords), ct.byref(mask))
    assert child.value
    X11.XSetInputFocus(display, child.value, 1, 0)
    X11.XSync(display, False)


def exercise(app, host, fixture, watched, icon_dir):
    wait("StatusNotifier item registers", host.registered)

    def check_icon(state, description, timeout=8):
        expected_path = str(icon_dir / f"filescope-{state}-symbolic.svg")

        def matches():
            props = host.props()
            assert props["IconThemePath"] == "", props
            if (props["IconName"], props["IconAccessibleDesc"]) == (expected_path, description):
                return props["IconName"]
            return None

        path = wait(f"{state} SVG and its description", matches, timeout=timeout)
        icon = GdkPixbuf.Pixbuf.new_from_file_at_scale(path, 18, 18, True)
        assert icon.get_has_alpha(), path

    check_icon("active", "FileScope: 감시 중")
    rows = host.layout()
    _, status, _ = rows[0]
    _, root, _ = rows[1]
    assert status["label"] == "감시 중 · 경로 1개" and not status.get("enabled", True), status
    assert root["label"].endswith("/home") and not root.get("enabled", True), root
    database = fixture / "data/filescope/history.db"

    def scalar(query, arguments=()):
        with sqlite3.connect(f"file:{database}?mode=ro", uri=True) as connection:
            return connection.execute(query, arguments).fetchone()[0]

    def recorded(path):
        return scalar("SELECT count(*) FROM events WHERE CAST(path AS TEXT) = ?", (str(path),))

    def open_summary():
        host.activate("요약 열기")
        return find(app, "FileScope 요약", Atspi.Role.FRAME)

    main = find(app, "FileScope", Atspi.Role.FRAME)
    summary = open_summary()
    find(summary, "아직 바뀐 파일이 없습니다")
    rate = find(summary, "최근 1분 0건")
    find(summary, "하위 폴더 포함")
    find(summary, "● 0")
    capture("tray-empty")
    focus_window(rate)
    key("Escape")
    wait("Escape closes summary", lambda: not named(app, "FileScope 요약", Atspi.Role.FRAME))

    wait("baseline version recorded", lambda: scalar(
        "SELECT count(*) FROM versions WHERE CAST(path AS TEXT) = ?", (str(watched),)
    ))
    close(app)
    wait("main window hides while connected", lambda: not named(app, "FileScope", Atspi.Role.FRAME))

    summary = open_summary()
    marker = watched.parent / "while-hidden.txt"
    marker.write_text("recorded while main is hidden\n")
    change_name = f"메인 창에서 {marker.name} 변경 내용 보기"
    recent = wait("external change appears within two seconds",
                  lambda: named(summary, change_name), timeout=2)
    find(summary, "● 1")
    assert not named(summary, "최근 1분 0건")
    wait("hidden main still records history", lambda: recorded(marker))
    capture("tray-live")
    click(recent)
    main = find(app, "FileScope", Atspi.Role.FRAME)
    files = find(main, "파일 목록")
    wait("summary row opens its parent and selects the file",
         lambda: row(files, marker.name)
         and files.get_selection_iface().get_n_selected_children() > 0)
    wait("summary row opens the change panel", lambda: named(main, marker.name, Atspi.Role.LABEL))

    close(app)
    wait("main hides again", lambda: not named(app, "FileScope", Atspi.Role.FRAME))
    host.activate("메인 창 열기")
    main = find(app, "FileScope", Atspi.Role.FRAME)
    summary = open_summary()
    pause = find(summary, "감시 일시 정지")
    assert pause.get_state_set().contains(Atspi.StateType.FOCUSABLE)
    focus_window(find(summary, "● 감시 중"))
    assert pause.get_component_iface().grab_focus()
    key("space")
    find(summary, "Ⅱ 일시 정지됨")
    find(summary, "최근 1분 0건")
    wait("pause notice", lambda: notice(summary, "일시 정지 중에는 변경을 기록하지 않습니다"))
    check_icon("paused", "FileScope: 일시 정지됨")
    wait("pause gap opened", lambda: scalar(
        "SELECT count(*) FROM gaps WHERE kind=1 AND ended_at_us IS NULL"
    ))
    capture("tray-paused")

    skipped = watched.parent / "during-pause.txt"
    skipped.write_text("not an event\n")
    watched.write_text("changed during pause\n")
    time.sleep(0.6)
    assert not row(files, skipped.name)
    assert not named(summary, f"메인 창에서 {skipped.name} 변경 내용 보기")
    assert not recorded(skipped) and not recorded(watched)
    find(summary, "최근 1분 0건")
    host.activate("감시 재개")
    find(summary, "● 감시 중")
    wait("pause gap closed", lambda: scalar(
        "SELECT count(*) FROM gaps WHERE kind=1 AND ended_at_us IS NOT NULL"
    ))
    after = watched.parent / "after-resume.txt"
    after.write_text("recorded again\n")
    wait("resumed change within two seconds", lambda: named(
        summary, f"메인 창에서 {after.name} 변경 내용 보기"
    ), timeout=2)
    wait("resumed change written to history", lambda: recorded(after))
    assert not recorded(skipped) and not recorded(watched)

    host.activate("메인 창 열기")
    main = find(app, "FileScope", Atspi.Role.FRAME)
    files = find(main, "파일 목록")
    log = find(main, "이벤트 로그 목록")
    wait("hidden change remains in the reopened file list", lambda: row(files, marker.name))
    wait("hidden change remains in the reopened log", lambda: row(log, str(marker), 2))
    assert not row(log, str(skipped), 2)
    cell = wait("paused file in reopened list", lambda: row(files, watched.name))
    click(cell)
    wait("baseline rescan exposes new version in the panel",
         lambda: "+ changed during pause" in diff_text(main))

    force_overflow(app, marker, watched)
    check_icon("warning", "FileScope: 대기열 넘침 · 다시 검사함", timeout=30)
    wait("overflow notice is in the native menu", lambda: any(
        props.get("label") == "대기열 넘침 · 다시 검사함" for _, props, _ in host.layout()
    ))
    summary = open_summary()
    find(summary, "● 감시 중")
    wait("acknowledged overflow keeps its timestamped notice",
         lambda: notice(summary, "대기열이 넘쳐 다시 검사해 맞췄습니다"))
    check_icon("active", "FileScope: 감시 중")
    assert sum(showing(child) and child.get_name().startswith("메인 창에서 ")
               for child in descendants(summary)) == 5
    capture("tray-overflow")

    host.activate("감시 일시 정지")
    summary = open_summary()
    find(summary, "Ⅱ 일시 정지됨")
    host.activate("FileScope 종료")
    print("PASS connected tray, hidden monitoring/history, summary, pause/resume, overflow and "
          "paused exit", flush=True)


if __name__ == "__main__":
    binary = Path(sys.argv[1]).resolve()
    fixture = Path(sys.argv[2])
    watched = fixture / "home/child/previous.txt"
    watched.parent.mkdir()
    watched.write_text("baseline\n")
    host = StatusHost()
    run(binary, lambda app: exercise(app, host, fixture, watched, binary.parent / "tray-icons"))
