#include "nuklear_event_harness.h"
#include "ui/nuklear/metadata/metadata_state.h"
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <source_location>
using namespace wormedit;
namespace {
void check_timing_ui(bool value, std::source_location at = std::source_location::current()) {
    INFO("timing UI assertion at " << at.line());
    CHECK(value);
}
auto click_timing_text(nuklear_event_harness_t& harness, std::string_view text) -> std::vector<workspace_action_snapshot_t> {
    const auto position = harness.drawn_text_center(text);
    INFO(text);
    INFO(harness.drawn_text());
    REQUIRE(position);
    return test::click_at(harness, position->first, position->second);
}
void check_no_timing_action(const std::vector<workspace_action_snapshot_t>& frames) {
    for (const auto& frame : frames) check_timing_ui(!frame.get().timing_link.requested);
}
struct timing_ui_fixture_t {
    nuklear_event_harness_t harness{1280, 1400};
    timing_ui_fixture_t() {
        REQUIRE(harness.ready());
        auto& view = harness.view();
        view.active_tab = WORM_UI_WORKSPACE_TAB_METADATA;
        view.compose.visible = view.setup.visible = false;
        auto& metadata = view.metadata;
        metadata.visible = metadata.has_chart = metadata.chart_fields_editable = true;
        metadata.chart_id = chart_id_t{52};
        metadata.document_revision = 7;
        metadata.chart_name = "Target";
        metadata.chart_divisions_per_standard_measure = 960;
        metadata.timing_link_context = {.project = song_id_t{42}, .chart = chart_id_t{52}, .revision = 7, .session_generation = 17};
        metadata.chart_choices = {{.chart_id = chart_id_t{52}, .name = "Target"}, {.chart_id = chart_id_t{91}, .name = "Source"}};
        metadata.timing_links = {{.category = timing_category_t::bpm, .label = "BPM", .status = "Local", .requires_confirmation = true}};
        (void)harness.frame();
        (void)click_timing_text(harness, "Timing links");
        (void)harness.frame();
    }
    void begin_link() {
        check_no_timing_action(click_timing_text(harness, "Choose source"));
        (void)harness.frame();
        check_no_timing_action(click_timing_text(harness, "Source"));
        (void)harness.frame();
        check_no_timing_action(click_timing_text(harness, "Link"));
        (void)harness.frame();
    }
};
void check_confirmed_timing_action(const std::vector<workspace_action_snapshot_t>& frames) {
    const auto found = std::ranges::find_if(frames, [](const auto& frame) { return frame.get().timing_link.requested; });
    REQUIRE(found != frames.end());
    const auto& action = found->get().timing_link;
    check_timing_ui(action.context == ui::timing_link_context_t{.project = song_id_t{42}, .chart = chart_id_t{52}, .revision = 7, .session_generation = 17});
    check_timing_ui(action.request.category == timing_category_t::bpm);
    check_timing_ui(action.request.source == chart_id_t{91});
    check_timing_ui(action.request.discard_confirmed);
}
}
TEST_CASE("Timing source selection emits a confirmed action with stable category chart and session identity", "[frontend-state][event-harness][timing-links]") {
    timing_ui_fixture_t fixture;
    fixture.begin_link();
    check_confirmed_timing_action(click_timing_text(fixture.harness, "Replace"));
}
TEST_CASE("Cancelling timing replacement preserves the source choice and emits no edit", "[frontend-state][event-harness][timing-links]") {
    timing_ui_fixture_t fixture;
    fixture.begin_link();
    check_no_timing_action(click_timing_text(fixture.harness, "Cancel"));
    check_timing_ui(worm_ui_nuklear_metadata_state().timing_links.source_for(timing_category_t::bpm) == chart_id_t{91});
}
TEST_CASE("Timing confirmations expire across project sessions even with identical chart revisions", "[frontend-state][event-harness][timing-links]") {
    timing_ui_fixture_t fixture;
    fixture.begin_link();
    ++fixture.harness.view().metadata.timing_link_context.session_generation;
    const auto frame = fixture.harness.frame();
    check_timing_ui(!frame.get().timing_link.requested);
    check_timing_ui(!fixture.harness.drawn_text_center("Replace local timing?"));
}
TEST_CASE("Locked timing source controls cannot emit an edit", "[frontend-state][event-harness][timing-links]") {
    timing_ui_fixture_t fixture;
    fixture.harness.view().metadata.timing_locked = true;
    (void)fixture.harness.frame();
    check_no_timing_action(click_timing_text(fixture.harness, "Link"));
}
namespace {
struct creation_ui_fixture_t {
    nuklear_event_harness_t harness{1280, 1000};
    creation_ui_fixture_t() {
        REQUIRE(harness.ready());
        auto& view = harness.view();
        view.active_tab = WORM_UI_WORKSPACE_TAB_SETUP;
        view.metadata.visible = view.compose.visible = false;
        view.setup = {.timing_categories = {{.category = timing_category_t::bpm, .label = "BPM"}},
            .visible = true, .song_id = song_id_t{42}, .document_revision = 7, .session_generation = 17,
            .charts = {{.chart_id = chart_id_t{91}, .name = "Source", .author = "", .rating = "", .mode = "",
                .output_groups = "", .length = "", .diagnostic = ""}}, .selected_chart_id = chart_id_t{91}, .chart_count = 1};
        (void)harness.frame();
        (void)click_timing_text(harness, "+");
        (void)harness.frame();
    }
};
}
TEST_CASE("New chart dialog carries the explicitly selected timing sources", "[frontend-state][event-harness][timing-links]") {
    creation_ui_fixture_t fixture;
    auto& harness = fixture.harness;
    (void)click_timing_text(harness, "Independent");
    (void)harness.frame();
    (void)click_timing_text(harness, "Source");
    (void)harness.frame();
    const auto frames = click_timing_text(harness, "Create");
    const auto found = std::ranges::find_if(frames, [](const auto& frame) { return frame.get().project_chart.kind == WORM_UI_PROJECT_CHART_NEW; });
    REQUIRE(found != frames.end());
    check_timing_ui(found->get().project_chart.timing_sources.source(timing_category_t::bpm) == chart_id_t{91});
    check_timing_ui(found->get().project_chart.session_generation == 17);
}

TEST_CASE("Read-only timing controls cannot emit a link", "[frontend-state][event-harness][timing-links]") {
    timing_ui_fixture_t fixture;
    fixture.harness.view().metadata.chart_fields_editable = false;
    (void)fixture.harness.frame();
    check_no_timing_action(click_timing_text(fixture.harness, "Link"));
}
TEST_CASE("Cancelling new chart timing choices emits no creation", "[frontend-state][event-harness][timing-links]") {
    creation_ui_fixture_t fixture;
    const auto frames = click_timing_text(fixture.harness, "Cancel");
    for (const auto& frame : frames) check_timing_ui(frame.get().project_chart.kind != WORM_UI_PROJECT_CHART_NEW);
}
TEST_CASE("New chart timing choices expire when their document changes", "[frontend-state][event-harness][timing-links]") {
    creation_ui_fixture_t fixture;
    ++fixture.harness.view().setup.document_revision;
    const auto frame = fixture.harness.frame();
    check_timing_ui(frame.get().project_chart.kind != WORM_UI_PROJECT_CHART_NEW);
    check_timing_ui(!fixture.harness.drawn_text_center("Create"));
}
