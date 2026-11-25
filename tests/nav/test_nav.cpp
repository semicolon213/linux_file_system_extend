#include "nav/nav.hpp"

#include <chrono>
#include <cstdlib>
#include <fcntl.h>
#include <optional>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

#include <giomm/init.h>
#include <glib/gstdio.h>
#include <glibmm/fileutils.h>
#include <glibmm/main.h>
#include <glibmm/miscutils.h>
#include <sigc++/sigc++.h>

#include "test_support.hpp"

namespace {

using filescope::core::Entry;
using filescope::core::ErrorCode;
using filescope::core::FileType;
using filescope::core::Result;

template <typename T> T &required(std::optional<T> &value) {
    if (!value.has_value()) {
        g_error("expected optional value");
    }
    return *value;
}

void write_fixture(const std::string &path) {
    GError *error = nullptr;
    g_assert_true(g_file_set_contents(path.c_str(), "x", 1, &error));
    g_assert_no_error(error);
}

const Entry *find_entry(const std::vector<Entry> &entries, const std::string &name) {
    for (const auto &entry : entries) {
        if (entry.name == name) {
            return &entry;
        }
    }
    return nullptr;
}

Result<std::vector<Entry>> list_dir(const std::string &path, bool show_hidden = false,
                                    bool dirs_only = false,
                                    const Glib::RefPtr<Gio::Cancellable> &cancellable = {}) {
    std::optional<Result<std::vector<Entry>>> result;
    filescope::nav::list_dir_async(
        path, show_hidden, dirs_only, cancellable,
        [&result](Result<std::vector<Entry>> value) { result.emplace(std::move(value)); });
    g_assert_true(filescope::test::wait_until([&result] { return result.has_value(); }, 10000));
    if (!result.has_value()) {
        g_error("list_dir callback timed out");
    }
    return std::move(*result);
}

Result<std::vector<std::string>> complete(const std::string &input, std::size_t max_results) {
    std::optional<Result<std::vector<std::string>>> result;
    filescope::nav::complete_async(
        input, false, max_results, {},
        [&result](Result<std::vector<std::string>> value) { result.emplace(std::move(value)); });
    g_assert_true(filescope::test::wait_until([&result] { return result.has_value(); }, 10000));
    if (!result.has_value()) {
        g_error("complete callback timed out");
    }
    return std::move(*result);
}

void test_list_dir() {
    filescope::test::TestEnvironment environment;
    const std::string fixture = Glib::build_filename(environment.root(), "fixture");
    g_assert_cmpint(g_mkdir(fixture.c_str(), 0700), ==, 0);
    write_fixture(Glib::build_filename(fixture, "a.txt"));
    write_fixture(Glib::build_filename(fixture, "b.txt"));
    write_fixture(Glib::build_filename(fixture, ".hidden"));
    g_assert_cmpint(g_mkdir(Glib::build_filename(fixture, "dir").c_str(), 0700), ==, 0);
    g_assert_cmpint(symlink("dir", Glib::build_filename(fixture, "dir-link").c_str()), ==, 0);
    g_assert_cmpint(symlink("missing", Glib::build_filename(fixture, "broken").c_str()), ==, 0);
    const std::string invalid_name{"bad\xff", 4};
    const int invalid_fd = open(Glib::build_filename(fixture, invalid_name).c_str(),
                                O_CREAT | O_WRONLY | O_EXCL, 0600);
    g_assert_cmpint(invalid_fd, >=, 0);
    g_assert_cmpint(close(invalid_fd), ==, 0);

    const std::thread::id main_thread = std::this_thread::get_id();
    unsigned calls = 0;
    std::optional<Result<std::vector<Entry>>> result;
    filescope::nav::list_dir_async(
        fixture, false, false, {},
        [&result, &calls, main_thread](Result<std::vector<Entry>> value) {
            g_assert_true(std::this_thread::get_id() == main_thread);
            ++calls;
            result.emplace(std::move(value));
        });
    g_assert_true(filescope::test::wait_until([&result] { return result.has_value(); }, 10000));
    g_assert_cmpuint(calls, ==, 1);
    if (!result.has_value()) {
        g_error("list_dir callback timed out");
    }
    g_assert_true(result.value().ok());
    const auto &entries = result.value().value();
    g_assert_cmpuint(entries.size(), ==, 6);
    g_assert_null(find_entry(entries, "."));
    g_assert_null(find_entry(entries, ".."));
    g_assert_null(find_entry(entries, ".hidden"));
    const Entry *link = find_entry(entries, "dir-link");
    g_assert_nonnull(link);
    g_assert_true(link->type == FileType::SYMLINK);
    g_assert_true(link->target_type == FileType::DIRECTORY);
    g_assert_false(link->is_broken_link);
    const Entry *broken = find_entry(entries, "broken");
    g_assert_nonnull(broken);
    g_assert_true(broken->type == FileType::SYMLINK);
    g_assert_true(broken->is_broken_link);
    g_assert_nonnull(find_entry(entries, invalid_name));

    auto hidden = list_dir(fixture, true);
    g_assert_true(hidden.ok());
    g_assert_cmpuint(hidden.value().size(), ==, 7);
    g_assert_true(find_entry(hidden.value(), ".hidden")->is_hidden);

    auto directories = list_dir(fixture, true, true);
    g_assert_true(directories.ok());
    g_assert_cmpuint(directories.value().size(), ==, 2);
    g_assert_nonnull(find_entry(directories.value(), "dir"));
    g_assert_nonnull(find_entry(directories.value(), "dir-link"));
}

void test_list_dir_thread_default_context() {
    filescope::test::TestEnvironment environment;
    const auto context = Glib::MainContext::create();
    const auto loop = Glib::MainLoop::create(context);
    const auto timeout = Glib::TimeoutSource::create(5000);
    timeout->connect([&loop] {
        loop->quit();
        return false;
    });
    timeout->attach(context);
    bool delivered = false;
    context->push_thread_default();
    filescope::nav::list_dir_async(
        environment.root(), false, false, {},
        [&delivered, &loop, &context](const Result<std::vector<Entry>> &result) {
            g_assert_true(g_main_context_is_owner(context->gobj()));
            g_assert_true(result.ok());
            delivered = true;
            loop->quit();
        });
    context->pop_thread_default();
    g_assert_false(delivered);
    loop->run();
    timeout->destroy();
    g_assert_true(delivered);
}

void test_list_dir_failures() {
    filescope::test::TestEnvironment environment;
    auto cancellable = Gio::Cancellable::create();
    cancellable->cancel();
    auto cancelled = list_dir(environment.root(), false, false, cancellable);
    g_assert_false(cancelled.ok());
    g_assert_true(cancelled.error().code == ErrorCode::CANCELLED);

    auto invalid = list_dir("relative");
    g_assert_false(invalid.ok());
    g_assert_true(invalid.error().code == ErrorCode::INVALID_PATH);

    if (geteuid() == 0) {
        g_test_skip("permission behavior is not meaningful as root");
        return;
    }
    const std::string denied = Glib::build_filename(environment.root(), "denied");
    g_assert_cmpint(g_mkdir(denied.c_str(), 0700), ==, 0);
    g_assert_cmpint(g_chmod(denied.c_str(), 0000), ==, 0);
    auto permission = list_dir(denied);
    g_assert_false(permission.ok());
    g_assert_true(permission.error().code == ErrorCode::PERMISSION_DENIED);
    g_assert_cmpint(g_chmod(denied.c_str(), 0700), ==, 0);
}

class Receiver : public sigc::trackable {
  public:
    explicit Receiver(unsigned &calls) : calls_(calls) {}
    void receive(const Result<std::vector<Entry>> &) { ++calls_; }

  private:
    unsigned &calls_;
};

void test_disconnected_slot() {
    filescope::test::TestEnvironment environment;
    unsigned calls = 0;
    {
        Receiver receiver(calls);
        filescope::nav::list_dir_async(environment.root(), false, false, {},
                                       sigc::mem_fun(receiver, &Receiver::receive));
    }
    g_assert_false(filescope::test::wait_until([] { return false; }, 100));
    while (g_main_context_iteration(g_main_context_default(), false)) {
    }
    g_assert_cmpuint(calls, ==, 0);
}

void test_list_dir_stress() {
    filescope::test::TestEnvironment environment;
    const std::string fixture = Glib::build_filename(environment.root(), "fixture");
    g_assert_cmpint(g_mkdir(fixture.c_str(), 0700), ==, 0);
    for (unsigned i = 0; i < 10000; ++i) {
        const std::string path =
            Glib::build_filename(fixture, Glib::ustring::compose("entry-%1", i));
        const int fd = open(path.c_str(), O_CREAT | O_WRONLY | O_EXCL, 0600);
        g_assert_cmpint(fd, >=, 0);
        g_assert_cmpint(close(fd), ==, 0);
    }
    const auto started = std::chrono::steady_clock::now();
    auto result = list_dir(fixture);
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    g_test_message("listed 10000 entries in %.3f seconds", seconds);
    g_assert_true(result.ok());
    g_assert_cmpuint(result.value().size(), ==, 10000);
}

void test_history_parent_breadcrumb() {
    filescope::nav::History history("/A");
    history.push("/B");
    history.push("/C");
    auto destination = history.back();
    g_assert_cmpstr(required(destination).c_str(), ==, "/B");
    destination = history.back();
    g_assert_cmpstr(required(destination).c_str(), ==, "/A");
    g_assert_false(history.back().has_value());
    destination = history.forward();
    g_assert_cmpstr(required(destination).c_str(), ==, "/B");
    history.push("/D");
    g_assert_false(history.can_forward());
    history.push("/D");
    g_assert_cmpstr(history.current().c_str(), ==, "/D");

    filescope::nav::History limited("/0");
    for (unsigned i = 1; i <= 100; ++i) {
        limited.push("/" + std::to_string(i));
    }
    for (unsigned i = 0; i < 99; ++i) {
        g_assert_true(limited.back().has_value());
    }
    g_assert_cmpstr(limited.current().c_str(), ==, "/1");
    g_assert_false(limited.back().has_value());

    auto parent = filescope::nav::parent("/home/user/");
    g_assert_cmpstr(required(parent).c_str(), ==, "/home");
    g_assert_false(filescope::nav::parent("/").has_value());
    const auto crumbs = filescope::nav::breadcrumb("/home/u/p");
    g_assert_cmpuint(crumbs.size(), ==, 4);
    g_assert_cmpstr(crumbs[0].c_str(), ==, "/");
    g_assert_cmpstr(crumbs[3].c_str(), ==, "/home/u/p");
    const std::string invalid_path{"/x/\xff", 4};
    const auto invalid_crumbs = filescope::nav::breadcrumb(invalid_path);
    g_assert_cmpuint(invalid_crumbs.size(), ==, 3);
    g_assert_cmpmem(invalid_crumbs[2].data(), invalid_crumbs[2].size(), invalid_path.data(),
                    invalid_path.size());
}

void test_complete() {
    filescope::test::TestEnvironment environment;
    const std::string proj = Glib::build_filename(environment.root(), "proj");
    const std::string prof = Glib::build_filename(environment.root(), "prof");
    const std::string file = Glib::build_filename(environment.root(), "pro.txt");
    g_assert_cmpint(g_mkdir(proj.c_str(), 0700), ==, 0);
    g_assert_cmpint(g_mkdir(prof.c_str(), 0700), ==, 0);
    write_fixture(file);

    auto result = complete(Glib::build_filename(environment.root(), "pro"), 10);
    g_assert_true(result.ok());
    g_assert_cmpuint(result.value().size(), ==, 2);
    g_assert_cmpstr(result.value()[0].c_str(), ==, prof.c_str());
    g_assert_cmpstr(result.value()[1].c_str(), ==, proj.c_str());
    result = complete(Glib::build_filename(environment.root(), "pro"), 1);
    g_assert_true(result.ok());
    g_assert_cmpuint(result.value().size(), ==, 1);
    auto invalid = complete("relative", 1);
    g_assert_false(invalid.ok());
    g_assert_true(invalid.error().code == ErrorCode::INVALID_PATH);
}

void test_home_subprocess() {
    if (g_test_subprocess()) {
        const std::string actual = filescope::nav::home();
        g_assert_cmpstr(actual.c_str(), ==, std::getenv("HOME"));
        return;
    }
    g_setenv("HOME", "/tmp/filescope-home-test", TRUE);
    g_test_trap_subprocess(nullptr, 0, G_TEST_SUBPROCESS_DEFAULT);
    g_test_trap_assert_passed();
}

void test_bookmarks() {
    filescope::test::TestEnvironment environment;
    auto loaded = filescope::nav::Bookmarks::load();
    g_assert_true(loaded.ok());
    auto bookmarks = std::move(loaded).value();
    g_assert_true(bookmarks.add("/one").ok());
    g_assert_true(bookmarks.add("/two").ok());
    auto duplicate = bookmarks.add("/one");
    g_assert_false(duplicate.ok());
    g_assert_true(duplicate.error().code == ErrorCode::EXISTS);
    auto invalid = bookmarks.add("relative");
    g_assert_false(invalid.ok());
    g_assert_true(invalid.error().code == ErrorCode::INVALID_PATH);
    g_assert_true(bookmarks.save().ok());

    loaded = filescope::nav::Bookmarks::load();
    g_assert_true(loaded.ok());
    bookmarks = std::move(loaded).value();
    g_assert_cmpuint(bookmarks.list().size(), ==, 2);
    g_assert_cmpstr(bookmarks.list()[0].c_str(), ==, "/one");
    g_assert_cmpstr(bookmarks.list()[1].c_str(), ==, "/two");
    g_assert_true(bookmarks.remove("/one"));
    g_assert_false(bookmarks.remove("/missing"));

    const std::string config_path =
        Glib::build_filename(std::getenv("XDG_CONFIG_HOME"), "filescope", "bookmarks.ini");
    GError *error = nullptr;
    g_assert_true(g_file_set_contents(config_path.c_str(), "broken [", -1, &error));
    g_assert_no_error(error);
    loaded = filescope::nav::Bookmarks::load();
    g_assert_false(loaded.ok());
    g_assert_true(loaded.error().code == ErrorCode::FAILED);
    const std::string contents = Glib::file_get_contents(config_path);
    g_assert_cmpstr(contents.c_str(), ==, "broken [");
}

} // namespace

int main(int argc, char **argv) {
    Gio::init();
    g_test_init(&argc, &argv, nullptr);
    g_test_add_func("/nav/list-dir", test_list_dir);
    g_test_add_func("/nav/list-dir-thread-default", test_list_dir_thread_default_context);
    g_test_add_func("/nav/list-dir-failures", test_list_dir_failures);
    g_test_add_func("/nav/disconnected-slot", test_disconnected_slot);
    g_test_add_func("/nav/list-dir-stress", test_list_dir_stress);
    g_test_add_func("/nav/history-parent-breadcrumb", test_history_parent_breadcrumb);
    g_test_add_func("/nav/complete", test_complete);
    g_test_add_func("/nav/home", test_home_subprocess);
    g_test_add_func("/nav/bookmarks", test_bookmarks);
    return g_test_run();
}
