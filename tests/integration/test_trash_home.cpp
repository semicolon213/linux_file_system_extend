#include "trash/trash.hpp"

#include <array>
#include <cstdlib>
#include <fcntl.h>
#include <future>
#include <map>
#include <optional>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

#include <giomm/init.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <glibmm/main.h>

#include "core/fd.hpp"
#include "core/path.hpp"
#include "test_support.hpp"

namespace {

using namespace filescope;
using core::ErrorCode;
using fs::Confirmation;
using job::JobState;
using test::wait_until;

std::string root;

std::string entry(const std::string &id) { return trash::trash_dir() + "/files/" + id; }
std::string info(const std::string &id) {
    return trash::trash_dir() + "/info/" + id + ".trashinfo";
}

bool exists(const std::string &path) {
    struct stat status {};
    return lstat(path.c_str(), &status) == 0;
}

void write_file(const std::string &path, const char *contents) {
    g_assert_true(g_file_set_contents(path.c_str(), contents, -1, nullptr));
}

void create_dir(const std::string &path) { g_assert_cmpint(g_mkdir(path.c_str(), 0700), ==, 0); }

void reset_trash() {
    g_assert_true(trash::trash_dir() == root + "/data/Trash");
    if (exists(trash::trash_dir())) {
        g_assert_true(fs::remove_tree(trash::trash_dir(), Confirmation::CONFIRMED).ok());
    }
}

void make_trash() {
    reset_trash();
    create_dir(trash::trash_dir());
    create_dir(trash::trash_dir() + "/files");
    create_dir(trash::trash_dir() + "/info");
}

using Listing = core::Result<std::vector<trash::Item>>;

Listing list(const Glib::RefPtr<Gio::Cancellable> &token = {}) {
    std::optional<Listing> result;
    const auto caller = std::this_thread::get_id();
    trash::list_async(token, [&](Listing items) {
        g_assert_true(std::this_thread::get_id() == caller);
        g_assert_false(result.has_value());
        result.emplace(std::move(items));
    });
    g_assert_false(result.has_value());
    g_assert_true(wait_until([&] { return result.has_value(); }, 5000));
    if (!result) {
        g_error("Trash listing callback did not run");
    }
    return std::move(result.value());
}

struct Terminal {
    JobState state;
    std::optional<core::Error> error;
    std::vector<job::ItemResult> items;
};

template <typename Submit> Terminal run_job(job::JobQueue &queue, Submit submit) {
    std::optional<Terminal> terminal;
    job::JobId submitted = 0;
    sigc::connection connection =
        queue.signal_state().connect([&](auto id, auto state, const auto &error) {
            if (id == submitted && (state == JobState::DONE || state == JobState::FAILED ||
                                    state == JobState::CANCELLED)) {
                g_assert_false(terminal.has_value());
                terminal.emplace(Terminal{state, error, queue.results(id)});
            }
        });
    const auto result = submit();
    g_assert_true(result.ok());
    submitted = result.value();
    g_assert_true(wait_until([&] { return terminal.has_value(); }, 5000));
    connection.disconnect();
    if (!terminal) {
        g_error("Trash job did not reach a terminal state");
    }
    return std::move(terminal.value());
}

void assert_item_error(const Terminal &terminal, ErrorCode code) {
    g_assert_true(terminal.state == JobState::FAILED);
    g_assert_true(terminal.error.has_value());
    g_assert_true(terminal.error.value_or(core::Error{}).code == ErrorCode::PARTIAL);
    g_assert_cmpuint(terminal.items.size(), ==, 1);
    g_assert_true(terminal.items.front().error.has_value());
    g_assert_true(terminal.items.front().error.value_or(core::Error{}).code == code);
}

void test_list() {
    reset_trash();
    const auto missing = list();
    g_assert_true(missing.ok());
    g_assert_true(missing.value().empty());

    const auto raw = std::string("raw #%") + static_cast<char>(0xff);
    write_file(root + "/a.txt", "alpha");
    create_dir(root + "/d");
    write_file(root + "/d/one", "1");
    write_file(root + "/d/two", "2");
    write_file(root + "/" + raw, "xy");
    const auto before = g_get_real_time();
    for (const auto &name : {std::string("a.txt"), std::string("d"), raw}) {
        g_assert_true(fs::trash(core::path_join(root, name)).ok());
        g_assert_true(exists(entry(name)));
        g_assert_true(exists(info(name)));
    }
    const auto after = g_get_real_time();
    const auto listed = list();
    g_assert_true(listed.ok());
    g_assert_cmpuint(listed.value().size(), ==, 3);
    for (const auto &item : listed.value()) {
        g_assert_true(item.original_path == root + "/" + item.id);
        g_assert_false(item.info_missing);
        g_assert_true(item.deleted_at_us.has_value());
        constexpr std::int64_t tolerance = 2LL * G_USEC_PER_SEC;
        g_assert_cmpint(item.deleted_at_us.value_or(0), >=, before - tolerance);
        g_assert_cmpint(item.deleted_at_us.value_or(0), <=, after + tolerance);
        if (item.id == "d") {
            g_assert_true(item.type == core::FileType::DIRECTORY);
            g_assert_cmpuint(item.size, ==, 0);
        } else {
            g_assert_true(item.id == "a.txt" || item.id == raw);
            g_assert_true(item.type == core::FileType::REGULAR);
            g_assert_cmpuint(item.size, ==, item.id == raw ? 2 : 5);
        }
    }
    g_assert_cmpint(g_unlink(info("a.txt").c_str()), ==, 0);
    write_file(info("orphan"), "[Trash Info]\nPath=/tmp/unused\n");
    const auto metadata = "[Trash Info]\r\n" + std::string(20000, '#') +
                          "\r\nPath=relative%20name\r\nPath=/wrong\r\n"
                          "DeletionDate=broken\r\nDeletionDate=2026-11-25T10:00:00\r\n";
    write_file(info(raw), metadata.c_str());
    write_file(info("d"), "[Trash Info]\nPath=folder/%2e%2e/escape\n");
    const auto unpaired = list();
    g_assert_true(unpaired.ok());
    g_assert_cmpuint(unpaired.value().size(), ==, 3);
    std::map<std::string, trash::Item> items;
    for (const auto &item : unpaired.value()) {
        items.emplace(item.id, item);
    }
    g_assert_true(items.at("a.txt").info_missing);
    g_assert_true(items.at("d").info_missing);
    g_assert_false(items.at(raw).info_missing);
    g_assert_true(items.at(raw).original_path == root + "/data/relative name");
    g_assert_false(items.at(raw).deleted_at_us.has_value());

    const auto token = Gio::Cancellable::create();
    token->cancel();
    const auto cancelled = list(token);
    g_assert_false(cancelled.ok());
    g_assert_true(cancelled.error().code == ErrorCode::CANCELLED);
}

void test_restore() {
    make_trash();
    job::JobQueue queue;
    const auto restored = root + "/restore-file";
    const auto conflict = root + "/collision";
    const auto another = root + "/restore-dir";
    write_file(restored, "restored bytes");
    g_assert_cmpint(g_chmod(restored.c_str(), 0640), ==, 0);
    write_file(conflict, "old");
    create_dir(another);
    write_file(another + "/child", "child");
    for (const auto &path : {restored, conflict, another}) {
        g_assert_true(fs::trash(path).ok());
    }
    write_file(conflict, "new");

    const auto partial = run_job(
        queue, [&] { return trash::restore(queue, {"restore-file", "collision", "restore-dir"}); });
    g_assert_true(partial.state == JobState::FAILED);
    g_assert_true(partial.error.has_value());
    g_assert_true(partial.error.value_or(core::Error{}).code == ErrorCode::PARTIAL);
    g_assert_cmpuint(partial.items.size(), ==, 3);
    g_assert_false(partial.items[0].error.has_value());
    g_assert_true(partial.items[1].error.has_value());
    g_assert_true(partial.items[1].error.value_or(core::Error{}).code == ErrorCode::EXISTS);
    g_assert_false(partial.items[2].error.has_value());
    const auto text = fs::read_text(restored);
    g_assert_true(text.ok());
    g_assert_true(text.value().contents == "restored bytes");
    struct stat status {};
    g_assert_cmpint(lstat(restored.c_str(), &status), ==, 0);
    g_assert_cmpuint(status.st_mode & 0777, ==, 0640);
    g_assert_true(exists(another + "/child"));
    for (const auto *id : {"restore-file", "restore-dir"}) {
        g_assert_false(exists(entry(id)));
        g_assert_false(exists(info(id)));
    }
    const auto existing = fs::read_text(conflict);
    const auto trashed = fs::read_text(entry("collision"));
    g_assert_true(existing.ok());
    g_assert_true(trashed.ok());
    g_assert_true(existing.value().contents == "new");
    g_assert_true(trashed.value().contents == "old");
    g_assert_true(exists(info("collision")));

    write_file(entry("parent"), "keep");
    write_file(info("parent"), ("[Trash Info]\nPath=" + root + "/missing/parent\n").c_str());
    assert_item_error(run_job(queue, [&] { return trash::restore(queue, {"parent"}); }),
                      ErrorCode::NOT_FOUND);
    g_assert_true(exists(entry("parent")));
    g_assert_true(exists(info("parent")));

    write_file(entry("protected"), "keep");
    write_file(info("protected"), "[Trash Info]\nPath=/usr/bin/x\n");
    assert_item_error(run_job(queue, [&] { return trash::restore(queue, {"protected"}); }),
                      ErrorCode::PROTECTED_PATH);
    g_assert_true(exists(entry("protected")));
    g_assert_true(exists(info("protected")));

    write_file(entry("no-info"), "keep");
    assert_item_error(run_job(queue, [&] { return trash::restore(queue, {"no-info"}); }),
                      ErrorCode::NOT_FOUND);
    g_assert_true(exists(entry("no-info")));
}

void test_delete() {
    make_trash();
    for (const auto *id : {"choose", "remain"}) {
        write_file(entry(id), "contents");
        write_file(info(id), "[Trash Info]\nPath=/tmp/unused\n");
    }
    write_file(info("orphan"), "[Trash Info]\nPath=/tmp/unused\n");
    const auto outside = root + "/outside";
    write_file(outside, "untouched");
    g_assert_cmpint(symlink(outside.c_str(), entry("link").c_str()), ==, 0);

    job::JobQueue queue;
    unsigned submissions = 0;
    sigc::connection connection = queue.signal_state().connect([&](auto, auto state, const auto &) {
        if (state == JobState::QUEUED) {
            ++submissions;
        }
    });
    const auto refused = trash::delete_items(queue, {"choose"}, Confirmation::NONE);
    const auto not_empty = trash::empty(queue, Confirmation::NONE);
    g_assert_false(refused.ok());
    g_assert_false(not_empty.ok());
    g_assert_true(refused.error().code == ErrorCode::NOT_CONFIRMED);
    g_assert_true(not_empty.error().code == ErrorCode::NOT_CONFIRMED);
    const std::vector<std::string> invalid_ids{"../x", "a/b", "",
                                               ".",    "..",  std::string("x\0y", 3)};
    for (const auto &id : invalid_ids) {
        const auto bad_restore = trash::restore(queue, {"choose", id});
        const auto bad_delete = trash::delete_items(queue, {"choose", id}, Confirmation::CONFIRMED);
        g_assert_false(bad_restore.ok());
        g_assert_false(bad_delete.ok());
        g_assert_true(bad_restore.error().code == ErrorCode::INVALID_PATH);
        g_assert_true(bad_delete.error().code == ErrorCode::INVALID_PATH);
    }
    g_assert_cmpuint(submissions, ==, 0);
    connection.disconnect();
    g_assert_true(exists(entry("choose")));
    g_assert_true(exists(info("choose")));

    const auto selected = run_job(queue, [&] {
        return trash::delete_items(queue, {"choose", "link"}, Confirmation::CONFIRMED);
    });
    g_assert_true(selected.state == JobState::DONE);
    g_assert_false(exists(entry("choose")));
    g_assert_false(exists(info("choose")));
    g_assert_false(exists(entry("link")));
    g_assert_true(exists(outside));
    g_assert_true(exists(entry("remain")));
    g_assert_true(exists(info("remain")));
    g_assert_true(exists(info("orphan")));

    std::promise<void> unblock;
    const auto blocker = queue.submit("Hold queue", [ready = unblock.get_future().share()](auto &) {
        ready.wait();
        return core::Result<void>{};
    });
    g_assert_true(blocker.ok());
    const auto emptied = run_job(queue, [&] {
        const auto submitted = trash::empty(queue, Confirmation::CONFIRMED);
        write_file(entry("arrived-while-queued"), "late");
        write_file(info("arrived-while-queued"), "[Trash Info]\nPath=/tmp/unused\n");
        unblock.set_value();
        return submitted;
    });
    g_assert_true(emptied.state == JobState::DONE);
    g_assert_true(exists(trash::trash_dir() + "/files"));
    g_assert_true(exists(trash::trash_dir() + "/info"));
    g_assert_false(exists(entry("remain")));
    g_assert_false(exists(info("remain")));
    g_assert_false(exists(info("orphan")));
    g_assert_false(exists(entry("arrived-while-queued")));
    g_assert_false(exists(info("arrived-while-queued")));
    const auto all = list();
    g_assert_true(all.ok());
    g_assert_true(all.value().empty());
}

void test_cancel_empty() {
    make_trash();
    const auto tree = entry("tree");
    create_dir(tree);
    write_file(info("tree"), "[Trash Info]\nPath=/tmp/unused\n");
    write_file(entry("unrelated"), "other");
    write_file(info("unrelated"), "[Trash Info]\nPath=/tmp/other\n");
    constexpr int count = 12000;
    for (int i = 0; i < count; ++i) {
        const std::string path = tree + "/" + std::to_string(i);
        const core::UniqueFd fd(open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600));
        g_assert_true(static_cast<bool>(fd));
    }
    const core::UniqueFd watcher(inotify_init1(IN_CLOEXEC | IN_NONBLOCK));
    g_assert_true(static_cast<bool>(watcher));
    g_assert_cmpint(inotify_add_watch(watcher.get(), tree.c_str(), IN_DELETE), >=, 0);

    job::JobQueue queue;
    job::JobId submitted = 0;
    auto timeout = Glib::signal_timeout().connect(
        [&] {
            alignas(inotify_event) std::array<char, 4096> events{};
            if (read(watcher.get(), events.data(), events.size()) <= 0) {
                return true;
            }
            g_assert_true(queue.cancel(submitted));
            return false;
        },
        1);
    const auto cancelled = run_job(queue, [&] {
        const auto result = trash::empty(queue, Confirmation::CONFIRMED);
        g_assert_true(result.ok());
        submitted = result.value();
        return result;
    });
    timeout.disconnect();
    g_assert_true(cancelled.state == JobState::CANCELLED);
    g_assert_true(cancelled.error.has_value());
    g_assert_true(cancelled.error.value_or(core::Error{}).code == ErrorCode::CANCELLED);
    g_assert_true(exists(tree));
    g_assert_true(exists(info("tree")));
    g_assert_true(exists(entry("unrelated")) == exists(info("unrelated")));
}

} // namespace

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, nullptr);
    g_assert_cmpint(setenv("TZ", "UTC", 1), ==, 0);
    const test::TestEnvironment environment;
    root = environment.root();
    Gio::init();
    g_test_add_func("/trash/home/list", test_list);
    g_test_add_func("/trash/home/restore", test_restore);
    g_test_add_func("/trash/home/delete", test_delete);
    g_test_add_func("/trash/home/cancel-empty", test_cancel_empty);
    return g_test_run();
}
