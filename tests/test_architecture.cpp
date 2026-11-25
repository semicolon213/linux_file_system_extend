#include "test_support.hpp"

#include <fstream>
#include <string>
#include <vector>

#include <glib.h>
#include <glib/gstdio.h>
#include <glibmm.h>

namespace {

bool run_architecture_check(const std::string &root) {
    int status = 0;
    const std::vector<std::string> arguments = {"bash", "scripts/check-architecture.sh"};
    Glib::spawn_sync(root, arguments, Glib::SPAWN_SEARCH_PATH, sigc::slot<void()>(), nullptr,
                     nullptr, &status);
    return g_spawn_check_wait_status(status, nullptr);
}

void write_source(const std::string &directory, const char *include) {
    g_assert_cmpint(g_mkdir_with_parents(directory.c_str(), 0700), ==, 0);
    std::ofstream source(Glib::build_filename(directory, "sample.cpp"));
    g_assert_true(source.good());
    source << "#include \"" << include << "\"\n";
    source.close();
    g_assert_true(source.good());
}

void copy_architecture_script(const std::string &root) {
    const std::string source_path =
        Glib::build_filename(FILESCOPE_SOURCE_DIR, "scripts", "check-architecture.sh");
    const std::string scripts = Glib::build_filename(root, "scripts");
    g_assert_cmpint(g_mkdir_with_parents(scripts.c_str(), 0700), ==, 0);
    const std::string target_path = Glib::build_filename(scripts, "check-architecture.sh");
    std::ifstream source(source_path, std::ios::binary);
    std::ofstream target(target_path, std::ios::binary);
    g_assert_true(source.good());
    g_assert_true(target.good());
    target << source.rdbuf();
    target.close();
    g_assert_true(target.good());
}

void test_architecture_rejects_core_dependencies() {
    filescope::test::TestEnvironment environment;
    copy_architecture_script(environment.root());
    const std::string core = Glib::build_filename(environment.root(), "src", "core");

    write_source(core, "mon/mon.hpp");
    g_assert_false(run_architecture_check(environment.root()));
    write_source(core, "gtkmm.h");
    g_assert_false(run_architecture_check(environment.root()));
    write_source(core, "core/error.hpp");
    g_assert_true(run_architecture_check(environment.root()));
}

struct DependencyCase {
    const char *path;
    const char *module;
    const char *include;
    bool allowed;
};

constexpr DependencyCase r2_dependencies[] = {
    {"/architecture/diff/gtk", "diff", "gtkmm.h", false},
    {"/architecture/diff/pixbuf", "diff", "gdk-pixbuf/gdk-pixbuf.h", false},
    {"/architecture/search/history", "search", "his/his.hpp", false},
    {"/architecture/trash/watcher", "trash", "mon/mon.hpp", false},
    {"/architecture/fs/sqlite", "fs", "sqlite3.h", false},
    {"/architecture/his/ayatana", "his", "libayatana-appindicator/app-indicator.h", false},
    {"/architecture/his/sqlite", "his", "sqlite3.h", true},
    {"/architecture/ui/ayatana", "ui", "libayatana-appindicator/app-indicator.h", true},
};

void test_architecture_r2_dependency(gconstpointer data) {
    const auto &test_case = *static_cast<const DependencyCase *>(data);
    filescope::test::TestEnvironment environment;
    copy_architecture_script(environment.root());
    write_source(Glib::build_filename(environment.root(), "src", test_case.module),
                 test_case.include);
    g_assert_cmpint(run_architecture_check(environment.root()), ==, test_case.allowed);
}

} // namespace

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, nullptr);
    g_test_add_func("/architecture/core/dependency-check",
                    test_architecture_rejects_core_dependencies);
    for (const auto &test_case : r2_dependencies) {
        g_test_add_data_func(test_case.path, &test_case, test_architecture_r2_dependency);
    }
    return g_test_run();
}
