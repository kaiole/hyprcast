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

// What the C function registered as hyprcast.configure does. Lua calls that
// as a plain function with no object to call it on, so the caller keeps a
// pointer to the state that is in use and passes it here; when none is,
// `state` is null and the call is a Lua error like any other failure.
int call_configure(lua_State* lua, config_state* state) noexcept;

} // namespace hyprcast
