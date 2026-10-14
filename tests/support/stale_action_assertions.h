#pragma once

#include <wormedit/core/command.h>

#include <catch2/catch_test_macros.hpp>

#include <string_view>

namespace wormedit::test {

inline void check_stale_action_message(std::string_view message, std::string_view label) {
    CHECK(message == stale_action_message(label));
}

inline void check_stale_action_message(const diagnostic_list_t& diagnostics,
                                        std::string_view label) {
    REQUIRE_FALSE(diagnostics.empty());
    check_stale_action_message(diagnostics.front().message, label);
}

} // namespace wormedit::test
