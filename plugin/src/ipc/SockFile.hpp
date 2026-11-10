#pragma once

#include <unistd.h>

#include <filesystem>
#include <utility>

namespace hyprcast {

class CSockFile {
public:
    explicit CSockFile(std::filesystem::path sockPath)
        : m_sockPath(std::move(sockPath)) {}
    ~CSockFile() noexcept {
        if (m_bound) {
            ::unlink(m_sockPath.c_str());
        }
    }

    CSockFile(const CSockFile& other) = delete;
    CSockFile& operator=(const CSockFile& other) = delete;
    CSockFile(CSockFile&& other) = delete;
    CSockFile& operator=(CSockFile&& other) = delete;

    void markBound() noexcept { m_bound = true; }

private:
    std::filesystem::path m_sockPath;
    bool m_bound = false;
};

} // namespace hyprcast
