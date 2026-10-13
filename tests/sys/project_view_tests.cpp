#include "sys/project_view.h"
#include <wormedit/core/chart_editor.h>
#include <catch2/catch_test_macros.hpp>

using namespace wormedit;
using namespace wormedit::sys;
namespace {
void expect_project(bool value) { CHECK(value); }
void require_project(bool value) { REQUIRE(value); }
auto project_song() -> song_document_t {
    song_document_t song{.id = song_id_t{1}, .revision = 4};
    auto history = document_history_t::from(std::move(song));
    const auto created = apply_new_chart_command(history, 4);
    require_project(static_cast<bool>(created));
    history.document.charts[0].metadata = {.name = "Normal", .author = "Author", .rating = signed_rational_t{7, 2}};
    const auto& chart = history.document.charts[0];
    history.document.output_groups = {{.name = "SSC", .charts = {chart.id}}};
    return history.document;
}
}

TEST_CASE("Project chart table projects metadata layout groups notes and timing", "[unit][project-view]") {
    auto song = project_song();
    const auto table = build_project_chart_table(song);
    require_project(table.rows.size() == 1);
    const auto& row = table.rows[0];
    expect_project(row.chart == song.charts[0].id && row.name == "Normal" && row.author == "Author");
    expect_project(row.rating == "3.50" && row.key_count == 7);
    expect_project(row.output_groups == "SSC" && row.output_group_count == 1);
    expect_project(row.note_count == 0 && row.length == "2.00 s" && row.diagnostic.empty());
}

TEST_CASE("Project chart projection truncates rows without limiting the document", "[unit][project-view]") {
    auto song = project_song();
    song.charts.resize(257, song.charts[0]);
    const auto table = build_project_chart_table(song);
    expect_project(table.rows.size() == 256 && table.total_charts == 257 && table.rows_truncated);
    expect_project(song.charts.size() == 257);
    song.charts[0].metadata.name = std::string(5000, 'N');
    const auto bounded = build_project_chart_table(song);
    expect_project(bounded.rows[0].name.size() == 4096);
}

TEST_CASE("Project chart table cache refreshes revisions and reopened sessions", "[unit][project-view]") {
    auto song = project_song();
    project_chart_table_cache_t cache;
    expect_project(project_chart_table_for(cache, song, 1).rows[0].name == "Normal");
    song.charts[0].metadata.name = "Unpublished";
    expect_project(project_chart_table_for(cache, song, 1).rows[0].name == "Normal");
    ++song.revision;
    expect_project(project_chart_table_for(cache, song, 1).rows[0].name == "Unpublished");
    song.charts[0].metadata.name = "Reopened";
    expect_project(project_chart_table_for(cache, song, 2).rows[0].name == "Reopened");
}

TEST_CASE("Project chart commands preserve atomic rejection and open newly created charts", "[unit][project-view]") {
    auto history = document_history_t::from(project_song());
    auto created = apply_project_chart_command(history, {.kind = project_chart_command_kind_t::create,
        .expected_revision = history.document.revision, .chart = history.document.charts[0].id});
    require_project(static_cast<bool>(created.command));
    expect_project(created.open_chart == history.document.charts.back().id);
    const auto revision = history.document.revision;
    const auto count = history.document.charts.size();
    const auto stale = apply_project_chart_command(history, {.kind = project_chart_command_kind_t::erase,
        .expected_revision = revision - 1, .chart = history.document.charts[0].id});
    expect_project(!stale.command && !stale.command.diagnostics.empty());
    expect_project(history.document.revision == revision && history.document.charts.size() == count);
    const auto opened = apply_project_chart_command(history, {.kind = project_chart_command_kind_t::open,
        .expected_revision = revision, .chart = *created.open_chart});
    expect_project(opened.open_chart == created.open_chart && !opened.command.applied);
    require_project(static_cast<bool>(apply_project_chart_command(history, {.kind = project_chart_command_kind_t::move_up,
        .expected_revision = revision, .chart = *created.open_chart}).command));
    expect_project(history.document.charts.front().id == *created.open_chart);
}

TEST_CASE("Project chart UI rejects closed sessions stale delete and malformed batch ratings", "[unit][project-view]") {
    auto history = document_history_t::from(project_song());
    project_chart_runtime_t runtime;
    WormUiProjectChartAction action{.kind = WORM_UI_PROJECT_CHART_DELETE,
        .document_revision = history.document.revision, .session_generation = 0,
        .chart_id = history.document.charts[0].id.value};
    auto result = apply_project_chart_ui_action(runtime, history, action);
    expect_project(!result.applied && !result.status.empty());
    expect_project(result.severity == notification_severity_t::warning);
    action.session_generation = runtime.session_generation;
    --action.document_revision;
    result = apply_project_chart_ui_action(runtime, history, action);
    expect_project(!result.applied && !result.status.empty());
    expect_project(result.severity == notification_severity_t::error);
    expect_project(runtime.row_errors.contains(action.chart_id));
    action = {.kind = WORM_UI_PROJECT_CHART_BATCH_METADATA, .document_revision = history.document.revision,
        .session_generation = runtime.session_generation, .selected_count = 1, .edit_rating = true,
        .rating_numerator = "3", .rating_denominator = "0"};
    action.selected_chart_ids[0] = history.document.charts[0].id.value;
    result = apply_project_chart_ui_action(runtime, history, action);
    expect_project(!result.applied && !result.status.empty());
    expect_project(history.undo.empty() && history.document.charts.size() == 1);
    action.edit_rating = false;
    result = apply_project_chart_ui_action(runtime, history, action);
    expect_project(!result.applied && runtime.row_errors.empty());
    expect_project(result.severity == notification_severity_t::info);
}

TEST_CASE("Project chart scalar projection exposes row rejection and refreshes after mutations", "[unit][project-view]") {
    auto history = document_history_t::from(project_song());
    project_chart_runtime_t runtime;
    WormUiSetupView view{.visible = true};
    project_chart_view(runtime, history.document, view);
    expect_project(view.chart_row_count == 1 && view.document_revision == history.document.revision);
    expect_project(view.song_id == history.document.id.value && view.session_generation == 1);
    runtime.row_errors[history.document.charts[0].id.value] = "Cannot delete the last chart";
    project_chart_view(runtime, history.document, view);
    expect_project(std::string_view{view.charts[0].diagnostic} == "Cannot delete the last chart");
    const auto result = apply_project_chart_ui_action(runtime, history,
        {.kind = WORM_UI_PROJECT_CHART_DUPLICATE, .document_revision = history.document.revision,
         .session_generation = 1, .chart_id = history.document.charts[0].id.value});
    require_project(result.applied);
    project_chart_view(runtime, history.document, view);
    expect_project(view.chart_row_count == 2 && std::string_view{view.charts[0].diagnostic}.empty());
}

TEST_CASE("Project New templates the selected four-key chart instead of the first chart or default profile", "[unit][project-view]") {
    auto history = document_history_t::from(project_song());
    lane_input_profile_t profile{.layout = {.profile_id = "stepmania:dance-single", .display_name = "4K",
        .lanes = {{.id = lane_id_t{1}, .name = "Left", .display_order = 0, .glyph = lane_glyph_t::arrow_left},
                  {.id = lane_id_t{2}, .name = "Down", .display_order = 1, .glyph = lane_glyph_t::arrow_down},
                  {.id = lane_id_t{3}, .name = "Up", .display_order = 2, .glyph = lane_glyph_t::arrow_up},
                  {.id = lane_id_t{4}, .name = "Right", .display_order = 3, .glyph = lane_glyph_t::arrow_right}}}};
    profile.input = *default_input_profile_for(profile.layout);
    const auto active = apply_new_chart_command(history, history.document.revision, std::move(profile), format_family_t::stepmania);
    require_project(static_cast<bool>(active));
    auto& selected = history.document.charts.back();
    selected.divisions_per_standard_measure = 3840;
    selected.beat_zero_audio_offset_seconds = {-7, 20};
    project_chart_runtime_t runtime;
    const auto added = apply_project_chart_ui_action(runtime, history,
        {.kind = WORM_UI_PROJECT_CHART_NEW, .document_revision = history.document.revision,
         .session_generation = runtime.session_generation}, *active.payload.chart);
    require_project(added.applied && added.open_chart.has_value());
    const auto& chart = history.document.charts.back();
    expect_project(chart.id == *added.open_chart && chart.id != *active.payload.chart);
    expect_project(chart.source_family == format_family_t::stepmania && chart.divisions_per_standard_measure == 3840);
    expect_project(chart.beat_zero_audio_offset_seconds == signed_rational_t{-7, 20});
    expect_project(chart.lane_layouts[0].layout.lanes.size() == 4 && chart.measures[0].notes.empty());
    const auto table = build_project_chart_table(history.document);
    expect_project(table.rows[0].key_count == 7 && table.rows.back().key_count == 4);
    expect_project(table.rows.back().mode == "4K");
    expect_project(validate_document(history.document).empty());
}

TEST_CASE("Project New allows an empty document fallback and rejects a missing active template otherwise", "[unit][project-view]") {
    auto empty = document_history_t::from(song_document_t{.id = song_id_t{1}, .revision = 4});
    const auto fallback = apply_project_chart_command(empty, {.kind = project_chart_command_kind_t::create,
        .expected_revision = 4});
    require_project(static_cast<bool>(fallback.command));
    expect_project(empty.document.charts.back().lane_layouts[0].layout.profile_id == make_bms_7k_profile().layout.profile_id);
    const auto before = empty.document.revision;
    const auto rejected = apply_project_chart_command(empty, {.kind = project_chart_command_kind_t::create,
        .expected_revision = before, .chart = chart_id_t{9999}});
    expect_project(!rejected.command.applied && !rejected.command.diagnostics.empty());
    expect_project(empty.document.charts.size() == 1 && empty.document.revision == before);
}

namespace {
auto reorder_history() -> document_history_t {
    auto history = document_history_t::from(project_song());
    const auto first = history.document.charts.front().id;
    require_project(static_cast<bool>(apply_duplicate_chart_command(history, history.document.revision, first)));
    require_project(static_cast<bool>(apply_duplicate_chart_command(history, history.document.revision, first)));
    history.undo.clear();
    return history;
}
auto chart_ids(const document_history_t& history) -> std::vector<chart_id_t> {
    std::vector<chart_id_t> ids;
    for (const auto& chart : history.document.charts) ids.push_back(chart.id);
    return ids;
}
}

TEST_CASE("Project grip reorder resolves stable destination and creates one atomic Undo", "[project-view][chart-reorder]") {
    auto history = reorder_history();
    const auto original = chart_ids(history);
    project_chart_runtime_t runtime;
    const auto result = apply_project_chart_ui_action(runtime, history,
        {.kind = WORM_UI_PROJECT_CHART_REORDER, .document_revision = history.document.revision,
         .session_generation = runtime.session_generation, .chart_id = original[0].value,
         .target_chart_id = original[2].value, .insert_after = true});
    require_project(result.applied);
    expect_project((chart_ids(history) == std::vector{original[1], original[2], original[0]}));
    expect_project(history.undo.size() == 1);
    require_project(static_cast<bool>(undo_document_command(history)));
    expect_project(chart_ids(history) == original);
    const auto moved_back = apply_project_chart_ui_action(runtime, history,
        {.kind = WORM_UI_PROJECT_CHART_REORDER, .document_revision = history.document.revision,
         .session_generation = runtime.session_generation, .chart_id = original[2].value,
         .target_chart_id = original[0].value, .insert_after = false});
    require_project(moved_back.applied);
    expect_project((chart_ids(history) == std::vector{original[2], original[0], original[1]}));
}

TEST_CASE("Project grip reorder rejects stale or missing targets and leaves adjacent drops unchanged", "[project-view][chart-reorder]") {
    auto history = reorder_history();
    const auto original = chart_ids(history);
    project_chart_runtime_t runtime;
    WormUiProjectChartAction action{.kind = WORM_UI_PROJECT_CHART_REORDER,
        .document_revision = history.document.revision, .session_generation = runtime.session_generation,
        .chart_id = original[0].value, .target_chart_id = original[1].value};
    expect_project(!apply_project_chart_ui_action(runtime, history, action).applied);
    expect_project(history.undo.empty());
    action.insert_after = true;
    --action.document_revision;
    expect_project(!apply_project_chart_ui_action(runtime, history, action).applied);
    action.document_revision = history.document.revision;
    ++action.session_generation;
    expect_project(!apply_project_chart_ui_action(runtime, history, action).applied);
    action.session_generation = runtime.session_generation;
    action.target_chart_id = 99999;
    expect_project(!apply_project_chart_ui_action(runtime, history, action).applied);
    expect_project(chart_ids(history) == original && history.undo.empty());
}

TEST_CASE("Project output group actions project complete editable membership and reject foreign sessions", "[unit][project-view][output-groups]") {
    auto history = document_history_t::from(project_song());
    project_chart_runtime_t runtime;
    task_scheduler_t scheduler{1, 1};
    WormUiSetupView view{.visible = true};
    project_chart_view(runtime, history.document, view);
    require_project(view.output_group_count == 1);
    expect_project(view.output_groups[0].chart_ids[0] == history.document.charts[0].id.value);
    WormUiProjectOutputGroupAction action{.kind = WORM_UI_PROJECT_OUTPUT_GROUP_CREATE,
        .project_id = history.document.id.value, .document_revision = history.document.revision,
        .session_generation = runtime.session_generation, .name = "BMSON", .path = "exports/chart.bmson", .family = 4,
        .chart_count = 1};
    action.chart_ids[0] = history.document.charts[0].id.value;
    ++action.session_generation;
    expect_project(!apply_project_output_group_ui_action(runtime, history, action, scheduler).applied);
    --action.session_generation;
    require_project(apply_project_output_group_ui_action(runtime, history, action, scheduler).applied);
    project_chart_view(runtime, history.document, view);
    require_project(view.output_group_count == 2);
    expect_project(std::string_view{view.output_groups[1].path} == "exports/chart.bmson");
    expect_project(view.output_groups[1].family == 4);
    expect_project(!apply_project_output_group_ui_action(runtime, history, action, scheduler).applied);
    action.document_revision = history.document.revision;
    ++action.project_id;
    expect_project(!apply_project_output_group_ui_action(runtime, history, action, scheduler).applied);
}

TEST_CASE("Project conversion preview runs through the worker service and retires on document changes", "[unit][project-view][output-groups]") {
    auto history = document_history_t::from(project_song());
    history.document.output_groups[0].family = format_family_t::native_project;
    history.document.output_groups[0].relative_output_path = "exports/song.wormedit";
    project_chart_runtime_t runtime;
    task_scheduler_t scheduler{1, 1};
    WormUiSetupView view{.visible = true};
    project_chart_view(runtime, history.document, view);
    WormUiProjectOutputGroupAction action{.kind = WORM_UI_PROJECT_OUTPUT_GROUP_PREVIEW,
        .project_id = history.document.id.value, .document_revision = history.document.revision,
        .session_generation = runtime.session_generation};
    const auto before = history.document.revision;
    (void)apply_project_output_group_ui_action(runtime, history, action, scheduler);
    require_project(static_cast<bool>(runtime.preview_graph));
    require_project(runtime.preview_graph.wait());
    project_chart_view(runtime, history.document, view);
    expect_project(std::string_view{view.output_groups[0].preview_status}.starts_with("Preview: 1 file"));
    expect_project(history.document.revision == before);
    expect_project(history.undo.empty());
    (void)apply_project_output_group_ui_action(runtime, history, action, scheduler);
    ++history.document.revision;
    project_chart_view(runtime, history.document, view);
    expect_project(!runtime.preview_graph);
    expect_project(std::string_view{view.output_groups[0].preview_status} == "Preview not run");
}
