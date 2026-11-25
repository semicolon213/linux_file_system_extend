#pragma once

#include "core/event.hpp"
#include "his/his.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <gtkmm/box.h>

namespace filescope::ui {

namespace presentation {
std::string display_path(const std::string &path);
std::string format_bytes(std::uint64_t bytes);
std::string format_signed_delta(std::int64_t delta);
std::string event_label(core::EventKind kind);
std::string format_time(std::int64_t observed_at_us, bool milliseconds);
} // namespace presentation

class ChangePanel final : public Gtk::Box {
  public:
    struct Target {
        std::string path;
        bool directory = false;
        bool deleted = false;
        std::string event;
        std::int64_t observed_at_us = 0;
    };

    using Notify = std::function<void(const std::string &, bool)>;
    using FolderEvents = std::function<std::vector<core::EventPtr>(const std::string &)>;
    ChangePanel(Notify notify, FolderEvents folder_events);
    ~ChangePanel() override;

    void set_history(his::History *history, const std::string &reason = {});
    void set_target(Target target);
    void set_enabled(bool enabled);
    void update_events(const std::vector<core::EventPtr> &events);
    void version_added(const his::Version &version);
    bool cancel_confirmation();
    void shutdown();

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace filescope::ui
