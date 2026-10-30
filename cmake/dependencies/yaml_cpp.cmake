# Post-1.0 Quaver adapter: owner-approved YAML dependency, 2026-10-28.
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(yaml_cpp
    URL https://codeload.github.com/jbeder/yaml-cpp/tar.gz/refs/tags/0.8.0
    URL_HASH SHA256=fbe74bbdcee21d656715688706da3c8becfd946d92cd44705cc6098bb23b3a16)
# Upstream 0.8.0 still declares CMake 3.4. Keep its compatibility policy local
# so fresh CMake 4 configurations work without changing this project's policy.
block()
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    FetchContent_MakeAvailable(yaml_cpp)
endblock()
FetchContent_GetProperties(yaml_cpp)
