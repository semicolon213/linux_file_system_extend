#include <algorithm>
#include <array>
#include <fcntl.h>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include <glib.h>
#include <glib/gstdio.h>
#include <glibmm/main.h>

#include "mon/mon.hpp"
#include "mon/mon_testing.hpp"
#include "test_support.hpp"

namespace {

using filescope::core::EventKind;

bool has_kind(const std::vector<filescope::core::EventPtr> &events, EventKind kind,
              const std::string &path = {}) {
    return std::any_of(events.begin(), events.end(), [&](const auto &event) {
        return event->kind == kind && (path.empty() || event->path == path);
    });
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void write_file(const std::string &path, const std::string &contents, bool append = false) {
    std::ofstream output(path, append ? std::ios::app : std::ios::trunc);
    output << contents;
    output.close();
    g_assert_true(output.good());
}

void test_basic_events_and_size_index() {
    filescope::test::TestEnvironment environment;
    const std::string root = environment.root() + "/watch";
    const std::string file = root + "/a";
    g_assert_cmpint(g_mkdir(root.c_str(), 0700), ==, 0);
    write_file(file, std::string(100, 'a'));

    auto created = filescope::mon::Watcher::create();
    g_assert_true(created.ok());
    auto watcher = std::move(created).value();
    std::vector<filescope::core::EventPtr> events;
    watcher->signal_events().connect(
        [&events](const auto &batch) { events.insert(events.end(), batch.begin(), batch.end()); });
    auto added = watcher->add_root(root, filescope::mon::WatchMode::NON_RECURSIVE);
    g_assert_true(added.ok());
    g_assert_true(filescope::test::wait_until(
        [&] { return watcher->root_total(added.value()).has_value(); }, 2000));
    auto total = watcher->root_total(added.value()).value_or(filescope::mon::RootTotal{});
    g_assert_cmpuint(total.total_bytes, ==, 100);
    g_assert_cmpuint(total.file_count, ==, 1);

    write_file(file, std::string(50, 'b'), true);
    g_assert_cmpint(g_chmod(file.c_str(), 0600), ==, 0);
    g_assert_true(filescope::test::wait_until(
        [&] {
            return has_kind(events, EventKind::MODIFY, file) &&
                   has_kind(events, EventKind::CLOSE_WRITE, file) &&
                   has_kind(events, EventKind::ATTRIB, file);
        },
        2000));
    const auto close = std::find_if(events.begin(), events.end(), [&](const auto &event) {
        return event->kind == EventKind::CLOSE_WRITE && event->path == file;
    });
    g_assert_true(close != events.end());
    g_assert_true((*close)->stat.has_value());
    g_assert_cmpuint((*close)->stat.value_or(filescope::core::EventStat{}).size, ==, 150);
    g_assert_cmpuint((*close)->root_total.value_or(0), ==, 150);

    const std::string created_file = root + "/new";
    write_file(created_file, "0123456789");
    g_assert_true(filescope::test::wait_until(
        [&] { return has_kind(events, EventKind::CREATE, created_file); }, 2000));
    g_assert_cmpint(g_remove(created_file.c_str()), ==, 0);
    g_assert_true(filescope::test::wait_until(
        [&] { return has_kind(events, EventKind::DELETE, created_file); }, 2000));
    const std::string link = root + "/link";
    g_assert_cmpint(symlink(file.c_str(), link.c_str()), ==, 0);
    g_assert_true(filescope::test::wait_until(
        [&] { return has_kind(events, EventKind::CREATE, link); }, 2000));
    g_assert_cmpint(g_remove(link.c_str()), ==, 0);
    total = watcher->root_total(added.value()).value_or(filescope::mon::RootTotal{});
    g_assert_cmpuint(total.total_bytes, ==, 150);
}

void test_pause_keeps_index_and_dynamic_watches() {
    using namespace filescope::mon;
    filescope::test::TestEnvironment environment;
    const auto root = environment.root() + "/watch";
    const auto nested = root + "/a/b";
    const auto file = nested + "/file";
    const auto removed = root + "/removed";
    const auto moved = root + "/moved";
    const auto terminal = environment.root() + "/terminal";
    g_assert_cmpint(g_mkdir(root.c_str(), 0700), ==, 0);
    g_assert_cmpint(g_mkdir(terminal.c_str(), 0700), ==, 0);
    write_file(removed, "remove");
    write_file(moved, "move");

    auto created = Watcher::create();
    g_assert_true(created.ok());
    auto watcher = std::move(created).value();
    std::vector<filescope::core::EventPtr> events;
    std::vector<WatchInfo> states;
    watcher->signal_events().connect(
        [&events](const auto &batch) { events.insert(events.end(), batch.begin(), batch.end()); });
    watcher->signal_state().connect([&states](const auto &info, const auto &error) {
        g_assert_false(error.has_value());
        states.push_back(info);
    });
    const auto added = watcher->add_root(root, WatchMode::RECURSIVE);
    g_assert_true(added.ok());
    const auto id = added.value();
    g_assert_true(filescope::test::wait_until([&] { return states.size() == 1; }, 2000));
    g_assert_true(states.back().state == WatchState::ACTIVE);
    g_assert_false(watcher->paused());

    watcher->pause();
    watcher->pause();
    g_assert_true(watcher->paused());
    g_assert_true(filescope::test::wait_until([&] { return states.size() == 2; }, 2000));
    g_assert_true(states.back().state == WatchState::PAUSED);
    g_assert_true(states.back().reason == WatchReason::NONE);
    events.clear();

    g_assert_cmpint(g_mkdir_with_parents(nested.c_str(), 0700), ==, 0);
    write_file(file, "first");
    write_file(file, "more", true);
    g_assert_cmpint(g_remove(removed.c_str()), ==, 0);
    filescope::mon::testing::set_move_window_ms(*watcher, 5000);
    g_assert_cmpint(g_rename(moved.c_str(), (environment.root() + "/outside").c_str()), ==, 0);
    const auto added_while_paused = watcher->add_root(terminal, WatchMode::NON_RECURSIVE);
    g_assert_true(added_while_paused.ok());
    g_assert_true(filescope::test::wait_until([&] { return states.size() == 3; }, 2000));
    g_assert_cmpuint(states.back().id, ==, added_while_paused.value());
    g_assert_true(states.back().state == WatchState::PAUSED);
    g_assert_cmpint(g_rmdir(terminal.c_str()), ==, 0);
    g_assert_true(filescope::test::wait_until([&] { return states.size() == 4; }, 2000));
    g_assert_true(states.back().state == WatchState::STOPPED);
    g_assert_true(states.back().reason == WatchReason::ROOT_DELETED);
    g_assert_true(events.empty());

    watcher->resume();
    watcher->resume();
    g_assert_false(watcher->paused());
    g_assert_true(filescope::test::wait_until([&] { return states.size() == 5; }, 2000));
    g_assert_true(states.back().state == WatchState::ACTIVE);
    struct stat status {};
    g_assert_cmpint(lstat(file.c_str(), &status), ==, 0);
    auto total = static_cast<std::uint64_t>(status.st_size);
    g_assert_true(states.back().total_known);
    g_assert_cmpuint(states.back().total_bytes, ==, total);
    g_assert_true(events.empty());
    write_file(file, "after", true);
    g_assert_true(filescope::test::wait_until(
        [&] { return has_kind(events, EventKind::CLOSE_WRITE, file); }, 2000));
    g_assert_true(has_kind(events, EventKind::MODIFY, file));
    for (const auto &event : events) {
        g_assert_cmpuint(event->watch_id, ==, id);
        g_assert_cmpstr(event->path.c_str(), ==, file.c_str());
        g_assert_true(event->size_delta.has_value());
        g_assert_true(event->root_total.has_value());
        total = static_cast<std::uint64_t>(static_cast<std::int64_t>(total) +
                                           event->size_delta.value_or(0));
        g_assert_cmpuint(event->root_total.value_or(0), ==, total);
    }
    const auto root_total = watcher->root_total(id);
    g_assert_true(root_total.has_value());
    g_assert_cmpuint(root_total.value_or(RootTotal{}).total_bytes, ==, total);
    g_assert_cmpuint(states.size(), ==, 5);

    watcher->pause();
    g_assert_true(filescope::test::wait_until([&] { return states.size() == 6; }, 2000));
    events.clear();
    write_file(file, "overflow replacement");
    filescope::mon::testing::inject_overflow(*watcher);
    watcher->resume();
    g_assert_true(filescope::test::wait_until([&] { return states.size() == 7; }, 2000));
    g_assert_true(states.back().state == WatchState::ACTIVE);
    g_assert_cmpint(lstat(file.c_str(), &status), ==, 0);
    g_assert_cmpuint(states.back().total_bytes, ==, static_cast<std::uint64_t>(status.st_size));
    g_assert_true(events.empty());

    watcher->pause();
    g_assert_true(filescope::test::wait_until([&] { return states.size() == 8; }, 2000));
    filescope::mon::testing::inject_overflow(*watcher);
    watcher.reset();
    while (Glib::MainContext::get_default()->iteration(false)) {
    }
    g_assert_cmpuint(states.size(), ==, 8);
    g_assert_true(events.empty());
}

void test_recursive_moves_overflow_and_remove() {
    filescope::test::TestEnvironment environment;
    const std::string root = environment.root() + "/watch";
    const std::string outside = environment.root() + "/outside";
    g_assert_cmpint(g_mkdir(root.c_str(), 0700), ==, 0);
    g_assert_cmpint(g_mkdir(outside.c_str(), 0700), ==, 0);

    auto created = filescope::mon::Watcher::create();
    g_assert_true(created.ok());
    auto watcher = std::move(created).value();
    std::vector<filescope::core::EventPtr> events;
    std::vector<filescope::mon::WatchState> states;
    watcher->signal_events().connect(
        [&events](const auto &batch) { events.insert(events.end(), batch.begin(), batch.end()); });
    watcher->signal_state().connect(
        [&states](const auto &info, const auto &) { states.push_back(info.state); });
    auto added = watcher->add_root(root, filescope::mon::WatchMode::RECURSIVE);
    g_assert_true(added.ok());
    g_assert_true(filescope::test::wait_until(
        [&] { return watcher->root_total(added.value()).has_value(); }, 2000));

    const std::string a = root + "/a";
    const std::string b = a + "/b";
    g_assert_cmpint(g_mkdir_with_parents(b.c_str(), 0700), ==, 0);
    const std::string nested = b + "/file";
    write_file(nested, "data");
    g_assert_true(filescope::test::wait_until(
        [&] { return has_kind(events, EventKind::CREATE, nested); }, 2000));
    g_assert_true(watcher->list_roots().front().watch_count >= 3);

    const std::string renamed = root + "/renamed";
    g_assert_cmpint(g_rename(a.c_str(), renamed.c_str()), ==, 0);
    g_assert_true(filescope::test::wait_until(
        [&] { return has_kind(events, EventKind::RENAME, renamed); }, 2000));
    const std::string renamed_file = renamed + "/b/file";
    write_file(renamed_file, "x", true);
    g_assert_true(filescope::test::wait_until(
        [&] { return has_kind(events, EventKind::MODIFY, renamed_file); }, 2000));

    filescope::mon::testing::set_move_window_ms(*watcher, 10);
    const std::string moved_out = outside + "/renamed";
    g_assert_cmpint(g_rename(renamed.c_str(), moved_out.c_str()), ==, 0);
    g_assert_true(filescope::test::wait_until(
        [&] { return has_kind(events, EventKind::MOVED_OUT, renamed); }, 2000));

    filescope::mon::testing::inject_overflow(*watcher);
    g_assert_true(filescope::test::wait_until(
        [&] {
            return has_kind(events, EventKind::OVERFLOW) &&
                   has_kind(events, EventKind::RESYNC_DONE);
        },
        2000));
    g_assert_true(std::find(states.begin(), states.end(), filescope::mon::WatchState::DEGRADED) !=
                  states.end());
    g_assert_true(std::find(states.begin(), states.end(), filescope::mon::WatchState::RESYNCING) !=
                  states.end());
    g_assert_true(watcher->remove_root(added.value()));
    g_assert_true(filescope::test::wait_until([&] { return watcher->list_roots().empty(); }, 2000));
}

void test_multiple_roots_merge_moved_in_and_queue_gap() {
    filescope::test::TestEnvironment environment;
    const std::string first = environment.root() + "/first";
    const std::string second = environment.root() + "/second";
    const std::string outside = environment.root() + "/outside";
    g_assert_cmpint(g_mkdir(first.c_str(), 0700), ==, 0);
    g_assert_cmpint(g_mkdir(second.c_str(), 0700), ==, 0);
    g_assert_cmpint(g_mkdir(outside.c_str(), 0700), ==, 0);
    write_file(outside + "/x", std::string(70, 'x'));

    auto created = filescope::mon::Watcher::create();
    g_assert_true(created.ok());
    auto watcher = std::move(created).value();
    std::vector<filescope::core::EventPtr> events;
    watcher->signal_events().connect(
        [&events](const auto &batch) { events.insert(events.end(), batch.begin(), batch.end()); });
    auto first_id = watcher->add_root(first, filescope::mon::WatchMode::RECURSIVE);
    auto second_id = watcher->add_root(second, filescope::mon::WatchMode::NON_RECURSIVE);
    g_assert_true(first_id.ok());
    g_assert_true(second_id.ok());
    g_assert_true(filescope::test::wait_until(
        [&] {
            return watcher->root_total(first_id.value()).has_value() &&
                   watcher->root_total(second_id.value()).has_value();
        },
        2000));

    const std::string first_file = first + "/busy";
    const std::string second_file = second + "/other";
    const int fd = g_open(first_file.c_str(), O_CREAT | O_WRONLY | O_CLOEXEC, 0600);
    g_assert_cmpint(fd, >=, 0);
    const std::array<char, 4096> block{};
    for (int index = 0; index < 100; ++index) {
        g_assert_cmpint(write(fd, block.data(), block.size()), ==,
                        static_cast<ssize_t>(block.size()));
    }
    g_assert_cmpint(close(fd), ==, 0);
    write_file(second_file, "2");
    g_assert_true(filescope::test::wait_until(
        [&] {
            return has_kind(events, EventKind::CLOSE_WRITE, first_file) &&
                   has_kind(events, EventKind::CREATE, second_file);
        },
        2000));
    g_assert_true(std::any_of(events.begin(), events.end(), [&](const auto &event) {
        return event->path == first_file && event->watch_id == first_id.value();
    }));
    g_assert_true(std::any_of(events.begin(), events.end(), [&](const auto &event) {
        return event->path == second_file && event->watch_id == second_id.value();
    }));
    const auto modify_count = std::count_if(events.begin(), events.end(), [&](const auto &event) {
        return event->kind == EventKind::MODIFY && event->path == first_file;
    });
    g_assert_cmpint(modify_count, <, 100);

    const std::string incoming = first + "/incoming";
    g_assert_cmpint(g_rename(outside.c_str(), incoming.c_str()), ==, 0);
    g_assert_true(filescope::test::wait_until(
        [&] { return has_kind(events, EventKind::MOVED_IN, incoming); }, 2000));
    const auto moved = std::find_if(events.begin(), events.end(), [&](const auto &event) {
        return event->kind == EventKind::MOVED_IN && event->path == incoming;
    });
    g_assert_true(moved != events.end());
    g_assert_cmpint((*moved)->size_delta.value_or(0), ==, 70);

    filescope::mon::testing::set_queue_capacity(*watcher, 16);
    for (int index = 0; index < 1000; ++index) {
        write_file(first + "/q" + std::to_string(index), "q");
    }
    g_assert_true(
        filescope::test::wait_until([&] { return has_kind(events, EventKind::OVERFLOW); }, 2000));
}

void test_root_terminal_events_and_destruction() {
    filescope::test::TestEnvironment environment;
    const std::string deleted = environment.root() + "/deleted";
    const std::string moved = environment.root() + "/moved";
    const std::string destination = environment.root() + "/destination";
    g_assert_cmpint(g_mkdir(deleted.c_str(), 0700), ==, 0);
    g_assert_cmpint(g_mkdir(moved.c_str(), 0700), ==, 0);

    auto created = filescope::mon::Watcher::create();
    g_assert_true(created.ok());
    auto watcher = std::move(created).value();
    std::vector<filescope::core::EventPtr> events;
    std::vector<filescope::mon::WatchReason> reasons;
    watcher->signal_events().connect(
        [&events](const auto &batch) { events.insert(events.end(), batch.begin(), batch.end()); });
    watcher->signal_state().connect(
        [&reasons](const auto &info, const auto &) { reasons.push_back(info.reason); });
    auto deleted_id = watcher->add_root(deleted, filescope::mon::WatchMode::NON_RECURSIVE);
    auto moved_id = watcher->add_root(moved, filescope::mon::WatchMode::NON_RECURSIVE);
    g_assert_true(deleted_id.ok());
    g_assert_true(moved_id.ok());
    g_assert_true(filescope::test::wait_until(
        [&] {
            return watcher->root_total(deleted_id.value()).has_value() &&
                   watcher->root_total(moved_id.value()).has_value();
        },
        2000));
    g_assert_cmpint(g_rmdir(deleted.c_str()), ==, 0);
    g_assert_cmpint(g_rename(moved.c_str(), destination.c_str()), ==, 0);
    g_assert_true(filescope::test::wait_until(
        [&] {
            return has_kind(events, EventKind::DELETE_SELF, deleted) &&
                   has_kind(events, EventKind::MOVE_SELF, moved);
        },
        2000));
    g_assert_true(std::find(reasons.begin(), reasons.end(),
                            filescope::mon::WatchReason::ROOT_DELETED) != reasons.end());
    g_assert_true(std::find(reasons.begin(), reasons.end(),
                            filescope::mon::WatchReason::ROOT_MOVED) != reasons.end());

    const std::size_t delivered = events.size();
    watcher.reset();
    while (Glib::MainContext::get_default()->iteration(false)) {
    }
    g_assert_cmpuint(events.size(), ==, delivered);
}

} // namespace

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, nullptr);
    g_test_add_func("/mon/integration/basic-events-size", test_basic_events_and_size_index);
    g_test_add_func("/mon/integration/pause-keeps-index",
                    test_pause_keeps_index_and_dynamic_watches);
    g_test_add_func("/mon/integration/recursive-moves-overflow-remove",
                    test_recursive_moves_overflow_and_remove);
    g_test_add_func("/mon/integration/multiple-roots-merge-moved-in-queue-gap",
                    test_multiple_roots_merge_moved_in_and_queue_gap);
    g_test_add_func("/mon/integration/root-terminal-events-destruction",
                    test_root_terminal_events_and_destruction);
    return g_test_run();
}
