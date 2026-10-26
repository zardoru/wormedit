if(TARGET Lua::Lua)
    return()
endif()
include(${CMAKE_CURRENT_LIST_DIR}/lua_source.cmake)

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
