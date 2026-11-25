#include "search/search.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <dirent.h>
#include <exception>
#include <fcntl.h>
#include <limits>
#include <map>
#include <mutex>
#include <new>
#include <optional>
#include <sys/stat.h>
#include <thread>
#include <utility>
#include <variant>

#include <gio/gio.h>
#include <giomm/cancellable.h>
#include <glibmm/convert.h>
#include <glibmm/dispatcher.h>
#include <glibmm/exceptionhandler.h>

#include "core/fd.hpp"
#include "core/path.hpp"

namespace filescope::search {
namespace {

using Clock = std::chrono::steady_clock;
constexpr auto kHitInterval = std::chrono::milliseconds(100);
constexpr std::size_t kBatchSize = 500;
constexpr std::size_t kDeniedLimit = 100;

struct DirectoryCloser {
    void operator()(DIR *directory) const noexcept { closedir(directory); }
};
using DirectoryPtr = std::unique_ptr<DIR, DirectoryCloser>;

core::UniqueFd open_directory(int parent, const char *name) {
    int fd = -1;
    do {
        fd = openat(parent, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    } while (fd == -1 && errno == EINTR);
    return core::UniqueFd(fd);
}

DirectoryPtr read_directory(core::UniqueFd fd) {
    DirectoryPtr directory(fdopendir(fd.get()));
    if (directory) {
        fd.release();
    }
    return directory;
}

void record_denied(Summary &summary, const std::string &path) {
    if (summary.denied.size() < kDeniedLimit) {
        summary.denied.push_back(path);
    } else {
        summary.denied_more = true;
    }
}

core::FileType file_type(mode_t mode) {
    if (S_ISREG(mode)) {
        return core::FileType::REGULAR;
    }
    if (S_ISDIR(mode)) {
        return core::FileType::DIRECTORY;
    }
    if (S_ISLNK(mode)) {
        return core::FileType::SYMLINK;
    }
    return core::FileType::OTHER;
}

std::int64_t mtime_ns(const timespec &time) {
    constexpr std::int64_t scale = 1'000'000'000;
    constexpr auto min = std::numeric_limits<std::int64_t>::min();
    constexpr auto max = std::numeric_limits<std::int64_t>::max();
    if (time.tv_sec < min / scale) {
        if (time.tv_sec == min / scale - 1 && time.tv_nsec >= scale + min % scale) {
            return min + (time.tv_nsec - (scale + min % scale));
        }
        return min;
    }
    if (time.tv_sec > max / scale || (time.tv_sec == max / scale && time.tv_nsec > max % scale)) {
        return max;
    }
    return time.tv_sec * scale + time.tv_nsec;
}

core::Entry make_entry(std::string name, const struct stat &status, dev_t root_device) {
    core::Entry entry;
    entry.name = std::move(name);
    entry.type = file_type(status.st_mode);
    entry.target_type = entry.type;
    entry.is_hidden = entry.name.front() == '.';
    entry.is_mount_point = status.st_dev != root_device;
    entry.size = static_cast<std::uint64_t>(status.st_size);
    entry.mode = static_cast<std::uint32_t>(status.st_mode);
    entry.uid = static_cast<std::uint32_t>(status.st_uid);
    entry.gid = static_cast<std::uint32_t>(status.st_gid);
    entry.dev = static_cast<std::uint64_t>(status.st_dev);
    entry.ino = static_cast<std::uint64_t>(status.st_ino);
    entry.mtime_ns = mtime_ns(status.st_mtim);
    return entry;
}

bool matches(const Query &query, const std::string &name) {
    if (query.text.empty() && query.extensions.empty()) {
        return true;
    }
    const auto folded = Glib::filename_display_name(name).casefold();
    if (!query.text.empty() && folded.raw().find(query.text.raw()) == std::string::npos) {
        return false;
    }
    if (query.extensions.empty()) {
        return true;
    }
    const auto dot = folded.raw().find_last_of('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 == folded.bytes()) {
        return false;
    }
    return std::any_of(query.extensions.begin(), query.extensions.end(), [&](const auto &ext) {
        return folded.raw().compare(dot + 1, std::string::npos, ext.raw()) == 0;
    });
}

core::Error cancelled_error() {
    return core::make_error(core::ErrorCode::CANCELLED, "search cancelled");
}

struct Request {
    SearchId id;
    Query query;
    GCancellable *cancellable;
};

struct Message {
    SearchId id;
    std::variant<std::vector<Hit>, core::Result<Summary>> value;
};

struct Directory {
    std::string path;
    DirectoryPtr stream;
};

} // namespace

class Searcher::Impl {
  public:
    Impl() {
        dispatcher_connection_ = dispatcher_.connect(sigc::mem_fun(*this, &Impl::dispatch));
        worker_ = std::thread([this] { run(); });
    }

    ~Impl() {
        cancel(active_);
        {
            const std::lock_guard<std::mutex> lock(command_mutex_);
            stopping_ = true;
        }
        {
            const std::lock_guard<std::mutex> lock(delivery_mutex_);
            delivery_stopped_ = true;
        }
        command_ready_.notify_one();
        delivery_ready_.notify_one();
        worker_.join();
        dispatcher_connection_.disconnect();
    }

    core::Result<SearchId> start(Query query) {
        if (const auto valid = core::validate_absolute(query.root); !valid) {
            return valid.error();
        }
        const auto last = query.root.find_last_not_of('/');
        query.root.resize(last == std::string::npos ? 1 : last + 1);
        struct stat status {};
        if (lstat(query.root.c_str(), &status) != 0) {
            return core::error_from_errno(errno, "stat search root", query.root);
        }
        if (!S_ISDIR(status.st_mode)) {
            return core::make_error(core::ErrorCode::NOT_DIRECTORY,
                                    "search root is not a directory");
        }

        cancel(active_);
        active_ = next_id_++;
        if (active_ == 0) {
            active_ = next_id_++;
        }
        const auto token = cancellables_.emplace(active_, Gio::Cancellable::create()).first;
        {
            const std::lock_guard<std::mutex> lock(command_mutex_);
            commands_.push_back({active_, std::move(query), token->second->gobj()});
        }
        command_ready_.notify_one();
        return active_;
    }

    bool cancel(SearchId id) {
        if (id == 0 || id != active_) {
            return false;
        }
        const auto &token = cancellables_.at(id);
        if (token->is_cancelled()) {
            return false;
        }
        token->cancel();
        return true;
    }

    sigc::signal<void(SearchId, const std::vector<Hit> &)> hits;
    sigc::signal<void(SearchId, const core::Result<Summary> &)> done;

  private:
    void run() {
        for (;;) {
            std::optional<Request> request;
            {
                std::unique_lock<std::mutex> lock(command_mutex_);
                command_ready_.wait(lock, [this] { return stopping_ || !commands_.empty(); });
                if (stopping_) {
                    return;
                }
                request.emplace(std::move(commands_.front()));
                commands_.pop_front();
            }
            publish({request->id, execute(*request)});
        }
    }

    core::Result<Summary> execute(Request &request) {
        try {
            return scan(request);
        } catch (const std::bad_alloc &) {
            throw;
        } catch (const Glib::Error &error) {
            return core::error_from_glib(error);
        } catch (const std::exception &error) {
            return core::make_error(core::ErrorCode::IO, error.what());
        } catch (...) {
            return core::make_error(core::ErrorCode::IO, "search failed");
        }
    }

    core::Result<Summary> scan(Request &request) {
        if (g_cancellable_is_cancelled(request.cancellable)) {
            return cancelled_error();
        }
        auto &query = request.query;
        query.text = query.text.casefold();
        for (auto &extension : query.extensions) {
            extension = extension.casefold();
        }

        Summary summary;
        if (query.max_results == 0) {
            summary.truncated = true;
            return summary;
        }

        auto root_fd = open_directory(AT_FDCWD, query.root.c_str());
        struct stat root_status {};
        if (!root_fd || fstat(root_fd.get(), &root_status) != 0) {
            record_denied(summary, query.root);
            return summary;
        }
        auto root = read_directory(std::move(root_fd));
        if (!root) {
            record_denied(summary, query.root);
            return summary;
        }

        std::vector<Directory> stack;
        stack.push_back({query.root, std::move(root)});
        ++summary.dirs_scanned;
        std::vector<Hit> batch;
        auto flush_at = Clock::now() + kHitInterval;
        while (!stack.empty() && !g_cancellable_is_cancelled(request.cancellable)) {
            if (!batch.empty() && (batch.size() == kBatchSize || Clock::now() >= flush_at)) {
                publish({request.id, std::exchange(batch, {})});
                flush_at = Clock::now() + kHitInterval;
                continue;
            }

            auto &directory = stack.back();
            errno = 0;
            const auto *item = readdir(directory.stream.get());
            if (item == nullptr) {
                if (errno == EINTR) {
                    continue;
                }
                if (errno != 0) {
                    record_denied(summary, directory.path);
                }
                stack.pop_back();
                continue;
            }

            std::string name(item->d_name);
            if (name == "." || name == ".." || (!query.show_hidden && name.front() == '.')) {
                continue;
            }

            const auto parent = dirfd(directory.stream.get());
            const auto path = core::path_join(directory.path, name);
            struct stat status {};
            int result = 0;
            do {
                result = fstatat(parent, name.c_str(), &status, AT_SYMLINK_NOFOLLOW);
            } while (result != 0 && errno == EINTR);
            if (result != 0) {
                if (query.recursive && item->d_type == DT_DIR) {
                    record_denied(summary, path);
                }
                continue;
            }

            if (matches(query, name)) {
                batch.push_back({path, make_entry(name, status, root_status.st_dev)});
                if (++summary.hits == query.max_results) {
                    summary.truncated = true;
                    break;
                }
            }

            if (query.recursive && S_ISDIR(status.st_mode) && status.st_dev == root_status.st_dev) {
                auto child = open_directory(parent, name.c_str());
                struct stat opened {};
                if (!child || fstat(child.get(), &opened) != 0) {
                    record_denied(summary, path);
                    continue;
                }
                if (opened.st_dev != root_status.st_dev) {
                    continue;
                }
                auto stream = read_directory(std::move(child));
                if (stream) {
                    stack.push_back({path, std::move(stream)});
                    ++summary.dirs_scanned;
                } else {
                    record_denied(summary, path);
                }
            }
        }
        if (g_cancellable_is_cancelled(request.cancellable)) {
            return cancelled_error();
        }
        if (!batch.empty()) {
            publish({request.id, std::move(batch)});
        }
        return summary;
    }

    void publish(Message message) {
        {
            std::unique_lock<std::mutex> lock(delivery_mutex_);
            delivery_ready_.wait(lock, [this] { return delivery_stopped_ || !message_; });
            if (std::holds_alternative<std::vector<Hit>>(message.value)) {
                delivery_ready_.wait_until(lock, next_hits_, [this] { return delivery_stopped_; });
            }
            if (delivery_stopped_) {
                return;
            }
            message_ = std::move(message);
        }
        dispatcher_.emit();
    }

    void dispatch() {
        std::optional<Message> message;
        {
            const std::lock_guard<std::mutex> lock(delivery_mutex_);
            if (!message_) {
                return;
            }
            message.emplace(std::move(*message_));
        }
        const bool current =
            message->id == active_ && !cancellables_.at(message->id)->is_cancelled();
        const auto *batch = std::get_if<std::vector<Hit>>(&message->value);
        try {
            if (batch != nullptr) {
                if (current) {
                    hits.emit(message->id, *batch);
                }
            } else {
                finish(*message, current);
            }
        } catch (...) {
            Glib::exception_handlers_invoke();
        }
        {
            const std::lock_guard<std::mutex> lock(delivery_mutex_);
            if (current && batch != nullptr) {
                // Pace from actual delivery so a stalled main loop cannot receive a burst.
                next_hits_ = Clock::now() + kHitInterval;
            }
            message_.reset();
        }
        delivery_ready_.notify_one();
    }

    void finish(const Message &message, bool current) {
        cancellables_.erase(message.id);
        if (message.id == active_) {
            active_ = 0;
        }
        if (current) {
            done.emit(message.id, std::get<core::Result<Summary>>(message.value));
        } else {
            done.emit(message.id, cancelled_error());
        }
    }

    SearchId next_id_ = 1;
    SearchId active_ = 0;
    // The worker borrows each token until it posts that search's completion.
    std::map<SearchId, Glib::RefPtr<Gio::Cancellable>> cancellables_;

    std::mutex command_mutex_;
    std::condition_variable command_ready_;
    std::deque<Request> commands_;
    bool stopping_ = false;

    std::mutex delivery_mutex_;
    std::condition_variable delivery_ready_;
    std::optional<Message> message_;
    Clock::time_point next_hits_{};
    bool delivery_stopped_ = false;

    Glib::Dispatcher dispatcher_;
    sigc::connection dispatcher_connection_;
    std::thread worker_;
};

Searcher::Searcher() : impl_(std::make_unique<Impl>()) {}
Searcher::~Searcher() = default;

core::Result<SearchId> Searcher::start(Query query) { return impl_->start(std::move(query)); }
bool Searcher::cancel(SearchId id) { return impl_->cancel(id); }
sigc::signal<void(SearchId, const std::vector<Hit> &)> &Searcher::signal_hits() {
    return impl_->hits;
}
sigc::signal<void(SearchId, const core::Result<Summary> &)> &Searcher::signal_done() {
    return impl_->done;
}

} // namespace filescope::search
