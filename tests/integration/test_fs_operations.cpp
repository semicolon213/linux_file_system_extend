#include "fs/fs.hpp"

#include "test_support.hpp"

#include <filesystem>
#include <fstream>
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
    const std::string link = join(source, "link");
    g_assert_cmpint(symlink("file.txt", link.c_str()), ==, 0);
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
    g_assert_cmpint(lstat(join(copied, "link").c_str(), &link_status), ==, 0);
    g_assert_true(S_ISLNK(link_status.st_mode));
    g_assert_false(g_file_test(join(copied, "pipe").c_str(), G_FILE_TEST_EXISTS));

    const auto results = queue.results(submitted.value());
    bool found_unsupported = false;
    for (const auto &result : results) {
        found_unsupported = found_unsupported || (result.path == fifo && result.error.has_value() &&
                                                  result.error->code == ErrorCode::NOT_SUPPORTED);
    }
    g_assert_true(found_unsupported);
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
    g_test_add_func("/fs/integration/delete-partial", test_delete_partial_permission_failure);
    g_test_add_func("/fs/integration/cancel", test_copy_cancellation_cleanup);
    g_test_add_func("/fs/integration/trash", test_trash);
    return g_test_run();
}
