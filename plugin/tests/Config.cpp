#include "hyprcast/config/Config.hpp"

#include <lua.hpp>

#include <array>
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

using namespace hyprcast;

namespace {

void check(bool value) {
    if (!value) {
        std::cerr << "check failed\n";
        std::_Exit(1);
    }
}

} // namespace

int main() {
    config_state state;
    lua_State* lua = luaL_newstate();
    check(lua != nullptr);
    luaL_openlibs(lua);
    lua_pushlightuserdata(lua, &state);
    lua_pushcclosure(
        lua,
        [](lua_State* lua_state) {
            return static_cast<config_state*>(
                       lua_touserdata(lua_state, lua_upvalueindex(1)))
                ->configure(lua_state);
        },
        1);
    lua_setglobal(lua, "configure");
    const auto run = [&](const std::string& code, bool success = true) {
        const int top = lua_gettop(lua);
        const int result = luaL_dostring(lua, code.c_str());
        if ((result == LUA_OK) != success) {
            std::cerr << code << "\n"
                      << (result == LUA_OK ? "unexpected success"
                                           : lua_tostring(lua, -1))
                      << '\n';
            std::_Exit(1);
        }
        if (result != LUA_OK) {
            check(std::string(lua_tostring(lua, -1))
                      .contains("hyprcast.configure:"));
            lua_pop(lua, 1);
        }
        check(lua_gettop(lua) == top);
    };
    check(state.accepted() == config {});
    run("configure({})", false);
    state.begin_reload();
    run("configure({filter='include',keyboards={' Foo ', 'foo', ' Foo ', 'not "
        "connected'}})");
    check(state.accepted() == config {});
    state.finish_reload(true);
    check(state.accepted().filter_setting == keyboard_filter_setting::include);
    check(state.accepted().filtered_keyboards
          == std::vector<std::string>({" Foo ", "foo", "not connected"}));
    const auto previous = state.accepted();
    const std::array invalid {
        "configure()",
        "configure({}, {})",
        "configure(nil)",
        "configure(1)",
        "configure('include')",
        "configure({wat=true})",
        "configure({capture_keyboards='all'})",
        "configure({include={}})",
        "configure({exclude={}})",
        "configure({[1]='x'})",
        "configure({filter=1})",
        "configure({filter='INCLUDE'})",
        "configure({filter='include\\0bad'})",
        "configure({keyboards='x'})",
        "configure({keyboards={1}})",
        "configure({keyboards={''}})",
        "configure({keyboards={'a\\0b'}})",
        "configure({keyboards={[1]='a',[3]='b'}})",
        "configure({keyboards={[0]='a'}})",
        "configure({keyboards={[-1]='a'}})",
        "configure({keyboards={[1.5]='a'}})",
        "configure({keyboards={x='a'}})",
        "configure(setmetatable({}, {__index=function() error('ran') end}))",
        "configure({keyboards=setmetatable({}, {})})",
        "configure({['keyboards\\0x']={}})",
    };
    for (const auto* code : invalid) {
        state.begin_reload();
        run(code, false);
        state.finish_reload(
            true); // Even a caught validation error poisons this evaluation.
        check(state.accepted() == previous);
    }
    state.begin_reload();
    run("configure({filter='exclude',keyboards={'private'}})");
    state.finish_reload(false);
    check(state.accepted() == previous);
    state.begin_reload();
    run("configure({})");
    run("configure({})", false);
    state.finish_reload(true);
    check(state.accepted() == previous);
    state.begin_reload();
    run("configure({filter='exclude',keyboards={'private'}})");
    state.begin_reload();
    run("configure({filter='include'})");
    state.finish_reload(true);
    state.finish_reload(true);
    check(state.accepted().filter_setting == keyboard_filter_setting::include);
    check(state.accepted().filtered_keyboards.empty());
    state.begin_reload();
    state.finish_reload(true);
    check(state.accepted() == config {});
    state.begin_reload();
    run("configure({filter='exclude',keyboards={}})");
    state.finish_reload(true);
    check(state.accepted() == config {});
    state.begin_reload();
    run("configure({filter=nil,keyboards={'a','a','A'}})");
    state.finish_reload(true);
    check(state.accepted().filter_setting == keyboard_filter_setting::exclude);
    check(state.accepted().filtered_keyboards
          == std::vector<std::string>({"a", "A"}));
    state.begin_reload();
    run("configure({})");
    state.finish_reload(true);
    check(state.accepted() == config {});
    for (const auto& [code, expected] :
         std::vector<std::pair<std::string, std::string>> {
             {
                 "configure({wat=true})",
                 "unknown configuration field \"wat\"; expected filter or "
                 "keyboards",
             },
             {"configure({keyboards='x'})", "keyboards must be a table"},
             {
                 "configure({keyboards={'ok', 1}})",
                 "keyboards[2] must be a string",
             },
             {"configure({keyboards={''}})", "keyboards[1] must not be empty"},
             {
                 "configure({keyboards={'a\\0b'}})",
                 "keyboards[1] must not contain NUL bytes",
             },
         }) {
        state.begin_reload();
        check(luaL_dostring(lua, code.c_str()) != LUA_OK);
        check(std::string(lua_tostring(lua, -1))
                  .contains("hyprcast.configure: " + expected));
        lua_pop(lua, 1);
        state.finish_reload(false);
    }
    // Direct successful parsing preserves its argument stack too.
    state.begin_reload();
    lua_newtable(lua);
    check(state.configure(lua) == 0);
    check(lua_gettop(lua) == 1 && lua_istable(lua, 1));
    lua_pop(lua, 1);
    state.finish_reload(true);
    lua_close(lua);
    std::cout << "configuration tests passed\n";
}
