# zlib for libotoworm when targeting Windows. Cross builds must not pick up a host zlib, and
# mingw sysroots rarely ship one, so build the pinned release from source.
set(WORMEDIT_ZLIB_SOURCE_DIR "" CACHE PATH "Path to a pinned zlib source checkout")

if(TARGET ZLIB::ZLIB)
    return()
endif()

if(WORMEDIT_ZLIB_SOURCE_DIR)
    FetchContent_Declare(wormedit_zlib SOURCE_DIR "${WORMEDIT_ZLIB_SOURCE_DIR}")
else()
    FetchContent_Declare(
        wormedit_zlib
        GIT_REPOSITORY https://github.com/madler/zlib.git
        GIT_TAG 51b7f2abdade71cd9bb0e7a373ef2610ec6f9daf # v1.3.1
    )
endif()

set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SKIP_INSTALL_ALL ON)
FetchContent_GetProperties(wormedit_zlib)
if(NOT wormedit_zlib_POPULATED)
    FetchContent_Populate(wormedit_zlib)
    add_subdirectory("${wormedit_zlib_SOURCE_DIR}" "${wormedit_zlib_BINARY_DIR}" EXCLUDE_FROM_ALL)
endif()

add_library(ZLIB::ZLIB INTERFACE IMPORTED GLOBAL)
target_link_libraries(ZLIB::ZLIB INTERFACE zlibstatic)
# zlib's own include dirs are not exported on its targets; zconf.h is generated in the binary dir.
target_include_directories(ZLIB::ZLIB INTERFACE
    "${wormedit_zlib_SOURCE_DIR}" "${wormedit_zlib_BINARY_DIR}")
