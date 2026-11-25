#include "ui/tray.hpp"

#include "nav/nav.hpp"
#include "ui/change_panel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

#include <gdk/gdkkeysyms.h>
#include <gdk/gdkx.h>
#include <glibmm/fileutils.h>
#include <glibmm/miscutils.h>
#include <gtkmm.h>
#include <libayatana-appindicator/app-indicator.h>

namespace filescope::ui {
namespace presentation {

std::optional<Gdk::RGBA> tick_color(int direction, std::int64_t elapsed) {
    const bool motion = Gtk::Settings::get_default()->property_gtk_enable_animations();
    if (direction == 0 || elapsed < 0 || elapsed >= (motion ? 13600000 : 12000000)) {
        return std::nullopt;
    }
    Gdk::RGBA color(direction > 0 ? "#f0525a" : "#4d9bff");
    double alpha = 0.11;
    if (motion && elapsed < 600000) {
        alpha += ((direction > 0 ? 0.42 : 0.40) - alpha) *
                 std::pow(2.0, -10.0 * static_cast<double>(elapsed) / 600000.0);
    } else if (motion && elapsed > 12000000) {
        alpha *= 1.0 - static_cast<double>(elapsed - 12000000) / 1600000.0;
    }
    color.set_alpha(alpha);
    return color;
}

} // namespace presentation
namespace {

using namespace presentation;

constexpr auto kEmptyChanges = "아직 바뀐 파일이 없습니다";
constexpr std::size_t kMenuChanges = 3;
constexpr std::size_t kSummaryChanges = 5;
constexpr std::size_t kCollapsedRoots = 3;

const char *state_text(TrayState state) {
    switch (state) {
    case TrayState::ACTIVE:
    case TrayState::RESYNCED:
        return "감시 중";
    case TrayState::PAUSED:
        return "일시 정지됨";
    case TrayState::RESYNCING:
        return "대기열 넘침 · 다시 검사 중";
    case TrayState::ERROR:
        return "감시 오류";
    }
    return "감시 오류";
}

bool warning_icon(TrayState state) {
    return state == TrayState::RESYNCING || state == TrayState::RESYNCED ||
           state == TrayState::ERROR;
}

struct ChangeLabel {
    const char *symbol;
    const char *kind;
    int direction;
};

ChangeLabel change_label(const core::Event &event) {
    switch (event.kind) {
    case core::EventKind::CREATE:
    case core::EventKind::MOVED_IN:
        return {"●", "생성", 1};
    case core::EventKind::MODIFY:
    case core::EventKind::CLOSE_WRITE:
        return event.size_delta.value_or(0) < 0 ? ChangeLabel{"▼", "수정", -1}
                                                : ChangeLabel{"▲", "수정", 1};
    case core::EventKind::DELETE:
    case core::EventKind::DELETE_SELF:
    case core::EventKind::MOVED_OUT:
        return {"✕", "삭제", -1};
    case core::EventKind::MOVE:
    case core::EventKind::RENAME:
    case core::EventKind::MOVE_SELF:
        return {"→", "이동", 0};
    default:
        return {"◦", "속성", 0};
    }
}

void style(Gtk::Widget &widget, const char *name) { widget.get_style_context()->add_class(name); }

void accessible(Gtk::Widget &widget, const Glib::ustring &name) {
    widget.get_accessible()->set_name(name);
}

Gtk::Label &pack_label(Gtk::Box &box, const Glib::ustring &text, const char *css = nullptr,
                       bool expand = false) {
    auto *label = Gtk::make_managed<Gtk::Label>(text);
    label->set_xalign(0.0F);
    if (css) {
        style(*label, css);
    }
    box.pack_start(*label, expand, expand);
    return *label;
}

} // namespace

std::vector<TrayItem> tray_items(TrayState state, const std::vector<mon::WatchInfo> &roots,
                                 const std::vector<core::EventPtr> &changes, bool can_pause) {
    const auto info = TrayAction::INFO;
    const TrayItem separator{TrayAction::SEPARATOR, {}, {}};
    std::string status =
        state == TrayState::RESYNCED ? "대기열 넘침 · 다시 검사함" : state_text(state);
    if (state == TrayState::ACTIVE || state == TrayState::PAUSED) {
        status += " · 경로 " + std::to_string(roots.size()) + "개";
    }
    std::vector<TrayItem> items{{info, std::move(status), {}}};
    for (std::size_t i = 0; i < std::min(roots.size(), kCollapsedRoots); ++i) {
        items.push_back({info, display_path(roots[i].path), {}});
    }
    if (roots.size() > kCollapsedRoots) {
        const auto extra = roots.size() - kCollapsedRoots;
        items.push_back({info, "외 " + std::to_string(extra) + "개 경로 (요약에서 보기)", {}});
    }
    items.push_back(separator);
    if (changes.empty()) {
        items.push_back({info, kEmptyChanges, {}});
    }
    for (std::size_t i = 0; i < std::min(changes.size(), kMenuChanges); ++i) {
        const auto &event = changes[i];
        const auto label = change_label(*event);
        items.push_back({TrayAction::CHANGE,
                         std::string(label.symbol) + " " +
                             display_path(Glib::path_get_basename(event->path)) + " " + label.kind +
                             "    " + format_time(event->observed_at_us, false),
                         event});
    }
    items.push_back(separator);
    items.push_back({TrayAction::SUMMARY, "요약 열기", {}});
    items.push_back({TrayAction::MAIN, "메인 창 열기", {}});
    items.push_back(separator);
    items.push_back({can_pause ? TrayAction::PAUSE : info,
                     state == TrayState::PAUSED ? "감시 재개" : "감시 일시 정지",
                     {}});
    items.push_back({TrayAction::FOLDERS, "감시 폴더 변경…", {}});
    items.push_back(separator);
    items.push_back({TrayAction::QUIT, "FileScope 종료", {}});
    return items;
}

class Tray::Impl {
  public:
    Impl(Activate activate, sigc::slot<void()> disconnected)
        : activate_(std::move(activate)), disconnected_(std::move(disconnected)),
          indicator_(app_indicator_new_with_path(
                         "filescope", "filescope-active-symbolic",
                         APP_INDICATOR_CATEGORY_APPLICATION_STATUS,
                         Glib::build_filename(
                             Glib::path_get_dirname(Glib::convert_return_gchar_ptr_to_stdstring(
                                 g_file_read_link("/proc/self/exe", nullptr))),
                             "tray-icons")
                             .c_str()),
                     g_object_unref) {
        app_indicator_set_title(indicator_.get(), "FileScope");
        app_indicator_set_menu(indicator_.get(), menu_.gobj());
        connection_signal_ =
            g_signal_connect(indicator_.get(), APP_INDICATOR_SIGNAL_CONNECTION_CHANGED,
                             G_CALLBACK(+[](AppIndicator *, gboolean connected, gpointer data) {
                                 auto &self = *static_cast<Impl *>(data);
                                 if (!connected) {
                                     self.disconnected_();
                                 }
                             }),
                             this);
    }

    ~Impl() {
        g_signal_handler_disconnect(indicator_.get(), connection_signal_);
        app_indicator_set_status(indicator_.get(), APP_INDICATOR_STATUS_PASSIVE);
    }

    bool connected() const {
        gboolean connected = FALSE;
        g_object_get(indicator_.get(), "connected", &connected, nullptr);
        return connected != FALSE;
    }

    void update(TrayState state, std::vector<TrayItem> items) {
        if (state_ != state) {
            state_ = state;
            const char *name = state == TrayState::PAUSED ? "filescope-paused-symbolic"
                               : warning_icon(state)      ? "filescope-warning-symbolic"
                                                          : "filescope-active-symbolic";
            const auto description =
                std::string("FileScope: ") +
                (state == TrayState::RESYNCED ? "대기열 넘침 · 다시 검사함" : state_text(state));
            app_indicator_set_icon_full(indicator_.get(), name, description.c_str());
        }
        auto children = menu_.get_children();
        const bool same_actions =
            items.size() == items_.size() &&
            std::equal(items.begin(), items.end(), items_.begin(),
                       [](const auto &a, const auto &b) { return a.action == b.action; });
        if (!same_actions) {
            for (auto *child : children) {
                menu_.remove(*child);
            }
            for (std::size_t i = 0; i < items.size(); ++i) {
                const auto &item = items[i];
                if (item.action == TrayAction::SEPARATOR) {
                    menu_.append(*Gtk::make_managed<Gtk::SeparatorMenuItem>());
                    continue;
                }
                auto *row = Gtk::make_managed<Gtk::MenuItem>(item.label, false);
                row->set_sensitive(item.action != TrayAction::INFO);
                row->signal_activate().connect([this, i] {
                    const auto selected = items_[i];
                    activate_(selected.action, selected.event);
                });
                menu_.append(*row);
            }
            menu_.show_all();
        } else {
            for (std::size_t i = 0; i < items.size(); ++i) {
                if (items[i].label != items_[i].label) {
                    static_cast<Gtk::MenuItem *>(children[i])->set_label(items[i].label);
                }
            }
        }
        items_ = std::move(items);
        app_indicator_set_status(indicator_.get(), APP_INDICATOR_STATUS_ACTIVE);
    }

  private:
    Activate activate_;
    sigc::slot<void()> disconnected_;
    Gtk::Menu menu_;
    std::vector<TrayItem> items_;
    std::optional<TrayState> state_;
    std::unique_ptr<AppIndicator, decltype(&g_object_unref)> indicator_;
    gulong connection_signal_ = 0;
};

Tray::Tray(Activate activate, sigc::slot<void()> disconnected)
    : impl_(std::make_unique<Impl>(std::move(activate), std::move(disconnected))) {}
Tray::~Tray() = default;
bool Tray::connected() const { return impl_->connected(); }
void Tray::update(TrayState state, std::vector<TrayItem> items) {
    impl_->update(state, std::move(items));
}

class SummaryWindow::Impl {
  public:
    Impl(SummaryWindow &window, Tray::Activate activate)
        : window_(window), activate_(std::move(activate)) {
        window_.set_title("FileScope 요약");
        window_.set_default_size(360, -1);
        window_.set_resizable(false);
        style(window_, "fs-summary");
        auto *header = Gtk::make_managed<Gtk::HeaderBar>();
        header->set_title(window_.get_title());
        auto *close = Gtk::make_managed<Gtk::Button>("×");
        accessible(*close, "요약 닫기");
        close->signal_clicked().connect([this] { window_.hide(); });
        header->pack_end(*close);
        window_.set_titlebar(*header);
        header->show_all();
        window_.signal_delete_event().connect([this](GdkEventAny *) {
            window_.hide();
            return true;
        });
        window_.signal_key_press_event().connect(
            [this](GdkEventKey *event) {
                if (event->keyval == GDK_KEY_Escape) {
                    window_.hide();
                    return true;
                }
                return false;
            },
            false);

        auto *body = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL);
        window_.add(*body);
        auto *status = section(*body, Gtk::ORIENTATION_HORIZONTAL, "fs-summary-status");
        state_ = &pack_label(*status, {}, "fs-empty-title", true);
        rate_ = &pack_label(*status, {}, "fs-data");

        auto *scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
        scroll->set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
        scroll->set_propagate_natural_height(true);
        scroll->set_max_content_height(196);
        roots_ = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL);
        accessible(*roots_, "감시 경로");
        scroll->add(*roots_);
        body->pack_start(*scroll, false, false);
        root_toggle_ = Gtk::make_managed<Gtk::Button>();
        root_toggle_->signal_clicked().connect([this] {
            expanded_ = !expanded_;
            render_roots();
        });
        body->pack_start(*root_toggle_, false, false);

        tally_ = Gtk::make_managed<Gtk::Grid>();
        style(*tally_, "fs-summary-tally");
        accessible(*tally_, "이번 세션");
        tally_->set_column_homogeneous(true);
        body->pack_start(*tally_, false, false);
        const std::array labels{"생성", "수정", "삭제", "이동"};
        for (int i = 0; i < static_cast<int>(labels.size()); ++i) {
            auto *column = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 4);
            pack_label(*column, labels[i], "fs-dim");
            figures_[i] = &pack_label(*column, {}, "fs-figure");
            tally_->attach(*column, i, 0, 1, 1);
        }
        band_ = Gtk::make_managed<Gtk::Label>();
        style(*band_, "fs-band");
        style(*band_, "quiet");
        style(*band_, "fs-summary-band");
        band_->set_line_wrap(true);
        band_->set_max_width_chars(32);
        band_->set_xalign(0.0F);
        body->pack_start(*band_, false, false);

        auto *heading = section(*body, Gtk::ORIENTATION_HORIZONTAL, "fs-summary-heading");
        pack_label(*heading, "최근 변경", "fs-session-title", true);
        hint_ = &pack_label(*heading, "눌러서 변경 내용 보기", "fs-dim");
        recent_ = Gtk::make_managed<Gtk::ListBox>();
        recent_->set_selection_mode(Gtk::SELECTION_NONE);
        body->pack_start(*recent_, false, false);
        for (auto &row : rows_) {
            build_change(row);
        }
        empty_ = section(*body, Gtk::ORIENTATION_VERTICAL, "fs-summary-empty");
        pack_label(*empty_, kEmptyChanges);
        auto &empty_copy = pack_label(
            *empty_, "감시 중인 폴더에서 파일이 만들어지거나 바뀌면 여기에 바로 나타납니다.",
            "fs-empty-copy");
        empty_copy.set_line_wrap(true);
        empty_copy.set_max_width_chars(32);

        auto *foot = section(*body, Gtk::ORIENTATION_HORIZONTAL, "fs-summary-foot");
        auto *folders = button("폴더 변경", TrayAction::FOLDERS);
        foot->pack_start(*folders, false, false);
        pause_ = button("일시 정지", TrayAction::PAUSE);
        foot->pack_start(*pause_, false, false);
        auto *main = button("메인 창", TrayAction::MAIN);
        style(*main, "fs-summary-primary");
        foot->pack_end(*main, false, false);
        body->show_all();
        close->grab_focus();
    }

    void update(TrayState state, const std::vector<mon::WatchInfo> &roots,
                const dash::Counts &counts, std::uint32_t rate, const std::string &notice,
                const std::vector<core::EventPtr> &changes, bool can_pause) {
        const bool paused = state == TrayState::PAUSED;
        const auto shown = state == TrayState::RESYNCING ? TrayState::ACTIVE : state;
        state_->set_text(std::string(paused ? "Ⅱ " : (state == TrayState::ERROR ? "⚠ " : "● ")) +
                         state_text(shown));
        update_rate(rate);
        pause_->set_label(paused ? "감시 재개" : "일시 정지");
        accessible(*pause_, paused ? "감시 재개" : "감시 일시 정지");
        pause_->set_sensitive(can_pause);
        band_->set_text(paused ? "Ⅱ 일시 정지 중에는 변경을 기록하지 않습니다. 재개하면 감시 "
                                 "경로를 다시 검사해 놓친 변경을 맞춥니다."
                               : notice);
        band_->set_visible(paused || !notice.empty());
        std::vector<std::pair<std::string, bool>> paths;
        paths.reserve(roots.size());
        for (const auto &root : roots) {
            paths.emplace_back(root.path, root.mode == mon::WatchMode::RECURSIVE);
        }
        if (paths != paths_) {
            paths_ = std::move(paths);
            render_roots();
        }
        root_toggle_->set_visible(paths_.size() > kCollapsedRoots);
        const std::array values{counts.creates, counts.modifies, counts.deletes, counts.moves};
        const std::array signs{"● ", "▲ ", "✕ ", "→ "};
        const std::array colors{"fs-up", "fs-up", "fs-down", "fs-ink-2"};
        for (std::size_t i = 0; i < values.size(); ++i) {
            figures_[i]->set_text(signs[i] + std::to_string(values[i]));
            figures_[i]->get_style_context()->remove_class(values[i] == 0 ? colors[i] : "fs-dim");
            style(*figures_[i], values[i] == 0 ? "fs-dim" : colors[i]);
        }
        tally_->set_opacity(paused ? 0.55 : 1.0);
        recent_->set_opacity(paused ? 0.55 : 1.0);
        empty_->set_opacity(paused ? 0.55 : 1.0);
        hint_->set_visible(!changes.empty());
        empty_->set_visible(changes.empty());
        for (std::size_t i = 0; i < rows_.size(); ++i) {
            auto &row = rows_[i];
            const core::EventPtr next = i < changes.size() ? changes[i] : core::EventPtr{};
            row.widget->set_visible(static_cast<bool>(next));
            if (next && row.event != next) {
                render_change(row, *next);
            }
            row.event = next;
        }
    }

    void update_rate(std::uint32_t rate) {
        rate_->set_text("최근 1분 " + std::to_string(rate) + "건");
    }

    void redraw_changes() { recent_->queue_draw(); }

  private:
    struct Row {
        Gtk::ListBoxRow *widget = nullptr;
        Gtk::Button *button = nullptr;
        Gtk::Label *kind = nullptr;
        Gtk::Label *name = nullptr;
        Gtk::Label *path = nullptr;
        Gtk::Label *delta = nullptr;
        Gtk::Label *time = nullptr;
        core::EventPtr event;
    };

    static Gtk::Box *section(Gtk::Box &body, Gtk::Orientation direction, const char *css) {
        auto *box = Gtk::make_managed<Gtk::Box>(direction, 6);
        style(*box, css);
        body.pack_start(*box, false, false);
        return box;
    }

    Gtk::Button *button(const char *label, TrayAction action) {
        auto *result = Gtk::make_managed<Gtk::Button>(label);
        accessible(*result, label);
        result->signal_clicked().connect([this, action] { activate_(action, {}); });
        return result;
    }

    void render_roots() {
        for (auto *child : roots_->get_children()) {
            roots_->remove(*child);
        }
        const auto count = expanded_ ? paths_.size() : std::min(paths_.size(), kCollapsedRoots);
        for (std::size_t i = 0; i < count; ++i) {
            const auto &[path, recursive] = paths_[i];
            auto *box = section(*roots_, Gtk::ORIENTATION_HORIZONTAL, "fs-summary-path");
            auto *icon = Gtk::make_managed<Gtk::Image>("folder-symbolic", Gtk::ICON_SIZE_MENU);
            box->pack_start(*icon, false, false);
            auto &title = pack_label(*box, display_path(path), "fs-data", true);
            title.set_ellipsize(Pango::ELLIPSIZE_MIDDLE);
            title.set_max_width_chars(1);
            title.set_tooltip_text(title.get_text());
            pack_label(*box, recursive ? "하위 폴더 포함" : "이 폴더만", "fs-dim");
        }
        roots_->show_all();
        const auto extra = paths_.size() - count;
        root_toggle_->set_label(expanded_ ? "접기"
                                          : "› 외 " + std::to_string(extra) + "개 경로 더 보기");
    }

    void build_change(Row &row) {
        row.widget = Gtk::make_managed<Gtk::ListBoxRow>();
        row.widget->set_can_focus(false);
        row.widget->set_activatable(false);
        row.button = Gtk::make_managed<Gtk::Button>();
        style(*row.button, "fs-summary-row");
        row.button->signal_clicked().connect([this, &row] {
            if (const auto event = row.event) {
                activate_(TrayAction::CHANGE, event);
            }
        });
        auto *outer = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 6);
        row.kind = &pack_label(*outer, {}, "fs-data");
        row.kind->set_size_request(58, -1);
        auto *file = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL);
        row.name = &pack_label(*file, {});
        row.name->set_ellipsize(Pango::ELLIPSIZE_END);
        row.name->set_max_width_chars(1);
        row.path = &pack_label(*file, {}, "fs-data");
        style(*row.path, "fs-dim");
        row.path->set_ellipsize(Pango::ELLIPSIZE_MIDDLE);
        row.path->set_max_width_chars(1);
        outer->pack_start(*file, true, true);
        row.delta = &pack_label(*outer, {}, "fs-data");
        row.time = &pack_label(*outer, {}, "fs-data");
        style(*row.time, "fs-dim");
        row.button->add(*outer);
        row.widget->add(*row.button);
        recent_->add(*row.widget);
        row.widget->signal_draw().connect(
            [&row](const Cairo::RefPtr<Cairo::Context> &context) {
                if (row.event && !row.button->has_focus()) {
                    const auto elapsed = g_get_monotonic_time() - row.event->monotonic_ns / 1000;
                    if (const auto color =
                            tick_color(change_label(*row.event).direction, elapsed)) {
                        Gdk::Cairo::set_source_rgba(context, *color);
                        context->rectangle(0, 0, row.widget->get_allocated_width(),
                                           row.widget->get_allocated_height());
                        context->fill();
                    }
                }
                return false;
            },
            false);
    }

    static void render_change(Row &row, const core::Event &event) {
        const auto label = change_label(event);
        row.kind->set_text(std::string(label.symbol) + " " + label.kind);
        const auto name = display_path(Glib::path_get_basename(event.path));
        const bool deleted =
            event.kind == core::EventKind::DELETE || event.kind == core::EventKind::MOVED_OUT;
        if (deleted) {
            row.name->set_markup("<s>" + Glib::Markup::escape_text(name) + "</s>");
            style(*row.name, "fs-dim");
        } else {
            row.name->set_text(name);
            row.name->get_style_context()->remove_class("fs-dim");
        }
        row.path->set_text(display_path(nav::parent(event.path).value_or(event.path)));
        const auto delta = event.size_delta.value_or(0);
        if (delta == 0) {
            row.delta->set_text({});
        } else {
            auto value = format_signed_delta(delta);
            row.delta->set_text(value.substr(value.find(' ') + 1));
        }
        for (auto *field : {row.kind, row.delta}) {
            field->get_style_context()->remove_class("fs-up");
            field->get_style_context()->remove_class("fs-down");
            if (label.direction != 0) {
                style(*field, label.direction > 0 ? "fs-up" : "fs-down");
            }
        }
        row.time->set_text(format_time(event.observed_at_us, false));
        accessible(*row.button, "메인 창에서 " + name + " 변경 내용 보기");
    }

    SummaryWindow &window_;
    Tray::Activate activate_;
    Gtk::Label *state_ = nullptr;
    Gtk::Label *rate_ = nullptr;
    Gtk::Box *roots_ = nullptr;
    Gtk::Button *root_toggle_ = nullptr;
    std::vector<std::pair<std::string, bool>> paths_;
    bool expanded_ = false;
    Gtk::Grid *tally_ = nullptr;
    std::array<Gtk::Label *, 4> figures_{};
    Gtk::Label *band_ = nullptr;
    Gtk::Label *hint_ = nullptr;
    Gtk::ListBox *recent_ = nullptr;
    std::array<Row, kSummaryChanges> rows_{};
    Gtk::Box *empty_ = nullptr;
    Gtk::Button *pause_ = nullptr;
};

SummaryWindow::SummaryWindow(Tray::Activate activate)
    : impl_(std::make_unique<Impl>(*this, std::move(activate))) {}
SummaryWindow::~SummaryWindow() { hide(); }

void SummaryWindow::open() {
    if (!get_visible()) {
        const auto display = get_display();
        if (GDK_IS_X11_DISPLAY(display->gobj())) {
            const auto monitor = display->get_primary_monitor();
            if (monitor) {
                Gdk::Rectangle workarea;
                monitor->get_workarea(workarea);
                int minimum = 0;
                int natural = 0;
                get_preferred_width(minimum, natural);
                move(workarea.get_x() +
                         std::max(0, workarea.get_width() - std::max(360, natural) - 24),
                     workarea.get_y() + 16);
            }
        }
    }
    present();
}

void SummaryWindow::update(TrayState state, const std::vector<mon::WatchInfo> &roots,
                           const dash::Counts &counts, std::uint32_t rate,
                           const std::string &notice, const std::vector<core::EventPtr> &changes,
                           bool can_pause) {
    impl_->update(state, roots, counts, rate, notice, changes, can_pause);
}

void SummaryWindow::update_rate(std::uint32_t rate) { impl_->update_rate(rate); }
void SummaryWindow::redraw_changes() { impl_->redraw_changes(); }

} // namespace filescope::ui
