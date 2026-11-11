#pragma once

#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

namespace hyprcast::test_support {

// A fresh directory that is removed with everything in it when the object
// goes away. Tests keep their sockets here, never in the real runtime
// directory.
class temp_dir {
public:
    temp_dir() {
        // Short on purpose: socket paths are limited to about 107 bytes.
        std::string name =
            (std::filesystem::temp_directory_path() / "hyprcast.XXXXXX")
                .string();
        if (::mkdtemp(name.data()) == nullptr) {
            throw std::system_error(errno,
                                    std::generic_category(),
                                    "Cannot create a temporary directory");
        }
        path_ = name;
    }

    ~temp_dir() {
        std::error_code error_code;
        std::filesystem::remove_all(path_, error_code);
    }

    temp_dir(const temp_dir&) = delete;
    temp_dir& operator=(const temp_dir&) = delete;
    temp_dir(temp_dir&&) = delete;
    temp_dir& operator=(temp_dir&&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

} // namespace hyprcast::test_support
