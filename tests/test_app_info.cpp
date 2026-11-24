#include "app_info.hpp"

#include <glib.h>
#include <sigc++/sigc++.h>

namespace {

void test_app_info() {
    g_assert_cmpstr(filescope::app_name(), ==, "FileScope");
    g_assert_cmpstr(filescope::app_version(), ==, "0.1.0");
}

void test_sigc_functional_notation() {
    sigc::signal<void(int)> signal;
    int received = 0;
    signal.connect([&received](int value) { received = value; });
    signal.emit(7);
    g_assert_cmpint(received, ==, 7);
}

} // namespace

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, nullptr);
    g_test_add_func("/filescope/app-info", test_app_info);
    g_test_add_func("/filescope/sigc/functional-notation", test_sigc_functional_notation);
    return g_test_run();
}
