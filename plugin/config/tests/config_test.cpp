#include "hyprcast/config/config.hpp"

#include <gtest/gtest.h>
#include <lua.hpp>

#include <array>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace hyprcast;

struct lua_closer {
    void operator()(lua_State* lua) const noexcept { lua_close(lua); }
};

// A Lua state whose global `configure` reaches `state` the way the function
// Hyprland exposes as hl.plugin.hyprcast.configure does. `configure_unloaded`
// is the same function with no state behind it.
class ConfigStateTest : public testing::Test {
protected:
    ConfigStateTest() : lua(luaL_newstate()) {
        if (!lua) {
            return;
        }
        luaL_openlibs(lua.get());
        expose("configure", &state);
        expose("configure_unloaded", nullptr);
    }

    // Makes call_configure() for `target` the Lua global `name`.
    void expose(const char* name, config_state* target) {
        lua_pushlightuserdata(lua.get(), target);
        lua_pushcclosure(
            lua.get(),
            [](lua_State* lua_state) {
                return call_configure(
                    lua_state,
                    static_cast<config_state*>(
                        lua_touserdata(lua_state, lua_upvalueindex(1))));
            },
            1);
        lua_setglobal(lua.get(), name);
    }

    void SetUp() override { ASSERT_NE(lua, nullptr); }

    // Runs `code` and returns the error message, empty on success. The Lua
    // stack must be left as it was found either way.
    std::string run(const std::string& code) {
        const int top = lua_gettop(lua.get());
        std::string error;
        if (luaL_dostring(lua.get(), code.c_str()) != LUA_OK) {
            error = lua_tostring(lua.get(), -1);
            lua_pop(lua.get(), 1);
        }
        EXPECT_EQ(lua_gettop(lua.get()), top) << code;
        return error;
    }

    void expect_success(const std::string& code) {
        EXPECT_EQ(run(code), "") << code;
    }

    // Every failure is reported as a Lua error that names the function.
    void expect_failure(const std::string& code) {
        const std::string error = run(code);
        EXPECT_TRUE(error.contains("hl.plugin.hyprcast.configure:"))
            << code << "\n"
            << (error.empty() ? "unexpected success" : error);
    }

    // One complete, successful reload that evaluates `code`.
    void reload(const std::string& code) {
        state.begin_reload();
        expect_success(code);
        state.finish_reload(true);
    }

    // Gives the state a non-default accepted configuration and returns it.
    config accept_include_list() {
        reload("configure({filter='include',keyboards={'kept'}})");
        const config expected {
            .filter_setting = keyboard_filter_setting::include,
            .filtered_keyboards = {"kept"},
        };
        EXPECT_EQ(state.accepted(), expected);
        return state.accepted();
    }

    config_state state;
    std::unique_ptr<lua_State, lua_closer> lua;
};

TEST_F(ConfigStateTest, StartsWithDefaults) {
    EXPECT_EQ(state.accepted(), config {});
    EXPECT_EQ(config {}.filter_setting, keyboard_filter_setting::exclude);
    EXPECT_TRUE(config {}.filtered_keyboards.empty());
}

TEST_F(ConfigStateTest, RejectsConfigureOutsideReload) {
    expect_failure("configure({})");
    EXPECT_EQ(state.accepted(), config {});
}

TEST_F(ConfigStateTest, AppliesPendingConfigWhenReloadFinishes) {
    state.begin_reload();
    expect_success("configure({filter='include',keyboards={' Foo ', 'foo', ' "
                   "Foo ', 'not connected'}})");
    EXPECT_EQ(state.accepted(), config {});

    state.finish_reload(true);
    EXPECT_EQ(state.accepted().filter_setting,
              keyboard_filter_setting::include);
    // Names are kept verbatim; exact duplicates are dropped.
    EXPECT_EQ(state.accepted().filtered_keyboards,
              std::vector<std::string>({" Foo ", "foo", "not connected"}));
}

TEST_F(ConfigStateTest, RejectsInvalidInputAndKeepsPreviousConfig) {
    const config previous = accept_include_list();

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
        SCOPED_TRACE(code);
        state.begin_reload();
        expect_failure(code);
        // Even a caught validation error poisons this evaluation.
        state.finish_reload(true);
        EXPECT_EQ(state.accepted(), previous);
    }
}

TEST_F(ConfigStateTest, KeepsPreviousConfigWhenReloadFails) {
    const config previous = accept_include_list();

    state.begin_reload();
    expect_success("configure({filter='exclude',keyboards={'private'}})");
    state.finish_reload(false);
    EXPECT_EQ(state.accepted(), previous);
}

TEST_F(ConfigStateTest, RejectsSecondCallInOneReload) {
    const config previous = accept_include_list();

    state.begin_reload();
    expect_success("configure({})");
    expect_failure("configure({})");
    state.finish_reload(true);
    EXPECT_EQ(state.accepted(), previous);
}

TEST_F(ConfigStateTest, NestedReloadSupersedesTheOuterOne) {
    accept_include_list();

    state.begin_reload();
    expect_success("configure({filter='exclude',keyboards={'private'}})");
    state.begin_reload();
    expect_success("configure({filter='include'})");
    state.finish_reload(true);
    state.finish_reload(true);
    EXPECT_EQ(state.accepted().filter_setting,
              keyboard_filter_setting::include);
    EXPECT_TRUE(state.accepted().filtered_keyboards.empty());
}

TEST_F(ConfigStateTest, ReloadWithoutConfigureRestoresDefaults) {
    accept_include_list();

    state.begin_reload();
    state.finish_reload(true);
    EXPECT_EQ(state.accepted(), config {});
}

TEST_F(ConfigStateTest, EmptyExcludeListEqualsDefaults) {
    accept_include_list();

    reload("configure({filter='exclude',keyboards={}})");
    EXPECT_EQ(state.accepted(), config {});
}

TEST_F(ConfigStateTest, EmptyTableRestoresDefaults) {
    accept_include_list();

    reload("configure({})");
    EXPECT_EQ(state.accepted(), config {});
}

TEST_F(ConfigStateTest, NilFilterKeepsDefaultAndNamesAreCaseSensitive) {
    reload("configure({filter=nil,keyboards={'a','a','A'}})");
    EXPECT_EQ(state.accepted().filter_setting,
              keyboard_filter_setting::exclude);
    EXPECT_EQ(state.accepted().filtered_keyboards,
              std::vector<std::string>({"a", "A"}));
}

TEST_F(ConfigStateTest, ReportsWhichFieldIsInvalid) {
    const std::vector<std::pair<std::string, std::string>> cases {
        {
            "configure({wat=true})",
            "unknown configuration field \"wat\"; expected filter or "
            "keyboards",
        },
        {"configure({keyboards='x'})", "keyboards must be a table"},
        {"configure({keyboards={'ok', 1}})", "keyboards[2] must be a string"},
        {"configure({keyboards={''}})", "keyboards[1] must not be empty"},
        {
            "configure({keyboards={'a\\0b'}})",
            "keyboards[1] must not contain NUL bytes",
        },
    };
    for (const auto& [code, expected] : cases) {
        SCOPED_TRACE(code);
        state.begin_reload();
        const std::string error = run(code);
        EXPECT_TRUE(error.contains("hl.plugin.hyprcast.configure: " + expected))
            << error;
        state.finish_reload(false);
    }
}

// The function can outlive the plugin object in a Lua state that still holds
// it. Calling it then is an error the script can catch, and nothing more.
TEST_F(ConfigStateTest, ConfigureWithoutAStateIsALuaError) {
    state.begin_reload();

    const std::string error = run("configure_unloaded({filter='include'})");
    EXPECT_TRUE(
        error.ends_with("hl.plugin.hyprcast.configure: hyprcast is not loaded"))
        << error;
    expect_success("assert(not pcall(configure_unloaded, {}))");

    // The state in use has not heard of either call.
    expect_success("configure({keyboards={'kept'}})");
    state.finish_reload(true);
    EXPECT_EQ(state.accepted().filtered_keyboards,
              std::vector<std::string> {"kept"});
}

// Called directly rather than through Lua's call machinery, a successful
// configure() must leave its argument on the stack.
TEST_F(ConfigStateTest, DirectCallPreservesTheArgumentStack) {
    state.begin_reload();
    lua_newtable(lua.get());
    EXPECT_EQ(state.configure(lua.get()), 0);
    EXPECT_EQ(lua_gettop(lua.get()), 1);
    EXPECT_TRUE(lua_istable(lua.get(), 1));
    lua_pop(lua.get(), 1);
    state.finish_reload(true);
    EXPECT_EQ(state.accepted(), config {});
}

} // namespace
