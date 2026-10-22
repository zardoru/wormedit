#include "nuklear_event_harness.h"
#include <wormedit/core/metadata_inheritance.h>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>

namespace {
struct metadata_inheritance_fixture_t {
    nuklear_event_harness_t harness{1280, 1400};
    explicit metadata_inheritance_fixture_t(bool editable = true) {
        REQUIRE(harness.ready());
        auto& view = harness.view();
        view.active_tab = WORM_UI_WORKSPACE_TAB_METADATA;
        view.compose.visible = false;
        auto& metadata = view.metadata;
        metadata.visible = metadata.has_chart = true;
        metadata.chart_fields_editable = editable;
        metadata.document_revision = 7;
        metadata.chart_id = wormedit::chart_id_t{52};
        metadata.chart_name = "Normal";
        metadata.chart_author = "Mapper";
        metadata.chart_divisions_per_standard_measure = 960;
        for (const auto& field : wormedit::inherited_metadata_fields) {
            metadata.inheritance.push_back({.field = field.field, .label = std::string{field.label},
                .inherited = field.field == wormedit::inherited_metadata_field_t::title ? "Shared title" : ""});
        }
        (void)harness.frame();
        (void)click("Song field overrides");
        (void)harness.frame();
    }
    auto click(std::string_view text) -> std::vector<workspace_action_snapshot_t> {
        const auto position = harness.drawn_text_center(text);
        INFO(text);
        INFO(harness.drawn_text());
        REQUIRE(position);
        return wormedit::test::click_at(harness, position->first, position->second);
    }
};
void check_no_inheritance_action(const std::vector<workspace_action_snapshot_t>& frames) {
    for (const auto& frame : frames) CHECK(frame.get().metadata_edit != WORM_UI_METADATA_EDIT_INHERITANCE);
}
}
TEST_CASE("Metadata inherit checkboxes emit one owned atomic override action", "[frontend-state][event-harness][metadata-inheritance]") {
    metadata_inheritance_fixture_t fixture;
    check_no_inheritance_action(fixture.click("Inherit Title"));
    check_no_inheritance_action(fixture.click("Inherit Artist"));
    const auto frames = fixture.click("Apply fields");
    const auto found = std::ranges::find_if(frames, [](const auto& frame) {
        return frame.get().metadata_edit == WORM_UI_METADATA_EDIT_INHERITANCE;
    });
    REQUIRE(found != frames.end());
    const auto& action = found->get();
    CHECK(action.metadata_document_revision == 7);
    CHECK(action.metadata_chart_id == wormedit::chart_id_t{52});
    CHECK(action.metadata_overrides.value(wormedit::inherited_metadata_field_t::title) == std::optional<std::string>{"Shared title"});
    CHECK(action.metadata_overrides.value(wormedit::inherited_metadata_field_t::artist) == std::optional<std::string>{""});
    CHECK_FALSE(action.metadata_overrides.value(wormedit::inherited_metadata_field_t::genre));
}
TEST_CASE("Read-only Metadata inheritance cannot emit an edit", "[frontend-state][event-harness][metadata-inheritance]") {
    metadata_inheritance_fixture_t fixture{false};
    check_no_inheritance_action(fixture.click("Inherit Title"));
    check_no_inheritance_action(fixture.click("Apply fields"));
}
