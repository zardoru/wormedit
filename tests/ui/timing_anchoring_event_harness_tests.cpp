#include "nuklear_event_harness.h"
#include "ui/nuklear/compose/compose_state.h"
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
namespace {
void begin_timing(nuklear_event_harness_t& harness) {
    auto& timing = harness.view().timing;
    timing.stage_active = timing.stage_valid = true;
    timing.stage_lane = wormedit::timing_lane_kind_t::bpm;
    timing.stage_value = 120;
    timing.stage_primary_decimal_text = "120";
    timing.stage_primary_decimal_exact = true;
    timing.stage_primary_numerator_text = "120";
    timing.stage_primary_denominator_text = "1";
    harness.view().compose.timing_stage_active = true;
    harness.view().compose.timing_stage_valid = true;
    (void)harness.frame();
    (void)harness.frame();
}
auto click_anchoring(nuklear_event_harness_t& harness, const char* label) -> workspace_action_snapshot_t {
    const auto control = harness.drawn_text_center(label);
    REQUIRE(control);
    const auto frames = wormedit::test::click_at(harness, control->first, control->second);
    const auto found = std::ranges::find_if(frames, [](const auto& frame) { return frame.get().timing_anchoring.has_value(); });
    REQUIRE(found != frames.end());
    return *found;
}
void check_audio_action(const wormedit::ui::workspace_action_t& action) {
    REQUIRE(action.timing_anchoring);
    CHECK(action.timing_anchoring->positions == wormedit::timing_position_policy_t::audio);
    CHECK_FALSE(action.timing_anchoring->allow_quantization);
    CHECK(action.timing_stage_action == wormedit::ui::timing_stage_action_t::update);
    CHECK(action.timing_chart_id == wormedit::chart_id_t{1});
    CHECK(action.timing_document_revision == 1);
}
}
TEST_CASE("Inline BPM anchoring controls retain the draft and emit stamped policy changes", "[event-harness][timing-anchoring]") {
    nuklear_event_harness_t harness{1280, 900};
    REQUIRE(harness.ready());
    begin_timing(harness);
    const auto audio = click_anchoring(harness, "Keep audio time of notes");
    check_audio_action(audio.get());
    CHECK(worm_ui_nuklear_compose_state().inline_timing.inline_timing_open);
    auto& view = harness.view().timing;
    view.stage_anchoring = *audio.get().timing_anchoring;
    view.stage_moved_objects = 4;
    view.stage_rounded_positions = 2;
    (void)harness.frame();
    const auto quantize = click_anchoring(harness, "Allow quantization");
    REQUIRE(quantize.get().timing_anchoring);
    CHECK(quantize.get().timing_anchoring->allow_quantization);
    CHECK(worm_ui_nuklear_compose_state().inline_timing.inline_timing_open);
}

TEST_CASE("Rejected audio-time preview stays in the existing inline timing popup", "[event-harness][timing-anchoring]") {
    nuklear_event_harness_t harness{1280, 900};
    REQUIRE(harness.ready());
    begin_timing(harness);
    auto& view = harness.view().timing;
    view.stage_existing_object_id = wormedit::object_id_t{55};
    auto& state = worm_ui_nuklear_compose_state().inline_timing;
    state.inline_timing_object_id = 55;
    view.stage_valid = false;
    view.stage_anchoring.positions = wormedit::timing_position_policy_t::audio;
    view.stage_rounded_positions = 2;
    (void)harness.frame();
    const auto apply = harness.drawn_text_center("Apply timing edit");
    REQUIRE(apply);
    const auto frames = wormedit::test::click_at(harness, apply->first, apply->second);
    CHECK(std::ranges::any_of(frames, [](const auto& frame) {
        return frame.get().timing_stage_action == wormedit::ui::timing_stage_action_t::confirm;
    }));
    CHECK(state.inline_timing_open);
    CHECK(harness.drawn_text().find("Allow quantization") != std::string::npos);
}
