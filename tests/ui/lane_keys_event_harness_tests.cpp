#include "nuklear_event_harness.h"
#include "sys/preferences/lane_keys_runtime.h"
#include <wormedit/core/lane_profiles.h>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <source_location>

using namespace wormedit;
using namespace wormedit::sys;
namespace {
void check_lane_keys(bool value, std::source_location where = std::source_location::current()) {
    INFO("lane-key check at " << where.line());
    CHECK(value);
}
struct lane_keys_fixture_t {
    nuklear_event_harness_t harness;
    document_history_t history;
    program_preferences_runtime_t program;
    lane_keys_runtime_t runtime;
    lane_keys_fixture_t(int width = 1280, int height = 900, bool double_play = false) : harness{width, height} {
        REQUIRE(harness.ready());
        const auto profile = double_play ? make_bms_14k_profile() : make_bms_7k_profile();
        history = document_history_t::from({.id = song_id_t{1}, .revision = 3,
            .charts = {{.id = chart_id_t{2}, .source_family = format_family_t::bms,
                .measures = {{.id = measure_id_t{3}}},
                .lane_layouts = {{.id = object_id_t{4}, .position = {measure_id_t{3}, 0},
                                  .layout = profile.layout}}, .input_profile = profile.input}}});
        REQUIRE(runtime.open(history.document, chart_id_t{2}, {measure_id_t{3}, 0}, program.preferences));
        harness.view().lane_keys = &runtime.view();
        (void)harness.frame();
        (void)harness.frame();
    }
    auto click(std::string_view label) -> ui::lane_keys_action_t {
        INFO("click " << label);
        const auto center = harness.drawn_text_center(label);
        REQUIRE(center);
        ui::lane_keys_action_t action;
        for (const auto& frame : test::click_at(harness, center->first, center->second))
            if (frame.get().lane_keys.kind != ui::lane_keys_action_kind_t::none)
                action = frame.get().lane_keys;
        return action;
    }
    void dispatch(const ui::lane_keys_action_t& action) {
        (void)runtime.dispatch(action, program);
        (void)harness.frame();
    }
    void select_lane() {
        const auto tooltip = "Bind " + runtime.view().lanes[1].name;
        for (const auto& frame : harness.click_widget(tooltip))
            (void)runtime.dispatch(frame.get().lane_keys, program);
        (void)harness.frame();
    }
    void key(SDL_Keycode code, SDL_Scancode scancode) {
        SDL_Event event{};
        event.type = SDL_EVENT_KEY_DOWN;
        event.key.key = code;
        event.key.scancode = scancode;
        event.key.down = true;
        const auto snapshot = harness.frame(std::span{&event, 1});
        const auto& action = snapshot.get();
        check_lane_keys(action.compose_inputs.size() == 0);
        check_lane_keys(action.compose_viewport_inputs.size() == 0);
        runtime.capture(action.shortcut_events);
        (void)runtime.dispatch(action.lane_keys, program);
        event.type = SDL_EVENT_KEY_UP;
        event.key.down = false;
        (void)harness.frame(std::span{&event, 1});
    }
    auto selected_usage() const -> std::uint16_t {
        const auto& view = runtime.view();
        const auto lane = std::ranges::find(view.lanes, view.selected, &ui::lane_key_lane_view_t::id);
        REQUIRE(lane != view.lanes.end());
        return lane->usage;
    }
};
}

TEST_CASE("Lane-key dialog captures a physical key and Backspace unbinds it before Apply",
          "[frontend-state][event-harness][lane-keys]") {
    lane_keys_fixture_t fixture;
    fixture.select_lane();
    check_lane_keys(fixture.runtime.view().capturing);
    fixture.key(SDLK_A, SDL_SCANCODE_A);
    check_lane_keys(fixture.selected_usage() == SDL_SCANCODE_A);
    check_lane_keys(fixture.program.preferences.lane_keys.layouts.empty());
    fixture.select_lane();
    fixture.key(SDLK_BACKSPACE, SDL_SCANCODE_BACKSPACE);
    check_lane_keys(fixture.selected_usage() == 0);
    fixture.dispatch(fixture.click("Apply"));
    check_lane_keys(!fixture.runtime.view().visible);
    REQUIRE_FALSE(fixture.program.preferences.lane_keys.layouts.empty());
    const auto& saved = fixture.program.preferences.lane_keys.layouts.begin()->second;
    check_lane_keys(saved.keys[1] == 0);
    check_lane_keys(fixture.history.undo.empty());
    check_lane_keys(fixture.history.document.revision == 3);
}

TEST_CASE("Lane-key duplicates disable Apply and choosing a preset restores valid bindings",
          "[frontend-state][event-harness][lane-keys]") {
    lane_keys_fixture_t fixture;
    fixture.select_lane();
    fixture.key(SDLK_1, SDL_SCANCODE_1);
    check_lane_keys(!fixture.runtime.view().can_apply);
    check_lane_keys(fixture.click("Apply").kind == ui::lane_keys_action_kind_t::none);
    check_lane_keys(fixture.program.preferences.lane_keys.layouts.empty());
    fixture.dispatch(fixture.click("Custom"));
    fixture.dispatch(fixture.click("Wormedit default"));
    check_lane_keys(fixture.runtime.view().can_apply);
    fixture.dispatch(fixture.click("Apply"));
    auto second = fixture.history.document.charts.front();
    for (auto& lane : second.lane_layouts.front().layout.lanes) lane.id.value += 100;
    for (auto& binding : second.input_profile.bindings) binding.lane.value += 100;
    lane_key_profile_cache_t cache;
    const auto& resolved = cache.resolve(fixture.program.preferences.lane_keys,
        second.lane_layouts.front().layout, second.input_profile);
    check_lane_keys(resolved.bindings.front().lane.value == second.input_profile.bindings.front().lane.value);
    check_lane_keys(resolved.bindings.front().key == physical_key_t::digit_1);
}

TEST_CASE("Lane-key capture refuses editing keys and Escape keeps the current key",
          "[frontend-state][event-harness][lane-keys]") {
    lane_keys_fixture_t fixture;
    fixture.select_lane();
    const auto before = fixture.selected_usage();
    fixture.key(SDLK_SPACE, SDL_SCANCODE_SPACE);
    check_lane_keys(fixture.selected_usage() == before);
    check_lane_keys(fixture.runtime.view().diagnostic.find("letter or digit") != std::string::npos);
    fixture.key(SDLK_ESCAPE, SDL_SCANCODE_ESCAPE);
    check_lane_keys(!fixture.runtime.view().capturing);
    check_lane_keys(fixture.runtime.view().visible);
    fixture.dispatch(fixture.click("Cancel"));
    check_lane_keys(!fixture.runtime.view().visible);
    check_lane_keys(fixture.program.preferences.lane_keys.layouts.empty());
}

TEST_CASE("Lane-key draft closes if its captured layout disappears", "[frontend-state][event-harness][lane-keys]") {
    lane_keys_fixture_t fixture;
    fixture.history.document.charts.front().lane_layouts.clear();
    fixture.runtime.refresh(fixture.history.document);
    check_lane_keys(!fixture.runtime.view().visible);
    check_lane_keys(fixture.program.preferences.lane_keys.layouts.empty());
}

TEST_CASE("Applying a captured lane key also changes a second chart with different lane IDs",
          "[frontend-state][event-harness][lane-keys]") {
    lane_keys_fixture_t fixture;
    fixture.select_lane();
    fixture.key(SDLK_A, SDL_SCANCODE_A);
    fixture.dispatch(fixture.click("Apply"));
    auto second = fixture.history.document.charts.front();
    for (auto& lane : second.lane_layouts.front().layout.lanes) lane.id.value += 100;
    for (auto& binding : second.input_profile.bindings) binding.lane.value += 100;
    lane_key_profile_cache_t cache;
    const auto& profile = cache.resolve(fixture.program.preferences.lane_keys,
        second.lane_layouts.front().layout, second.input_profile);
    const auto lane_id = lane_id_t{fixture.runtime.view().selected.value + 100};
    const auto binding = std::ranges::find(profile.bindings, lane_id, &lane_binding_t::lane);
    REQUIRE(binding != profile.bindings.end());
    check_lane_keys(binding->key == physical_key_t::key_a);
    check_lane_keys(second.input_profile != profile);
}

TEST_CASE("Lane-key Reset remains a draft until Apply and Cancel preserves the saved override",
          "[frontend-state][event-harness][lane-keys]") {
    lane_keys_fixture_t fixture;
    fixture.select_lane();
    fixture.key(SDLK_A, SDL_SCANCODE_A);
    fixture.dispatch(fixture.click("Apply"));
    const auto saved = fixture.program.preferences.lane_keys;
    REQUIRE(fixture.runtime.open(fixture.history.document, chart_id_t{2}, {measure_id_t{3}, 0}, fixture.program.preferences));
    (void)fixture.harness.frame();
    fixture.dispatch(fixture.click("Reset to preset"));
    check_lane_keys(fixture.program.preferences.lane_keys == saved);
    fixture.dispatch(fixture.click("Cancel"));
    check_lane_keys(fixture.program.preferences.lane_keys == saved);
    REQUIRE(fixture.runtime.open(fixture.history.document, chart_id_t{2}, {measure_id_t{3}, 0}, fixture.program.preferences));
    (void)fixture.harness.frame();
    fixture.dispatch(fixture.click("Reset to preset"));
    fixture.dispatch(fixture.click("Apply"));
    check_lane_keys(fixture.program.preferences.lane_keys.layouts.empty());
}

TEST_CASE("Lane-key dialog keeps Apply reachable at 800 by 700", "[frontend-state][event-harness][lane-keys]") {
    lane_keys_fixture_t fixture{800, 700};
    const auto center = fixture.harness.drawn_text_center("Apply");
    REQUIRE(center);
    check_lane_keys(center->second < 700);
    check_lane_keys(fixture.click("Apply").kind == ui::lane_keys_action_kind_t::apply);
}

TEST_CASE("DP7 dialog offers the real interleaved preset and applies Wormedit keys in stage order",
          "[frontend-state][event-harness][lane-keys]") {
    lane_keys_fixture_t fixture{1280, 900, true};
    check_lane_keys(fixture.runtime.view().can_interleave);
    check_lane_keys(fixture.runtime.view().preset == ui::lane_key_preset_t::ibmsc);
    fixture.dispatch(fixture.click("iBMSC interleaved (DP)"));
    fixture.dispatch(fixture.click("Wormedit default"));
    check_lane_keys(fixture.runtime.view().lanes[0].usage == SDL_SCANCODE_1);
    check_lane_keys(fixture.runtime.view().lanes[1].usage == SDL_SCANCODE_Q);
    check_lane_keys(fixture.runtime.view().lanes[8].usage == SDL_SCANCODE_Y);
    check_lane_keys(fixture.runtime.view().lanes[8].right_stage);
    check_lane_keys(fixture.runtime.view().can_apply);
    fixture.dispatch(fixture.click("Apply"));
    check_lane_keys(!fixture.runtime.view().visible);
    check_lane_keys(fixture.program.preferences.lane_keys.layouts.size() == 1);
}
