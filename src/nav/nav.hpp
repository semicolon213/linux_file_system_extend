#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <giomm/cancellable.h>
#include <glibmm/refptr.h>
#include <sigc++/sigc++.h>

#include "core/error.hpp"
#include "core/types.hpp"

namespace filescope::nav {

using ListDirSlot = sigc::slot<void(core::Result<std::vector<core::Entry>>)>;
using CompleteSlot = sigc::slot<void(core::Result<std::vector<std::string>>)>;

void list_dir_async(const std::string &path, bool show_hidden, bool dirs_only,
                    const Glib::RefPtr<Gio::Cancellable> &cancellable, ListDirSlot done);

class History {
  public:
    static constexpr std::size_t kMaxEntries = 100;

    explicit History(std::string initial_path);
    const std::string &current() const noexcept;
    void push(const std::string &path);
    bool can_back() const noexcept;
    bool can_forward() const noexcept;
    std::optional<std::string> back();
    std::optional<std::string> forward();

  private:
    std::vector<std::string> paths_;
    std::size_t current_ = 0;
};

std::optional<std::string> parent(const std::string &path);
std::vector<std::string> breadcrumb(const std::string &path);

void complete_async(const std::string &input, bool show_hidden, std::size_t max_results,
                    const Glib::RefPtr<Gio::Cancellable> &cancellable, CompleteSlot done);

std::string home();

class Bookmarks {
  public:
    static core::Result<Bookmarks> load();
    const std::vector<std::string> &list() const noexcept;
    core::Result<void> add(const std::string &path);
    bool remove(const std::string &path);
    core::Result<void> save() const;

  private:
    std::vector<std::string> paths_;
};

} // namespace filescope::nav
