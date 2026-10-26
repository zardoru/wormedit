# Pinned, build-time-only dependencies; FetchContent's standard SOURCE_DIR overrides work offline.
include(${CMAKE_CURRENT_LIST_DIR}/lua_source.cmake)
FetchContent_Declare(wormedit_ldoc
    GIT_REPOSITORY https://github.com/lunarmodules/LDoc.git
    GIT_TAG 09f82c959c50d8c3d5a968c379b1c75de66b002d # v1.5.0
    SOURCE_SUBDIR wormedit-no-build)
FetchContent_Declare(wormedit_penlight
    GIT_REPOSITORY https://github.com/lunarmodules/Penlight.git
    GIT_TAG e2e5f6477bd060f0d94349662f24ac1ecfdf543e # 1.14.0
    SOURCE_SUBDIR wormedit-no-build)
FetchContent_Declare(wormedit_lfs
    GIT_REPOSITORY https://github.com/lunarmodules/luafilesystem.git
    GIT_TAG 7c6e1b013caec0602ca4796df3b1d7253a2dd258 # v1_8_0
    SOURCE_SUBDIR wormedit-no-build)
FetchContent_MakeAvailable(wormedit_ldoc wormedit_penlight wormedit_lfs)

set(wormedit_ldoc_host_dir "${CMAKE_BINARY_DIR}/ldoc-host")
set(wormedit_ldoc_host "${wormedit_ldoc_host_dir}/bin/wormedit_ldoc_host")
if(CMAKE_HOST_WIN32)
    string(APPEND wormedit_ldoc_host ".exe")
endif()
set(wormedit_ldoc_host_compiler "")
if(CMAKE_CROSSCOMPILING)
    find_program(WORMEDIT_HOST_C_COMPILER NAMES cc clang gcc REQUIRED NO_CMAKE_FIND_ROOT_PATH)
    set(wormedit_ldoc_host_compiler "-DCMAKE_C_COMPILER=${WORMEDIT_HOST_C_COMPILER}")
endif()
file(GLOB wormedit_ldoc_host_sources CONFIGURE_DEPENDS "${lua_SOURCE_DIR}/*.c" "${lua_SOURCE_DIR}/*.h"
    "${wormedit_lfs_SOURCE_DIR}/src/*.c" "${wormedit_lfs_SOURCE_DIR}/src/*.h")
add_custom_command(OUTPUT "${wormedit_ldoc_host}"
    COMMAND "${CMAKE_COMMAND}" -S "${CMAKE_SOURCE_DIR}/cmake/ldoc-host" -B "${wormedit_ldoc_host_dir}"
        "-DLUA_SOURCE_DIR=${lua_SOURCE_DIR}" "-DLFS_SOURCE_DIR=${wormedit_lfs_SOURCE_DIR}"
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE= ${wormedit_ldoc_host_compiler}
    COMMAND "${CMAKE_COMMAND}" --build "${wormedit_ldoc_host_dir}" --config Release --parallel 2
    DEPENDS "${CMAKE_SOURCE_DIR}/cmake/ldoc-host/CMakeLists.txt" "${CMAKE_SOURCE_DIR}/cmake/ldoc-host/main.c"
        ${wormedit_ldoc_host_sources}
    COMMENT "Building the native LDoc runner"
    VERBATIM)
