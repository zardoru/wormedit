# Xiph libogg 1.3.6 and libvorbis 1.3.7, BSD-3-Clause (COPYING in each archive).
# Release hashes: https://www.xiph.org/downloads/
set(WORMEDIT_OGG_SOURCE_DIR "" CACHE PATH "Path to libogg 1.3.6 sources")
set(WORMEDIT_VORBIS_SOURCE_DIR "" CACHE PATH "Path to libvorbis 1.3.7 sources")

function(wormedit_build_vorbis)
    set(BUILD_SHARED_LIBS OFF)
    set(BUILD_TESTING OFF)
    set(INSTALL_DOCS OFF)
    set(INSTALL_CMAKE_PACKAGE_MODULE OFF)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    if(WORMEDIT_OGG_SOURCE_DIR)
        FetchContent_Declare(wormedit_ogg SOURCE_DIR "${WORMEDIT_OGG_SOURCE_DIR}")
    else()
        FetchContent_Declare(wormedit_ogg
            URL https://downloads.xiph.org/releases/ogg/libogg-1.3.6.tar.gz
            URL_HASH SHA256=83e6704730683d004d20e21b8f7f55dcb3383cdf84c0daedf30bde175f774638
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    endif()
    FetchContent_MakeAvailable(wormedit_ogg)
    # The upstream module recognizes an existing Ogg::ogg target, but still
    # requires these variables. Point it at our pinned build, never host libraries.
    set(OGG_LIBRARY Ogg::ogg)
    set(OGG_INCLUDE_DIR "${wormedit_ogg_SOURCE_DIR}/include")
    if(WORMEDIT_VORBIS_SOURCE_DIR)
        FetchContent_Declare(wormedit_vorbis SOURCE_DIR "${WORMEDIT_VORBIS_SOURCE_DIR}")
    else()
        FetchContent_Declare(wormedit_vorbis
            URL https://downloads.xiph.org/releases/vorbis/libvorbis-1.3.7.tar.gz
            URL_HASH SHA256=0e982409a9c3fc82ee06e08205b1355e5c6aa4c36bca58146ef399621b0ce5ab
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    endif()
    FetchContent_MakeAvailable(wormedit_vorbis)
    include(CheckIncludeFile)
    check_include_file(alloca.h WORMEDIT_HAVE_ALLOCA_H)
    if(WORMEDIT_HAVE_ALLOCA_H)
        foreach(vorbis_target vorbis vorbisenc vorbisfile)
            target_compile_definitions(${vorbis_target} PRIVATE HAVE_ALLOCA_H=1)
        endforeach()
    endif()
endfunction()
wormedit_build_vorbis()
