#include "fs/fs.hpp"
#include "fs/fs_testing.hpp"

#include "test_support.hpp"

#include <cerrno>
#include <csignal>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

#include <giomm/init.h>
#include <glib.h>
#include <glib/gstdio.h>

namespace {

using filescope::core::ErrorCode;
using filescope::fs::Confirmation;

std::string join(const std::string &left, const std::string &right) { return left + "/" + right; }

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

bool has_temporary_file(const std::string &directory_path) {
    for (const auto &entry : std::filesystem::directory_iterator(directory_path)) {
        if (entry.path().filename().string().rfind(".filescope-", 0) == 0) {
            return true;
        }
    }
    return false;
}

template <class T> void assert_error(const filescope::core::Result<T> &result, ErrorCode code) {
    g_assert_false(result.ok());
    g_assert_cmpint(static_cast<int>(result.error().code), ==, static_cast<int>(code));
}

void test_install_replace() {
    filescope::test::TestEnvironment environment;
    const std::string source = join(environment.root(), "source");
    const std::string target = join(environment.root(), "target");
    const std::string contents{"B\0C", 3};
    write_file(source, contents);
    write_file(target, "A");
    g_assert_cmpint(chmod(target.c_str(), 0640), ==, 0);
    const auto before = filescope::fs::read_text(target);
    g_assert_true(before.ok());

    const auto installed = filescope::fs::install_file(target, source, before.value().stamp, {});
    g_assert_true(installed.ok());
    g_assert_true(read_file(target) == contents);
    struct stat status {};
    g_assert_cmpint(lstat(target.c_str(), &status), ==, 0);
    g_assert_cmpint(status.st_mode & 07777, ==, 0640);
    g_assert_cmpuint(installed.value().ino, !=, before.value().stamp.ino);
    g_assert_cmpuint(installed.value().ino, ==, status.st_ino);
    g_assert_cmpuint(installed.value().dev, ==, status.st_dev);
    g_assert_cmpuint(installed.value().size, ==, status.st_size);
    g_assert_cmpint(installed.value().mtime_ns, ==,
                    status.st_mtim.tv_sec * 1000000000LL + status.st_mtim.tv_nsec);

    const auto explicit_mode = filescope::fs::install_file(target, source, installed.value(), 0600);
    g_assert_true(explicit_mode.ok());
    g_assert_cmpint(lstat(target.c_str(), &status), ==, 0);
    g_assert_cmpint(status.st_mode & 07777, ==, 0600);

    write_file(target, "external modification");
    assert_error(filescope::fs::install_file(target, source, explicit_mode.value(), {}),
                 ErrorCode::CONFLICT);
    g_assert_true(read_file(target) == "external modification");
    g_assert_cmpint(unlink(target.c_str()), ==, 0);
    assert_error(filescope::fs::install_file(target, source, explicit_mode.value(), {}),
                 ErrorCode::NOT_FOUND);
    g_assert_false(has_temporary_file(environment.root()));
}

void test_install_create() {
    filescope::test::TestEnvironment environment;
    const std::string source = join(environment.root(), "source");
    const std::string target = join(environment.root(), "target");
    write_file(source, "source bytes");
    const auto reference = filescope::fs::create_file(environment.root(), "reference");
    g_assert_true(reference.ok());

    g_assert_true(filescope::fs::install_file(target, source, {}, {}).ok());
    g_assert_true(read_file(target) == "source bytes");
    struct stat created {};
    struct stat expected {};
    g_assert_cmpint(lstat(target.c_str(), &created), ==, 0);
    g_assert_cmpint(lstat(reference.value().c_str(), &expected), ==, 0);
    g_assert_cmpint(created.st_mode & 07777, ==, expected.st_mode & 07777);

    write_file(source, "new bytes");
    assert_error(filescope::fs::install_file(target, source, {}, {}), ErrorCode::EXISTS);
    g_assert_true(read_file(target) == "source bytes");
    assert_error(
        filescope::fs::install_file(join(environment.root(), "missing/file"), source, {}, {}),
        ErrorCode::NOT_FOUND);
    g_assert_false(has_temporary_file(environment.root()));
}

void test_install_rejects_unsafe_sources_and_targets() {
    filescope::test::TestEnvironment environment;
    const std::string source = join(environment.root(), "source");
    const std::string target = join(environment.root(), "target");
    const std::string fifo = join(environment.root(), "fifo");
    const std::string link = join(environment.root(), "link");
    write_file(source, "source bytes");
    g_assert_cmpint(mkfifo(fifo.c_str(), 0600), ==, 0);
    g_assert_cmpint(symlink(source.c_str(), link.c_str()), ==, 0);

    for (const auto &path : {environment.root(), fifo, link}) {
        assert_error(filescope::fs::install_file(target, path, {}, {}), ErrorCode::NOT_SUPPORTED);
    }
    assert_error(filescope::fs::install_file("/usr/x", join(environment.root(), "missing"), {}, {}),
                 ErrorCode::PROTECTED_PATH);
    const auto cancellable = Gio::Cancellable::create();
    cancellable->cancel();
    assert_error(filescope::fs::install_file(target, source, {}, {}, cancellable),
                 ErrorCode::CANCELLED);
    g_assert_false(g_file_test(target.c_str(), G_FILE_TEST_EXISTS));
    g_assert_false(has_temporary_file(environment.root()));
}

void test_install_write_failure_cleanup() {
    if (!g_test_subprocess()) {
        g_test_trap_subprocess(nullptr, guint64{5} * G_USEC_PER_SEC, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed();
        return;
    }
    filescope::test::TestEnvironment environment;
    const std::string source = join(environment.root(), "source");
    const std::string target = join(environment.root(), "target");
    write_file(source, "replacement");
    write_file(target, "original");
    const auto before = filescope::fs::read_text(target);
    g_assert_true(before.ok());

    // The first byte is written, then the next write fails with EFBIG.
    struct rlimit limit {};
    g_assert_cmpint(getrlimit(RLIMIT_FSIZE, &limit), ==, 0);
    limit.rlim_cur = 1;
    g_assert_cmpint(setrlimit(RLIMIT_FSIZE, &limit), ==, 0);
    g_assert_true(std::signal(SIGXFSZ, SIG_IGN) != SIG_ERR);
    assert_error(filescope::fs::install_file(target, source, before.value().stamp, {}),
                 ErrorCode::FAILED);
    g_assert_true(read_file(target) == "original");
    g_assert_false(has_temporary_file(environment.root()));
}

void test_remove_tree() {
    filescope::test::TestEnvironment environment;
    const std::string outside = join(environment.root(), "outside");
    const std::string tree = join(environment.root(), "tree");
    const std::string child = join(tree, "child");
    const std::string leaf = join(child, "leaf");
    const std::string link = join(tree, "link");
    const std::string root_link = join(environment.root(), "root-link");
    g_assert_cmpint(mkdir(outside.c_str(), 0700), ==, 0);
    write_file(join(outside, "keep"), "outside bytes");
    g_assert_cmpint(mkdir(tree.c_str(), 0700), ==, 0);
    g_assert_cmpint(mkdir(child.c_str(), 0700), ==, 0);
    write_file(leaf, "tree bytes");
    g_assert_cmpint(symlink(outside.c_str(), link.c_str()), ==, 0);
    g_assert_cmpint(symlink(outside.c_str(), root_link.c_str()), ==, 0);

    assert_error(filescope::fs::remove_tree(tree, Confirmation::NONE), ErrorCode::NOT_CONFIRMED);
    const auto cancellable = Gio::Cancellable::create();
    cancellable->cancel();
    assert_error(filescope::fs::remove_tree(tree, Confirmation::CONFIRMED, cancellable),
                 ErrorCode::CANCELLED);
    g_assert_true(read_file(leaf) == "tree bytes");
    g_assert_true(g_file_test(link.c_str(), G_FILE_TEST_IS_SYMLINK));
    assert_error(filescope::fs::remove_tree("/", Confirmation::CONFIRMED, cancellable),
                 ErrorCode::PROTECTED_PATH);
    assert_error(filescope::fs::remove_tree(environment.root(), Confirmation::CONFIRMED),
                 ErrorCode::PROTECTED_PATH);
    g_assert_true(filescope::fs::remove_tree(tree, Confirmation::CONFIRMED).ok());
    g_assert_true(filescope::fs::remove_tree(root_link + "/", Confirmation::CONFIRMED).ok());
    g_assert_false(g_file_test(tree.c_str(), G_FILE_TEST_EXISTS));
    g_assert_false(g_file_test(root_link.c_str(), G_FILE_TEST_IS_SYMLINK));
    g_assert_true(read_file(join(outside, "keep")) == "outside bytes");
}

void test_remove_tree_permission_denied() {
    if (geteuid() == 0) {
        g_test_skip("permission denial is not reproducible as root");
        return;
    }
    filescope::test::TestEnvironment environment;
    const std::string tree = join(environment.root(), "tree");
    const std::string first = join(tree, "first");
    const std::string second = join(tree, "second");
    g_assert_cmpint(mkdir(tree.c_str(), 0700), ==, 0);
    g_assert_cmpint(mkdir(first.c_str(), 0700), ==, 0);
    g_assert_cmpint(mkdir(second.c_str(), 0700), ==, 0);
    // Deny the first entry in traversal order so a fail-fast walker would leave its sibling.
    const std::string locked = std::filesystem::directory_iterator(tree)->path().string();
    const std::string &sibling = locked == first ? second : first;
    const std::string hidden = join(locked, "hidden");
    write_file(hidden, "protected");
    write_file(join(sibling, "leaf"), "removable");
    g_assert_cmpint(chmod(locked.c_str(), 0000), ==, 0);
    const auto removed = filescope::fs::remove_tree(tree, Confirmation::CONFIRMED);
    g_assert_cmpint(chmod(locked.c_str(), 0700), ==, 0);
    assert_error(removed, ErrorCode::PERMISSION_DENIED);
    g_assert_true(read_file(hidden) == "protected");
    g_assert_false(g_file_test(sibling.c_str(), G_FILE_TEST_EXISTS));
}

void test_create_and_rename() {
    filescope::test::TestEnvironment environment;

    const auto file = filescope::fs::create_file(environment.root(), "a.txt");
    g_assert_true(file.ok());
    g_assert_true(g_file_test(file.value().c_str(), G_FILE_TEST_IS_REGULAR));
    write_file(file.value(), "a");

    const auto duplicate = filescope::fs::create_file(environment.root(), "a.txt");
    g_assert_false(duplicate.ok());
    g_assert_cmpint(static_cast<int>(duplicate.error().code), ==,
                    static_cast<int>(ErrorCode::EXISTS));
    g_assert_true(read_file(file.value()) == "a");

    const auto directory = filescope::fs::create_dir(environment.root(), "d");
    g_assert_true(directory.ok());
    g_assert_true(g_file_test(directory.value().c_str(), G_FILE_TEST_IS_DIR));

    const auto invalid = filescope::fs::create_file(environment.root(), "a/b");
    g_assert_false(invalid.ok());
    g_assert_cmpint(static_cast<int>(invalid.error().code), ==,
                    static_cast<int>(ErrorCode::INVALID_PATH));

    const std::string second = join(environment.root(), "b.txt");
    write_file(second, "b");
    const auto collision = filescope::fs::rename(file.value(), "b.txt");
    g_assert_false(collision.ok());
    g_assert_cmpint(static_cast<int>(collision.error().code), ==,
                    static_cast<int>(ErrorCode::EXISTS));
    g_assert_true(read_file(file.value()) == "a");
    g_assert_true(read_file(second) == "b");
}

void test_text_save_and_conflict() {
    filescope::test::TestEnvironment environment;
    const std::string path = join(environment.root(), "text.txt");
    write_file(path, "before");
    g_assert_cmpint(chmod(path.c_str(), 0640), ==, 0);

    const auto read = filescope::fs::read_text(path);
    g_assert_true(read.ok());
    g_assert_cmpstr(read.value().contents.c_str(), ==, "before");

    const auto saved =
        filescope::fs::write_text(path, "after", read.value().stamp, Confirmation::NONE);
    g_assert_true(saved.ok());
    g_assert_true(read_file(path) == "after");
    struct stat status {};
    g_assert_cmpint(stat(path.c_str(), &status), ==, 0);
    g_assert_cmpint(status.st_mode & 0777, ==, 0640);
    g_assert_false(has_temporary_file(environment.root()));

    const auto stale = filescope::fs::read_text(path);
    g_assert_true(stale.ok());
    write_file(path, "external-change");
    const auto conflict =
        filescope::fs::write_text(path, "lost", stale.value().stamp, Confirmation::NONE);
    g_assert_false(conflict.ok());
    g_assert_cmpint(static_cast<int>(conflict.error().code), ==,
                    static_cast<int>(ErrorCode::CONFLICT));
    g_assert_true(read_file(path) == "external-change");
    g_assert_false(has_temporary_file(environment.root()));

    const auto unconfirmed =
        filescope::fs::write_text(path, "forced", std::nullopt, Confirmation::NONE);
    g_assert_false(unconfirmed.ok());
    g_assert_cmpint(static_cast<int>(unconfirmed.error().code), ==,
                    static_cast<int>(ErrorCode::NOT_CONFIRMED));
    g_assert_true(read_file(path) == "external-change");
}

void test_text_too_large() {
    filescope::test::TestEnvironment environment;
    const std::string path = join(environment.root(), "large.txt");
    const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_CLOEXEC, 0600);
    g_assert_cmpint(fd, >=, 0);
    g_assert_cmpint(ftruncate(fd, static_cast<off_t>(filescope::fs::kTextMaxBytes + 1U)), ==, 0);
    g_assert_cmpint(close(fd), ==, 0);

    const auto read = filescope::fs::read_text(path);
    g_assert_false(read.ok());
    g_assert_cmpint(static_cast<int>(read.error().code), ==,
                    static_cast<int>(ErrorCode::TOO_LARGE));
}

void test_text_write_failure_cleanup() {
    filescope::test::TestEnvironment environment;
    const std::string path = join(environment.root(), "text.txt");
    write_file(path, "original");
    const auto read = filescope::fs::read_text(path);
    g_assert_true(read.ok());

    filescope::fs::testing::fail_next_write(ErrorCode::NO_SPACE);
    const auto failed =
        filescope::fs::write_text(path, "replacement", read.value().stamp, Confirmation::NONE);
    g_assert_false(failed.ok());
    g_assert_cmpint(static_cast<int>(failed.error().code), ==,
                    static_cast<int>(ErrorCode::NO_SPACE));
    g_assert_true(read_file(path) == "original");
    g_assert_false(has_temporary_file(environment.root()));
}

void test_conflict_scan_and_protection() {
    filescope::test::TestEnvironment environment;
    const std::string source = join(environment.root(), "source.txt");
    const std::string destination = join(environment.root(), "destination");
    g_assert_cmpint(g_mkdir(destination.c_str(), 0700), ==, 0);
    write_file(source, "source");
    write_file(join(destination, "source.txt"), "destination");

    const auto conflicts = filescope::fs::find_conflicts({source}, destination);
    g_assert_true(conflicts.ok());
    g_assert_cmpuint(conflicts.value().size(), ==, 1U);
    g_assert_cmpstr(conflicts.value().front().c_str(), ==, source.c_str());

    const auto protected_create = filescope::fs::create_file("/usr", "filescope-test");
    g_assert_false(protected_create.ok());
    g_assert_cmpint(static_cast<int>(protected_create.error().code), ==,
                    static_cast<int>(ErrorCode::PROTECTED_PATH));
}

void test_create_permission_denied() {
    if (geteuid() == 0) {
        g_test_skip("permission denial is not reproducible as root");
        return;
    }
    filescope::test::TestEnvironment environment;
    const std::string directory = join(environment.root(), "locked");
    g_assert_cmpint(g_mkdir(directory.c_str(), 0700), ==, 0);
    g_assert_cmpint(chmod(directory.c_str(), 0500), ==, 0);

    const auto result = filescope::fs::create_file(directory, "denied.txt");
    g_assert_false(result.ok());
    g_assert_cmpint(static_cast<int>(result.error().code), ==,
                    static_cast<int>(ErrorCode::PERMISSION_DENIED));
    g_assert_false(g_file_test(join(directory, "denied.txt").c_str(), G_FILE_TEST_EXISTS));
    g_assert_cmpint(chmod(directory.c_str(), 0700), ==, 0);
}

} // namespace

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, nullptr);
    Gio::init();
    g_test_add_func("/fs/create-rename", test_create_and_rename);
    g_test_add_func("/fs/text/save-conflict", test_text_save_and_conflict);
    g_test_add_func("/fs/text/too-large", test_text_too_large);
    g_test_add_func("/fs/text/write-failure-cleanup", test_text_write_failure_cleanup);
    g_test_add_func("/fs/conflicts-protection", test_conflict_scan_and_protection);
    g_test_add_func("/fs/create-permission-denied", test_create_permission_denied);
    g_test_add_func("/fs/install/replace", test_install_replace);
    g_test_add_func("/fs/install/create", test_install_create);
    g_test_add_func("/fs/install/reject-unsafe", test_install_rejects_unsafe_sources_and_targets);
    g_test_add_func("/fs/install/write-failure-cleanup", test_install_write_failure_cleanup);
    g_test_add_func("/fs/remove-tree/confirmed", test_remove_tree);
    g_test_add_func("/fs/remove-tree/permission-denied", test_remove_tree_permission_denied);
    return g_test_run();
}
