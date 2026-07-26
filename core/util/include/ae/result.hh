#pragma once

#include <cassert>
#include <optional>
#include <string>
#include <utility>

namespace ae {

// Error categories shared across engine modules. `code` carries the
// module-specific raw value (e.g. a CURLcode or an HTTP status).
enum class ErrorKind {
    Io,
    Network,
    Http,      // transport succeeded but status indicates failure
    Parse,
    Tagging,
    Canceled,
    Other,
};

struct Error {
    ErrorKind kind = ErrorKind::Other;
    int code = 0;
    std::string message;
};

// Minimal C++17 stand-in for std::expected<T, Error>.
template <typename T>
class Result {
public:
    Result(T value) : value_(std::move(value)) {}
    Result(Error error) : error_(std::move(error)) {}

    bool ok() const { return value_.has_value(); }
    explicit operator bool() const { return ok(); }

    T &value() {
        assert(ok());
        return *value_;
    }
    const T &value() const {
        assert(ok());
        return *value_;
    }
    T take() {
        assert(ok());
        return std::move(*value_);
    }

    const Error &error() const {
        assert(!ok());
        return *error_;
    }

private:
    std::optional<T> value_;
    std::optional<Error> error_;
};

template <>
class Result<void> {
public:
    Result() = default;
    Result(Error error) : error_(std::move(error)) {}

    bool ok() const { return !error_.has_value(); }
    explicit operator bool() const { return ok(); }

    const Error &error() const {
        assert(!ok());
        return *error_;
    }

private:
    std::optional<Error> error_;
};

} // namespace ae
