#include "his/his_internal.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <filesystem>
#include <limits>
#include <string_view>
#include <thread>
#include <unordered_map>

#include <fcntl.h>
#include <glib.h>
#include <glibmm/checksum.h>
#include <sqlite3.h>
#include <sys/stat.h>
#include <unistd.h>

#include "core/fd.hpp"
#include "core/path.hpp"

namespace filescope::his {

namespace testing {
enum class FaultPoint : std::uint8_t { AFTER_BLOB_RENAME, AFTER_COMMIT_BEFORE_UNLINK };
using Rows = std::vector<std::vector<std::optional<std::string>>>;
} // namespace testing

namespace {

namespace disk = std::filesystem;
using core::ErrorCode;
using core::UniqueFd;

constexpr char schema_v1[] = R"sql(
CREATE TABLE sessions(id INTEGER PRIMARY KEY, started_at_us INTEGER NOT NULL, ended_at_us INTEGER);
CREATE TABLE events(
  id INTEGER PRIMARY KEY, session_id INTEGER NOT NULL REFERENCES sessions(id),
  seq INTEGER NOT NULL, observed_at_us INTEGER NOT NULL, kind INTEGER NOT NULL,
  path BLOB NOT NULL, old_path BLOB, is_dir INTEGER NOT NULL, raw_mask INTEGER NOT NULL,
  merged_count INTEGER NOT NULL, gap_id INTEGER NOT NULL DEFAULT 0, size_delta INTEGER
);
CREATE INDEX events_time ON events(observed_at_us);
CREATE INDEX events_path ON events(path, observed_at_us);
CREATE TABLE blobs(
  sha256 BLOB PRIMARY KEY, size INTEGER NOT NULL, ref_count INTEGER NOT NULL CHECK(ref_count >= 0),
  created_at_us INTEGER NOT NULL
) WITHOUT ROWID;
CREATE TABLE files(
  path BLOB PRIMARY KEY, last_number INTEGER NOT NULL, deleted_at_us INTEGER
) WITHOUT ROWID;
CREATE INDEX files_deleted ON files(deleted_at_us) WHERE deleted_at_us IS NOT NULL;
CREATE TABLE versions(
  id INTEGER PRIMARY KEY, path BLOB NOT NULL REFERENCES files(path), number INTEGER NOT NULL,
  captured_at_us INTEGER NOT NULL, origin INTEGER NOT NULL, size INTEGER NOT NULL,
  mtime_ns INTEGER NOT NULL, mode INTEGER NOT NULL, sha256 BLOB NOT NULL REFERENCES blobs(sha256),
  UNIQUE(path, number)
);
CREATE INDEX versions_path ON versions(path, number DESC);
CREATE INDEX versions_age ON versions(captured_at_us);
CREATE TABLE gaps(
  id INTEGER PRIMARY KEY, kind INTEGER NOT NULL, started_at_us INTEGER NOT NULL, ended_at_us INTEGER,
  root BLOB
);
PRAGMA user_version=1;
)sql";

constexpr std::array migrations{schema_v1};
constexpr std::uint64_t event_trim_interval = 10000;
constexpr std::int64_t us_per_day = 24LL * 60 * 60 * G_USEC_PER_SEC;
constexpr std::int64_t ns_per_second = 1000000000;
enum class Gap : std::uint8_t { OVERFLOW, PAUSE, NOT_RUNNING, HIS_BACKLOG };

struct SqlError {
    int code;
    std::string message;

    bool busy() const { return code == SQLITE_BUSY || code == SQLITE_LOCKED; }
    bool corrupt() const { return code == SQLITE_CORRUPT || code == SQLITE_NOTADB; }
    core::Error error() const {
        switch (code) {
        case SQLITE_FULL:
            return core::make_error(ErrorCode::NO_SPACE, message);
        case SQLITE_CANTOPEN:
        case SQLITE_IOERR:
        case SQLITE_CORRUPT:
        case SQLITE_NOTADB:
        case SQLITE_BUSY:
        case SQLITE_LOCKED:
            return core::make_error(ErrorCode::IO, message);
        case SQLITE_PERM:
        case SQLITE_READONLY:
            return core::make_error(ErrorCode::PERMISSION_DENIED, message);
        default:
            return core::make_error(ErrorCode::FAILED, message);
        }
    }
};

void check_sql(sqlite3 *db, int code) {
    if (code != SQLITE_OK) {
        throw SqlError{code & 0xff, db ? sqlite3_errmsg(db) : sqlite3_errstr(code)};
    }
}

struct CloseDb {
    void operator()(sqlite3 *db) const { sqlite3_close(db); }
};
struct Finalize {
    void operator()(sqlite3_stmt *stmt) const { sqlite3_finalize(stmt); }
};
using Db = std::unique_ptr<sqlite3, CloseDb>;
using Prepared = std::unique_ptr<sqlite3_stmt, Finalize>;

Db connect(const std::string &path, int flags) {
    sqlite3 *raw = nullptr;
    const int result = sqlite3_open_v2(path.c_str(), &raw, flags | SQLITE_OPEN_NOFOLLOW, nullptr);
    Db db(raw);
    check_sql(db.get(), result);
    return db;
}

void exec(sqlite3 *db, const char *sql) {
    check_sql(db, sqlite3_exec(db, sql, nullptr, nullptr, nullptr));
}

class Statement {
  public:
    explicit Statement(sqlite3_stmt *stmt) : stmt_(stmt) {}
    Statement(const Statement &) = delete;
    Statement &operator=(const Statement &) = delete;
    ~Statement() {
        sqlite3_reset(stmt_);
        sqlite3_clear_bindings(stmt_);
    }

    void bind(int index, std::int64_t value) { check(sqlite3_bind_int64(stmt_, index, value)); }
    void bind(int index, const std::string &value) {
        check(sqlite3_bind_blob64(stmt_, index, value.data(), value.size(), SQLITE_TRANSIENT));
    }
    void bind_null(int index) { check(sqlite3_bind_null(stmt_, index)); }
    bool row() {
        const int code = sqlite3_step(stmt_);
        if (code == SQLITE_ROW) {
            return true;
        }
        if (code != SQLITE_DONE) {
            check(code);
        }
        return false;
    }
    void run() {
        while (row()) {
        }
    }
    bool is_null(int index) const { return sqlite3_column_type(stmt_, index) == SQLITE_NULL; }
    std::int64_t integer(int index) const { return sqlite3_column_int64(stmt_, index); }
    std::string bytes(int index) const {
        const auto *data = static_cast<const char *>(sqlite3_column_blob(stmt_, index));
        return data ? std::string(data, sqlite3_column_bytes(stmt_, index)) : std::string();
    }

  private:
    void check(int code) const { check_sql(sqlite3_db_handle(stmt_), code); }
    sqlite3_stmt *stmt_;
};

[[noreturn]] void file_error(const char *op, const std::string &path) {
    throw core::error_from_errno(errno, op, path);
}

void secure_directory(const std::string &path, bool parents = false) {
    const int result =
        parents ? g_mkdir_with_parents(path.c_str(), 0700) : mkdir(path.c_str(), 0700);
    if (result != 0 && errno != EEXIST) {
        file_error("mkdir", path);
    }
    const UniqueFd fd(::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    struct stat info {};
    if (!fd || fstat(fd.get(), &info) != 0) {
        file_error("open history directory", path);
    }
    if (info.st_uid != geteuid()) {
        throw core::make_error(ErrorCode::PERMISSION_DENIED, "History directory owner differs");
    }
    if ((info.st_mode & 07777) != 0700 && fchmod(fd.get(), 0700) != 0) {
        file_error("chmod", path);
    }
}

bool check_db_file(const std::string &path) {
    struct stat info {};
    if (lstat(path.c_str(), &info) != 0) {
        if (errno == ENOENT) {
            return false;
        }
        file_error("lstat", path);
    }
    if (!S_ISREG(info.st_mode) || info.st_uid != geteuid() || info.st_nlink != 1) {
        throw core::make_error(ErrorCode::PERMISSION_DENIED, "Unsafe history database file");
    }
    return true;
}

std::string hex_digest(const std::string &digest) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string hex;
    hex.reserve(digest.size() * 2);
    for (const unsigned char byte : digest) {
        hex.push_back(digits[byte >> 4]);
        hex.push_back(digits[byte & 15]);
    }
    return hex;
}

bool hex_name(std::string_view name, std::size_t size) {
    return name.size() == size && name.find_first_not_of("0123456789abcdefABCDEF") == name.npos;
}

std::pair<std::string, std::string> prefix_range(const std::string &dir) {
    auto prefix = core::canonicalize(dir);
    if (prefix != "/") {
        prefix += '/';
    }
    auto end = prefix;
    end.back() = '0';
    return {std::move(prefix), std::move(end)};
}

void unlink_file(const std::string &path) {
    if (unlink(path.c_str()) != 0 && errno != ENOENT) {
        file_error("unlink", path);
    }
}

struct TempFile {
    std::string path;
    UniqueFd fd;

    explicit TempFile(std::string pattern) : path(std::move(pattern)) {
        fd.reset(mkostemp(path.data(), O_CLOEXEC));
        if (!fd) {
            file_error("create capture temporary file", path);
        }
        if (fchmod(fd.get(), 0600) != 0) {
            const int saved_errno = errno;
            unlink(path.c_str());
            errno = saved_errno;
            file_error("chmod temporary file", path);
        }
    }
    ~TempFile() {
        if (unlink(path.c_str()) != 0 && errno != ENOENT) {
            g_message("History could not remove capture temporary file: %s", g_strerror(errno));
        }
    }
};

void sync_fd(int fd, const std::string &path) {
    if (fsync(fd) != 0) {
        file_error("fsync", path);
    }
}

void sync_directory(const std::string &path) {
    const UniqueFd fd(::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    if (!fd) {
        file_error("open directory", path);
    }
    sync_fd(fd.get(), path);
}

ssize_t read_chunk(int fd, char *buffer, std::size_t size, const std::string &path) {
    ssize_t count = 0;
    do {
        count = read(fd, buffer, size);
    } while (count < 0 && errno == EINTR);
    if (count < 0) {
        file_error("read", path);
    }
    return count;
}

void write_chunk(const TempFile &temp, const char *buffer, std::size_t size) {
    while (size != 0) {
        const auto count = write(temp.fd.get(), buffer, size);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            if (count == 0) {
                errno = EIO;
            }
            file_error("write", temp.path);
        }
        buffer += count;
        size -= count;
    }
}

core::Error no_space() {
    return core::make_error(ErrorCode::NO_SPACE, "Latest history versions exceed the store limit");
}

} // namespace

namespace internal {

class Store::Impl {
  public:
    explicit Impl(Options options)
        : options_(std::move(options)), db_path_(options_.data_dir + "/history.db"),
          blobs_(options_.data_dir + "/blobs"), tmp_(options_.data_dir + "/tmp") {}

    template <typename T, typename F> core::Result<T> result(F action) {
        try {
            return action();
        } catch (const SqlError &error) {
            corrupt_ = corrupt_ || error.corrupt();
            return error.error();
        } catch (const core::Error &error) {
            return error;
        } catch (const disk::filesystem_error &error) {
            return core::error_from_errno(error.code().value(), "history filesystem",
                                          error.path1().string());
        } catch (const std::exception &error) {
            return core::make_error(ErrorCode::IO, error.what());
        } catch (...) {
            return core::make_error(ErrorCode::IO, "Unexpected history storage failure");
        }
    }

    bool open() {
        secure_directory(options_.data_dir, true);
        bool reset = false;
        try {
            open_database();
        } catch (const SqlError &error) {
            if (!error.corrupt()) {
                throw;
            }
            statements_.clear();
            db_.reset();
            const auto quarantine = db_path_ + ".corrupt-" + std::to_string(g_get_real_time());
            disk::rename(db_path_, quarantine);
            for (const auto *suffix : {"-wal", "-shm"}) {
                if (disk::exists(disk::symlink_status(db_path_ + suffix))) {
                    disk::rename(db_path_ + suffix, quarantine + suffix);
                }
            }
            open_database();
            reset = true;
        }
        secure_directory(tmp_);
        secure_directory(blobs_);
        return reset;
    }

    void recover(const std::function<void()> &between_steps) {
        for (const auto &entry : disk::directory_iterator(tmp_)) {
            if (!disk::is_directory(entry.symlink_status())) {
                unlink_file(entry.path().string());
            } else {
                g_message("History left an unexpected directory in tmp");
            }
        }
        between_steps();
        if (session_ == 0) {
            start_session();
        }
        between_steps();
        recover_blobs();
        between_steps();
        std::vector<std::string> removed;
        bool over_limit = false;
        transaction([&] {
            removed.clear();
            exec(db_.get(), "UPDATE blobs SET ref_count=(SELECT count(*) FROM versions v "
                            "WHERE v.sha256=blobs.sha256) WHERE ref_count!=(SELECT count(*) "
                            "FROM versions v WHERE v.sha256=blobs.sha256)");
            over_limit = trim_versions();
            collect_unused(removed);
            trim_events();
        });
        over_limit_ = over_limit;
        event_rows_ = count_events();
        unlink_blobs(removed);
        if (over_limit_) {
            throw no_space();
        }
    }

    void record(const std::vector<core::EventPtr> &events, const std::vector<Backlog> &drops) {
        std::uint64_t written = 0;
        bool trimmed = false;
        transaction([&] {
            written = 0;
            for (const auto &event : events) {
                if (event && !owned_path(event->path)) {
                    record_event(*event);
                    ++written;
                }
            }
            for (const auto &drop : drops) {
                add_gap(Gap::HIS_BACKLOG, drop.first, drop.last, {});
            }
            trimmed = event_rows_ + written > options_.limits.max_event_rows ||
                      events_since_trim_ + written >= event_trim_interval;
            if (trimmed) {
                trim_events();
            }
        });
        if (trimmed) {
            event_rows_ = count_events();
            events_since_trim_ = 0;
        } else {
            event_rows_ += written;
            events_since_trim_ += written;
        }
    }

    Capture capture(const std::string &path, Origin origin, const std::function<void()> &after_copy,
                    bool defer_retry, std::optional<std::uint64_t> baseline_bytes) {
        const auto valid = core::validate_absolute(path);
        if (!valid) {
            throw valid.error();
        }
        if (is_excluded(path, options_.data_dir)) {
            if (origin == Origin::PRE_RESTORE) {
                throw core::make_error(ErrorCode::NOT_SUPPORTED, "Path is excluded from history");
            }
            return {};
        }
        if (over_limit_) {
            throw no_space();
        }

        auto capture = capture_once(path, origin, after_copy, baseline_bytes);
        if (capture.changed && !defer_retry) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(options_.limits.capture_delay_ms));
            capture = capture_once(path, origin, after_copy, baseline_bytes);
        }
        return capture;
    }

    void start_scan() {
        transaction([&] {
            auto close = sql("UPDATE gaps SET ended_at_us=max(started_at_us,?1) "
                             "WHERE kind=2 AND ended_at_us IS NULL");
            close.bind(1, g_get_real_time());
            close.run();
        });
    }

    void note_pause(bool paused, std::int64_t at) {
        transaction([&] {
            if (paused) {
                auto open = sql("SELECT 1 FROM gaps WHERE kind=1 AND ended_at_us IS NULL");
                if (!open.row()) {
                    add_gap(Gap::PAUSE, at, std::nullopt, {});
                }
            } else {
                auto close = sql("UPDATE gaps SET ended_at_us=max(started_at_us,?1) "
                                 "WHERE kind=1 AND ended_at_us IS NULL");
                close.bind(1, at);
                close.run();
            }
        });
    }

    core::Result<fs::FileStamp> restore(VersionId id, fs::Confirmation confirmation,
                                        const std::function<void(Version)> &added) {
        if (confirmation != fs::Confirmation::CONFIRMED) {
            return core::make_error(ErrorCode::NOT_CONFIRMED,
                                    "History restore requires confirmation");
        }
        const auto source = version_source(id);
        const auto safe = core::check_protected(source.path, core::PathOp::WRITE);
        if (!safe) {
            return safe.error();
        }
        // PRE_RESTORE can prune the selected version, so keep the blob linked through install.
        const TempFile pinned(tmp_ + "/restore-XXXXXX");
        pin_blob(source.blob, pinned);
        auto saved = capture(source.path, Origin::PRE_RESTORE, {}, false, std::nullopt);
        if (!saved.stamp) {
            return core::make_error(ErrorCode::CONFLICT, "File changed during history capture");
        }
        if (saved.version) {
            added(std::move(*saved.version));
        }
        return fs::install_file(source.path, pinned.path, saved.stamp, source.mode);
    }

    core::Result<fs::FileStamp> recover_file(VersionId id) {
        const auto source = version_source(id);
        const TempFile pinned(tmp_ + "/recover-XXXXXX");
        pin_blob(source.blob, pinned);
        fs::FileStamp stamp;
        bool installed = false;
        transaction([&] {
            clear_deleted(source.path);
            // A retried COMMIT must keep the file from the first successful install.
            if (!installed) {
                const auto result =
                    fs::install_file(source.path, pinned.path, std::nullopt, source.mode);
                if (!result) {
                    throw result.error();
                }
                stamp = result.value();
                installed = true;
            }
        });
        return stamp;
    }

    FileHistory load(const std::string &path, bool latest_only = false) {
        const auto valid = core::validate_absolute(path);
        if (!valid) {
            throw valid.error();
        }
        FileHistory history;
        history.path = path;
        {
            auto statement = sql("SELECT deleted_at_us FROM files WHERE path=?1");
            statement.bind(1, path);
            if (statement.row() && !statement.is_null(0)) {
                history.deleted_at_us = statement.integer(0);
            }
        }
        {
            auto statement =
                sql("SELECT id, number, captured_at_us, origin, size, mtime_ns, mode, sha256 "
                    "FROM versions WHERE path=?1 ORDER BY number DESC LIMIT ?2");
            statement.bind(1, path);
            statement.bind(2, latest_only ? 1 : -1);
            while (statement.row()) {
                history.versions.push_back(
                    {statement.integer(0), path, static_cast<std::uint32_t>(statement.integer(1)),
                     statement.integer(2), static_cast<Origin>(statement.integer(3)),
                     static_cast<std::uint64_t>(statement.integer(4)), statement.integer(5),
                     static_cast<std::uint32_t>(statement.integer(6)),
                     hex_digest(statement.bytes(7))});
            }
        }
        if (!history.versions.empty()) {
            auto statement =
                sql("SELECT 1 FROM gaps WHERE (started_at_us>?1 OR ended_at_us IS NULL "
                    "OR ended_at_us>?1) AND (root IS NULL OR root=?2 OR "
                    "substr(?2,1,length(root)+1)=CAST(root || '/' AS BLOB) OR "
                    "root=X'2f') LIMIT 1");
            statement.bind(1, history.versions.front().captured_at_us);
            statement.bind(2, path);
            history.gap_after_latest = statement.row();
        }
        return history;
    }

    std::vector<FileHistory> deleted(const std::string &dir) {
        const auto valid = core::validate_absolute(dir);
        if (!valid) {
            throw valid.error();
        }
        const auto [start, end] = prefix_range(dir);
        auto statement = sql("SELECT path FROM files WHERE path>=?1 AND path<?2 AND "
                             "deleted_at_us IS NOT NULL AND EXISTS(SELECT 1 FROM versions v "
                             "WHERE v.path=files.path) ORDER BY path");
        statement.bind(1, start);
        statement.bind(2, end);
        std::vector<FileHistory> histories;
        while (statement.row()) {
            histories.push_back(load(statement.bytes(0), true));
        }
        return histories;
    }

    struct VersionSource {
        std::string path;
        std::string blob;
        std::uint32_t mode;
    };

    VersionSource version_source(VersionId id) {
        auto statement = sql("SELECT path,sha256,mode FROM versions WHERE id=?1");
        statement.bind(1, id);
        if (!statement.row()) {
            throw core::make_error(ErrorCode::NOT_FOUND, "History version not found");
        }
        const auto digest = statement.bytes(1);
        if (digest.size() != 32) {
            throw core::make_error(ErrorCode::IO, "Invalid history blob digest");
        }
        return {statement.bytes(0), hex_digest(digest),
                static_cast<std::uint32_t>(statement.integer(2))};
    }

    UniqueFd open_shard(const std::string &hex) const {
        const auto path = blobs_ + "/" + hex.substr(0, 2);
        UniqueFd shard(::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
        if (!shard) {
            file_error("open blob directory", path);
        }
        return shard;
    }

    void pin_blob(const std::string &hex, const TempFile &pinned) const {
        const auto shard = open_shard(hex);
        unlink_file(pinned.path);
        if (linkat(shard.get(), hex.c_str(), AT_FDCWD, pinned.path.c_str(), 0) != 0) {
            file_error("pin history blob", pinned.path);
        }
    }

    std::string content(VersionId id) {
        const auto hex = version_source(id).blob;
        const auto path = blob_path(hex);
        const auto shard = open_shard(hex);
        const UniqueFd fd(
            openat(shard.get(), hex.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK));
        if (!fd) {
            file_error("open blob", path);
        }
        struct stat info {};
        if (fstat(fd.get(), &info) != 0) {
            file_error("fstat blob", path);
        }
        if (!S_ISREG(info.st_mode)) {
            throw core::make_error(ErrorCode::IO, "History blob is not a regular file");
        }
        std::string bytes;
        std::array<char, 65536> buffer{};
        while (const auto count = read_chunk(fd.get(), buffer.data(), buffer.size(), path)) {
            std::uint64_t total = 0;
            if (__builtin_add_overflow(bytes.size(), count, &total) || total > max_content_bytes) {
                throw core::make_error(ErrorCode::TOO_LARGE, "History content exceeds 8 MiB");
            }
            bytes.append(buffer.data(), count);
        }
        return bytes;
    }

    void close() {
        if (db_ && session_ != 0 && !corrupt_) {
            transaction([&] {
                auto statement = sql("UPDATE sessions SET ended_at_us=?1 WHERE id=?2");
                statement.bind(1, g_get_real_time());
                statement.bind(2, session_);
                statement.run();
            });
            check_sql(db_.get(),
                      sqlite3_wal_checkpoint_v2(db_.get(), nullptr, SQLITE_CHECKPOINT_TRUNCATE,
                                                nullptr, nullptr));
        }
    }

    void release_database() {
        statements_.clear();
        db_.reset();
    }

    bool corrupt_ = false;
    bool over_limit_ = false;
    std::optional<testing::FaultPoint> fault_;

  private:
    Statement sql(const char *text) {
        auto &cached = statements_[text];
        if (!cached) {
            sqlite3_stmt *raw = nullptr;
            const int result =
                sqlite3_prepare_v3(db_.get(), text, -1, SQLITE_PREPARE_PERSISTENT, &raw, nullptr);
            cached.reset(raw);
            check_sql(db_.get(), result);
        }
        return Statement(cached.get());
    }

    template <typename F> void transaction(F run) {
        for (int attempt = 0;; ++attempt) {
            try {
                exec(db_.get(), "BEGIN IMMEDIATE");
                run();
                exec(db_.get(), "COMMIT");
                return;
            } catch (const SqlError &error) {
                rollback();
                if (!error.busy() || attempt == 3) {
                    throw;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100 << attempt));
            } catch (...) {
                rollback();
                throw;
            }
        }
    }

    void rollback() {
        if (!sqlite3_get_autocommit(db_.get())) {
            exec(db_.get(), "ROLLBACK");
        }
    }

    void open_database() {
        UniqueFd created(::open(db_path_.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0600));
        if (!created && errno != EEXIST) {
            file_error("create database", db_path_);
        }
        if (created && fchmod(created.get(), 0600) != 0) {
            file_error("chmod database", db_path_);
        }
        // A separate descriptor closing after SQLite opens can release its process locks.
        created.reset();
        const std::array paths{db_path_, db_path_ + "-wal", db_path_ + "-shm"};
        for (const auto &path : paths) {
            check_db_file(path);
        }
        db_ = connect(db_path_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX);
        check_sql(db_.get(), sqlite3_busy_timeout(db_.get(), 2000));
        std::size_t version = 0;
        {
            auto statement = sql("PRAGMA user_version");
            statement.row();
            version = static_cast<std::size_t>(statement.integer(0));
        }
        if (version > migrations.size()) {
            throw core::make_error(ErrorCode::NOT_SUPPORTED, "History database schema is newer");
        }
        for (const auto &path : paths) {
            if (check_db_file(path) && chmod(path.c_str(), 0600) != 0) {
                file_error("chmod database file", path);
            }
        }
        {
            auto statement = sql("PRAGMA journal_mode=WAL");
            if (!statement.row() || statement.bytes(0) != "wal") {
                throw core::make_error(ErrorCode::IO, "History database requires WAL");
            }
        }
        exec(db_.get(),
             "PRAGMA synchronous=NORMAL; PRAGMA foreign_keys=ON; PRAGMA temp_store=MEMORY");
        for (; version < migrations.size(); ++version) {
            transaction([&] { exec(db_.get(), migrations[version]); });
        }
    }

    std::string blob_path(const std::string &hex) const {
        return blobs_ + "/" + hex.substr(0, 2) + "/" + hex;
    }

    bool owned_path(const std::string &path) const {
        if (path.empty()) {
            return false;
        }
        const auto normalized = core::canonicalize(path);
        return normalized == options_.data_dir || core::is_ancestor(options_.data_dir, normalized);
    }

    void start_session() {
        transaction([&] {
            exec(db_.get(), "UPDATE sessions SET ended_at_us=COALESCE((SELECT max(observed_at_us) "
                            "FROM events WHERE session_id=sessions.id), started_at_us) "
                            "WHERE ended_at_us IS NULL");
            const auto now = g_get_real_time();
            {
                auto previous = sql("SELECT ended_at_us FROM sessions ORDER BY id DESC LIMIT 1");
                if (previous.row()) {
                    const auto end = previous.integer(0);
                    auto gaps = sql("UPDATE gaps SET ended_at_us=max(started_at_us,?1) "
                                    "WHERE kind IN (0,1) AND ended_at_us IS NULL");
                    gaps.bind(1, end);
                    gaps.run();
                    add_gap(Gap::NOT_RUNNING, end, std::nullopt, {});
                }
            }
            auto insert = sql("INSERT INTO sessions(started_at_us) VALUES(?1)");
            insert.bind(1, now);
            insert.run();
        });
        session_ = sqlite3_last_insert_rowid(db_.get());
    }

    void add_gap(Gap kind, std::int64_t start, std::optional<std::int64_t> end,
                 const std::string &root) {
        auto statement =
            sql("INSERT INTO gaps(kind,started_at_us,ended_at_us,root) VALUES(?1,?2,?3,?4)");
        statement.bind(1, static_cast<int>(kind));
        statement.bind(2, start);
        if (end) {
            statement.bind(3, *end);
        } else {
            statement.bind_null(3);
        }
        if (!root.empty()) {
            statement.bind(4, root);
        } else {
            statement.bind_null(4);
        }
        statement.run();
    }

    void record_event(const core::Event &event) {
        auto statement = sql("INSERT INTO events(session_id,seq,observed_at_us,kind,path,old_path,"
                             "is_dir,raw_mask,merged_count,gap_id,size_delta) "
                             "VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11)");
        statement.bind(1, session_);
        statement.bind(2, static_cast<std::int64_t>(event.seq));
        statement.bind(3, event.observed_at_us);
        statement.bind(4, static_cast<int>(event.kind));
        statement.bind(5, event.path);
        if (!event.old_path.empty()) {
            statement.bind(6, event.old_path);
        } else {
            statement.bind_null(6);
        }
        statement.bind(7, event.is_dir);
        statement.bind(8, event.raw_mask);
        statement.bind(9, event.merged_count);
        statement.bind(10, static_cast<std::int64_t>(event.gap_id));
        if (event.size_delta) {
            statement.bind(11, *event.size_delta);
        } else {
            statement.bind_null(11);
        }
        statement.run();
        using core::EventKind;
        switch (event.kind) {
        case EventKind::DELETE:
        case EventKind::MOVED_OUT:
            mark_deleted(event.path, event.is_dir, event.observed_at_us);
            break;
        case EventKind::RENAME:
        case EventKind::MOVE:
            mark_deleted(event.old_path, event.is_dir, event.observed_at_us);
            break;
        case EventKind::CREATE:
        case EventKind::CLOSE_WRITE:
            if (!event.is_dir) {
                clear_deleted(event.path);
            }
            break;
        case EventKind::OVERFLOW:
            add_gap(Gap::OVERFLOW, event.observed_at_us, std::nullopt, event.path);
            break;
        case EventKind::RESYNC_DONE: {
            auto close = sql("UPDATE gaps SET ended_at_us=max(started_at_us,?1) WHERE kind=0 "
                             "AND ended_at_us IS NULL AND (root IS NULL OR root=?2)");
            close.bind(1, event.observed_at_us);
            close.bind(2, event.path);
            close.run();
            break;
        }
        default:
            break;
        }
    }

    void clear_deleted(const std::string &path) {
        auto clear =
            sql("UPDATE files SET deleted_at_us=NULL WHERE path=?1 AND deleted_at_us IS NOT NULL");
        clear.bind(1, path);
        clear.run();
    }

    void mark_deleted(const std::string &path, bool directory, std::int64_t now) {
        if (path.empty() || owned_path(path)) {
            return;
        }
        if (directory) {
            const auto [start, end] = prefix_range(path);
            auto statement = sql("UPDATE files SET deleted_at_us=?1 WHERE path>=?2 AND path<?3 "
                                 "AND EXISTS(SELECT 1 FROM versions v WHERE v.path=files.path)");
            statement.bind(1, now);
            statement.bind(2, start);
            statement.bind(3, end);
            statement.run();
        } else {
            auto statement = sql("UPDATE files SET deleted_at_us=?1 WHERE path=?2 AND "
                                 "EXISTS(SELECT 1 FROM versions v WHERE v.path=files.path)");
            statement.bind(1, now);
            statement.bind(2, path);
            statement.run();
        }
    }

    std::uint64_t count_events() {
        auto statement = sql("SELECT count(*) FROM events");
        statement.row();
        return statement.integer(0);
    }

    void trim_events() {
        const auto now = g_get_real_time();
        const auto days = static_cast<std::int64_t>(options_.limits.event_retention_days);
        auto expired = sql("DELETE FROM events WHERE observed_at_us<?1");
        constexpr auto max_days = std::numeric_limits<std::int64_t>::max() / us_per_day;
        expired.bind(1, now - std::min(days, max_days) * us_per_day);
        expired.run();
        auto excess = sql("DELETE FROM events WHERE id IN (SELECT id FROM events "
                          "ORDER BY observed_at_us DESC,id DESC LIMIT -1 OFFSET ?1)");
        const auto max_rows = std::min<std::uint64_t>(options_.limits.max_event_rows,
                                                      std::numeric_limits<std::int64_t>::max());
        excess.bind(1, static_cast<std::int64_t>(max_rows));
        excess.run();
    }

    Capture capture_once(const std::string &path, Origin origin,
                         const std::function<void()> &after_copy,
                         std::optional<std::uint64_t> baseline_bytes) {
        const UniqueFd source(::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK));
        if (!source) {
            if (origin != Origin::PRE_RESTORE &&
                (errno == ENOENT || errno == ENOTDIR || errno == ELOOP || errno == EACCES)) {
                g_debug("Skipping unavailable history capture source");
                return {};
            }
            file_error("open capture source", path);
        }
        struct stat before {};
        if (fstat(source.get(), &before) != 0) {
            file_error("fstat capture source", path);
        }
        auto limit = std::min(options_.limits.max_file_bytes, max_content_bytes);
        if (baseline_bytes) {
            limit = std::min({limit, options_.limits.baseline_max_file_bytes, *baseline_bytes});
        }
        if (!S_ISREG(before.st_mode)) {
            if (origin == Origin::PRE_RESTORE) {
                throw core::make_error(ErrorCode::NOT_SUPPORTED, "Restore requires a regular file");
            }
            g_debug("Skipping non-regular history capture source");
            return {};
        }
        if (static_cast<std::uint64_t>(before.st_size) > limit) {
            if (origin == Origin::PRE_RESTORE) {
                throw core::make_error(ErrorCode::TOO_LARGE,
                                       "Restore target exceeds capture limit");
            }
            g_debug("Skipping history capture source above the size limit");
            return {};
        }

        const TempFile temp(tmp_ + "/capture-XXXXXX");
        Glib::Checksum checksum(Glib::Checksum::CHECKSUM_SHA256);
        std::array<char, 65536> buffer{};
        std::uint64_t size = 0;
        while (const auto count = read_chunk(source.get(), buffer.data(), buffer.size(), path)) {
            if (__builtin_add_overflow(size, count, &size) || size > limit) {
                return {std::nullopt, std::nullopt, true};
            }
            checksum.update(reinterpret_cast<const guchar *>(buffer.data()), count);
            write_chunk(temp, buffer.data(), count);
        }
        if (after_copy) {
            after_copy();
        }
        struct stat after {};
        if (fstat(source.get(), &after) != 0) {
            file_error("fstat capture source", path);
        }
        struct stat named {};
        if (lstat(path.c_str(), &named) != 0) {
            if (errno != ENOENT && errno != ENOTDIR) {
                file_error("lstat capture source", path);
            }
            return {std::nullopt, std::nullopt, true};
        }
        if (before.st_size != after.st_size || before.st_mtim.tv_sec != after.st_mtim.tv_sec ||
            before.st_mtim.tv_nsec != after.st_mtim.tv_nsec ||
            size != static_cast<std::uint64_t>(before.st_size) || after.st_dev != named.st_dev ||
            after.st_ino != named.st_ino || after.st_size != named.st_size ||
            after.st_mtim.tv_sec != named.st_mtim.tv_sec ||
            after.st_mtim.tv_nsec != named.st_mtim.tv_nsec) {
            return {std::nullopt, std::nullopt, true};
        }
        std::int64_t mtime = 0;
        if (__builtin_mul_overflow(before.st_mtim.tv_sec, ns_per_second, &mtime) ||
            __builtin_add_overflow(mtime, before.st_mtim.tv_nsec, &mtime)) {
            throw core::make_error(ErrorCode::IO, "Capture timestamp is outside history range");
        }
        const fs::FileStamp stamp{static_cast<std::uint64_t>(before.st_dev),
                                  static_cast<std::uint64_t>(before.st_ino), size, mtime};
        std::string digest(32, '\0');
        gsize digest_size = digest.size();
        checksum.get_digest(reinterpret_cast<guint8 *>(digest.data()), &digest_size);
        {
            auto latest =
                sql("SELECT sha256 FROM versions WHERE path=?1 ORDER BY number DESC LIMIT 1");
            latest.bind(1, path);
            if (latest.row() && latest.bytes(0) == digest) {
                clear_deleted(path);
                return {std::nullopt, stamp};
            }
        }
        const auto hex = hex_digest(digest);
        install_blob(temp, hex);
        Version version{0,      path, 0,     g_get_real_time(),
                        origin, size, mtime, static_cast<std::uint32_t>(before.st_mode),
                        hex};
        std::vector<std::string> removed;
        bool over_limit = false;
        transaction([&] {
            removed.clear();
            insert_version(version, digest);
            trim_path(path);
            over_limit = trim_store();
            collect_unused(removed);
        });
        over_limit_ = over_limit;
        if (fault_ == testing::FaultPoint::AFTER_COMMIT_BEFORE_UNLINK) {
            _exit(77);
        }
        unlink_blobs(removed);
        return {std::move(version), stamp};
    }

    void install_blob(const TempFile &temp, const std::string &hex) {
        sync_fd(temp.fd.get(), temp.path);
        const auto path = blob_path(hex);
        const auto shard = disk::path(path).parent_path().string();
        secure_directory(shard);
        const auto status = disk::symlink_status(path);
        if (disk::exists(status)) {
            if (!disk::is_regular_file(status)) {
                throw core::make_error(ErrorCode::IO, "History blob path is not a regular file");
            }
            return;
        }
        disk::rename(temp.path, path);
        sync_directory(shard);
        sync_directory(blobs_);
        if (fault_ == testing::FaultPoint::AFTER_BLOB_RENAME) {
            _exit(77);
        }
    }

    void insert_version(Version &version, const std::string &digest) {
        auto blob = sql("INSERT INTO blobs(sha256,size,ref_count,created_at_us) VALUES(?1,?2,1,?3) "
                        "ON CONFLICT(sha256) DO UPDATE SET ref_count=ref_count+1");
        blob.bind(1, digest);
        blob.bind(2, static_cast<std::int64_t>(version.size));
        blob.bind(3, version.captured_at_us);
        blob.run();

        auto file =
            sql("INSERT INTO files(path,last_number) VALUES(?1,1) ON CONFLICT(path) "
                "DO UPDATE SET last_number=last_number+1,deleted_at_us=NULL RETURNING last_number");
        file.bind(1, version.path);
        file.row();
        const auto number = file.integer(0);
        if (number > std::numeric_limits<std::uint32_t>::max()) {
            throw core::make_error(ErrorCode::NO_SPACE, "History version number limit reached");
        }
        version.number = number;
        file.run();

        auto insert =
            sql("INSERT INTO versions(path,number,captured_at_us,origin,size,mtime_ns,mode,sha256) "
                "VALUES(?1,?2,?3,?4,?5,?6,?7,?8)");
        insert.bind(1, version.path);
        insert.bind(2, version.number);
        insert.bind(3, version.captured_at_us);
        insert.bind(4, static_cast<int>(version.origin));
        insert.bind(5, static_cast<std::int64_t>(version.size));
        insert.bind(6, version.mtime_ns);
        insert.bind(7, version.mode);
        insert.bind(8, digest);
        insert.run();
        version.id = sqlite3_last_insert_rowid(db_.get());
    }

    void remove_version(VersionId id) {
        auto remove = sql("DELETE FROM versions WHERE id=?1 RETURNING sha256");
        remove.bind(1, id);
        if (remove.row()) {
            auto ref = sql("UPDATE blobs SET ref_count=ref_count-1 WHERE sha256=?1");
            ref.bind(1, remove.bytes(0));
            ref.run();
        }
        remove.run();
    }

    void trim_path(const std::string &path) {
        auto statement =
            sql("SELECT id FROM versions WHERE path=?1 ORDER BY number DESC LIMIT -1 OFFSET ?2");
        statement.bind(1, path);
        statement.bind(2, std::max(1u, options_.limits.max_versions_per_file));
        std::vector<VersionId> expired;
        while (statement.row()) {
            expired.push_back(statement.integer(0));
        }
        for (const auto id : expired) {
            remove_version(id);
        }
    }

    bool trim_versions() {
        {
            auto paths = sql("SELECT path FROM versions GROUP BY path HAVING count(*)>?1");
            paths.bind(1, std::max(1u, options_.limits.max_versions_per_file));
            std::vector<std::string> excess;
            while (paths.row()) {
                excess.push_back(paths.bytes(0));
            }
            for (const auto &path : excess) {
                trim_path(path);
            }
        }
        return trim_store();
    }

    bool trim_store() {
        auto total = sql("SELECT coalesce(sum(size),0) FROM blobs WHERE ref_count>0");
        total.row();
        auto bytes = static_cast<std::uint64_t>(total.integer(0));
        total.run();
        const auto limit = options_.limits.max_store_bytes;
        if (bytes <= limit) {
            return false;
        }
        const auto target = limit - limit / 10 - (limit % 10 != 0 ? 1 : 0);
        while (bytes > target) {
            auto oldest =
                sql("SELECT v.id,b.size,b.ref_count FROM versions v JOIN blobs b USING(sha256) "
                    "WHERE v.number < (SELECT max(x.number) FROM versions x WHERE x.path=v.path) "
                    "ORDER BY v.captured_at_us,v.id LIMIT 1");
            if (!oldest.row()) {
                break;
            }
            const auto id = oldest.integer(0);
            if (oldest.integer(2) == 1) {
                bytes -= oldest.integer(1);
            }
            oldest.run();
            remove_version(id);
        }
        return bytes > limit;
    }

    void collect_unused(std::vector<std::string> &removed) {
        auto statement = sql("DELETE FROM blobs WHERE ref_count=0 RETURNING sha256");
        while (statement.row()) {
            removed.push_back(hex_digest(statement.bytes(0)));
        }
    }

    void unlink_blobs(const std::vector<std::string> &removed) const {
        for (const auto &hex : removed) {
            if (hex_name(hex, 64)) {
                const auto path = blob_path(hex);
                const UniqueFd shard(::open(disk::path(path).parent_path().c_str(),
                                            O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
                if (!shard) {
                    if (errno != ENOENT) {
                        file_error("open blob directory", path);
                    }
                    continue;
                }
                if (unlinkat(shard.get(), hex.c_str(), 0) != 0 && errno != ENOENT) {
                    file_error("unlink blob", path);
                }
            }
        }
    }

    void recover_blobs() {
        std::unordered_map<std::string, std::string> missing;
        {
            auto statement = sql("SELECT sha256 FROM blobs");
            while (statement.row()) {
                auto digest = statement.bytes(0);
                auto hex = hex_digest(digest);
                missing.emplace(std::move(hex), std::move(digest));
            }
        }
        for (const auto &shard : disk::directory_iterator(blobs_)) {
            if (!hex_name(shard.path().filename().string(), 2) ||
                !disk::is_directory(shard.symlink_status())) {
                g_message("History left an unexpected entry in blobs");
                continue;
            }
            for (const auto &entry : disk::directory_iterator(shard.path())) {
                const auto hex = entry.path().filename().string();
                if (!hex_name(hex, 64) || !disk::is_regular_file(entry.symlink_status())) {
                    g_message("History left an unexpected entry in a blob directory");
                    continue;
                }
                if (entry.path() != blob_path(hex) || missing.erase(hex) == 0) {
                    unlink_file(entry.path().string());
                }
            }
        }
        transaction([&] {
            for (const auto &entry : missing) {
                const auto &digest = entry.second;
                auto versions = sql("DELETE FROM versions WHERE sha256=?1");
                versions.bind(1, digest);
                versions.run();
                auto blob = sql("DELETE FROM blobs WHERE sha256=?1");
                blob.bind(1, digest);
                blob.run();
                g_message("History removed references to a missing blob");
            }
        });
    }

    Options options_;
    std::string db_path_;
    std::string blobs_;
    std::string tmp_;
    Db db_;
    std::unordered_map<std::string_view, Prepared> statements_;
    std::int64_t session_ = 0;
    std::uint64_t event_rows_ = 0;
    std::uint64_t events_since_trim_ = 0;
};

Store::Store(Options options) : impl_(std::make_unique<Impl>(std::move(options))) {}
Store::~Store() = default;

core::Result<bool> Store::open() {
    return impl_->result<bool>([&] { return impl_->open(); });
}
core::Status Store::recover(const std::function<void()> &between_steps) {
    return impl_->result<void>([&]() -> core::Status {
        impl_->recover(between_steps);
        return {};
    });
}
core::Status Store::record(const std::vector<core::EventPtr> &events,
                           const std::vector<Backlog> &drops) {
    return impl_->result<void>([&]() -> core::Status {
        impl_->record(events, drops);
        return {};
    });
}
core::Result<Capture> Store::capture(const std::string &path, Origin origin,
                                     const std::function<void()> &after_copy, bool defer_retry,
                                     std::optional<std::uint64_t> baseline_bytes) {
    return impl_->result<Capture>(
        [&] { return impl_->capture(path, origin, after_copy, defer_retry, baseline_bytes); });
}
core::Status Store::start_scan() {
    return impl_->result<void>([&]() -> core::Status {
        impl_->start_scan();
        return {};
    });
}
core::Status Store::note_pause(bool paused, std::int64_t at) {
    return impl_->result<void>([&]() -> core::Status {
        impl_->note_pause(paused, at);
        return {};
    });
}
core::Result<fs::FileStamp> Store::restore(VersionId id, fs::Confirmation confirmation,
                                           const std::function<void(Version)> &added) {
    return impl_->result<fs::FileStamp>([&] { return impl_->restore(id, confirmation, added); });
}
core::Result<fs::FileStamp> Store::recover_file(VersionId id) {
    return impl_->result<fs::FileStamp>([&] { return impl_->recover_file(id); });
}
core::Result<FileHistory> Store::load(const std::string &path) {
    return impl_->result<FileHistory>([&] { return impl_->load(path); });
}
core::Result<std::vector<FileHistory>> Store::deleted(const std::string &dir) {
    return impl_->result<std::vector<FileHistory>>([&] { return impl_->deleted(dir); });
}
core::Result<std::string> Store::content(VersionId id) {
    return impl_->result<std::string>([&] { return impl_->content(id); });
}
core::Status Store::close() {
    auto result = impl_->result<void>([&]() -> core::Status {
        impl_->close();
        return {};
    });
    impl_->release_database();
    return result;
}
void Store::set_fault(testing::FaultPoint point) { impl_->fault_ = point; }

bool Store::corrupt() const { return impl_->corrupt_; }
bool Store::over_limit() const { return impl_->over_limit_; }

} // namespace internal

namespace testing {

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): fixture takes a database path, then SQL.
core::Result<Rows> query(const std::string &db_path, const std::string &sql,
                         const std::function<void()> &while_open) {
    try {
        auto db =
            connect(db_path, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX);
        check_sql(db.get(), sqlite3_busy_timeout(db.get(), 2000));
        Rows rows;
        const char *next = sql.c_str();
        while (*next != '\0') {
            sqlite3_stmt *raw = nullptr;
            const int code = sqlite3_prepare_v2(db.get(), next, -1, &raw, &next);
            const Prepared prepared(raw);
            check_sql(db.get(), code);
            if (!prepared) {
                continue;
            }
            Statement statement(prepared.get());
            while (statement.row()) {
                auto &row = rows.emplace_back();
                const int columns = sqlite3_column_count(prepared.get());
                for (int index = 0; index < columns; ++index) {
                    row.push_back(statement.is_null(index)
                                      ? std::nullopt
                                      : std::make_optional(statement.bytes(index)));
                }
            }
        }
        if (while_open) {
            while_open();
        }
        return rows;
    } catch (const SqlError &error) {
        return error.error();
    }
}

} // namespace testing
} // namespace filescope::his
