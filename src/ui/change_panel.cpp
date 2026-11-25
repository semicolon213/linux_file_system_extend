#include "ui/change_panel.hpp"

#include "core/path.hpp"
#include "diff/diff.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <numeric>
#include <optional>
#include <sstream>
#include <utility>

#include <giomm/contenttype.h>
#include <gtkmm.h>

namespace filescope::ui {
namespace {

using namespace presentation;

struct Unref {
    void operator()(void *object) const noexcept { g_object_unref(object); }
};

using Pixbuf = std::unique_ptr<GdkPixbuf, Unref>;

struct TextRun {
    diff::LineOp op;
    bool heading;
    std::string text;
};

struct Comparison {
    diff::ContentKind kind = diff::ContentKind::BINARY;
    std::string summary;
    std::vector<TextRun> text;
    Pixbuf before;
    Pixbuf after;
    diff::ImageDiff image;
};

// The stored byte limit alone cannot bound the memory needed to decode a PNG.
void check_image_size(GdkPixbufLoader *loader, int width, int height, gboolean *oversized) {
    constexpr std::int64_t kMaxPixels = 16LL * 1024 * 1024;
    if (width <= 0 || height <= 0 || static_cast<std::int64_t>(width) * height > kMaxPixels) {
        *oversized = TRUE;
        gdk_pixbuf_loader_set_size(loader, 1, 1);
    }
}

Pixbuf decode_image(const std::string &bytes) {
    const std::unique_ptr<GdkPixbufLoader, Unref> loader(gdk_pixbuf_loader_new());
    gboolean oversized = FALSE;
    g_signal_connect(loader.get(), "size-prepared", G_CALLBACK(check_image_size), &oversized);
    const auto *data = reinterpret_cast<const guchar *>(bytes.data());
    const bool written = gdk_pixbuf_loader_write(loader.get(), data, bytes.size(), nullptr);
    const bool closed = gdk_pixbuf_loader_close(loader.get(), nullptr);
    if (!written || !closed || oversized) {
        return {};
    }
    auto *image = gdk_pixbuf_loader_get_pixbuf(loader.get());
    return Pixbuf(image ? gdk_pixbuf_add_alpha(image, FALSE, 0, 0, 0) : nullptr);
}

diff::ImageView image_view(const Pixbuf &image) {
    return {static_cast<std::uint32_t>(gdk_pixbuf_get_width(image.get())),
            static_cast<std::uint32_t>(gdk_pixbuf_get_height(image.get())),
            static_cast<std::size_t>(gdk_pixbuf_get_rowstride(image.get())),
            gdk_pixbuf_read_pixels(image.get())};
}

std::string dimensions(const Pixbuf &image) {
    const auto view = image_view(image);
    return std::to_string(view.width) + " × " + std::to_string(view.height);
}

void add_text_run(Comparison &comparison, diff::LineOp op, bool heading, std::string line) {
    // Batch adjacent lines without delaying the next main-loop dispatch.
    if (comparison.text.empty() || comparison.text.back().op != op ||
        comparison.text.back().heading != heading ||
        comparison.text.back().text.size() + line.size() > 4096) {
        comparison.text.push_back({op, heading, std::move(line)});
    } else {
        comparison.text.back().text += line;
    }
}

std::string numbered_line(const diff::Line &line) {
    std::ostringstream text;
    const char *symbol = line.op == diff::LineOp::ADD      ? "+"
                         : line.op == diff::LineOp::DELETE ? "−"
                                                           : " ";
    text << std::setw(5) << (line.old_number ? std::to_string(line.old_number) : "") << ' '
         << std::setw(5) << (line.new_number ? std::to_string(line.new_number) : "") << ' '
         << symbol << ' ' << line.text << '\n';
    return text.str();
}

Comparison compare_text(const std::string &before, const std::string &after, bool first,
                        GCancellable *cancel) {
    Comparison comparison;
    comparison.kind = diff::ContentKind::TEXT;
    // A first version has no edit-distance limit: each line is an insertion.
    if (first) {
        std::uint32_t number = 0;
        for (std::size_t start = 0; start < after.size();) {
            if (g_cancellable_is_cancelled(cancel)) {
                return {};
            }
            const auto end = after.find('\n', start);
            add_text_run(
                comparison, diff::LineOp::ADD, false,
                numbered_line({diff::LineOp::ADD, 0, ++number, after.substr(start, end - start)}));
            start = end == std::string::npos ? after.size() : end + 1;
        }
        comparison.summary = "새 파일 · 이전 버전 없음 · +" + std::to_string(number) + " 줄 추가";
        if (!after.empty() && after.back() != '\n') {
            add_text_run(comparison, diff::LineOp::CONTEXT, true,
                         "현재 버전의 마지막 줄바꿈 없음\n");
        }
        return comparison;
    }
    const auto result = diff::diff_lines(before, after);
    if (!result.ok()) {
        comparison.summary = "줄 Diff를 표시할 수 없습니다 · " + result.error().message;
        return comparison;
    }
    const auto &diff = result.value();
    comparison.summary = "+" + std::to_string(diff.added) + " 줄 추가    −" +
                         std::to_string(diff.deleted) + " 줄 삭제";
    if (diff.too_complex) {
        comparison.summary += " · 변경 범위가 커서 줄 Diff를 생략했습니다";
    } else if (diff.hunks.empty()) {
        comparison.summary += " · 내용이 같습니다";
    }
    for (const auto &hunk : diff.hunks) {
        add_text_run(comparison, diff::LineOp::CONTEXT, true,
                     "@@ -" + std::to_string(hunk.old_start) + "," +
                         std::to_string(hunk.old_count) + " +" + std::to_string(hunk.new_start) +
                         "," + std::to_string(hunk.new_count) + " @@\n");
        for (const auto &line : hunk.lines) {
            if (g_cancellable_is_cancelled(cancel)) {
                return {};
            }
            add_text_run(comparison, line.op, false, numbered_line(line));
        }
    }
    if (diff.old_missing_newline || diff.new_missing_newline) {
        add_text_run(comparison, diff::LineOp::CONTEXT, true,
                     std::string(diff.old_missing_newline && diff.new_missing_newline ? "두 버전의"
                                 : diff.old_missing_newline ? "이전 버전의"
                                                            : "현재 버전의") +
                         " 마지막 줄바꿈 없음\n");
    }
    return comparison;
}

Comparison compare_content(const std::optional<std::string> &old_bytes, const std::string &after,
                           GCancellable *cancel) {
    const std::string empty;
    const auto &before = old_bytes ? *old_bytes : empty;
    const auto kind = diff::classify(after);
    if (kind == diff::ContentKind::TEXT && diff::classify(before) == kind) {
        return compare_text(before, after, !old_bytes, cancel);
    }
    Comparison comparison;
    if (kind == diff::ContentKind::IMAGE && (!old_bytes || diff::classify(before) == kind)) {
        comparison.kind = kind;
        comparison.before = old_bytes ? decode_image(before) : Pixbuf{};
        if (!g_cancellable_is_cancelled(cancel)) {
            comparison.after = decode_image(after);
        }
        if (!comparison.after || (old_bytes && !comparison.before)) {
            comparison.summary = "이미지를 읽을 수 없거나 해상도 한도를 초과했습니다";
        } else if (comparison.before) {
            diff::ImageOptions options;
            options.max_regions = std::numeric_limits<std::uint32_t>::max();
            auto result = diff::compare_images(image_view(comparison.before),
                                               image_view(comparison.after), options);
            if (result.ok()) {
                comparison.image = std::move(result).value();
            }
        }
    }
    return comparison;
}

class ImageComparison final : public Gtk::DrawingArea {
  public:
    ImageComparison(Comparison comparison, Gtk::Scale &slider)
        : comparison_(std::move(comparison)), slider_(slider) {
        set_size_request(-1, 180);
        get_accessible()->set_name("이미지 비교");
        get_accessible()->set_description("바뀐 영역 점선 상자 " +
                                          std::to_string(comparison_.image.regions.size()) + "개");
        slider_.signal_value_changed().connect(sigc::mem_fun(*this, &ImageComparison::queue_draw));
    }

    void set_overlay(bool overlay) {
        overlay_ = overlay;
        slider_.set_visible(overlay);
        queue_draw();
    }

  private:
    static void paint(const Cairo::RefPtr<Cairo::Context> &context, const Pixbuf &image,
                      std::pair<double, double> position, double scale) {
        context->save();
        context->translate(position.first, position.second);
        context->scale(scale, scale);
        gdk_cairo_set_source_pixbuf(context->cobj(), image.get(), 0, 0);
        context->paint();
        context->restore();
    }

    bool on_draw(const Cairo::RefPtr<Cairo::Context> &context) override {
        const auto after = image_view(comparison_.after);
        const auto before = comparison_.before ? image_view(comparison_.before) : after;
        constexpr double kGap = 8.0;
        const double width = get_allocated_width();
        const double height = get_allocated_height();
        const bool pair = comparison_.before && !overlay_;
        const double box = pair ? (width - kGap) / 2.0 : width;
        const double scale = std::min(box / std::max(before.width, after.width),
                                      height / std::max(before.height, after.height));
        const double right = pair ? box + kGap : 0.0;
        const double x = right + (box - after.width * scale) / 2.0;
        const double y = (height - after.height * scale) / 2.0;
        paint(context, comparison_.after, {x, y}, scale);
        if (comparison_.before) {
            const double left = (box - before.width * scale) / 2.0;
            const double top = (height - before.height * scale) / 2.0;
            if (overlay_) {
                const double boundary = width * slider_.get_value() / 100.0;
                context->save();
                context->rectangle(0, 0, boundary, height);
                context->clip();
                paint(context, comparison_.before, {left, top}, scale);
                context->restore();
                context->set_source_rgb(230.0 / 255, 233.0 / 255, 238.0 / 255);
                context->set_line_width(2.0);
                context->move_to(boundary, 0);
                context->line_to(boundary, height);
                context->stroke();
            } else {
                paint(context, comparison_.before, {left, top}, scale);
                context->set_line_width(1.0);
                context->set_source_rgba(248.0 / 255, 81.0 / 255, 73.0 / 255, 0.5);
                context->rectangle(left, top, before.width * scale, before.height * scale);
                context->stroke();
                context->set_source_rgba(63.0 / 255, 185.0 / 255, 80.0 / 255, 0.5);
                context->rectangle(x, y, after.width * scale, after.height * scale);
                context->stroke();
            }
        }
        context->set_source_rgb(29.0 / 255, 39.0 / 255, 51.0 / 255);
        context->set_line_width(1.0);
        context->set_dash(std::vector<double>{4.0, 3.0}, 0);
        for (const auto &rect : comparison_.image.regions) {
            context->rectangle(x + rect.x * scale, y + rect.y * scale, rect.width * scale,
                               rect.height * scale);
        }
        context->stroke();
        return true;
    }

    Comparison comparison_;
    Gtk::Scale &slider_;
    bool overlay_ = false;
};

Gtk::Label &append_label(Gtk::Box &parent, const std::string &text, const char *style = "") {
    auto *label = Gtk::make_managed<Gtk::Label>(text);
    label->set_xalign(0.0F);
    label->set_line_wrap(true);
    label->set_line_wrap_mode(Pango::WRAP_WORD_CHAR);
    label->set_max_width_chars(38);
    if (*style != '\0') {
        label->get_style_context()->add_class(style);
    }
    parent.pack_start(*label, false, false);
    label->show();
    return *label;
}

Gtk::Button &append_button(Gtk::Box &parent, const std::string &text,
                           const sigc::slot<void> &action) {
    auto *button = Gtk::make_managed<Gtk::Button>(text);
    button->get_accessible()->set_name(text);
    button->signal_clicked().connect(action);
    parent.pack_start(*button, false, false);
    button->show();
    return *button;
}

void clear(Gtk::Box &box) {
    for (auto *child : box.get_children()) {
        gtk_widget_destroy(child->gobj());
    }
}

struct Rows : Gtk::TreeModel::ColumnRecord {
    Rows() {
        add(name);
        add(time);
        add(size);
        add(delta);
        add(index);
    }
    Gtk::TreeModelColumn<Glib::ustring> name, time, size, delta;
    Gtk::TreeModelColumn<std::size_t> index;
};

std::string version_name(const his::Version &version) {
    return "v" + std::to_string(version.number);
}

std::string capture_time(std::int64_t time) {
    return Glib::DateTime::create_now_local(time / G_USEC_PER_SEC).format("%Y-%m-%d %H:%M:%S");
}

std::string deletion_error(const core::Error &error) {
    if (error.code == core::ErrorCode::EXISTS) {
        return "같은 이름의 파일이 이미 있습니다";
    }
    return error.message;
}

} // namespace

class ChangePanel::Impl : public sigc::trackable {
  public:
    Impl(Gtk::Box &owner, Notify notify, FolderEvents folder_events)
        : owner_(owner), notify_(std::move(notify)), folder_events_(std::move(folder_events)) {
        owner_.get_accessible()->set_name("변경 내용");
        owner_.get_style_context()->add_class("fs-change-panel");

        head_ = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 4);
        head_->get_style_context()->add_class("fs-change-head");
        owner_.pack_start(*head_, false, false);
        auto *scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
        scroll->set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
        body_ = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 8);
        body_->get_style_context()->add_class("fs-change-body");
        scroll->add(*body_);
        owner_.pack_start(*scroll, true, true);
        refresh();
    }

    void set_history(his::History *history, const std::string &reason) {
        history_ = history;
        reason_ = reason;
        refresh();
    }

    void set_target(Target target) {
        if (target.path == target_.path && target.deleted == target_.deleted &&
            target.directory == target_.directory &&
            target.observed_at_us <= target_.observed_at_us) {
            return;
        }
        target_ = std::move(target);
        selected_ = 0;
        show_versions_ = false;
        refresh();
    }

    void set_enabled(bool enabled) {
        enabled_ = enabled;
        owner_.set_visible(enabled_);
        refresh();
    }

    void update_events(const std::vector<core::EventPtr> &events) {
        if (!enabled_ || !history_ || target_.path.empty()) {
            return;
        }
        if (target_.directory) {
            if (!show_versions_ && std::any_of(events.begin(), events.end(), [this](const auto &e) {
                    return core::is_ancestor(target_.path, e->path) ||
                           (!e->old_path.empty() && core::is_ancestor(target_.path, e->old_path));
                })) {
                draw_folder();
            }
            return;
        }
        if (std::any_of(events.begin(), events.end(), [](const auto &event) {
                return event->kind == core::EventKind::OVERFLOW;
            })) {
            refresh();
            return;
        }
        for (auto it = events.rbegin(); it != events.rend(); ++it) {
            const auto &event = **it;
            if (event.path == target_.path || event.old_path == target_.path) {
                target_.event = event_label(event.kind);
                target_.observed_at_us = event.observed_at_us;
                if (event.kind == core::EventKind::DELETE ||
                    event.kind == core::EventKind::MOVED_OUT || event.old_path == target_.path ||
                    event.kind == core::EventKind::CREATE ||
                    event.kind == core::EventKind::MOVED_IN) {
                    refresh();
                }
                return;
            }
        }
    }

    void version_added(const his::Version &version) {
        if (enabled_ && !target_.directory && version.path == target_.path) {
            refresh();
        }
    }

    bool cancel_confirmation() {
        if (!confirmation_) {
            return false;
        }
        gtk_widget_destroy(GTK_WIDGET(confirmation_->gobj()));
        confirmation_ = nullptr;
        if (restore_) {
            restore_->grab_focus();
        }
        return true;
    }

    void shutdown() {
        life_.reset();
        cancel();
    }

  private:
    struct Input {
        std::optional<std::string> before;
        std::string after;
    };

    struct Request {
        std::weak_ptr<int> life;
        Impl *owner = nullptr;
        std::uint64_t generation = 0;
        Input input;
        std::optional<Comparison> comparison;
        std::atomic<bool> submitted{false};
        std::atomic<bool> completed{false};
    };

    void cancel() {
        ++generation_;
        if (cancellable_) {
            cancellable_->cancel();
            cancellable_.reset();
        }
        queued_.reset();
        text_idle_.disconnect();
        text_runs_.clear();
        text_buffer_.reset();
    }

    void reset_body() {
        cancel();
        confirmation_ = nullptr;
        restore_ = nullptr;
        clear(*head_);
        clear(*body_);
        if (!target_.path.empty()) {
            append_label(*head_, display_path(Glib::path_get_basename(target_.path)),
                         "fs-empty-title");
            append_label(*head_, display_path(target_.path), "fs-change-path");
        }
    }

    void refresh() {
        reset_body();
        if (!enabled_ || !life_) {
            return;
        }
        if (!history_ || history_->state() == his::State::DISABLED) {
            append_label(*body_, "보관된 버전 없음", "fs-empty-title");
            append_label(*body_,
                         reason_ + "\n이력 폴더에 접근할 수 있는지 확인한 뒤 다시 시작하세요.",
                         "fs-empty-copy");
            return;
        }
        if (target_.path.empty()) {
            append_label(*body_, "변경 내용을 볼 파일이나 폴더를 선택하세요");
            return;
        }
        if (target_.directory) {
            draw_folder();
            return;
        }
        append_label(*body_, "불러오는 중…");
        history_->load_history_async(target_.path,
                                     sigc::bind(sigc::mem_fun(*this, &Impl::loaded), generation_));
    }

    void loaded(core::Result<his::FileHistory> result, std::uint64_t generation) {
        if (generation != generation_ || !life_) {
            return;
        }
        clear(*body_);
        if (!result.ok()) {
            append_label(*body_, "이력을 불러올 수 없습니다 · " + result.error().message);
            return;
        }
        file_ = std::move(result).value();
        const bool deleted = target_.deleted || file_.deleted_at_us.has_value();
        if (deleted) {
            const auto time = file_.deleted_at_us.value_or(target_.observed_at_us);
            append_label(*head_, "✕ 삭제" + (time > 0 ? " · " + capture_time(time) : ""));
        }
        if (file_.versions.empty()) {
            append_label(*body_, deleted ? "복구할 버전이 없습니다" : "보관된 버전 없음",
                         "fs-empty-title");
            append_label(*body_, "이 파일에서 보관한 내용이 없습니다");
            show_gap();
            return;
        }
        const auto &latest = file_.versions.front();
        const auto *before = previous_version();
        const bool deleted_facts = deleted && selected_ == 0;
        std::string meta;
        if (!deleted) {
            meta = (target_.event.empty() ? "◦ 기록" : target_.event) + " · ";
        }
        if (before && !deleted_facts) {
            meta += version_name(*before) + " → " + version_name(latest) + " · " +
                    capture_time(before->captured_at_us);
        } else {
            meta += version_name(latest);
        }
        append_label(*head_, meta, "fs-change-meta");
        auto *buttons = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 6);
        head_->pack_start(*buttons, false, false);
        buttons->show();
        if (deleted) {
            append_button(*buttons, version_name(latest) + " 복원",
                          sigc::bind(sigc::mem_fun(*this, &Impl::perform), latest.id, true));
        } else if (before && before->id != latest.id && !show_versions_) {
            restore_ = &append_button(*buttons, version_name(*before) + "으로 되돌리기",
                                      sigc::bind(sigc::mem_fun(*this, &Impl::confirm), *before));
        }
        if (!show_versions_) {
            append_button(*buttons, "전체 이력", [this] {
                show_versions_ = true;
                refresh();
            });
        }
        show_gap();
        if (show_versions_) {
            draw_versions();
        } else if (deleted_facts) {
            append_label(*body_, "보관된 파일", "fs-empty-title");
            draw_facts(latest, nullptr, true);
        } else {
            progress_ = &append_label(*body_, "비교하는 중…");
            cancellable_ = Gio::Cancellable::create();
            history_->read_content_async(
                latest.id, sigc::bind(sigc::mem_fun(*this, &Impl::read_after), generation_,
                                      before ? before->id : his::VersionId{0}));
        }
    }

    void show_gap() {
        if (file_.gap_after_latest) {
            append_label(*body_, "최신 버전 뒤에 감시하지 못한 구간이 있습니다");
        }
    }

    void read_after(core::Result<std::string> result, std::uint64_t generation, his::VersionId id) {
        if (generation != generation_ || !life_) {
            return;
        }
        if (!result.ok()) {
            progress_->set_text("현재 버전을 읽을 수 없습니다 · " + result.error().message);
        } else if (id != 0) {
            history_->read_content_async(id, sigc::bind(sigc::mem_fun(*this, &Impl::read_before),
                                                        generation, std::move(result).value()));
        } else {
            start_comparison({std::nullopt, std::move(result).value()});
        }
    }

    void read_before(core::Result<std::string> result, std::uint64_t generation,
                     std::string after) {
        if (generation != generation_ || !life_) {
            return;
        }
        if (!result.ok()) {
            progress_->set_text("이전 버전을 읽을 수 없습니다 · " + result.error().message);
        } else {
            start_comparison({std::move(result).value(), std::move(after)});
        }
    }

    void start_comparison(Input input) {
        // Keep only the latest waiting comparison while the current diff releases its buffers.
        if (comparing_) {
            queued_ = std::move(input);
            return;
        }
        auto request = std::make_unique<Request>();
        request->life = life_;
        request->owner = this;
        request->generation = generation_;
        request->input = std::move(input);
        const std::unique_ptr<GTask, Unref> task(
            g_task_new(nullptr, cancellable_->gobj(), comparison_ready, nullptr));
        request->submitted.store(true, std::memory_order_release);
        g_task_set_task_data(task.get(), request.release(), [](void *data) {
            const std::unique_ptr<Request> owned(static_cast<Request *>(data));
        });
        comparing_ = true;
        g_task_run_in_thread(task.get(), compute_comparison);
    }

    static void compute_comparison(GTask *task, gpointer, gpointer data, GCancellable *cancel) {
        auto &request = *static_cast<Request *>(data);
        g_assert_true(request.submitted.load(std::memory_order_acquire));
        try {
            if (!g_cancellable_is_cancelled(cancel)) {
                request.comparison.emplace(
                    compare_content(request.input.before, request.input.after, cancel));
            }
        } catch (const std::exception &error) {
            g_debug("FileScope comparison failed: %s", error.what());
        }
        const bool success = request.comparison.has_value();
        request.completed.store(true, std::memory_order_release);
        g_task_return_boolean(task, success);
    }

    static void comparison_ready(GObject *, GAsyncResult *result, gpointer) {
        auto *task = G_TASK(result);
        auto &request = *static_cast<Request *>(g_task_get_task_data(task));
        g_assert_true(request.completed.load(std::memory_order_acquire));
        const bool success = g_task_propagate_boolean(task, nullptr);
        if (request.life.expired()) {
            return;
        }
        auto &owner = *request.owner;
        owner.comparing_ = false;
        try {
            if (owner.generation_ == request.generation) {
                if (success && request.comparison) {
                    owner.render_comparison(std::move(*request.comparison));
                } else {
                    owner.progress_->set_text("버전을 비교할 수 없습니다");
                }
            }
            if (owner.queued_) {
                auto next = std::move(*owner.queued_);
                owner.queued_.reset();
                owner.start_comparison(std::move(next));
            }
        } catch (const std::exception &error) {
            g_warning("FileScope could not display the comparison: %s", error.what());
        }
    }

    const his::Version *previous_version() const {
        const auto selected = std::find_if(file_.versions.begin(), file_.versions.end(),
                                           [this](const auto &v) { return v.id == selected_; });
        if (selected != file_.versions.end()) {
            return &*selected;
        }
        return file_.versions.size() > 1 ? &file_.versions[1] : nullptr;
    }

    void render_comparison(Comparison comparison) {
        const auto &latest = file_.versions.front();
        const auto *before = previous_version();
        const std::string first = before ? "" : "새 파일 · 이전 버전 없음 · ";
        switch (comparison.kind) {
        case diff::ContentKind::TEXT:
            progress_->set_text(comparison.summary);
            if (!comparison.text.empty()) {
                draw_text(std::move(comparison.text));
            }
            break;
        case diff::ContentKind::IMAGE:
            if (!comparison.summary.empty()) {
                progress_->set_text(first + comparison.summary);
                break;
            }
            progress_->set_text(
                first + (comparison.before ? dimensions(comparison.before) + " → " : "") +
                dimensions(comparison.after) + "\n" +
                (before ? format_bytes(before->size) + " → " : "") + format_bytes(latest.size) +
                " · 바뀐 영역 " + std::to_string(comparison.image.region_count) + "곳");
            draw_images(std::move(comparison), latest, before);
            break;
        case diff::ContentKind::BINARY:
            progress_->set_text(first + "바이너리 변경");
            draw_facts(latest, before, false);
            break;
        }
    }

    void draw_text(std::vector<TextRun> runs) {
        auto *scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
        scroll->get_style_context()->add_class("fs-change-diff");
        scroll->set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
        scroll->set_min_content_height(180);
        auto *view = Gtk::make_managed<Gtk::TextView>();
        view->set_editable(false);
        view->set_cursor_visible(false);
        view->set_monospace(true);
        view->set_wrap_mode(Gtk::WRAP_CHAR);
        view->get_accessible()->set_name("통합 줄 Diff");
        text_buffer_ = view->get_buffer();
        add_tag_ = text_buffer_->create_tag("추가");
        add_tag_->property_paragraph_background_rgba() = Gdk::RGBA("rgba(63,185,80,0.14)");
        add_tag_->property_foreground() = "#b6f0bf";
        delete_tag_ = text_buffer_->create_tag("삭제");
        delete_tag_->property_paragraph_background_rgba() = Gdk::RGBA("rgba(248,81,73,0.14)");
        delete_tag_->property_foreground() = "#ffc2bd";
        heading_tag_ = text_buffer_->create_tag("덩어리");
        heading_tag_->property_paragraph_background() = "#18202a";
        heading_tag_->property_foreground() = "#8a94a3";
        scroll->add(*view);
        body_->pack_start(*scroll, true, true);
        scroll->show_all();
        text_runs_ = std::move(runs);
        next_text_ = 0;
        text_idle_ = Glib::signal_idle().connect(sigc::mem_fun(*this, &Impl::insert_text));
    }

    bool insert_text() {
        const auto limit = std::min(text_runs_.size(), next_text_ + 8);
        while (next_text_ < limit) {
            const auto &run = text_runs_[next_text_++];
            const auto tag = run.heading                      ? heading_tag_
                             : run.op == diff::LineOp::ADD    ? add_tag_
                             : run.op == diff::LineOp::DELETE ? delete_tag_
                                                              : Glib::RefPtr<Gtk::TextTag>{};
            if (tag) {
                text_buffer_->insert_with_tag(text_buffer_->end(), run.text, tag);
            } else {
                text_buffer_->insert(text_buffer_->end(), run.text);
            }
        }
        if (next_text_ < text_runs_.size()) {
            return true;
        }
        text_runs_.clear();
        return false;
    }

    void draw_images(Comparison comparison, const his::Version &latest,
                     const his::Version *before) {
        auto *modes = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL);
        modes->get_style_context()->add_class("linked");
        auto *pair = Gtk::make_managed<Gtk::RadioButton>("나란히");
        auto *overlay = Gtk::make_managed<Gtk::RadioButton>("겹쳐 밀기");
        auto group = pair->get_group();
        overlay->set_group(group);
        pair->set_mode(false);
        overlay->set_mode(false);
        pair->get_accessible()->set_name("나란히");
        overlay->get_accessible()->set_name("겹쳐 밀기");
        modes->pack_start(*pair, false, false);
        modes->pack_start(*overlay, false, false);
        body_->pack_start(*modes, false, false);
        auto *slider = Gtk::make_managed<Gtk::Scale>(Gtk::ORIENTATION_HORIZONTAL);
        slider->set_range(0, 100);
        slider->set_increments(1, 10);
        slider->set_value(50);
        slider->set_draw_value(false);
        slider->get_accessible()->set_name("겹쳐 밀기 위치");
        slider->set_no_show_all(true);
        auto *image = Gtk::make_managed<ImageComparison>(std::move(comparison), *slider);
        body_->pack_start(*image, false, false);
        body_->pack_start(*slider, false, false);
        image->show();
        if (before) {
            pair->set_active(true);
            overlay->signal_toggled().connect(
                [overlay, image] { image->set_overlay(overlay->get_active()); });
            modes->show_all();
            auto *captions = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 8);
            captions->set_homogeneous(true);
            append_label(*captions, version_name(*before) + " · " + format_bytes(before->size),
                         "fs-data")
                .set_xalign(0.5F);
            append_label(*captions, version_name(latest) + " · " + format_bytes(latest.size),
                         "fs-data")
                .set_xalign(0.5F);
            body_->pack_start(*captions, false, false);
            captions->show();
        }
    }

    void draw_facts(const his::Version &latest, const his::Version *before, bool deleted) {
        auto *facts = Gtk::make_managed<Gtk::Grid>();
        facts->set_column_spacing(16);
        facts->set_row_spacing(4);
        body_->pack_start(*facts, false, false);
        const auto delta = static_cast<std::int64_t>(latest.size) -
                           (before ? static_cast<std::int64_t>(before->size) : 0);
        bool uncertain = false;
        const auto type = Gio::content_type_guess(target_.path, nullptr, 0, uncertain);
        const std::pair<const char *, std::string> entries[] = {
            {"종류", uncertain ? "파일" : Gio::content_type_get_description(type).raw()},
            {"마지막 크기",
             format_bytes(latest.size) + (deleted ? "" : " · " + format_signed_delta(delta))},
            {"SHA-256", latest.sha256},
            {"보관 버전", std::to_string(file_.versions.size()) + "개 · " + version_name(latest)},
        };
        int row = 0;
        for (const auto &[key, value] : entries) {
            auto *name = Gtk::make_managed<Gtk::Label>(key);
            name->set_xalign(0.0F);
            auto *text = Gtk::make_managed<Gtk::Label>(value);
            text->set_xalign(0.0F);
            text->set_line_wrap(true);
            text->set_line_wrap_mode(Pango::WRAP_CHAR);
            text->set_max_width_chars(28);
            text->set_selectable(true);
            text->get_style_context()->add_class("fs-data");
            facts->attach(*name, 0, row, 1, 1);
            facts->attach(*text, 1, row++, 1, 1);
        }
        facts->show_all();
    }

    Gtk::TreeView &append_table(const char *name, const Glib::RefPtr<Gtk::ListStore> &model) {
        auto *view = Gtk::make_managed<Gtk::TreeView>(model);
        view->get_accessible()->set_name(name);
        auto *scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
        scroll->set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
        scroll->set_min_content_height(160);
        scroll->add(*view);
        body_->pack_start(*scroll, true, true);
        return *view;
    }

    void draw_versions() {
        append_button(*body_, "← 변경 내용으로", [this] {
            show_versions_ = false;
            refresh();
        });
        const auto bytes =
            std::accumulate(file_.versions.begin(), file_.versions.end(), std::uint64_t{0},
                            [](auto size, const auto &version) { return size + version.size; });
        append_label(*body_,
                     "전체 이력 · 보관 " + std::to_string(file_.versions.size()) + "개 · " +
                         format_bytes(bytes),
                     "fs-empty-title");
        const auto model = Gtk::ListStore::create(rows_);
        auto &view = append_table("전체 이력 버전 목록", model);
        view.append_column("버전", rows_.name);
        view.append_column("시각", rows_.time);
        view.append_column("크기", rows_.size);
        view.append_column("변동", rows_.delta);
        for (const int index : {2, 3}) {
            auto *cell =
                static_cast<Gtk::CellRendererText *>(view.get_column(index)->get_first_cell());
            cell->property_xalign() = 1.0F;
        }
        for (std::size_t i = 0; i < file_.versions.size(); ++i) {
            const auto &version = file_.versions[i];
            const auto row = *model->append();
            row[rows_.name] = version_name(version) + (i == 0 ? " 현재" : "");
            const char *origin = version.origin == his::Origin::BASELINE      ? "◦"
                                 : version.origin == his::Origin::PRE_RESTORE ? "↶"
                                                                              : "▲";
            row[rows_.time] = std::string(origin) + " " + capture_time(version.captured_at_us);
            row[rows_.size] = format_bytes(version.size);
            row[rows_.delta] =
                i + 1 < file_.versions.size()
                    ? format_signed_delta(static_cast<std::int64_t>(version.size) -
                                          static_cast<std::int64_t>(file_.versions[i + 1].size))
                    : "—";
            row[rows_.index] = i;
        }
        view.signal_row_activated().connect([this, model](const auto &path, auto *) {
            const auto row = model->get_iter(path);
            if (row) {
                selected_ = file_.versions[(*row)[rows_.index]].id;
                show_versions_ = false;
                refresh();
            }
        });
        view.get_parent()->show_all();
        append_label(*body_, "보관 한도에 도달하면 오래된 버전부터 정리합니다", "fs-change-meta");
        view.grab_focus();
    }

    void draw_folder() {
        clear(*body_);
        const auto events = folder_events_(target_.path);
        if (events.empty()) {
            append_label(*body_, "이 세션에서 하위 변경 없음");
            return;
        }
        const auto model = Gtk::ListStore::create(rows_);
        auto &view = append_table("폴더 하위 변경", model);
        view.append_column("시각", rows_.time);
        view.append_column("이벤트", rows_.size);
        view.append_column("경로", rows_.name);
        view.append_column("값", rows_.delta);
        for (std::size_t i = 0; i < events.size(); ++i) {
            const auto &event = *events[i];
            const auto row = *model->append();
            row[rows_.time] = format_time(event.observed_at_us, true);
            row[rows_.size] = event_label(event.kind);
            row[rows_.name] = display_path(event.path);
            row[rows_.delta] = format_signed_delta(event.size_delta.value_or(0));
            row[rows_.index] = i;
        }
        view.signal_row_activated().connect([this, model, events](const auto &path, auto *) {
            const auto row = model->get_iter(path);
            if (row) {
                const auto &event = *events[(*row)[rows_.index]];
                if (event.path != target_.path) {
                    set_target({event.path, event.is_dir,
                                event.kind == core::EventKind::DELETE ||
                                    event.kind == core::EventKind::MOVED_OUT,
                                event_label(event.kind), event.observed_at_us});
                }
            }
        });
        view.get_parent()->show_all();
    }

    void confirm(const his::Version &version) {
        if (confirmation_ || pending_write_) {
            return;
        }
        confirmation_ = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 6);
        confirmation_->get_style_context()->add_class("fs-change-confirm");
        body_->pack_start(*confirmation_, false, false);
        body_->reorder_child(*confirmation_, 0);
        append_label(*confirmation_, version_name(version) + "으로 되돌릴까요?");
        append_label(*confirmation_, "지금 버전도 이력에 남습니다");
        auto *buttons = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 6);
        confirmation_->pack_start(*buttons, false, false);
        auto &cancel = append_button(*buttons, "취소", [this] { cancel_confirmation(); });
        append_button(*buttons, "되돌리기 확정",
                      sigc::bind(sigc::mem_fun(*this, &Impl::perform), version.id, false));
        confirmation_->show_all();
        cancel.grab_focus();
    }

    void perform(his::VersionId id, bool recover) {
        if (pending_write_ || !life_ || !history_) {
            return;
        }
        cancel_confirmation();
        pending_write_ = true;
        auto done = sigc::bind(sigc::mem_fun(*this, &Impl::write_done), target_.path, recover);
        if (recover) {
            history_->recover_async(id, done);
        } else {
            history_->restore_async(id, fs::Confirmation::CONFIRMED, done);
        }
    }

    void write_done(const core::Result<fs::FileStamp> &result, const std::string &path,
                    bool recover) {
        pending_write_ = false;
        if (!life_) {
            return;
        }
        if (!result.ok()) {
            notify_("⚠ " + std::string(recover ? "복원 실패 · " : "되돌리기 실패 · ") +
                        deletion_error(result.error()),
                    false);
            return;
        }
        notify_(recover ? "파일을 복원했습니다" : "파일을 되돌렸습니다", true);
        if (target_.path == path) {
            target_.deleted = false;
            target_.event = "↶ 복원";
            selected_ = 0;
            show_versions_ = false;
            refresh();
        }
    }

    Gtk::Box &owner_;
    Notify notify_;
    FolderEvents folder_events_;
    his::History *history_ = nullptr;
    Target target_;
    his::FileHistory file_;
    std::string reason_;
    Gtk::Box *head_ = nullptr;
    Gtk::Box *body_ = nullptr;
    Gtk::Box *confirmation_ = nullptr;
    Gtk::Button *restore_ = nullptr;
    Gtk::Label *progress_ = nullptr;
    Glib::RefPtr<Gio::Cancellable> cancellable_;
    std::optional<Input> queued_;
    std::uint64_t generation_ = 0;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
    Glib::RefPtr<Gtk::TextBuffer> text_buffer_;
    Glib::RefPtr<Gtk::TextTag> add_tag_, delete_tag_, heading_tag_;
    sigc::connection text_idle_;
    std::vector<TextRun> text_runs_;
    std::size_t next_text_ = 0;
    Rows rows_;
    his::VersionId selected_ = 0;
    bool show_versions_ = false;
    bool enabled_ = true;
    bool comparing_ = false;
    bool pending_write_ = false;
};

ChangePanel::ChangePanel(Notify notify, FolderEvents folder_events)
    : Gtk::Box(Gtk::ORIENTATION_VERTICAL),
      impl_(std::make_unique<Impl>(*this, std::move(notify), std::move(folder_events))) {
    set_no_show_all(true);
    for (auto *child : get_children()) {
        child->show_all();
    }
}

ChangePanel::~ChangePanel() { shutdown(); }

void ChangePanel::set_history(his::History *history, const std::string &reason) {
    impl_->set_history(history, reason);
}

void ChangePanel::set_target(Target target) { impl_->set_target(std::move(target)); }

void ChangePanel::set_enabled(bool enabled) { impl_->set_enabled(enabled); }

void ChangePanel::update_events(const std::vector<core::EventPtr> &events) {
    impl_->update_events(events);
}

void ChangePanel::version_added(const his::Version &version) { impl_->version_added(version); }

bool ChangePanel::cancel_confirmation() { return impl_->cancel_confirmation(); }

void ChangePanel::shutdown() { impl_->shutdown(); }

} // namespace filescope::ui
