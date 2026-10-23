#include "nuklear_event_harness.h"
#include "ui/nuklear/compose/compose_state.h"
#include <catch2/catch_test_macros.hpp>
#include <array>
#include <algorithm>
using namespace wormedit;
namespace {
struct bga_uses_ui_t {
    nuklear_event_harness_t harness{1600, 1000};
    ui::bga_usage_target_t target{chart_id_t{99}, object_id_t{555}, {measure_id_t{88}, 480},
                                visual_event_kind_t::layer2, resource_id_t{20}};
    bga_uses_ui_t() {
        REQUIRE(harness.ready());
        auto& state = worm_ui_nuklear_compose_state();
        state.dock_layout.layout.activate(wormedit::dock_panel_t::images, false);
        state.dock_layout.layout.show(wormedit::dock_panel_t::sounds, false);
        state.dock_layout.layout.show(wormedit::dock_panel_t::layers, false);
        (void)harness.frame();
        auto& view = harness.view().compose;
        view.bga_all_resource_ids = {20};
        view.bga_matching_ids = {20};
        view.bga_total_matching = 1;
        view.bga_rows = {{.resource_id = resource_id_t{20}, .name = "image.png"}};
        view.bga_usage = {.song = view.project_id, .revision = view.document_revision,
            .resource = resource_id_t{20}, .asset = asset_id_t{10}, .valid = true, .total_matching = 1,
            .rows = {{target, "S · M2:480 · Layer 2 · #555"}}};
        state.bga_catalog.bga_selection.primary = 20;
        state.bga_catalog.bga_selection.selected = {20};
        (void)harness.frame();
    }
    auto click(std::string_view text) -> std::array<workspace_action_snapshot_t, 2> {
        const auto center = harness.drawn_text_center(text);
        if (!center) FAIL("Missing text: " << text << "\n" << harness.drawn_text());
        const auto [x, y] = *center;
        const auto pressed = harness.frame(std::array{test::mouse_motion(x, y), test::mouse_button(true, x, y)});
        const auto released = harness.frame(std::array{test::mouse_motion(x, y), test::mouse_button(false, x, y)});
        return {pressed, released};
    }
    void open() { (void)click("Find uses"); (void)harness.frame(); }
};
}
TEST_CASE("BGA dock Find uses emits the complete stable event identity", "[event-harness][bga-usage]") {
    bga_uses_ui_t f;
    f.open();
    const auto frames = f.click("S · M2:480 · Layer 2 · #555");
    CHECK(std::ranges::count_if(frames, [](const auto& frame) {
        return frame.get().kind != WORM_UI_WORKSPACE_ACTION_NONE;
    }) == 1);
    const auto activated = std::ranges::find_if(frames, [](const auto& frame) {
        return frame.get().kind == WORM_UI_WORKSPACE_ACTION_BGA_USAGE_ACTIVATE;
    });
    REQUIRE(activated != frames.end());
    CHECK(activated->get().bga_usage.target == f.target);
    CHECK(activated->get().bga_usage.resource == resource_id_t{20});
    ui::bga_dock_request_t request;
    worm_ui_frontend_bga_dock_request(&request);
    CHECK(request.find_uses_open);
    CHECK(request.selected_resource == resource_id_t{20});
}
TEST_CASE("BGA dock never offers a stale row and can close Find uses", "[event-harness][bga-usage]") {
    bga_uses_ui_t f;
    f.open();
    ++f.harness.view().compose.document_revision;
    (void)f.harness.frame();
    CHECK_FALSE(f.harness.drawn_text_center("S · M2:480 · Layer 2 · #555"));
    CHECK(f.harness.drawn_text_center("Refreshing BGA uses…"));
    (void)f.click("Hide uses");
    CHECK_FALSE(worm_ui_nuklear_compose_state().bga_catalog.find_uses_open);
}
TEST_CASE("BGA dock preserves selected resource across chart navigation and displays empty uses", "[event-harness][bga-usage]") {
    bga_uses_ui_t f;
    f.open();
    auto& view = f.harness.view().compose;
    view.chart_id = chart_id_t{99};
    view.bga_usage.rows.clear();
    view.bga_usage.total_matching = 0;
    (void)f.harness.frame();
    CHECK(worm_ui_nuklear_compose_state().bga_catalog.bga_selection.primary == 20);
    CHECK(f.harness.drawn_text_center("No BGA uses."));
}
