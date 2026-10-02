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

option(WORMEDIT_BUILD_APP "Build the SDL3 GPU application" ON)
option(WORMEDIT_ENABLE_LUA "Build the pinned Lua runtime adapter" OFF)
option(WORMEDIT_ENABLE_AUDIO_DECODER "Build the headless MP3/Ogg decoder target" ON)
option(WORMEDIT_ENABLE_IMAGE_DECODER "Build the headless bounded static-image decoder target" ON)
option(WORMEDIT_ENABLE_VIDEO_DECODER "Build the optional bounded FFmpeg video-frame decoder" ON)
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
    src/core/bms_branch_editor.cpp
    src/core/bms_branch_projection.cpp
    src/core/bmson_sound_channel_editor.cpp
    src/core/bpm_editor.cpp
    src/core/chart_compare.cpp
    src/core/command.cpp
    src/core/command_registry.cpp
    src/core/chart_clipboard.cpp
    src/core/document.cpp
    src/core/editor_navigation_input.cpp
    src/core/editor_scene.cpp
    src/core/bms_profile_detection.cpp
    src/core/bga_preview.cpp
    src/core/format_field_schema.cpp
    src/core/grid.cpp
    src/core/interaction.cpp
    src/core/interaction_cursor.cpp
    src/core/interaction_hit_test.cpp
    src/core/interaction_selection.cpp
    src/core/lane_profiles.cpp
    src/core/marker_editor.cpp
    src/core/kiai_spans.cpp
    src/core/marker_navigation.cpp
    src/core/metadata_editor.cpp
    src/core/chart_editor.cpp
    src/core/measure.cpp
    src/core/lane_layout_editor.cpp
    src/core/measure_editor.cpp
    src/core/lane_transform.cpp
    src/core/note_label_editor.cpp
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
    src/core/resource_assignment.cpp
    src/core/resource_file_copy.cpp
    src/core/scroll_speed_editor.cpp
    src/core/semantic_layer_editor.cpp
    src/core/shortcut_profile_storage.cpp
    src/core/atomic_file_storage.cpp
    src/core/preferences.cpp
    src/core/shortcut_profile_startup.cpp
    src/core/sound_slice.cpp
    src/core/arena.cpp
    src/core/notifications.cpp
    src/core/storyboard.cpp
    src/core/storyboard_preview.cpp
    src/core/sv_recipe.cpp
    src/core/startup_session.cpp
    src/core/task_graph.cpp
    src/core/time_shift.cpp
    src/core/tap_tempo.cpp
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
    src/formats/bms_branch_selection.cpp
    src/formats/bms_writer.cpp
    src/formats/bmson.cpp
    src/formats/bmson_writer.cpp
    src/formats/conversion.cpp
    src/formats/conversion_publication.cpp
    src/formats/conversion_publication_storage.cpp
    src/formats/native_project.cpp
    src/formats/native_project_migrations.cpp
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
    src/sys/project_open.cpp
)
target_include_directories(wormedit-convert PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(wormedit-convert PRIVATE wormedit::core nlohmann_json::nlohmann_json)
if(MSVC)
    target_compile_options(wormedit-convert PRIVATE /W4 /permissive-)
else()
    target_compile_options(wormedit-convert PRIVATE -Wall -Wextra -Wpedantic)
endif()

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
    target_sources(wormedit_audio_decoder PRIVATE src/audio/playback_rate.cpp src/audio/time_stretch.cpp)
endif()

if(WORMEDIT_ENABLE_IMAGE_DECODER OR WORMEDIT_ENABLE_VIDEO_DECODER)
    add_library(wormedit_visual_assets src/visual/visual_asset.cpp)
    add_library(wormedit::visual_assets ALIAS wormedit_visual_assets)
    target_include_directories(wormedit_visual_assets PUBLIC "${CMAKE_SOURCE_DIR}/include")
    target_compile_features(wormedit_visual_assets PUBLIC cxx_std_20)
    target_link_libraries(wormedit_visual_assets PUBLIC wormedit::core)
    if(MSVC)
        target_compile_options(wormedit_visual_assets PRIVATE /W4 /permissive-)
    else()
        target_compile_options(wormedit_visual_assets PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

if(WORMEDIT_ENABLE_IMAGE_DECODER)
    include(cmake/dependencies/stb_image.cmake)
    add_library(wormedit_visual_decoder
        src/visual/image_decoder.cpp
        src/visual/image_job.cpp
    )
    add_library(wormedit::visual_decoder ALIAS wormedit_visual_decoder)
    target_include_directories(wormedit_visual_decoder PUBLIC "${CMAKE_SOURCE_DIR}/include")
    target_compile_features(wormedit_visual_decoder PUBLIC cxx_std_20)
    target_link_libraries(wormedit_visual_decoder PUBLIC wormedit::visual_assets PRIVATE stb::image)
    if(MSVC)
        target_compile_options(wormedit_visual_decoder PRIVATE /W4 /permissive-)
    else()
        target_compile_options(wormedit_visual_decoder PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

if(WORMEDIT_ENABLE_VIDEO_DECODER)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(WORMEDIT_FFMPEG QUIET IMPORTED_TARGET
            libavformat
            libavcodec
            libavutil
            libswscale
        )
    endif()
    if(TARGET PkgConfig::WORMEDIT_FFMPEG)
        add_library(wormedit_video_decoder
            src/visual/video_decoder.cpp
            src/visual/video_job.cpp
        )
        add_library(wormedit::video_decoder ALIAS wormedit_video_decoder)
        target_include_directories(wormedit_video_decoder PUBLIC "${CMAKE_SOURCE_DIR}/include")
        target_compile_features(wormedit_video_decoder PUBLIC cxx_std_20)
        target_link_libraries(wormedit_video_decoder
            PUBLIC wormedit::visual_assets
            PRIVATE PkgConfig::WORMEDIT_FFMPEG
        )
        if(MSVC)
            target_compile_options(wormedit_video_decoder PRIVATE /W4 /permissive-)
        else()
            target_compile_options(wormedit_video_decoder PRIVATE -Wall -Wextra -Wpedantic)
        endif()
    else()
        message(STATUS
            "FFmpeg development libraries were not found; optional video decoding is disabled"
        )
    endif()
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
        src/common/static_interval_index.cpp
        src/editor/field.cpp
        src/sys/audio.cpp
        src/sys/application_defaults.cpp
        src/sys/application_commands.cpp
        src/sys/compose_minimap_navigation.cpp
        src/sys/compose_bookmark_commands.cpp
        src/sys/compose_playhead_skip.cpp
        src/sys/compose_bgm_bar.cpp
        src/sys/compose_measure_commands.cpp
        src/sys/compose_selection_transform.cpp
        src/sys/compose_label_search.cpp
        src/sys/compose_dead_spans.cpp
        src/sys/compose_kiai_spans.cpp
        src/sys/compose_sv_graph.cpp
        src/sys/compose_recipe_areas.cpp
        src/sys/compose_speed_spans.cpp
        src/sys/recipe_form.cpp
        src/sys/compose_quantize_preview.cpp
        src/sys/application_command_dispatch.cpp
        src/sys/compose_resource_presenter.cpp
        src/sys/compose_view.cpp
        src/sys/compose_input.cpp
        src/sys/compose_area_actions.cpp
        src/sys/compose_actions.cpp
        src/sys/compose_audition_requests.cpp
        src/sys/crash_backtrace.cpp
        src/sys/audio_source_registration.cpp
        src/sys/application_edit_helpers.cpp
        src/sys/application_frame_projection.cpp
        src/sys/application_frame_services.cpp
        src/sys/application_startup.cpp
        src/sys/audio_project_import.cpp
        src/sys/project_open.cpp
        src/sys/project_save_paths.cpp
        src/sys/main.cpp
        src/sys/frame_timing.cpp
        src/sys/timing_view.cpp
        src/sys/timing_edit_runtime.cpp
        src/sys/metadata_drop_state.cpp
        src/sys/metadata_asset_runtime.cpp
        src/sys/metadata_actions.cpp
        src/sys/metadata_view_projection.cpp
        src/sys/waveform_view_projection.cpp
        src/sys/bmson_waveform_projection.cpp
        src/sys/compose_waveform_overlays.cpp
        src/sys/resource_job_state.cpp
        src/sys/player_preview_presenter.cpp
        src/sys/workspace_projection.cpp
        src/sys/workspace_frame_view.cpp
        src/sys/transport_player_actions.cpp
        src/sys/external_player.cpp
        src/sys/preferences_runtime.cpp
        src/sys/options_runtime.cpp
        src/sys/notification_history_runtime.cpp
        src/sys/project_view.cpp
        src/sys/tap_timing_runtime.cpp
        src/sys/platform_recovery.cpp
        src/sys/platform_shortcut_profile.cpp
        src/sys/window.cpp
        src/ui/nuklear/audio_panel.cpp
        src/ui/nuklear/external_player_panel.cpp
        src/ui/nuklear/options_window.cpp
        src/ui/nuklear/notification_toasts.cpp
        src/ui/nuklear/notification_panel.cpp
        src/ui/nuklear/tap_timing_panel.cpp
        src/ui/nuklear/audio_state.cpp
        src/ui/nuklear/bms_branch_panel.cpp
        src/ui/nuklear/arrow_glyph.cpp
        src/ui/nuklear/compose_bookmark_label.cpp
        src/ui/nuklear/compose_markers.cpp
        src/ui/nuklear/compose_area_menu.cpp
        src/ui/nuklear/compose_recipe_menu.cpp
        src/ui/nuklear/compose_recipe_form.cpp
        src/ui/nuklear/compose_canvas.cpp
        src/ui/nuklear/compose_toolbar.cpp
        src/ui/nuklear/compose_inspector.cpp
        src/ui/nuklear/compose_input_panel.cpp
        src/ui/nuklear/compose_panel.cpp
        src/ui/nuklear/compose_resources.cpp
        src/ui/nuklear/compose_state.cpp
        src/ui/nuklear/frontend.cpp
        src/ui/nuklear/sdl_backend.cpp
        src/ui/field/field_renderer.cpp
        src/ui/field/field_tile_cache.cpp
        src/ui/field/field_glyphs.cpp
        src/ui/nuklear/image_quad.cpp
        src/ui/nuklear/lucide_icons.cpp
        src/ui/nuklear/metadata_panel.cpp
        src/ui/nuklear/metadata_state.cpp
        src/ui/nuklear/player_preview.cpp
        src/ui/nuklear/player_preview_texture.cpp
        src/ui/nuklear/setup_panel.cpp
        src/ui/nuklear/setup_state.cpp
        src/ui/nuklear/timing_stage_editor.cpp
        src/ui/nuklear/timing_state.cpp
        src/ui/nuklear/meter_state.cpp
        src/ui/nuklear/theme.cpp
    )
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/assets/fonts")
    configure_file("${CMAKE_SOURCE_DIR}/assets/fonts/AdwaitaMono-Regular.ttf"
                   "${CMAKE_BINARY_DIR}/assets/fonts/AdwaitaMono-Regular.ttf" COPYONLY)
    configure_file("${CMAKE_SOURCE_DIR}/assets/fonts/LICENSE"
                   "${CMAKE_BINARY_DIR}/assets/fonts/LICENSE" COPYONLY)
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/assets/themes")
    configure_file("${CMAKE_SOURCE_DIR}/assets/themes/ui-dark.json"
                   "${CMAKE_BINARY_DIR}/assets/themes/ui-dark.json" COPYONLY)
    configure_file("${CMAKE_SOURCE_DIR}/assets/themes/compose-field.json"
                   "${CMAKE_BINARY_DIR}/assets/themes/compose-field.json" COPYONLY)
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
            otoworm::otoworm
            nlohmann_json::nlohmann_json
    )

    target_compile_definitions(wormedit PRIVATE
        $<$<CONFIG:Debug>:WORMEDIT_FRAME_ALLOCATION_HOOK=1>)

    if(UNIX)
        set_target_properties(wormedit PROPERTIES ENABLE_EXPORTS ON)
    endif()

    if(WORMEDIT_ENABLE_IMAGE_DECODER)
        target_link_libraries(wormedit PRIVATE wormedit::visual_decoder)
        target_compile_definitions(wormedit PRIVATE WORMEDIT_HAS_IMAGE_DECODER=1)
    endif()

    if(TARGET wormedit_video_decoder)
        target_link_libraries(wormedit PRIVATE wormedit::video_decoder)
        target_compile_definitions(wormedit PRIVATE WORMEDIT_HAS_VIDEO_DECODER=1)
    endif()

    if(WORMEDIT_ENABLE_AUDIO_DECODER)
        target_sources(wormedit PRIVATE
            src/sys/chart_preview_state.cpp
            src/sys/audio_transport_frame.cpp
            src/sys/waveform_analysis_runtime.cpp
            src/sys/sdl_audition_output.cpp
            src/sys/sdl_preview_output.cpp
        )
        target_link_libraries(wormedit PRIVATE wormedit::audio_decoder)
        target_compile_definitions(wormedit PRIVATE WORMEDIT_HAS_AUDIO_DECODER=1)
    endif()

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
endif()
