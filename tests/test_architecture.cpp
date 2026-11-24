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

void write_source(const std::string &root, const char *include) {
    std::ofstream source(Glib::build_filename(root, "src", "core", "sample.cpp"));
    g_assert_true(source.good());
    source << "#include \"" << include << "\"\n";
    source.close();
    g_assert_true(source.good());
}

void copy_architecture_script(const std::string &root) {
    const std::string source_path =
        Glib::build_filename(FILESCOPE_SOURCE_DIR, "scripts", "check-architecture.sh");
    const std::string target_path = Glib::build_filename(root, "scripts", "check-architecture.sh");
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
    const std::string scripts = Glib::build_filename(environment.root(), "scripts");
    const std::string core = Glib::build_filename(environment.root(), "src", "core");
    g_assert_cmpint(g_mkdir_with_parents(scripts.c_str(), 0700), ==, 0);
    g_assert_cmpint(g_mkdir_with_parents(core.c_str(), 0700), ==, 0);
    copy_architecture_script(environment.root());

    write_source(environment.root(), "mon/mon.hpp");
    g_assert_false(run_architecture_check(environment.root()));
    write_source(environment.root(), "gtkmm.h");
    g_assert_false(run_architecture_check(environment.root()));
    write_source(environment.root(), "core/error.hpp");
    g_assert_true(run_architecture_check(environment.root()));
}

} // namespace

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, nullptr);
    g_test_add_func("/architecture/core/dependency-check",
                    test_architecture_rejects_core_dependencies);
    return g_test_run();
}
