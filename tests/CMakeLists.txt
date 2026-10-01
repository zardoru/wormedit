include(Catch)

if(UNIX)
    add_executable(wormedit_crash_backtrace_tests
        sys/crash_backtrace_tests.cpp
        ../src/sys/crash_backtrace.cpp
    )
    target_include_directories(wormedit_crash_backtrace_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
    set_target_properties(wormedit_crash_backtrace_tests PROPERTIES ENABLE_EXPORTS ON)
    add_test(NAME crash-backtrace COMMAND wormedit_crash_backtrace_tests)
    set_tests_properties(crash-backtrace PROPERTIES LABELS "integration;sys" TIMEOUT 15)
endif()

add_executable(wormedit_core_tests
    ../src/sys/compose_resource_presenter.cpp
    ../src/sys/compose_area_actions.cpp
    ../src/sys/application_commands.cpp
    ../src/sys/project_view.cpp
    ../src/sys/tap_timing_runtime.cpp
    ../src/sys/transport_player_actions.cpp
    ../src/sys/compose_minimap_navigation.cpp
    ../src/sys/application_defaults.cpp
    ../src/sys/project_open.cpp
    ../src/sys/project_save_paths.cpp
    core/audio_offset_editor_tests.cpp
    core/audio_fixture_inventory_tests.cpp
    core/autosave_loop_tests.cpp
    core/bms_branch_editor_tests.cpp
    core/bmson_sound_channel_editor_tests.cpp
    core/bmson_slice_reset_tests.cpp
    core/bpm_editor_tests.cpp
    core/bga_preview_tests.cpp
    core/chart_compare_tests.cpp
    core/command_tests.cpp
    core/compose_resource_presenter_tests.cpp
    core/command_registry_tests.cpp
    sys/application_commands_tests.cpp
    sys/project_view_tests.cpp
    sys/tap_timing_runtime_tests.cpp
    sys/application_defaults_tests.cpp
    sys/project_open_tests.cpp
    sys/project_save_paths_tests.cpp
    core/document_tests.cpp
    core/editor_navigation_input_tests.cpp
    core/editor_scene_tests.cpp
    core/bms_profile_detection_tests.cpp
    core/fixture_inventory_tests.cpp
    core/grid_tests.cpp
    core/interaction_tests.cpp
    core/chart_clipboard_tests.cpp
    core/lane_profile_tests.cpp
    core/marker_editor_tests.cpp
    core/marker_navigation_tests.cpp
    core/metadata_editor_tests.cpp
    core/chart_editor_tests.cpp
    core/measure_editor_tests.cpp
    core/measure_tests.cpp
    core/note_label_editor_tests.cpp
    core/pause_warp_editor_tests.cpp
    core/player_preview_tests.cpp
    core/storyboard_preview_tests.cpp
    core/quantize_tests.cpp
    core/rational_tests.cpp
    core/recovery_controller_tests.cpp
    core/resource_assignment_tests.cpp
    core/resource_file_copy_tests.cpp
    core/scroll_speed_editor_tests.cpp
    core/semantic_layer_editor_tests.cpp
    core/preferences_tests.cpp
    core/shortcut_profile_storage_tests.cpp
    core/shortcut_profile_startup_tests.cpp
    core/arena_tests.cpp
    core/notifications_tests.cpp
    core/storyboard_tests.cpp
    core/sv_recipe_tests.cpp
    core/startup_session_tests.cpp
    core/task_graph_tests.cpp
    core/tap_tempo_tests.cpp
    core/timing_tests.cpp
    core/timing_discontinuity_tests.cpp
    core/timing_anchor_editor_tests.cpp
    core/timing_lane_controller_tests.cpp
    core/viewport_input_tests.cpp
    core/virtual_canvas_tests.cpp
    core/video_preview_tests.cpp
    core/visual_layout_tests.cpp
    core/workspace_tests.cpp
    formats/bms_tests.cpp
    formats/bms_writer_tests.cpp
    formats/bmson_tests.cpp
    formats/bmson_writer_tests.cpp
    formats/conversion_tests.cpp
    formats/conversion_publication_tests.cpp
    formats/native_project_tests.cpp
    formats/native_project_storage_tests.cpp
    formats/osu_mania_tests.cpp
    formats/stepmania_tests.cpp
    formats/fixture_validation_tests.cpp
)
target_include_directories(wormedit_core_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_compile_definitions(wormedit_core_tests
    PRIVATE WORMEDIT_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)
if(Iconv_FOUND)
    target_compile_definitions(wormedit_core_tests PRIVATE WORMEDIT_TEST_HAS_ICONV=1)
endif()
target_link_libraries(wormedit_core_tests
    PRIVATE
        wormedit::core
        Catch2::Catch2WithMain
        nlohmann_json::nlohmann_json
)

catch_discover_tests(wormedit_core_tests
    TEST_PREFIX "core."
    PROPERTIES LABELS "unit;core"
    DISCOVERY_MODE PRE_TEST
)

if(WORMEDIT_ENABLE_AUDIO_DECODER)
    add_executable(wormedit_audio_tests
        ../src/sys/project_open.cpp
        ../src/sys/application_defaults.cpp
        ../src/sys/audio_project_import.cpp
        ../src/sys/audio_source_registration.cpp
        core/audio_audition_bridge_tests.cpp
        core/audio_audition_pump_tests.cpp
        core/audio_audition_resolver_tests.cpp
        core/audio_audition_session_tests.cpp
        core/audio_object_audition_tests.cpp
        core/audio_block_cache_tests.cpp
        core/audio_decoder_tests.cpp
        sys/audio_project_import_tests.cpp
        sys/audio_source_registration_tests.cpp
        core/audio_preview_bridge_tests.cpp
        core/audio_preview_pump_tests.cpp
        core/audio_preview_session_tests.cpp
        core/audio_playback_rate_tests.cpp
        core/audio_time_stretch_tests.cpp
        core/audio_preview_repro_tests.cpp
        core/audio_source_registry_tests.cpp
        core/audio_transport_tests.cpp
        core/audio_waveform_tests.cpp
        core/audio_aligned_waveform_tests.cpp
        core/audio_waveform_job_tests.cpp
    )
    target_compile_definitions(wormedit_audio_tests
        PRIVATE WORMEDIT_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
                WORMEDIT_HAS_AUDIO_DECODER=1
    )
    target_include_directories(wormedit_audio_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
    target_link_libraries(wormedit_audio_tests
        PRIVATE
            wormedit::audio_decoder
            Catch2::Catch2WithMain
    )
    catch_discover_tests(wormedit_audio_tests
        TEST_PREFIX "audio."
        PROPERTIES LABELS "unit;audio"
        DISCOVERY_MODE PRE_TEST
    )

    if(WORMEDIT_BUILD_APP AND TARGET SDL3::SDL3)
        add_executable(wormedit_sdl_audio_tests
            sys/sdl_audition_output_tests.cpp
            sys/sdl_preview_output_tests.cpp
            ../src/sys/chart_preview_state.cpp
            ../src/sys/frame_timing.cpp
            ../src/sys/sdl_audition_output.cpp
            ../src/sys/sdl_preview_output.cpp
        )
        target_compile_definitions(wormedit_sdl_audio_tests
            PRIVATE WORMEDIT_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
        )
        target_include_directories(wormedit_sdl_audio_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
        target_link_libraries(wormedit_sdl_audio_tests
            PRIVATE
                wormedit::audio_decoder
                SDL3::SDL3
                Catch2::Catch2WithMain
        )
        catch_discover_tests(wormedit_sdl_audio_tests
            TEST_PREFIX "sdl-audio."
            PROPERTIES LABELS "integration;audio;sdl" TIMEOUT 30
            DISCOVERY_MODE PRE_TEST
        )
    endif()
endif()

if(WORMEDIT_ENABLE_IMAGE_DECODER)
    add_executable(wormedit_visual_tests
        visual/image_decoder_tests.cpp
        visual/image_job_tests.cpp
    )
    target_link_libraries(wormedit_visual_tests
        PRIVATE
            wormedit::visual_decoder
            Catch2::Catch2WithMain
    )
    catch_discover_tests(wormedit_visual_tests
        TEST_PREFIX "visual."
        PROPERTIES LABELS "unit;visual"
        DISCOVERY_MODE PRE_TEST
    )
endif()

if(TARGET wormedit_video_decoder)
    add_executable(wormedit_video_tests
        visual/video_decoder_tests.cpp
        visual/video_job_tests.cpp
    )
    target_compile_definitions(wormedit_video_tests
        PRIVATE WORMEDIT_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    )
    target_link_libraries(wormedit_video_tests
        PRIVATE
            wormedit::video_decoder
            Catch2::Catch2WithMain
    )
    catch_discover_tests(wormedit_video_tests
        TEST_PREFIX "video."
        PROPERTIES LABELS "unit;visual;video"
        DISCOVERY_MODE PRE_TEST
    )
endif()

if(TARGET wormedit_video_decoder AND TARGET wormedit_visual_decoder)
    add_executable(wormedit_player_preview_delivery_tests
        sys/player_preview_presenter_tests.cpp
        ../src/sys/player_preview_presenter.cpp
    )
    target_compile_definitions(wormedit_player_preview_delivery_tests PRIVATE
        WORMEDIT_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
        WORMEDIT_HAS_IMAGE_DECODER=1
        WORMEDIT_HAS_VIDEO_DECODER=1
    )
    target_include_directories(wormedit_player_preview_delivery_tests PRIVATE
        "${CMAKE_SOURCE_DIR}/src"
    )
    target_link_libraries(wormedit_player_preview_delivery_tests PRIVATE
        wormedit::visual_decoder
        wormedit::video_decoder
        Catch2::Catch2WithMain
    )
    catch_discover_tests(wormedit_player_preview_delivery_tests
        TEST_PREFIX "player-preview-delivery."
        PROPERTIES LABELS "integration;visual;video"
        DISCOVERY_MODE PRE_TEST
    )
endif()

if(WORMEDIT_BUILD_APP AND TARGET SDL3::SDL3)
    add_executable(wormedit_options_state_tests
        ui/nuklear_options_state_tests.cpp
        ../src/sys/options_runtime.cpp
        ../src/sys/preferences_runtime.cpp
        ../src/sys/platform_shortcut_profile.cpp
    )
    target_include_directories(wormedit_options_state_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
    target_link_libraries(wormedit_options_state_tests PRIVATE
        wormedit::core SDL3::SDL3 Catch2::Catch2WithMain)
    catch_discover_tests(wormedit_options_state_tests
        TEST_PREFIX "frontend-state."
        PROPERTIES LABELS "unit;options"
        DISCOVERY_MODE PRE_TEST)

    add_executable(wormedit_external_player_tests
        sys/external_player_tests.cpp
        ../src/sys/external_player.cpp
    )
    target_include_directories(wormedit_external_player_tests PRIVATE
        "${CMAKE_SOURCE_DIR}/src"
    )
    target_link_libraries(wormedit_external_player_tests PRIVATE
        wormedit::core SDL3::SDL3 Catch2::Catch2WithMain
    )
    catch_discover_tests(wormedit_external_player_tests
        TEST_PREFIX "external_player."
        PROPERTIES LABELS "integration;external_player"
        DISCOVERY_MODE PRE_TEST
    )

    add_executable(wormedit_frontend_state_tests
        ui/nuklear_audio_state_tests.cpp
        ui/arrow_glyph_tests.cpp
        ui/nuklear_compose_state_tests.cpp
        ui/nuklear_metadata_state_tests.cpp
        ui/nuklear_setup_state_tests.cpp
        ui/nuklear_timing_state_tests.cpp
        ../src/ui/nuklear/audio_state.cpp
        ../src/ui/nuklear/arrow_glyph.cpp
        ../src/ui/nuklear/compose_state.cpp
        ../src/ui/nuklear/metadata_state.cpp
        ../src/ui/nuklear/setup_state.cpp
        ../src/ui/nuklear/timing_state.cpp
    )
    target_include_directories(wormedit_frontend_state_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
    target_link_libraries(wormedit_frontend_state_tests
        PRIVATE
            SDL3::SDL3
            Catch2::Catch2WithMain
    )
    catch_discover_tests(wormedit_frontend_state_tests
        TEST_PREFIX "frontend-state."
        PROPERTIES LABELS "unit;frontend"
        DISCOVERY_MODE PRE_TEST
    )

    add_executable(wormedit_nuklear_popup_combo_tests
        ui/nuklear_popup_combo_tests.cpp)
    target_include_directories(wormedit_nuklear_popup_combo_tests PRIVATE
        "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/include")
    target_compile_definitions(wormedit_nuklear_popup_combo_tests PRIVATE
        WORMEDIT_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
    target_link_libraries(wormedit_nuklear_popup_combo_tests PRIVATE
        wormedit::ui_frontend Catch2::Catch2WithMain)
    catch_discover_tests(wormedit_nuklear_popup_combo_tests
        TEST_PREFIX "frontend-state."
        PROPERTIES LABELS "unit;frontend;architecture"
        DISCOVERY_MODE PRE_TEST)

    add_executable(wormedit_nuklear_sdl_backend_tests
        ui/nuklear_sdl_backend_tests.cpp
        ../src/ui/nuklear/sdl_backend.cpp
        ../src/ui/field/field_renderer.cpp
        ../src/ui/field/field_glyphs.cpp
        ../src/sys/frame_timing.cpp
    )
    target_include_directories(wormedit_nuklear_sdl_backend_tests PRIVATE
        "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/include")
    target_link_libraries(wormedit_nuklear_sdl_backend_tests PRIVATE
        wormedit::ui_frontend SDL3::SDL3 Catch2::Catch2WithMain)
    catch_discover_tests(wormedit_nuklear_sdl_backend_tests
        TEST_PREFIX "nuklear-sdl-backend."
        PROPERTIES LABELS "unit;frontend;rendering"
            ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy"
        DISCOVERY_MODE PRE_TEST)

    add_executable(wormedit_field_renderer_tests
        ui/field_renderer_tests.cpp
        ../src/ui/nuklear/arrow_glyph.cpp
        ../src/ui/field/field_renderer.cpp
        ../src/ui/field/field_glyphs.cpp
        ../src/ui/nuklear/sdl_backend.cpp
        ../src/sys/frame_timing.cpp)
    target_include_directories(wormedit_field_renderer_tests PRIVATE
        "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/include")
    target_link_libraries(wormedit_field_renderer_tests PRIVATE
        wormedit::ui_frontend SDL3::SDL3 Catch2::Catch2WithMain)
    catch_discover_tests(wormedit_field_renderer_tests
        TEST_PREFIX "field-renderer."
        PROPERTIES LABELS "unit;frontend;rendering"
            ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy"
        DISCOVERY_MODE PRE_TEST)

    add_executable(wormedit_player_preview_texture_tests
        ui/nuklear_player_preview_texture_tests.cpp
        ../src/ui/nuklear/image_quad.cpp
        ../src/ui/nuklear/player_preview.cpp
        ../src/ui/nuklear/player_preview_texture.cpp
    )
    target_include_directories(wormedit_player_preview_texture_tests PRIVATE
        "${CMAKE_SOURCE_DIR}/src"
    )
    target_link_libraries(wormedit_player_preview_texture_tests
        PRIVATE
            wormedit::core
            wormedit::ui_frontend
            SDL3::SDL3
            Catch2::Catch2WithMain
    )
    catch_discover_tests(wormedit_player_preview_texture_tests
        TEST_PREFIX "player-preview-texture."
        PROPERTIES LABELS "integration;frontend;visual"
        DISCOVERY_MODE PRE_TEST
    )
endif()

if(WORMEDIT_ENABLE_LUA)
    add_executable(wormedit_lua_tests
        lua/lua_runtime_tests.cpp
    )
    target_link_libraries(wormedit_lua_tests
        PRIVATE
            Lua::Lua
            Catch2::Catch2WithMain
    )
    catch_discover_tests(wormedit_lua_tests
        TEST_PREFIX "lua."
        PROPERTIES LABELS "unit;lua"
        DISCOVERY_MODE PRE_TEST
    )
endif()

if(WORMEDIT_BUILD_APP AND TARGET SDL3::SDL3)
    add_executable(wormedit_frame_timing_tests
        sys/frame_timing_tests.cpp
        ui/nuklear_notification_state_tests.cpp
        ui/nuklear_notification_panel_tests.cpp
        sys/notification_history_runtime_tests.cpp
        ../src/ui/nuklear/notification_toasts.cpp
        ../src/ui/nuklear/notification_panel.cpp
        ../src/sys/notification_history_runtime.cpp
        ../src/ui/nuklear/theme.cpp
        ../src/sys/frame_timing.cpp
        ../src/sys/compose_area_actions.cpp
        ../src/sys/compose_view.cpp
        ../src/ui/nuklear/arrow_glyph.cpp
        ../src/sys/compose_resource_presenter.cpp
        ../src/sys/timing_view.cpp
        ../src/sys/workspace_projection.cpp
    )
    target_include_directories(wormedit_frame_timing_tests PRIVATE
        "${CMAKE_SOURCE_DIR}/src" "${CMAKE_BINARY_DIR}/generated")
    target_link_libraries(wormedit_frame_timing_tests PRIVATE
        wormedit::ui_frontend
        nlohmann_json::nlohmann_json
        wormedit::core SDL3::SDL3 Catch2::Catch2WithMain)
    if(TARGET wormedit_audio_decoder)
        target_sources(wormedit_frame_timing_tests PRIVATE
            sys/compose_beat_fit_tests.cpp
            ../src/sys/application_defaults.cpp
            ../src/sys/waveform_view_projection.cpp
            ../src/sys/waveform_analysis_runtime.cpp
            ../src/sys/bmson_waveform_projection.cpp
            ../src/sys/compose_input.cpp
            ../src/sys/compose_minimap_navigation.cpp)
        target_compile_definitions(wormedit_frame_timing_tests PRIVATE WORMEDIT_HAS_AUDIO_DECODER=1)
        target_link_libraries(wormedit_frame_timing_tests PRIVATE wormedit::audio_decoder)
    endif()
    catch_discover_tests(wormedit_frame_timing_tests TEST_PREFIX "frame-timing."
        PROPERTIES LABELS "unit;sys;performance" DISCOVERY_MODE PRE_TEST)
endif()

if(WORMEDIT_BUILD_APP AND TARGET SDL3::SDL3)
    add_executable(wormedit_frame_projection_arena_tests
        sys/frame_projection_arena_tests.cpp
        ../src/sys/frame_timing.cpp)
    target_include_directories(wormedit_frame_projection_arena_tests PRIVATE
        "${CMAKE_SOURCE_DIR}/src")
    target_compile_definitions(wormedit_frame_projection_arena_tests PRIVATE
        $<$<CONFIG:Debug>:WORMEDIT_FRAME_ALLOCATION_HOOK=1>)
    target_link_libraries(wormedit_frame_projection_arena_tests PRIVATE
        SDL3::SDL3 Catch2::Catch2WithMain)
    catch_discover_tests(wormedit_frame_projection_arena_tests TEST_PREFIX "frame-arena."
        PROPERTIES LABELS "unit;sys;performance" DISCOVERY_MODE PRE_TEST)
endif()
