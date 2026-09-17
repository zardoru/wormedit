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
set(WORMEDIT_UI_BACKEND "nuklear" CACHE STRING "Editor UI backend (nuklear, imgui, or clay)")
set_property(CACHE WORMEDIT_UI_BACKEND PROPERTY STRINGS nuklear imgui clay)
if(NOT WORMEDIT_UI_BACKEND MATCHES "^(nuklear|imgui|clay)$")
    message(FATAL_ERROR
        "Unsupported WORMEDIT_UI_BACKEND='${WORMEDIT_UI_BACKEND}'; choose nuklear, imgui, or clay"
    )
endif()
if(WORMEDIT_BUILD_APP AND NOT WORMEDIT_UI_BACKEND STREQUAL "nuklear")
    message(FATAL_ERROR
        "WORMEDIT_UI_BACKEND='${WORMEDIT_UI_BACKEND}' has no adapter yet; currently available: nuklear"
    )
endif()

add_library(wormedit_core
    src/core/autosave_loop.cpp
    src/core/bms_branch_editor.cpp
    src/core/bpm_editor.cpp
    src/core/chart_compare.cpp
    src/core/command.cpp
    src/core/command_registry.cpp
    src/core/document.cpp
    src/core/editor_scene.cpp
    src/core/bms_profile_detection.cpp
    src/core/bga_preview.cpp
    src/core/grid.cpp
    src/core/interaction.cpp
    src/core/lane_profiles.cpp
    src/core/marker_editor.cpp
    src/core/marker_navigation.cpp
    src/core/metadata_editor.cpp
    src/core/measure.cpp
    src/core/measure_editor.cpp
    src/core/note_label_editor.cpp
    src/core/pause_warp_editor.cpp
    src/core/quantize.cpp
    src/core/rational.cpp
    src/core/recovery_controller.cpp
    src/core/scroll_speed_editor.cpp
    src/core/semantic_layer_editor.cpp
    src/core/shortcut_profile_storage.cpp
    src/core/shortcut_profile_startup.cpp
    src/core/storyboard.cpp
    src/core/sv_recipe.cpp
    src/core/startup_session.cpp
    src/core/task_graph.cpp
    src/core/timing.cpp
    src/core/timing_anchor_editor.cpp
    src/core/timing_lane_controller.cpp
    src/core/viewport_input.cpp
    src/core/workspace.cpp
    src/formats/bms.cpp
    src/formats/bms_writer.cpp
    src/formats/bmson.cpp
    src/formats/bmson_writer.cpp
    src/formats/native_project.cpp
    src/formats/native_project_storage.cpp
    src/formats/osu_mania.cpp
    src/formats/stepmania.cpp
)
add_library(wormedit::core ALIAS wormedit_core)
target_include_directories(wormedit_core PUBLIC "${CMAKE_SOURCE_DIR}/include")
target_compile_features(wormedit_core PUBLIC cxx_std_20)
target_link_libraries(wormedit_core PRIVATE nlohmann_json::nlohmann_json)
find_package(Threads REQUIRED)
target_link_libraries(wormedit_core PUBLIC Threads::Threads)

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

    # libotoworm's conversion bridge selects iconv through its legacy LINUX
    # preprocessor symbol.  Define it at the dependency boundary on Linux so
    # its already-required Iconv target and its headers agree.
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_compile_definitions(otoworm PRIVATE LINUX)
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

    # The application links one private, stable frontend target. Backend types
    # and dependencies do not enter wormedit_core or its public headers.
    add_library(wormedit_ui_frontend INTERFACE)
    add_library(wormedit::ui_frontend ALIAS wormedit_ui_frontend)
    target_link_libraries(wormedit_ui_frontend INTERFACE nuklear_headers)

    add_executable(wormedit
        src/common/chart.cpp
        src/editor/field.cpp
        src/sys/audio.cpp
        src/sys/main.cpp
        src/sys/platform_recovery.cpp
        src/sys/platform_shortcut_profile.cpp
        src/sys/window.cpp
        src/ui/nuklear/frontend.cpp
    )

    target_include_directories(wormedit
        PRIVATE
            "${CMAKE_SOURCE_DIR}/include"
            "${CMAKE_SOURCE_DIR}/src"
            "${CMAKE_SOURCE_DIR}/src/vendor"
            "${CMAKE_BINARY_DIR}/generated"
    )

    target_link_libraries(wormedit
        PRIVATE
            wormedit::core
            wormedit::ui_frontend
            SDL3::SDL3
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
    add_test(
        NAME architecture.ui_frontend_boundary
        COMMAND "${CMAKE_COMMAND}"
            -DWORMEDIT_SOURCE_ROOT=${CMAKE_SOURCE_DIR}
            -P "${CMAKE_SOURCE_DIR}/cmake/check_ui_boundary.cmake"
    )
    set_tests_properties(architecture.ui_frontend_boundary PROPERTIES LABELS "unit;architecture")
endif()
