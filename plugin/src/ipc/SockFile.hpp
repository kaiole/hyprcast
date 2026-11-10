#pragma once

#include <unistd.h>

#include <filesystem>
#include <utility>

namespace hyprcast {

class sock_file {
public:
    explicit sock_file(std::filesystem::path sock_path)
        : sock_path_(std::move(sock_path)) {}
    ~sock_file() noexcept {
        if (bound_) {
            ::unlink(sock_path_.c_str());
        }
    }

    sock_file(const sock_file& other) = delete;
    sock_file& operator=(const sock_file& other) = delete;
    sock_file(sock_file&& other) = delete;
    sock_file& operator=(sock_file&& other) = delete;

    void mark_bound() noexcept { bound_ = true; }

private:
    std::filesystem::path sock_path_;
    bool bound_ = false;
};

} // namespace hyprcast
