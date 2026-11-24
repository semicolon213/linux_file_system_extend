#include "app_info.h"

#include <glib.h>

static void test_app_info(void) {
    g_assert_cmpstr(filescope_app_name(), ==, "FileScope");
    g_assert_cmpstr(filescope_app_version(), ==, "0.1.0");
}

int main(int argc, char *argv[]) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/filescope/app-info", test_app_info);
    return g_test_run();
}
