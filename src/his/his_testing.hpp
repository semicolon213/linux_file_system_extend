#pragma once

#include <functional>

#include "his/his.hpp"

namespace filescope::his::testing {

enum class FaultPoint : std::uint8_t { AFTER_BLOB_RENAME, AFTER_COMMIT_BEFORE_UNLINK };

void set_fault(History &history, FaultPoint point);
void flush(History &history);
void capture(History &history, const std::string &path, Origin origin = Origin::AUTO,
             std::function<void()> after_copy = {});
std::size_t pending_events(History &history);

// Separate SQLite connection for fixtures, integrity queries and a held writer lock.
using Rows = std::vector<std::vector<std::optional<std::string>>>;
core::Result<Rows> query(const std::string &db_path, const std::string &sql,
                         const std::function<void()> &while_open = {});

} // namespace filescope::his::testing
