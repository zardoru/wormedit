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
    auto history = make_document_history(std::move(song));
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
    auto history = make_document_history(project_song());
    auto created = apply_project_chart_command(history, {.kind = project_chart_command_kind_t::create,
        .expected_revision = history.document.revision});
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
    auto history = make_document_history(project_song());
    project_chart_runtime_t runtime;
    WormUiProjectChartAction action{.kind = WORM_UI_PROJECT_CHART_DELETE,
        .document_revision = history.document.revision, .session_generation = 0,
        .chart_id = history.document.charts[0].id.value};
    auto result = apply_project_chart_ui_action(runtime, history, action);
    expect_project(!result.applied && !result.status.empty());
    action.session_generation = runtime.session_generation;
    --action.document_revision;
    result = apply_project_chart_ui_action(runtime, history, action);
    expect_project(!result.applied && !result.status.empty());
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
}

TEST_CASE("Project chart scalar projection exposes row rejection and refreshes after mutations", "[unit][project-view]") {
    auto history = make_document_history(project_song());
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
