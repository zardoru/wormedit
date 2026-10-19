#include "ui/nuklear/setup_state.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <vector>

TEST_CASE("Nuklear setup view bounds every scalar export choice",
          "[unit][frontend][setup]") {
    WormUiProjectOutputGroupView group{
        .name = "StepMania",
        .family = 2,
        .path = "ssc",
        .preview_status = "Ready",
        .has_stepmania_policy = true,
        .stepmania_file_profile = WORM_UI_STEPMANIA_FILE_SSC,
        .stepmania_timing_syntax = WORM_UI_STEPMANIA_TIMING_NATIVE_WARPS,
        .stepmania_implementation = WORM_UI_STEPMANIA_IMPLEMENTATION_STEP_MANIA_5_1,
        };
    WormUiSetupView view{
        .visible = true,
        .output_groups = &group,
        .output_group_count = 1,
    };
    CHECK(worm_ui_nuklear_setup_view_valid(&view));

    group.stepmania_file_profile = static_cast<WormUiStepmaniaFileProfile>(99);
    CHECK_FALSE(worm_ui_nuklear_setup_view_valid(&view));
    group.stepmania_file_profile = WORM_UI_STEPMANIA_FILE_SSC;
    group.has_stepmania_policy = false;
    CHECK_FALSE(worm_ui_nuklear_setup_view_valid(&view));

    WormUiSetupView hidden{};
    CHECK(worm_ui_nuklear_setup_view_valid(&hidden));
    CHECK_FALSE(worm_ui_nuklear_setup_view_valid(nullptr));
}

namespace {
void expect_project_state(bool value) { CHECK(value); }
auto selection_view(WormUiProjectChartRow* rows, size_t count) -> WormUiSetupView {
    return {.visible = true, .song_id = 1, .document_revision = 4, .session_generation = 1,
        .charts = rows, .chart_row_count = count};
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

namespace {
const std::array<std::uint64_t, 5> shown_rows{10, 20, 30, 40, 50};

auto selected_ids(const project_chart_selection_t& state) -> std::vector<std::uint64_t> {
    return {state.ids.begin(), state.ids.begin() + static_cast<std::ptrdiff_t>(state.count)};
}
}

TEST_CASE("Project row clicks: plain selects one, Ctrl toggles, Shift selects the range from the anchor",
          "[unit][frontend][setup]") {
    project_chart_selection_t state;
    worm_ui_nuklear_click_project_chart(state, shown_rows, 20, false, false);
    CHECK(selected_ids(state) == std::vector<std::uint64_t>{20});
    worm_ui_nuklear_click_project_chart(state, shown_rows, 40, true, false);
    CHECK(selected_ids(state) == std::vector<std::uint64_t>{20, 40});
}

TEST_CASE("Project row Shift ranges extend from the last clicked row", "[unit][frontend][setup]") {
    project_chart_selection_t state;
    worm_ui_nuklear_click_project_chart(state, shown_rows, 20, false, false);
    worm_ui_nuklear_click_project_chart(state, shown_rows, 40, true, false);
    // The Ctrl-click on 40 moved the anchor there, so Shift ranges start from 40.
    worm_ui_nuklear_click_project_chart(state, shown_rows, 10, false, true);
    CHECK(selected_ids(state) == std::vector<std::uint64_t>{10, 20, 30, 40});
    CHECK(state.anchor == 40);
    worm_ui_nuklear_click_project_chart(state, shown_rows, 50, false, true);
    CHECK(selected_ids(state) == std::vector<std::uint64_t>{40, 50});
    worm_ui_nuklear_click_project_chart(state, shown_rows, 30, true, false);
    CHECK(selected_ids(state) == std::vector<std::uint64_t>{40, 50, 30});
    CHECK(state.anchor == 30);
}

TEST_CASE("Shift-click without an anchor on screen behaves as a plain click", "[unit][frontend][setup]") {
    project_chart_selection_t state;
    worm_ui_nuklear_click_project_chart(state, shown_rows, 30, false, true);
    CHECK(selected_ids(state) == std::vector<std::uint64_t>{30});
    CHECK(state.anchor == 30);
}

TEST_CASE("Header select-all toggles every shown row and reports partial state", "[unit][frontend][setup]") {
    project_chart_selection_t state;
    CHECK(worm_ui_nuklear_project_selection_summary(state, shown_rows) == project_selection_summary_t::none);
    worm_ui_nuklear_click_project_chart(state, shown_rows, 10, false, false);
    CHECK(worm_ui_nuklear_project_selection_summary(state, shown_rows) == project_selection_summary_t::partial);
    worm_ui_nuklear_toggle_all_project_charts(state, shown_rows);
    CHECK(worm_ui_nuklear_project_selection_summary(state, shown_rows) == project_selection_summary_t::all);
    CHECK(state.count == shown_rows.size());
    worm_ui_nuklear_toggle_all_project_charts(state, shown_rows);
    CHECK(state.count == 0);
}

TEST_CASE("Selection anchor is dropped when its chart leaves the table", "[unit][frontend][setup]") {
    WormUiProjectChartRow rows[]{{.chart_id = 10}, {.chart_id = 20}};
    auto view = selection_view(rows, 2);
    project_chart_selection_t state;
    worm_ui_nuklear_reconcile_project_selection(state, view);
    worm_ui_nuklear_click_project_chart(state, std::array<std::uint64_t, 2>{10, 20}, 20, false, false);
    CHECK(state.anchor == 20);
    view.chart_row_count = 1;
    worm_ui_nuklear_reconcile_project_selection(state, view);
    CHECK(state.anchor == 0);
    CHECK(state.count == 0);
}

TEST_CASE("Project chart drag retains stable destination IDs and captured guards", "[frontend][setup][chart-reorder]") {
    WormUiProjectChartRow rows[]{{.chart_id = 10}, {.chart_id = 20}, {.chart_id = 30}};
    auto view = selection_view(rows, 3);
    project_chart_drag_t drag;
    expect_project_state(worm_ui_nuklear_begin_chart_drag(drag, view, 10));
    worm_ui_nuklear_target_chart_drag(drag, view, 30, true);
    const auto result = worm_ui_nuklear_finish_chart_drag(drag, view);
    expect_project_state(result.kind == WORM_UI_PROJECT_CHART_REORDER);
    expect_project_state(result.chart_id == 10 && result.target_chart_id == 30 && result.insert_after);
    expect_project_state(result.document_revision == 4 && result.session_generation == 1);
    expect_project_state(drag.source == 0);
    expect_project_state(worm_ui_nuklear_begin_chart_drag(drag, view, 20));
    worm_ui_nuklear_target_chart_drag(drag, view, 20, false);
    expect_project_state(worm_ui_nuklear_finish_chart_drag(drag, view).kind == WORM_UI_PROJECT_CHART_NONE);
    expect_project_state(worm_ui_nuklear_begin_chart_drag(drag, view, 20));
    worm_ui_nuklear_target_chart_drag(drag, view, 999, false);
    expect_project_state(worm_ui_nuklear_finish_chart_drag(drag, view).kind == WORM_UI_PROJECT_CHART_NONE);
}

TEST_CASE("Project chart drags cancel when their document session or visible rows expire", "[frontend][setup][chart-reorder]") {
    WormUiProjectChartRow rows[]{{.chart_id = 10}, {.chart_id = 20}};
    const auto original = selection_view(rows, 2);
    for (int change = 0; change < 5; ++change) {
        auto view = original;
        project_chart_drag_t drag;
        expect_project_state(worm_ui_nuklear_begin_chart_drag(drag, view, 10));
        worm_ui_nuklear_target_chart_drag(drag, view, 20, false);
        if (change == 0) ++view.document_revision;
        if (change == 1) ++view.session_generation;
        if (change == 2) view.visible = false;
        if (change == 3) view.chart_row_count = 1;
        if (change == 4) ++view.song_id;
        expect_project_state(worm_ui_nuklear_finish_chart_drag(drag, view).kind == WORM_UI_PROJECT_CHART_NONE);
        expect_project_state(drag.source == 0);
    }
}
