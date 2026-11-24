#include "core/path.hpp"

#include <array>
#include <limits.h>
#include <string>
#include <unistd.h>

#include <glib.h>

#include "test_support.hpp"

namespace {

using filescope::core::ErrorCode;

void assert_error(const filescope::core::Result<void> &result, ErrorCode code) {
    g_assert_false(result.ok());
    g_assert_true(result.error().code == code);
}

void test_validate_name() {
    const std::array invalid = {std::string(), std::string("a/b"), std::string("."),
                                std::string(".."), std::string("a\0b", 3)};
    for (const std::string &name : invalid) {
        assert_error(filescope::core::validate_name(name), ErrorCode::INVALID_PATH);
    }
    assert_error(filescope::core::validate_name(std::string(NAME_MAX + 1, 'x')),
                 ErrorCode::NAME_TOO_LONG);
    g_assert_true(filescope::core::validate_name(std::string(NAME_MAX, 'x')).ok());
    g_assert_true(filescope::core::validate_name("notes.md").ok());
}

void test_validate_absolute() {
    assert_error(filescope::core::validate_absolute("relative"), ErrorCode::INVALID_PATH);
    assert_error(filescope::core::validate_absolute(std::string("/a\0b", 4)),
                 ErrorCode::INVALID_PATH);
    std::string long_path(PATH_MAX, 'x');
    long_path.front() = '/';
    assert_error(filescope::core::validate_absolute(long_path), ErrorCode::NAME_TOO_LONG);
    g_assert_true(filescope::core::validate_absolute("/tmp").ok());
}

void test_canonical_and_ancestor() {
    const std::string canonical = filescope::core::canonicalize("/a//b/./c/../d");
    const std::string joined = filescope::core::path_join("/a/b", "c");
    g_assert_cmpstr(canonical.c_str(), ==, "/a/b/d");
    g_assert_cmpstr(joined.c_str(), ==, "/a/b/c");
    g_assert_true(filescope::core::is_ancestor("/a/b", "/a/b/d"));
    g_assert_true(filescope::core::is_ancestor("/a/b", "/a/b"));
    g_assert_false(filescope::core::is_ancestor("/a/b", "/a/bc"));
}

void test_protected_paths() {
    filescope::test::TestEnvironment environment;
    const std::string run_user = "/run/user/" + std::to_string(getuid()) + "/a";
    const std::string temporary_child = environment.root() + "/removable";

    assert_error(filescope::core::check_protected("/usr/bin/x", filescope::core::PathOp::WRITE),
                 ErrorCode::PROTECTED_PATH);
    assert_error(filescope::core::check_protected("/", filescope::core::PathOp::REMOVE),
                 ErrorCode::PROTECTED_PATH);
    assert_error(
        filescope::core::check_protected(environment.root(), filescope::core::PathOp::REMOVE),
        ErrorCode::PROTECTED_PATH);
    g_assert_true(filescope::core::check_protected(run_user, filescope::core::PathOp::WRITE).ok());
    g_assert_true(
        filescope::core::check_protected("/run/media/disk", filescope::core::PathOp::WRITE).ok());
    g_assert_true(
        filescope::core::check_protected(temporary_child, filescope::core::PathOp::REMOVE).ok());
    g_assert_true(
        filescope::core::check_protected("/usr/bin/x", filescope::core::PathOp::READ).ok());
}

void test_access() {
    filescope::test::TestEnvironment environment;
    const std::string new_file = environment.root() + "/new";
    g_assert_true(filescope::core::check_access(new_file, filescope::core::PathOp::WRITE).ok());
    g_assert_true(filescope::core::check_access(new_file, filescope::core::PathOp::REMOVE).ok());
}

} // namespace

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, nullptr);
    g_test_add_func("/core/path/validate-name", test_validate_name);
    g_test_add_func("/core/path/validate-absolute", test_validate_absolute);
    g_test_add_func("/core/path/canonical-ancestor", test_canonical_and_ancestor);
    g_test_add_func("/core/path/protected", test_protected_paths);
    g_test_add_func("/core/path/access", test_access);
    return g_test_run();
}
