cmake_minimum_required(VERSION 3.25)

project(wormedit
    VERSION 0.1.0
    DESCRIPTION "Cross-platform rhythm chart editor"
    LANGUAGES C CXX
)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
include(FetchContent)

set(FETCHCONTENT_UPDATES_DISCONNECTED ON)

include(cmake/dependencies/nlohmann_json.cmake)

option(WORMEDIT_BUILD_APP "Build the existing SDL/OpenGL application scaffold" ON)
option(WORMEDIT_ENABLE_LUA "Build the pinned Lua runtime adapter" OFF)
option(WORMEDIT_ENABLE_AUDIO_DECODER "Build the headless MP3/Ogg decoder target" ON)

add_library(wormedit_core
    src/core/command.cpp
    src/core/document.cpp
    src/core/bms_profile_detection.cpp
    src/core/lane_profiles.cpp
    src/core/measure.cpp
    src/core/quantize.cpp
    src/core/rational.cpp
    src/formats/bms.cpp
    src/formats/bmson.cpp
)
add_library(wormedit::core ALIAS wormedit_core)
target_include_directories(wormedit_core PUBLIC "${CMAKE_SOURCE_DIR}/include")
target_compile_features(wormedit_core PUBLIC cxx_std_20)
target_link_libraries(wormedit_core PRIVATE nlohmann_json::nlohmann_json)

find_package(Iconv QUIET)
if(Iconv_FOUND)
    target_link_libraries(wormedit_core PRIVATE Iconv::Iconv)
    target_compile_definitions(wormedit_core PRIVATE WORMEDIT_HAS_ICONV=1)
endif()

if(MSVC)
    target_compile_options(wormedit_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(wormedit_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WORMEDIT_ENABLE_LUA)
    include(cmake/dependencies/lua.cmake)
endif()

if(WORMEDIT_ENABLE_AUDIO_DECODER)
    include(cmake/dependencies/miniaudio.cmake)
endif()

if(WORMEDIT_BUILD_APP)
    if(EXISTS "${CMAKE_SOURCE_DIR}/build/_deps/sdl3-src/CMakeLists.txt")
        set(WORMEDIT_DEFAULT_SDL3_SOURCE_DIR "${CMAKE_SOURCE_DIR}/build/_deps/sdl3-src")
    endif()
    if(EXISTS "${CMAKE_SOURCE_DIR}/build/_deps/nuklear-src/nuklear.h")
        set(WORMEDIT_DEFAULT_NUKLEAR_SOURCE_DIR "${CMAKE_SOURCE_DIR}/build/_deps/nuklear-src")
    endif()

    set(SDL3_SOURCE_DIR "${WORMEDIT_DEFAULT_SDL3_SOURCE_DIR}" CACHE PATH "Local SDL3 source directory")
    set(nuklear_SOURCE_DIR "${WORMEDIT_DEFAULT_NUKLEAR_SOURCE_DIR}" CACHE PATH "Local Nuklear source directory")

    if(SDL3_SOURCE_DIR)
        FetchContent_Declare(SDL3 SOURCE_DIR "${SDL3_SOURCE_DIR}")
    else()
        FetchContent_Declare(
            SDL3
            GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
            GIT_TAG e0b12bbcea659c5f60d7b1e7797902600b5a8a52
        )
    endif()

    if(nuklear_SOURCE_DIR)
        FetchContent_Declare(nuklear SOURCE_DIR "${nuklear_SOURCE_DIR}")
    else()
        FetchContent_Declare(
            nuklear
            GIT_REPOSITORY https://github.com/Immediate-Mode-UI/Nuklear.git
            GIT_TAG 2667d0a19a8251f780d3326bcbda265a8d060ed4
        )
    endif()

    FetchContent_MakeAvailable(SDL3 nuklear)

    find_package(OpenGL REQUIRED)
    find_package(Git QUIET)

    if(NOT TARGET otoworm::otoworm)
        add_subdirectory("${CMAKE_SOURCE_DIR}/../libotoworm" "${CMAKE_BINARY_DIR}/_deps/libotoworm-build")
    endif()

    set(WORMEDIT_GIT_HASH "nogit")
    if(Git_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            OUTPUT_VARIABLE WORMEDIT_GIT_HASH_RESULT
            ERROR_QUIET
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if(WORMEDIT_GIT_HASH_RESULT)
            set(WORMEDIT_GIT_HASH "${WORMEDIT_GIT_HASH_RESULT}")
        endif()
    endif()

    configure_file(
        "${CMAKE_SOURCE_DIR}/config.h.in"
        "${CMAKE_BINARY_DIR}/generated/wormedit/config.h"
        @ONLY
    )

    add_library(nuklear_headers INTERFACE)
    target_include_directories(nuklear_headers INTERFACE "${nuklear_SOURCE_DIR}")

    add_executable(wormedit
        src/common/chart.cpp
        src/editor/field.cpp
        src/sys/audio.cpp
        src/sys/main.cpp
        src/sys/vendorimpl.cpp
        src/sys/window.cpp
    )

    target_include_directories(wormedit
        PRIVATE
            "${CMAKE_SOURCE_DIR}/include"
            "${CMAKE_SOURCE_DIR}/src/vendor"
            "${CMAKE_BINARY_DIR}/generated"
    )

    target_link_libraries(wormedit
        PRIVATE
            wormedit::core
            SDL3::SDL3
            nuklear_headers
            otoworm::otoworm
            OpenGL::GL
    )

    if(MSVC)
        target_compile_options(wormedit PRIVATE /W4 /permissive-)
    else()
        target_compile_options(wormedit PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

if(BUILD_TESTING)
    include(cmake/dependencies/catch2.cmake)
    add_subdirectory(tests)
endif()
