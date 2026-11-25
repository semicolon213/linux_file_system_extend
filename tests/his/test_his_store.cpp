#include <filesystem>
#include <fstream>
#include <thread>

#include <glib.h>
#include <glibmm/init.h>
#include <sys/stat.h>

#include "his/his_testing.hpp"
#include "test_support.hpp"

namespace {

namespace disk = std::filesystem;
using namespace filescope;
using his::testing::flush;
using his::testing::Rows;

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): test helper takes a path, then contents.
void write_file(const std::string &path, const std::string &bytes) {
    std::ofstream file(path, std::ios::binary);
    file << bytes;
    file.close();
    g_assert_true(file.good());
}

std::string read_file(const std::string &path) {
    std::ifstream input(path, std::ios::binary);
    g_assert_true(input.good());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

mode_t permissions(const std::string &path) {
    struct stat info {};
    g_assert_cmpint(lstat(path.c_str(), &info), ==, 0);
    return info.st_mode & 07777;
}

struct Fixture {
    test::TestEnvironment environment;
    std::string data = environment.root() + "/history";
    std::string database = data + "/history.db";
    his::Limits limits;
    std::unique_ptr<his::History> history;

    void open() {
        auto result = his::History::open({data, limits});
        if (!result) {
            g_error("History open failed: %s", result.error().message.c_str());
        }
        history = std::move(result).value();
        flush(*history);
    }

    Rows sql(const std::string &statement) const {
        auto result = his::testing::query(database, statement);
        if (!result) {
            g_error("Fixture SQL failed: %s", result.error().message.c_str());
        }
        return std::move(result).value();
    }

    std::string path(const std::string &name) const { return environment.root() + "/" + name; }

    void capture(const std::string &file, const std::string &bytes) {
        write_file(file, bytes);
        his::testing::capture(*history, file);
        flush(*history);
    }
};

template <typename T, typename F> core::Result<T> await(F request) {
    std::optional<core::Result<T>> result;
    const auto main = std::this_thread::get_id();
    request([&](core::Result<T> value) {
        g_assert_true(main == std::this_thread::get_id());
        g_assert_false(result.has_value());
        result = std::move(value);
    });
    g_assert_true(test::wait_until([&] { return result.has_value(); }, 3000));
    if (result) {
        return std::move(*result);
    }
    g_error("History callback did not complete");
}

his::FileHistory load(his::History &history, const std::string &path) {
    auto result = await<his::FileHistory>(
        [&](his::HistorySlot done) { history.load_history_async(path, std::move(done)); });
    g_assert_true(result.ok());
    return std::move(result).value();
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): event sequence precedes its timestamp.
core::Event event(core::EventKind kind, const std::string &path, std::uint64_t seq,
                  std::int64_t at) {
    core::Event result;
    result.kind = kind;
    result.path = path;
    result.seq = seq;
    result.observed_at_us = at;
    return result;
}

void test_open_migration_permissions() {
    Fixture fixture;
    fixture.open();
    g_assert_cmpuint(permissions(fixture.data), ==, 0700);
    g_assert_cmpuint(permissions(fixture.data + "/tmp"), ==, 0700);
    g_assert_cmpuint(permissions(fixture.data + "/blobs"), ==, 0700);
    for (const auto *suffix : {"", "-wal", "-shm"}) {
        g_assert_cmpuint(permissions(fixture.database + suffix), ==, 0600);
    }
    const Rows version{{"1"}};
    const Rows required{{"blobs"},    {"events"},        {"events_path"},  {"events_time"},
                        {"files"},    {"files_deleted"}, {"gaps"},         {"sessions"},
                        {"versions"}, {"versions_age"},  {"versions_path"}};
    g_assert_true(fixture.sql("PRAGMA user_version") == version);
    g_assert_true(fixture.sql("SELECT name FROM sqlite_master WHERE name NOT LIKE 'sqlite_%' "
                              "ORDER BY name") == required);
    fixture.history.reset();
}

void test_existing_v0_and_newer() {
    Fixture fixture;
    g_assert_true(disk::create_directory(fixture.data));
    g_assert_cmpint(chmod(fixture.data.c_str(), 0755), ==, 0);
    g_assert_true(fixture.sql("PRAGMA user_version=0").empty());
    const Rows zero{{"0"}};
    g_assert_true(fixture.sql("PRAGMA user_version") == zero);
    fixture.open();
    const Rows one{{"1"}};
    g_assert_true(fixture.sql("PRAGMA user_version") == one);
    g_assert_cmpuint(permissions(fixture.data), ==, 0700);
    fixture.history.reset();

    g_assert_true(fixture.sql("PRAGMA user_version=99").empty());
    const auto before = read_file(fixture.database);
    const auto result = his::History::open({fixture.data, {}});
    g_assert_false(result.ok());
    g_assert_true(result.error().code == core::ErrorCode::NOT_SUPPORTED);
    g_assert_true(before == read_file(fixture.database));
}

void test_corrupt_reset() {
    Fixture fixture;
    g_assert_true(disk::create_directory(fixture.data));
    const std::string corrupt(200, '\xff');
    write_file(fixture.database, corrupt);
    fixture.open();
    unsigned resets = 0;
    fixture.history->signal_state().connect([&](his::State state, const auto &error) {
        g_assert_true(state == his::State::READY);
        g_assert_true(error.has_value());
        g_assert_true(error->code == core::ErrorCode::IO);
        g_assert_cmpstr(error->message.c_str(), ==, "history reset");
        ++resets;
    });
    g_assert_true(test::wait_until([&] { return resets == 1; }, 3000));
    while (g_main_context_iteration(nullptr, false)) {
    }
    g_assert_cmpuint(resets, ==, 1);
    unsigned backups = 0;
    for (const auto &entry : disk::directory_iterator(fixture.data)) {
        if (entry.path().filename().string().find("history.db.corrupt-") == 0) {
            g_assert_true(read_file(entry.path()) == corrupt);
            ++backups;
        }
    }
    g_assert_cmpuint(backups, ==, 1);
    const Rows ok{{"ok"}};
    g_assert_true(fixture.sql("PRAGMA integrity_check") == ok);
}

void test_events_and_retention() {
    Fixture fixture;
    fixture.limits.max_event_rows = 5;
    fixture.open();
    const auto path = fixture.path("raw-\xff");
    const auto now = g_get_real_time();
    auto create = event(core::EventKind::CREATE, path, 1, now);
    create.raw_mask = 256;
    create.size_delta = 4;
    auto modify = event(core::EventKind::MODIFY, path, 2, now + 1);
    modify.merged_count = 3;
    auto rename = event(core::EventKind::RENAME, fixture.path("moved"), 3, now + 2);
    rename.old_path = path;
    rename.is_dir = true;
    auto overflow = event(core::EventKind::OVERFLOW, "", 4, now + 3);
    auto resync = event(core::EventKind::RESYNC_DONE, fixture.path("root"), 5, now + 4);
    overflow.gap_id = 23;
    resync.gap_id = 23;
    auto self = event(core::EventKind::MODIFY, fixture.data + "/history.db", 6, now + 5);

    fixture.history->record_events({core::make_event(create), core::make_event(modify),
                                    core::make_event(rename), core::make_event(overflow),
                                    core::make_event(self)});
    flush(*fixture.history);
    const Rows gap_open{{"0", std::to_string(now + 3), std::nullopt}};
    g_assert_true(fixture.sql("SELECT kind,started_at_us,ended_at_us FROM gaps") == gap_open);
    fixture.history->record_events({core::make_event(resync)});
    flush(*fixture.history);

    const auto fields =
        "SELECT seq,observed_at_us,kind,path,old_path,is_dir,raw_mask,"
        "merged_count,gap_id,size_delta,typeof(path),session_id FROM events ORDER BY id";
    Rows expected;
    for (const auto *entry : {&create, &modify, &rename, &overflow, &resync}) {
        expected.push_back(
            {std::to_string(entry->seq), std::to_string(entry->observed_at_us),
             std::to_string(static_cast<int>(entry->kind)), entry->path,
             entry->old_path.empty() ? std::nullopt : std::make_optional(entry->old_path),
             entry->is_dir ? "1" : "0", std::to_string(entry->raw_mask),
             std::to_string(entry->merged_count), std::to_string(entry->gap_id),
             entry->size_delta ? std::make_optional(std::to_string(*entry->size_delta))
                               : std::nullopt,
             "blob", "1"});
    }
    g_assert_true(fixture.sql(fields) == expected);
    const Rows gap_closed{{"0", std::to_string(now + 3), std::to_string(now + 4)}};
    g_assert_true(fixture.sql("SELECT kind,started_at_us,ended_at_us FROM gaps") == gap_closed);
    fixture.history.reset();
    fixture.open();
    g_assert_true(fixture.sql(fields) == expected);
    self.path = fixture.path("another");
    fixture.history->record_events({core::make_event(self)});
    flush(*fixture.history);
    const Rows sequences{{"2"}, {"3"}, {"4"}, {"5"}, {"6"}};
    g_assert_true(fixture.sql("SELECT seq FROM events ORDER BY id") == sequences);
}

void test_capture_and_content() {
    Fixture fixture;
    fixture.limits.capture_delay_ms = 1;
    fixture.open();
    unsigned added = 0;
    fixture.history->signal_version_added().connect([&](const his::Version &) { ++added; });
    const auto a = fixture.path("A");
    const auto b = fixture.path("B");
    const std::string bytes("2\0\xff", 3);
    fixture.capture(a, "1");
    fixture.capture(a, "1");
    fixture.capture(a, bytes);
    fixture.capture(b, bytes);
    const auto history = load(*fixture.history, a);
    g_assert_cmpuint(added, ==, 3);
    g_assert_cmpuint(history.versions.size(), ==, 2);
    g_assert_cmpuint(history.versions[0].number, ==, 2);
    g_assert_cmpuint(history.versions[1].number, ==, 1);
    g_assert_true(history.versions[0].origin == his::Origin::AUTO);
    g_assert_cmpuint(history.versions[0].size, ==, bytes.size());
    g_assert_cmpuint(history.versions[0].mode & 0777, ==, permissions(a));
    g_assert_false(history.gap_after_latest);
    const Rows refs{{"1", "32", "1"}, {"2", "32", "3"}};
    g_assert_true(
        fixture.sql("SELECT ref_count,length(sha256),size FROM blobs ORDER BY ref_count") == refs);
    const auto hex = history.versions[0].sha256;
    const auto blob = fixture.data + "/blobs/" + hex.substr(0, 2) + "/" + hex;
    g_assert_cmpuint(permissions(blob), ==, 0600);
    g_assert_cmpuint(permissions(disk::path(blob).parent_path()), ==, 0700);
    auto content = await<std::string>([&](his::ContentSlot done) {
        fixture.history->read_content_async(history.versions[0].id, std::move(done));
    });
    g_assert_true(content.ok());
    g_assert_true(content.value() == bytes);
    content = await<std::string>(
        [&](his::ContentSlot done) { fixture.history->read_content_async(0, std::move(done)); });
    g_assert_false(content.ok());
    g_assert_true(content.error().code == core::ErrorCode::NOT_FOUND);
    g_assert_true(disk::remove(blob));
    content = await<std::string>([&](his::ContentSlot done) {
        fixture.history->read_content_async(history.versions[0].id, std::move(done));
    });
    g_assert_false(content.ok());
    g_assert_true(content.error().code == core::ErrorCode::NOT_FOUND);

    const auto changing = fixture.path("changing");
    write_file(changing, "x");
    unsigned attempts = 0;
    his::testing::capture(*fixture.history, changing, his::Origin::AUTO,
                          [&] { write_file(changing, std::string(++attempts + 1, 'y')); });
    flush(*fixture.history);
    g_assert_cmpuint(attempts, ==, 2);
    g_assert_true(load(*fixture.history, changing).versions.empty());
    g_assert_true(disk::is_empty(fixture.data + "/tmp"));

    const auto large = fixture.path("large");
    write_file(large, "");
    disk::resize_file(large, 8ULL * 1024 * 1024 + 1);
    his::testing::capture(*fixture.history, large);
    flush(*fixture.history);
    g_assert_true(load(*fixture.history, large).versions.empty());
}

void test_version_limits() {
    Fixture fixture;
    fixture.limits.max_versions_per_file = 3;
    fixture.open();
    const auto file = fixture.path("file");
    fixture.capture(file, "1");
    const auto first = load(*fixture.history, file).versions.front().sha256;
    for (const auto *bytes : {"2", "3", "4", "5"}) {
        fixture.capture(file, bytes);
    }
    const Rows numbers{{"3"}, {"4"}, {"5"}};
    g_assert_true(fixture.sql("SELECT number FROM versions ORDER BY number") == numbers);
    g_assert_false(disk::exists(fixture.data + "/blobs/" + first.substr(0, 2) + "/" + first));
    const Rows three{{"3"}};
    g_assert_true(fixture.sql("SELECT count(*) FROM blobs") == three);
    fixture.history.reset();

    Fixture budget;
    budget.limits.max_store_bytes = 10;
    budget.open();
    const auto a = budget.path("A");
    const auto b = budget.path("B");
    budget.capture(a, "1111");
    budget.capture(b, "2222");
    budget.capture(a, "3333");
    g_assert_cmpuint(load(*budget.history, a).versions.front().number, ==, 2);
    g_assert_cmpuint(load(*budget.history, a).versions.size(), ==, 1);
    g_assert_cmpuint(load(*budget.history, b).versions.size(), ==, 1);
    unsigned degraded = 0;
    budget.history->signal_state().connect([&](his::State state, const auto &error) {
        g_assert_true(state == his::State::DEGRADED);
        g_assert_true(error.has_value() && error->code == core::ErrorCode::NO_SPACE);
        ++degraded;
    });
    const auto c = budget.path("C");
    budget.capture(c, "4444");
    g_assert_true(test::wait_until([&] { return degraded == 1; }, 3000));
    g_assert_true(budget.history->state() == his::State::DEGRADED);
    g_assert_cmpuint(load(*budget.history, c).versions.size(), ==, 1);
    const auto d = budget.path("D");
    budget.capture(d, "5555");
    g_assert_true(load(*budget.history, d).versions.empty());
}

void test_deleted_and_gaps() {
    Fixture fixture;
    fixture.open();
    const auto dir = fixture.path("a/b");
    const auto direct = dir + "/file";
    const auto nested = dir + "/sub/\xff%_";
    const auto sibling = fixture.path("a/bc/file");
    disk::create_directories(disk::path(nested).parent_path());
    disk::create_directories(disk::path(sibling).parent_path());
    fixture.capture(direct, "first");
    fixture.capture(direct, "second");
    fixture.capture(nested, "nested");
    fixture.capture(sibling, "outside");
    const auto now = g_get_real_time();
    auto remove_file = event(core::EventKind::DELETE, direct, 1, now);
    auto remove_dir = event(core::EventKind::DELETE, dir, 2, now + 1);
    remove_dir.is_dir = true;
    auto overflow = event(core::EventKind::OVERFLOW, dir, 3, now + 2);
    auto done = event(core::EventKind::RESYNC_DONE, dir, 4, now + 3);
    fixture.history->record_events({core::make_event(remove_file)});
    g_assert_true(load(*fixture.history, direct).deleted_at_us == now);
    fixture.history->record_events(
        {core::make_event(remove_dir), core::make_event(overflow), core::make_event(done)});
    auto deleted = await<std::vector<his::FileHistory>>([&](his::DeletedSlot callback) {
        fixture.history->list_deleted_async(dir, std::move(callback));
    });
    g_assert_true(deleted.ok());
    g_assert_cmpuint(deleted.value().size(), ==, 2);
    for (const auto &entry : deleted.value()) {
        g_assert_cmpuint(entry.versions.size(), ==, 1);
        g_assert_true(entry.deleted_at_us == now + 1);
        g_assert_true(entry.gap_after_latest);
    }
    const auto other = load(*fixture.history, sibling);
    g_assert_false(other.deleted_at_us.has_value());
    g_assert_false(other.gap_after_latest);

    auto recreated = event(core::EventKind::CLOSE_WRITE, direct, 5, now + 4);
    fixture.history->record_events({core::make_event(recreated)});
    g_assert_false(load(*fixture.history, direct).deleted_at_us.has_value());
    g_assert_true(load(*fixture.history, nested).deleted_at_us.has_value());
}

} // namespace

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, nullptr);
    Glib::init();
    g_test_add_func("/his/open/schema-and-modes", test_open_migration_permissions);
    g_test_add_func("/his/open/v0-and-newer", test_existing_v0_and_newer);
    g_test_add_func("/his/open/corrupt-reset", test_corrupt_reset);
    g_test_add_func("/his/events/fields-reopen-retention", test_events_and_retention);
    g_test_add_func("/his/capture/dedup-and-content", test_capture_and_content);
    g_test_add_func("/his/capture/limits", test_version_limits);
    g_test_add_func("/his/query/deleted-and-gaps", test_deleted_and_gaps);
    return g_test_run();
}
