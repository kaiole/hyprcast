#pragma once

#include <unistd.h>

#include <utility>

namespace hyprcast {

// Move-only owner of a POSIX file descriptor, in the spirit of std::unique_ptr.
// A negative descriptor means "empty".
class unique_fd {
public:
    unique_fd() noexcept = default;
    explicit unique_fd(int fd) noexcept : fd_(fd) {}
    ~unique_fd() noexcept { reset(); }

    unique_fd(const unique_fd&) = delete;
    unique_fd& operator=(const unique_fd&) = delete;
    unique_fd(unique_fd&& other) noexcept : fd_(other.release()) {}
    unique_fd& operator=(unique_fd&& other) noexcept {
        reset(other.release());
        return *this;
    }

    [[nodiscard]] int get() const noexcept { return fd_; }
    [[nodiscard]] bool valid() const noexcept { return fd_ >= 0; }
    explicit operator bool() const noexcept { return valid(); }

    // Gives up ownership without closing; the caller becomes responsible for
    // the descriptor.
    [[nodiscard]] int release() noexcept { return std::exchange(fd_, -1); }

    void reset(int fd = -1) noexcept {
        if (fd_ == fd) {
            return;
        }
        if (fd_ >= 0) {
            ::close(fd_);
        }
        fd_ = fd;
    }

private:
    int fd_ = -1;
};

} // namespace hyprcast
