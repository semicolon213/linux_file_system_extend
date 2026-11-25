#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <glibmm/ustring.h>
#include <sigc++/sigc++.h>

#include "core/error.hpp"
#include "core/types.hpp"

namespace filescope::search {

using SearchId = std::uint64_t;

struct Query {
    std::string root;
    Glib::ustring text;
    std::vector<Glib::ustring> extensions;
    bool recursive = true;
    bool show_hidden = false;
    std::size_t max_results = 5000;
};

struct Hit {
    std::string path;
    core::Entry entry;
};

struct Summary {
    std::size_t hits = 0;
    std::size_t dirs_scanned = 0;
    std::vector<std::string> denied;
    bool denied_more = false;
    bool truncated = false;
};

// Main-thread API. Signals run on the default main context.
class Searcher {
  public:
    Searcher();
    ~Searcher();
    Searcher(const Searcher &) = delete;
    Searcher &operator=(const Searcher &) = delete;

    core::Result<SearchId> start(Query query);
    bool cancel(SearchId id);

    sigc::signal<void(SearchId, const std::vector<Hit> &)> &signal_hits();
    sigc::signal<void(SearchId, const core::Result<Summary> &)> &signal_done();

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace filescope::search
