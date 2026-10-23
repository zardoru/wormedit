#include "sys/resources/bga_usage_actions.h"
#include "sys/resources/bga_usage_view.h"
#include "../support/bga_usage_fixture.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
using namespace wormedit;
using namespace wormedit::sys;
namespace {
struct usage_fixture_t {
    document_history_t history = document_history_t::from(test::bga_usage_song());
    workspace_controller_t workspace = *workspace_controller_t::from(history.document).value;
    std::optional<editor_interaction_state_t> interaction;
    ui::bga_dock_request_t request{.selected_project = song_id_t{1}, .selected_resource = resource_id_t{20},
        .find_uses_open = true, .visible = true};
    ui::workspace_action_t action{};
    usage_fixture_t() {
        workspace.active_tab = workspace_tab_t::compose;
        const auto view = bga_usage_view_t::from(history.document, resource_id_t{20});
        action.kind = WORM_UI_WORKSPACE_ACTION_BGA_USAGE_ACTIVATE;
        action.bga_usage = {view.song, view.revision, view.resource, view.asset, view.rows.back().target};
    }
    auto activate() -> std::optional<std::string> {
        return dispatch_bga_usage_action(action, request, history, workspace, interaction);
    }
};
}
TEST_CASE("BGA use activation opens the exact chart cursor and event without document edits", "[sys][bga-usage]") {
    usage_fixture_t f;
    REQUIRE_FALSE(f.activate());
    CHECK(f.workspace.selected_chart == chart_id_t{8});
    REQUIRE(f.interaction);
    CHECK(f.interaction->cursor == local_position_t{measure_id_t{2}, 720});
    CHECK(f.interaction->selected_objects == std::vector<editor_selection_object_t>{{editor_selection_object_kind_t::bga_event, object_id_t{35}}});
    CHECK(f.history.document.revision == 4);
}
TEST_CASE("BGA use navigation preserves resource selection and Undo history", "[sys][bga-usage]") {
    usage_fixture_t f;
    f.workspace.selected_visual_asset = asset_id_t{11};
    REQUIRE_FALSE(f.activate());
    CHECK(f.workspace.selected_visual_asset == asset_id_t{11});
    CHECK(f.request.selected_resource == resource_id_t{20});
    CHECK(f.interaction->bga_lane_visible);
    CHECK(f.history.undo.empty());
    CHECK(f.history.document.charts.back().bga_events.front().resource == resource_id_t{20});
}
TEST_CASE("BGA usage rejects stale deleted hidden or changed row identity atomically", "[sys][bga-usage]") {
    usage_fixture_t f;
    const auto change = GENERATE(0, 1, 2, 3, 4, 5, 6, 7);
    switch (change) {
    case 0: ++f.action.bga_usage.revision; break;
    case 1: f.history.document.charts.back().bga_events.clear(); break;
    case 2: f.request.selected_resource = resource_id_t{21}; break;
    case 3: f.request.visible = false; break;
    case 4: f.request.find_uses_open = false; break;
    case 5: f.history.document.resources.clear(); break;
    case 6: f.action.bga_usage.target.kind = visual_event_kind_t::poor; break;
    case 7: ++f.action.bga_usage.target.position.row; break;
    }
    const auto chart = f.workspace.selected_chart;
    CHECK(f.activate().has_value());
    CHECK(f.workspace.selected_chart == chart);
    CHECK_FALSE(f.interaction);
    CHECK(f.history.undo.empty());
}
TEST_CASE("BGA usage reveals the event lane while retaining an existing chart view", "[sys][bga-usage]") {
    usage_fixture_t f;
    REQUIRE_FALSE(f.activate());
    f.interaction->bga_lane_visible = false;
    f.interaction->active_snap_denominator = 8;
    REQUIRE_FALSE(f.activate());
    CHECK(f.interaction->bga_lane_visible);
    CHECK(f.interaction->active_snap_denominator == 8);
}
TEST_CASE("BGA use activation selects every modeled event lane ", "[sys][bga-usage]") {
    usage_fixture_t f;
    const auto index = GENERATE(0U, 1U, 2U, 3U, 4U);
    f.action.bga_usage.target = bga_usage_view_t::from(f.history.document, resource_id_t{20}).rows[index].target;
    REQUIRE_FALSE(f.activate());
    CHECK(f.interaction->cursor == f.action.bga_usage.target.position);
    CHECK(f.interaction->selected_objects.front().id == f.action.bga_usage.target.event);
}
TEST_CASE("BGA video usage activates through the base event lane", "[sys][bga-usage]") {
    usage_fixture_t f;
    f.request.selected_resource = resource_id_t{22};
    const auto view = bga_usage_view_t::from(f.history.document, resource_id_t{22});
    f.action.bga_usage = {view.song, view.revision, view.resource, view.asset, view.rows.front().target};
    REQUIRE_FALSE(f.activate());
    CHECK(f.interaction->chart == chart_id_t{7});
    CHECK(f.interaction->selected_objects.front().id == object_id_t{34});
    CHECK(f.history.document.revision == 4);
}
