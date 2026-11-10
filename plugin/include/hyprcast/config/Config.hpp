#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct lua_State;

namespace hyprcast {

enum class keyboard_filter_setting : std::uint8_t {
    include,
    exclude,
};

struct config {
    keyboard_filter_setting filter_setting = keyboard_filter_setting::exclude;
    std::vector<std::string> filtered_keyboards;
    bool operator==(const config&) const = default;
};

class config_state {
public:
    [[nodiscard]] const config& accepted() const noexcept { return accepted_; }
    void begin_reload() noexcept;
    void finish_reload(bool successful) noexcept;
    int configure(lua_State* lua) noexcept;

private:
    config accepted_, pending_;
    bool evaluating_ = false;
    bool called_ = false;
    bool failed_ = false;
};

} // namespace hyprcast
