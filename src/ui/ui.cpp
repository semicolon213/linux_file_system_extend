#include "ui/ui.hpp"

#include "core/event.hpp"
#include "core/path.hpp"
#include "dash/dash.hpp"
#include "fs/fs.hpp"
#include "job/job.hpp"
#include "mon/mon.hpp"
#include "nav/nav.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <deque>
#include <iomanip>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <gdk/gdkkeysyms.h>
#include <giomm/appinfo.h>
#include <giomm/file.h>
#include <glib.h>
#include <glibmm/fileutils.h>
#include <glibmm/i18n.h>
#include <glibmm/keyfile.h>
#include <glibmm/main.h>
#include <glibmm/miscutils.h>
#include <gtkmm.h>
#include <pangomm/layout.h>

namespace filescope::ui {
namespace {

constexpr std::size_t kMaxLogRows = 500;
constexpr int kWindowWidth = 1366;
constexpr int kWindowHeight = 860;

std::string window_config_path() {
    return Glib::build_filename(Glib::get_user_config_dir(), "filescope", "window.ini");
}

const char kCss[] = R"CSS(
@define-color fs_bg #10151c; @define-color fs_bg_2 #141b24;
@define-color fs_raise #18202a; @define-color fs_hover #1c2632;
@define-color fs_line #263241; @define-color fs_line_soft #1b2430;
@define-color fs_line_float #33435a; @define-color fs_vol_mid #5f6a7a;
@define-color fs_ink #e6e9ee; @define-color fs_ink_2 #b9c1cc; @define-color fs_dim #8a94a3;
@define-color fs_up #f0525a; @define-color fs_up_wash alpha(@fs_up, 0.11);
@define-color fs_down #4d9bff; @define-color fs_down_wash alpha(@fs_down, 0.11);
@define-color fs_sel #f5c84b; @define-color fs_sel_wash alpha(@fs_sel, 0.10);
@define-color fs_scroll_thumb #2f3d4f;

window, .fs-root, notebook, notebook stack { background: @fs_bg; color: @fs_ink; }
.fs-root, headerbar { font-family: Ubuntu, "Noto Sans CJK KR", sans-serif; }
headerbar { min-height: 48px; background: @fs_raise; color: @fs_ink; border-bottom: 1px solid @fs_line; }
button { min-height: 30px; border-radius: 4px; border: 1px solid @fs_line; background: transparent; color: @fs_ink_2; padding: 0 8px; }
button:hover, button:checked { background: @fs_hover; color: @fs_ink; }
button:disabled { opacity: .4; }
*:focus { outline: 2px solid @fs_sel; outline-offset: -2px; }
entry { min-height: 30px; border-radius: 4px; border: 1px solid @fs_line; background: @fs_bg; color: @fs_ink; }
notebook > header { background: @fs_bg; border-bottom: 1px solid @fs_line; }
notebook > header tab { min-height: 34px; color: @fs_dim; border-right: 1px solid @fs_line_soft; }
notebook > header tab:checked { color: @fs_ink; box-shadow: inset 0 -2px @fs_sel; }
treeview.view { background: @fs_bg; color: @fs_ink; border-bottom: 1px solid @fs_line_soft; }
treeview.view:selected { background: @fs_sel_wash; color: @fs_ink; }
treeview header button { min-height: 32px; border-radius: 0; border-width: 0 0 1px 0; color: @fs_dim; font-size: 12px; }
.fs-sidebar { background: @fs_bg_2; border-right: 1px solid @fs_line; padding: 12px 0; }
.fs-sidebar-title { color: @fs_dim; font-size: 11px; font-weight: 600; padding: 4px 12px; }
.fs-band { background: @fs_ink; color: @fs_bg; padding: 6px 12px; font-weight: 500; }
.fs-band.quiet { background: @fs_raise; color: @fs_ink; border-bottom: 1px solid @fs_line; }
.fs-empty-title { font-size: 16px; font-weight: 600; color: @fs_ink; }
.fs-empty-copy { color: @fs_ink_2; }
.fs-skeleton { background: @fs_raise; min-height: 10px; border-radius: 2px; }
@keyframes fs-loading { 50% { background: #212c39; } }
.fs-skeleton.animated { animation: fs-loading 1.4s infinite; }
.fs-bottom { background: @fs_bg_2; border-top: 1px solid @fs_line; }
.fs-bottom-toolbar { min-height: 38px; border-bottom: 1px solid @fs_line; }
.fs-chip { min-height: 24px; border-radius: 12px; color: @fs_dim; padding: 0 9px; }
.fs-chip:checked { color: @fs_ink; border-color: @fs_ink_2; }
.fs-session { min-width: 260px; border-left: 1px solid @fs_line; padding: 12px 16px; }
.fs-session-title { color: @fs_ink_2; font-size: 13px; font-weight: 600; }
.fs-figure { font-family: "Ubuntu Mono", "Noto Sans Mono", monospace; font-size: 19px; font-weight: 600; }
.fs-up { color: @fs_up; } .fs-down { color: @fs_down; } .fs-dim { color: @fs_dim; }
.fs-data { font-family: "Ubuntu Mono", "Noto Sans Mono", monospace; }
.fs-status { min-height: 28px; background: @fs_raise; border-top: 1px solid @fs_line; color: @fs_dim; padding: 0 12px; font-size: 12px; }
.fs-job { background: @fs_raise; border-top: 1px solid @fs_line; padding: 6px 12px; }
.fs-toast { background: @fs_raise; color: @fs_ink; border: 1px solid @fs_line_float; border-radius: 4px; padding: 8px 12px; }
scrollbar slider { background: @fs_scroll_thumb; min-width: 8px; min-height: 8px; }
)CSS";

struct Palette {
    static constexpr double kBgR = 16.0 / 255.0;
    static constexpr double kBgG = 21.0 / 255.0;
    static constexpr double kBgB = 28.0 / 255.0;
    static constexpr double kLineR = 38.0 / 255.0;
    static constexpr double kLineG = 50.0 / 255.0;
    static constexpr double kLineB = 65.0 / 255.0;
    static constexpr double kDimR = 138.0 / 255.0;
    static constexpr double kDimG = 148.0 / 255.0;
    static constexpr double kDimB = 163.0 / 255.0;
    static constexpr double kInkR = 230.0 / 255.0;
    static constexpr double kInkG = 233.0 / 255.0;
    static constexpr double kInkB = 238.0 / 255.0;
    static constexpr double kUpR = 240.0 / 255.0;
    static constexpr double kUpG = 82.0 / 255.0;
    static constexpr double kUpB = 90.0 / 255.0;
    static constexpr double kDownR = 77.0 / 255.0;
    static constexpr double kDownG = 155.0 / 255.0;
    static constexpr double kDownB = 1.0;
    static constexpr double kMidR = 95.0 / 255.0;
    static constexpr double kMidG = 106.0 / 255.0;
    static constexpr double kMidB = 122.0 / 255.0;
};

std::string display_path(const std::string &path) { return Glib::filename_display_name(path); }

std::string basename(const std::string &path) {
    if (path == "/") {
        return "/";
    }
    const std::size_t end = path.find_last_not_of('/');
    const std::size_t slash = path.find_last_of('/', end);
    return path.substr(slash == std::string::npos ? 0 : slash + 1, end - slash);
}

std::string format_bytes(std::uint64_t bytes) {
    static constexpr std::array<const char *, 4> kUnits{"B", "KB", "MB", "GB"};
    double value = static_cast<double>(bytes);
    std::size_t unit = 0;
    while (value >= 1000.0 && unit + 1 < kUnits.size()) {
        value /= 1000.0;
        ++unit;
    }
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(unit == 0 ? 0 : 1) << value << ' ' << kUnits[unit];
    return stream.str();
}

std::string format_signed_delta(std::int64_t delta) {
    if (delta == 0) {
        return "—";
    }
    const std::uint64_t magnitude = delta < 0 ? static_cast<std::uint64_t>(-(delta + 1)) + 1U
                                              : static_cast<std::uint64_t>(delta);
    return std::string(delta > 0 ? "▲ +" : "▼ −") + format_bytes(magnitude);
}

std::string format_delta(const core::Event &event) {
    if (event.kind == core::EventKind::CREATE || event.kind == core::EventKind::MOVED_IN) {
        return "● 신규";
    }
    if (event.kind == core::EventKind::DELETE || event.kind == core::EventKind::MOVED_OUT) {
        return "✕ 삭제";
    }
    if (event.kind == core::EventKind::RENAME || event.kind == core::EventKind::MOVE) {
        return "—";
    }
    if (event.kind == core::EventKind::ATTRIB) {
        return "—";
    }
    return format_signed_delta(event.size_delta.value_or(0));
}

std::string event_label(core::EventKind kind) {
    switch (kind) {
    case core::EventKind::CREATE:
    case core::EventKind::MOVED_IN:
        return "● CREATE";
    case core::EventKind::DELETE:
    case core::EventKind::MOVED_OUT:
    case core::EventKind::DELETE_SELF:
        return "✕ DELETE";
    case core::EventKind::MODIFY:
    case core::EventKind::CLOSE_WRITE:
        return "▲ MODIFY";
    case core::EventKind::RENAME:
    case core::EventKind::MOVE:
    case core::EventKind::MOVE_SELF:
        return "→ MOVE";
    case core::EventKind::ATTRIB:
        return "◦ ATTRIB";
    case core::EventKind::OVERFLOW:
    case core::EventKind::RESYNC_DONE:
    case core::EventKind::UNMOUNT:
        return "◦ STATUS";
    }
    return "◦ STATUS";
}

std::string format_time(std::int64_t observed_at_us, bool milliseconds) {
    const std::time_t seconds = static_cast<std::time_t>(observed_at_us / G_USEC_PER_SEC);
    std::tm local{};
    localtime_r(&seconds, &local);
    std::ostringstream stream;
    stream << std::put_time(&local, "%H:%M:%S");
    if (milliseconds) {
        stream << '.' << std::setw(3) << std::setfill('0')
               << (observed_at_us % G_USEC_PER_SEC) / 1000;
    }
    return stream.str();
}

class CandleChart final : public Gtk::DrawingArea {
  public:
    explicit CandleChart(dash::Dash &dash) : dash_(dash) {
        set_can_focus(true);
        set_hexpand(true);
        set_vexpand(true);
        set_size_request(-1, 170);
        set_tooltip_text("감시 폴더 총용량 1분봉 차트");
        get_accessible()->set_name("감시 폴더 총용량 1분봉 차트");
        add_events(Gdk::KEY_PRESS_MASK);
    }

  protected:
    bool on_key_press_event(GdkEventKey *event) override {
        const auto candles = dash_.candles(30);
        if (event->keyval == GDK_KEY_Escape) {
            clear_selection();
        } else if (event->keyval == GDK_KEY_Left || event->keyval == GDK_KEY_Right) {
            if (candles.empty()) {
                return true;
            }
            if (!selected_.has_value()) {
                selected_ = candles.size() - 1;
            } else if (event->keyval == GDK_KEY_Left && selected_.value() > 0) {
                --selected_.value();
            } else if (event->keyval == GDK_KEY_Right && selected_.value() + 1 < candles.size()) {
                ++selected_.value();
            }
            *selected_ = std::min(*selected_, candles.size() - 1);
            const auto &candle = candles[selected_.value()];
            std::ostringstream description;
            description << format_time(candle.minute_start_us, false) << ", 시 " << candle.open
                        << ", 고 " << candle.high << ", 저 " << candle.low << ", 종 "
                        << candle.close << ", 생성 " << candle.creates << ", 수정 "
                        << candle.modifies << ", 삭제 " << candle.deletes;
            get_accessible()->set_description(description.str());
            set_tooltip_text(description.str());
        } else {
            return Gtk::DrawingArea::on_key_press_event(event);
        }
        queue_draw();
        return true;
    }

    bool on_focus_out_event(GdkEventFocus *event) override {
        clear_selection();
        queue_draw();
        return Gtk::DrawingArea::on_focus_out_event(event);
    }

    void clear_selection() {
        selected_.reset();
        set_tooltip_text("감시 폴더 총용량 1분봉 차트");
        get_accessible()->set_description("");
    }

    bool on_draw(const Cairo::RefPtr<Cairo::Context> &context) override {
        const Gtk::Allocation allocation = get_allocation();
        const double width = static_cast<double>(allocation.get_width());
        const double height = static_cast<double>(allocation.get_height());
        constexpr double kLeft = 12.0;
        constexpr double kRight = 92.0;
        constexpr double kTop = 12.0;
        constexpr double kBottom = 22.0;
        const double plot_width = std::max(1.0, width - kLeft - kRight);
        const double usable_height = std::max(1.0, height - kTop - kBottom);
        const double price_height = usable_height * 0.7;
        const double volume_top = kTop + price_height + 8.0;
        const double volume_height = std::max(1.0, height - kBottom - volume_top);

        context->set_source_rgb(Palette::kBgR, Palette::kBgG, Palette::kBgB);
        context->paint();
        context->set_line_width(1.0);
        context->set_source_rgb(Palette::kLineR, Palette::kLineG, Palette::kLineB);
        for (int line = 0; line < 4; ++line) {
            const double y = kTop + price_height * static_cast<double>(line) / 3.0;
            context->move_to(kLeft, y);
            context->line_to(kLeft + plot_width, y);
        }
        context->move_to(kLeft, volume_top + volume_height);
        context->line_to(kLeft + plot_width, volume_top + volume_height);
        context->stroke();

        const auto candles = dash_.candles(30);
        if (candles.empty()) {
            context->set_source_rgb(Palette::kDimR, Palette::kDimG, Palette::kDimB);
            context->move_to(kLeft + 8.0, kTop + 12.0);
            create_pango_layout("이벤트가 오면 1분봉이 표시됩니다")->show_in_cairo_context(context);
            return true;
        }

        std::uint64_t minimum = UINT64_MAX;
        std::uint64_t maximum = 0;
        std::uint32_t max_volume = 1;
        for (const auto &candle : candles) {
            if (candle.level_known && candle.has_events) {
                minimum = std::min(minimum, candle.low);
                maximum = std::max(maximum, candle.high);
            }
            max_volume = std::max(max_volume, candle.creates + candle.modifies + candle.deletes);
        }
        if (minimum == UINT64_MAX) {
            minimum = 0;
        }
        const double slot = plot_width / 30.0;
        const double body_width = std::clamp(slot * 0.56, 3.0, 12.0);
        const std::size_t first = 30U - candles.size();
        const auto price_y = [&](std::uint64_t value) {
            const double offset =
                static_cast<double>(std::clamp(value, minimum, maximum) - minimum);
            const double range = static_cast<double>(maximum - minimum);
            return kTop + price_height * (range > 0.0 ? 1.0 - offset / range : 0.5);
        };

        for (std::size_t index = 0; index < candles.size(); ++index) {
            const auto &candle = candles[index];
            const double x = kLeft + (static_cast<double>(first + index) + 0.5) * slot;
            if (candle.level_known && candle.has_events) {
                if (candle.close > candle.open) {
                    context->set_source_rgb(Palette::kUpR, Palette::kUpG, Palette::kUpB);
                } else if (candle.close < candle.open) {
                    context->set_source_rgb(Palette::kDownR, Palette::kDownG, Palette::kDownB);
                } else {
                    context->set_source_rgb(Palette::kMidR, Palette::kMidG, Palette::kMidB);
                }
                context->set_line_width(1.5);
                context->move_to(x, price_y(candle.high));
                context->line_to(x, price_y(candle.low));
                context->stroke();
                const double open_y = price_y(candle.open);
                const double close_y = price_y(candle.close);
                context->rectangle(x - body_width / 2.0, std::min(open_y, close_y), body_width,
                                   std::max(1.5, std::abs(open_y - close_y)));
                context->fill();
            }

            const std::array<std::pair<std::uint32_t, std::array<double, 3>>, 3> volumes{{
                {candle.creates, {Palette::kUpR, Palette::kUpG, Palette::kUpB}},
                {candle.modifies, {Palette::kMidR, Palette::kMidG, Palette::kMidB}},
                {candle.deletes, {Palette::kDownR, Palette::kDownG, Palette::kDownB}},
            }};
            double bottom = volume_top + volume_height;
            for (const auto &volume : volumes) {
                if (volume.first == 0) {
                    continue;
                }
                const double segment =
                    std::max(1.0, volume_height * static_cast<double>(volume.first) /
                                          static_cast<double>(max_volume) -
                                      2.0);
                bottom -= segment;
                context->set_source_rgba(volume.second[0], volume.second[1], volume.second[2],
                                         0.85);
                context->rectangle(x - body_width / 2.0, bottom, body_width, segment);
                context->fill();
                bottom -= 2.0;
            }

            if (selected_.has_value() && selected_.value() == index) {
                context->set_source_rgb(Palette::kInkR, Palette::kInkG, Palette::kInkB);
                const std::vector<double> dash{3.0, 3.0};
                context->set_dash(dash, 0.0);
                context->set_line_width(1.0);
                context->move_to(x, kTop);
                context->line_to(x, volume_top + volume_height);
                context->stroke();
                context->unset_dash();
            }
        }

        context->set_source_rgb(Palette::kDimR, Palette::kDimG, Palette::kDimB);
        context->select_font_face("monospace", Cairo::FONT_SLANT_NORMAL, Cairo::FONT_WEIGHT_NORMAL);
        context->set_font_size(11.0);
        const auto axis_label = [](std::uint64_t bytes) {
            constexpr std::uint64_t kKB = 1024;
            constexpr std::uint64_t kMB = kKB * kKB;
            std::ostringstream label;
            label << std::fixed;
            if (bytes >= kMB) {
                label << std::setprecision(3) << static_cast<double>(bytes) / kMB << " MB";
            } else if (bytes >= kKB) {
                label << std::setprecision(1) << static_cast<double>(bytes) / kKB << " KB";
            } else {
                label << bytes << " B";
            }
            return label.str();
        };
        const auto &last = candles.back();
        const double current_y = price_y(last.close);
        constexpr double kLabelHeight = 18.0;
        for (int line = 0; line < 4; ++line) {
            const double y = kTop + price_height * static_cast<double>(line) / 3.0;
            if ((last.level_known && std::abs(y - current_y) < kLabelHeight) ||
                (maximum == minimum && line != 0)) {
                continue;
            }
            const auto step = static_cast<unsigned>(line);
            const auto range = maximum - minimum;
            const auto value = maximum - (range / 3U * step + range % 3U * step / 3U);
            context->move_to(kLeft + plot_width + 8.0, y + 4.0);
            context->show_text(axis_label(value));
        }
        if (last.level_known) {
            context->set_source_rgb(Palette::kInkR, Palette::kInkG, Palette::kInkB);
            context->move_to(kLeft, current_y);
            context->line_to(kLeft + plot_width, current_y);
            context->stroke();
            context->rectangle(kLeft + plot_width + 2.0, current_y - kLabelHeight / 2.0,
                               kRight - 4.0, kLabelHeight);
            context->fill();
            context->set_source_rgb(Palette::kBgR, Palette::kBgG, Palette::kBgB);
            context->move_to(kLeft + plot_width + 8.0, current_y + 4.0);
            context->show_text(axis_label(last.close));
        }
        return true;
    }

  private:
    dash::Dash &dash_;
    std::optional<std::size_t> selected_;
};

class FileColumns final : public Gtk::TreeModel::ColumnRecord {
  public:
    FileColumns() {
        add(name);
        add(path);
        add(size);
        add(delta);
        add(event);
        add(modified);
        add(mode);
        add(is_dir);
        add(deleted);
        add(tick_direction);
        add(tick_start);
    }
    Gtk::TreeModelColumn<Glib::ustring> name;
    Gtk::TreeModelColumn<std::string> path;
    Gtk::TreeModelColumn<Glib::ustring> size;
    Gtk::TreeModelColumn<Glib::ustring> delta;
    Gtk::TreeModelColumn<Glib::ustring> event;
    Gtk::TreeModelColumn<Glib::ustring> modified;
    Gtk::TreeModelColumn<Glib::ustring> mode;
    Gtk::TreeModelColumn<bool> is_dir;
    Gtk::TreeModelColumn<bool> deleted;
    Gtk::TreeModelColumn<int> tick_direction;
    Gtk::TreeModelColumn<std::int64_t> tick_start;
};

class LogColumns final : public Gtk::TreeModel::ColumnRecord {
  public:
    LogColumns() {
        add(time);
        add(event);
        add(path);
        add(value);
        add(tick_start);
        add(raw_path);
    }
    Gtk::TreeModelColumn<Glib::ustring> time;
    Gtk::TreeModelColumn<Glib::ustring> event;
    Gtk::TreeModelColumn<Glib::ustring> path;
    Gtk::TreeModelColumn<Glib::ustring> value;
    Gtk::TreeModelColumn<std::int64_t> tick_start;
    Gtk::TreeModelColumn<std::string> raw_path;
};

enum class LogFilter : std::uint8_t { ALL, CREATE_DELETE, MODIFY, MOVE, ATTRIB };

struct LogRow {
    core::EventPtr event;
    std::string time;
    std::string label;
    std::string value;
};

bool matches_filter(core::EventKind kind, LogFilter filter) {
    if (filter == LogFilter::ALL) {
        return true;
    }
    if (filter == LogFilter::CREATE_DELETE) {
        return kind == core::EventKind::CREATE || kind == core::EventKind::DELETE ||
               kind == core::EventKind::MOVED_IN || kind == core::EventKind::MOVED_OUT;
    }
    if (filter == LogFilter::MODIFY) {
        return kind == core::EventKind::MODIFY || kind == core::EventKind::CLOSE_WRITE;
    }
    if (filter == LogFilter::MOVE) {
        return kind == core::EventKind::MOVE || kind == core::EventKind::RENAME ||
               kind == core::EventKind::MOVE_SELF;
    }
    return kind == core::EventKind::ATTRIB;
}

} // namespace

class MainWindow::Impl : public sigc::trackable {
  public:
    struct Tab {
        explicit Tab(const std::string &initial) : history(initial) {}
        nav::History history;
        std::optional<nav::History> pending_history;
        Glib::RefPtr<Gio::Cancellable> cancellable;
        Glib::RefPtr<Gtk::ListStore> model;
        std::unordered_map<std::string, Gtk::TreeModel::iterator> rows;
        std::optional<std::string> pending_selection;
        Gtk::TreeView *tree = nullptr;
        std::unique_ptr<Gtk::Menu> menu;
        Gtk::Stack *state = nullptr;
        Gtk::Label *error_label = nullptr;
        std::uint64_t generation = 0;
        bool show_hidden = false;
    };

    explicit Impl(MainWindow &window) : window_(window), dash_(60) {
        install_style();
        build_window();
        connect_models();
        load_bookmarks();
        create_tab(nav::home());
        auto watcher_result = mon::Watcher::create();
        if (watcher_result.ok()) {
            watcher_ = std::move(watcher_result).value();
            watcher_events_ =
                watcher_->signal_events().connect(sigc::mem_fun(*this, &Impl::on_events));
            watcher_state_ =
                watcher_->signal_state().connect(sigc::mem_fun(*this, &Impl::on_watch_state));
            const auto root = watcher_->add_root(nav::home(), mon::WatchMode::RECURSIVE);
            if (!root.ok()) {
                show_band("⚠ 감시를 시작할 수 없습니다", false);
            }
        } else {
            show_band("⚠ 감시 기능을 사용할 수 없습니다", false);
        }
        update_metrics();
        metrics_timer_ = Glib::signal_timeout().connect_seconds(
            [this] {
                update_metrics();
                if (job_pulse_) {
                    job_progress_->pulse();
                }
                chart_->queue_draw();
                for (auto *chart : detached_charts_) {
                    chart->queue_draw();
                }
                return true;
            },
            1);
    }

    ~Impl() { shutdown(); }

    void shutdown() {
        if (shutdown_) {
            return;
        }
        shutdown_ = true;
        root_->set_sensitive(false);
        tick_timer_.disconnect();
        metrics_timer_.disconnect();
        toast_timer_.disconnect();
        if (completion_cancel_) {
            completion_cancel_->cancel();
        }
        for (auto &tab : tabs_) {
            if (tab->cancellable) {
                tab->cancellable->cancel();
            }
        }
        tree_cancel_->cancel();
        watcher_events_.disconnect();
        watcher_state_.disconnect();
        watcher_.reset();
        queue_.shutdown();
        save_window_state();
        for (auto &chart_window : chart_windows_) {
            chart_window->hide();
        }
        chart_windows_.clear();
    }

  private:
    void install_style() {
        const auto provider = Gtk::CssProvider::create();
        provider->load_from_data(kCss);
        Gtk::StyleContext::add_provider_for_screen(Gdk::Screen::get_default(), provider,
                                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }

    static void add_class(Gtk::Widget &widget, const std::string &name) {
        widget.get_style_context()->add_class(name);
    }

    static void set_accessible(Gtk::Widget &widget, const Glib::ustring &name) {
        widget.get_accessible()->set_name(name);
    }

    void build_window() {
        window_.set_title("FileScope");
        int width = kWindowWidth;
        int height = kWindowHeight;
        try {
            Glib::KeyFile config;
            config.load_from_file(window_config_path());
            width = config.get_integer("Window", "width");
            height = config.get_integer("Window", "height");
            if (config.get_boolean("Window", "maximized")) {
                window_.maximize();
            }
        } catch (const Glib::Error &error) {
            g_debug("FileScope window configuration unavailable: %s", error.what().c_str());
        }
        window_.set_default_size(std::clamp(width, 1024, 8192), std::clamp(height, 640, 8192));
        window_.set_size_request(1024, 640);
        window_.add_events(Gdk::KEY_PRESS_MASK);
        window_.signal_key_press_event().connect(sigc::mem_fun(*this, &Impl::on_key), false);
        window_.signal_delete_event().connect([this](GdkEventAny *) {
            shutdown();
            return false;
        });

        root_ = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL);
        add_class(*root_, "fs-root");
        auto *overlay = Gtk::make_managed<Gtk::Overlay>();
        overlay->add(*root_);
        window_.add(*overlay);
        toast_ = Gtk::make_managed<Gtk::Revealer>();
        toast_->set_halign(Gtk::ALIGN_END);
        toast_->set_valign(Gtk::ALIGN_END);
        toast_->set_margin_end(12);
        toast_->set_margin_bottom(40);
        toast_label_ = Gtk::make_managed<Gtk::Label>();
        add_class(*toast_label_, "fs-toast");
        toast_->add(*toast_label_);
        overlay->add_overlay(*toast_);

        header_ = Gtk::make_managed<Gtk::HeaderBar>();
        header_->set_show_close_button(true);
        header_->set_title("FileScope");
        window_.set_titlebar(*header_);
        back_ = make_header_button("‹", "뒤로");
        forward_ = make_header_button("›", "앞으로");
        up_ = make_header_button("↑", "상위 폴더");
        header_->pack_start(*back_);
        header_->pack_start(*forward_);
        header_->pack_start(*up_);
        back_->signal_clicked().connect(sigc::mem_fun(*this, &Impl::go_back));
        forward_->signal_clicked().connect(sigc::mem_fun(*this, &Impl::go_forward));
        up_->signal_clicked().connect(sigc::mem_fun(*this, &Impl::go_up));

        breadcrumb_ = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 4);
        auto *breadcrumb_view = Gtk::make_managed<Gtk::ScrolledWindow>();
        breadcrumb_view->set_policy(Gtk::POLICY_EXTERNAL, Gtk::POLICY_NEVER);
        breadcrumb_view->set_propagate_natural_width(true);
        breadcrumb_view->add(*breadcrumb_);
        set_accessible(*breadcrumb_view, "현재 경로");
        header_->set_custom_title(*breadcrumb_view);
        breadcrumb_scroll_ = breadcrumb_view->get_hadjustment();
        breadcrumb_scroll_->signal_changed().connect(
            sigc::mem_fun(*this, &Impl::show_breadcrumb_end));
        search_ = Gtk::make_managed<Gtk::SearchEntry>();
        // design-pending: U-6
        completion_ = Gtk::EntryCompletion::create();
        completion_model_ = Gtk::ListStore::create(completion_columns_);
        completion_->set_model(completion_model_);
        completion_->set_text_column(completion_columns_.path);
        search_->set_completion(completion_);
        search_->signal_changed().connect(sigc::mem_fun(*this, &Impl::complete_address));
        search_->set_placeholder_text("폴더로 이동");
        search_->set_size_request(220, 30);
        set_accessible(*search_, "주소 입력 및 폴더 자동완성");
        search_->signal_activate().connect(sigc::mem_fun(*this, &Impl::activate_search));
        header_->pack_end(*search_);

        metrics_ = Gtk::make_managed<Gtk::Label>();
        add_class(*metrics_, "fs-data");
        header_->pack_end(*metrics_);

        content_ = Gtk::make_managed<Gtk::Paned>(Gtk::ORIENTATION_HORIZONTAL);
        root_->pack_start(*content_, true, true);
        build_sidebar();
        build_main_area();
        build_bottom();
        build_status();
    }

    void save_window_state() {
        if (!window_.get_realized()) {
            return;
        }
        int width = 0;
        int height = 0;
        window_.get_size(width, height);
        const auto path = window_config_path();
        if (g_mkdir_with_parents(Glib::path_get_dirname(path).c_str(), 0700) != 0) {
            g_warning("FileScope could not create the configuration directory");
            return;
        }
        try {
            Glib::KeyFile config;
            config.set_integer("Window", "width", width);
            config.set_integer("Window", "height", height);
            config.set_boolean("Window", "maximized", window_.is_maximized());
            Glib::file_set_contents(path, config.to_data());
        } catch (const Glib::Error &error) {
            g_warning("FileScope could not save window state: %s", error.what().c_str());
        }
    }

    // Both arguments are UI text: the visible symbol and its spoken name.
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    Gtk::Button *make_header_button(const Glib::ustring &label, const Glib::ustring &name) {
        auto *button = Gtk::make_managed<Gtk::Button>(label);
        button->set_size_request(30, 30);
        set_accessible(*button, name);
        return button;
    }

    void build_sidebar() {
        sidebar_ = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 4);
        sidebar_->set_size_request(240, -1);
        add_class(*sidebar_, "fs-sidebar");
        auto *favorite_title = Gtk::make_managed<Gtk::Label>("즐겨찾기");
        favorite_title->set_xalign(0.0F);
        add_class(*favorite_title, "fs-sidebar-title");
        sidebar_->pack_start(*favorite_title, false, false);
        bookmarks_box_ = Gtk::make_managed<Gtk::ListBox>();
        bookmarks_box_->set_selection_mode(Gtk::SELECTION_NONE);
        bookmarks_box_->signal_row_activated().connect(
            sigc::mem_fun(*this, &Impl::activate_bookmark));
        sidebar_->pack_start(*bookmarks_box_, false, false);
        auto *system_title = Gtk::make_managed<Gtk::Label>("폴더");
        system_title->set_xalign(0.0F);
        add_class(*system_title, "fs-sidebar-title");
        sidebar_->pack_start(*system_title, false, false);
        tree_model_ = Gtk::TreeStore::create(tree_columns_);
        tree_cancel_ = Gio::Cancellable::create();
        auto *tree = Gtk::make_managed<Gtk::TreeView>(tree_model_);
        tree->set_headers_visible(false);
        tree->set_enable_search(false);
        tree->append_column("폴더", tree_columns_.name);
        set_accessible(*tree, "폴더 트리");
        const auto home = tree_model_->append();
        append_folder(home, nav::home());
        (*home)[tree_columns_.name] = "⌂  홈";
        tree->signal_row_expanded().connect(sigc::mem_fun(*this, &Impl::expand_folder));
        tree->signal_row_activated().connect(
            [this](const Gtk::TreeModel::Path &path, Gtk::TreeViewColumn *) {
                const auto row = tree_model_->get_iter(path);
                if (row) {
                    const std::string folder = (*row)[tree_columns_.path];
                    if (!folder.empty()) {
                        navigate(folder, true);
                    }
                }
            });
        auto *scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
        scroll->add(*tree);
        sidebar_->pack_start(*scroll, true, true);
        content_->add1(*sidebar_);
    }

    void append_folder(const Gtk::TreeModel::iterator &iter, const std::string &path) {
        (*iter)[tree_columns_.path] = path;
        (*iter)[tree_columns_.name] = display_path(basename(path));
        (*tree_model_->append(iter->children()))[tree_columns_.name] = "불러오는 중…";
    }

    void expand_folder(const Gtk::TreeModel::iterator &iter, const Gtk::TreeModel::Path &path) {
        if ((*iter)[tree_columns_.loaded]) {
            return;
        }
        const std::string folder = (*iter)[tree_columns_.path];
        if (folder.empty()) {
            return;
        }
        (*iter)[tree_columns_.loaded] = true;
        nav::list_dir_async(folder, false, true, tree_cancel_,
                            sigc::bind(sigc::mem_fun(*this, &Impl::finish_folder),
                                       Gtk::TreeRowReference(tree_model_, path)));
    }

    void finish_folder(core::Result<std::vector<core::Entry>> result,
                       const Gtk::TreeRowReference &reference) {
        if (shutdown_ || !reference.is_valid()) {
            return;
        }
        const auto iter = tree_model_->get_iter(reference.get_path());
        if (!result.ok()) {
            (*iter->children().begin())[tree_columns_.name] = "폴더를 열 수 없습니다";
            (*iter)[tree_columns_.loaded] = false;
            return;
        }
        const std::string parent = (*iter)[tree_columns_.path];
        for (const auto &entry : result.value()) {
            append_folder(tree_model_->append(iter->children()),
                          core::path_join(parent, entry.name));
        }
        tree_model_->erase(iter->children().begin());
    }

    void build_main_area() {
        auto *right = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL);
        band_ = Gtk::make_managed<Gtk::Revealer>();
        band_box_ = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 8);
        add_class(*band_box_, "fs-band");
        band_label_ = Gtk::make_managed<Gtk::Label>();
        band_label_->set_xalign(0.0F);
        band_box_->pack_start(*band_label_, true, true);
        watch_start_ = Gtk::make_managed<Gtk::Button>("감시 시작");
        watch_start_->set_no_show_all(true);
        watch_start_->signal_clicked().connect([this] {
            if (watcher_ && current_tab()) {
                const auto result =
                    watcher_->add_root(current_tab()->history.current(), mon::WatchMode::RECURSIVE);
                if (!result.ok()) {
                    show_band("⚠ 감시를 시작할 수 없습니다", false);
                }
            }
        });
        band_box_->pack_start(*watch_start_, false, false);
        stale_parent_ = Gtk::make_managed<Gtk::Button>("상위 폴더로 이동");
        stale_parent_->set_no_show_all(true);
        stale_parent_->signal_clicked().connect(sigc::mem_fun(*this, &Impl::go_up));
        band_box_->pack_start(*stale_parent_, false, false);
        band_->add(*band_box_);
        right->pack_start(*band_, false, false);
        job_result_ = Gtk::make_managed<Gtk::Revealer>();
        auto *job_result_row = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 8);
        add_class(*job_result_row, "fs-band");
        add_class(*job_result_row, "quiet");
        job_result_label_ = Gtk::make_managed<Gtk::Label>();
        job_result_label_->set_xalign(0.0F);
        auto *dismiss_job_result = Gtk::make_managed<Gtk::Button>("확인");
        set_accessible(*dismiss_job_result, "작업 결과 닫기");
        dismiss_job_result->signal_clicked().connect(
            [this] { job_result_->set_reveal_child(false); });
        job_result_row->pack_start(*job_result_label_, true, true);
        job_result_row->pack_end(*dismiss_job_result, false, false);
        job_result_->add(*job_result_row);
        right->pack_start(*job_result_, false, false);
        notebook_ = Gtk::make_managed<Gtk::Notebook>();
        notebook_->set_scrollable(true);
        notebook_->signal_switch_page().connect(sigc::mem_fun(*this, &Impl::switch_tab));
        right->pack_start(*notebook_, true, true);
        content_->add2(*right);
    }

    void build_bottom() {
        bottom_ = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL);
        bottom_->set_vexpand(false);
        bottom_->set_size_request(-1, 232);
        add_class(*bottom_, "fs-bottom");
        auto *left = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL);
        bottom_->pack_start(*left, true, true);
        auto *toolbar = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 6);
        add_class(*toolbar, "fs-bottom-toolbar");
        bottom_switcher_ = Gtk::make_managed<Gtk::StackSwitcher>();
        toolbar->pack_start(*bottom_switcher_, false, false, 8);
        const std::array<std::pair<const char *, LogFilter>, 5> filters{{
            {"전체", LogFilter::ALL},
            {"생성·삭제", LogFilter::CREATE_DELETE},
            {"수정", LogFilter::MODIFY},
            {"이동", LogFilter::MOVE},
            {"속성", LogFilter::ATTRIB},
        }};
        for (const auto &filter : filters) {
            auto *button = Gtk::make_managed<Gtk::ToggleButton>(filter.first);
            add_class(*button, "fs-chip");
            button->set_active(filter.second == LogFilter::ALL);
            button->signal_toggled().connect(
                sigc::bind(sigc::mem_fun(*this, &Impl::set_log_filter), button, filter.second));
            filter_buttons_.push_back(button);
            toolbar->pack_start(*button, false, false);
        }
        log_search_ = Gtk::make_managed<Gtk::SearchEntry>();
        log_search_->set_placeholder_text("경로 거르기");
        set_accessible(*log_search_, "경로 거르기");
        log_search_->set_size_request(200, 26);
        log_search_->signal_search_changed().connect(sigc::mem_fun(*this, &Impl::rebuild_log));
        toolbar->pack_end(*log_search_, false, false, 8);
        left->pack_start(*toolbar, false, false);
        overflow_ = Gtk::make_managed<Gtk::Revealer>();
        auto *overflow_row = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 8);
        add_class(*overflow_row, "fs-band");
        add_class(*overflow_row, "quiet");
        overflow_label_ = Gtk::make_managed<Gtk::Label>();
        auto *dismiss = Gtk::make_managed<Gtk::Button>("확인");
        dismiss->signal_clicked().connect([this] { overflow_->set_reveal_child(false); });
        overflow_row->pack_start(*overflow_label_, true, true);
        overflow_row->pack_end(*dismiss, false, false);
        overflow_->add(*overflow_row);
        left->pack_start(*overflow_, false, false);

        bottom_stack_ = Gtk::make_managed<Gtk::Stack>();
        bottom_stack_->set_transition_type(Gtk::STACK_TRANSITION_TYPE_NONE);
        bottom_switcher_->set_stack(*bottom_stack_);
        log_model_ = Gtk::ListStore::create(log_columns_);
        log_tree_ = Gtk::make_managed<Gtk::TreeView>(log_model_);
        set_accessible(*log_tree_, "이벤트 로그 목록");
        log_tree_->set_headers_visible(false);
        log_tree_->append_column("시각", log_columns_.time);
        log_tree_->append_column("이벤트", log_columns_.event);
        log_tree_->append_column("경로", log_columns_.path);
        log_tree_->append_column("값", log_columns_.value);
        log_tree_->get_column(0)->set_fixed_width(118);
        log_tree_->get_column(1)->set_fixed_width(110);
        log_tree_->get_column(3)->set_fixed_width(150);
        for (auto *column : log_tree_->get_columns()) {
            auto *renderer = column->get_first_cell();
            column->set_cell_data_func(*renderer, sigc::mem_fun(*this, &Impl::paint_log_tick));
        }
        log_tree_->signal_row_activated().connect(sigc::mem_fun(*this, &Impl::activate_log));
        log_tree_->get_selection()->signal_changed().connect([this] {
            const auto row = log_tree_->get_selection()->get_selected();
            if (row) {
                activate_log(Gtk::TreeModel::Path(row), nullptr);
            }
        });
        auto *log_scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
        log_scroll->add(*log_tree_);
        bottom_stack_->add(*log_scroll, "log", "이벤트 로그");

        auto *chart_page = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL);
        auto *legend = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 12);
        legend->set_margin_start(12);
        legend->set_margin_end(12);
        auto *up = Gtk::make_managed<Gtk::Label>("■ 상승(용량 증가)");
        add_class(*up, "fs-up");
        auto *down = Gtk::make_managed<Gtk::Label>("■ 하락(용량 감소)");
        add_class(*down, "fs-down");
        auto *volume = Gtk::make_managed<Gtk::Label>("거래량: ● 생성 · ▲▼ 수정 · ✕ 삭제");
        add_class(*volume, "fs-dim");
        auto *detach = Gtk::make_managed<Gtk::Button>("새 창으로");
        detach->signal_clicked().connect(sigc::mem_fun(*this, &Impl::open_chart_window));
        legend->pack_start(*up, false, false);
        legend->pack_start(*down, false, false);
        legend->pack_start(*volume, false, false);
        legend->pack_end(*detach, false, false);
        chart_page->pack_start(*legend, false, false);
        chart_ = Gtk::make_managed<CandleChart>(dash_);
        chart_page->pack_start(*chart_, true, true);
        bottom_stack_->add(*chart_page, "chart", "변경량 · 1분봉");
        const char *preview = g_getenv("FILESCOPE_UI_PREVIEW");
        if (preview != nullptr && std::string(preview) == "chart") {
            window_.signal_map().connect([this] { bottom_stack_->set_visible_child("chart"); });
        }
        left->pack_start(*bottom_stack_, true, true);
        build_session_counts();
        root_->pack_start(*bottom_, false, false);
    }

    void build_session_counts() {
        auto *session = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 8);
        add_class(*session, "fs-session");
        auto *title = Gtk::make_managed<Gtk::Label>("이번 세션");
        title->set_xalign(0.0F);
        add_class(*title, "fs-session-title");
        session->pack_start(*title, false, false);
        auto *grid = Gtk::make_managed<Gtk::Grid>();
        grid->set_row_spacing(8);
        grid->set_column_spacing(12);
        const std::array<const char *, 5> labels{{
            "● 생성",
            "▲ 수정",
            "✕ 삭제",
            "→ 이동",
            "◦ 속성",
        }};
        for (std::size_t index = 0; index < labels.size(); ++index) {
            auto *label = Gtk::make_managed<Gtk::Label>(labels[index]);
            label->set_xalign(0.0F);
            auto *value = Gtk::make_managed<Gtk::Label>("0");
            value->set_xalign(0.0F);
            add_class(*value, "fs-figure");
            add_class(*value, "fs-dim");
            grid->attach(*label, static_cast<int>(index % 2U) * 2, static_cast<int>(index / 2U), 1,
                         1);
            grid->attach(*value, static_cast<int>(index % 2U) * 2 + 1, static_cast<int>(index / 2U),
                         1, 1);
            count_labels_[index] = value;
        }
        session->pack_start(*grid, true, true);
        bottom_->pack_start(*session, false, false);
    }

    void build_status() {
        // design-pending: U-6
        job_box_ = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 8);
        add_class(*job_box_, "fs-job");
        job_label_ = Gtk::make_managed<Gtk::Label>("작업 준비");
        job_progress_ = Gtk::make_managed<Gtk::ProgressBar>();
        job_progress_->set_hexpand(true);
        job_cancel_ = Gtk::make_managed<Gtk::Button>("취소");
        job_cancel_->signal_clicked().connect(sigc::mem_fun(*this, &Impl::cancel_job));
        job_box_->pack_start(*job_label_, false, false);
        job_box_->pack_start(*job_progress_, true, true);
        job_box_->pack_start(*job_cancel_, false, false);
        job_box_->show_all();
        job_box_->hide();
        job_box_->set_no_show_all(true);
        root_->pack_start(*job_box_, false, false);

        status_ = Gtk::make_managed<Gtk::Label>("0개 항목");
        status_->set_xalign(0.0F);
        add_class(*status_, "fs-status");
        root_->pack_start(*status_, false, false);
    }

    void connect_models() {
        queue_state_ = queue_.signal_state().connect(sigc::mem_fun(*this, &Impl::on_job_state));
        queue_progress_ =
            queue_.signal_progress().connect(sigc::mem_fun(*this, &Impl::on_job_progress));
    }

    void load_bookmarks() {
        auto loaded = nav::Bookmarks::load();
        if (loaded.ok()) {
            bookmarks_ = std::move(loaded.value());
        }
        rebuild_bookmarks();
    }

    void rebuild_bookmarks() {
        const auto children = bookmarks_box_->get_children();
        for (Gtk::Widget *child : children) {
            gtk_widget_destroy(child->gobj());
        }
        for (const auto &path : bookmarks_.list()) {
            auto *row = Gtk::make_managed<Gtk::ListBoxRow>();
            auto *label = Gtk::make_managed<Gtk::Label>("☆  " + display_path(path));
            label->set_xalign(0.0F);
            label->set_ellipsize(Pango::ELLIPSIZE_MIDDLE);
            row->add(*label);
            row->set_data("filescope-path", const_cast<char *>(path.c_str()));
            bookmarks_box_->append(*row);
        }
        bookmarks_box_->show_all();
    }

    void activate_bookmark(Gtk::ListBoxRow *row) {
        const int index = row->get_index();
        if (index >= 0 && static_cast<std::size_t>(index) < bookmarks_.list().size()) {
            navigate(bookmarks_.list()[static_cast<std::size_t>(index)], true);
        }
    }

    void create_tab(const std::string &path) {
        auto tab = std::make_unique<Tab>(path);
        tab->model = Gtk::ListStore::create(file_columns_);
        tab->tree = Gtk::make_managed<Gtk::TreeView>(tab->model);
        tab->tree->set_headers_visible(true);
        tab->tree->set_enable_search(false);
        set_accessible(*tab->tree, "파일 목록");
        tab->tree->get_selection()->set_mode(Gtk::SELECTION_MULTIPLE);
        tab->tree->append_column("이름", file_columns_.name);
        tab->tree->append_column("크기", file_columns_.size);
        tab->tree->append_column("변동", file_columns_.delta);
        tab->tree->append_column("이벤트", file_columns_.event);
        tab->tree->append_column("수정 시각", file_columns_.modified);
        tab->tree->append_column("권한", file_columns_.mode);
        for (int column : {0, 1}) {
            auto *renderer = dynamic_cast<Gtk::CellRendererText *>(
                tab->tree->get_column(column)->get_first_cell());
            tab->tree->get_column(column)->add_attribute(renderer->property_strikethrough(),
                                                         file_columns_.deleted);
        }
        for (auto *column : tab->tree->get_columns()) {
            auto *renderer = column->get_first_cell();
            column->set_cell_data_func(
                *renderer, sigc::bind(sigc::mem_fun(*this, &Impl::paint_tick), tab.get()));
        }
        build_file_menu(*tab);
        tab->tree->get_column(0)->set_expand(true);
        tab->tree->get_column(1)->set_fixed_width(92);
        tab->tree->get_column(2)->set_fixed_width(112);
        tab->tree->get_column(3)->set_fixed_width(96);
        tab->tree->get_column(4)->set_fixed_width(122);
        tab->tree->get_column(5)->set_fixed_width(104);
        tab->tree->signal_row_activated().connect(
            sigc::bind(sigc::mem_fun(*this, &Impl::activate_file), tab.get()));
        tab->tree->get_selection()->signal_changed().connect(
            sigc::mem_fun(*this, &Impl::update_status));

        tab->state = Gtk::make_managed<Gtk::Stack>();
        auto *scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
        scroll->add(*tab->tree);
        tab->state->add(*scroll, "list");
        auto *empty = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 8);
        empty->set_valign(Gtk::ALIGN_CENTER);
        empty->set_halign(Gtk::ALIGN_CENTER);
        auto *empty_title = Gtk::make_managed<Gtk::Label>("이 폴더는 비어 있습니다");
        add_class(*empty_title, "fs-empty-title");
        auto *empty_copy = Gtk::make_managed<Gtk::Label>(
            "감시 중이라 새 파일이 생기면 바로 나타납니다.  touch notes.md");
        add_class(*empty_copy, "fs-empty-copy");
        empty->pack_start(*empty_title, false, false);
        empty->pack_start(*empty_copy, false, false);
        auto *empty_actions = Gtk::make_managed<Gtk::ButtonBox>(Gtk::ORIENTATION_HORIZONTAL);
        auto *new_file = Gtk::make_managed<Gtk::Button>("새 파일");
        auto *new_folder = Gtk::make_managed<Gtk::Button>("새 폴더");
        new_file->signal_clicked().connect([this] { create_entry(false); });
        new_folder->signal_clicked().connect([this] { create_entry(true); });
        empty_actions->add(*new_file);
        empty_actions->add(*new_folder);
        empty->pack_start(*empty_actions, false, false);
        tab->state->add(*empty, "empty");
        auto *loading = Gtk::make_managed<Gtk::Grid>();
        loading->set_valign(Gtk::ALIGN_START);
        loading->set_row_spacing(24);
        loading->set_column_spacing(12);
        loading->set_margin_top(12);
        loading->set_margin_start(12);
        loading->set_margin_end(12);
        set_accessible(*loading, "불러오는 중…");
        const bool motion = Gtk::Settings::get_default()->property_gtk_enable_animations();
        for (int row = 0; row < 9; ++row) {
            for (int column = 0; column < 6; ++column) {
                auto *bar = Gtk::make_managed<Gtk::Box>();
                add_class(*bar, "fs-skeleton");
                if (motion) {
                    add_class(*bar, "animated");
                }
                bar->set_hexpand(true);
                loading->attach(*bar, column, row, 1, 1);
            }
        }
        tab->state->add(*loading, "loading");
        auto *error = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 8);
        error->set_valign(Gtk::ALIGN_CENTER);
        error->set_halign(Gtk::ALIGN_CENTER);
        tab->error_label = Gtk::make_managed<Gtk::Label>();
        add_class(*tab->error_label, "fs-empty-copy");
        auto *parent = Gtk::make_managed<Gtk::Button>("상위 폴더로 이동");
        parent->signal_clicked().connect(sigc::mem_fun(*this, &Impl::go_up));
        error->pack_start(*tab->error_label, false, false);
        error->pack_start(*parent, false, false);
        tab->state->add(*error, "error");
        tab->state->set_visible_child("loading");
        auto *tab_label = Gtk::make_managed<Gtk::Label>(display_path(basename(path)));
        notebook_->append_page(*tab->state, *tab_label);
        tabs_.push_back(std::move(tab));
        notebook_->set_current_page(static_cast<int>(tabs_.size() - 1));
        notebook_->show_all();
        load_tab(*tabs_.back());
    }

    void build_file_menu(Tab &tab) {
        tab.menu = std::make_unique<Gtk::Menu>();
        const auto add_action = [&](const Glib::ustring &label, const sigc::slot<void> &action) {
            auto *item = Gtk::make_managed<Gtk::MenuItem>(label);
            item->signal_activate().connect(action);
            tab.menu->append(*item);
        };
        add_action("기본 앱으로 열기", [this, &tab] {
            const auto rows = tab.tree->get_selection()->get_selected_rows();
            if (rows.size() == 1) {
                activate_file(rows.front(), nullptr, &tab);
            }
        });
        add_action("텍스트 편집", [this] {
            const auto paths = selected_paths();
            if (paths.size() == 1) {
                edit_text(paths.front());
            }
        });
        add_action("이름 변경", sigc::mem_fun(*this, &Impl::rename_selected));
        add_action("복사", [this] { copy_selection(false); });
        add_action("잘라내기", [this] { copy_selection(true); });
        add_action("붙여넣기", sigc::mem_fun(*this, &Impl::paste_clipboard));
        add_action("휴지통으로 이동", [this] { remove_selected(false); });
        tab.menu->show_all();
        tab.tree->signal_popup_menu().connect([&tab] {
            tab.menu->popup_at_widget(tab.tree, Gdk::GRAVITY_CENTER, Gdk::GRAVITY_NORTH_WEST,
                                      nullptr);
            return true;
        });
        tab.tree->signal_button_press_event().connect(
            [&tab](GdkEventButton *event) {
                if (event->type != GDK_BUTTON_PRESS || event->button != GDK_BUTTON_SECONDARY) {
                    return false;
                }
                Gtk::TreeModel::Path path;
                if (tab.tree->get_path_at_pos(static_cast<int>(event->x),
                                              static_cast<int>(event->y), path)) {
                    auto selection = tab.tree->get_selection();
                    if (!selection->is_selected(path)) {
                        tab.tree->set_cursor(path);
                    }
                }
                tab.menu->popup_at_pointer(reinterpret_cast<GdkEvent *>(event));
                return true;
            },
            false);
    }

    Tab *current_tab() {
        const int page = notebook_->get_current_page();
        if (page < 0 || static_cast<std::size_t>(page) >= tabs_.size()) {
            return nullptr;
        }
        return tabs_[static_cast<std::size_t>(page)].get();
    }

    void switch_tab(Gtk::Widget *, guint page) {
        if (page < tabs_.size()) {
            update_navigation(*tabs_[page]);
            update_watch_band();
            update_status();
        }
    }

    void navigate(const std::string &path, bool push) {
        Tab *tab = current_tab();
        if (tab == nullptr) {
            return;
        }
        if (push) {
            tab->pending_history = tab->history;
            tab->pending_history->push(core::canonicalize(path));
        }
        load_tab(*tab);
    }

    void load_tab(Tab &tab) {
        if (tab.cancellable) {
            tab.cancellable->cancel();
        }
        tab.cancellable = Gio::Cancellable::create();
        ++tab.generation;
        tab.state->set_visible_child("loading");
        status_->set_text("불러오는 중…");
        update_navigation(tab);
        const auto &history = tab.pending_history ? *tab.pending_history : tab.history;
        nav::list_dir_async(
            history.current(), tab.show_hidden, false, tab.cancellable,
            sigc::bind(sigc::mem_fun(*this, &Impl::finish_list), &tab, tab.generation));
    }

    void finish_list(core::Result<std::vector<core::Entry>> result, Tab *tab,
                     std::uint64_t generation) {
        if (shutdown_ || generation != tab->generation) {
            return;
        }
        if (!result.ok()) {
            tab->error_label->set_text(result.error().code == core::ErrorCode::PERMISSION_DENIED
                                           ? "읽을 권한이 없습니다"
                                           : "폴더를 열 수 없습니다");
            tab->state->set_visible_child(tab->pending_history
                                              ? (tab->model->children().empty() ? "empty" : "list")
                                              : "error");
            tab->pending_history.reset();
            tab->pending_selection.reset();
            if (current_tab() == tab) {
                update_navigation(*tab);
                update_status();
                show_band("⚠ " + tab->error_label->get_text(), false);
            }
            return;
        }
        if (tab->pending_history) {
            tab->history = std::move(*tab->pending_history);
            tab->pending_history.reset();
        }
        tab->rows.clear();
        tab->model->clear();
        auto entries = std::move(result.value());
        std::sort(entries.begin(), entries.end(),
                  [](const core::Entry &left, const core::Entry &right) {
                      if ((left.type == core::FileType::DIRECTORY) !=
                          (right.type == core::FileType::DIRECTORY)) {
                          return left.type == core::FileType::DIRECTORY;
                      }
                      return left.name < right.name;
                  });
        for (const auto &entry : entries) {
            append_entry(*tab, entry);
        }
        tab->state->set_visible_child(entries.empty() ? "empty" : "list");
        if (tab->pending_selection) {
            select_file(*tab, *tab->pending_selection);
            tab->pending_selection.reset();
        }
        if (current_tab() == tab) {
            update_navigation(*tab);
            update_watch_band();
            update_status();
        }
    }

    void append_entry(Tab &tab, const core::Entry &entry) {
        const auto iter = tab.model->append();
        const auto &row = *iter;
        row[file_columns_.name] = display_path(entry.name);
        const auto path = core::path_join(tab.history.current(), entry.name);
        row[file_columns_.path] = path;
        tab.rows.emplace(path, iter);
        row[file_columns_.size] =
            entry.type == core::FileType::DIRECTORY ? "—" : format_bytes(entry.size);
        row[file_columns_.delta] = "—";
        row[file_columns_.event] = "";
        row[file_columns_.modified] = format_time(entry.mtime_ns / 1000, false);
        std::ostringstream mode;
        mode << std::oct << std::setw(4) << std::setfill('0') << (entry.mode & 07777U);
        row[file_columns_.mode] = mode.str();
        row[file_columns_.is_dir] = entry.type == core::FileType::DIRECTORY ||
                                    (entry.type == core::FileType::SYMLINK &&
                                     entry.target_type == core::FileType::DIRECTORY);
    }

    void update_navigation(Tab &tab) {
        back_->set_sensitive(tab.history.can_back());
        forward_->set_sensitive(tab.history.can_forward());
        up_->set_sensitive(nav::parent(tab.history.current()).has_value());
        const auto children = breadcrumb_->get_children();
        for (Gtk::Widget *child : children) {
            gtk_widget_destroy(child->gobj());
        }
        const auto crumbs = nav::breadcrumb(tab.history.current());
        for (std::size_t index = 0; index < crumbs.size(); ++index) {
            if (index != 0) {
                breadcrumb_->pack_start(*Gtk::make_managed<Gtk::Label>("›"), false, false);
            }
            auto *button = Gtk::make_managed<Gtk::Button>(
                crumbs[index] == "/" ? "/" : display_path(basename(crumbs[index])));
            button->set_relief(Gtk::RELIEF_NONE);
            button->signal_clicked().connect(
                sigc::bind(sigc::mem_fun(*this, &Impl::navigate), crumbs[index], true));
            breadcrumb_->pack_start(*button, false, false);
        }
        breadcrumb_->show_all();
        show_breadcrumb_end();
        search_->set_text(display_path(tab.history.current()));
        const int page = notebook_->get_current_page();
        if (page >= 0) {
            auto *label = dynamic_cast<Gtk::Label *>(notebook_->get_tab_label(*tab.state));
            if (label != nullptr) {
                label->set_text(display_path(basename(tab.history.current())));
            }
        }
    }

    void show_breadcrumb_end() {
        breadcrumb_scroll_->set_value(breadcrumb_scroll_->get_upper() -
                                      breadcrumb_scroll_->get_page_size());
    }

    void activate_search() {
        const std::string input = search_->get_text();
        if (!input.empty()) {
            navigate(input, true);
        }
    }

    void complete_address() {
        if (completion_cancel_) {
            completion_cancel_->cancel();
        }
        completion_model_->clear();
        if (!search_->has_focus()) {
            return;
        }
        completion_cancel_ = Gio::Cancellable::create();
        const Tab *tab = current_tab();
        nav::complete_async(
            search_->get_text(), tab != nullptr && tab->show_hidden, 20, completion_cancel_,
            sigc::bind(sigc::mem_fun(*this, &Impl::finish_completion), completion_cancel_));
    }

    void finish_completion(core::Result<std::vector<std::string>> result,
                           const Glib::RefPtr<Gio::Cancellable> &request) {
        if (shutdown_ || request != completion_cancel_ || request->is_cancelled() || !result.ok()) {
            return;
        }
        for (const auto &path : result.value()) {
            (*completion_model_->append())[completion_columns_.path] = display_path(path);
        }
        completion_->complete();
    }

    void go_back() {
        Tab *tab = current_tab();
        if (tab != nullptr && tab->history.can_back()) {
            tab->pending_history = tab->history;
            tab->pending_history->back();
            load_tab(*tab);
        }
    }

    void go_forward() {
        Tab *tab = current_tab();
        if (tab != nullptr && tab->history.can_forward()) {
            tab->pending_history = tab->history;
            tab->pending_history->forward();
            load_tab(*tab);
        }
    }

    void go_up() {
        Tab *tab = current_tab();
        if (tab == nullptr) {
            return;
        }
        const auto parent = nav::parent(tab->history.current());
        if (parent.has_value()) {
            navigate(parent.value(), true);
        }
    }

    void activate_file(const Gtk::TreeModel::Path &path, Gtk::TreeViewColumn *, Tab *tab) {
        const auto iterator = tab->model->get_iter(path);
        if (!iterator) {
            return;
        }
        const auto &row = *iterator;
        if (row[file_columns_.deleted]) {
            return;
        }
        const std::string selected = row[file_columns_.path];
        if (row[file_columns_.is_dir]) {
            navigate(selected, true);
            return;
        }
        try {
            Gio::AppInfo::launch_default_for_uri(Glib::filename_to_uri(selected));
        } catch (const Glib::Error &error) {
            show_band(error.code() == G_IO_ERROR_NOT_SUPPORTED ? "⚠ 기본 앱이 없습니다"
                                                               : "⚠ 파일을 열 수 없습니다",
                      false);
        }
    }

    std::vector<std::string> selected_paths() const {
        const int page = notebook_->get_current_page();
        if (page < 0 || static_cast<std::size_t>(page) >= tabs_.size()) {
            return {};
        }
        const Tab &tab = *tabs_[static_cast<std::size_t>(page)];
        if (!tab.tree->get_sensitive() || tab.state->get_visible_child_name() != "list") {
            return {};
        }
        std::vector<std::string> paths;
        for (const auto &path : tab.tree->get_selection()->get_selected_rows()) {
            const auto iterator = tab.model->get_iter(path);
            if (iterator && !(*iterator)[file_columns_.deleted]) {
                paths.push_back((*iterator)[file_columns_.path]);
            }
        }
        return paths;
    }

    bool on_key(GdkEventKey *event) {
        const bool control = (event->state & GDK_CONTROL_MASK) != 0U;
        const bool shift = (event->state & GDK_SHIFT_MASK) != 0U;
        const bool alt = (event->state & GDK_MOD1_MASK) != 0U;
        if (control && event->keyval == GDK_KEY_t) {
            create_tab(current_tab() == nullptr ? nav::home() : current_tab()->history.current());
            return true;
        }
        if (control && event->keyval == GDK_KEY_l) {
            search_->grab_focus();
            search_->select_region(0, -1);
            return true;
        }
        if (control && event->keyval == GDK_KEY_h) {
            Tab *tab = current_tab();
            if (tab != nullptr) {
                tab->show_hidden = !tab->show_hidden;
                load_tab(*tab);
            }
            return true;
        }
        if (alt && event->keyval == GDK_KEY_Left) {
            go_back();
            return true;
        }
        if (alt && event->keyval == GDK_KEY_Right) {
            go_forward();
            return true;
        }
        if (alt && event->keyval == GDK_KEY_Up) {
            go_up();
            return true;
        }
        auto *focus = window_.get_focus();
        if (focus != nullptr && focus->is_ancestor(*bottom_switcher_) &&
            (event->keyval == GDK_KEY_Left || event->keyval == GDK_KEY_Right)) {
            bottom_stack_->set_visible_child(event->keyval == GDK_KEY_Left ? "log" : "chart");
            return true;
        }
        if (dynamic_cast<Gtk::Editable *>(focus) != nullptr) {
            return false;
        }
        if (control && event->keyval == GDK_KEY_d && current_tab()) {
            const auto result = bookmarks_.add(current_tab()->history.current());
            if (!result.ok() || !bookmarks_.save().ok()) {
                show_band("⚠ 즐겨찾기를 저장할 수 없습니다", false);
            }
            rebuild_bookmarks();
            return true;
        }
        if (control && event->keyval == GDK_KEY_e) {
            const auto paths = selected_paths();
            if (paths.size() == 1) {
                edit_text(paths.front());
            }
            return true;
        }
        if (event->keyval == GDK_KEY_Delete) {
            remove_selected(shift);
            return true;
        }
        if (event->keyval == GDK_KEY_F2) {
            rename_selected();
            return true;
        }
        if (control && event->keyval == GDK_KEY_c) {
            copy_selection(false);
            return true;
        }
        if (control && event->keyval == GDK_KEY_x) {
            copy_selection(true);
            return true;
        }
        if (control && event->keyval == GDK_KEY_v) {
            paste_clipboard();
            return true;
        }
        if (control && shift && event->keyval == GDK_KEY_n) {
            create_entry(true);
            return true;
        }
        if (control && event->keyval == GDK_KEY_n) {
            create_entry(false);
            return true;
        }
        return false;
    }

    void copy_selection(bool cut) {
        clipboard_ = selected_paths();
        clipboard_cut_ = cut;
    }

    void edit_text(const std::string &path) {
        auto file = std::make_shared<std::optional<fs::TextFile>>();
        const auto task =
            queue_.submit("텍스트 열기", [file, path](job::Job &) -> core::Result<void> {
                auto result = fs::read_text(path);
                if (!result.ok()) {
                    return result.error();
                }
                file->emplace(std::move(result.value()));
                return {};
            });
        if (!task.ok()) {
            show_band("⚠ 파일을 열 수 없습니다", false);
            return;
        }
        editor_jobs_.emplace(task.value(), [this, file, path](job::JobState state) {
            if (state == job::JobState::DONE && file->has_value()) {
                show_editor(path, **file);
            } else {
                show_band("⚠ 파일을 열 수 없습니다", false);
            }
        });
    }

    void show_editor(const std::string &path, const fs::TextFile &file) {
        // design-pending: U-6
        Gtk::Dialog dialog(display_path(path), window_, true);
        dialog.set_default_size(640, 400);
        dialog.add_button("취소", Gtk::RESPONSE_CANCEL);
        dialog.add_button("저장", Gtk::RESPONSE_OK);
        auto *scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
        auto *editor = Gtk::make_managed<Gtk::TextView>();
        set_accessible(*editor, "파일 내용");
        editor->get_buffer()->set_text(file.contents);
        scroll->add(*editor);
        dialog.get_content_area()->pack_start(*scroll, true, true);
        dialog.show_all();
        if (dialog.run() != Gtk::RESPONSE_OK) {
            return;
        }
        const auto contents = editor->get_buffer()->get_text();
        const auto task = queue_.submit(
            "텍스트 저장", [path, contents, stamp = file.stamp](job::Job &) -> core::Result<void> {
                const auto result = fs::write_text(path, contents, stamp, fs::Confirmation::NONE);
                if (!result.ok()) {
                    return result.error();
                }
                return {};
            });
        if (!task.ok()) {
            show_band("⚠ 저장을 시작할 수 없습니다", false);
            return;
        }
        editor_jobs_.emplace(task.value(), [this](job::JobState state) {
            if (state != job::JobState::DONE) {
                show_band("⚠ 파일이 바뀌었거나 저장할 수 없습니다", false);
            }
        });
    }

    // The title and optional starting value both use GTK text.
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    std::optional<std::string> ask_name(const Glib::ustring &title,
                                        const Glib::ustring &initial = {}) {
        // design-pending: U-6
        Gtk::Dialog dialog(title, window_, true);
        dialog.add_button("취소", Gtk::RESPONSE_CANCEL);
        dialog.add_button("확인", Gtk::RESPONSE_OK);
        auto *entry = Gtk::make_managed<Gtk::Entry>();
        entry->set_text(initial);
        entry->set_activates_default(true);
        dialog.get_content_area()->pack_start(*entry, false, false, 12);
        dialog.set_default_response(Gtk::RESPONSE_OK);
        dialog.show_all();
        if (dialog.run() != Gtk::RESPONSE_OK) {
            return std::nullopt;
        }
        return entry->get_text();
    }

    void create_entry(bool directory) {
        Tab *tab = current_tab();
        if (!can_modify(tab)) {
            return;
        }
        const auto name = ask_name(directory ? "새 폴더" : "새 파일");
        if (!name.has_value()) {
            return;
        }
        const auto result = directory ? fs::create_dir(tab->history.current(), name.value())
                                      : fs::create_file(tab->history.current(), name.value());
        if (!result.ok()) {
            show_band("⚠ 만들 수 없습니다", false);
        } else {
            load_tab(*tab);
        }
    }

    void rename_selected() {
        const auto paths = selected_paths();
        if (paths.size() != 1) {
            return;
        }
        const auto name = ask_name("이름 변경", display_path(basename(paths.front())));
        if (!name.has_value()) {
            return;
        }
        const auto result = fs::rename(paths.front(), name.value());
        if (!result.ok()) {
            show_band("⚠ 이름을 바꿀 수 없습니다", false);
        } else if (current_tab() != nullptr) {
            load_tab(*current_tab());
        }
    }

    void remove_selected(bool permanent) {
        const auto paths = selected_paths();
        if (paths.empty()) {
            return;
        }
        if (!permanent) {
            const auto result = fs::trash_many(queue_, paths);
            handle_job_submit(result, "휴지통으로 이동");
            return;
        }
        // design-pending: U-6
        Gtk::MessageDialog dialog(window_, "영구 삭제하시겠습니까?", false, Gtk::MESSAGE_WARNING,
                                  Gtk::BUTTONS_NONE, true);
        dialog.set_secondary_text(std::to_string(paths.size()) + "개 항목 · " +
                                  display_path(basename(paths.front())));
        dialog.add_button("취소", Gtk::RESPONSE_CANCEL);
        dialog.add_button("영구 삭제", Gtk::RESPONSE_OK);
        dialog.set_default_response(Gtk::RESPONSE_CANCEL);
        if (dialog.run() == Gtk::RESPONSE_OK) {
            handle_job_submit(fs::delete_permanently(queue_, paths, fs::Confirmation::CONFIRMED),
                              "영구 삭제");
        }
    }

    void paste_clipboard() {
        Tab *tab = current_tab();
        if (!can_modify(tab) || clipboard_.empty()) {
            return;
        }
        fs::ConflictPolicy policy = fs::ConflictPolicy::FAIL;
        const auto conflicts = fs::find_conflicts(clipboard_, tab->history.current());
        if (!conflicts.ok()) {
            show_band("⚠ 붙여넣기를 준비할 수 없습니다", false);
            return;
        }
        if (!conflicts.value().empty()) {
            // design-pending: U-6
            Gtk::Dialog dialog("같은 이름의 항목", window_, true);
            dialog.add_button("취소", Gtk::RESPONSE_CANCEL);
            dialog.add_button("건너뛰기", 1);
            dialog.add_button("둘 다 유지", 2);
            dialog.add_button("덮어쓰기", 3);
            dialog.set_default_response(Gtk::RESPONSE_CANCEL);
            auto *label = Gtk::make_managed<Gtk::Label>(std::to_string(conflicts.value().size()) +
                                                        "개 이름이 겹칩니다");
            dialog.get_content_area()->pack_start(*label, false, false, 12);
            dialog.show_all();
            const int response = dialog.run();
            if (response < 1 || response > 3) {
                return;
            }
            policy = response == 1   ? fs::ConflictPolicy::SKIP
                     : response == 2 ? fs::ConflictPolicy::KEEP_BOTH
                                     : fs::ConflictPolicy::OVERWRITE;
        }
        const fs::Confirmation confirmation = policy == fs::ConflictPolicy::OVERWRITE
                                                  ? fs::Confirmation::CONFIRMED
                                                  : fs::Confirmation::NONE;
        const auto result =
            clipboard_cut_
                ? fs::move(queue_, clipboard_, tab->history.current(), policy, confirmation)
                : fs::copy(queue_, clipboard_, tab->history.current(), policy, confirmation);
        handle_job_submit(result, clipboard_cut_ ? "이동" : "복사");
        if (result.ok() && clipboard_cut_) {
            clipboard_.clear();
        }
    }

    void handle_job_submit(const core::Result<job::JobId> &result, const Glib::ustring &title) {
        if (!result.ok()) {
            show_band("⚠ 작업을 시작할 수 없습니다", false);
            return;
        }
        file_jobs_.emplace(result.value(), title);
        job_result_->set_reveal_child(false);
        if (!active_job_) {
            show_next_job();
        }
    }

    void show_next_job() {
        if (file_jobs_.empty()) {
            active_job_.reset();
            job_pulse_ = false;
            job_box_->hide();
            return;
        }
        const auto &next = *file_jobs_.begin();
        active_job_ = next.first;
        job_label_->set_text(next.second);
        job_progress_->pulse();
        job_pulse_ = true;
        job_box_->show();
    }

    void cancel_job() {
        if (active_job_.has_value()) {
            queue_.cancel(active_job_.value());
            job_label_->set_text("취소 중…");
        }
    }

    void on_job_progress(job::JobId id, const job::Progress &progress) {
        if (!active_job_.has_value() || id != active_job_.value()) {
            return;
        }
        if (progress.bytes_total > 0 || progress.items_total > 0) {
            job_pulse_ = false;
            const double fraction = progress.bytes_total > 0
                                        ? static_cast<double>(progress.bytes_done) /
                                              static_cast<double>(progress.bytes_total)
                                        : static_cast<double>(progress.items_done) /
                                              static_cast<double>(progress.items_total);
            job_progress_->set_fraction(std::clamp(fraction, 0.0, 1.0));
        } else {
            job_pulse_ = true;
            job_progress_->pulse();
        }
        job_label_->set_text(display_path(progress.current_path));
    }

    void on_job_state(job::JobId id, job::JobState state, const std::optional<core::Error> &) {
        if (state != job::JobState::DONE && state != job::JobState::FAILED &&
            state != job::JobState::CANCELLED) {
            return;
        }
        const auto editor = editor_jobs_.find(id);
        if (editor != editor_jobs_.end()) {
            auto done = std::move(editor->second);
            editor_jobs_.erase(editor);
            queue_.forget(id);
            done(state);
            return;
        }
        const auto results = queue_.results(id);
        if (state == job::JobState::FAILED) {
            show_band("⚠ 일부 항목을 처리하지 못했습니다 (" + std::to_string(results.size()) +
                          "개 결과)",
                      false);
        } else if (state == job::JobState::CANCELLED) {
            const auto complete =
                std::count_if(results.begin(), results.end(), [](const job::ItemResult &item) {
                    return !item.error && !item.skipped;
                });
            job_result_label_->set_text("작업을 취소했습니다 · 완료된 항목 " +
                                        std::to_string(complete) + "개");
            job_result_->set_reveal_child(true);
        } else {
            toast_label_->set_text("파일 작업을 완료했습니다");
            toast_->set_reveal_child(true);
            toast_timer_.disconnect();
            toast_timer_ = Glib::signal_timeout().connect_seconds(
                [this] {
                    toast_->set_reveal_child(false);
                    return false;
                },
                5);
        }
        queue_.forget(id);
        file_jobs_.erase(id);
        if (active_job_ == id) {
            show_next_job();
        }
        if (current_tab() != nullptr) {
            load_tab(*current_tab());
        }
    }

    void on_events(const std::vector<core::EventPtr> &events) {
        dash_.add_events(events);
        for (const auto &event : events) {
            log_rows_.push_front({event, format_time(event->observed_at_us, true),
                                  event_label(event->kind),
                                  format_signed_delta(event->size_delta.value_or(0))});
            if (log_rows_.size() > kMaxLogRows) {
                log_rows_.pop_back();
            }
            for (auto &tab : tabs_) {
                apply_event(*tab, *event);
            }
            const std::int64_t now_ns = g_get_monotonic_time() * 1000;
            if (event->monotonic_ns > 0 && now_ns >= event->monotonic_ns) {
                const auto latency = now_ns - event->monotonic_ns;
                dash_.record_latency(latency);
                g_debug("FileScope event-to-model latency: %" G_GINT64_FORMAT " ns", latency);
            }
        }
        if (!events.empty()) {
            start_tick_timer();
        }
        update_status();
        rebuild_log();
        update_counts();
        update_metrics();
        chart_->queue_draw();
        for (CandleChart *chart : detached_charts_) {
            chart->queue_draw();
        }
    }

    void paint_tick(Gtk::CellRenderer *cell, const Gtk::TreeModel::iterator &row, Tab *tab) {
        auto &text = static_cast<Gtk::CellRendererText &>(*cell);
        const bool stale = !tab->tree->get_sensitive();
        const bool deleted = (*row)[file_columns_.deleted];
        text.property_foreground_set() = false;
        if (stale || deleted) {
            text.property_foreground() = "#8a94a3";
        }
        const int direction = (*row)[file_columns_.tick_direction];
        const std::int64_t start = (*row)[file_columns_.tick_start];
        const auto elapsed = g_get_monotonic_time() - start;
        const bool motion = Gtk::Settings::get_default()->property_gtk_enable_animations();
        text.property_cell_background_set() = false;
        if (stale || direction == 0 || elapsed >= (motion ? 13600000 : 12000000) ||
            tab->tree->get_selection()->is_selected(row)) {
            return;
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
        text.property_cell_background_rgba() = color;
    }

    void mark_tick(const Gtk::TreeModel::Row &row, int direction) {
        row[file_columns_.tick_direction] = direction;
        row[file_columns_.tick_start] = g_get_monotonic_time();
        if (direction == 0) {
            return;
        }
        start_tick_timer();
    }

    void paint_log_tick(Gtk::CellRenderer *cell, const Gtk::TreeModel::iterator &row) {
        auto &text = static_cast<Gtk::CellRendererText &>(*cell);
        const std::int64_t start = (*row)[log_columns_.tick_start];
        const auto elapsed = g_get_monotonic_time() - start;
        text.property_cell_background_set() = false;
        if (elapsed >= 0 && elapsed < 1200000 &&
            Gtk::Settings::get_default()->property_gtk_enable_animations() &&
            !log_tree_->get_selection()->is_selected(row)) {
            Gdk::RGBA color("#243041");
            color.set_alpha(1.0 - static_cast<double>(elapsed) / 1200000.0);
            text.property_cell_background_rgba() = color;
        }
    }

    void start_tick_timer() {
        last_tick_ = g_get_monotonic_time();
        if (!tick_timer_.connected()) {
            tick_timer_ =
                Glib::signal_timeout().connect(sigc::mem_fun(*this, &Impl::redraw_ticks), 33);
        }
    }

    bool redraw_ticks() {
        if (auto *tab = current_tab()) {
            tab->tree->queue_draw();
        }
        log_tree_->queue_draw();
        return g_get_monotonic_time() - last_tick_ < 13600000;
    }

    void apply_event(Tab &tab, const core::Event &event) {
        const auto &directory = tab.history.current();
        if (event.kind == core::EventKind::RESYNC_DONE) {
            if (watcher_ && watcher_->find_root(directory) == event.watch_id) {
                load_tab(tab);
                if (current_tab() == &tab) {
                    hide_band();
                    overflow_label_->set_text("대기열이 넘쳐 다시 검사해 맞췄습니다 · " +
                                              format_time(event.observed_at_us, false));
                    overflow_->set_reveal_child(true);
                }
            }
            return;
        }
        const auto parent = nav::parent(event.path);
        const auto old_parent = event.old_path.empty() ? std::nullopt : nav::parent(event.old_path);
        const auto name = basename(event.path);
        const bool visible_destination =
            parent == directory && !name.empty() && (tab.show_hidden || name.front() != '.');
        const bool visible_source = old_parent == directory;
        if (!visible_destination && !visible_source) {
            return;
        }
        const auto &source_path = visible_source ? event.old_path : event.path;
        const auto source = tab.rows.find(source_path);
        auto found = source != tab.rows.end() ? source->second : Gtk::TreeModel::iterator{};
        if (!visible_destination || event.kind == core::EventKind::DELETE ||
            event.kind == core::EventKind::MOVED_OUT) {
            if (found) {
                (*found)[file_columns_.deleted] = true;
                (*found)[file_columns_.delta] = "✕ 삭제";
                (*found)[file_columns_.event] = event_label(event.kind);
                mark_tick(*found, -1);
                if (!Gtk::Settings::get_default()->property_gtk_enable_animations().get_value()) {
                    tab.rows.erase(source_path);
                    tab.model->erase(found);
                } else {
                    const Gtk::TreeRowReference reference(tab.model, Gtk::TreeModel::Path(found));
                    Glib::signal_timeout().connect(
                        sigc::bind(sigc::mem_fun(*this, &Impl::remove_deleted_row), &tab,
                                   reference),
                        30600);
                }
            }
        } else {
            const auto destination = tab.rows.find(event.path);
            if (destination != tab.rows.end()) {
                if (!found) {
                    found = destination->second;
                } else if (source_path != event.path) {
                    tab.model->erase(destination->second);
                }
            }
            if (!found &&
                (event.kind == core::EventKind::CREATE || event.kind == core::EventKind::MOVED_IN ||
                 visible_source || old_parent.has_value())) {
                found = tab.model->append();
            }
            if (found) {
                if (source_path != event.path) {
                    tab.rows.erase(source_path);
                }
                tab.rows.insert_or_assign(event.path, found);
                const auto &row = *found;
                row[file_columns_.deleted] = false;
                row[file_columns_.name] = display_path(basename(event.path));
                row[file_columns_.path] = event.path;
                row[file_columns_.is_dir] = event.is_dir;
                if (event.stat.has_value()) {
                    row[file_columns_.size] = format_bytes(event.stat->size);
                }
                row[file_columns_.delta] = format_delta(event);
                if (!event.size_delta && (event.kind == core::EventKind::MODIFY ||
                                          event.kind == core::EventKind::CLOSE_WRITE)) {
                    set_unknown_delta(tab, found);
                }
                row[file_columns_.event] = event_label(event.kind);
                row[file_columns_.modified] = format_time(event.observed_at_us, false);
                const auto delta = event.size_delta.value_or(0);
                const bool created = event.kind == core::EventKind::CREATE ||
                                     event.kind == core::EventKind::MOVED_IN;
                mark_tick(row, created || delta > 0 ? 1 : delta < 0 ? -1 : 0);
            }
        }
        update_list_visibility(tab);
    }

    static void set_unknown_delta(Tab &tab, const Gtk::TreeModel::iterator &row) {
        const auto accessible = tab.tree->get_accessible();
        auto *object = accessible->gobj();
        if (ATK_IS_TABLE(object)) {
            const auto path = Gtk::TreeModel::Path(row);
            if (auto *cell = atk_table_ref_at(ATK_TABLE(object), path[0], 2)) {
                atk_object_set_name(cell, "변동 알 수 없음");
                g_object_unref(cell);
            }
        }
    }

    bool remove_deleted_row(Tab *tab, const Gtk::TreeRowReference &reference) {
        if (!shutdown_ && reference.is_valid()) {
            const auto path = reference.get_path();
            const auto row = tab->model->get_iter(path);
            if (row && (*row)[file_columns_.deleted]) {
                if (tab->tree->get_selection()->is_selected(path)) {
                    return true;
                }
                const std::string file_path = (*row)[file_columns_.path];
                tab->rows.erase(file_path);
                tab->model->erase(row);
                update_list_visibility(*tab);
                update_status();
            }
        }
        return false;
    }

    static void update_list_visibility(Tab &tab) {
        const auto child = tab.state->get_visible_child_name();
        if (child == "list" || child == "empty") {
            tab.state->set_visible_child(tab.model->children().empty() ? "empty" : "list");
        }
    }

    void on_watch_state(const mon::WatchInfo &info, const std::optional<core::Error> &) {
        if (info.reason == mon::WatchReason::ROOT_DELETED) {
            deleted_roots_.insert(info.path);
            overflow_->set_reveal_child(false);
            update_watch_band();
        } else if (info.state == mon::WatchState::DEGRADED ||
                   info.state == mon::WatchState::ERROR) {
            show_band(info.reason == mon::WatchReason::WATCH_LIMIT
                          ? "⚠ 감시 한도 때문에 일부 하위 폴더를 감시하지 못합니다"
                          : "⚠ 감시 중 오류가 발생했습니다",
                      false);
        } else if (info.state == mon::WatchState::ACTIVE) {
            deleted_roots_.erase(info.path);
            g_debug("FileScope watch active");
            dash_.set_root_total(info.id,
                                 info.total_known ? std::optional<std::uint64_t>(info.total_bytes)
                                                  : std::nullopt,
                                 g_get_real_time());
            update_watch_band();
        } else if (info.state == mon::WatchState::RESYNCING) {
            show_band("대기열이 넘쳐 다시 검사하고 있습니다", true);
        }
        update_metrics();
    }

    void update_watch_band() {
        auto *tab = current_tab();
        if (!watcher_ || tab == nullptr || tab->state->get_visible_child_name() == "error") {
            return;
        }
        const auto &path = tab->history.current();
        for (const auto &root : deleted_roots_) {
            if (core::is_ancestor(root, path)) {
                tab->tree->set_sensitive(false);
                show_band("⚠ 감시 폴더가 삭제되었습니다", false);
                stale_parent_->show();
                return;
            }
        }
        tab->tree->set_sensitive(true);
        if (watcher_->find_root(path) == 0) {
            show_band("이 폴더를 감시하지 않습니다", true);
            watch_start_->show();
        } else if (watch_start_->get_visible() || stale_parent_->get_visible()) {
            hide_band();
        }
    }

    void show_band(const Glib::ustring &message, bool quiet) {
        watch_start_->hide();
        stale_parent_->hide();
        band_label_->set_text(message);
        if (quiet) {
            add_class(*band_box_, "quiet");
        } else {
            band_box_->get_style_context()->remove_class("quiet");
        }
        band_->set_reveal_child(true);
    }

    static bool can_modify(const Tab *tab) {
        if (tab == nullptr || !tab->tree->get_sensitive()) {
            return false;
        }
        const auto state = tab->state->get_visible_child_name();
        return state == "list" || state == "empty";
    }

    void hide_band() {
        watch_start_->hide();
        stale_parent_->hide();
        band_->set_reveal_child(false);
    }

    void set_log_filter(Gtk::ToggleButton *button, LogFilter filter) {
        if (!button->get_active()) {
            if (log_filter_ == filter) {
                button->set_active(true);
            }
            return;
        }
        log_filter_ = filter;
        for (Gtk::ToggleButton *other : filter_buttons_) {
            if (other != button) {
                other->set_active(false);
            }
        }
        rebuild_log();
    }

    void rebuild_log() {
        log_model_->clear();
        const std::string needle = log_search_->get_text();
        for (const auto &record : log_rows_) {
            if (!matches_filter(record.event->kind, log_filter_) ||
                (!needle.empty() && record.event->path.find(needle) == std::string::npos)) {
                continue;
            }
            const auto iter = log_model_->append();
            const auto &row = *iter;
            row[log_columns_.time] = record.time;
            row[log_columns_.event] = record.label;
            row[log_columns_.path] = display_path(record.event->path);
            row[log_columns_.raw_path] = record.event->path;
            row[log_columns_.value] = record.value;
            row[log_columns_.tick_start] = record.event->monotonic_ns / 1000;
        }
    }

    void activate_log(const Gtk::TreeModel::Path &path, Gtk::TreeViewColumn *) {
        const auto iterator = log_model_->get_iter(path);
        Tab *tab = current_tab();
        if (!iterator || tab == nullptr) {
            return;
        }
        const std::string path_key = (*iterator)[log_columns_.raw_path];
        if (select_file(*tab, path_key)) {
            return;
        }
        if (const auto directory = nav::parent(path_key);
            directory && *directory != tab->history.current()) {
            tab->pending_selection = path_key;
            navigate(*directory, true);
        }
    }

    static bool select_file(Tab &tab, const std::string &path) {
        const auto row = tab.rows.find(path);
        if (row == tab.rows.end()) {
            return false;
        }
        tab.tree->get_selection()->unselect_all();
        tab.tree->get_selection()->select(row->second);
        tab.tree->scroll_to_row(Gtk::TreeModel::Path(row->second));
        return true;
    }

    void update_counts() {
        const dash::Counts counts = dash_.counts();
        const std::array values{counts.creates, counts.modifies, counts.deletes, counts.moves,
                                counts.attribs};
        const std::array colors{"fs-up", "fs-up", "fs-down", "fs-dim", "fs-dim"};
        for (std::size_t index = 0; index < values.size(); ++index) {
            auto *label = count_labels_[index];
            label->set_text(std::to_string(values[index]));
            if (values[index] > 0) {
                label->get_style_context()->remove_class("fs-dim");
                add_class(*label, colors[index]);
            }
        }
    }

    void update_metrics() {
        const std::size_t roots = watcher_ ? watcher_->list_roots().size() : 0;
        const auto latency = dash_.latency_p95_ns();
        std::ostringstream text;
        text << "감시 경로 " << roots << "   최근 1분 "
             << dash_.events_last_minute(g_get_real_time());
        if (latency.has_value()) {
            text << "   반영 지연 P95 " << latency.value() / 1000000 << "ms";
        }
        metrics_->set_text(text.str());
    }

    void update_status() {
        Tab *tab = current_tab();
        if (tab == nullptr) {
            return;
        }
        const std::size_t total = tab->model->children().size();
        const std::size_t selected = tab->tree->get_selection()->count_selected_rows();
        const mon::WatchUsage usage = watcher_ ? watcher_->usage() : mon::WatchUsage{};
        std::ostringstream text;
        text << total << "개 항목 · " << selected << "개 선택        inotify " << usage.used << '/'
             << usage.limit;
        status_->set_text(text.str());
    }

    void open_chart_window() {
        if (!chart_windows_.empty()) {
            chart_windows_.front()->present();
            return;
        }
        auto chart_window = std::make_unique<Gtk::Window>();
        chart_window->set_title("FileScope · 변경량 1분봉");
        chart_window->set_default_size(900, 420);
        auto *chart = Gtk::make_managed<CandleChart>(dash_);
        chart_window->add(*chart);
        chart_window->set_transient_for(window_);
        chart_window->signal_delete_event().connect([window = chart_window.get()](GdkEventAny *) {
            window->hide();
            return true;
        });
        chart_window->show_all();
        detached_charts_.push_back(chart);
        chart_windows_.push_back(std::move(chart_window));
    }

    MainWindow &window_;
    bool shutdown_ = false;
    Gtk::Box *root_ = nullptr;
    Gtk::HeaderBar *header_ = nullptr;
    Gtk::Button *back_ = nullptr;
    Gtk::Button *forward_ = nullptr;
    Gtk::Button *up_ = nullptr;
    Gtk::Box *breadcrumb_ = nullptr;
    Glib::RefPtr<Gtk::Adjustment> breadcrumb_scroll_;
    Gtk::SearchEntry *search_ = nullptr;
    struct CompletionColumns : Gtk::TreeModel::ColumnRecord {
        CompletionColumns() { add(path); }
        Gtk::TreeModelColumn<Glib::ustring> path;
    } completion_columns_;
    Glib::RefPtr<Gtk::EntryCompletion> completion_;
    Glib::RefPtr<Gtk::ListStore> completion_model_;
    Glib::RefPtr<Gio::Cancellable> completion_cancel_;
    Gtk::Label *metrics_ = nullptr;
    Gtk::Paned *content_ = nullptr;
    Gtk::Box *sidebar_ = nullptr;
    Gtk::ListBox *bookmarks_box_ = nullptr;
    struct FolderColumns : Gtk::TreeModel::ColumnRecord {
        FolderColumns() {
            add(name);
            add(path);
            add(loaded);
        }
        Gtk::TreeModelColumn<Glib::ustring> name;
        Gtk::TreeModelColumn<std::string> path;
        Gtk::TreeModelColumn<bool> loaded;
    } tree_columns_;
    Glib::RefPtr<Gtk::TreeStore> tree_model_;
    Glib::RefPtr<Gio::Cancellable> tree_cancel_;
    Gtk::Revealer *band_ = nullptr;
    Gtk::Box *band_box_ = nullptr;
    Gtk::Label *band_label_ = nullptr;
    Gtk::Revealer *job_result_ = nullptr;
    Gtk::Label *job_result_label_ = nullptr;
    Gtk::Button *watch_start_ = nullptr;
    Gtk::Button *stale_parent_ = nullptr;
    Gtk::Notebook *notebook_ = nullptr;
    Gtk::Box *bottom_ = nullptr;
    Gtk::StackSwitcher *bottom_switcher_ = nullptr;
    Gtk::Stack *bottom_stack_ = nullptr;
    Gtk::SearchEntry *log_search_ = nullptr;
    Gtk::TreeView *log_tree_ = nullptr;
    CandleChart *chart_ = nullptr;
    Gtk::Label *status_ = nullptr;
    Gtk::Revealer *overflow_ = nullptr;
    Gtk::Label *overflow_label_ = nullptr;
    Gtk::Revealer *toast_ = nullptr;
    Gtk::Label *toast_label_ = nullptr;
    Gtk::Box *job_box_ = nullptr;
    Gtk::Label *job_label_ = nullptr;
    Gtk::ProgressBar *job_progress_ = nullptr;
    Gtk::Button *job_cancel_ = nullptr;
    std::array<Gtk::Label *, 5> count_labels_{};
    std::vector<Gtk::ToggleButton *> filter_buttons_;
    FileColumns file_columns_;
    LogColumns log_columns_;
    Glib::RefPtr<Gtk::ListStore> log_model_;
    std::vector<std::unique_ptr<Tab>> tabs_;
    nav::Bookmarks bookmarks_;
    std::unique_ptr<mon::Watcher> watcher_;
    std::unordered_set<std::string> deleted_roots_;
    job::JobQueue queue_;
    dash::Dash dash_;
    sigc::connection watcher_events_;
    sigc::connection tick_timer_;
    sigc::connection metrics_timer_;
    sigc::connection toast_timer_;
    std::int64_t last_tick_ = 0;
    sigc::connection watcher_state_;
    sigc::connection queue_state_;
    sigc::connection queue_progress_;
    std::deque<LogRow> log_rows_;
    LogFilter log_filter_ = LogFilter::ALL;
    std::vector<std::string> clipboard_;
    bool clipboard_cut_ = false;
    std::optional<job::JobId> active_job_;
    std::map<job::JobId, Glib::ustring> file_jobs_;
    bool job_pulse_ = false;
    std::unordered_map<job::JobId, std::function<void(job::JobState)>> editor_jobs_;
    std::vector<std::unique_ptr<Gtk::Window>> chart_windows_;
    std::vector<CandleChart *> detached_charts_;
};

core::Result<std::unique_ptr<MainWindow>>
MainWindow::create(const Glib::RefPtr<Gtk::Application> &app) {
    try {
        return std::make_unique<MainWindow>(app);
    } catch (const Glib::Error &error) {
        return core::error_from_glib(error);
    } catch (const std::exception &error) {
        return core::make_error(core::ErrorCode::FAILED, error.what());
    }
}

MainWindow::MainWindow(const Glib::RefPtr<Gtk::Application> &app)
    : Gtk::ApplicationWindow(app), impl_(std::make_unique<Impl>(*this)) {}

MainWindow::~MainWindow() { shutdown(); }

void MainWindow::shutdown() {
    if (impl_) {
        impl_->shutdown();
    }
}

} // namespace filescope::ui
