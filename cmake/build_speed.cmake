# Build-time helpers that never change what is built: a compiler cache and a fast linker.
# Both are on by default when found and can be switched off for debugging the toolchain.
option(WORMEDIT_USE_CCACHE "Wrap compiles with ccache or sccache when installed" ON)
option(WORMEDIT_FAST_LINKER "Link with lld or mold on native Linux builds when installed" ON)

if(WORMEDIT_USE_CCACHE AND NOT CMAKE_CXX_COMPILER_LAUNCHER AND NOT MSVC)
    find_program(WORMEDIT_CCACHE NAMES ccache sccache)
    if(WORMEDIT_CCACHE)
        set(CMAKE_C_COMPILER_LAUNCHER "${WORMEDIT_CCACHE}")
        set(CMAKE_CXX_COMPILER_LAUNCHER "${WORMEDIT_CCACHE}")
        message(STATUS "Compiler cache: ${WORMEDIT_CCACHE}")
    endif()
endif()

# The test executables are hundreds of MB in Debug (GNU ld takes ~20 s per relink, lld ~3 s).
if(WORMEDIT_FAST_LINKER AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT CMAKE_CROSSCOMPILING
        AND NOT CMAKE_EXE_LINKER_FLAGS MATCHES "-fuse-ld=")
    include(CheckLinkerFlag)
    foreach(linker IN ITEMS lld mold)
        check_linker_flag(CXX "-fuse-ld=${linker}" WORMEDIT_LINKER_${linker}_WORKS)
        if(WORMEDIT_LINKER_${linker}_WORKS)
            add_link_options("-fuse-ld=${linker}")
            message(STATUS "Linker: ${linker}")
            break()
        endif()
    endforeach()
endif()
