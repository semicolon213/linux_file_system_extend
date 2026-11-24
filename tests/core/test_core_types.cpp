#include "core/types.hpp"

#include <glib.h>

namespace {

void test_entry_value() {
    filescope::core::Entry source;
    source.name = "entry";
    source.type = filescope::core::FileType::SYMLINK;
    source.target_type = filescope::core::FileType::DIRECTORY;
    source.size = 42;
    source.ino = 7;

    auto copy = source;
    source.name = "changed";
    g_assert_cmpstr(copy.name.c_str(), ==, "entry");
    g_assert_true(copy.type == filescope::core::FileType::SYMLINK);
    g_assert_cmpuint(copy.size, ==, 42);
    g_assert_cmpuint(copy.ino, ==, 7);
}

} // namespace

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, nullptr);
    g_test_add_func("/core/types/entry-value", test_entry_value);
    return g_test_run();
}
