#include <glib.h>
#include <glib/gstdio.h>

#include "core/error.hpp"
#include "mon/mon.hpp"
#include "test_support.hpp"

namespace {

void test_validation_and_overlap() {
    filescope::test::TestEnvironment environment;
    const std::string root = environment.root() + "/root";
    const std::string child = root + "/child";
    g_assert_cmpint(g_mkdir(root.c_str(), 0700), ==, 0);
    g_assert_cmpint(g_mkdir(child.c_str(), 0700), ==, 0);

    auto created = filescope::mon::Watcher::create();
    g_assert_true(created.ok());
    auto watcher = std::move(created).value();

    auto relative = watcher->add_root("relative", filescope::mon::WatchMode::NON_RECURSIVE);
    g_assert_false(relative.ok());
    g_assert_cmpint(static_cast<int>(relative.error().code), ==,
                    static_cast<int>(filescope::core::ErrorCode::INVALID_PATH));

    auto missing = watcher->add_root(root + "/missing", filescope::mon::WatchMode::NON_RECURSIVE);
    g_assert_false(missing.ok());
    g_assert_cmpint(static_cast<int>(missing.error().code), ==,
                    static_cast<int>(filescope::core::ErrorCode::NOT_FOUND));

    auto added = watcher->add_root(root, filescope::mon::WatchMode::NON_RECURSIVE);
    g_assert_true(added.ok());
    auto overlap = watcher->add_root(child, filescope::mon::WatchMode::NON_RECURSIVE);
    g_assert_false(overlap.ok());
    g_assert_cmpint(static_cast<int>(overlap.error().code), ==,
                    static_cast<int>(filescope::core::ErrorCode::EXISTS));
    g_assert_cmpuint(watcher->find_root(root), ==, added.value());
    g_assert_false(watcher->remove_root(999999));
}

} // namespace

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, nullptr);
    g_test_add_func("/mon/api/validation-overlap", test_validation_and_overlap);
    return g_test_run();
}
