#include <chrono>
#include <fstream>
#include <set>

#include <glib.h>
#include <glib/gstdio.h>
#include <glibmm/main.h>

#include "mon/mon.hpp"
#include "test_support.hpp"

namespace {

void test_thousand_events_per_second_for_ten_seconds() {
    filescope::test::TestEnvironment environment;
    const std::string root = environment.root() + "/watch";
    g_assert_cmpint(g_mkdir(root.c_str(), 0700), ==, 0);
    auto created = filescope::mon::Watcher::create();
    g_assert_true(created.ok());
    auto watcher = std::move(created).value();
    std::set<std::string> deleted;
    bool gap = false;
    watcher->signal_events().connect([&](const auto &batch) {
        for (const auto &event : batch) {
            if (event->kind == filescope::core::EventKind::DELETE) {
                deleted.insert(event->path);
            }
            gap = gap || event->kind == filescope::core::EventKind::OVERFLOW;
        }
    });
    auto added = watcher->add_root(root, filescope::mon::WatchMode::NON_RECURSIVE);
    g_assert_true(added.ok());
    g_assert_true(filescope::test::wait_until(
        [&] { return watcher->root_total(added.value()).has_value(); }, 2000));

    const auto started = std::chrono::steady_clock::now();
    std::size_t operations = 0;
    for (int second = 0; second < 10; ++second) {
        const auto interval = std::chrono::steady_clock::now();
        for (int index = 0; index < 500; ++index) {
            const std::string path =
                root + "/f" + std::to_string(second) + "-" + std::to_string(index);
            std::ofstream(path) << 'x';
            g_assert_cmpint(g_remove(path.c_str()), ==, 0);
            operations += 2;
        }
        while (std::chrono::steady_clock::now() - interval < std::chrono::seconds(1)) {
            Glib::MainContext::get_default()->iteration(false);
            g_usleep(1000);
        }
    }
    g_assert_true(filescope::test::wait_until([&] { return gap || deleted.size() == 5000; }, 2000));
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started);
    g_test_message("stress operations=%zu elapsed_s=%.3f rate=%.1f gap=%s", operations,
                   elapsed.count(), static_cast<double>(operations) / elapsed.count(),
                   gap ? "yes" : "no");
}

} // namespace

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, nullptr);
    g_test_add_func("/mon/stress/1000-events-10-seconds",
                    test_thousand_events_per_second_for_ten_seconds);
    return g_test_run();
}
