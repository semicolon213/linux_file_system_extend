#include "app_info.hpp"
#include "ui/ui.hpp"

#include <cstring>
#include <iostream>
#include <optional>
#include <string>

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
        argv[--argc] = nullptr;
        return true;
    }
    return false;
}

std::optional<std::string> take_value_option(int &argc, char **argv, const char *option) {
    for (int index = 1; index + 1 < argc; ++index) {
        if (std::strcmp(argv[index], option) != 0) {
            continue;
        }
        std::string value = argv[index + 1];
        for (int next = index; next + 2 < argc; ++next) {
            argv[next] = argv[next + 2];
        }
        argc -= 2;
        argv[argc] = nullptr;
        return value;
    }
    return std::nullopt;
}

} // namespace

int main(int argc, char **argv) {
    const bool smoke = take_smoke_option(argc, argv);
    const auto screenshot = take_value_option(argc, argv, "--screenshot");
    const auto application = Gtk::Application::create(filescope::kAppId);
    std::unique_ptr<filescope::ui::MainWindow> window;
    bool failed = false;

    application->signal_activate().connect([&application, &window, &failed, smoke, screenshot] {
        if (smoke) {
            std::cout << filescope::app_name() << '\n';
            application->quit();
            return;
        }
        if (!window) {
            auto created = filescope::ui::MainWindow::create(application);
            if (!created.ok()) {
                std::cerr << created.error().message << '\n';
                failed = true;
                application->quit();
                return;
            }
            window = std::move(created.value());
        }
        window->show_all();
        window->present();
        if (screenshot.has_value()) {
            Glib::signal_timeout().connect_once(
                [&application, &window, &failed, screenshot] {
                    try {
                        const auto image = Gdk::Pixbuf::create(window->get_window(), 0, 0,
                                                               window->get_allocated_width(),
                                                               window->get_allocated_height());
                        if (image) {
                            image->save(screenshot.value(), "png");
                        } else {
                            std::cerr << "Could not capture the FileScope window\n";
                            failed = true;
                        }
                    } catch (const std::exception &error) {
                        std::cerr << error.what() << '\n';
                        failed = true;
                    }
                    application->quit();
                },
                2000);
        }
    });
    application->signal_shutdown().connect([&window] {
        if (window) {
            window->shutdown();
            window.reset();
        }
    });
    const int status = application->run(argc, argv);
    return failed && status == 0 ? 1 : status;
}
