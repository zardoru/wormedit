cmake_minimum_required(VERSION 3.25)

if(POLICY CMP0177)
    cmake_policy(SET CMP0177 NEW)
endif()

project(wormedit
    VERSION 0.1.0
    DESCRIPTION "Cross-platform rhythm chart editor"
    LANGUAGES C CXX
)

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
    src/core/bms_branch_editor.cpp
    src/core/bms_branch_view.cpp
    src/core/bmson_lane_mode.cpp
    src/core/bmson_sound_channel_editor.cpp
    src/core/sound_selection.cpp
    src/core/note_to_bgm.cpp
    src/core/bpm_editor.cpp
    src/core/chart_compare.cpp
    src/core/command.cpp
    src/core/command_registry.cpp
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
    src/core/interaction.cpp
    src/core/note_placement.cpp
    src/core/note_move.cpp
    src/core/interaction_gesture_lifecycle.cpp
    src/core/interaction_cursor.cpp
    src/core/interaction_hit_test.cpp
    src/core/interaction_selection.cpp
    src/core/lane_profiles.cpp
    src/core/marker_editor.cpp
    src/core/kiai_spans.cpp
    src/core/marker_navigation.cpp
    src/core/metadata_editor.cpp
    src/core/natural_order.cpp
    src/core/chart_editor.cpp
    src/core/chart_import.cpp
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
    src/core/lane_migration.cpp
    src/core/channel_map_editor.cpp
    src/core/resource_assignment.cpp
    src/core/resource_file_copy.cpp
    src/core/scroll_speed_editor.cpp
    src/core/semantic_layer_editor.cpp
    src/core/shortcut_profile_storage.cpp
    src/core/atomic_file_storage.cpp
    src/core/preferences.cpp
    src/core/shortcut_profile_startup.cpp
    src/core/sound_slice.cpp
    src/core/audio_slicer.cpp
    src/core/sound_import.cpp
    src/core/arena.cpp
    src/core/notifications.cpp
    src/core/storyboard.cpp
    src/core/storyboard_preview.cpp
    src/core/sv_recipe.cpp
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
    src/formats/bms_clipboard.cpp
    src/formats/bms_branch_selection.cpp
    src/formats/bms_writer.cpp
    src/formats/bmson.cpp
    src/formats/bmson_writer.cpp
    src/formats/conversion.cpp
    src/formats/slice_spans.cpp
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
    src/sys/project_open.cpp
)
target_include_directories(wormedit-convert PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(wormedit-convert PRIVATE wormedit::core nlohmann_json::nlohmann_json)
wormedit_enable_warnings(wormedit-convert)

# Manual reference tables generated from the command registry and layout catalog.
add_executable(wormedit-manual-reference
    src/tools/wormedit_manual_reference.cpp
    src/sys/shortcut_rows.cpp
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
    target_compile_definitions(wormedit_core PRIVATE WORMEDIT_HAS_ICONV=1)
endif()

wormedit_enable_warnings(wormedit_core)

if(WORMEDIT_ENABLE_LUA)
    include(cmake/dependencies/lua.cmake)
endif()

if(WORMEDIT_ENABLE_AUDIO_DECODER)
    include(cmake/dependencies/miniaudio.cmake)
    target_sources(wormedit_audio_decoder PRIVATE
        src/formats/slice_span_render.cpp
        src/audio/playback_rate.cpp
        src/audio/time_stretch.cpp
        src/audio/onset.cpp
        src/audio/offline_mix.cpp
        src/audio/metronome.cpp
    )
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
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/tooltip.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/context_menu.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/dialog.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/chart_import_dialog.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/components.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/ui_gallery.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/audio_panel.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/bmson_lane_mode_ui.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/external_player_panel.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/options_window.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/shortcuts_panel.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/notification_toasts.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/notification_panel.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/tap_timing_panel.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/timing_settings_panel.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/audio_state.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/bms_branch_panel.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/arrow_glyph.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/font_atlas.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/compose_bookmark_label.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/compose_bookmark_chooser.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/compose_sound_picker.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/command_menu.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/compose_bookmark_rename.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/compose_note_label_inline.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/compose_bmson_tab.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/compose_markers.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/compose_area_menu.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/compose_recipe_menu.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/compose_note_menu.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/compose_recipe_form.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/compose_canvas.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/compose_toolbar.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/compose_snap_grid.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/compose_inspector.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/compose_input_panel.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/compose_keyboard_input.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/compose_viewport_input.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/compose_timing_input.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/compose_object_input.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/compose_panel.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/compare_panel.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/compose_resources.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/compose_state.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/frontend.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/menu_bar.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/sdl_backend.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/field/field_renderer.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/field/field_tile_cache.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/field/field_glyphs.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/image_quad.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/lucide_icons.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/metadata_panel.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/metadata_audio_section.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/metadata_difficulty_section.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/metadata_format_fields.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/metadata_song_section.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/metadata_state.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/player_preview.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/player_preview_texture.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/setup_panel.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/setup_state.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/sound_import_state.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/sound_import_dialog.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/sound_import_runtime.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/keysound_list_state.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/measure_resize.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/compose_measure_resize_input.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/compose_sounds_panel.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/sound_slot_batch.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/compose_bga_panel.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/compose_measures_panel.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/compose_layers_panel.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/timing_stage_editor.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/text_field.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/timing_state.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/meter_state.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/nuklear/theme.cpp"
    )

endif()

if(WORMEDIT_BUILD_APP)
    add_executable(wormedit
        src/common/static_interval_index.cpp
        src/sys/audio.cpp
        src/sys/application_defaults.cpp
        src/sys/application_commands.cpp
        src/sys/compose_minimap_navigation.cpp
        src/sys/bookmark_commands.cpp
        src/sys/compose_navigation.cpp
        src/sys/bookmark_chooser_view.cpp
        src/sys/bookmark_navigation.cpp
        src/sys/bookmark_rename_commands.cpp
        src/sys/compose_playhead_skip.cpp
        src/sys/compose_bgm_bar.cpp
        src/sys/sound_list_view.cpp
        src/sys/sound_picker_view.cpp
        src/sys/sound_usage_view.cpp
        src/sys/sound_usage_actions.cpp
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
        src/sys/compose_scene_view.cpp
        src/sys/compose_marker_view.cpp
        src/sys/compose_selection_view.cpp
        src/sys/compose_draw_source_view.cpp
        src/sys/compose_hover_timing.cpp
        src/sys/compare_view.cpp
        src/sys/compose_input.cpp
        src/sys/compose_waveform_fit_input.cpp
        src/sys/compose_timing_input.cpp
        src/sys/compose_resource_input.cpp
        src/sys/compose_navigation_input.cpp
        src/sys/compose_note_to_bgm.cpp
        src/sys/compose_area_actions.cpp
        src/sys/compose_action_outcome.cpp
        src/sys/document_edit_feedback.cpp
        src/sys/compose_actions.cpp
        src/sys/compose_resource_actions.cpp
        src/sys/compose_marker_actions.cpp
        src/sys/compose_bookmark_actions.cpp
        src/sys/compose_measure_actions.cpp
        src/sys/compose_recipe_actions.cpp
        src/sys/compose_transform_actions.cpp
        src/sys/compose_sound_actions.cpp
        src/sys/compose_bga_actions.cpp
        src/sys/bga_list_view.cpp
        src/sys/bga_dock_runtime.cpp
        src/sys/compose_audition_requests.cpp
        src/sys/crash_backtrace.cpp
        src/sys/audio_source_registration.cpp
        src/sys/project_audio_sources.cpp
        src/sys/metronome.cpp
        src/sys/application_frame_view.cpp
        src/sys/application_frame_services.cpp
        src/sys/resource_drop_router.cpp
        src/sys/frame_pacing.cpp
        src/sys/sound_drop_transaction.cpp
        src/sys/application_startup.cpp
        src/sys/application_lifetime.cpp
        src/sys/recovery_notification_feedback.cpp
        src/sys/audio_project_import.cpp
        src/sys/project_open.cpp
        src/sys/chart_import_session.cpp
        src/sys/project_save_paths.cpp
        src/sys/main.cpp
        src/sys/application.cpp
        src/sys/application_timing_stage.cpp
        src/sys/frame_timing.cpp
        src/sys/timing_view.cpp
        src/sys/timing_edit_runtime.cpp
        src/sys/metadata_drop_state.cpp
        src/sys/metadata_asset_runtime.cpp
        src/sys/metadata_actions.cpp
        src/sys/metadata_view.cpp
        src/sys/waveform_view.cpp
        src/sys/bmson_waveform_view.cpp
        src/sys/compose_waveform_overlays.cpp
        src/sys/resource_job_state.cpp
        src/sys/output_group_export.cpp
        src/sys/player_preview_presenter.cpp
        src/sys/workspace_view.cpp
        src/sys/workspace_frame_view.cpp
        src/sys/transport_player_actions.cpp
        src/sys/external_player.cpp
        src/sys/preferences_runtime.cpp
        src/sys/options_runtime.cpp
        src/sys/shortcut_rows.cpp
        src/sys/menu_command_view.cpp
        src/sys/notification_history_runtime.cpp
        src/sys/project_view.cpp
        src/sys/tap_timing_runtime.cpp
        src/sys/platform_recovery.cpp
        src/sys/platform_shortcut_profile.cpp
        src/sys/window.cpp
        ${WORMEDIT_UI_FRONTEND_SOURCES}
    )
    if(APPLE)
        target_sources(wormedit PRIVATE src/sys/macos_menu.mm)
        set_source_files_properties(src/sys/macos_menu.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
        set_target_properties(wormedit PROPERTIES OBJCXX_STANDARD 23 OBJCXX_STANDARD_REQUIRED ON)
        target_compile_definitions(wormedit PRIVATE WORMEDIT_HAS_MACOS_MENU=1)
        target_link_libraries(wormedit PRIVATE "-framework AppKit")
    endif()
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/assets/fonts")
    configure_file("${CMAKE_SOURCE_DIR}/assets/fonts/AdwaitaMono-Regular.ttf"
                   "${CMAKE_BINARY_DIR}/assets/fonts/AdwaitaMono-Regular.ttf" COPYONLY)
    foreach(font_asset NotoSansCJKjp-Regular.otf NotoSansCJK-LICENSE README.md)
        configure_file("${CMAKE_SOURCE_DIR}/assets/fonts/${font_asset}"
                       "${CMAKE_BINARY_DIR}/assets/fonts/${font_asset}" COPYONLY)
    endforeach()
    configure_file("${CMAKE_SOURCE_DIR}/assets/fonts/LICENSE"
                   "${CMAKE_BINARY_DIR}/assets/fonts/LICENSE" COPYONLY)
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/assets/themes")
    configure_file("${CMAKE_SOURCE_DIR}/assets/themes/ui-dark.json"
                   "${CMAKE_BINARY_DIR}/assets/themes/ui-dark.json" COPYONLY)
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
            otoworm::otoworm
            nlohmann_json::nlohmann_json
    )

    target_compile_definitions(wormedit PRIVATE
        $<$<CONFIG:Debug>:WORMEDIT_FRAME_ALLOCATION_HOOK=1>)

    if(UNIX)
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
    else()
        install(TARGETS wormedit RUNTIME DESTINATION "${WORMEDIT_INSTALL_BINDIR}")
    endif()
    install(FILES assets/fonts/AdwaitaMono-Regular.ttf assets/fonts/LICENSE
        assets/fonts/NotoSansCJKjp-Regular.otf assets/fonts/NotoSansCJK-LICENSE assets/fonts/README.md
        DESTINATION "${WORMEDIT_INSTALL_BINDIR}/assets/fonts")
    install(FILES assets/themes/ui-dark.json DESTINATION "${WORMEDIT_INSTALL_BINDIR}/assets/themes")
    install(DIRECTORY assets/themes/field DESTINATION "${WORMEDIT_INSTALL_BINDIR}/assets/themes"
        FILES_MATCHING PATTERN "*.json")
    install(FILES assets/icons/lucide/LICENSE
        DESTINATION "${WORMEDIT_INSTALL_BINDIR}/assets/icons/lucide")
    install(FILES LICENSE README.md DESTINATION "${WORMEDIT_INSTALL_BINDIR}")
    include(cmake/wormedit_manual.cmake)

    if(WORMEDIT_ENABLE_IMAGE_DECODER)
        target_link_libraries(wormedit PRIVATE wormedit::visual_decoder)
        target_compile_definitions(wormedit PRIVATE WORMEDIT_HAS_IMAGE_DECODER=1)
    endif()

    if(WORMEDIT_ENABLE_FFMPEG AND TARGET PkgConfig::WORMEDIT_FFMPEG)
        target_link_libraries(wormedit PRIVATE PkgConfig::WORMEDIT_FFMPEG)
        set_source_files_properties(src/sys/audio_project_import.cpp TARGET_DIRECTORY wormedit
            PROPERTIES COMPILE_DEFINITIONS WORMEDIT_HAS_FFMPEG=1)
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
