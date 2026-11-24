#include <cstring>
#include <sys/inotify.h>

#include <glib.h>

#include "mon/mon_testing.hpp"

namespace {

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void append_raw(std::vector<std::uint8_t> &buffer, int wd, std::uint32_t mask, std::uint32_t cookie,
                const char *name) {
    const std::size_t name_size = name == nullptr ? 0 : std::strlen(name) + 1;
    const std::size_t padded = (name_size + 3U) & ~std::size_t(3U);
    const std::size_t old_size = buffer.size();
    buffer.resize(old_size + sizeof(inotify_event) + padded);
    auto *event = reinterpret_cast<inotify_event *>(buffer.data() + old_size);
    event->wd = wd;
    event->mask = mask;
    event->cookie = cookie;
    event->len = static_cast<std::uint32_t>(padded);
    if (name != nullptr) {
        std::memcpy(event->name, name, name_size);
    }
}

void test_cookie_unknown_and_boundary() {
    std::vector<std::uint8_t> raw;
    append_raw(raw, 4, IN_MOVED_FROM, 22, "old");
    append_raw(raw, 4, IN_MOVED_TO, 22, "new");
    append_raw(raw, 99, IN_CREATE, 0, "ignored");
    append_raw(raw, 4, IN_IGNORED, 0, nullptr);
    append_raw(raw, 4, IN_UNMOUNT, 0, nullptr);
    raw.push_back(0xff);

    const auto events = filescope::mon::testing::normalize_raw_events(raw, {{4, 7, "/tmp/root"}});
    g_assert_cmpuint(events.size(), ==, 2);
    g_assert_cmpint(static_cast<int>(events[0].kind), ==,
                    static_cast<int>(filescope::core::EventKind::RENAME));
    g_assert_cmpstr(events[0].old_path.c_str(), ==, "/tmp/root/old");
    g_assert_cmpstr(events[0].path.c_str(), ==, "/tmp/root/new");
    g_assert_cmpuint(events[0].cookie, ==, 22);
    g_assert_cmpint(static_cast<int>(events[1].kind), ==,
                    static_cast<int>(filescope::core::EventKind::UNMOUNT));
}

void test_unpaired_and_overflow() {
    std::vector<std::uint8_t> raw;
    append_raw(raw, 4, IN_MOVED_FROM, 10, "out");
    append_raw(raw, 4, IN_MOVED_TO, 11, "in");
    append_raw(raw, -1, IN_Q_OVERFLOW, 0, nullptr);
    const auto events = filescope::mon::testing::normalize_raw_events(raw, {{4, 7, "/tmp/root"}});
    g_assert_cmpuint(events.size(), ==, 3);
    g_assert_cmpint(static_cast<int>(events[0].kind), ==,
                    static_cast<int>(filescope::core::EventKind::MOVED_IN));
    g_assert_cmpint(static_cast<int>(events[1].kind), ==,
                    static_cast<int>(filescope::core::EventKind::OVERFLOW));
    g_assert_cmpint(static_cast<int>(events[2].kind), ==,
                    static_cast<int>(filescope::core::EventKind::MOVED_OUT));
}

} // namespace

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, nullptr);
    g_test_add_func("/mon/normalize/cookie-unknown-boundary", test_cookie_unknown_and_boundary);
    g_test_add_func("/mon/normalize/unpaired-overflow", test_unpaired_and_overflow);
    return g_test_run();
}
