set(WORMEDIT_LUA_SOURCE_DIR "" CACHE PATH "Path to a Lua 5.4.8 source checkout")

if(TARGET Lua::Lua)
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

add_library(wormedit_lua STATIC "${lua_SOURCE_DIR}/onelua.c")
add_library(Lua::Lua ALIAS wormedit_lua)
target_compile_definitions(wormedit_lua PRIVATE MAKE_LIB)
target_include_directories(wormedit_lua PUBLIC "${lua_SOURCE_DIR}")
set_target_properties(wormedit_lua PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)

if(UNIX)
    target_link_libraries(wormedit_lua PUBLIC m)
endif()
