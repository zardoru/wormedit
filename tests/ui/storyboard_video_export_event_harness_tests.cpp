#include "nuklear_event_harness.h"
#include "sys/commands/application_commands.h"
#include "ui/nuklear/app/menu_bar.h"
#include <catch2/catch_test_macros.hpp>
#include <algorithm>

using namespace wormedit;
using namespace wormedit::ui;
namespace {
void prepare_video_dialog(nuklear_event_harness_t& harness) {
    REQUIRE(harness.ready());
    auto& view = harness.view();
    view.active_tab = WORM_UI_WORKSPACE_TAB_STORYBOARD;
    view.compose.visible = false; view.storyboard.visible = true;
    view.storyboard.project = view.project_id; view.storyboard.revision = view.document_revision;
    view.storyboard.storyboards = {{.id = storyboard_id_t{14}, .name = "Scene", .kind = storyboard_kind_t::osbx}};
    view.storyboard.refill_state({.selection = {.storyboard = storyboard_id_t{14}}});
    view.storyboard.video_export = {.open = true, .available = true, .can_start = true, .generation = 3,
        .estimated_bytes = 1048576, .free_bytes = 1073741824, .estimated_seconds = 42,
        .selection = storyboard_loop_region_t{.begin_ms = 10, .end_ms = 100},
        .capabilities = {{.width = 1920, .height = 1080, .available = true},
            {.width = 3840, .height = 2160, .available = false, .reason = "actual encoder refused level 5.2"}}};
    (void)harness.frame(); (void)harness.frame();
}
auto video_click(nuklear_event_harness_t& harness, std::string_view label) -> storyboard_video_export_action_t {
    const auto position = harness.drawn_text_center(label);
    REQUIRE(position);
    const auto frames = test::click_at(harness, position->first, position->second);
    for (const auto& frame : frames)
        if (frame.get().storyboard.kind == storyboard_action_kind_t::video_export) return frame.get().storyboard.video_export;
    return {};
}
}
TEST_CASE("Storyboard video dialog exposes the actual UHD refusal and disables that option", "[frontend-state][event-harness][storyboard-video]") {
    nuklear_event_harness_t harness{1280, 1000}; prepare_video_dialog(harness);
    CHECK(harness.drawn_text().find("actual encoder refused level 5.2") != std::string::npos);
    const auto action = video_click(harness, "3840×2160");
    CHECK(action.kind == storyboard_video_action_kind_t::none);
    CHECK(harness.view().storyboard.video_export.settings.width == 1920);
}
TEST_CASE("Storyboard video dialog starts through its shared typed action", "[frontend-state][event-harness][storyboard-video]") {
    nuklear_event_harness_t harness{1280, 1000}; prepare_video_dialog(harness);
    const auto action = video_click(harness, "Export video");
    CHECK(action.kind == storyboard_video_action_kind_t::start);
    CHECK(action.generation == 3);
    CHECK(action.settings.layers == storyboard_conditional_layer_t::pass);
    CHECK_FALSE(action.settings.replace_existing);
}
TEST_CASE("Storyboard video dialog configures one conditional scenario", "[frontend-state][event-harness][storyboard-video]") {
    nuklear_event_harness_t harness{1280, 1000}; prepare_video_dialog(harness);
    const auto action = video_click(harness, "Neither");
    CHECK(action.kind == storyboard_video_action_kind_t::configure);
    CHECK(action.settings.layers == storyboard_conditional_layer_t::neither);
}

TEST_CASE("Video export dialog renders in a valid Compose workspace frame", "[frontend-state][event-harness][storyboard-video]") {
    nuklear_event_harness_t harness{1280, 1000}; prepare_video_dialog(harness);
    auto& view = harness.view();
    view.active_tab = WORM_UI_WORKSPACE_TAB_COMPOSE;
    view.compose.visible = true;
    view.storyboard.visible = false;
    (void)harness.frame(); (void)harness.frame();
    const auto action = video_click(harness, "Export video");
    CHECK(action.kind == storyboard_video_action_kind_t::start);
    CHECK(action.generation == 3);
    CHECK(view.active_tab == WORM_UI_WORKSPACE_TAB_COMPOSE);
}
TEST_CASE("Storyboard video running dialog cancels without sending a second start", "[frontend-state][event-harness][storyboard-video]") {
    nuklear_event_harness_t harness{1280, 1000}; prepare_video_dialog(harness);
    harness.view().storyboard.video_export.running = true; (void)harness.frame(); (void)harness.frame();
    CHECK(video_click(harness, "Cancel export").kind == storyboard_video_action_kind_t::cancel);
}
TEST_CASE("Storyboard video unavailable build remains an actionable explanatory dialog", "[frontend-state][event-harness][storyboard-video]") {
    nuklear_event_harness_t harness{1280, 1000}; prepare_video_dialog(harness);
    auto& video = harness.view().storyboard.video_export;
    video.available = false; video.can_start = false; video.capabilities.clear();
    video.blockers = {"Video export unavailable in this build."}; (void)harness.frame(); (void)harness.frame();
    CHECK(harness.drawn_text().find("Video export unavailable") != std::string::npos);
    CHECK(video_click(harness, "Export video").kind == storyboard_video_action_kind_t::none);
    CHECK(video_click(harness, "Close").kind == storyboard_video_action_kind_t::close);
}
TEST_CASE("File video menu uses the same registered application command", "[frontend-state][event-harness][storyboard-video]") {
    nuklear_event_harness_t harness{1280, 1000}; REQUIRE(harness.ready());
    const auto menu = build_workspace_menu(harness.view(), false, false, false);
    const auto item = std::ranges::find(menu, workspace_menu_action_t::export_storyboard_video, &workspace_menu_item_t::action);
    REQUIRE(item != menu.end());
    CHECK(std::string_view{item->command_id} == "document.export_storyboard_video");
    const auto command = sys::parse_application_command(item->command_id);
    REQUIRE(command); CHECK(command->kind == sys::application_command_kind_t::document_export_storyboard_video);
}

TEST_CASE("Storyboard toolbar opens the same video dialog action", "[frontend-state][event-harness][storyboard-video]") {
    nuklear_event_harness_t harness{1280, 1000}; prepare_video_dialog(harness);
    harness.view().storyboard.video_export.open = false; (void)harness.frame(); (void)harness.frame();
    CHECK(video_click(harness, "Export video…").kind == storyboard_video_action_kind_t::open);
}

TEST_CASE("A retained UHD disk refusal can request a smaller real configuration probe", "[frontend-state][event-harness][storyboard-video]") {
    nuklear_event_harness_t harness{1280, 1000}; prepare_video_dialog(harness);
    auto& view = harness.view().storyboard.video_export;
    view.settings.width = 3840; view.settings.height = 2160;
    view.capabilities.clear(); view.can_start = false; view.status = "Insufficient free disk space";
    (void)harness.frame(); (void)harness.frame();
    const auto action = video_click(harness, "1920×1080");
    CHECK(action.kind == storyboard_video_action_kind_t::configure);
    CHECK(action.settings.width == 1920);
}
