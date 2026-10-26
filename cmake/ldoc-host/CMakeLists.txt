cmake_minimum_required(VERSION 3.25)
project(wormedit_ldoc_host LANGUAGES C)
# This separate project deliberately has no target toolchain: documentation must run on the
# build host even when Wormedit itself targets Windows or another architecture.
add_executable(wormedit_ldoc_host main.c "${LUA_SOURCE_DIR}/onelua.c" "${LFS_SOURCE_DIR}/src/lfs.c")
target_compile_definitions(wormedit_ldoc_host PRIVATE MAKE_LIB)
target_include_directories(wormedit_ldoc_host PRIVATE "${LUA_SOURCE_DIR}" "${LFS_SOURCE_DIR}/src")
set_target_properties(wormedit_ldoc_host PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(wormedit_ldoc_host PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/bin")
endforeach()
if(UNIX)
    target_compile_definitions(wormedit_ldoc_host PRIVATE LUA_USE_POSIX)
    target_link_libraries(wormedit_ldoc_host PRIVATE m ${CMAKE_DL_LIBS})
endif()
