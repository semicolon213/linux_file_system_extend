#include "app_info.hpp"

#include <cstring>
#include <iostream>

#include <gtkmm.h>

namespace {

bool take_smoke_option(int &argc, char **argv) {
    for (int index = 1; index < argc; ++index) {
        if (std::strcmp(argv[index], "--smoke") != 0) {
            continue;
        }
        for (int next = index; next + 1 < argc; ++next) {
            argv[next] = argv[next + 1];
        }
        --argc;
        return true;
    }
    return false;
}

} // namespace

int main(int argc, char **argv) {
    const bool smoke = take_smoke_option(argc, argv);
    const auto application = Gtk::Application::create(filescope::kAppId);
    Gtk::ApplicationWindow window;
    window.set_title(filescope::app_name());

    application->signal_activate().connect([&application, &window, smoke] {
        application->add_window(window);
        window.show_all();
        if (smoke) {
            std::cout << window.get_title() << '\n';
            Glib::signal_idle().connect([&application] {
                application->quit();
                return false;
            });
        }
    });
    return application->run(argc, argv);
}
