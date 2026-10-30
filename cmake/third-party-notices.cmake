# One notice inventory for every application install. Keep Windows' existing
# third-party/ layout and place other platforms' notices next to their binary.
set(wormedit_notice_root "${WORMEDIT_INSTALL_BINDIR}/third-party")
function(wormedit_install_notices dependency)
    install(FILES ${ARGN} DESTINATION "${wormedit_notice_root}/${dependency}")
endfunction()

wormedit_install_notices(SDL3 "${sdl3_SOURCE_DIR}/LICENSE.txt")
wormedit_install_notices(SDL3 "${CMAKE_CURRENT_LIST_DIR}/notices/SDL3-README.txt"
    "${CMAKE_CURRENT_LIST_DIR}/notices/Apache-2.0.txt")
wormedit_install_notices(SDL3/hidapi "${sdl3_SOURCE_DIR}/src/hidapi/LICENSE.txt"
    "${sdl3_SOURCE_DIR}/src/hidapi/LICENSE-bsd.txt"
    "${sdl3_SOURCE_DIR}/src/hidapi/LICENSE-orig.txt")
wormedit_install_notices(SDL3/yuv2rgb "${sdl3_SOURCE_DIR}/src/video/yuv2rgb/LICENSE")
# Preserve notices embedded in SDL's bundled math, Khronos and other sources.
# Use CMake's archive implementation rather than parsing license comments.
set(wormedit_sdl_notice_archive "${CMAKE_BINARY_DIR}/SDL3-source-notices.tar.xz")
install(CODE "execute_process(COMMAND \"${CMAKE_COMMAND}\" -E tar cJf
    \"${wormedit_sdl_notice_archive}\" include src LICENSE.txt
    WORKING_DIRECTORY \"${sdl3_SOURCE_DIR}\" COMMAND_ERROR_IS_FATAL ANY)")
wormedit_install_notices(SDL3 "${wormedit_sdl_notice_archive}")
wormedit_install_notices(yaml-cpp "${yaml_cpp_SOURCE_DIR}/LICENSE")
wormedit_install_notices(nuklear "${nuklear_SOURCE_DIR}/LICENSE")
if(WORMEDIT_NLOHMANN_JSON_SOURCE_DIR)
    set(wormedit_json_notice_source "${WORMEDIT_NLOHMANN_JSON_SOURCE_DIR}")
else()
    set(wormedit_json_notice_source "${nlohmann_json_SOURCE_DIR}")
endif()
wormedit_install_notices(nlohmann-json "${wormedit_json_notice_source}/LICENSE.MIT")
if(WORMEDIT_ENABLE_AUDIO_DECODER)
    wormedit_install_notices(miniaudio "${wormedit_miniaudio_source_dir}/LICENSE"
        "${wormedit_miniaudio_source_dir}/miniaudio.h"
        "${wormedit_miniaudio_source_dir}/extras/stb_vorbis.c")
    # These sources are populated inside wormedit_build_vorbis's function scope.
    FetchContent_GetProperties(wormedit_ogg SOURCE_DIR wormedit_ogg_notice_source)
    FetchContent_GetProperties(wormedit_vorbis SOURCE_DIR wormedit_vorbis_notice_source)
    wormedit_install_notices(libogg "${wormedit_ogg_notice_source}/COPYING")
    wormedit_install_notices(libvorbis "${wormedit_vorbis_notice_source}/COPYING")
endif()
if(WORMEDIT_ENABLE_IMAGE_DECODER)
    wormedit_install_notices(stb "${wormedit_stb_source_dir}/LICENSE")
endif()
if(WORMEDIT_ENABLE_LUA OR WORMEDIT_INSTALL_MANUAL)
    wormedit_install_notices(Lua "${lua_SOURCE_DIR}/lua.h")
endif()
if(WORMEDIT_INSTALL_MANUAL AND WORMEDIT_ASCIIDOCTOR)
    # The generated HTML embeds Asciidoctor's MIT-licensed default stylesheet.
    # RubyGems exposes the selected installation; distro packages may keep its
    # license in /usr/share/doc instead of the gem directory.
    find_program(wormedit_notice_ruby NAMES ruby)
    if(wormedit_notice_ruby)
        execute_process(COMMAND "${wormedit_notice_ruby}" -rrubygems -e
            "puts Gem::Specification.find_by_name('asciidoctor').full_gem_path"
            OUTPUT_VARIABLE wormedit_asciidoctor_gem OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
    endif()
    find_file(WORMEDIT_ASCIIDOCTOR_NOTICE_FILE NAMES LICENSE copyright
        PATHS "${wormedit_asciidoctor_gem}" /usr/share/doc/ruby-asciidoctor
            /usr/share/doc/asciidoctor /usr/share/licenses/asciidoctor
        NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH
        DOC "Copyright/license notice for the Asciidoctor HTML stylesheet")
    if(WORMEDIT_ASCIIDOCTOR_NOTICE_FILE)
        wormedit_install_notices(Asciidoctor "${WORMEDIT_ASCIIDOCTOR_NOTICE_FILE}")
    else()
        install(CODE "message(FATAL_ERROR \"Set WORMEDIT_ASCIIDOCTOR_NOTICE_FILE for the bundled stylesheet.\")")
    endif()
    wormedit_install_notices(LDoc "${wormedit_ldoc_SOURCE_DIR}/COPYRIGHT")
    wormedit_install_notices(Penlight "${wormedit_penlight_SOURCE_DIR}/LICENSE.md")
    wormedit_install_notices(LuaFileSystem "${wormedit_lfs_SOURCE_DIR}/LICENSE")
endif()

# Windows builds pinned zlib; Linux normally links the distribution's zlib.
# An explicit file also supports other SDKs without borrowing a host notice in
# a cross-build. A missing notice fails installation instead of being omitted.
set(WORMEDIT_ZLIB_NOTICE_FILE "" CACHE FILEPATH "License/copyright notice for system zlib")
if(wormedit_zlib_SOURCE_DIR)
    set(wormedit_zlib_notice "${wormedit_zlib_SOURCE_DIR}/LICENSE")
elseif(WORMEDIT_ZLIB_NOTICE_FILE)
    set(wormedit_zlib_notice "${WORMEDIT_ZLIB_NOTICE_FILE}")
elseif(APPLE)
    # macOS links the operating system's /usr/lib/libz.dylib; it is not bundled.
    set(wormedit_zlib_system_library ON)
elseif(NOT CMAKE_CROSSCOMPILING)
    find_file(wormedit_zlib_notice NAMES LICENSE copyright
        PATHS /usr/share/licenses/zlib /usr/share/doc/zlib1g NO_DEFAULT_PATH)
endif()
if(wormedit_zlib_notice)
    wormedit_install_notices(zlib "${wormedit_zlib_notice}")
elseif(NOT wormedit_zlib_system_library)
    install(CODE "message(FATAL_ERROR \"Set WORMEDIT_ZLIB_NOTICE_FILE for this zlib SDK.\")")
endif()

set(WORMEDIT_FFMPEG_NOTICES_DIR "" CACHE PATH
    "FFmpeg SDK license, corresponding source archive and build configuration directory")
option(WORMEDIT_FFMPEG_LOCAL_ONLY
    "Package a host FFmpeg (e.g. Homebrew) without redistribution notices; local test builds only" OFF)
if(TARGET PkgConfig::WORMEDIT_FFMPEG)
    set(wormedit_ffmpeg_notice_dir "${WORMEDIT_FFMPEG_NOTICES_DIR}")
    if(NOT wormedit_ffmpeg_notice_dir)
        foreach(directory IN LISTS WORMEDIT_FFMPEG_LIBRARY_DIRS)
            if(IS_DIRECTORY "${directory}/../share/wormedit-ffmpeg")
                set(wormedit_ffmpeg_notice_dir "${directory}/../share/wormedit-ffmpeg")
                break()
            endif()
        endforeach()
    endif()
    if(wormedit_ffmpeg_notice_dir)
        # Require the source/license contract produced by our FFmpeg SDK builds.
        foreach(notice COPYING.LGPLv2.1 LICENSE.md config.h config.log)
            if(NOT EXISTS "${wormedit_ffmpeg_notice_dir}/${notice}")
                message(FATAL_ERROR "Missing FFmpeg redistribution notice: ${wormedit_ffmpeg_notice_dir}/${notice}")
            endif()
        endforeach()
        file(GLOB wormedit_ffmpeg_source_archives "${wormedit_ffmpeg_notice_dir}/ffmpeg-*.tar.xz")
        if(NOT wormedit_ffmpeg_source_archives)
            message(FATAL_ERROR "FFmpeg notices must include the corresponding ffmpeg-*.tar.xz source archive")
        endif()
        install(DIRECTORY "${wormedit_ffmpeg_notice_dir}/"
            DESTINATION "${wormedit_notice_root}/ffmpeg")
    elseif(WORMEDIT_FFMPEG_LOCAL_ONLY)
        install(FILES "${CMAKE_CURRENT_LIST_DIR}/notices/FFmpeg-LOCAL-ONLY.txt"
            DESTINATION "${wormedit_notice_root}/ffmpeg")
    else()
        install(CODE "message(FATAL_ERROR \"Set WORMEDIT_FFMPEG_NOTICES_DIR before distributing this FFmpeg build.\")")
    endif()
endif()

wormedit_install_notices(fonts "${CMAKE_SOURCE_DIR}/assets/fonts/LICENSE"
    "${CMAKE_SOURCE_DIR}/assets/fonts/NotoSansCJK-LICENSE"
    "${CMAKE_SOURCE_DIR}/assets/fonts/README.md")
wormedit_install_notices(lucide "${CMAKE_SOURCE_DIR}/assets/icons/lucide/LICENSE")
wormedit_install_notices(hitsounds "${CMAKE_SOURCE_DIR}/assets/hitsounds/LICENSE.md")
wormedit_install_notices(field-themes "${CMAKE_SOURCE_DIR}/assets/themes/field/IBMSC-PROVENANCE.md")
wormedit_install_notices(interface-themes "${CMAKE_SOURCE_DIR}/assets/themes/licenses/NORD-LICENSE.txt"
    "${CMAKE_SOURCE_DIR}/assets/themes/licenses/CATPPUCCIN-LICENSE.txt"
    "${CMAKE_SOURCE_DIR}/assets/themes/licenses/SOLARIZED-LICENSE.txt"
    "${CMAKE_SOURCE_DIR}/assets/themes/licenses/OSU-LICENSE.txt"
    "${CMAKE_SOURCE_DIR}/assets/themes/INTERFACE-PROVENANCE.md")
# Embedded dependencies retain their notices in these source files. Copy the
# complete files to preserve the text without a second license parser.
wormedit_install_notices(libotoworm/minizip
    "${WORMEDIT_OTOWORM_SOURCE_DIR}/src-thirdparty/minizip/unzip.h"
    "${WORMEDIT_OTOWORM_SOURCE_DIR}/src-thirdparty/minizip/ioapi.h")
wormedit_install_notices(libotoworm/utf8 "${WORMEDIT_OTOWORM_SOURCE_DIR}/src-thirdparty/utf8.h")
wormedit_install_notices(libotoworm/json "${WORMEDIT_OTOWORM_SOURCE_DIR}/src-thirdparty/json.hpp")
wormedit_install_notices(libotoworm/sha256
    "${WORMEDIT_OTOWORM_SOURCE_DIR}/src-thirdparty/sha256/sha256.h"
    "${WORMEDIT_OTOWORM_SOURCE_DIR}/src-thirdparty/sha256/sha256.cpp")
wormedit_install_notices(libotoworm/sha256 "${CMAKE_CURRENT_LIST_DIR}/notices/sha256.txt")
wormedit_install_notices(libotoworm "${CMAKE_CURRENT_LIST_DIR}/notices/libotoworm-MIT.txt")

if(WIN32 AND MINGW)
    # The Windows toolchain links libgcc/libstdc++ statically. Their exception
    # permits this distribution; retain it together with the underlying GPL.
    wormedit_install_notices(gcc-runtime
        "${CMAKE_CURRENT_LIST_DIR}/notices/GCC-RUNTIME-LIBRARY-EXCEPTION.txt"
        "${CMAKE_CURRENT_LIST_DIR}/notices/GPL-3.0.txt")
    find_path(WORMEDIT_MINGW_NOTICES_DIR NAMES COPYING.MinGW-w64-runtime.txt copyright
        PATHS /usr/share/licenses/mingw-w64-crt /usr/share/doc/mingw-w64-common
        NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH
        DOC "Copyright/license directory for this MinGW runtime SDK")
    if(WORMEDIT_MINGW_NOTICES_DIR)
        file(GLOB wormedit_mingw_notices "${WORMEDIT_MINGW_NOTICES_DIR}/*.txt"
            "${WORMEDIT_MINGW_NOTICES_DIR}/copyright")
        wormedit_install_notices(mingw-runtime ${wormedit_mingw_notices})
    else()
        install(CODE "message(FATAL_ERROR \"Set WORMEDIT_MINGW_NOTICES_DIR for this MinGW SDK.\")")
    endif()
endif()
