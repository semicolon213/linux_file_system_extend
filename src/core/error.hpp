#pragma once

#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include <glibmm/error.h>

namespace filescope::core {

enum class ErrorCode : std::uint8_t {
    FAILED,
    NOT_FOUND,
    PERMISSION_DENIED,
    EXISTS,
    NOT_DIRECTORY,
    IS_DIRECTORY,
    NO_SPACE,
    IO,
    CROSS_DEVICE,
    LOOP,
    NAME_TOO_LONG,
    INVALID_PATH,
    PROTECTED_PATH,
    NOT_CONFIRMED,
    NOT_SUPPORTED,
    CONFLICT,
    TOO_LARGE,
    CANCELLED,
    WATCH_LIMIT,
    TOO_MANY_OPEN,
    PARTIAL,
};

struct Error {
    ErrorCode code = ErrorCode::FAILED;
    std::string message;
};

template <typename T> class [[nodiscard]] Result {
  public:
    Result(T value) : storage_(std::move(value)) {}
    Result(Error error) : storage_(std::move(error)) {}

    bool ok() const noexcept { return std::holds_alternative<T>(storage_); }
    explicit operator bool() const noexcept { return ok(); }

    const T &value() const & {
        if (!ok()) {
            std::abort();
        }
        return std::get<T>(storage_);
    }

    T &value() & {
        if (!ok()) {
            std::abort();
        }
        return std::get<T>(storage_);
    }

    T &&value() && {
        if (!ok()) {
            std::abort();
        }
        return std::get<T>(std::move(storage_));
    }

    const Error &error() const & {
        if (ok()) {
            std::abort();
        }
        return std::get<Error>(storage_);
    }

  private:
    std::variant<T, Error> storage_;
};

template <> class [[nodiscard]] Result<void> {
  public:
    Result() noexcept = default;
    Result(Error error) : error_(std::move(error)) {}

    bool ok() const noexcept { return !error_.has_value(); }
    explicit operator bool() const noexcept { return ok(); }

    const Error &error() const & {
        if (!error_.has_value()) {
            std::abort();
        }
        return error_.value();
    }

  private:
    std::optional<Error> error_;
};

using Status = Result<void>;

// [any] Maps errno to the public error taxonomy.
ErrorCode error_code_from_errno(int errnum) noexcept;
// [any] Preserves errno text and renders path bytes safely for diagnostics.
Error error_from_errno(int errnum, std::string_view op, const std::string &path);
// [any] Maps Glib errors at a module boundary while preserving the original message.
Error error_from_glib(const Glib::Error &error);
// [any] Constructs an error value.
Error make_error(ErrorCode code, std::string message);

} // namespace filescope::core
