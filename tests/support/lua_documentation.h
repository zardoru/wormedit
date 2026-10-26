#pragma once
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

inline auto lua_documentation_example(const char* name) -> std::string {
    std::ifstream input(std::filesystem::path(WORMEDIT_LUA_EXAMPLE_DIR) / name, std::ios::binary);
    REQUIRE(input.is_open());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
