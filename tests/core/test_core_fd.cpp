#include "core/fd.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <utility>

#include <glib.h>

namespace {

bool is_open(int fd) { return fcntl(fd, F_GETFD) != -1; }

void test_lifetime() {
    int first_pipe[2]{};
    int second_pipe[2]{};
    g_assert_cmpint(pipe(first_pipe), ==, 0);
    g_assert_cmpint(pipe(second_pipe), ==, 0);

    const int first = first_pipe[0];
    const int second = second_pipe[0];
    {
        filescope::core::UniqueFd owner(first);
        g_assert_true(owner);
        owner.reset(second);
        g_assert_false(is_open(first));
        g_assert_true(is_open(second));

        filescope::core::UniqueFd moved(std::move(owner));
        filescope::core::UniqueFd assigned;
        assigned = std::move(moved);
        g_assert_cmpint(assigned.release(), ==, second);
    }
    g_assert_true(is_open(second));
    g_assert_cmpint(close(second), ==, 0);
    g_assert_cmpint(close(first_pipe[1]), ==, 0);
    g_assert_cmpint(close(second_pipe[1]), ==, 0);
}

void test_move_assignment() {
    int first_pipe[2]{};
    int second_pipe[2]{};
    g_assert_cmpint(pipe(first_pipe), ==, 0);
    g_assert_cmpint(pipe(second_pipe), ==, 0);
    {
        filescope::core::UniqueFd first(first_pipe[0]);
        filescope::core::UniqueFd second(second_pipe[0]);
        second = std::move(first);
        g_assert_false(is_open(second_pipe[0]));
        g_assert_true(is_open(first_pipe[0]));
    }
    g_assert_false(is_open(first_pipe[0]));
    g_assert_cmpint(close(first_pipe[1]), ==, 0);
    g_assert_cmpint(close(second_pipe[1]), ==, 0);
}

void test_destruction() {
    int descriptors[2]{};
    g_assert_cmpint(pipe(descriptors), ==, 0);
    const int owned = descriptors[0];
    { filescope::core::UniqueFd owner(owned); }
    g_assert_false(is_open(owned));
    g_assert_cmpint(close(descriptors[1]), ==, 0);
}

} // namespace

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, nullptr);
    g_test_add_func("/core/fd/lifetime", test_lifetime);
    g_test_add_func("/core/fd/move-assignment", test_move_assignment);
    g_test_add_func("/core/fd/destruction", test_destruction);
    return g_test_run();
}
