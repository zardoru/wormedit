set(WORMEDIT_NLOHMANN_JSON_SOURCE_DIR "" CACHE PATH "Path to a local nlohmann/json 3.12.0 source checkout")

set(JSON_Diagnostic_Positions ON CACHE BOOL "Enable nlohmann/json source byte positions" FORCE)
set(JSON_BuildTests OFF CACHE BOOL "Do not build nlohmann/json tests" FORCE)
set(JSON_Install OFF CACHE BOOL "Do not install nlohmann/json" FORCE)

if(WORMEDIT_NLOHMANN_JSON_SOURCE_DIR)
    if(NOT EXISTS "${WORMEDIT_NLOHMANN_JSON_SOURCE_DIR}/single_include/nlohmann/json.hpp")
        message(FATAL_ERROR "WORMEDIT_NLOHMANN_JSON_SOURCE_DIR must be a nlohmann/json 3.12.0 source checkout")
    endif()
    add_library(wormedit_nlohmann_json INTERFACE)
    target_include_directories(wormedit_nlohmann_json INTERFACE
        "${WORMEDIT_NLOHMANN_JSON_SOURCE_DIR}/single_include")
    target_compile_definitions(wormedit_nlohmann_json INTERFACE JSON_DIAGNOSTIC_POSITIONS=1)
    add_library(nlohmann_json::nlohmann_json ALIAS wormedit_nlohmann_json)
else()
    FetchContent_Declare(
        nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
        URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()
