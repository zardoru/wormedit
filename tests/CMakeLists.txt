# Windows binaries run under Wine see the host filesystem through the Z: drive, so compile-time
# host paths need that prefix to be absolute for them.
set(WORMEDIT_TEST_PATH_PREFIX "")
if(CMAKE_CROSSCOMPILING AND WIN32 AND CMAKE_CROSSCOMPILING_EMULATOR MATCHES "wine")
    set(WORMEDIT_TEST_PATH_PREFIX "Z:")
endif()

include(Catch)

find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
add_test(NAME tooling.complexity
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_complexity_tests.py")
set_tests_properties(tooling.complexity PROPERTIES LABELS "unit;tooling")

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
    core/async_job_tests.cpp
    core/lane_occupancy_tests.cpp
    ../src/common/static_interval_index.cpp
    sys/bga_lane_workflow_tests.cpp
    sys/stem_window_tests.cpp
    ../src/sys/compose_resource_presenter.cpp
    ../src/sys/compose_area_actions.cpp
    ../src/sys/compose_action_outcome.cpp
    ../src/sys/compose_audition_requests.cpp
    ../src/sys/application_commands.cpp
    ../src/sys/project_view.cpp
    ../src/sys/tap_timing_runtime.cpp
    ../src/sys/transport_player_actions.cpp
    ../src/sys/compose_minimap_navigation.cpp
    ../src/sys/bookmark_commands.cpp
    ../src/sys/compose_navigation.cpp
    ../src/sys/bookmark_rename_commands.cpp
    ../src/sys/compose_playhead_skip.cpp
    ../src/sys/compose_bgm_bar.cpp
    ../src/sys/sound_list_view.cpp
    ../src/sys/sound_picker_view.cpp
    ../src/sys/compose_measure_commands.cpp
    ../src/sys/compose_note_to_bgm.cpp
    ../src/sys/compose_selection_transform.cpp
    ../src/sys/compose_label_search.cpp
    ../src/sys/compose_dead_spans.cpp
    ../src/sys/compose_kiai_spans.cpp
    ../src/sys/compose_sv_graph.cpp
    ../src/sys/compose_recipe_areas.cpp
    ../src/sys/compose_speed_spans.cpp
    ../src/sys/recipe_form.cpp
    ../src/sys/compose_quantize_preview.cpp
    ../src/sys/application_defaults.cpp
    ../src/sys/recovery_notification_feedback.cpp
    sys/recovery_notification_tests.cpp
    ../src/sys/project_open.cpp
    ../src/sys/chart_import_session.cpp
    ../src/sys/project_save_paths.cpp
    core/audio_offset_editor_tests.cpp
    core/audio_fixture_inventory_tests.cpp
    core/autosave_loop_tests.cpp
    core/bms_branch_editor_tests.cpp
    core/bmson_sound_channel_editor_tests.cpp
    core/note_to_bgm_tests.cpp
    core/bmson_lane_mode_tests.cpp
    core/bmson_slice_reset_tests.cpp
    core/bpm_editor_tests.cpp
    core/bga_preview_tests.cpp
    core/chart_compare_tests.cpp
    core/natural_order_tests.cpp
    core/command_tests.cpp
    core/proposal_tests.cpp
    core/compose_resource_presenter_tests.cpp
    core/command_registry_tests.cpp
    sys/application_commands_tests.cpp
    sys/shortcut_rows_tests.cpp
    ../src/sys/shortcut_rows.cpp
    sys/compose_bookmark_commands_tests.cpp
    sys/bookmark_rename_commands_tests.cpp
    sys/compose_playhead_skip_tests.cpp
    sys/compose_bgm_bar_tests.cpp
    sys/sound_list_projection_tests.cpp
    sys/bga_list_projection_tests.cpp
    ../src/sys/bga_list_view.cpp
    sys/compose_measure_commands_tests.cpp
    sys/selected_sound_layers_tests.cpp
    sys/sound_selection_tests.cpp
    sys/compose_selection_transform_tests.cpp
    sys/compose_label_search_tests.cpp
    sys/compose_dead_spans_tests.cpp
    sys/compose_scroll_geometry_tests.cpp
    sys/compose_kiai_spans_tests.cpp
    sys/compose_sv_graph_tests.cpp
    sys/compose_recipe_areas_tests.cpp
    sys/compose_speed_spans_tests.cpp
    sys/compose_clipboard_selection_tests.cpp
    sys/recipe_form_tests.cpp
    sys/compose_quantize_preview_tests.cpp
    sys/project_view_tests.cpp
    sys/tap_timing_runtime_tests.cpp
    sys/application_defaults_tests.cpp
    sys/project_open_tests.cpp
    sys/project_save_paths_tests.cpp
    core/document_tests.cpp
    core/chart_indices_tests.cpp
    core/keyed_cache_tests.cpp
    core/editor_navigation_input_tests.cpp
    core/editor_scene_tests.cpp
    core/editor_scene_bgm_layers_tests.cpp
    core/bms_profile_detection_tests.cpp
    core/fixture_inventory_tests.cpp
    core/format_field_schema_tests.cpp
    core/grid_tests.cpp
    core/snap_tests.cpp
    core/span_manipulation_tests.cpp
    core/active_gesture_tests.cpp
    core/interaction_edit_commit_tests.cpp
    core/interaction_tests.cpp
    core/cursor_write_guard_tests.cpp
    core/chart_clipboard_tests.cpp
    core/lane_profile_tests.cpp
    core/lane_transform_tests.cpp
    core/time_shift_tests.cpp
    core/marker_editor_tests.cpp
    core/marker_navigation_tests.cpp
    core/metadata_editor_tests.cpp
    core/chart_editor_tests.cpp
    core/chart_import_tests.cpp
    core/lane_layout_editor_tests.cpp
    core/measure_editor_tests.cpp
    core/measure_tests.cpp
    core/result_tests.cpp
    core/sound_slice_rules_tests.cpp
    core/audio_slicer_tests.cpp
    core/sound_import_tests.cpp
    core/note_label_editor_tests.cpp
    core/pause_warp_editor_tests.cpp
    core/player_preview_tests.cpp
    core/storyboard_preview_tests.cpp
    core/quantize_tests.cpp
    core/rational_tests.cpp
    core/recovery_controller_tests.cpp
    core/chart_object_ids_tests.cpp
    core/bgm_columns_tests.cpp
    core/lane_profile_switch_tests.cpp
    core/lane_migration_tests.cpp
    core/channel_map_editor_tests.cpp
    core/resource_assignment_tests.cpp
    core/resource_file_copy_tests.cpp
    core/scroll_speed_editor_tests.cpp
    core/semantic_layer_editor_tests.cpp
    core/sound_groups_tests.cpp
    core/sound_group_proposal_tests.cpp
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
    core/tap_timing_apply_tests.cpp
    sys/tap_timing_apply_tests.cpp
    sys/timing_settings_tests.cpp
    core/timing_tests.cpp
    core/timing_discontinuity_tests.cpp
    core/timing_anchor_editor_tests.cpp
    core/timing_lane_controller_tests.cpp
    core/viewport_input_tests.cpp
    core/virtual_canvas_tests.cpp
    core/video_preview_tests.cpp
    core/visual_layout_tests.cpp
    core/bmson_layer_defaults_tests.cpp
    core/workspace_tests.cpp
    formats/bms_tests.cpp
    formats/bms_clipboard_tests.cpp
    formats/bms_writer_tests.cpp
    formats/bmson_tests.cpp
    formats/bmson_writer_tests.cpp
    formats/conversion_tests.cpp
    formats/slice_spans_tests.cpp
    formats/conversion_publication_tests.cpp
    formats/conversion_loss_matrix_tests.cpp
    formats/native_project_tests.cpp
    formats/native_project_storage_tests.cpp
    formats/osu_mania_tests.cpp
    formats/stepmania_tests.cpp
    formats/fixture_validation_tests.cpp
)
target_include_directories(wormedit_core_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_compile_definitions(wormedit_core_tests
    PRIVATE WORMEDIT_FIXTURE_DIR="${WORMEDIT_TEST_PATH_PREFIX}${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
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

# Catch expands list-valued PROPERTIES; repeat scalar LABELS/ENVIRONMENT properties
# so CTest appends each value without shifting subsequent property names.
catch_discover_tests(wormedit_core_tests
    TEST_PREFIX "core."
    PROPERTIES LABELS unit LABELS core
    DISCOVERY_MODE PRE_TEST
)

if(WORMEDIT_ENABLE_AUDIO_DECODER)
    # Intentionally not discovered by CTest: opt in with [.performance] in Release.
    add_executable(wormedit_performance_tests core/performance_budget_tests.cpp)
    target_link_libraries(wormedit_performance_tests
        PRIVATE wormedit::audio_decoder Catch2::Catch2WithMain)

    add_executable(wormedit_audio_tests
        ../src/sys/project_open.cpp
        ../src/sys/application_defaults.cpp
        ../src/sys/audio_project_import.cpp
        ../src/sys/audio_source_registration.cpp
        ../src/sys/project_audio_sources.cpp
        ../src/sys/metronome.cpp
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
        core/audio_playhead_snapshot_tests.cpp
        core/audio_preview_session_tests.cpp
        core/audio_resident_samples_tests.cpp
        core/audio_playback_rate_tests.cpp
        core/audio_time_stretch_tests.cpp
        core/audio_preview_repro_tests.cpp
        core/audio_source_registry_tests.cpp
        core/audio_transport_tests.cpp
        core/audio_waveform_tests.cpp
        core/audio_onset_tests.cpp
        core/audio_offline_mix_tests.cpp
        core/audio_waveform_viewport_tests.cpp
        core/audio_slice_render_tests.cpp
        formats/slice_span_render_tests.cpp
        core/audio_aligned_waveform_tests.cpp
        core/audio_waveform_job_tests.cpp
    )
    target_compile_definitions(wormedit_audio_tests
        PRIVATE WORMEDIT_FIXTURE_DIR="${WORMEDIT_TEST_PATH_PREFIX}${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
                WORMEDIT_HAS_AUDIO_DECODER=1
    )
    target_include_directories(wormedit_audio_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
    target_link_libraries(wormedit_audio_tests
        PRIVATE
            wormedit::audio_decoder
            Catch2::Catch2WithMain
    )
    if(WORMEDIT_ENABLE_FFMPEG AND TARGET PkgConfig::WORMEDIT_FFMPEG)
        target_link_libraries(wormedit_audio_tests PRIVATE PkgConfig::WORMEDIT_FFMPEG)
        target_compile_definitions(wormedit_audio_tests PRIVATE WORMEDIT_HAS_FFMPEG=1)

        # Exercise the tolerant capability branch even on FFmpeg-equipped development hosts.
        add_executable(wormedit_audio_project_import_no_ffmpeg_tests
            ../src/sys/audio_project_import.cpp
            sys/audio_project_import_tests.cpp)
        target_include_directories(wormedit_audio_project_import_no_ffmpeg_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
        target_compile_definitions(wormedit_audio_project_import_no_ffmpeg_tests PRIVATE
            WORMEDIT_FIXTURE_DIR="${WORMEDIT_TEST_PATH_PREFIX}${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
            WORMEDIT_HAS_AUDIO_DECODER=1)
        target_link_libraries(wormedit_audio_project_import_no_ffmpeg_tests PRIVATE
            wormedit::audio_decoder Catch2::Catch2WithMain)
        catch_discover_tests(wormedit_audio_project_import_no_ffmpeg_tests
            TEST_PREFIX "audio-no-ffmpeg."
            PROPERTIES LABELS unit LABELS audio
            DISCOVERY_MODE PRE_TEST)
    endif()
    catch_discover_tests(wormedit_audio_tests
        TEST_PREFIX "audio."
        PROPERTIES LABELS unit LABELS audio
        DISCOVERY_MODE PRE_TEST
    )

    if(WORMEDIT_BUILD_APP AND TARGET SDL3::SDL3)
        add_executable(wormedit_sdl_audio_tests
            sys/audio_transport_notification_tests.cpp
            sys/audition_notification_tests.cpp
            ../src/sys/compose_audition_requests.cpp
            ../src/sys/audio_source_registration.cpp
            ../src/sys/project_audio_sources.cpp
        ../src/sys/metronome.cpp
            ../src/sys/audio_transport_frame.cpp
            sys/sdl_audition_output_tests.cpp
            sys/sdl_preview_output_tests.cpp
            ../src/sys/chart_preview_state.cpp
            ../src/sys/frame_timing.cpp
            ../src/sys/sdl_audition_output.cpp
            ../src/sys/sdl_preview_output.cpp
        )
        target_compile_definitions(wormedit_sdl_audio_tests
            PRIVATE WORMEDIT_HAS_AUDIO_DECODER=1 WORMEDIT_HAS_PROJECT_AUDIO_OUTPUTS=1 WORMEDIT_FIXTURE_DIR="${WORMEDIT_TEST_PATH_PREFIX}${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
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
            PROPERTIES LABELS integration LABELS audio LABELS sdl TIMEOUT 30
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
        PROPERTIES LABELS unit LABELS visual
        DISCOVERY_MODE PRE_TEST
    )
endif()

if(TARGET wormedit_video_decoder)
    add_executable(wormedit_video_tests
        visual/video_decoder_tests.cpp
        visual/video_job_tests.cpp
    )
    target_compile_definitions(wormedit_video_tests
        PRIVATE WORMEDIT_FIXTURE_DIR="${WORMEDIT_TEST_PATH_PREFIX}${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    )
    target_link_libraries(wormedit_video_tests
        PRIVATE
            wormedit::video_decoder
            Catch2::Catch2WithMain
    )
    catch_discover_tests(wormedit_video_tests
        TEST_PREFIX "video."
        PROPERTIES LABELS unit LABELS visual LABELS video
        DISCOVERY_MODE PRE_TEST
    )
endif()

if(TARGET wormedit_video_decoder AND TARGET wormedit_visual_decoder)
    add_executable(wormedit_player_preview_delivery_tests
        sys/player_preview_presenter_tests.cpp
        ../src/sys/player_preview_presenter.cpp
    )
    target_compile_definitions(wormedit_player_preview_delivery_tests PRIVATE
        WORMEDIT_FIXTURE_DIR="${WORMEDIT_TEST_PATH_PREFIX}${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
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
        PROPERTIES LABELS integration LABELS visual LABELS video
        DISCOVERY_MODE PRE_TEST
    )
endif()

add_executable(wormedit_player_preview_status_tests
    sys/player_preview_status_tests.cpp
    ../src/sys/player_preview_presenter.cpp
)
target_include_directories(wormedit_player_preview_status_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)
target_link_libraries(wormedit_player_preview_status_tests PRIVATE
    wormedit::core
    Catch2::Catch2WithMain
)
catch_discover_tests(wormedit_player_preview_status_tests
    TEST_PREFIX "player-preview-status."
    PROPERTIES LABELS unit LABELS sys
    DISCOVERY_MODE PRE_TEST
)

if(WORMEDIT_BUILD_APP AND TARGET SDL3::SDL3)
    add_executable(wormedit_options_state_tests
        ui/nuklear_options_state_tests.cpp
        ../src/sys/options_runtime.cpp
        ../src/sys/preferences_runtime.cpp
        ../src/sys/platform_shortcut_profile.cpp
    )
    target_include_directories(wormedit_options_state_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
    target_link_libraries(wormedit_options_state_tests PRIVATE
        wormedit::core SDL3::SDL3 nlohmann_json::nlohmann_json Catch2::Catch2WithMain)
    catch_discover_tests(wormedit_options_state_tests
        TEST_PREFIX "frontend-state."
        PROPERTIES LABELS unit LABELS options
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
        PROPERTIES LABELS integration LABELS external_player
        DISCOVERY_MODE PRE_TEST
    )

    add_executable(wormedit_frontend_state_tests
        ui/theme_tests.cpp
        ui/nuklear_audio_state_tests.cpp
        ui/arrow_glyph_tests.cpp
        ui/compose_bookmark_label_tests.cpp
        ui/popup_lifecycle_tests.cpp
        ui/content_margin_tests.cpp
        ui/nuklear_compose_state_tests.cpp
        ui/nuklear_metadata_state_tests.cpp
        ui/nuklear_setup_state_tests.cpp
        ui/sound_import_state_tests.cpp
        ui/keysound_list_state_tests.cpp
        ui/measure_resize_tests.cpp
        ui/nuklear_timing_state_tests.cpp
        ui/nuklear_meter_state_tests.cpp
        ../src/ui/nuklear/audio_state.cpp
        ../src/ui/nuklear/arrow_glyph.cpp
        ../src/ui/nuklear/compose_bookmark_label.cpp
        ../src/ui/nuklear/theme.cpp
        ../src/ui/nuklear/compose_state.cpp
        ../src/ui/nuklear/metadata_state.cpp
        ../src/ui/nuklear/setup_state.cpp
        ../src/ui/nuklear/sound_import_state.cpp
        ../src/ui/nuklear/keysound_list_state.cpp
        ../src/ui/nuklear/measure_resize.cpp
        ../src/ui/nuklear/timing_state.cpp
        ../src/ui/nuklear/meter_state.cpp
    )
    target_include_directories(wormedit_frontend_state_tests PRIVATE
        "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/include")
    target_link_libraries(wormedit_frontend_state_tests
        PRIVATE
            wormedit::core
            wormedit::ui_frontend
            nlohmann_json::nlohmann_json
            SDL3::SDL3
            Catch2::Catch2WithMain
    )
    catch_discover_tests(wormedit_frontend_state_tests
        TEST_PREFIX "frontend-state."
        PROPERTIES LABELS unit LABELS frontend
        DISCOVERY_MODE PRE_TEST
    )

    add_executable(wormedit_nuklear_popup_combo_tests
        ui/nuklear_popup_combo_tests.cpp
        ../src/ui/nuklear/components.cpp
        ../src/ui/nuklear/theme.cpp
        ../src/ui/nuklear/arrow_glyph.cpp
        ../src/ui/nuklear/tooltip.cpp)
    target_include_directories(wormedit_nuklear_popup_combo_tests PRIVATE
        "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/include")
    target_link_libraries(wormedit_nuklear_popup_combo_tests PRIVATE
        wormedit::core wormedit::ui_frontend nlohmann_json::nlohmann_json Catch2::Catch2WithMain)
    catch_discover_tests(wormedit_nuklear_popup_combo_tests
        TEST_PREFIX "frontend-state."
        PROPERTIES LABELS unit LABELS frontend LABELS architecture
        DISCOVERY_MODE PRE_TEST)

    add_executable(wormedit_nuklear_sdl_backend_tests
        ui/nuklear_sdl_backend_tests.cpp
        ../src/ui/nuklear/sdl_backend.cpp
        ../src/ui/field/field_renderer.cpp
        ../src/ui/field/field_tile_cache.cpp
        ../src/common/static_interval_index.cpp
        ../src/ui/field/field_glyphs.cpp
        ../src/sys/frame_timing.cpp
    )
    target_include_directories(wormedit_nuklear_sdl_backend_tests PRIVATE
        "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/include")
    target_link_libraries(wormedit_nuklear_sdl_backend_tests PRIVATE
        wormedit::ui_frontend SDL3::SDL3 Catch2::Catch2WithMain)
    catch_discover_tests(wormedit_nuklear_sdl_backend_tests
        TEST_PREFIX "nuklear-sdl-backend."
        PROPERTIES LABELS unit LABELS frontend LABELS rendering
            ENVIRONMENT SDL_VIDEODRIVER=dummy ENVIRONMENT SDL_AUDIODRIVER=dummy
        DISCOVERY_MODE PRE_TEST)

    add_executable(wormedit_field_renderer_tests
        ui/field_renderer_tests.cpp
        ui/field_tile_cache_tests.cpp
        ../src/ui/nuklear/arrow_glyph.cpp
        ../src/ui/field/field_renderer.cpp
        ../src/ui/field/field_tile_cache.cpp
        ../src/common/static_interval_index.cpp
        ../src/ui/field/field_glyphs.cpp
        ../src/ui/nuklear/sdl_backend.cpp
        ../src/sys/frame_timing.cpp)
    target_include_directories(wormedit_field_renderer_tests PRIVATE
        "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/include")
    target_link_libraries(wormedit_field_renderer_tests PRIVATE
        wormedit::ui_frontend SDL3::SDL3 Catch2::Catch2WithMain)
    catch_discover_tests(wormedit_field_renderer_tests
        TEST_PREFIX "field-renderer."
        PROPERTIES LABELS unit LABELS frontend LABELS rendering
            ENVIRONMENT SDL_VIDEODRIVER=dummy ENVIRONMENT SDL_AUDIODRIVER=dummy
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
        PROPERTIES LABELS integration LABELS frontend LABELS visual
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
        PROPERTIES LABELS unit LABELS lua
        DISCOVERY_MODE PRE_TEST
    )
endif()

if((WORMEDIT_BUILD_APP OR WORMEDIT_BUILD_EVENT_HARNESS) AND TARGET SDL3::SDL3)
    add_executable(wormedit_frontend_event_harness_tests
        sys/application_lifetime_tests.cpp
        ../src/sys/application_lifetime.cpp
        ../src/sys/application_startup.cpp
        ../src/sys/platform_recovery.cpp
        ../src/sys/audio.cpp
        ../src/sys/window.cpp
        ../src/sys/external_player.cpp
        ../src/sys/resource_job_state.cpp
        ../src/sys/output_group_export.cpp
        ui/compose_stem_lane_render_tests.cpp
        ui/nuklear_event_harness.cpp
        ui/nuklear_event_harness_tests.cpp
        ui/widget_lookup_event_harness_tests.cpp
        ../src/sys/resource_drop_router.cpp
        ../src/sys/metadata_actions.cpp
        ../src/sys/metadata_asset_runtime.cpp
        ../src/sys/sound_drop_transaction.cpp
        ../src/sys/compose_sound_actions.cpp
        ui/gesture_lifecycle_event_harness_tests.cpp
        ui/tag_grips_event_harness_tests.cpp
        ui/command_menu_event_harness_tests.cpp
        ui/selected_sound_layers_menu_tests.cpp
        ui/font_atlas_tests.cpp
        ui/nuklear_tooltip_tests.cpp
        ui/context_menu_tests.cpp
        ui/dialog_tests.cpp
        ui/components_tests.cpp
        ui/hidden_notification_history_event_harness_tests.cpp
        ui/notification_hit_region_event_harness_tests.cpp
        sys/global_drop_notification_tests.cpp
        ../src/sys/metadata_drop_state.cpp
        ui/timing_settings_tests.cpp
        ui/tap_timing_panel_event_harness_tests.cpp
        ui/compose_beat_fit_tests.cpp
        ui/compose_measure_resize_tests.cpp
        ui/sound_source_copy_event_harness_tests.cpp
        ui/sound_slot_batch_event_harness_tests.cpp
        ui/compose_hover_timing_event_harness_tests.cpp
        ui/minimap_center_event_harness_tests.cpp
        ui/manual_sound_group_event_harness_tests.cpp
        ui/sound_filter_event_harness_tests.cpp
        ui/dock_select_all_event_harness_tests.cpp
        ui/note_label_inline_event_harness_tests.cpp
        ui/bmson_tab_event_harness_tests.cpp
        ui/bmson_lane_mode_event_harness_tests.cpp
        ui/quantize_report_event_harness_tests.cpp
        ui/keep_pitch_event_harness_tests.cpp
        ui/markers_event_harness_tests.cpp
        ui/bookmark_chooser_event_harness_tests.cpp
        ui/dock_shortcut_event_harness_tests.cpp
        ui/snap_grid_event_harness_tests.cpp
        ui/sound_picker_event_harness_tests.cpp
        ui/bookmark_rename_event_harness_tests.cpp
        ui/popup_menu_event_harness_tests.cpp
        ui/menu_bar_event_harness_tests.cpp
        ../src/sys/menu_command_view.cpp
        ../src/sys/shortcut_rows.cpp
        ../src/sys/bookmark_rename_commands.cpp
        ui/compose_bgm_layer_tests.cpp
        ui/sound_usage_event_harness_tests.cpp
        sys/sound_drop_transaction_tests.cpp
        ../src/sys/sound_drop_transaction.cpp
        ui/bga_advance_workflow_tests.cpp
        ui/bga_lane_event_harness_tests.cpp
        ui/playback_status_event_harness_tests.cpp
        ui/live_resource_table_event_harness_tests.cpp
        ui/audio_sounds_workflow_event_harness_tests.cpp
        ui/inspector_resource_workflow_tests.cpp
        ../src/sys/bookmark_chooser_view.cpp
        ../src/sys/bookmark_navigation.cpp
        ../src/sys/compose_area_actions.cpp
        ../src/sys/compose_action_outcome.cpp
        ../src/sys/compose_audition_requests.cpp
        ../src/sys/compose_bgm_bar.cpp
        ../src/sys/bookmark_commands.cpp
        ../src/sys/compose_navigation.cpp
        ../src/sys/compose_dead_spans.cpp
        ../src/sys/compose_input.cpp
        ../src/sys/compose_waveform_fit_input.cpp
        ../src/sys/compose_timing_input.cpp
        ../src/sys/compose_resource_input.cpp
        ../src/sys/compose_navigation_input.cpp
        ../src/sys/compose_kiai_spans.cpp
        ../src/sys/compose_minimap_navigation.cpp
        ../src/sys/compose_note_to_bgm.cpp
        ../src/sys/compose_playhead_skip.cpp
        ../src/sys/compose_quantize_preview.cpp
        ../src/sys/compose_recipe_areas.cpp
        ../src/sys/compose_resource_presenter.cpp
        ../src/sys/compose_speed_spans.cpp
        ../src/sys/compose_sv_graph.cpp
        ../src/sys/compose_view.cpp
        ../src/sys/compose_scene_view.cpp
        ../src/sys/compose_marker_view.cpp
        ../src/sys/compose_selection_view.cpp
        ../src/sys/compose_draw_source_view.cpp
        ../src/sys/recipe_form.cpp
        ../src/sys/sound_list_view.cpp
        ../src/sys/sound_picker_view.cpp
        ../src/sys/sound_usage_view.cpp
        ../src/sys/workspace_view.cpp
        ${WORMEDIT_UI_FRONTEND_SOURCES}
        "${CMAKE_SOURCE_DIR}/src/common/static_interval_index.cpp"
        "${CMAKE_SOURCE_DIR}/src/sys/frame_timing.cpp")
    target_include_directories(wormedit_frontend_event_harness_tests PRIVATE
        "${CMAKE_SOURCE_DIR}/include" "${CMAKE_SOURCE_DIR}/src"
        "${CMAKE_SOURCE_DIR}/src/vendor" "${CMAKE_BINARY_DIR}/generated")
    target_compile_definitions(wormedit_frontend_event_harness_tests PRIVATE
        WORMEDIT_TEST_ICON_WIDGETS=1
        WORMEDIT_SOURCE_ROOT="${WORMEDIT_TEST_PATH_PREFIX}${CMAKE_SOURCE_DIR}")
    target_link_libraries(wormedit_frontend_event_harness_tests PRIVATE
        wormedit::core wormedit::ui_frontend SDL3::SDL3 otoworm::otoworm
        nlohmann_json::nlohmann_json Catch2::Catch2WithMain)
    if(WORMEDIT_ENABLE_IMAGE_DECODER)
        target_link_libraries(wormedit_frontend_event_harness_tests PRIVATE
            wormedit::visual_decoder)
        target_compile_definitions(wormedit_frontend_event_harness_tests PRIVATE
            WORMEDIT_HAS_IMAGE_DECODER=1)
    endif()
    if(TARGET wormedit_video_decoder)
        target_link_libraries(wormedit_frontend_event_harness_tests PRIVATE
            wormedit::video_decoder)
        target_compile_definitions(wormedit_frontend_event_harness_tests PRIVATE
            WORMEDIT_HAS_VIDEO_DECODER=1)
    endif()
    if(WORMEDIT_ENABLE_AUDIO_DECODER)
        target_sources(wormedit_frontend_event_harness_tests PRIVATE
            ../src/sys/audio_source_registration.cpp
            ../src/sys/project_audio_sources.cpp
            ../src/sys/metronome.cpp
            ../src/sys/chart_preview_state.cpp
            ../src/sys/sdl_audition_output.cpp
            ../src/sys/sdl_preview_output.cpp)
        target_link_libraries(wormedit_frontend_event_harness_tests PRIVATE
            wormedit::audio_decoder)
        target_compile_definitions(wormedit_frontend_event_harness_tests PRIVATE
            WORMEDIT_HAS_AUDIO_DECODER=1)
    endif()
    catch_discover_tests(wormedit_frontend_event_harness_tests
        TEST_PREFIX "frontend-state."
        ADD_TAGS_AS_LABELS
        PROPERTIES TIMEOUT 30 DISCOVERY_MODE PRE_TEST)
endif()

if(WORMEDIT_BUILD_APP AND TARGET SDL3::SDL3)
    if(TARGET wormedit_visual_decoder)
        add_executable(wormedit_bga_actions_tests sys/bga_actions_tests.cpp
            ../src/sys/compose_action_outcome.cpp
            ../src/sys/compose_audition_requests.cpp
            ../src/sys/compose_bga_actions.cpp ../src/sys/bga_list_view.cpp)
        target_include_directories(wormedit_bga_actions_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
        target_compile_definitions(wormedit_bga_actions_tests PRIVATE WORMEDIT_HAS_IMAGE_DECODER=1)
        target_link_libraries(wormedit_bga_actions_tests PRIVATE wormedit::core wormedit::visual_decoder Catch2::Catch2WithMain)
        catch_discover_tests(wormedit_bga_actions_tests TEST_PREFIX "bga-actions.")
    endif()

    add_executable(wormedit_frame_timing_tests
        ../src/sys/external_player.cpp
        ../src/ui/nuklear/components.cpp
        sys/bga_lane_projection_tests.cpp
        sys/compose_hover_timing_tests.cpp
        sys/timing_lane_ui_tests.cpp
        ../src/sys/compose_hover_timing.cpp
        sys/frame_timing_tests.cpp
        sys/compose_notification_tests.cpp
        ui/nuklear_notification_state_tests.cpp
        ui/nuklear_notification_panel_tests.cpp
        sys/notification_history_runtime_tests.cpp
        ../src/ui/nuklear/notification_toasts.cpp
        ../src/ui/nuklear/tooltip.cpp
        ../src/ui/nuklear/notification_panel.cpp
        ../src/sys/notification_history_runtime.cpp
        ../src/ui/nuklear/theme.cpp
        ../src/sys/frame_timing.cpp
        ../src/sys/compose_area_actions.cpp
        ../src/sys/compose_action_outcome.cpp
        ../src/sys/compose_audition_requests.cpp
        ../src/sys/compose_view.cpp
        ../src/sys/compose_scene_view.cpp
        ../src/sys/compose_marker_view.cpp
        ../src/sys/compose_selection_view.cpp
        ../src/sys/compose_draw_source_view.cpp
        ../src/sys/compose_dead_spans.cpp
        ../src/sys/compose_kiai_spans.cpp
        ../src/sys/compose_sv_graph.cpp
        ../src/sys/compose_recipe_areas.cpp
        ../src/sys/compose_speed_spans.cpp
        ../src/sys/compose_quantize_preview.cpp
        ../src/common/static_interval_index.cpp
        ../src/ui/nuklear/arrow_glyph.cpp
        ../src/sys/compose_resource_presenter.cpp
        ../src/sys/timing_view.cpp
        ../src/sys/recipe_form.cpp
        ../src/sys/workspace_view.cpp
        ../src/sys/bookmark_chooser_view.cpp
        ../src/sys/bookmark_navigation.cpp
        ../src/sys/sound_list_view.cpp
        ../src/sys/sound_picker_view.cpp
        ../src/sys/sound_usage_view.cpp
        sys/sound_usage_projection_tests.cpp
        sys/bookmark_chooser_projection_tests.cpp
        sys/sound_usage_actions_tests.cpp
        ../src/sys/sound_usage_actions.cpp
    )
    target_include_directories(wormedit_frame_timing_tests PRIVATE
        "${CMAKE_SOURCE_DIR}/src" "${CMAKE_BINARY_DIR}/generated")
    target_link_libraries(wormedit_frame_timing_tests PRIVATE
        wormedit::ui_frontend
        nlohmann_json::nlohmann_json
        wormedit::core SDL3::SDL3 Catch2::Catch2WithMain)
    if(TARGET wormedit_audio_decoder)
        target_sources(wormedit_frame_timing_tests PRIVATE
            sys/compose_action_notification_tests.cpp
            ../src/sys/compose_actions.cpp
            ../src/sys/compose_resource_actions.cpp
            ../src/sys/compose_marker_actions.cpp
            ../src/sys/compose_bookmark_actions.cpp
            ../src/sys/compose_measure_actions.cpp
            ../src/sys/compose_recipe_actions.cpp
            ../src/sys/compose_transform_actions.cpp
            ../src/sys/compose_bga_actions.cpp
            ../src/sys/compose_measure_commands.cpp
            ../src/sys/compose_selection_transform.cpp
            ../src/sys/compose_label_search.cpp
            ../src/sys/bookmark_rename_commands.cpp
            ../src/sys/resource_job_state.cpp
            ../src/sys/output_group_export.cpp
            ../src/sys/metadata_asset_runtime.cpp
            ../src/sys/metadata_drop_state.cpp
            sys/minimap_center_tests.cpp
            sys/compose_beat_fit_tests.cpp
            sys/compose_onset_snap_tests.cpp
            sys/application_commands_tests.cpp
            ../src/sys/options_runtime.cpp
            ../src/sys/preferences_runtime.cpp
            ../src/sys/platform_shortcut_profile.cpp
            sys/manual_sound_group_command_tests.cpp
            sys/sound_properties_actions_tests.cpp
            sys/audio_waveform_projection_tests.cpp
            ../src/sys/compose_sound_actions.cpp
            ../src/sys/application_command_dispatch.cpp
            ../src/sys/application_commands.cpp
            ../src/sys/transport_player_actions.cpp
            ../src/sys/application_defaults.cpp
            ../src/sys/waveform_view.cpp
            ../src/sys/waveform_analysis_runtime.cpp
            ../src/sys/audio_source_registration.cpp
        ../src/sys/project_audio_sources.cpp
        ../src/sys/metronome.cpp
            sys/audio_notification_tests.cpp
            ../src/sys/bmson_waveform_view.cpp
            ../src/sys/compose_input.cpp
            ../src/sys/compose_waveform_fit_input.cpp
            ../src/sys/compose_timing_input.cpp
            ../src/sys/compose_resource_input.cpp
            ../src/sys/compose_navigation_input.cpp
            ../src/sys/compose_note_to_bgm.cpp
            ../src/sys/compose_minimap_navigation.cpp
            ../src/sys/bookmark_commands.cpp
            ../src/sys/compose_navigation.cpp
            ../src/sys/compose_playhead_skip.cpp
            ../src/sys/compose_bgm_bar.cpp)
        target_sources(wormedit_frame_timing_tests PRIVATE
            ../src/sys/chart_preview_state.cpp
            ../src/sys/sdl_audition_output.cpp
            ../src/sys/sdl_preview_output.cpp)
        target_compile_definitions(wormedit_frame_timing_tests PRIVATE WORMEDIT_HAS_AUDIO_DECODER=1
            WORMEDIT_FIXTURE_DIR="${WORMEDIT_TEST_PATH_PREFIX}${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
        target_link_libraries(wormedit_frame_timing_tests PRIVATE wormedit::audio_decoder)
    endif()
    target_compile_definitions(wormedit_frame_timing_tests PRIVATE
        WORMEDIT_TEST_APPLICATION_COMMAND_DISPATCH_ONLY=1)
    catch_discover_tests(wormedit_frame_timing_tests TEST_PREFIX "frame-timing."
        PROPERTIES LABELS unit LABELS sys LABELS performance DISCOVERY_MODE PRE_TEST)
endif()

if(WORMEDIT_BUILD_APP AND TARGET SDL3::SDL3)
    add_executable(wormedit_frame_projection_arena_tests
        ../src/sys/external_player.cpp
        sys/frame_projection_arena_tests.cpp
        sys/resource_notification_tests.cpp
        ../src/sys/resource_job_state.cpp
        ../src/sys/output_group_export.cpp
        sys/metadata_notification_tests.cpp
        ../src/sys/sound_usage_view.cpp
        ../src/sys/frame_timing.cpp
        ../src/sys/workspace_view.cpp
        ../src/sys/bookmark_chooser_view.cpp
        ../src/sys/bookmark_navigation.cpp
        ../src/sys/sound_list_view.cpp
        ../src/sys/sound_picker_view.cpp
        ../src/sys/timing_view.cpp
        ../src/sys/recipe_form.cpp
        ../src/sys/compose_resource_presenter.cpp
        ../src/sys/metadata_view.cpp
        ../src/sys/metadata_drop_state.cpp
        ../src/sys/metadata_asset_runtime.cpp
        ../src/sys/metadata_actions.cpp)
    target_include_directories(wormedit_frame_projection_arena_tests PRIVATE
        "${CMAKE_SOURCE_DIR}/src" "${CMAKE_BINARY_DIR}/generated")
    target_compile_definitions(wormedit_frame_projection_arena_tests PRIVATE
        $<$<CONFIG:Debug>:WORMEDIT_FRAME_ALLOCATION_HOOK=1>)
    target_link_libraries(wormedit_frame_projection_arena_tests PRIVATE
        wormedit::core SDL3::SDL3 Catch2::Catch2WithMain)
    if(WORMEDIT_ENABLE_AUDIO_DECODER)
        target_link_libraries(wormedit_frame_projection_arena_tests PRIVATE
            wormedit::audio_decoder)
        target_compile_definitions(wormedit_frame_projection_arena_tests PRIVATE
            WORMEDIT_HAS_AUDIO_DECODER=1)
    endif()
    # Projection tests use drop-state storage, not its desktop event dispatcher.
    # Discard that unreferenced handler rather than substituting frontend behavior.
    if(MSVC)
        target_compile_options(wormedit_frame_projection_arena_tests PRIVATE /Gy)
        target_link_options(wormedit_frame_projection_arena_tests PRIVATE /OPT:REF)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(wormedit_frame_projection_arena_tests PRIVATE
            -ffunction-sections -fdata-sections)
        if(APPLE)
            target_link_options(wormedit_frame_projection_arena_tests PRIVATE -Wl,-dead_strip)
        else()
            target_link_options(wormedit_frame_projection_arena_tests PRIVATE -Wl,--gc-sections)
        endif()
    endif()
    catch_discover_tests(wormedit_frame_projection_arena_tests TEST_PREFIX "frame-arena."
        PROPERTIES LABELS unit LABELS sys LABELS performance DISCOVERY_MODE PRE_TEST)
endif()

add_executable(wormedit_compose_mode_tests ui/compose_mode_tests.cpp)
target_include_directories(wormedit_compose_mode_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(wormedit_compose_mode_tests PRIVATE Catch2::Catch2WithMain)
catch_discover_tests(wormedit_compose_mode_tests
    TEST_PREFIX "frontend-state."
    PROPERTIES LABELS unit LABELS compose_mode
    DISCOVERY_MODE PRE_TEST)

add_executable(wormedit_convert_tests tools/wormedit_convert_tests.cpp)
target_compile_definitions(wormedit_convert_tests
    PRIVATE
        WORMEDIT_FIXTURE_DIR="${WORMEDIT_TEST_PATH_PREFIX}${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
        WORMEDIT_CONVERT_EXE="$<TARGET_FILE:wormedit-convert>"
)
target_include_directories(wormedit_convert_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(wormedit_convert_tests
    PRIVATE
        wormedit::core
        Catch2::Catch2WithMain
        nlohmann_json::nlohmann_json
)
add_dependencies(wormedit_convert_tests wormedit-convert)
catch_discover_tests(wormedit_convert_tests
    TEST_PREFIX "wormedit_convert."
    PROPERTIES LABELS unit LABELS tools
    DISCOVERY_MODE PRE_TEST
)

# Pure pacing policy has no SDL, core, or Catch dependencies.
add_executable(wormedit_frame_pacing_tests
    sys/frame_pacing_tests.cpp
    ../src/sys/frame_pacing.cpp)
target_include_directories(wormedit_frame_pacing_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
add_test(NAME frame-pacing COMMAND wormedit_frame_pacing_tests)
set_tests_properties(frame-pacing PROPERTIES LABELS "unit;sys")

if(APPLE AND WORMEDIT_BUILD_APP)
    add_executable(wormedit_macos_menu_tests sys/macos_menu_tests.mm ../src/sys/macos_menu.mm)
    set_target_properties(wormedit_macos_menu_tests PROPERTIES OBJCXX_STANDARD 23 OBJCXX_STANDARD_REQUIRED ON)
    target_compile_options(wormedit_macos_menu_tests PRIVATE -fobjc-arc)
    target_include_directories(wormedit_macos_menu_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
    target_link_libraries(wormedit_macos_menu_tests PRIVATE wormedit::core SDL3::SDL3 Catch2::Catch2WithMain "-framework AppKit")
    catch_discover_tests(wormedit_macos_menu_tests TEST_PREFIX "macos-menu.")
endif()

# Label after Catch discovery so its existing labels and properties are preserved.
set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/slow_tests.cmake")

# Compile the actual unavailable owner even when the application has audio enabled.
add_executable(wormedit_audition_unavailable_tests
    sys/audition_unavailable_tests.cpp
    ../src/sys/compose_audition_requests.cpp
)
target_include_directories(wormedit_audition_unavailable_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(wormedit_audition_unavailable_tests PRIVATE
    wormedit::core Catch2::Catch2WithMain)
catch_discover_tests(wormedit_audition_unavailable_tests
    TEST_PREFIX "audition-unavailable."
    PROPERTIES LABELS unit LABELS sys DISCOVERY_MODE PRE_TEST)
