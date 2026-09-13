include(Catch)

add_executable(wormedit_core_tests
    core/document_tests.cpp
    core/fixture_inventory_tests.cpp
    core/lane_profile_tests.cpp
    core/measure_tests.cpp
    core/quantize_tests.cpp
    core/rational_tests.cpp
)
target_compile_definitions(wormedit_core_tests
    PRIVATE WORMEDIT_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)
target_link_libraries(wormedit_core_tests
    PRIVATE
        wormedit::core
        Catch2::Catch2WithMain
)

catch_discover_tests(wormedit_core_tests
    TEST_PREFIX "core."
    PROPERTIES LABELS "unit;core"
    DISCOVERY_MODE PRE_TEST
)

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
