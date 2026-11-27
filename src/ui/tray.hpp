#pragma once

#include "core/event.hpp"
#include "dash/dash.hpp"
#include "mon/mon.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <gtkmm/window.h>

namespace filescope::ui {

namespace presentation {
std::optional<Gdk::RGBA> tick_color(int direction, std::int64_t elapsed);
}

enum class TrayState : std::uint8_t { ACTIVE, PAUSED, RESYNCING, RESYNCED, ERROR, IDLE };
enum class TrayAction : std::uint8_t {
    INFO,
    SEPARATOR,
    CHANGE,
    SUMMARY,
    MAIN,
    PAUSE,
    FOLDERS,
    QUIT
};

struct TrayItem {
    TrayAction action;
    std::string label;
    core::EventPtr event;
};

// Changes are the newest-first prefix of the main window's event log.
std::vector<TrayItem> tray_items(TrayState state, const std::vector<mon::WatchInfo> &roots,
                                 const std::vector<core::EventPtr> &changes, bool can_pause);

class Tray {
  public:
    using Activate = sigc::slot<void(TrayAction, const core::EventPtr &)>;
    Tray(Activate activate, sigc::slot<void()> disconnected);
    ~Tray();

    bool connected() const;
    void update(TrayState state, std::vector<TrayItem> items);

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

class SummaryWindow final : public Gtk::Window {
  public:
    SummaryWindow(Tray::Activate activate, sigc::slot<void(core::WatchId)> stop_watch);
    ~SummaryWindow() override;

    void open();
    void update(TrayState state, const std::vector<mon::WatchInfo> &roots,
                const dash::Counts &counts, std::uint32_t rate, const std::string &notice,
                const std::vector<core::EventPtr> &changes, bool can_pause);
    void update_rate(std::uint32_t rate);
    void redraw_changes();

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace filescope::ui
