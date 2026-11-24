#include "core/event.hpp"

#include <array>
#include <utility>

namespace filescope::core {

EventPtr make_event(Event event) { return std::make_shared<const Event>(std::move(event)); }

const char *to_string(EventKind kind) noexcept {
    static constexpr std::array<const char *, 14> names = {
        "CREATE", "DELETE", "MODIFY",      "CLOSE_WRITE", "ATTRIB",  "MOVED_OUT", "MOVED_IN",
        "RENAME", "MOVE",   "DELETE_SELF", "MOVE_SELF",   "UNMOUNT", "OVERFLOW",  "RESYNC_DONE",
    };
    const auto index = static_cast<std::size_t>(kind);
    return index < names.size() ? names[index] : "UNKNOWN";
}

} // namespace filescope::core
