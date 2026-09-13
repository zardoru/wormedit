#include <catch2/catch_test_macros.hpp>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

TEST_CASE("pinned Lua runtime evaluates a valid generator expression", "[unit][lua]")
{
    auto* state = luaL_newstate();
    REQUIRE(state != nullptr);
    luaL_openlibs(state);

    REQUIRE(luaL_dostring(state, "return 192 * 5") == LUA_OK);
    REQUIRE(lua_isinteger(state, -1));
    CHECK(lua_tointeger(state, -1) == 960);

    lua_close(state);
}

TEST_CASE("pinned Lua runtime reports malformed source", "[unit][lua][negative]")
{
    auto* state = luaL_newstate();
    REQUIRE(state != nullptr);

    CHECK(luaL_loadstring(state, "measure {") == LUA_ERRSYNTAX);
    REQUIRE(lua_isstring(state, -1));

    lua_close(state);
}
