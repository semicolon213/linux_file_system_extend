#include "search/search.hpp"

#include <string>
#include <unistd.h>

#include <giomm/init.h>
#include <glib.h>
#include <glib/gstdio.h>

#include "test_support.hpp"

namespace {

using filescope::core::ErrorCode;
using filescope::search::Query;
using filescope::search::Searcher;

void test_validation() {
    const filescope::test::TestEnvironment environment;
    Searcher searcher;
    Query query;
    const auto check = [&](const std::string &path, ErrorCode expected) {
        query.root = path;
        const auto result = searcher.start(query);
        g_assert_false(result.ok());
        g_assert_true(result.error().code == expected);
    };
    check("relative", ErrorCode::INVALID_PATH);
    check(environment.root() + std::string(1, '\0'), ErrorCode::INVALID_PATH);
    check("/" + std::string(4096, 'a'), ErrorCode::NAME_TOO_LONG);
    check(environment.root() + "/missing", ErrorCode::NOT_FOUND);
    const auto file = environment.root() + "/file";
    g_assert_true(g_file_set_contents(file.c_str(), "", 0, nullptr));
    check(file, ErrorCode::NOT_DIRECTORY);
    const auto link = environment.root() + "/link";
    g_assert_cmpint(symlink(environment.root().c_str(), link.c_str()), ==, 0);
    check(link + "/", ErrorCode::NOT_DIRECTORY);
    g_assert_false(searcher.cancel(0));
    g_assert_false(searcher.cancel(123));
}

void test_empty_and_zero_cap() {
    const filescope::test::TestEnvironment environment;
    Searcher searcher;
    Query query;
    query.root = environment.root() + "/empty";
    g_assert_cmpint(g_mkdir(query.root.c_str(), 0700), ==, 0);
    searcher.signal_hits().connect([](auto, const auto &) { g_assert_not_reached(); });
    bool done = false;
    bool expect_truncated = false;
    searcher.signal_done().connect([&](auto, const auto &result) {
        g_assert_false(done);
        g_assert_true(result.ok());
        g_assert_cmpuint(result.value().hits, ==, 0);
        g_assert_true(result.value().truncated == expect_truncated);
        done = true;
    });
    const auto empty = searcher.start(query);
    g_assert_true(empty.ok());
    g_assert_true(filescope::test::wait_until([&] { return done; }, 2000));
    g_assert_false(searcher.cancel(empty.value()));

    done = false;
    expect_truncated = true;
    g_assert_true(g_file_set_contents((query.root + "/file").c_str(), "", 0, nullptr));
    query.max_results = 0;
    const auto zero_cap = searcher.start(query);
    g_assert_true(zero_cap.ok());
    g_assert_cmpuint(empty.value(), !=, zero_cap.value());
    g_assert_true(filescope::test::wait_until([&] { return done; }, 2000));
}

} // namespace

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, nullptr);
    Gio::init();
    g_test_add_func("/search/validation", test_validation);
    g_test_add_func("/search/empty-and-zero-cap", test_empty_and_zero_cap);
    return g_test_run();
}
