cmake_minimum_required(VERSION 3.25)

if(POLICY CMP0177)
    cmake_policy(SET CMP0177 NEW)
endif()

project(wormedit
    VERSION 1.0.0
    DESCRIPTION "Cross-platform rhythm chart editor"
    LANGUAGES C CXX
)

set(WORMEDIT_VERSION_SUFFIX "-rc2" CACHE STRING "Release suffix (empty for a final release)")
if(NOT WORMEDIT_VERSION_SUFFIX MATCHES "^[-.0-9A-Za-z]*$")
    message(FATAL_ERROR "WORMEDIT_VERSION_SUFFIX may contain only [-.0-9A-Za-z]")
endif()
set(WORMEDIT_VERSION_FULL "${PROJECT_VERSION}${WORMEDIT_VERSION_SUFFIX}")
find_package(Git QUIET)
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

configure_file("${CMAKE_SOURCE_DIR}/config.h.in" "${CMAKE_BINARY_DIR}/config.h" @ONLY)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
include(FetchContent)

set(FETCHCONTENT_UPDATES_DISCONNECTED ON)

include(cmake/dependencies/nlohmann_json.cmake)
include(cmake/warnings.cmake)

option(WORMEDIT_BUILD_APP "Build the SDL3 GPU application" ON)
option(WORMEDIT_BUILD_EVENT_HARNESS "Build windowless frontend event tests (requires BUILD_TESTING)" OFF)
if(APPLE AND WORMEDIT_BUILD_APP)
    enable_language(OBJCXX)
endif()

option(WORMEDIT_ENABLE_LUA "Build the pinned Lua runtime adapter" OFF)
option(WORMEDIT_ENABLE_AUDIO_DECODER "Build the headless MP3/Ogg decoder target" ON)
option(WORMEDIT_ENABLE_IMAGE_DECODER "Build the headless bounded static-image decoder target" ON)
option(WORMEDIT_WINDOWS_CONSOLE "Keep a console window attached to the Windows executable" OFF)
option(WORMEDIT_ENABLE_VIDEO_DECODER "Build the optional bounded FFmpeg video-frame decoder" ON)
option(WORMEDIT_ENABLE_FFMPEG "Enable optional FFmpeg audio metadata prefill" ON)
option(WORMEDIT_BUILD_WASM_SMOKE "Build the post-1.0 browser codec smoke target" OFF)
if(WORMEDIT_BUILD_WASM_SMOKE)
    if(NOT EMSCRIPTEN)
        message(FATAL_ERROR "WORMEDIT_BUILD_WASM_SMOKE requires the wasm-smoke preset and Emscripten")
    endif()
    if(WORMEDIT_BUILD_APP OR WORMEDIT_BUILD_EVENT_HARNESS OR BUILD_TESTING OR
       WORMEDIT_ENABLE_LUA OR WORMEDIT_ENABLE_AUDIO_DECODER OR WORMEDIT_ENABLE_IMAGE_DECODER OR
       WORMEDIT_ENABLE_VIDEO_DECODER OR WORMEDIT_ENABLE_FFMPEG)
        message(FATAL_ERROR "The WASM smoke lane supports the core codec only; use the wasm-smoke preset")
    endif()
endif()
if(EMSCRIPTEN)
    if(WORMEDIT_ENABLE_FFMPEG OR WORMEDIT_ENABLE_VIDEO_DECODER)
        message(FATAL_ERROR "The browser target does not support host FFmpeg; disable FFmpeg/video decoding")
    endif()
    # The native codec catches parse/allocation failures. Retain those catches in WASM too.
    add_compile_options(-fwasm-exceptions -pthread)
    add_link_options(-fwasm-exceptions -pthread)
    # EM_ASM's documented $0 placeholders are valid SDK syntax in these units.
    set_source_files_properties(src/core/atomic_file_storage.cpp src/core/resource_file_copy.cpp
        src/formats/native_project_storage.cpp src/formats/slice_span_render.cpp
        src/formats/conversion_publication_storage.cpp
        src/common/browser_files.cpp src/sys/app/main.cpp tests/wasm/storage.cpp
        PROPERTIES COMPILE_OPTIONS "-Wno-dollar-in-identifier-extension")
    include(CheckCXXSourceCompiles)
    include(CMakePushCheckState)
    cmake_push_check_state(RESET)
    set(CMAKE_REQUIRED_FLAGS "-std=c++23 -fwasm-exceptions -pthread")
    check_cxx_source_compiles("
        #include <expected>
        #include <functional>
        #include <stop_token>
        #include <thread>
        int main() {
            std::stop_source source;
            std::jthread thread;
            std::expected<int, int> value{std::bind_back(std::plus<int>{}, 2)(1)};
            return source.stop_requested() || thread.joinable() || *value != 3;
        }" WORMEDIT_WASM_HAS_CXX23)
    cmake_pop_check_state()
    if(NOT WORMEDIT_WASM_HAS_CXX23)
        message(FATAL_ERROR "The WASM core requires C++23 libc++ with expected, bind_back, stop_token and jthread; use a recent Emscripten SDK (verified: 6.0.5)")
    endif()
endif()
option(WORMEDIT_REQUIRE_FFMPEG "Require FFmpeg development libraries for shipped builds" OFF)
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
    src/core/audio_offset_editor.cpp
    src/core/autosave_loop.cpp
    src/core/command_journal.cpp
    src/core/command_journal_runtime.cpp
    src/core/bms_branch_editor.cpp
    src/core/bms_program.cpp
    src/core/bms_program_projection.cpp
    src/core/bms_native_content.cpp
    src/core/bms_program_edit.cpp
    src/core/bms_program_identity.cpp
    src/core/bms_program_catalog.cpp
    src/core/bms_program_timing.cpp
    src/core/bms_branch_view.cpp
    src/core/bmson_lane_mode.cpp
    src/core/bmson_sound_channel_editor.cpp
    src/core/sound_selection.cpp
    src/core/note_to_bgm.cpp
    src/core/timing_reposition.cpp
    src/core/bpm_editor.cpp
    src/core/chart_compare.cpp
    src/core/command.cpp
    src/core/command_registry.cpp
    src/core/chart_clipboard_recipes.cpp
    src/core/chart_clipboard.cpp
    src/core/document.cpp
    src/core/sound_groups.cpp
    src/core/sound_group_proposal.cpp
    src/core/editor_navigation_input.cpp
    src/core/editor_scene.cpp
    src/core/lane_presentation.cpp
    src/core/bms_profile_detection.cpp
    src/core/bga_preview.cpp
    src/core/bmson_layer_defaults.cpp
    src/core/format_field_schema.cpp
    src/core/grid.cpp
    src/core/resource_paint_gesture.cpp
    src/core/interaction.cpp
    src/core/note_placement.cpp
    src/core/note_move.cpp
    src/core/interaction_gesture_lifecycle.cpp
    src/core/interaction_cursor.cpp
    src/core/interaction_hit_test.cpp
    src/core/interaction_selection.cpp
    src/core/lane_profiles.cpp
    src/core/marker_editor.cpp
    src/core/song_preview.cpp
    src/core/export_cut.cpp
    src/core/export_cut_axis.cpp
    src/core/export_cut_frames.cpp
    src/core/export_cut_transform.cpp
    src/core/export_cut_transform_scoped.cpp
    src/core/kiai_spans.cpp
    src/core/marker_navigation.cpp
    src/core/timing_links.cpp
    src/core/metadata_inheritance.cpp
    src/core/metadata_editor.cpp
    src/core/natural_order.cpp
    src/core/chart_editor.cpp
    src/core/chart_import.cpp
    src/core/measure.cpp
    src/core/lane_layout_editor.cpp
    src/core/measure_editor.cpp
    src/core/lane_transform.cpp
    src/core/note_label_editor.cpp
    src/core/hitsound_editor.cpp
    src/core/osu_hitsample.cpp
    src/core/pause_warp_editor.cpp
    src/core/player_preview.cpp
    src/core/quantize.cpp
    src/core/snap.cpp
    src/core/dead_span_rules.cpp
    src/core/span_manipulation.cpp
    src/core/rational.cpp
    src/core/recovery_controller.cpp
    src/core/bgm_columns.cpp
    src/core/lane_profile_switch.cpp
    src/core/lane_migration.cpp
    src/core/channel_map_editor.cpp
    src/core/resource_assignment.cpp
    src/core/resource_file_copy.cpp
    src/core/scroll_speed_editor.cpp
    src/core/sample_control_editor.cpp
    src/core/semantic_layer_editor.cpp
    src/core/shortcut_profile_storage.cpp
    src/core/atomic_file_storage.cpp
    src/core/preferences.cpp
    src/core/ui_layout_preferences.cpp
    src/core/preview_mix.cpp
    src/core/project_settings.cpp
    src/core/lane_keys.cpp
    src/core/shortcut_profile_startup.cpp
    src/core/sound_slice.cpp
    src/core/audio_slicer.cpp
    src/core/sound_import.cpp
    src/core/arena.cpp
    src/core/notifications.cpp
    src/core/storyboard.cpp
    src/core/storyboard_preview.cpp
    src/core/sv_recipe_definitions.cpp
    src/core/sv_recipe.cpp
    src/core/generator.cpp
    src/core/generator_chart.cpp
    src/core/startup_session.cpp
    src/core/task_graph.cpp
    src/core/time_shift.cpp
    src/core/tap_tempo.cpp
    src/core/tap_timing_apply.cpp
    src/core/timing.cpp
    src/core/timing_discontinuity.cpp
    src/core/timing_anchor_editor.cpp
    src/core/timing_lane_controller.cpp
    src/core/viewport_input.cpp
    src/core/virtual_canvas.cpp
    src/core/video_preview.cpp
    src/core/visual_layout.cpp
    src/core/workspace.cpp
    src/formats/bms.cpp
    src/formats/bms_program_import.cpp
    src/formats/bms_program_document.cpp
    src/formats/bms_clipboard.cpp
    src/formats/bms_writer.cpp
    src/formats/bms_program_writer.cpp
    src/formats/bmson.cpp
    src/formats/bmson_writer.cpp
    src/formats/conversion.cpp
    src/formats/slice_spans.cpp
    src/formats/conversion_publication.cpp
    src/formats/conversion_publication_storage.cpp
    src/formats/native_project.cpp
    src/formats/native_project_delta.cpp
    src/formats/native_project_migrations.cpp
    src/formats/native_project_storage.cpp
    src/formats/osu_mania.cpp
    src/formats/stepmania.cpp
)
add_library(wormedit::core ALIAS wormedit_core)
target_include_directories(wormedit_core PUBLIC "${CMAKE_SOURCE_DIR}/include")
target_compile_features(wormedit_core PUBLIC cxx_std_23)
target_link_libraries(wormedit_core PRIVATE nlohmann_json::nlohmann_json)
find_package(Threads REQUIRED)
target_link_libraries(wormedit_core PUBLIC Threads::Threads)

if(WIN32)
    # No iconv on Windows: legacy BMS code pages go through the Win32 NLS API.
    target_sources(wormedit_core PRIVATE src/formats/win32_codepage.cpp)
    target_include_directories(wormedit_core PRIVATE "${CMAKE_SOURCE_DIR}/src/formats")
    # Keep <windows.h> from defining min/max macros that break std::min/std::max.
    target_compile_definitions(wormedit_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# Headless converter: the editor's conversion service behind a JSON request.
add_executable(wormedit-convert
    src/tools/wormedit_convert.cpp
    src/sys/project/project_open.cpp
)
target_include_directories(wormedit-convert PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(wormedit-convert PRIVATE wormedit::core nlohmann_json::nlohmann_json)
wormedit_enable_warnings(wormedit-convert)

# Manual reference tables generated from the command registry and layout catalog.
add_executable(wormedit-manual-reference
    src/tools/wormedit_manual_reference.cpp
    src/sys/commands/shortcut_rows.cpp
)
target_include_directories(wormedit-manual-reference PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(wormedit-manual-reference PRIVATE wormedit::core)
wormedit_enable_warnings(wormedit-manual-reference)
add_custom_target(manual-reference
    COMMAND "${CMAKE_COMMAND}" -DWORMEDIT_SOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -DWORMEDIT_MANUAL_REFERENCE=$<TARGET_FILE:wormedit-manual-reference> -DWORMEDIT_WRITE=ON
        -P "${CMAKE_SOURCE_DIR}/cmake/check_manual_reference.cmake"
    DEPENDS wormedit-manual-reference
    COMMENT "Regenerating docs/manual/generated"
)

find_package(Iconv QUIET)
if(Iconv_FOUND)
    target_link_libraries(wormedit_core PRIVATE Iconv::Iconv)
    if(EMSCRIPTEN)
        if(NOT Iconv_IS_BUILT_IN)
            message(FATAL_ERROR "The browser lane requires target-libc iconv, not a host iconv library")
        endif()
        set_property(SOURCE src/formats/bms.cpp src/formats/bms_writer.cpp APPEND PROPERTY
            COMPILE_DEFINITIONS WORMEDIT_HAS_ICONV=1)
    else()
        target_compile_definitions(wormedit_core PRIVATE WORMEDIT_HAS_ICONV=1)
    endif()
endif()

wormedit_enable_warnings(wormedit_core)

if(WORMEDIT_BUILD_WASM_SMOKE)
    add_executable(wormedit-wasm-smoke src/tools/wasm_smoke.cpp)
    target_link_libraries(wormedit-wasm-smoke PRIVATE wormedit::core)
    wormedit_enable_warnings(wormedit-wasm-smoke)
    set_target_properties(wormedit-wasm-smoke PROPERTIES SUFFIX ".js")
    target_link_options(wormedit-wasm-smoke PRIVATE
        --bind --no-entry
        -sMODULARIZE=1 -sEXPORT_NAME=createWormeditSmoke -sENVIRONMENT=web
        -sALLOW_MEMORY_GROWTH=1 -sMAXIMUM_MEMORY=268435456 -sSTACK_SIZE=5242880
        "--preload-file=${CMAKE_SOURCE_DIR}/tests/fixtures/bmson/slicing-and-mixed-sounds.bmson@/sample.bmson"
    )
    set_property(TARGET wormedit-wasm-smoke APPEND PROPERTY LINK_DEPENDS
        "${CMAKE_SOURCE_DIR}/tests/fixtures/bmson/slicing-and-mixed-sounds.bmson")
    configure_file(packaging/wasm/index.html "${CMAKE_BINARY_DIR}/index.html" COPYONLY)
endif()

if(WORMEDIT_ENABLE_LUA)
    include(cmake/dependencies/lua.cmake)
    add_library(wormedit_scripting
        src/scripting/lua_sandbox.cpp
        src/scripting/lua_curve.cpp
        src/scripting/lua_document_api.cpp
        src/scripting/lua_project.cpp
        src/scripting/chart_lua.cpp
        src/scripting/lua_generator.cpp
    )
    add_library(wormedit::scripting ALIAS wormedit_scripting)
    target_include_directories(wormedit_scripting PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_compile_features(wormedit_scripting PUBLIC cxx_std_23)
    target_link_libraries(wormedit_scripting PUBLIC Threads::Threads wormedit::core PRIVATE Lua::Lua nlohmann_json::nlohmann_json)
    target_compile_definitions(wormedit_scripting PUBLIC WORMEDIT_HAS_LUA=1)
    target_link_libraries(wormedit-convert PRIVATE wormedit::scripting)
    wormedit_enable_warnings(wormedit_scripting)
endif()

if(WORMEDIT_ENABLE_AUDIO_DECODER)
    include(cmake/dependencies/miniaudio.cmake)
    if(EMSCRIPTEN)
        # Emscripten's C EM_ASM macro requires GNU variadic-macro extensions.
        set_target_properties(wormedit_audio_decoder PROPERTIES C_EXTENSIONS ON)
        target_compile_definitions(wormedit_audio_decoder PRIVATE MA_NO_DEVICE_IO)
    endif()
    include(cmake/dependencies/vorbis.cmake)
    target_sources(wormedit_audio_decoder PRIVATE
        src/formats/slice_span_render.cpp
        src/formats/export_cut_audio.cpp
        src/audio/playback_rate.cpp
        src/audio/time_stretch.cpp
        src/audio/onset.cpp
        src/audio/offline_mix.cpp
        src/audio/metronome.cpp
        src/audio/hitsound_resolver.cpp
        src/audio/ogg_encoder.cpp
        src/audio/stream_encoder.cpp
    )
    target_link_libraries(wormedit_audio_decoder PRIVATE vorbisenc Ogg::ogg)
    target_link_libraries(wormedit-convert PRIVATE wormedit::audio_decoder)
    target_compile_definitions(wormedit-convert PRIVATE WORMEDIT_HAS_AUDIO_DECODER=1)
endif()

if(WORMEDIT_ENABLE_IMAGE_DECODER OR WORMEDIT_ENABLE_VIDEO_DECODER)
    add_library(wormedit_visual_assets src/visual/visual_asset.cpp)
    add_library(wormedit::visual_assets ALIAS wormedit_visual_assets)
    target_include_directories(wormedit_visual_assets PUBLIC "${CMAKE_SOURCE_DIR}/include")
    target_compile_features(wormedit_visual_assets PUBLIC cxx_std_23)
    target_link_libraries(wormedit_visual_assets PUBLIC wormedit::core)
    wormedit_enable_warnings(wormedit_visual_assets)
endif()

if(WORMEDIT_ENABLE_IMAGE_DECODER)
    include(cmake/dependencies/stb_image.cmake)
    add_library(wormedit_visual_decoder
        src/visual/image_decoder.cpp
        src/visual/image_job.cpp
    )
    add_library(wormedit::visual_decoder ALIAS wormedit_visual_decoder)
    target_include_directories(wormedit_visual_decoder PUBLIC "${CMAKE_SOURCE_DIR}/include")
    target_compile_features(wormedit_visual_decoder PUBLIC cxx_std_23)
    target_link_libraries(wormedit_visual_decoder PUBLIC wormedit::visual_assets PRIVATE stb::image)
    wormedit_enable_warnings(wormedit_visual_decoder)
endif()

if(WORMEDIT_ENABLE_FFMPEG OR WORMEDIT_ENABLE_VIDEO_DECODER OR WORMEDIT_REQUIRE_FFMPEG)
    set(WORMEDIT_FFMPEG_FIND_MODE QUIET)
    if(WORMEDIT_REQUIRE_FFMPEG)
        set(WORMEDIT_FFMPEG_FIND_MODE REQUIRED)
    endif()
    find_package(PkgConfig ${WORMEDIT_FFMPEG_FIND_MODE})
    if(PkgConfig_FOUND)
        pkg_check_modules(WORMEDIT_FFMPEG ${WORMEDIT_FFMPEG_FIND_MODE} IMPORTED_TARGET
            libavformat
            libavcodec
            libavutil
            libswscale
        )
    endif()
endif()

if(WORMEDIT_ENABLE_AUDIO_DECODER AND TARGET PkgConfig::WORMEDIT_FFMPEG)
    target_link_libraries(wormedit_audio_decoder PRIVATE PkgConfig::WORMEDIT_FFMPEG)
    set_source_files_properties(src/audio/stream_encoder.cpp PROPERTIES COMPILE_DEFINITIONS WORMEDIT_HAS_FFMPEG=1)
endif()

if(WORMEDIT_ENABLE_VIDEO_DECODER)
    if(TARGET PkgConfig::WORMEDIT_FFMPEG)
        add_library(wormedit_video_decoder
            src/visual/video_decoder.cpp
            src/visual/video_job.cpp
        )
        add_library(wormedit::video_decoder ALIAS wormedit_video_decoder)
        target_include_directories(wormedit_video_decoder PUBLIC "${CMAKE_SOURCE_DIR}/include")
        target_compile_features(wormedit_video_decoder PUBLIC cxx_std_23)
        target_link_libraries(wormedit_video_decoder
            PUBLIC wormedit::visual_assets
            PRIVATE PkgConfig::WORMEDIT_FFMPEG
        )
        wormedit_enable_warnings(wormedit_video_decoder)
    else()
        message(STATUS
            "FFmpeg development libraries were not found; optional video decoding is disabled"
        )
    endif()
endif()

if(WORMEDIT_BUILD_APP OR (BUILD_TESTING AND WORMEDIT_BUILD_EVENT_HARNESS))
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
            GIT_TAG cc2713c0477118d3074b625abe8df7de7118fe2e
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
    if(EMSCRIPTEN)
        set_target_properties(SDL3-static PROPERTIES C_EXTENSIONS ON)
    endif()
    include(cmake/sdl/bounded-presentation.cmake)
    wormedit_enable_bounded_sdl_presentation("${sdl3_SOURCE_DIR}")

    if(NOT EMSCRIPTEN)
        set(WORMEDIT_OTOWORM_SOURCE_DIR "${CMAKE_SOURCE_DIR}/third_party/libotoworm" CACHE PATH
            "libotoworm source directory (the third_party submodule by default)")
        if(NOT EXISTS "${WORMEDIT_OTOWORM_SOURCE_DIR}/CMakeLists.txt")
            message(FATAL_ERROR "libotoworm not found at ${WORMEDIT_OTOWORM_SOURCE_DIR}; "
                "run: git submodule update --init")
        endif()
        if(WIN32)
            include(cmake/dependencies/zlib.cmake)
        endif()
        if(NOT TARGET otoworm::otoworm)
            add_subdirectory("${WORMEDIT_OTOWORM_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/_deps/libotoworm-build")
        endif()

        # libotoworm is a separate project that builds as C++20; keep its own sources
        # there while wormedit uses C++23 (D14). Its C++23 fixes are kept for upstream.
        set_target_properties(otoworm PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)

        # libotoworm's conversion bridge selects iconv through its legacy LINUX
        # preprocessor symbol.  Define it at the dependency boundary on Linux so
        # its already-required Iconv target and its headers agree.
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            target_compile_definitions(otoworm PRIVATE LINUX)
        endif()
        # libotoworm tests the legacy WIN32 symbol, which only MSVC defines on its own.
        if(WIN32 AND NOT MSVC)
            target_compile_definitions(otoworm PRIVATE WIN32 MINGW)
        endif()
    endif()

    configure_file("${CMAKE_BINARY_DIR}/config.h"
        "${CMAKE_BINARY_DIR}/generated/wormedit/config.h" COPYONLY)

    add_library(nuklear_headers INTERFACE)
    if(MINGW)
        # Nuklear's printf attribute selects legacy MSVCRT formatting on MinGW,
        # but its vsnprintf implementation uses MinGW's C99 formatting (%zu, etc.).
        # Adapt a build-local copy; keep the pinned/shared dependency source intact.
        file(READ "${nuklear_SOURCE_DIR}/nuklear.h" wormedit_nuklear_header)
        string(REPLACE "format(__printf__," "format(__gnu_printf__,"
            wormedit_nuklear_header "${wormedit_nuklear_header}")
        file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/nuklear/nuklear.h"
            CONTENT "${wormedit_nuklear_header}" @ONLY)
        target_include_directories(nuklear_headers INTERFACE "${CMAKE_CURRENT_BINARY_DIR}/generated/nuklear")
        target_compile_definitions(nuklear_headers INTERFACE __USE_MINGW_ANSI_STDIO=1)
    else()
        target_include_directories(nuklear_headers INTERFACE "${nuklear_SOURCE_DIR}")
    endif()

    # The application links one private, stable frontend target. Backend types
    # and dependencies do not enter wormedit_core or its public headers.
    add_library(wormedit_ui_frontend INTERFACE)
    add_library(wormedit::ui_frontend ALIAS wormedit_ui_frontend)
    target_link_libraries(wormedit_ui_frontend INTERFACE nuklear_headers)

    set(WORMEDIT_UI_FRONTEND_SOURCES
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/chrome/tooltip.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/chrome/context_menu.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/chrome/dialog.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/project/chart_import_dialog.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/chrome/components.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/chrome/ui_gallery.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/audio/audio_panel.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/project/bmson_lane_mode_ui.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/preferences/external_player_panel.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/preferences/options_window.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/preferences/lane_keys_dialog.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/preferences/shortcuts_panel.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/chrome/notification_toasts.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/chrome/notification_panel.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/audio/tap_timing_panel.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/timing/timing_settings_panel.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/audio/audio_state.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/project/bms_branch_panel.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/render/arrow_glyph.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/render/font_atlas.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/compose/compose_bookmark_label.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/compose/compose_bookmark_chooser.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/compose/compose_sound_picker.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/app/command_menu.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/compose/compose_bookmark_rename.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/compose/compose_note_label_inline.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/compose/compose_bmson_tab.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/compose/compose_markers.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/compose/compose_export_cuts.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/compose/compose_area_menu.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/audio/preview_audio_dialog.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/compose/compose_recipe_menu.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/compose/compose_note_menu.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/compose/compose_recipe_form.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/compose/compose_canvas.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/compose/compose_toolbar.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/compose/compose_snap_grid.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/compose/compose_inspector.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/compose/compose_input_panel.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/compose/compose_keyboard_input.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/compose/compose_viewport_input.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/compose/compose_timing_input.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/compose/compose_object_input.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/compose/compose_panel.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/dock.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/project/compare_panel.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/compose/compose_resources.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/compose/compose_state.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/app/frontend.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/app/menu_bar.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/app/sdl_backend.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/render/sdl_command_mesh.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/render/sdl_region_cache.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/render/sdl_region_frame.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/field/field_renderer.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/field/field_tile_cache.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/field/field_glyphs.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/render/image_quad.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/render/lucide_icons.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/metadata/metadata_panel.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/metadata/metadata_audio_section.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/metadata/metadata_difficulty_section.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/metadata/metadata_inheritance_section.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/metadata/metadata_timing_links_section.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/metadata/metadata_format_fields.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/metadata/metadata_song_section.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/metadata/metadata_state.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/audio/player_preview.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/audio/player_preview_texture.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/project/setup_panel.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/project/setup_state.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/resources/sound_import_state.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/resources/sound_import_dialog.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/resources/sound_import_runtime.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/resources/keysound_list_state.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/compose/measure_resize.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/compose/compose_measure_resize_input.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/compose/compose_sounds_panel.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/resources/sound_slot_batch.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/compose/compose_bga_panel.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/compose/compose_measures_panel.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/compose/compose_layers_panel.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/compose/compose_mixer_panel.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/timing/timing_stage_editor.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/chrome/text_field.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/timing/timing_state.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/audio/meter_state.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/render/theme.cpp"
    )

endif()

if(WORMEDIT_BUILD_APP)
    add_executable(wormedit
        src/common/static_interval_index.cpp
        src/sys/audio/audio.cpp
        src/sys/app/application_defaults.cpp
        src/sys/commands/application_commands.cpp
        src/sys/compose/input/compose_minimap_navigation.cpp
        src/sys/bookmarks/bookmark_commands.cpp
        src/sys/compose/input/compose_navigation.cpp
        src/sys/bookmarks/bookmark_chooser_view.cpp
        src/sys/bookmarks/bookmark_navigation.cpp
        src/sys/bookmarks/bookmark_rename_commands.cpp
        src/sys/compose/input/compose_playhead_skip.cpp
        src/sys/compose/compose_bgm_bar.cpp
        src/sys/resources/bms_catalog_bindings.cpp
        src/sys/resources/sound_list_view.cpp
        src/sys/resources/sound_picker_view.cpp
        src/sys/resources/sound_usage_view.cpp
        src/sys/resources/bga_usage_view.cpp
        src/sys/resources/sound_usage_actions.cpp
        src/sys/resources/bga_usage_actions.cpp
        src/sys/compose/actions/compose_measure_commands.cpp
        src/sys/compose/actions/compose_selection_transform.cpp
        src/sys/compose/compose_label_search.cpp
        src/sys/compose/compose_dead_spans.cpp
        src/sys/compose/compose_kiai_spans.cpp
        src/sys/compose/compose_sv_graph.cpp
        src/sys/compose/views/compose_recipe_areas.cpp
        src/sys/compose/compose_speed_spans.cpp
        src/sys/compose/views/recipe_form.cpp
        src/sys/compose/actions/compose_quantize_preview.cpp
        src/sys/compose/actions/compose_quantize.cpp
        src/sys/commands/application_command_dispatch.cpp
        src/sys/compose/views/compose_resource_presenter.cpp
        src/sys/compose/views/compose_view.cpp
        src/sys/compose/views/compose_scene_view.cpp
        src/sys/compose/views/compose_marker_view.cpp
        src/sys/compose/views/compose_export_cut_view.cpp
        src/sys/compose/views/compose_cut_axis_view.cpp
        src/sys/compose/views/compose_selection_view.cpp
        src/sys/compose/views/compose_draw_source_view.cpp
        src/sys/compose/views/compose_hover_timing.cpp
        src/sys/project/compare_view.cpp
        src/sys/compose/input/compose_input.cpp
        src/sys/compose/input/compose_waveform_fit_input.cpp
        src/sys/compose/input/compose_timing_input.cpp
        src/sys/compose/input/compose_export_cut_input.cpp
        src/sys/compose/input/compose_resource_input.cpp
        src/sys/compose/input/compose_navigation_input.cpp
        src/sys/compose/actions/compose_note_to_bgm.cpp
        src/sys/compose/actions/compose_area_actions.cpp
        src/sys/compose/actions/compose_action_outcome.cpp
        src/sys/commands/document_edit_feedback.cpp
        src/sys/compose/actions/compose_actions.cpp
        src/sys/compose/actions/compose_resource_actions.cpp
        src/sys/compose/actions/compose_marker_actions.cpp
        src/sys/compose/actions/compose_bookmark_actions.cpp
        src/sys/compose/actions/compose_export_cut_actions.cpp
        src/sys/compose/actions/compose_measure_actions.cpp
        src/sys/compose/actions/compose_recipe_actions.cpp
        src/sys/compose/recipe_evaluation_runtime.cpp
        src/sys/compose/generator_runtime.cpp
        src/sys/compose/views/generator_view.cpp
        src/sys/compose/actions/compose_generator_actions.cpp
        src/ui/nuklear/compose/compose_generator_form.cpp
        src/sys/compose/actions/compose_transform_actions.cpp
        src/sys/compose/actions/compose_sound_actions.cpp
        src/sys/resources/sound_import_staging.cpp
        src/sys/compose/actions/compose_bga_actions.cpp
        src/sys/resources/bga_list_view.cpp
        src/sys/resources/bga_dock_runtime.cpp
        src/sys/compose/actions/compose_audition_requests.cpp
        src/sys/app/crash_backtrace.cpp
        src/sys/audio/audio_source_registration.cpp
        src/sys/audio/project_audio_sources.cpp
        src/sys/audio/metronome.cpp
        src/sys/app/application_frame_view.cpp
        src/sys/app/application_frame_services.cpp
        src/sys/resources/resource_drop_router.cpp
        src/sys/app/frame_pacing.cpp
        src/sys/app/display_health.cpp
        src/sys/app/display_watchdog.cpp
        src/sys/resources/sound_drop_transaction.cpp
        src/sys/app/application_startup.cpp
        src/sys/app/application_lifetime.cpp
        src/sys/project/recovery_notification_feedback.cpp
        src/sys/audio/audio_project_import.cpp
        src/sys/project/project_open.cpp
        src/sys/project/chart_import_session.cpp
        src/sys/project/project_save_paths.cpp
        src/sys/app/main.cpp
        src/sys/app/application.cpp
        src/sys/app/application_timing_stage.cpp
        src/sys/app/frame_timing.cpp
        src/sys/timing/timing_view.cpp
        src/sys/timing/timing_edit_runtime.cpp
        src/sys/metadata/metadata_drop_state.cpp
        src/sys/metadata/metadata_asset_runtime.cpp
        src/sys/metadata/metadata_actions.cpp
        src/sys/metadata/metadata_view.cpp
        src/sys/audio/waveform_view.cpp
        src/sys/audio/bmson_waveform_view.cpp
        src/sys/compose/views/compose_waveform_overlays.cpp
        src/sys/resources/resource_job_state.cpp
        src/sys/audio/preview_audio_export.cpp
        src/sys/audio/audio_slicer_runtime.cpp
        src/sys/project/output_group_export.cpp
        src/sys/audio/player_preview_presenter.cpp
        src/sys/project/workspace_view.cpp
        src/sys/bms_program_runtime.cpp
        src/sys/project/workspace_frame_view.cpp
        src/sys/audio/transport_player_actions.cpp
        src/sys/preferences/external_player.cpp
        src/sys/preferences/preferences_runtime.cpp
        src/sys/preferences/options_runtime.cpp
        src/sys/preferences/lane_keys_runtime.cpp
        src/sys/commands/shortcut_rows.cpp
        src/sys/commands/menu_command_view.cpp
        src/sys/notifications/notification_history_runtime.cpp
        src/sys/project/project_view.cpp
        src/sys/audio/tap_timing_runtime.cpp
        src/sys/app/platform_recovery.cpp
        src/sys/commands/platform_shortcut_profile.cpp
        src/sys/app/window.cpp
        ${WORMEDIT_UI_FRONTEND_SOURCES}
    )
    if(APPLE)
        target_sources(wormedit PRIVATE src/sys/app/macos_menu.mm)
        set_source_files_properties(src/sys/app/macos_menu.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
        set_target_properties(wormedit PROPERTIES OBJCXX_STANDARD 23 OBJCXX_STANDARD_REQUIRED ON)
        target_compile_definitions(wormedit PRIVATE WORMEDIT_HAS_MACOS_MENU=1)
        target_link_libraries(wormedit PRIVATE "-framework AppKit")
    endif()
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/assets/fonts")
    file(COPY "${CMAKE_SOURCE_DIR}/assets/hitsounds" DESTINATION "${CMAKE_BINARY_DIR}/assets")
    configure_file("${CMAKE_SOURCE_DIR}/assets/fonts/AdwaitaMono-Regular.ttf"
                   "${CMAKE_BINARY_DIR}/assets/fonts/AdwaitaMono-Regular.ttf" COPYONLY)
    foreach(font_asset NotoSansCJKjp-Regular.otf NotoSansCJK-LICENSE README.md)
        configure_file("${CMAKE_SOURCE_DIR}/assets/fonts/${font_asset}"
                       "${CMAKE_BINARY_DIR}/assets/fonts/${font_asset}" COPYONLY)
    endforeach()
    configure_file("${CMAKE_SOURCE_DIR}/assets/fonts/LICENSE"
                   "${CMAKE_BINARY_DIR}/assets/fonts/LICENSE" COPYONLY)
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/assets/themes")
    file(GLOB WORMEDIT_INTERFACE_THEMES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/assets/themes/ui-*.json")
    foreach(theme IN LISTS WORMEDIT_INTERFACE_THEMES)
        get_filename_component(theme_name "${theme}" NAME)
        configure_file("${theme}" "${CMAKE_BINARY_DIR}/assets/themes/${theme_name}" COPYONLY)
    endforeach()
    file(COPY "${CMAKE_SOURCE_DIR}/assets/themes/licenses" DESTINATION "${CMAKE_BINARY_DIR}/assets/themes")
    file(GLOB WORMEDIT_FIELD_SCHEMES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/assets/themes/field/*.json")
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/assets/themes/field")
    foreach(scheme IN LISTS WORMEDIT_FIELD_SCHEMES)
        get_filename_component(scheme_name "${scheme}" NAME)
        configure_file("${scheme}" "${CMAKE_BINARY_DIR}/assets/themes/field/${scheme_name}" COPYONLY)
    endforeach()
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/assets/icons/lucide")
    configure_file("${CMAKE_SOURCE_DIR}/assets/icons/lucide/LICENSE"
                   "${CMAKE_BINARY_DIR}/assets/icons/lucide/LICENSE" COPYONLY)

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
            nlohmann_json::nlohmann_json
    )
    if(NOT EMSCRIPTEN)
        target_link_libraries(wormedit PRIVATE otoworm::otoworm)
    endif()
    if(EMSCRIPTEN)
        target_sources(wormedit PRIVATE src/common/browser_files.cpp)
        set_target_properties(wormedit PROPERTIES SUFFIX ".js")
        target_link_options(wormedit PRIVATE
            -sMODULARIZE=1 -sEXPORT_NAME=createWormedit -sENVIRONMENT=web,worker
            -sPTHREAD_POOL_SIZE=16 -sDEFAULT_PTHREAD_STACK_SIZE=2097152
            -sPROXY_TO_PTHREAD=1 -sOFFSCREEN_FRAMEBUFFER=1 -sOFFSCREENCANVAS_SUPPORT=1
            -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=134217728 -sMAXIMUM_MEMORY=1073741824
            -sSTACK_SIZE=5242880 -sFORCE_FILESYSTEM=1 -sMAX_WEBGL_VERSION=2
            "-sEXPORTED_RUNTIME_METHODS=['FS','ccall']"
            "-sEXPORTED_FUNCTIONS=['_main','_malloc','_free','_wormedit_browser_dialog_complete']"
            "--preload-file=${CMAKE_SOURCE_DIR}/assets@/assets"
        )
        configure_file(packaging/wasm/editor.html "${CMAKE_BINARY_DIR}/index.html" COPYONLY)
        configure_file(packaging/wasm/editor-files.js "${CMAKE_BINARY_DIR}/editor-files.js" COPYONLY)
        include(cmake/dependencies/fflate.cmake)
        file(GLOB_RECURSE WORMEDIT_BROWSER_ASSETS CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/assets/*")
        set_property(TARGET wormedit APPEND PROPERTY LINK_DEPENDS ${WORMEDIT_BROWSER_ASSETS})
        add_executable(wormedit-wasm-storage EXCLUDE_FROM_ALL tests/wasm/storage.cpp)
        target_include_directories(wormedit-wasm-storage PRIVATE "${CMAKE_SOURCE_DIR}/src")
        target_link_libraries(wormedit-wasm-storage PRIVATE wormedit::core)
        wormedit_enable_warnings(wormedit-wasm-storage)
        set_target_properties(wormedit-wasm-storage PROPERTIES SUFFIX ".js")
        target_link_options(wormedit-wasm-storage PRIVATE
            -sMODULARIZE=1 -sEXPORT_NAME=createWormeditStorage -sENVIRONMENT=web,worker
            -sPROXY_TO_PTHREAD=1 -sPTHREAD_POOL_SIZE=4 -sDEFAULT_PTHREAD_STACK_SIZE=1048576
            -sINITIAL_MEMORY=33554432 -sFORCE_FILESYSTEM=1)
        configure_file(tests/wasm/storage.html "${CMAKE_BINARY_DIR}/storage.html" COPYONLY)
    endif()

    target_compile_definitions(wormedit PRIVATE
        $<$<CONFIG:Debug>:WORMEDIT_FRAME_ALLOCATION_HOOK=1>)

    if(UNIX AND NOT EMSCRIPTEN)
        set_target_properties(wormedit PROPERTIES ENABLE_EXPORTS ON)
    endif()

    if(WIN32 AND NOT WORMEDIT_WINDOWS_CONSOLE)
        set_target_properties(wormedit PROPERTIES WIN32_EXECUTABLE ON)
    endif()

    # SDL_GetBasePath() is the executable directory, so assets/ is installed next to it.
    if(WIN32)
        set(WORMEDIT_INSTALL_BINDIR ".")
    else()
        set(WORMEDIT_INSTALL_BINDIR "bin")
    endif()
    if(WIN32)
        include(cmake/windows-runtime.cmake)
    elseif(APPLE)
        include(cmake/macos-bundle.cmake)
    else()
        install(TARGETS wormedit RUNTIME DESTINATION "${WORMEDIT_INSTALL_BINDIR}")
    endif()
    install(FILES assets/fonts/AdwaitaMono-Regular.ttf assets/fonts/LICENSE
        assets/fonts/NotoSansCJKjp-Regular.otf assets/fonts/NotoSansCJK-LICENSE assets/fonts/README.md
        DESTINATION "${WORMEDIT_INSTALL_BINDIR}/assets/fonts")
    install(FILES ${WORMEDIT_INTERFACE_THEMES} DESTINATION "${WORMEDIT_INSTALL_BINDIR}/assets/themes")
    install(DIRECTORY assets/hitsounds DESTINATION "${WORMEDIT_INSTALL_BINDIR}/assets")
    install(DIRECTORY assets/themes/field DESTINATION "${WORMEDIT_INSTALL_BINDIR}/assets/themes"
        FILES_MATCHING PATTERN "*.json")
    install(FILES assets/icons/lucide/LICENSE
        DESTINATION "${WORMEDIT_INSTALL_BINDIR}/assets/icons/lucide")
    install(FILES LICENSE README.md DESTINATION "${WORMEDIT_INSTALL_BINDIR}")
    include(cmake/wormedit_manual.cmake)
    include(cmake/third-party-notices.cmake)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        install(FILES packaging/linux/wormedit.desktop DESTINATION "share/applications")
        install(FILES packaging/linux/wormedit.png DESTINATION "share/icons/hicolor/256x256/apps")
    endif()

    if(WORMEDIT_ENABLE_IMAGE_DECODER)
        target_link_libraries(wormedit PRIVATE wormedit::visual_decoder)
        target_compile_definitions(wormedit PRIVATE WORMEDIT_HAS_IMAGE_DECODER=1)
    endif()

    if(WORMEDIT_ENABLE_FFMPEG AND TARGET PkgConfig::WORMEDIT_FFMPEG)
        target_link_libraries(wormedit PRIVATE PkgConfig::WORMEDIT_FFMPEG)
        set_source_files_properties(src/sys/audio/audio_project_import.cpp TARGET_DIRECTORY wormedit
            PROPERTIES COMPILE_DEFINITIONS WORMEDIT_HAS_FFMPEG=1)
    endif()

    if(TARGET wormedit_video_decoder)
        target_link_libraries(wormedit PRIVATE wormedit::video_decoder)
        target_compile_definitions(wormedit PRIVATE WORMEDIT_HAS_VIDEO_DECODER=1)
    endif()

    if(WORMEDIT_ENABLE_AUDIO_DECODER)
        target_sources(wormedit PRIVATE
            src/sys/audio/chart_preview_state.cpp
            src/sys/audio/export_cut_preview.cpp
            src/sys/audio/audio_transport_frame.cpp
            src/sys/audio/waveform_analysis_runtime.cpp
            src/sys/audio/sdl_audition_output.cpp
            src/sys/audio/sdl_preview_output.cpp
            src/sys/audio/audio_options_runtime.cpp
        )
        target_link_libraries(wormedit PRIVATE wormedit::audio_decoder)
        target_compile_definitions(wormedit PRIVATE WORMEDIT_HAS_AUDIO_DECODER=1)
    endif()

    wormedit_enable_warnings(wormedit)
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
    add_test(
        NAME architecture.sdl_gpu_backend
        COMMAND "${CMAKE_COMMAND}"
            -DWORMEDIT_SOURCE_ROOT=${CMAKE_SOURCE_DIR}
            -P "${CMAKE_SOURCE_DIR}/cmake/check_sdl_gpu_backend.cmake"
    )
    set_tests_properties(architecture.sdl_gpu_backend PROPERTIES LABELS "unit;architecture")
    add_test(
        NAME documentation.chart_editor_state
        COMMAND "${CMAKE_COMMAND}"
            -DWORMEDIT_SOURCE_ROOT=${CMAKE_SOURCE_DIR}
            -P "${CMAKE_SOURCE_DIR}/cmake/check_chart_editor_docs.cmake"
    )
    set_tests_properties(documentation.chart_editor_state PROPERTIES LABELS "unit;documentation")
    add_test(
        NAME documentation.ceremony_ratchet
        COMMAND "${CMAKE_COMMAND}"
            -DWORMEDIT_SOURCE_ROOT=${CMAKE_SOURCE_DIR}
            -P "${CMAKE_SOURCE_DIR}/cmake/check_ceremony_ratchet.cmake"
    )
    set_tests_properties(documentation.ceremony_ratchet PROPERTIES LABELS "unit;documentation")
    add_test(
        NAME architecture.edit_execution
        COMMAND "${CMAKE_COMMAND}"
            -DWORMEDIT_SOURCE_ROOT=${CMAKE_SOURCE_DIR}
            -P "${CMAKE_SOURCE_DIR}/cmake/check_edit_execution.cmake"
    )
    set_tests_properties(architecture.edit_execution PROPERTIES LABELS "unit;architecture")
    add_test(
        NAME documentation.architecture_names
        COMMAND "${CMAKE_COMMAND}"
            -DWORMEDIT_SOURCE_ROOT=${CMAKE_SOURCE_DIR}
            -P "${CMAKE_SOURCE_DIR}/cmake/check_architecture_names.cmake"
    )
    set_tests_properties(documentation.architecture_names PROPERTIES LABELS "unit;documentation")
    add_test(
        NAME documentation.manual_reference
        COMMAND "${CMAKE_COMMAND}"
            -DWORMEDIT_SOURCE_ROOT=${CMAKE_SOURCE_DIR}
            -DWORMEDIT_MANUAL_REFERENCE=$<TARGET_FILE:wormedit-manual-reference>
            -P "${CMAKE_SOURCE_DIR}/cmake/check_manual_reference.cmake"
    )
    set_tests_properties(documentation.manual_reference PROPERTIES LABELS "unit;documentation")
    add_test(
        NAME documentation.manual_links
        COMMAND "${CMAKE_COMMAND}"
            -DWORMEDIT_SOURCE_ROOT=${CMAKE_SOURCE_DIR}
            -P "${CMAKE_SOURCE_DIR}/cmake/check_manual_docs.cmake"
    )
    set_tests_properties(documentation.manual_links PROPERTIES LABELS "unit;documentation")
endif()

if(WORMEDIT_BUILD_APP AND WORMEDIT_ENABLE_AUDIO_DECODER)
    target_compile_definitions(wormedit PRIVATE WORMEDIT_HAS_PROJECT_AUDIO_OUTPUTS=1 WORMEDIT_HAS_PROJECT_AUDIO_WAVEFORMS=1)
endif()

if(TARGET wormedit AND WORMEDIT_ENABLE_LUA)
    target_link_libraries(wormedit PRIVATE wormedit::scripting)
endif()
