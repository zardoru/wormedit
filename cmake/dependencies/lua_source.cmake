set(WORMEDIT_LUA_SOURCE_DIR "" CACHE PATH "Path to a Lua 5.4.8 source checkout")

if(DEFINED lua_SOURCE_DIR)
    return()
endif()

if(WORMEDIT_LUA_SOURCE_DIR)
    FetchContent_Declare(lua SOURCE_DIR "${WORMEDIT_LUA_SOURCE_DIR}")
else()
    FetchContent_Declare(
        lua
        GIT_REPOSITORY https://github.com/lua/lua.git
        GIT_TAG 6e22fedb74cf0c9b6656e9fce8b7331db847c605
    )
endif()

FetchContent_MakeAvailable(lua)

if(NOT EXISTS "${lua_SOURCE_DIR}/onelua.c" OR NOT EXISTS "${lua_SOURCE_DIR}/lua.h")
    message(FATAL_ERROR "Lua source directory must contain Lua 5.4.8 onelua.c and lua.h")
endif()
