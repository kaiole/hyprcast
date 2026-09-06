#include "hyprcast/config/Config.hpp"

#include <lua.hpp>

#include <cstdlib>
#include <iostream>
#include <string>

using namespace Hyprcast;
static void check(bool value) {
    if (!value) {
        std::cerr << "check failed\n";
        std::exit(1);
    }
}
int main() {
    CConfig    state;
    lua_State* L = luaL_newstate();
    check(L != nullptr);
    luaL_openlibs(L);
    lua_pushlightuserdata(L, &state);
    lua_pushcclosure(L, [](lua_State* L) { return static_cast<CConfig*>(lua_touserdata(L, lua_upvalueindex(1)))->configure(L); }, 1);
    lua_setglobal(L, "configure");
    auto run = [&](const std::string& code, bool success = true) {
        const int top    = lua_gettop(L);
        const int result = luaL_dostring(L, code.c_str());
        if ((result == LUA_OK) != success) {
            std::cerr << code << "\n" << (result == LUA_OK ? "unexpected success" : lua_tostring(L, -1)) << '\n';
            std::exit(1);
        }
        if (result != LUA_OK) {
            check(std::string(lua_tostring(L, -1)).contains("hyprcast.configure:"));
            lua_pop(L, 1);
        }
        check(lua_gettop(L) == top);
    };
    check(state.accepted() == SConfig{});
    run("configure({})", false);
    state.beginReload();
    run("configure({filter='include',keyboards={' Foo ', 'foo', ' Foo ', 'not connected'}})");
    check(state.accepted() == SConfig{});
    state.finishReload(true);
    check(state.accepted().filter == eKeyboardFilter::INCLUDE);
    check(state.accepted().keyboards == std::vector<std::string>({" Foo ", "foo", "not connected"}));
    const auto  previous  = state.accepted();
    const char* invalid[] = {"configure()",
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
                             "configure(setmetatable({}, {__index=function() error('must not execute') end}))",
                             "configure({keyboards=setmetatable({}, {})})",
                             "configure({['keyboards\\0x']={}})"};
    for (auto code : invalid) {
        state.beginReload();
        run(code, false);
        state.finishReload(true); // Even a caught validation error poisons this evaluation.
        check(state.accepted() == previous);
    }
    state.beginReload();
    run("configure({filter='exclude',keyboards={'private'}})");
    state.finishReload(false);
    check(state.accepted() == previous);
    state.beginReload();
    run("configure({})");
    run("configure({})", false);
    state.finishReload(true);
    check(state.accepted() == previous);
    state.beginReload();
    run("configure({filter='exclude',keyboards={'private'}})");
    state.beginReload();
    run("configure({filter='include'})");
    state.finishReload(true);
    state.finishReload(true);
    check(state.accepted().filter == eKeyboardFilter::INCLUDE);
    check(state.accepted().keyboards.empty());
    state.beginReload();
    state.finishReload(true);
    check(state.accepted() == SConfig{});
    state.beginReload();
    run("configure({filter='exclude',keyboards={}})");
    state.finishReload(true);
    check(state.accepted() == SConfig{});
    state.beginReload();
    run("configure({filter=nil,keyboards={'a','a','A'}})");
    state.finishReload(true);
    check(state.accepted().filter == eKeyboardFilter::EXCLUDE);
    check(state.accepted().keyboards == std::vector<std::string>({"a", "A"}));
    state.beginReload();
    run("configure({})");
    state.finishReload(true);
    check(state.accepted() == SConfig{});
    // Direct successful parsing preserves its argument stack too.
    state.beginReload();
    lua_newtable(L);
    check(state.configure(L) == 0);
    check(lua_gettop(L) == 1 && lua_istable(L, 1));
    lua_pop(L, 1);
    state.finishReload(true);
    lua_close(L);
    std::cout << "configuration tests passed\n";
}
