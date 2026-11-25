#pragma once

#include <memory>

#include <gtkmm/application.h>
#include <gtkmm/applicationwindow.h>

#include "core/error.hpp"

namespace filescope::ui {

class MainWindow : public Gtk::ApplicationWindow {
  public:
    // [main] Creates the main window and owns its watcher, queue, and dashboard.
    static core::Result<std::unique_ptr<MainWindow>>
    create(const Glib::RefPtr<Gtk::Application> &app);
    ~MainWindow() override;

    // [main] Stops producers before GTK children are destroyed. Idempotent.
    void shutdown();

    // Public only so std::make_unique can enforce RAII; callers use create().
    explicit MainWindow(const Glib::RefPtr<Gtk::Application> &app);

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace filescope::ui
