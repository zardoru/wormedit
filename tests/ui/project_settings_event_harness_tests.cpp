#include "nuklear_event_harness.h"
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <source_location>

namespace {
void check_project_setting(bool value, std::source_location at = std::source_location::current()) {
    INFO("project setting assertion at " << at.line());
    CHECK(value);
}
}

TEST_CASE("Project settings inheritance emits a stamped document action", "[frontend-state][event-harness][project-settings]") {
    nuklear_event_harness_t harness{1280, 1000};
    REQUIRE(harness.ready());
    auto& view = harness.view();
    view.active_tab = WORM_UI_WORKSPACE_TAB_SETUP;
    view.compose.visible = false;
    view.metadata.visible = false;
    view.setup = {.visible = true, .song_id = {42}, .document_revision = 53, .session_generation = 17};
    view.setup.settings.default_snap = 4;
    view.setup.settings.program_default_snap = 4;
    view.setup.settings.audio_directory = "assets";
    view.setup.settings.visual_directory = "assets";
    (void)harness.frame();
    const auto tree = harness.drawn_text_center("Project settings");
    REQUIRE(tree);
    (void)wormedit::test::click_at(harness, tree->first, tree->second);
    (void)harness.frame();
    const auto toggle = harness.drawn_text_center("Use program default for new-chart snap");
    REQUIRE(toggle);
    const auto result = wormedit::test::click_at(harness, toggle->first, toggle->second);
    const auto found = std::ranges::find_if(result, [](const auto& frame) { return frame.get().project_settings.requested; });
    REQUIRE(found != result.end());
    const auto& action = found->get().project_settings;
    check_project_setting(action.requested);
    check_project_setting(action.project == wormedit::song_id_t{42});
    check_project_setting(action.revision == 53);
    check_project_setting(action.session_generation == 17);
    check_project_setting(action.field == wormedit::project_setting_field_t::default_snap);
    check_project_setting(!action.use_program_default);
    check_project_setting(action.integer == 4);
}
