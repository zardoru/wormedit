#include "sys/application_commands.h"

#include <wormedit/core/command_registry.h>

#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <string>

using namespace wormedit::sys;

TEST_CASE("application command IDs parse into typed commands", "[unit][sys][commands]") {
    CHECK(parse_application_command("history.undo") ==
          application_command_t{.kind = application_command_kind_t::history_undo});
    CHECK(parse_application_command("workspace.open_compare_validate") ==
          application_command_t{.kind = application_command_kind_t::workspace_open_compare_validate});
    CHECK(parse_application_command("semantic_layer.remove") ==
          application_command_t{.kind = application_command_kind_t::semantic_layer_remove});
    CHECK((parse_application_command("semantic_layer.select_slot_12") ==
           application_command_t{.kind = application_command_kind_t::semantic_layer_select_slot,
                                 .semantic_layer_slot = 12}));
}

TEST_CASE("application command parser rejects unknown IDs and malformed slots",
          "[unit][sys][commands]") {
    CHECK_FALSE(parse_application_command("not.a.command"));
    CHECK_FALSE(parse_application_command("history.undo.extra"));

    CHECK_FALSE(parse_application_command("semantic_layer.select_slot_"));
    CHECK_FALSE(parse_application_command("semantic_layer.select_slot_0"));
    CHECK_FALSE(parse_application_command("semantic_layer.select_slot_-1"));
    CHECK_FALSE(parse_application_command("semantic_layer.select_slot_2x"));
    CHECK_FALSE(parse_application_command("semantic_layer.select_slot_1.0"));

    const auto overflow = std::to_string(std::numeric_limits<std::size_t>::max()) + "0";
    CHECK_FALSE(parse_application_command("semantic_layer.select_slot_" + overflow));
}

TEST_CASE("every built-in command ID has a typed representation", "[unit][sys][commands]") {
    const auto registry = wormedit::make_default_editor_command_registry(
        [](const wormedit::command_invocation_context_t&) {
            return wormedit::command_invocation_result_t{};
        });
    for (const auto& command : registry.commands) {
        INFO(command.id.value);
        CHECK(parse_application_command(command.id.value).has_value());
    }
}
