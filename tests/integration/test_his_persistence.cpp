#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <random>
#include <string_view>
#include <thread>

#include <glib.h>
#include <glibmm/init.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "core/fd.hpp"
#include "his/his_testing.hpp"
#include "mon/mon.hpp"
#include "test_support.hpp"

extern char **environ;

namespace {

namespace disk = std::filesystem;
using namespace filescope;
using his::testing::flush;
using his::testing::Rows;
using namespace std::chrono_literals;

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): test helper takes a path, then contents.
void write_file(const std::string &path, const std::string &contents) {
    std::ofstream stream(path, std::ios::binary);
    stream << contents;
    stream.close();
    g_assert_true(stream.good());
}

std::unique_ptr<his::History> open_history(const std::string &data,
                                           const his::Limits &limits = {}) {
    auto result = his::History::open({data, limits});
    if (!result) {
        g_error("History open failed: %s", result.error().message.c_str());
    }
    return std::move(result).value();
}

Rows sql(const std::string &data, const std::string &statement) {
    auto result = his::testing::query(data + "/history.db", statement);
    if (!result) {
        g_error("Fixture SQL failed: %s", result.error().message.c_str());
    }
    return std::move(result).value();
}

const std::string &required(const std::optional<std::string> &cell) {
    if (cell) {
        return *cell;
    }
    g_error("Expected a non-NULL SQLite column");
}

core::EventPtr make_event(const std::string &path, std::uint64_t seq) {
    core::Event event;
    event.path = path;
    event.kind = core::EventKind::MODIFY;
    event.observed_at_us = g_get_real_time();
    event.seq = seq;
    return core::make_event(std::move(event));
}

void test_mon_persistence() {
    test::TestEnvironment environment;
    const auto data = environment.root() + "/history";
    const auto watched = environment.root() + "/watch";
    g_assert_true(disk::create_directory(watched));
    auto history = open_history(data);
    auto created = mon::Watcher::create();
    g_assert_true(created.ok());
    auto watcher = std::move(created).value();
    bool active = false;
    watcher->signal_state().connect([&](const mon::WatchInfo &info, const auto &) {
        active = info.state == mon::WatchState::ACTIVE;
    });
    unsigned writes = 0;
    const auto file = watched + "/binary-\xff";
    watcher->signal_events().connect([&](const auto &events) {
        history->record_events(events);
        for (const auto &event : events) {
            if (event->kind == core::EventKind::CLOSE_WRITE && event->path == file) {
                ++writes;
            }
        }
    });
    g_assert_true(watcher->add_root(watched, mon::WatchMode::RECURSIVE).ok());
    g_assert_true(test::wait_until([&] { return active; }, 3000));
    write_file(file, "new");
    g_assert_true(test::wait_until([&] { return writes > 0; }, 3000));
    watcher.reset();
    history.reset();
    history = open_history(data);
    flush(*history);
    const Rows persisted{{file}};
    g_assert_true(sql(data, "SELECT path FROM events WHERE kind=3") == persisted);
}

void test_busy_retry_and_recovery() {
    test::TestEnvironment environment;
    const auto data = environment.root() + "/history";
    auto history = open_history(data);
    flush(*history);
    std::vector<his::State> states;
    history->signal_state().connect([&](his::State state, const auto &error) {
        g_assert_true((state == his::State::DEGRADED) == error.has_value());
        if (error) {
            g_assert_true(error->code == core::ErrorCode::IO);
        }
        states.push_back(state);
    });
    const auto writer = his::testing::query(data + "/history.db", "BEGIN EXCLUSIVE", [&] {
        const auto start = std::chrono::steady_clock::now();
        history->record_events({make_event(environment.root(), 1)});
        g_assert_true(test::wait_until([&] { return !states.empty(); }, 16000));
        g_assert_true(std::chrono::steady_clock::now() - start >= 8s);
        g_assert_true(history->state() == his::State::DEGRADED);
    });
    g_assert_true(writer.ok());
    history->record_events({make_event(environment.root(), 2)});
    g_assert_true(test::wait_until([&] { return states.size() == 2; }, 3000));
    g_assert_true(history->state() == his::State::READY);
    const Rows retained{{"2"}};
    const Rows backlog{{"3"}};
    g_assert_true(sql(data, "SELECT seq FROM events") == retained);
    g_assert_true(sql(data, "SELECT kind FROM gaps WHERE ended_at_us IS NOT NULL") == backlog);
}

void test_shutdown_and_backlog() {
    test::TestEnvironment environment;
    const auto data = environment.root() + "/history";
    const auto file = environment.root() + "/file";
    write_file(file, "initial");
    his::Limits limits;
    limits.max_pending_events = 10;
    auto history = open_history(data, limits);
    flush(*history);
    unsigned deliveries = 0;
    history->signal_version_added().connect([&](const auto &) { ++deliveries; });
    history->signal_state().connect([&](his::State, const auto &) { ++deliveries; });

    std::promise<void> copying;
    std::promise<void> release;
    auto copied = copying.get_future();
    auto resume = release.get_future();
    his::testing::capture(*history, file, his::Origin::AUTO, [&] {
        copying.set_value();
        resume.wait();
    });
    g_assert_true(copied.wait_for(3s) == std::future_status::ready);
    for (unsigned i = 0; i < 1000; ++i) {
        history->record_events({make_event(file, i)});
        history->load_history_async(file, [&](auto) { ++deliveries; });
    }
    g_assert_cmpuint(his::testing::pending_events(*history), ==, limits.max_pending_events);
    release.set_value();
    history->shutdown();
    history->load_history_async(file, [&](auto) { ++deliveries; });
    while (g_main_context_iteration(nullptr, false)) {
    }
    g_assert_cmpuint(deliveries, ==, 0);
    const Rows ten{{"10", "0", "9"}};
    g_assert_true(sql(data, "SELECT count(*),min(seq),max(seq) FROM events") == ten);
    const auto dropped = sql(data, "SELECT count(*) FROM gaps WHERE kind=3");
    g_assert_cmpuint(std::stoul(required(dropped.front().front())), >, 0);
    const Rows ended{{"1"}};
    g_assert_true(sql(data, "SELECT count(*) FROM sessions WHERE ended_at_us IS NOT NULL") ==
                  ended);
}

void assert_invariants(const std::string &data, bool allow_unexpected = false) {
    const Rows ok{{"ok"}};
    const Rows zero{{"0"}};
    g_assert_true(sql(data, "PRAGMA integrity_check") == ok);
    g_assert_true(sql(data, "PRAGMA foreign_key_check").empty());
    g_assert_true(sql(data, "SELECT count(*) FROM blobs b WHERE b.ref_count != "
                            "(SELECT count(*) FROM versions v WHERE v.sha256=b.sha256)") == zero);
    g_assert_true(disk::is_empty(data + "/tmp"));

    const auto blobs = sql(data, "SELECT lower(hex(sha256)),size FROM blobs");
    for (const auto &row : blobs) {
        const auto &hash = required(row[0]);
        auto path = data + "/blobs/";
        path.append(hash, 0, 2).append("/").append(hash);
        g_assert_true(disk::is_regular_file(path));
        g_assert_cmpuint(disk::file_size(path), ==, std::stoull(required(row[1])));
    }
    std::size_t count = 0;
    for (const auto &entry : disk::recursive_directory_iterator(data + "/blobs")) {
        if (entry.is_regular_file()) {
            if (allow_unexpected && entry.path().filename() == "keep-me") {
                continue;
            }
            ++count;
        }
    }
    g_assert_cmpuint(count, ==, blobs.size());
}

void test_orphans_and_missing() {
    test::TestEnvironment environment;
    const auto data = environment.root() + "/history";
    const auto file = environment.root() + "/file";
    write_file(file, "bytes");
    {
        auto history = open_history(data);
        his::testing::capture(*history, file);
        flush(*history);
    }
    const auto rows = sql(data, "SELECT lower(hex(sha256)) FROM blobs");
    g_assert_cmpuint(rows.size(), ==, 1);
    const auto &hash = required(rows.front().front());
    const auto shard = data + "/blobs/" + hash.substr(0, 2);
    g_assert_true(disk::remove(shard + "/" + hash));
    const auto orphan = data + "/blobs/aa/" + std::string(64, 'a');
    g_assert_true(disk::create_directory(data + "/blobs/aa"));
    write_file(orphan, "orphan");
    write_file(shard + "/keep-me", "ignored");
    write_file(data + "/tmp/abandoned", "partial");

    auto history = open_history(data);
    flush(*history);
    g_assert_false(disk::exists(orphan));
    g_assert_true(disk::exists(shard + "/keep-me"));
    g_assert_true(sql(data, "SELECT id FROM versions").empty());
    assert_invariants(data, true);
    write_file(file, "new version");
    his::testing::capture(*history, file);
    flush(*history);
    const Rows number{{"2"}};
    g_assert_true(sql(data, "SELECT number FROM versions") == number);

    history.reset();
    sql(data, "UPDATE blobs SET ref_count=17");
    history = open_history(data);
    flush(*history);
    assert_invariants(data, true);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): mirrors the helper process arguments.
int crash_child(const std::string &data, const std::string &work, const std::string &mode,
                int ready_fd) {
    const core::UniqueFd ready(ready_fd);
    his::Limits limits;
    limits.max_versions_per_file = 1;
    auto history = open_history(data, limits);
    flush(*history);
    const auto file = work + "/source";
    if (mode == "rename") {
        his::testing::set_fault(*history, his::testing::FaultPoint::AFTER_BLOB_RENAME);
    } else if (mode == "commit") {
        his::testing::set_fault(*history, his::testing::FaultPoint::AFTER_COMMIT_BEFORE_UNLINK);
    }

    const auto base = std::to_string(g_get_real_time());
    for (unsigned i = 0;; ++i) {
        const auto version = base + "/" + std::to_string(i);
        write_file(file, version + std::string(65536, 'a'));
        history->record_events({make_event(file, i)});
        his::testing::capture(*history, file);
        flush(*history);
        if (i == 0) {
            if (mode != "kill") {
                return 1;
            }
            g_assert_cmpint(write(ready.get(), "r", 1), ==, 1);
        }
    }
}

void run_crash(const std::string &data, const std::string &work, const std::string &mode,
               std::chrono::microseconds delay = 0us) {
    std::array<int, 2> pipe_fds{};
    g_assert_cmpint(pipe(pipe_fds.data()), ==, 0);
    const core::UniqueFd reader(pipe_fds[0]);
    core::UniqueFd writer(pipe_fds[1]);
    const std::string exe = "/proc/self/exe";
    std::array<std::string, 6> args{exe,  "--history-crash-child",     data, work,
                                    mode, std::to_string(writer.get())};
    std::array<char *, 7> argv{};
    for (std::size_t i = 0; i < args.size(); ++i) {
        argv[i] = args[i].data();
    }
    pid_t child = 0;
    g_assert_cmpint(posix_spawn(&child, exe.c_str(), nullptr, nullptr, argv.data(), environ), ==,
                    0);
    writer.reset();
    if (mode == "kill") {
        pollfd descriptor{reader.get(), POLLIN, 0};
        g_assert_cmpint(poll(&descriptor, 1, 10000), ==, 1);
        char byte = 0;
        g_assert_cmpint(read(reader.get(), &byte, 1), ==, 1);
        g_assert_cmpint(byte, ==, 'r');
        std::this_thread::sleep_for(delay);
        g_assert_cmpint(kill(child, SIGKILL), ==, 0);
    }
    int status = 0;
    g_assert_cmpint(waitpid(child, &status, 0), ==, child);
    if (mode == "kill") {
        g_assert_true(WIFSIGNALED(status));
        g_assert_cmpint(WTERMSIG(status), ==, SIGKILL);
    } else {
        g_assert_true(WIFEXITED(status));
        g_assert_cmpint(WEXITSTATUS(status), ==, 77);
    }
}

void test_crash_recovery() {
    test::TestEnvironment environment;
    const auto data = environment.root() + "/history";
    std::mt19937 random(14004);
    std::uniform_int_distribution<int> delay(0, 15000);

    for (unsigned i = 0; i < 22; ++i) {
        const std::string mode = i < 20 ? "kill" : (i == 20 ? "rename" : "commit");
        run_crash(data, environment.root(), mode, std::chrono::microseconds(delay(random)));

        const auto previous = sql(data, "SELECT id,started_at_us,ended_at_us FROM sessions "
                                        "ORDER BY id DESC LIMIT 1");
        g_assert_cmpuint(previous.size(), ==, 1);
        g_assert_false(previous[0][2].has_value());
        const auto end = sql(data, "SELECT COALESCE(max(observed_at_us),"
                                   "(SELECT started_at_us FROM sessions ORDER BY id DESC LIMIT 1)) "
                                   "FROM events WHERE session_id=(SELECT max(id) FROM sessions)");
        if (mode == "rename" || mode == "commit") {
            std::size_t stored_files = 0;
            for (const auto &entry : disk::recursive_directory_iterator(data + "/blobs")) {
                if (entry.is_regular_file()) {
                    ++stored_files;
                }
            }
            g_assert_cmpuint(stored_files, ==, sql(data, "SELECT sha256 FROM blobs").size() + 1);
        }
        auto history = open_history(data);
        flush(*history);
        assert_invariants(data);
        const auto recovered =
            sql(data, "SELECT ended_at_us FROM sessions WHERE id=" + required(previous[0][0]));
        g_assert_true(recovered == end);
        const Rows expected_gap{{end.front().front(), std::nullopt}};
        g_assert_true(sql(data, "SELECT started_at_us,ended_at_us FROM gaps WHERE kind=2 "
                                "ORDER BY id DESC LIMIT 1") == expected_gap);
    }
    g_test_message("Recovered after 20 SIGKILLs and both injected _exit(77) points");
}

} // namespace

int main(int argc, char **argv) {
    Glib::init();
    if (argc == 6 && std::string_view(argv[1]) == "--history-crash-child") {
        return crash_child(argv[2], argv[3], argv[4], std::stoi(argv[5]));
    }
    g_test_init(&argc, &argv, nullptr);
    g_test_add_func("/his/integration/inotify", test_mon_persistence);
    g_test_add_func("/his/integration/busy", test_busy_retry_and_recovery);
    g_test_add_func("/his/integration/shutdown", test_shutdown_and_backlog);
    g_test_add_func("/his/integration/orphans", test_orphans_and_missing);
    g_test_add_func("/his/integration/crash", test_crash_recovery);
    return g_test_run();
}
