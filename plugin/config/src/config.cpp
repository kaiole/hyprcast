#include "hyprcast/config/config.hpp"

#include <lua.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <exception>
#include <format>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace hyprcast {
namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::invalid_argument(std::string {message});
    }
}

std::string_view string_at(lua_State* lua, int index) {
    size_t length = 0;
    const char* data = lua_tolstring(lua, index, &length);
    return {data, length};
}

void plain_table(lua_State* lua, int index) {
    require(lua_type(lua, index) == LUA_TTABLE, "expected a table");
    require(lua_getmetatable(lua, index) == 0, "metatables are not supported");
}

void read_list(lua_State* lua, std::vector<std::string>& out) {
    require(lua_type(lua, -1) == LUA_TTABLE, "keyboards must be a table");
    require(lua_getmetatable(lua, -1) == 0,
            "keyboards must not have a metatable");
    const int table = lua_absindex(lua, -1);
    const size_t length = lua_rawlen(lua, table);
    size_t count = 0;
    lua_pushnil(lua);
    while (lua_next(lua, table)) {
        require(lua_isinteger(lua, -2) != 0,
                "keyboards must be a dense array with integer keys");
        const auto key = lua_tointeger(lua, -2);
        require(key > 0 && std::cmp_less_equal(key, length),
                "keyboards must be a dense array starting at 1");
        require(lua_type(lua, -1) == LUA_TSTRING,
                std::format("keyboards[{}] must be a string", key));
        const auto name = string_at(lua, -1);
        require(!name.empty(),
                std::format("keyboards[{}] must not be empty", key));
        require(!name.contains('\0'),
                std::format("keyboards[{}] must not contain NUL bytes", key));
        ++count;
        lua_pop(lua, 1);
    }
    require(count == length, "keyboards must not contain holes");
    for (size_t i = 1; i <= length; ++i) {
        lua_rawgeti(lua, table, static_cast<lua_Integer>(i));
        const auto name = string_at(lua, -1);
        if (std::ranges::find(out, name) == out.end()) {
            out.emplace_back(name);
        }
        lua_pop(lua, 1);
    }
}

config parse(lua_State* lua) {
    require(lua_gettop(lua) == 1,
            "configure expects exactly one table argument");
    plain_table(lua, 1);
    config candidate;
    lua_pushnil(lua);
    while (lua_next(lua, 1)) {
        require(lua_type(lua, -2) == LUA_TSTRING,
                "configuration field names must be strings");
        const auto key = string_at(lua, -2);
        if (key == "filter") {
            require(lua_type(lua, -1) == LUA_TSTRING,
                    "filter must be 'include' or 'exclude'");
            const auto filter = string_at(lua, -1);
            require(filter == "include" || filter == "exclude",
                    "filter must be 'include' or 'exclude'");
            candidate.filter_setting = filter == "include"
                ? keyboard_filter_setting::include
                : keyboard_filter_setting::exclude;
        } else if (key == "keyboards") {
            read_list(lua, candidate.filtered_keyboards);
        } else {
            throw std::invalid_argument(
                std::format("unknown configuration field {:?}; expected filter "
                            "or keyboards",
                            key));
        }
        lua_pop(lua, 1);
    }
    return candidate;
}

} // namespace

void config_state::begin_reload() noexcept {
    // A nested Hyprland reload supersedes the preceding evaluation.
    pending_ = {};
    evaluating_ = true;
    called_ = failed_ = false;
}

void config_state::finish_reload(bool successful) noexcept {
    if (!evaluating_) {
        return;
    }
    if (successful && !failed_) {
        accepted_ = std::move(pending_);
    }
    evaluating_ = false;
}

int config_state::configure(lua_State* lua) noexcept {
    const int top = lua_gettop(lua);
    std::array<char, 512> error {};
    // Reserve stack before constructing owning C++ locals. Parsing uses only
    // raw, non-allocating Lua reads and never invokes user code or coerces
    // strings.
    if (!lua_checkstack(lua, 8)) {
        failed_ = true;
        return luaL_error(lua, "hyprcast.configure: insufficient Lua stack");
    }
    try {
        require(evaluating_,
                "configure is only available during a full config reload");
        require(!called_,
                "configure may only be called once per config evaluation");
        called_ = true;
        pending_ = parse(lua);
    } catch (const std::exception& e) {
        failed_ = true;
        std::snprintf(error.data(),
                      error.size(),
                      "hyprcast.configure: %s",
                      e.what());
    } catch (...) {
        failed_ = true;
        std::snprintf(error.data(),
                      error.size(),
                      "hyprcast.configure: unexpected C++ exception");
    }
    lua_settop(lua, top);
    // No owning C++ locals or active catch scopes survive this Lua longjmp.
    if (error.front() != '\0') {
        return luaL_error(lua, "%s", error.data());
    }
    return 0;
}

} // namespace hyprcast
