#include "ui/tray.hpp"

#include <glib.h>

namespace {

using namespace filescope;
using namespace filescope::ui;

core::EventPtr sample(core::EventKind kind, const char *path, std::uint64_t sequence) {
    auto event = std::make_shared<core::Event>();
    event->kind = kind;
    event->path = path;
    event->seq = sequence;
    event->observed_at_us = 1000000 * static_cast<std::int64_t>(sequence);
    return event;
}

void check_item(const TrayItem &item, TrayAction action, const char *label) {
    g_assert_true(item.action == action);
    g_assert_cmpstr(item.label.c_str(), ==, label);
}

void test_menu() {
    std::vector<mon::WatchInfo> roots(3);
    roots[0].path = "/first";
    roots[1].path = "/second";
    roots[2].path = "/third";
    const std::vector changes{sample(core::EventKind::MODIFY, "/first/newest", 4),
                              sample(core::EventKind::CREATE, "/first/middle", 3),
                              sample(core::EventKind::DELETE, "/second/older", 2),
                              sample(core::EventKind::MOVE, "/third/oldest", 1)};
    const auto active = tray_items(TrayState::ACTIVE, roots, changes, true);
    g_assert_cmpuint(active.size(), ==, 16);
    check_item(active[0], TrayAction::INFO, "감시 중 · 경로 3개");
    check_item(active[1], TrayAction::INFO, "/first");
    check_item(active[2], TrayAction::INFO, "/second");
    check_item(active[3], TrayAction::INFO, "/third");
    check_item(active[4], TrayAction::SEPARATOR, "");
    check_item(active[5], TrayAction::CHANGE, "▲ newest 수정    00:00:04");
    check_item(active[6], TrayAction::CHANGE, "● middle 생성    00:00:03");
    check_item(active[7], TrayAction::CHANGE, "✕ older 삭제    00:00:02");
    for (std::size_t i = 0; i < 3; ++i) {
        g_assert_true(active[5 + i].event == changes[i]);
    }
    check_item(active[8], TrayAction::SEPARATOR, "");
    check_item(active[9], TrayAction::SUMMARY, "요약 열기");
    check_item(active[10], TrayAction::MAIN, "메인 창 열기");
    check_item(active[11], TrayAction::SEPARATOR, "");
    check_item(active[12], TrayAction::PAUSE, "감시 일시 정지");
    check_item(active[13], TrayAction::FOLDERS, "감시 폴더 변경…");
    check_item(active[14], TrayAction::SEPARATOR, "");
    check_item(active[15], TrayAction::QUIT, "FileScope 종료");

    const auto paused = tray_items(TrayState::PAUSED, roots, changes, true);
    check_item(paused[0], TrayAction::INFO, "일시 정지됨 · 경로 3개");
    check_item(paused[12], TrayAction::PAUSE, "감시 재개");
    const auto overflow = tray_items(TrayState::RESYNCED, roots, changes, true);
    check_item(overflow[0], TrayAction::INFO, "대기열 넘침 · 다시 검사함");
    const auto idle = tray_items(TrayState::IDLE, {}, changes, false);
    check_item(idle[0], TrayAction::INFO, "FileScope: 감시 경로 없음");
    check_item(idle[2], TrayAction::CHANGE, "▲ newest 수정    00:00:04");
    check_item(idle[9], TrayAction::INFO, "감시 일시 정지");

    roots.emplace_back();
    roots.back().path = "/fourth";
    const auto empty = tray_items(TrayState::ACTIVE, roots, {}, true);
    g_assert_cmpuint(empty.size(), ==, 15);
    check_item(empty[0], TrayAction::INFO, "감시 중 · 경로 4개");
    check_item(empty[4], TrayAction::INFO, "외 1개 경로 (요약에서 보기)");
    check_item(empty[6], TrayAction::INFO, "아직 바뀐 파일이 없습니다");
}

} // namespace

int main(int argc, char **argv) {
    g_setenv("TZ", "UTC", TRUE);
    g_test_init(&argc, &argv, nullptr);
    g_test_add_func("/ui/tray/menu", test_menu);
    return g_test_run();
}
