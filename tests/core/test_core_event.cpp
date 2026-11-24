#include "core/event.hpp"

#include <array>
#include <memory>
#include <thread>
#include <utility>

#include <glib.h>

namespace {

void copy_event(const filescope::core::EventPtr &event) {
    for (unsigned i = 0; i < 100000; ++i) {
        auto copy = event;
        g_assert_true(copy->seq == 1);
        copy.reset();
    }
}

void test_event_sharing() {
    filescope::core::Event source;
    source.seq = 1;
    source.kind = filescope::core::EventKind::RENAME;
    source.synthetic = true;
    source.path = "/tmp/new";
    source.old_path = "/tmp/old";
    source.stat = filescope::core::EventStat{10, 11, 12};
    source.size_delta = -13;
    source.root_total = 14;

    auto event = filescope::core::make_event(std::move(source));
    const std::weak_ptr<const filescope::core::Event> weak = event;
    std::thread first(copy_event, std::cref(event));
    std::thread second(copy_event, std::cref(event));
    first.join();
    second.join();
    g_assert_cmpstr(event->path.c_str(), ==, "/tmp/new");
    g_assert_cmpint(event->size_delta.value_or(0), ==, -13);
    event.reset();
    g_assert_true(weak.expired());
}

void test_kind_strings() {
    static constexpr std::array kinds = {
        filescope::core::EventKind::CREATE,    filescope::core::EventKind::DELETE,
        filescope::core::EventKind::MODIFY,    filescope::core::EventKind::CLOSE_WRITE,
        filescope::core::EventKind::ATTRIB,    filescope::core::EventKind::MOVED_OUT,
        filescope::core::EventKind::MOVED_IN,  filescope::core::EventKind::RENAME,
        filescope::core::EventKind::MOVE,      filescope::core::EventKind::DELETE_SELF,
        filescope::core::EventKind::MOVE_SELF, filescope::core::EventKind::UNMOUNT,
        filescope::core::EventKind::OVERFLOW,  filescope::core::EventKind::RESYNC_DONE,
    };
    static constexpr std::array names = {
        "CREATE", "DELETE", "MODIFY",      "CLOSE_WRITE", "ATTRIB",  "MOVED_OUT", "MOVED_IN",
        "RENAME", "MOVE",   "DELETE_SELF", "MOVE_SELF",   "UNMOUNT", "OVERFLOW",  "RESYNC_DONE",
    };
    for (std::size_t i = 0; i < kinds.size(); ++i) {
        g_assert_cmpstr(filescope::core::to_string(kinds[i]), ==, names[i]);
    }
}

} // namespace

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, nullptr);
    g_test_add_func("/core/event/sharing", test_event_sharing);
    g_test_add_func("/core/event/kind-strings", test_kind_strings);
    return g_test_run();
}
