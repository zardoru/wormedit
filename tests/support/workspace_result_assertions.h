#pragma once

#include <wormedit/core/workspace.h>

#include <catch2/catch_test_macros.hpp>

namespace wormedit::test {

template <class Workspace>
void check_workspace_rejection(const diagnosed_t<std::optional<Workspace>>& result) {
    CHECK_FALSE((result.value && result.diagnostics.empty()));
}

template <class Workspace>
void require_workspace_result(const diagnosed_t<std::optional<Workspace>>& result) {
    REQUIRE(result.diagnostics.empty());
    REQUIRE(result.value);
}

} // namespace wormedit::test
