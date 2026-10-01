#include "ui/nuklear/setup_state.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Nuklear setup view bounds every scalar export choice",
          "[unit][frontend][setup]") {
    WormUiSetupView view{
        .visible = true,
        .stepmania_file_profile = WORM_UI_STEPMANIA_FILE_SSC,
        .stepmania_timing_syntax = WORM_UI_STEPMANIA_TIMING_NATIVE_WARPS,
        .stepmania_implementation = WORM_UI_STEPMANIA_IMPLEMENTATION_STEP_MANIA_5_1,
        .stepmania_policy_valid = true,
        .stepmania_policy_status = "Ready",
    };
    CHECK(worm_ui_nuklear_setup_view_valid(&view));

    view.stepmania_file_profile = static_cast<WormUiStepmaniaFileProfile>(99);
    CHECK_FALSE(worm_ui_nuklear_setup_view_valid(&view));
    view.stepmania_file_profile = WORM_UI_STEPMANIA_FILE_SSC;
    view.stepmania_policy_status = nullptr;
    CHECK_FALSE(worm_ui_nuklear_setup_view_valid(&view));

    WormUiSetupView hidden{};
    CHECK(worm_ui_nuklear_setup_view_valid(&hidden));
    CHECK_FALSE(worm_ui_nuklear_setup_view_valid(nullptr));
}

namespace {
void expect_project_state(bool value) { CHECK(value); }
auto selection_view(WormUiProjectChartRow* rows, size_t count) -> WormUiSetupView {
    return {.visible = true, .song_id = 1, .document_revision = 4, .session_generation = 1,
        .charts = rows, .chart_row_count = count, .stepmania_policy_status = "Ready"};
}
}

TEST_CASE("Project chart selection follows stable IDs through reorder deletion and sessions", "[unit][frontend][setup]") {
    WormUiProjectChartRow rows[]{{.chart_id = 10}, {.chart_id = 20}, {.chart_id = 30}};
    auto view = selection_view(rows, 3);
    project_chart_selection_t state;
    worm_ui_nuklear_reconcile_project_selection(state, view);
    worm_ui_nuklear_toggle_project_chart_selection(state, 10);
    worm_ui_nuklear_toggle_project_chart_selection(state, 30);
    std::swap(rows[0], rows[2]);
    ++view.document_revision;
    worm_ui_nuklear_reconcile_project_selection(state, view);
    expect_project_state(state.count == 2 && worm_ui_nuklear_project_chart_selected(state, 10));
    expect_project_state(worm_ui_nuklear_project_chart_selected(state, 30));
    view.chart_row_count = 2; // reordered chart 10 is now removed
    worm_ui_nuklear_reconcile_project_selection(state, view);
    expect_project_state(state.count == 1 && state.ids[0] == 30);
    ++view.session_generation;
    worm_ui_nuklear_reconcile_project_selection(state, view);
    expect_project_state(state.count == 0);
}

TEST_CASE("Project delete confirmation expires after revisions and session changes", "[unit][frontend][setup]") {
    WormUiProjectChartRow rows[]{{.chart_id = 10}};
    auto view = selection_view(rows, 1);
    project_chart_selection_t state;
    worm_ui_nuklear_reconcile_project_selection(state, view);
    state.pending_delete = 10;
    state.pending_delete_revision = 4;
    worm_ui_nuklear_reconcile_project_selection(state, view);
    expect_project_state(state.pending_delete == 10);
    ++view.document_revision;
    worm_ui_nuklear_reconcile_project_selection(state, view);
    expect_project_state(state.pending_delete == 0);
    state.pending_delete = 10;
    state.pending_delete_revision = 5;
    ++view.session_generation;
    worm_ui_nuklear_reconcile_project_selection(state, view);
    expect_project_state(state.pending_delete == 0);
}

TEST_CASE("Project chart view bounds rows and requires complete scalar row strings", "[unit][frontend][setup]") {
    WormUiProjectChartRow rows[]{{.chart_id = 10, .name = "Chart", .author = "Author", .rating = "Unset",
        .mode = "7K", .output_groups = "Unassigned", .length = "2.00 s", .diagnostic = ""}};
    auto view = selection_view(rows, 1);
    expect_project_state(worm_ui_nuklear_setup_view_valid(&view));
    view.chart_row_count = 257;
    expect_project_state(!worm_ui_nuklear_setup_view_valid(&view));
    view.chart_row_count = 1;
    rows[0].length = nullptr;
    expect_project_state(!worm_ui_nuklear_setup_view_valid(&view));
    view.charts = nullptr;
    expect_project_state(!worm_ui_nuklear_setup_view_valid(&view));
}
