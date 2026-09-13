include(Catch)

add_executable(wormedit_core_tests
    core/audio_fixture_inventory_tests.cpp
    core/bga_preview_tests.cpp
    core/chart_compare_tests.cpp
    core/command_tests.cpp
    core/document_tests.cpp
    core/editor_scene_tests.cpp
    core/bms_profile_detection_tests.cpp
    core/fixture_inventory_tests.cpp
    core/grid_tests.cpp
    core/interaction_tests.cpp
    core/lane_profile_tests.cpp
    core/metadata_editor_tests.cpp
    core/measure_tests.cpp
    core/quantize_tests.cpp
    core/rational_tests.cpp
    core/recovery_controller_tests.cpp
    core/storyboard_tests.cpp
    core/startup_session_tests.cpp
    core/task_graph_tests.cpp
    core/timing_tests.cpp
    core/viewport_input_tests.cpp
    core/workspace_tests.cpp
    formats/bms_tests.cpp
    formats/bms_writer_tests.cpp
    formats/bmson_tests.cpp
    formats/bmson_writer_tests.cpp
    formats/native_project_tests.cpp
    formats/native_project_storage_tests.cpp
    formats/osu_mania_tests.cpp
    formats/stepmania_tests.cpp
)
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
        core/audio_block_cache_tests.cpp
        core/audio_decoder_tests.cpp
        core/audio_preview_bridge_tests.cpp
        core/audio_transport_tests.cpp
        core/audio_waveform_tests.cpp
    )
    target_compile_definitions(wormedit_audio_tests
        PRIVATE WORMEDIT_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    )
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
