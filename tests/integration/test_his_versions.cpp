#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#include <glib.h>
#include <glibmm/init.h>
#include <sys/stat.h>

#include "his/his_testing.hpp"
#include "mon/mon.hpp"
#include "test_support.hpp"

namespace {

using namespace filescope;
using namespace std::chrono_literals;
namespace disk = std::filesystem;
using Clock = std::chrono::steady_clock;
using core::ErrorCode;
using core::EventKind;
using his::Origin;

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): path, then file contents.
void write_file(const std::string &path, const std::string &content) {
    std::ofstream out(path, std::ios::binary);
    out << content;
    out.close();
    g_assert_true(out.good());
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): path, then expected contents.
void expect_file(const std::string &path, const std::string &content) {
    std::ifstream input(path, std::ios::binary);
    g_assert_true(input.good());
    const std::string actual{std::istreambuf_iterator<char>(input), {}};
    g_assert_true(actual == content);
}

template <typename T, typename F> core::Result<T> await(F start) {
    std::optional<core::Result<T>> result;
    unsigned calls = 0;
    const auto owner = std::this_thread::get_id();
    start([&](core::Result<T> value) {
        g_assert_true(std::this_thread::get_id() == owner);
        ++calls;
        result.emplace(std::move(value));
    });
    if (!test::wait_until([&] { return result.has_value(); }, 2000) || !result) {
        g_error("Asynchronous history operation timed out");
    }
    g_assert_cmpuint(calls, ==, 1);
    return std::move(*result);
}

template <typename T> void expect_error(const core::Result<T> &result, ErrorCode expected) {
    g_assert_false(result.ok());
    g_assert_true(result.error().code == expected);
}

struct Fixture {
    test::TestEnvironment environment;
    const std::string root = environment.root() + "/watch";
    const std::string data = root + "/history";
    std::unique_ptr<his::History> history;
    std::unique_ptr<mon::Watcher> watcher;
    std::vector<his::Version> added;
    std::vector<core::EventPtr> observed;
    mon::WatchState state = mon::WatchState::REGISTERING;
    unsigned scans = 0;

    Fixture() { g_assert_true(disk::create_directory(root)); }

    void start(const his::Limits &limits = {}, mon::WatchMode mode = mon::WatchMode::RECURSIVE) {
        const auto owner = std::this_thread::get_id();
        auto opened = his::History::open({data, limits});
        g_assert_true(opened.ok());
        history = std::move(opened).value();
        history->signal_version_added().connect([this, owner](const his::Version &version) {
            g_assert_true(std::this_thread::get_id() == owner);
            added.push_back(version);
        });
        auto created = mon::Watcher::create();
        g_assert_true(created.ok());
        watcher = std::move(created).value();
        watcher->signal_events().connect([this](const auto &batch) {
            history->record_events(batch);
            observed.insert(observed.end(), batch.begin(), batch.end());
        });
        watcher->signal_state().connect([this](const mon::WatchInfo &info, const auto &) {
            if ((info.state == mon::WatchState::ACTIVE ||
                 info.state == mon::WatchState::DEGRADED) &&
                (state == mon::WatchState::REGISTERING || state == mon::WatchState::RESYNCING ||
                 state == mon::WatchState::PAUSED)) {
                history->scan_root(info.path, info.mode == mon::WatchMode::RECURSIVE);
                ++scans;
            }
            state = info.state;
        });
        g_assert_true(watcher->add_root(root, mode).ok());
        g_assert_true(test::wait_until([&] { return scans > 0; }, 2000));
    }

    void close() {
        watcher.reset();
        history.reset();
        scans = 0;
        state = mon::WatchState::REGISTERING;
        observed.clear();
        added.clear();
    }

    ~Fixture() {
        watcher.reset();
        history.reset();
    }

    his::FileHistory load(const std::string &path) {
        auto result =
            await<his::FileHistory>([&](auto done) { history->load_history_async(path, done); });
        g_assert_true(result.ok());
        return std::move(result).value();
    }

    void expect_version(const std::string &path, std::uint32_t number, Origin origin,
                        const std::string &content) {
        const auto found = std::find_if(added.begin(), added.end(), [&](const auto &version) {
            return version.path == path && version.number == number;
        });
        g_assert_true(found != added.end());
        g_assert_true(found->origin == origin);
        const auto version_id = found->id;
        auto read =
            await<std::string>([&](auto done) { history->read_content_async(version_id, done); });
        g_assert_true(read.ok());
        g_assert_true(read.value() == content);
    }

    void wait_versions(std::size_t count) {
        g_assert_true(test::wait_until([&] { return added.size() >= count; }, 2000));
        g_assert_cmpuint(added.size(), ==, count);
    }

    void wait_event(EventKind kind, const std::string &path) {
        g_assert_true(test::wait_until(
            [&] {
                return std::any_of(observed.begin(), observed.end(), [&](const auto &event) {
                    return event->kind == kind && event->path == path;
                });
            },
            2000));
    }

    core::Result<fs::FileStamp>
    restore(his::VersionId id, fs::Confirmation confirmation = fs::Confirmation::CONFIRMED) {
        return await<fs::FileStamp>(
            [&](auto done) { history->restore_async(id, confirmation, done); });
    }

    core::Result<fs::FileStamp> recover(his::VersionId id) {
        return await<fs::FileStamp>([&](auto done) { history->recover_async(id, done); });
    }

    his::testing::Rows query(const char *sql) const {
        auto result = his::testing::query(data + "/history.db", sql);
        g_assert_true(result.ok());
        return std::move(result).value();
    }
};

void test_auto() {
    Fixture fixture;
    const auto file = fixture.root + "/notes.md";
    write_file(file, "a\n");
    fixture.start();
    fixture.wait_versions(1);
    fixture.expect_version(file, 1, Origin::BASELINE, "a\n");

    write_file(file, "b\n");
    fixture.wait_versions(2);
    fixture.expect_version(file, 2, Origin::AUTO, "b\n");

    const auto start = Clock::now();
    for (int i = 0; i < 10; ++i) {
        write_file(file, "save " + std::to_string(i));
    }
    g_assert_true(Clock::now() - start < 300ms);
    fixture.wait_versions(3);
    fixture.expect_version(file, 3, Origin::AUTO, "save 9");
    fixture.observed.clear();
    write_file(file, "save 9");
    fixture.wait_event(EventKind::CLOSE_WRITE, file);
    his::testing::flush(*fixture.history);
    g_assert_cmpuint(fixture.load(file).versions.size(), ==, 3);

    const auto temporary = fixture.root + "/.goutputstream-x";
    write_file(temporary, "atomic");
    disk::rename(temporary, file);
    fixture.wait_versions(4);
    fixture.expect_version(file, 4, Origin::AUTO, "atomic");
    g_assert_true(fixture.load(temporary).versions.empty());

    const auto outside = fixture.environment.root() + "/saved";
    const auto imported = fixture.root + "/imported";
    write_file(outside, "already written");
    disk::rename(outside, imported);
    fixture.wait_versions(5);
    fixture.expect_version(imported, 1, Origin::AUTO, "already written");
    fixture.wait_event(EventKind::MOVED_IN, imported);

    const auto moved = fixture.root + "/moved.md";
    disk::rename(file, moved);
    fixture.wait_versions(6);
    fixture.expect_version(moved, 1, Origin::AUTO, "atomic");
    const auto old = fixture.load(file);
    g_assert_cmpuint(old.versions.size(), ==, 4);
    g_assert_true(old.deleted_at_us.has_value());
}

void test_excluded_and_unrecoverable() {
    Fixture fixture;
    const std::vector<std::string> excluded{
        fixture.root + "/.git/config", fixture.root + "/.ssh/id_rsa",
        fixture.root + "/node_modules/x.js", fixture.root + "/a.swp", fixture.data + "/own"};
    for (const auto &path : excluded) {
        disk::create_directories(disk::path(path).parent_path());
    }
    const auto huge = fixture.root + "/large";
    fixture.start();
    for (const auto &path : excluded) {
        write_file(path, "secret");
        fixture.wait_event(EventKind::CLOSE_WRITE, path);
    }
    write_file(huge, std::string(8ULL * 1024 * 1024 + 1, 'x'));
    fixture.wait_event(EventKind::CLOSE_WRITE, huge);
    his::testing::flush(*fixture.history);
    g_assert_true(
        fixture.query("SELECT count(*) FROM events WHERE CAST(path AS TEXT) LIKE '%/history/%'") ==
        his::testing::Rows{{"0"}});
    g_assert_true(fixture.query("SELECT count(*) FROM events WHERE kind=3 AND CAST(path AS TEXT) "
                                "LIKE '%/.git/config'") == his::testing::Rows{{"1"}});
    for (const auto &path : excluded) {
        g_assert_true(fixture.load(path).versions.empty());
    }
    disk::remove(excluded.front());
    disk::remove(huge);
    fixture.wait_event(EventKind::DELETE, excluded.front());
    fixture.wait_event(EventKind::DELETE, huge);
    for (const auto &path : {excluded.front(), huge}) {
        const auto entry = fixture.load(path);
        g_assert_true(entry.versions.empty());
        g_assert_false(entry.deleted_at_us.has_value());
    }
    g_assert_true(fixture.added.empty());
}

void test_baseline_and_pause() {
    Fixture fixture;
    const auto file = fixture.root + "/first";
    const auto unchanged = fixture.root + "/second";
    write_file(file, "a");
    write_file(unchanged, "same");
    write_file(fixture.root + "/third", "third");
    disk::create_directory(fixture.root + "/nested");
    write_file(fixture.root + "/nested/fourth", "fourth");
    const auto large = fixture.root + "/large";
    write_file(large, std::string(2ULL * 1024 * 1024, 'x'));
    fixture.start();
    fixture.wait_versions(4);
    g_assert_true(fixture.load(large).versions.empty());
    fixture.close();

    write_file(file, "while closed");
    fixture.start();
    fixture.wait_versions(1);
    fixture.expect_version(file, 2, Origin::BASELINE, "while closed");
    g_assert_true(
        fixture.query("SELECT count(*) FROM gaps WHERE kind=2 AND ended_at_us IS NOT NULL") ==
        his::testing::Rows{{"1"}});
    g_assert_cmpuint(fixture.load(unchanged).versions.size(), ==, 1);
    g_assert_true(fixture.load(unchanged).gap_after_latest);
    g_assert_false(fixture.load(file).gap_after_latest);

    fixture.watcher->pause();
    fixture.history->note_pause(true);
    g_assert_true(test::wait_until([&] { return fixture.state == mon::WatchState::PAUSED; }, 2000));
    fixture.observed.clear();
    write_file(file, "while paused");
    fixture.watcher->resume();
    fixture.history->note_pause(false);
    fixture.wait_versions(2);
    fixture.expect_version(file, 3, Origin::BASELINE, "while paused");
    g_assert_true(
        std::none_of(fixture.observed.begin(), fixture.observed.end(), [&](const auto &event) {
            return event->path == file && event->kind == EventKind::CLOSE_WRITE;
        }));
    g_assert_true(
        fixture.query("SELECT count(*) FROM gaps WHERE kind=1 AND ended_at_us IS NOT NULL") ==
        his::testing::Rows{{"1"}});
    g_assert_true(fixture.load(unchanged).gap_after_latest);
    g_assert_false(fixture.load(file).gap_after_latest);

    Fixture limited;
    for (int i = 0; i < 5; ++i) {
        write_file(limited.root + "/" + std::to_string(i), "x");
    }
    disk::create_directory(limited.root + "/nested");
    write_file(limited.root + "/nested/ignored", "x");
    his::Limits limits;
    limits.baseline_max_files = 3;
    limited.start(limits, mon::WatchMode::NON_RECURSIVE);
    limited.wait_versions(3);
    g_assert_true(limited.load(limited.root + "/nested/ignored").versions.empty());
    g_assert_true(limited.query("SELECT count(*) FROM versions") == his::testing::Rows{{"3"}});
}

void test_restore_and_recover() {
    Fixture fixture;
    const auto folder = fixture.root + "/folder";
    disk::create_directory(folder);
    const auto file = folder + "/notes";
    write_file(file, "a");
    g_assert_cmpint(chmod(file.c_str(), 0640), ==, 0);
    fixture.start();
    fixture.wait_versions(1);
    const auto original = fixture.added.front().id;
    write_file(file, "b");
    g_assert_cmpint(chmod(file.c_str(), 0600), ==, 0);
    fixture.wait_versions(2);
    expect_error(fixture.restore(original, fs::Confirmation::NONE), ErrorCode::NOT_CONFIRMED);
    expect_file(file, "b");
    g_assert_true(fixture.restore(original).ok());
    expect_file(file, "a");
    struct stat info {};
    g_assert_cmpint(stat(file.c_str(), &info), ==, 0);
    g_assert_cmpuint(info.st_mode & 0777, ==, 0640);
    fixture.wait_versions(3);
    fixture.expect_version(file, 2, Origin::AUTO, "b");
    fixture.expect_version(file, 3, Origin::AUTO, "a");

    write_file(file, "not captured yet");
    g_assert_true(fixture.restore(original).ok());
    fixture.wait_versions(5);
    fixture.expect_version(file, 4, Origin::PRE_RESTORE, "not captured yet");
    fixture.expect_version(file, 5, Origin::AUTO, "a");

    write_file(file, std::string(8ULL * 1024 * 1024 + 1, 'x'));
    expect_error(fixture.restore(original), ErrorCode::TOO_LARGE);
    g_assert_cmpuint(disk::file_size(file), ==, 8ULL * 1024 * 1024 + 1);
    disk::remove(file);
    fixture.wait_event(EventKind::DELETE, file);
    expect_error(fixture.restore(original), ErrorCode::NOT_FOUND);
    g_assert_true(fixture.load(file).deleted_at_us.has_value());
    g_assert_true(fixture.recover(original).ok());
    expect_file(file, "a");
    g_assert_false(fixture.load(file).deleted_at_us.has_value());
    write_file(file, "new owner");
    expect_error(fixture.recover(original), ErrorCode::EXISTS);
    expect_file(file, "new owner");
    disk::remove_all(folder);
    expect_error(fixture.recover(original), ErrorCode::NOT_FOUND);
}

void test_shutdown() {
    Fixture fixture;
    const auto pending = fixture.root + "/pending";
    his::Limits limits;
    limits.capture_delay_ms = 10000;
    fixture.start(limits);
    write_file(pending, "waiting");
    fixture.wait_event(EventKind::CLOSE_WRITE, pending);
    const auto scan = fixture.environment.root() + "/scan";
    disk::create_directory(scan);
    for (int i = 0; i < 2000; ++i) {
        write_file(scan + "/" + std::to_string(i), "scan");
    }
    fixture.history->scan_root(scan, true);
    g_assert_true(test::wait_until([&] { return !fixture.added.empty(); }, 2000));
    fixture.history->shutdown();
    const auto deliveries = fixture.added.size();
    static_cast<void>(test::wait_until([&] { return fixture.added.size() != deliveries; }, 50));
    g_assert_cmpuint(fixture.added.size(), ==, deliveries);
    g_assert_true(disk::is_empty(fixture.data + "/tmp"));
    g_assert_true(
        fixture.query("SELECT count(*) FROM versions WHERE CAST(path AS TEXT) LIKE '%/pending'") ==
        his::testing::Rows{{"0"}});
    g_assert_true(fixture.query("SELECT count(*)<2000 FROM versions") == his::testing::Rows{{"1"}});
}

void test_restore_prunes_selected_version() {
    Fixture fixture;
    his::Limits limits;
    limits.max_versions_per_file = 1;
    const auto file = fixture.root + "/notes";
    write_file(file, "selected");
    fixture.start(limits);
    fixture.wait_versions(1);
    const auto id = fixture.added.front().id;
    write_file(file, "current");
    g_assert_true(fixture.restore(id).ok());
    expect_file(file, "selected");
    fixture.wait_versions(2);
    fixture.expect_version(file, 2, Origin::PRE_RESTORE, "current");
    auto old =
        await<std::string>([&](auto done) { fixture.history->read_content_async(id, done); });
    expect_error(old, ErrorCode::NOT_FOUND);
}

} // namespace

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, nullptr);
    Glib::init();
    g_test_add_func("/his/versions/auto", test_auto);
    g_test_add_func("/his/versions/excluded", test_excluded_and_unrecoverable);
    g_test_add_func("/his/versions/baseline-pause", test_baseline_and_pause);
    g_test_add_func("/his/versions/restore-recover", test_restore_and_recover);
    g_test_add_func("/his/versions/restore-retention", test_restore_prunes_selected_version);
    g_test_add_func("/his/versions/shutdown", test_shutdown);
    return g_test_run();
}
