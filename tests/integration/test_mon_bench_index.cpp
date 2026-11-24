#include <chrono>
#include <fstream>
#include <sys/resource.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "mon/mon.hpp"
#include "test_support.hpp"

namespace {

std::uint64_t resident_bytes() {
    std::ifstream status("/proc/self/status");
    std::string key;
    while (status >> key) {
        if (key == "VmRSS:") {
            std::uint64_t kib = 0;
            status >> kib;
            return kib * 1024;
        }
        std::string rest;
        std::getline(status, rest);
    }
    return 0;
}

void test_large_index_budget() {
    filescope::test::TestEnvironment environment;
    const std::string root = environment.root() + "/watch";
    g_assert_cmpint(g_mkdir(root.c_str(), 0700), ==, 0);
    for (int directory_index = 0; directory_index < 10000; ++directory_index) {
        const std::string directory = root + "/d" + std::to_string(directory_index);
        g_assert_cmpint(g_mkdir(directory.c_str(), 0700), ==, 0);
        for (int file_index = 0; file_index < 10; ++file_index) {
            std::ofstream(directory + "/f" + std::to_string(file_index)) << 'x';
        }
    }

    const std::uint64_t before = resident_bytes();
    const auto started = std::chrono::steady_clock::now();
    auto created = filescope::mon::Watcher::create();
    g_assert_true(created.ok());
    auto watcher = std::move(created).value();
    auto added = watcher->add_root(root, filescope::mon::WatchMode::RECURSIVE);
    g_assert_true(added.ok());
    g_assert_true(filescope::test::wait_until(
        [&] { return watcher->root_total(added.value()).has_value(); }, 30000));
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started);
    const std::uint64_t after = resident_bytes();
    const std::uint64_t delta = after > before ? after - before : 0;
    const auto total = watcher->root_total(added.value());
    const auto measured = total.value_or(filescope::mon::RootTotal{});
    g_assert_cmpuint(measured.file_count, ==, 100000);
    g_assert_cmpuint(measured.total_bytes, ==, 100000);
    g_test_message(
        "benchmark files=100000 directories=10000 register_s=%.3f rss_delta=%" G_GUINT64_FORMAT,
        elapsed.count(), delta);
#if !defined(__SANITIZE_ADDRESS__) && !defined(__SANITIZE_THREAD__)
    g_assert_cmpuint(delta, <=, std::uint64_t{16} * 1024U * 1024U);
#endif
}

} // namespace

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, nullptr);
    g_test_add_func("/mon/benchmark/index-budget", test_large_index_budget);
    return g_test_run();
}
