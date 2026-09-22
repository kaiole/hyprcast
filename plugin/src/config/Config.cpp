#include "hyprcast/config/Config.hpp"

#include <lua.hpp>

#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace Hyprcast {
    namespace {
        void require(bool condition, const char* message) {
            if (!condition) {
                throw std::invalid_argument(message);
            }
        }

        std::string_view stringAt(lua_State* L, int index) {
            size_t      length = 0;
            const char* data   = lua_tolstring(L, index, &length);
            return {data, length};
        }

        void plainTable(lua_State* L, int index) {
            require(lua_type(L, index) == LUA_TTABLE, "expected a table");
            require(!lua_getmetatable(L, index), "metatables are not supported");
        }

        void readList(lua_State* L, std::vector<std::string>& out) {
            plainTable(L, -1);
            const int    table  = lua_absindex(L, -1);
            const size_t length = lua_rawlen(L, table);
            size_t       count  = 0;
            lua_pushnil(L);
            while (lua_next(L, table)) {
                require(lua_isinteger(L, -2), "keyboards must be a dense array with integer keys");
                const auto key = lua_tointeger(L, -2);
                require(key > 0 && std::cmp_less_equal(key, length), "keyboards must be a dense array starting at 1");
                require(lua_type(L, -1) == LUA_TSTRING, "keyboard entries must be strings");
                const auto name = stringAt(L, -1);
                require(!name.empty(), "keyboard names must not be empty");
                require(!name.contains('\0'), "keyboard names must not contain NUL bytes");
                ++count;
                lua_pop(L, 1);
            }
            require(count == length, "keyboards must not contain holes");
            for (size_t i = 1; i <= length; ++i) {
                lua_rawgeti(L, table, static_cast<lua_Integer>(i));
                const auto name = stringAt(L, -1);
                if (std::ranges::find(out, name) == out.end()) {
                    out.emplace_back(name);
                }
                lua_pop(L, 1);
            }
        }

        SConfig parse(lua_State* L) {
            require(lua_gettop(L) == 1, "configure expects exactly one table argument");
            plainTable(L, 1);
            SConfig candidate;
            lua_pushnil(L);
            while (lua_next(L, 1)) {
                require(lua_type(L, -2) == LUA_TSTRING, "configuration field names must be strings");
                const auto key = stringAt(L, -2);
                if (key == "filter") {
                    require(lua_type(L, -1) == LUA_TSTRING, "filter must be 'include' or 'exclude'");
                    const auto filter = stringAt(L, -1);
                    require(filter == "include" || filter == "exclude", "filter must be 'include' or 'exclude'");
                    candidate.filterSetting = filter == "include" ? eKeyboardFilterSetting::INCLUDE : eKeyboardFilterSetting::EXCLUDE;
                } else if (key == "keyboards") {
                    readList(L, candidate.filteredKeyboards);
                } else {
                    throw std::invalid_argument("unknown configuration field; expected filter or keyboards");
                }
                lua_pop(L, 1);
            }
            return candidate;
        }
    }

    void CConfig::beginReload() noexcept {
        // A nested Hyprland reload supersedes the preceding evaluation.
        m_pending    = {};
        m_evaluating = true;
        m_called = m_failed = false;
    }

    void CConfig::finishReload(bool successful) noexcept {
        if (!m_evaluating) {
            return;
        }
        if (successful && !m_failed) {
            m_accepted = std::move(m_pending);
        }
        m_evaluating = false;
    }

    int CConfig::configure(lua_State* L) noexcept {
        const int top        = lua_gettop(L);
        char      error[512] = {};
        // Reserve stack before constructing owning C++ locals. Parsing uses only raw,
        // non-allocating Lua reads and never invokes user code or coerces strings.
        if (!lua_checkstack(L, 8)) {
            m_failed = true;
            return luaL_error(L, "hyprcast.configure: insufficient Lua stack");
        }
        try {
            require(m_evaluating, "configure is only available during a full config reload");
            require(!m_called, "configure may only be called once per config evaluation");
            m_called  = true;
            m_pending = parse(L);
        } catch (const std::exception& e) {
            m_failed = true;
            std::snprintf(error, sizeof(error), "hyprcast.configure: %s", e.what());
        } catch (...) {
            m_failed = true;
            std::snprintf(error, sizeof(error), "hyprcast.configure: unexpected C++ exception");
        }
        lua_settop(L, top);
        // No owning C++ locals or active catch scopes survive this Lua longjmp.
        if (error[0]) {
            return luaL_error(L, "%s", error);
        }
        return 0;
    }
}
