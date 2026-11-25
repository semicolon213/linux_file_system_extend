#include "fs/fs.hpp"
#include "fs/fs_testing.hpp"

#include "test_support.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include <giomm/init.h>
#include <glib.h>
#include <glib/gstdio.h>

namespace {

using filescope::core::ErrorCode;
using filescope::fs::Confirmation;
using filescope::fs::ConflictPolicy;
using filescope::job::JobId;
using filescope::job::JobQueue;
using filescope::job::JobState;

std::string join(const std::string &left, const std::string &right) { return left + "/" + right; }

void make_directory(const std::string &path) {
    g_assert_cmpint(g_mkdir(path.c_str(), 0700), ==, 0);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): test helper reads path then contents.
void write_file(const std::string &path, const std::string &contents) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    g_assert_true(stream.good());
}

std::string read_file(const std::string &path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

JobState wait_for_terminal(JobQueue &queue, JobId id) {
    g_assert_true(filescope::test::wait_until(
        [&queue, id] {
            const JobState state = queue.state(id);
            return state == JobState::DONE || state == JobState::FAILED ||
                   state == JobState::CANCELLED;
        },
        5000));
    return queue.state(id);
}

void test_copy_tree_and_special_file() {
    filescope::test::TestEnvironment environment;
    const std::string source = join(environment.root(), "tree");
    const std::string destination = join(environment.root(), "destination");
    make_directory(source);
    make_directory(destination);
    const std::string regular = join(source, "file.txt");
    write_file(regular, "contents");
    g_assert_cmpint(chmod(regular.c_str(), 0640), ==, 0);
    const std::string outside = join(environment.root(), "outside");
    make_directory(outside);
    write_file(join(outside, "sentinel"), "outside");
    const std::string child = join(source, "child");
    make_directory(child);
    const std::string link = join(child, "link");
    g_assert_cmpint(symlink(outside.c_str(), link.c_str()), ==, 0);
    const std::string fifo = join(source, "pipe");
    g_assert_cmpint(mkfifo(fifo.c_str(), 0600), ==, 0);

    JobQueue queue;
    const auto submitted =
        filescope::fs::copy(queue, {source}, destination, ConflictPolicy::FAIL, Confirmation::NONE);
    g_assert_true(submitted.ok());
    g_assert_cmpint(static_cast<int>(wait_for_terminal(queue, submitted.value())), ==,
                    static_cast<int>(JobState::FAILED));

    const std::string copied = join(destination, "tree");
    g_assert_true(read_file(join(copied, "file.txt")) == "contents");
    struct stat regular_status {};
    g_assert_cmpint(stat(join(copied, "file.txt").c_str(), &regular_status), ==, 0);
    g_assert_cmpint(regular_status.st_mode & 0777, ==, 0640);
    struct stat link_status {};
    g_assert_cmpint(lstat(join(copied, "child/link").c_str(), &link_status), ==, 0);
    g_assert_true(S_ISLNK(link_status.st_mode));
    g_assert_false(g_file_test(join(copied, "pipe").c_str(), G_FILE_TEST_EXISTS));

    const auto results = queue.results(submitted.value());
    bool found_unsupported = false;
    for (const auto &result : results) {
        found_unsupported = found_unsupported || (result.path == fifo && result.error.has_value() &&
                                                  result.error->code == ErrorCode::NOT_SUPPORTED);
    }
    g_assert_true(found_unsupported);

    const auto removed =
        filescope::fs::delete_permanently(queue, {source}, Confirmation::CONFIRMED);
    g_assert_true(removed.ok());
    g_assert_true(wait_for_terminal(queue, removed.value()) == JobState::DONE);
    g_assert_false(std::filesystem::exists(source));
    g_assert_true(read_file(join(outside, "sentinel")) == "outside");
}

void test_conflict_policies() {
    filescope::test::TestEnvironment environment;
    const std::string source_dir = join(environment.root(), "source");
    const std::string destination = join(environment.root(), "destination");
    make_directory(source_dir);
    make_directory(destination);
    const std::string source = join(source_dir, "a.txt");
    const std::string target = join(destination, "a.txt");
    write_file(source, "new");
    write_file(target, "old");
    JobQueue queue;

    const auto failed =
        filescope::fs::copy(queue, {source}, destination, ConflictPolicy::FAIL, Confirmation::NONE);
    g_assert_true(failed.ok());
    g_assert_cmpint(static_cast<int>(wait_for_terminal(queue, failed.value())), ==,
                    static_cast<int>(JobState::FAILED));
    g_assert_true(read_file(target) == "old");

    const auto skipped =
        filescope::fs::copy(queue, {source}, destination, ConflictPolicy::SKIP, Confirmation::NONE);
    g_assert_true(skipped.ok());
    g_assert_cmpint(static_cast<int>(wait_for_terminal(queue, skipped.value())), ==,
                    static_cast<int>(JobState::DONE));
    g_assert_true(queue.results(skipped.value()).front().skipped);

    const auto kept = filescope::fs::copy(queue, {source}, destination, ConflictPolicy::KEEP_BOTH,
                                          Confirmation::NONE);
    g_assert_true(kept.ok());
    g_assert_cmpint(static_cast<int>(wait_for_terminal(queue, kept.value())), ==,
                    static_cast<int>(JobState::DONE));
    g_assert_true(read_file(join(destination, "a (2).txt")) == "new");

    const auto unconfirmed = filescope::fs::copy(queue, {source}, destination,
                                                 ConflictPolicy::OVERWRITE, Confirmation::NONE);
    g_assert_false(unconfirmed.ok());
    g_assert_cmpint(static_cast<int>(unconfirmed.error().code), ==,
                    static_cast<int>(ErrorCode::NOT_CONFIRMED));
    g_assert_true(read_file(target) == "old");

    const auto overwritten = filescope::fs::copy(
        queue, {source}, destination, ConflictPolicy::OVERWRITE, Confirmation::CONFIRMED);
    g_assert_true(overwritten.ok());
    g_assert_cmpint(static_cast<int>(wait_for_terminal(queue, overwritten.value())), ==,
                    static_cast<int>(JobState::DONE));
    g_assert_true(read_file(target) == "new");
}

void test_move_and_delete() {
    filescope::test::TestEnvironment environment;
    const std::string destination = join(environment.root(), "destination");
    make_directory(destination);
    const std::string source = join(environment.root(), "move.txt");
    write_file(source, "move");
    struct stat before {};
    g_assert_cmpint(stat(source.c_str(), &before), ==, 0);
    JobQueue queue;

    const auto moved =
        filescope::fs::move(queue, {source}, destination, ConflictPolicy::FAIL, Confirmation::NONE);
    g_assert_true(moved.ok());
    g_assert_cmpint(static_cast<int>(wait_for_terminal(queue, moved.value())), ==,
                    static_cast<int>(JobState::DONE));
    const std::string target = join(destination, "move.txt");
    struct stat after {};
    g_assert_cmpint(stat(target.c_str(), &after), ==, 0);
    g_assert_cmpuint(static_cast<guint64>(before.st_ino), ==, static_cast<guint64>(after.st_ino));

    const std::string tree = join(environment.root(), "tree");
    make_directory(tree);
    make_directory(join(tree, "child"));
    const auto invalid_move = filescope::fs::move(queue, {tree}, join(tree, "child"),
                                                  ConflictPolicy::FAIL, Confirmation::NONE);
    g_assert_false(invalid_move.ok());
    g_assert_cmpint(static_cast<int>(invalid_move.error().code), ==,
                    static_cast<int>(ErrorCode::INVALID_PATH));

    const std::string one = join(environment.root(), "one");
    const std::string two = join(environment.root(), "two");
    const std::string three = join(environment.root(), "three");
    write_file(one, "1");
    write_file(two, "2");
    write_file(three, "3");
    const auto rejected =
        filescope::fs::delete_permanently(queue, {one, two, three}, Confirmation::NONE);
    g_assert_false(rejected.ok());
    g_assert_true(g_file_test(one.c_str(), G_FILE_TEST_EXISTS));

    const auto removed =
        filescope::fs::delete_permanently(queue, {one, two, three}, Confirmation::CONFIRMED);
    g_assert_true(removed.ok());
    g_assert_cmpint(static_cast<int>(wait_for_terminal(queue, removed.value())), ==,
                    static_cast<int>(JobState::DONE));
    g_assert_cmpuint(queue.results(removed.value()).size(), ==, 3U);
    g_assert_false(g_file_test(one.c_str(), G_FILE_TEST_EXISTS));
    g_assert_false(g_file_test(two.c_str(), G_FILE_TEST_EXISTS));
    g_assert_false(g_file_test(three.c_str(), G_FILE_TEST_EXISTS));
}

void test_move_overwrite() {
    filescope::test::TestEnvironment environment;
    const std::string source_dir = join(environment.root(), "source");
    const std::string destination = join(environment.root(), "destination");
    make_directory(source_dir);
    make_directory(destination);
    const std::string source = join(source_dir, "tree");
    const std::string target = join(destination, "tree");
    make_directory(source);
    make_directory(target);
    write_file(join(source, "new"), "new");
    write_file(join(target, "old"), "old");
    JobQueue queue;
    const auto move = [&] {
        const auto submitted = filescope::fs::move(
            queue, {source}, destination, ConflictPolicy::OVERWRITE, Confirmation::CONFIRMED);
        g_assert_true(submitted.ok());
        return wait_for_terminal(queue, submitted.value());
    };

    if (geteuid() != 0) {
        g_assert_cmpint(chmod(source_dir.c_str(), 0500), ==, 0);
        g_assert_true(move() == JobState::FAILED);
        g_assert_true(read_file(join(target, "old")) == "old");
        g_assert_true(read_file(join(source, "new")) == "new");
        g_assert_cmpint(chmod(source_dir.c_str(), 0700), ==, 0);
    }

    g_assert_true(move() == JobState::DONE);
    g_assert_true(read_file(join(target, "new")) == "new");
    g_assert_false(std::filesystem::exists(join(target, "old")));
    g_assert_false(std::filesystem::exists(source));
    g_assert_true(std::distance(std::filesystem::directory_iterator(destination),
                                std::filesystem::directory_iterator()) == 1);

    std::filesystem::remove_all(target);
    write_file(source, "new file");
    write_file(target, "old file");
    struct stat before {};
    struct stat after {};
    g_assert_cmpint(stat(source.c_str(), &before), ==, 0);
    g_assert_true(move() == JobState::DONE);
    g_assert_cmpint(stat(target.c_str(), &after), ==, 0);
    g_assert_true(before.st_ino == after.st_ino);
    g_assert_true(read_file(target) == "new file");
}

std::string cross_device_directory(const std::string &source_root) {
    struct stat source_status {};
    g_assert_cmpint(stat(source_root.c_str(), &source_status), ==, 0);
    const std::string runtime = "/run/user/" + std::to_string(getuid());
    for (const char *parent : {runtime.c_str(), "/mnt/wsl", "/mnt/c", "/media", "/run/media"}) {
        struct stat status {};
        if (stat(parent, &status) == 0 && status.st_dev != source_status.st_dev) {
            std::string candidate = join(parent, "filescope-exdev-XXXXXX");
            if (mkdtemp(candidate.data()) != nullptr) {
                return candidate;
            }
        }
    }
    return {};
}

void test_move_cross_device() {
    filescope::test::TestEnvironment environment;
    const std::string destination = cross_device_directory(environment.root());
    if (destination.empty()) {
        g_test_skip("No writable alternate device among candidate mount points");
        return;
    }

    const std::string source = join(environment.root(), "tree");
    const std::string nested = join(source, "nested");
    const std::string target = join(destination, "tree");
    const std::string fifo = join(nested, "pipe");
    make_directory(source);
    make_directory(nested);
    write_file(join(nested, "file"), "preserved");
    g_assert_cmpint(mkfifo(fifo.c_str(), 0600), ==, 0);
    JobQueue queue;
    const auto move = [&](ConflictPolicy policy, Confirmation confirmation) {
        const auto submitted =
            filescope::fs::move(queue, {source}, destination, policy, confirmation);
        g_assert_true(submitted.ok());
        return submitted.value();
    };
    const JobId failed = move(ConflictPolicy::FAIL, Confirmation::NONE);
    g_assert_true(wait_for_terminal(queue, failed) == JobState::FAILED);
    bool fifo_failed = false;
    bool tree_partial = false;
    for (const auto &item : queue.results(failed)) {
        fifo_failed |=
            item.path == fifo && item.error && item.error->code == ErrorCode::NOT_SUPPORTED;
        tree_partial |= item.path == source && item.error && item.error->code == ErrorCode::PARTIAL;
    }
    g_assert_true(fifo_failed);
    if (g_test_verbose()) {
        for (const auto &item : queue.results(failed)) {
            if (item.error) {
                g_test_message("%s: %s", item.path.c_str(), item.error->message.c_str());
            }
        }
    }
    g_assert_true(tree_partial);
    g_assert_true(std::filesystem::is_fifo(fifo));
    g_assert_true(read_file(join(nested, "file")) == "preserved");
    g_assert_true(read_file(join(target, "nested/file")) == "preserved");

    write_file(join(target, "old"), "old target");
    const JobId rollback = move(ConflictPolicy::OVERWRITE, Confirmation::CONFIRMED);
    g_assert_true(wait_for_terminal(queue, rollback) == JobState::FAILED);
    g_assert_true(std::filesystem::is_fifo(fifo));
    g_assert_true(read_file(join(target, "old")) == "old target");
    g_assert_true(std::distance(std::filesystem::directory_iterator(destination),
                                std::filesystem::directory_iterator()) == 1);

    g_assert_cmpint(unlink(fifo.c_str()), ==, 0);
    const JobId moved = move(ConflictPolicy::OVERWRITE, Confirmation::CONFIRMED);
    g_assert_true(wait_for_terminal(queue, moved) == JobState::DONE);
    g_assert_false(std::filesystem::exists(source));
    g_assert_false(std::filesystem::exists(join(target, "old")));
    g_assert_true(read_file(join(target, "nested/file")) == "preserved");
    std::filesystem::remove_all(destination);
}

void test_move_cross_device_conflict() {
    filescope::test::TestEnvironment environment;
    const std::string source = join(environment.root(), "a");
    std::string destination = cross_device_directory(environment.root());
    if (destination.empty()) {
        destination = join(environment.root(), "destination");
        make_directory(destination);
        filescope::fs::testing::force_next_cross_device_move();
        g_test_message("EXDEV forced: no writable alternate device");
    } else {
        g_test_message("EXDEV from a real alternate device: %s", destination.c_str());
    }
    const std::string target = join(destination, "a");
    make_directory(source);
    make_directory(join(source, "sub"));
    write_file(join(source, "x"), "x source");
    write_file(join(source, "sub/y"), "y source");
    write_file(join(source, "changed"), "old");
    filescope::fs::testing::set_source_removal_hook([](const std::string &path) {
        write_file(join(path, "sub/z"), "new file");
        write_file(join(path, "changed"), "updated file");
    });
    JobQueue queue;
    const auto moved =
        filescope::fs::move(queue, {source}, destination, ConflictPolicy::FAIL, Confirmation::NONE);
    g_assert_true(moved.ok());
    g_assert_true(wait_for_terminal(queue, moved.value()) == JobState::FAILED);
    const auto results = queue.results(moved.value());
    for (const auto &name : {"sub/z", "changed"}) {
        bool conflict = false;
        for (const auto &item : results) {
            conflict |= item.path == join(source, name) && item.error &&
                        item.error->code == ErrorCode::CONFLICT;
        }
        g_assert_true(conflict);
    }
    g_assert_true(read_file(join(target, "x")) == "x source");
    g_assert_true(read_file(join(target, "sub/y")) == "y source");
    g_assert_true(read_file(join(target, "changed")) == "old");
    g_assert_false(std::filesystem::exists(join(target, "sub/z")));
    g_assert_false(std::filesystem::exists(join(source, "x")));
    g_assert_false(std::filesystem::exists(join(source, "sub/y")));
    g_assert_true(read_file(join(source, "sub/z")) == "new file");
    g_assert_true(read_file(join(source, "changed")) == "updated file");
    std::filesystem::remove_all(destination);
}

void test_copy_directory_overwrite() {
    filescope::test::TestEnvironment environment;
    const std::string source = join(environment.root(), "d");
    const std::string destination = join(environment.root(), "destination");
    const std::string target = join(destination, "d");
    make_directory(source);
    make_directory(destination);
    make_directory(target);
    g_assert_cmpint(chmod(target.c_str(), 0750), ==, 0);
    write_file(join(target, "old-a"), "old A");
    write_file(join(target, "old-b"), "old B");
    write_file(join(source, "new-a"), "new A");
    write_file(join(source, "new-b"), "new B");
    static std::mutex pause_mutex;
    static std::condition_variable resume;
    static bool paused = false;
    JobQueue queue;
    const auto copy = [&](bool cancel = false) {
        const auto submitted = filescope::fs::copy(
            queue, {source}, destination, ConflictPolicy::OVERWRITE, Confirmation::CONFIRMED);
        g_assert_true(submitted.ok());
        if (cancel) {
            g_assert_true(filescope::test::wait_until(
                [] {
                    const std::lock_guard<std::mutex> lock(pause_mutex);
                    return paused;
                },
                5000));
            {
                const std::lock_guard<std::mutex> lock(pause_mutex);
                g_assert_true(queue.cancel(submitted.value()));
            }
            resume.notify_one();
        }
        const auto state = wait_for_terminal(queue, submitted.value());
        g_assert_true(std::distance(std::filesystem::directory_iterator(destination),
                                    std::filesystem::directory_iterator()) == 1);
        return state;
    };
    const auto check_original = [&] {
        g_assert_true(read_file(join(target, "old-a")) == "old A");
        g_assert_true(read_file(join(target, "old-b")) == "old B");
        g_assert_true(std::distance(std::filesystem::directory_iterator(target),
                                    std::filesystem::directory_iterator()) == 2);
        struct stat status {};
        g_assert_cmpint(stat(target.c_str(), &status), ==, 0);
        g_assert_cmpint(status.st_mode & 0777, ==, 0750);
    };
    filescope::fs::testing::set_copy_write_hook([](filescope::job::Job &) {
        static unsigned writes = 0;
        if (++writes == 2) {
            filescope::fs::testing::fail_next_write(ErrorCode::NO_SPACE);
        }
    });
    g_assert_true(copy() == JobState::FAILED);
    filescope::fs::testing::set_copy_write_hook(nullptr);
    check_original();

    const std::string large(std::size_t{2} * 64U * 1024U, 'x');
    write_file(join(source, "new-a"), large);
    write_file(join(source, "new-b"), large);
    filescope::fs::testing::set_copy_write_hook([](filescope::job::Job &job) {
        static unsigned writes = 0;
        if (++writes == 2) {
            std::unique_lock<std::mutex> lock(pause_mutex);
            paused = true;
            g_assert_true(resume.wait_for(lock, std::chrono::seconds(5),
                                          [&job] { return job.is_cancelled(); }));
        }
    });
    g_assert_true(copy(true) == JobState::CANCELLED);
    filescope::fs::testing::set_copy_write_hook(nullptr);
    check_original();

    g_assert_true(copy() == JobState::DONE);
    g_assert_true(read_file(join(target, "new-a")) == large);
    g_assert_true(read_file(join(target, "new-b")) == large);
    g_assert_false(std::filesystem::exists(join(target, "old-a")));
    g_assert_false(std::filesystem::exists(join(target, "old-b")));
}

void test_delete_partial_permission_failure() {
    if (geteuid() == 0) {
        g_test_skip("permission denial is not reproducible as root");
        return;
    }
    filescope::test::TestEnvironment environment;
    const std::string one = join(environment.root(), "one");
    const std::string two = join(environment.root(), "two");
    const std::string locked = join(environment.root(), "locked");
    make_directory(locked);
    const std::string denied = join(locked, "denied");
    write_file(one, "1");
    write_file(two, "2");
    write_file(denied, "3");
    g_assert_cmpint(chmod(locked.c_str(), 0500), ==, 0);

    JobQueue queue;
    const auto submitted =
        filescope::fs::delete_permanently(queue, {one, denied, two}, Confirmation::CONFIRMED);
    g_assert_true(submitted.ok());
    g_assert_cmpint(static_cast<int>(wait_for_terminal(queue, submitted.value())), ==,
                    static_cast<int>(JobState::FAILED));
    const auto results = queue.results(submitted.value());
    g_assert_cmpuint(results.size(), ==, 3U);
    g_assert_false(g_file_test(one.c_str(), G_FILE_TEST_EXISTS));
    g_assert_true(g_file_test(denied.c_str(), G_FILE_TEST_EXISTS));
    g_assert_false(g_file_test(two.c_str(), G_FILE_TEST_EXISTS));
    g_assert_true(results[1].error.has_value());
    g_assert_cmpint(static_cast<int>(results[1].error.value_or(filescope::core::Error{}).code), ==,
                    static_cast<int>(ErrorCode::PERMISSION_DENIED));
    g_assert_cmpint(chmod(locked.c_str(), 0700), ==, 0);
}

void test_copy_cancellation_cleanup() {
    filescope::test::TestEnvironment environment;
    const std::string source = join(environment.root(), "large.bin");
    const std::string destination = join(environment.root(), "destination");
    make_directory(destination);
    {
        std::ofstream stream(source, std::ios::binary | std::ios::trunc);
        const std::string block(std::size_t{1024} * 1024U, 'x');
        for (unsigned index = 0; index < 64U; ++index) {
            stream.write(block.data(), static_cast<std::streamsize>(block.size()));
        }
        g_assert_true(stream.good());
    }
    JobQueue queue;
    const sigc::connection cancel_on_running = queue.signal_state().connect(
        [&queue](JobId id, JobState state, const std::optional<filescope::core::Error> &) {
            if (state == JobState::RUNNING) {
                queue.cancel(id);
            }
        });
    const auto submitted =
        filescope::fs::copy(queue, {source}, destination, ConflictPolicy::FAIL, Confirmation::NONE);
    g_assert_true(submitted.ok());
    g_assert_cmpint(static_cast<int>(wait_for_terminal(queue, submitted.value())), ==,
                    static_cast<int>(JobState::CANCELLED));
    g_assert_true(g_file_test(source.c_str(), G_FILE_TEST_EXISTS));
    g_assert_false(g_file_test(join(destination, "large.bin").c_str(), G_FILE_TEST_EXISTS));
    for (const auto &entry : std::filesystem::directory_iterator(destination)) {
        g_assert_false(entry.path().filename().string().rfind(".filescope-", 0) == 0);
    }
}

void test_trash() {
    filescope::test::TestEnvironment environment;
    const std::string path = join(environment.root(), "trash.txt");
    write_file(path, "trash");
    const auto result = filescope::fs::trash(path);
    g_assert_true(result.ok());
    g_assert_false(g_file_test(path.c_str(), G_FILE_TEST_EXISTS));
    g_assert_true(g_file_test(join(environment.root(), "data/Trash/files/trash.txt").c_str(),
                              G_FILE_TEST_EXISTS));
}

} // namespace

int main(int argc, char **argv) {
    Gio::init();
    g_test_init(&argc, &argv, nullptr);
    g_test_add_func("/fs/integration/copy-tree", test_copy_tree_and_special_file);
    g_test_add_func("/fs/integration/conflicts", test_conflict_policies);
    g_test_add_func("/fs/integration/move-delete", test_move_and_delete);
    g_test_add_func("/fs/integration/move-overwrite", test_move_overwrite);
    g_test_add_func("/fs/integration/move-cross-device", test_move_cross_device);
    g_test_add_func("/fs/integration/move-cross-device-conflict", test_move_cross_device_conflict);
    g_test_add_func("/fs/integration/copy-directory-overwrite", test_copy_directory_overwrite);
    g_test_add_func("/fs/integration/delete-partial", test_delete_partial_permission_failure);
    g_test_add_func("/fs/integration/cancel", test_copy_cancellation_cleanup);
    g_test_add_func("/fs/integration/trash", test_trash);
    return g_test_run();
}
