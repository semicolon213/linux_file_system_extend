#pragma once

#include "core/types.hpp"
#include "fs/fs.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace filescope::trash {

struct Item {
    std::string id;
    std::string original_path;
    std::optional<std::int64_t> deleted_at_us;
    core::FileType type = core::FileType::OTHER;
    std::uint64_t size = 0;
    bool info_missing = false;
};

using ListSlot = sigc::slot<void(core::Result<std::vector<Item>>)>;

std::string trash_dir();
// [main] Delivers the listing once on the caller's main context.
void list_async(const Glib::RefPtr<Gio::Cancellable> &cancellable, ListSlot done);
// [main] Submits a no-overwrite restore for each id.
core::Result<job::JobId> restore(job::JobQueue &queue, const std::vector<std::string> &ids);
// [main] Submits permanent deletion for the confirmed selection.
core::Result<job::JobId> delete_items(job::JobQueue &queue, const std::vector<std::string> &ids,
                                      fs::Confirmation confirmation);
// [main] Enumerates the trash when the confirmed job runs.
core::Result<job::JobId> empty(job::JobQueue &queue, fs::Confirmation confirmation);

} // namespace filescope::trash
