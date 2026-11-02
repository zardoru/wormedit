#include "nuklear_event_harness.h"
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <string_view>

namespace {
using namespace wormedit;
void check_native_export_action(const ui::project_output_group_action_t& action) {
    REQUIRE((action.stepmania.has_value()));
    CHECK((action.stepmania->storyboard == stepmania_storyboard_mode_t::video));
    CHECK((action.stepmania->conditional_layer == storyboard_conditional_layer_t::neither));
}
struct native_export_fixture_t {
    nuklear_event_harness_t harness{1280, 1100};
    native_export_fixture_t() {
        auto& view = harness.view();
        view.active_tab = WORM_UI_WORKSPACE_TAB_SETUP;
        view.compose.visible = false;
        view.setup.visible = true;
        view.setup.project_has_native_path = true;
        view.setup.song_id = view.project_id;
        view.setup.document_revision = view.document_revision;
        view.setup.session_generation = 8;
        view.setup.selected_chart_id = chart_id_t{10};
        view.setup.charts = {{.chart_id = chart_id_t{10}, .name = "Difficulty", .mode = "4K",
            .compatible_output_families = 0x1f}};
        view.setup.chart_count = 1;
        view.setup.output_groups = {{.document_index = 0, .name = "Native SSC", .family = format_family_t::stepmania,
            .path = "exports/song.ssc", .destination_kind = output_destination_kind_t::file,
            .chart_ids = {chart_id_t{10}}, .stepmania = stepmania_export_policy_t{}}};
        (void)harness.frame();
    }
    auto click(std::string_view label) -> std::vector<workspace_action_snapshot_t> {
        const auto point = harness.drawn_text_center(label);
        INFO(label << "\n" << harness.drawn_text());
        REQUIRE((point.has_value()));
        return test::click_at(harness, point->first, point->second);
    }
    void edit() {
        (void)click("⋯");
        (void)harness.frame();
        (void)click("Edit");
        (void)harness.frame();
    }
};
}

TEST_CASE("StepMania export editor captures native mode and shared conditional choice", "[frontend-state][event-harness][output-groups][storyboard]") {
    native_export_fixture_t fixture;
    fixture.edit();
    REQUIRE((fixture.harness.drawn_text_center("Storyboard").has_value()));
    (void)fixture.click("Actors");
    (void)fixture.harness.frame();
    (void)fixture.click("Video");
    (void)fixture.harness.frame();
    (void)fixture.click("Pass");
    (void)fixture.harness.frame();
    (void)fixture.click("Neither");
    (void)fixture.harness.frame();
    const auto frames = fixture.click("Save");
    const auto saved = std::ranges::find_if(frames, [](const auto& frame) {
        return frame.get().project_output_group.kind == WORM_UI_PROJECT_OUTPUT_GROUP_UPDATE;
    });
    REQUIRE((saved != frames.end()));
    const auto& action = saved->get().project_output_group;
    check_native_export_action(action);
    fixture.harness.view().setup.output_groups[0].stepmania = action.stepmania;
    (void)fixture.harness.frame();
    fixture.edit();
    CHECK((fixture.harness.drawn_text_center("Video").has_value()));
    CHECK((fixture.harness.drawn_text_center("Neither").has_value()));
}

TEST_CASE("StepMania editor selects conservative NotITG SM profile and matching filename", "[frontend-state][event-harness][notitg]") {
    native_export_fixture_t fixture;
    fixture.edit();
    (void)fixture.click("Syntax only");
    (void)fixture.harness.frame();
    (void)fixture.click("NotITG v4 (SM)");
    (void)fixture.harness.frame();
    const auto frames = fixture.click("Save");
    const auto saved = std::ranges::find_if(frames, [](const auto& frame) {
        return frame.get().project_output_group.kind == WORM_UI_PROJECT_OUTPUT_GROUP_UPDATE;
    });
    REQUIRE((saved != frames.end()));
    const auto& action = saved->get().project_output_group;
    REQUIRE(action.stepmania);
    CHECK(action.stepmania->file_profile == stepmania_profile_t::sm);
    CHECK(action.stepmania->implementation_profile == stepmania_implementation_profile_t::notitg_v4);
    CHECK(action.path == "exports/song.sm");
}
