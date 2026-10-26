/* Build-time documentation runner only; this is never linked into Wormedit's sandbox. */
#include <stdio.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <lfs.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        fputs("usage: wormedit_ldoc_host script.lua [arguments]\n", stderr);
        return 1;
    }
    lua_State *state = luaL_newstate();
    if (!state) return 1;
    luaL_openlibs(state);
    luaL_requiref(state, "lfs", luaopen_lfs, 0);
    lua_pop(state, 1);
    lua_createtable(state, argc - 2, 1);
    for (int i = 0; i < argc; ++i) {
        lua_pushstring(state, argv[i]);
        lua_rawseti(state, -2, i - 1);
    }
    lua_setglobal(state, "arg");
    int status = luaL_loadfile(state, argv[1]);
    if (status == LUA_OK) status = lua_pcall(state, 0, LUA_MULTRET, 0);
    if (status != LUA_OK) fprintf(stderr, "%s\n", lua_tostring(state, -1));
    lua_close(state);
    return status == LUA_OK ? 0 : 1;
}
