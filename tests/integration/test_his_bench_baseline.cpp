#include <chrono>
#include <filesystem>
#include <fstream>

#include <glib.h>
#include <glibmm/init.h>

#include "his/his.hpp"
#include "test_support.hpp"

namespace {

using namespace filescope;
using Clock = std::chrono::steady_clock;

void measure_baseline() {
    test::TestEnvironment environment;
    const auto root = environment.root() + "/scan";
    std::filesystem::create_directory(root);
    constexpr unsigned count = 20000;
    for (unsigned i = 0; i < count; ++i) {
        std::ofstream out(root + "/file-" + std::to_string(i));
        out << "baseline";
        out.close();
        g_assert_true(out.good());
    }
    auto opened = his::History::open({environment.root() + "/history", {}});
    g_assert_true(opened.ok());
    auto history = std::move(opened).value();
    unsigned captured = 0;
    std::string first;
    history->signal_version_added().connect([&](const his::Version &version) {
        g_assert_true(version.origin == his::Origin::BASELINE);
        if (++captured == 1) {
            first = version.path;
        }
    });
    const auto start = Clock::now();
    history->scan_root(root, true);
    g_assert_true(test::wait_until([&] { return !first.empty(); }, 1000));
    const auto query_start = Clock::now();
    bool answered = false;
    history->load_history_async(first, [&](core::Result<his::FileHistory> result) {
        g_assert_true(result.ok());
        g_assert_cmpuint(result.value().versions.size(), ==, 1);
        g_assert_cmpuint(captured, <, count);
        answered = true;
    });
    g_assert_true(test::wait_until([&] { return answered; }, 1000));
    const auto latency =
        std::chrono::duration<double, std::milli>(Clock::now() - query_start).count();
    g_assert_true(test::wait_until([&] { return captured >= count; }, 90000));
    const auto seconds = std::chrono::duration<double>(Clock::now() - start).count();
    g_assert_cmpuint(captured, ==, count);
    history->shutdown();
    g_test_message("G8 baseline: %u files in %.3f s (%.2f files/s), history query %.3f ms", count,
                   seconds, count / seconds, latency);
}

} // namespace

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, nullptr);
    Glib::init();
    g_test_add_func("/his/g8/baseline", measure_baseline);
    return g_test_run();
}
