#include "ui/nuklear/setup_state.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <vector>

using namespace wormedit;

TEST_CASE("Nuklear setup view bounds every scalar export choice",
          "[unit][frontend][setup]") {
    wormedit::ui::project_output_group_view_t group{
        .name = "StepMania",
        .family = format_family_t::stepmania,
        .path = "ssc",
        .preview_status = "Ready",
        .stepmania = stepmania_export_policy_t{stepmania_profile_t::ssc,
            stepmania_timing_syntax_t::native_warps, stepmania_implementation_profile_t::stepmania_5_1},
        };
    wormedit::ui::setup_view_t view{
        .visible = true,
        .output_groups = {group},
    };
    CHECK(worm_ui_nuklear_setup_view_valid(&view));

    auto& policy = view.output_groups.front().stepmania;
    policy->file_profile = static_cast<stepmania_profile_t>(99);
    CHECK_FALSE(worm_ui_nuklear_setup_view_valid(&view));
    policy->file_profile = stepmania_profile_t::ssc;
    policy.reset();
    CHECK_FALSE(worm_ui_nuklear_setup_view_valid(&view));

    wormedit::ui::setup_view_t hidden{};
    CHECK(worm_ui_nuklear_setup_view_valid(&hidden));
    CHECK_FALSE(worm_ui_nuklear_setup_view_valid(nullptr));
}

namespace {
void expect_project_state(bool value) { CHECK(value); }
auto selection_view(wormedit::ui::project_chart_row_view_t* rows, size_t count) -> wormedit::ui::setup_view_t {
    return {.visible = true, .song_id = song_id_t{1}, .document_revision = 4, .session_generation = 1,
        .charts = {rows, rows + count}};
}
}

TEST_CASE("Project chart selection follows stable IDs through reorder deletion and sessions", "[unit][frontend][setup]") {
    wormedit::ui::project_chart_row_view_t rows[]{{.chart_id = chart_id_t{10}}, {.chart_id = chart_id_t{20}}, {.chart_id = chart_id_t{30}}};
    auto view = selection_view(rows, 3);
    project_chart_selection_t state;
    worm_ui_nuklear_reconcile_project_selection(state, view);
    worm_ui_nuklear_toggle_project_chart_selection(state, 10);
    worm_ui_nuklear_toggle_project_chart_selection(state, 30);
    std::swap(view.charts[0], view.charts[2]);
    ++view.document_revision;
    worm_ui_nuklear_reconcile_project_selection(state, view);
    expect_project_state(state.ids.size() == 2 && worm_ui_nuklear_project_chart_selected(state, 10));
    expect_project_state(worm_ui_nuklear_project_chart_selected(state, 30));
    view.charts.resize(2); // reordered chart 10 is now removed
    worm_ui_nuklear_reconcile_project_selection(state, view);
    expect_project_state(state.ids.size() == 1 && state.ids[0] == 30);
    ++view.session_generation;
    worm_ui_nuklear_reconcile_project_selection(state, view);
    expect_project_state(state.ids.size() == 0);
}

TEST_CASE("Project delete confirmation expires after revisions and session changes", "[unit][frontend][setup]") {
    wormedit::ui::project_chart_row_view_t rows[]{{.chart_id = chart_id_t{10}}};
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

TEST_CASE("Project chart view owns every row and requires valid identity", "[unit][frontend][setup]") {
    wormedit::ui::project_chart_row_view_t rows[]{{.chart_id = chart_id_t{10}, .name = "Chart", .author = "Author", .rating = "Unset",
        .mode = "7K", .output_groups = "Unassigned", .length = "2.00 s", .diagnostic = ""}};
    auto view = selection_view(rows, 1);
    expect_project_state(worm_ui_nuklear_setup_view_valid(&view));
    view.charts.resize(257, view.charts.front());
    expect_project_state(worm_ui_nuklear_setup_view_valid(&view));
    view.charts.resize(1);
    view.charts.front().chart_id = {};
    expect_project_state(!worm_ui_nuklear_setup_view_valid(&view));
    view.charts.clear();
    expect_project_state(worm_ui_nuklear_setup_view_valid(&view));
}

namespace {
const std::array<std::uint64_t, 5> shown_rows{10, 20, 30, 40, 50};

auto selected_ids(const project_chart_selection_t& state) -> std::vector<std::uint64_t> {
    return {state.ids.begin(), state.ids.begin() + static_cast<std::ptrdiff_t>(state.ids.size())};
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
    CHECK(state.ids.size() == shown_rows.size());
    worm_ui_nuklear_toggle_all_project_charts(state, shown_rows);
    CHECK(state.ids.size() == 0);
}

TEST_CASE("Selection anchor is dropped when its chart leaves the table", "[unit][frontend][setup]") {
    wormedit::ui::project_chart_row_view_t rows[]{{.chart_id = chart_id_t{10}}, {.chart_id = chart_id_t{20}}};
    auto view = selection_view(rows, 2);
    project_chart_selection_t state;
    worm_ui_nuklear_reconcile_project_selection(state, view);
    worm_ui_nuklear_click_project_chart(state, std::array<std::uint64_t, 2>{10, 20}, 20, false, false);
    CHECK(state.anchor == 20);
    view.charts.resize(1);
    worm_ui_nuklear_reconcile_project_selection(state, view);
    CHECK(state.anchor == 0);
    CHECK(state.ids.size() == 0);
}

TEST_CASE("Project chart drag retains stable destination IDs and captured guards", "[frontend][setup][chart-reorder]") {
    wormedit::ui::project_chart_row_view_t rows[]{{.chart_id = chart_id_t{10}}, {.chart_id = chart_id_t{20}}, {.chart_id = chart_id_t{30}}};
    auto view = selection_view(rows, 3);
    project_chart_drag_t drag;
    expect_project_state(worm_ui_nuklear_begin_chart_drag(drag, view, 10));
    worm_ui_nuklear_target_chart_drag(drag, view, 30, true);
    const auto result = worm_ui_nuklear_finish_chart_drag(drag, view);
    expect_project_state(result.kind == WORM_UI_PROJECT_CHART_REORDER);
    expect_project_state(result.chart_id == chart_id_t{10} && result.target_chart_id == chart_id_t{30} && result.insert_after);
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
    wormedit::ui::project_chart_row_view_t rows[]{{.chart_id = chart_id_t{10}}, {.chart_id = chart_id_t{20}}};
    const auto original = selection_view(rows, 2);
    for (int change = 0; change < 5; ++change) {
        auto view = original;
        project_chart_drag_t drag;
        expect_project_state(worm_ui_nuklear_begin_chart_drag(drag, view, 10));
        worm_ui_nuklear_target_chart_drag(drag, view, 20, false);
        if (change == 0) ++view.document_revision;
        if (change == 1) ++view.session_generation;
        if (change == 2) view.visible = false;
        if (change == 3) view.charts.resize(1);
        if (change == 4) ++view.song_id.value;
        expect_project_state(worm_ui_nuklear_finish_chart_drag(drag, view).kind == WORM_UI_PROJECT_CHART_NONE);
        expect_project_state(drag.source == 0);
    }
}
