#include "sys/project/compare_view.h"
#include "sys/project/workspace_view.h"
#include <wormedit/core/bms_branch_editor.h>
#include <wormedit/core/bms_program_edit.h>
#include <wormedit/core/chart_editor.h>
#include <wormedit/formats/bms.h>
#include <catch2/catch_test_macros.hpp>
#include <set>

using namespace wormedit;
using namespace wormedit::sys;
namespace {
auto scoped_history() -> document_history_t {
    const std::string text = "#BPM 120\n#WAV01 a.wav\n#00011:01\n#RANDOM 2\n#IF 1\n"
        "#00012:0001\n#ELSE\n#00013:0001\n#ENDIF\n#ENDRANDOM\n";
    auto parsed = parse_bms_bytes(std::as_bytes(std::span{text.data(), text.size()}),
        {.lane_profile_id = "bme-7k-outside-scratch"});
    REQUIRE(parsed.value.song);
    auto history = document_history_t::from(std::move(*parsed.value.song));
    const auto* owner = history.document.prepared_bms_programs.front().get();
    const auto first = owner->difficulty();
    const auto selector = std::ranges::find_if(owner->prepared().program.nodes(), [](const auto& node) {
        return std::holds_alternative<bms_random_scope_t>(node.value);
    });
    REQUIRE(apply_bms_program_preview_command(history,
        {.stamp = chart_stamp_t::from(history, first), .node = selector->id, .value = 1}).applied);
    REQUIRE(apply_duplicate_chart_command(history, first).command.applied);
    owner = history.document.find_bms_program(first);
    const auto inactive = std::ranges::find_if(owner->prepared().program.nodes(), [](const auto& node) {
        return std::holds_alternative<bms_else_scope_t>(node.value);
    });
    history.bms_edit_target = {.difficulty = first, .chart = *owner->prepared().program.target_chart(inactive->id)};
    return history;
}
auto note_ids(const chart_document_t& chart) -> std::set<object_id_t> {
    std::set<object_id_t> result;
    for (const auto& measure : chart.measures)
        for (const auto& note : measure.notes) result.insert(note.id);
    return result;
}
auto note_ids(const ui::compose_view_t& view) -> std::set<object_id_t> {
    std::set<object_id_t> result;
    for (const auto& note : view.note_endpoints) result.insert(note.note_id);
    return result;
}
void check_playback_panes(const frontend_workspace_view_t& first, const frontend_workspace_view_t& second,
    const document_history_t& history, const ui::compare_view_t& view) {
    CHECK(note_ids(first.view.compose) == note_ids(*history.document.read_chart(view.first_chart_id)));
    CHECK(note_ids(second.view.compose) == note_ids(*history.document.read_chart(view.second_chart_id)));
    CHECK(first.view.compose.differences.empty());
    CHECK(second.view.compose.differences.empty());
}
void check_read_only(const frontend_workspace_view_t& first, const frontend_workspace_view_t& second,
    const ui::compare_view_t& view) {
    CHECK(first.view.compose.input_occluded);
    CHECK(second.view.compose.input_occluded);
    REQUIRE(view.charts.size() == 1);
    CHECK(view.charts.front().id == view.second_chart_id);
}
}

TEST_CASE("Comparison panes use playback while retaining inactive BMS authoring selection", "[compare-view][bms]") {
    auto history = scoped_history();
    const auto target = history.bms_edit_target;
    auto binding = workspace_controller_t::from(history);
    REQUIRE(binding.value);
    auto& workspace = *binding.value;
    workspace.selected_chart = target->difficulty;
    workspace.active_tab = workspace_tab_t::compare_validate;
    frontend_workspace_view_t result, first, second;
    compare_scene_cache_t cache;
    application_command_state_t application;
    populate_compare_view(result, first, second, history, workspace, cache, application, 1280, 900);
    REQUIRE(result.view.compare.has_two_difficulties);
    check_playback_panes(first, second, history, result.view.compare);
    check_read_only(first, second, result.view.compare);
    const auto* snapshot = &*cache.panes.value().playback;
    populate_compare_view(result, first, second, history, workspace, cache, application, 1280, 900);
    CHECK(&*cache.panes.value().playback == snapshot);
    CHECK(history.bms_edit_target == target);
    CHECK(note_ids(*history.read_chart(target->difficulty)) != note_ids(first.view.compose));
}
