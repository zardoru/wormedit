#include "nuklear_event_harness.h"
#include "ui/nuklear/compose/compose_state.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <array>
#include <source_location>
#include <string_view>

// Timing-control workflows split out of bga_lane_event_harness_tests.cpp; the helpers below are
// the few small drivers they share with it.
namespace {
void check_bga(bool condition, const std::source_location location =
                                    std::source_location::current()) {
    INFO("assertion caller line " << location.line());
    CHECK(condition);
}

using wormedit::test::mouse_button;
using wormedit::test::mouse_motion;

auto mouse_wheel(float amount, float x, float y) -> SDL_Event {
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_WHEEL;
    event.wheel.y = static_cast<float>(amount);
    event.wheel.mouse_x = x;
    event.wheel.mouse_y = y;
    return event;
}

auto click(nuklear_event_harness_t& harness, float x, float y)
    -> std::array<workspace_action_snapshot_t, 2> {
    const auto down = std::array{mouse_motion(x, y), mouse_button(true, x, y)};
    const auto up = std::array{mouse_motion(x, y), mouse_button(false, x, y)};
    return {harness.frame(down), harness.frame(up)};
}
}

namespace {
struct timing_controls_fixture_t {
    nuklear_event_harness_t harness{1600, 1000};
    std::array<wormedit::ui::compose_waveform_overview_row_view_t, 2> rows{{{0, 0.5f}, {1, 0.5f}}};
    timing_controls_fixture_t() {
        REQUIRE(harness.ready());
        auto& view = harness.view().compose;
        view.waveform_overview_rows.assign(rows.begin(), rows.end());

        view.waveform_overview_total_frames = 96000;
        view.waveform_overview_sample_rate = 48000;
        view.waveform_audio_offset_denominator = 1;
        view.waveform_overview_visible_end_fraction = 1;
        view.audio_offset_editable = true;
        worm_ui_nuklear_compose_state().dock_layout.layout.show(wormedit::dock_panel_t::inspector, true);
        (void)harness.frame();
        const auto section = harness.drawn_text_center("Quantize / Snap");
        REQUIRE(section);
        (void)click(harness, section->first, section->second);
        (void)harness.frame();
    }
    auto reveal(std::string_view text) -> std::pair<float, float> {
        auto point = harness.drawn_text_center(text);
        for (int i = 0; i < 24 && !point; ++i) {
            (void)harness.frame(std::array{mouse_motion(1450, 700), mouse_wheel(-2, 1450, 700)});
            point = harness.drawn_text_center(text);
        }
        REQUIRE(point);
        return *point;
    }
    void edit_offset() {
        (void)harness.frame();
        const auto p = reveal("+0.000 s");
        auto down = mouse_button(true, p.first, p.second);
        auto up = mouse_button(false, p.first, p.second);
        down.button.clicks = 2;
        up.button.clicks = 2;
        (void)harness.frame(std::array{mouse_motion(p.first, p.second), down, up});
        check_bga(worm_ui_nuklear_compose_state().inline_offset.inline_offset_open);
        (void)harness.frame();
    }
};
auto timing_control_key(SDL_Keycode key, SDL_Scancode scancode) -> SDL_Event {
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.type = SDL_EVENT_KEY_DOWN;
    event.key.key = key;
    event.key.scancode = scancode;
    event.key.down = true;
    return event;
}
}

TEST_CASE("Timing controls leave the normal toolbar and appear during timing focus",
          "[frontend-state][event-harness][timing-controls]") {
    timing_controls_fixture_t f;
    auto& state = worm_ui_nuklear_compose_state();
    state.dock_layout.layout.show(wormedit::dock_panel_t::inspector, false);
    (void)f.harness.frame();
    check_bga(!f.harness.drawn_text_center("+0.000 s"));
    f.harness.view().compose.timing_focus = true;
    (void)f.harness.frame();
    check_bga(f.harness.drawn_text_center("+0.000 s").has_value());
    f.harness.view().compose.timing_focus = false;
    state.dock_layout.layout.show(wormedit::dock_panel_t::inspector, true);
    const auto snap = f.reveal("BPM snap");
    check_bga(snap.first > 1200);
    check_bga(f.harness.drawn_text_center("Timing settings…").has_value());
}

TEST_CASE("Offset editor commits one exact stamped action from either timing surface",
          "[frontend-state][event-harness][timing-controls]") {
    timing_controls_fixture_t f;
    const bool inspector = GENERATE(true, false);
    f.harness.view().compose.timing_focus = !inspector;
    f.edit_offset();
    check_bga(worm_ui_nuklear_compose_state().inline_offset.inline_offset_in_inspector == inspector);
    SDL_Event text{};
    text.type = SDL_EVENT_TEXT_INPUT;
    text.text.type = SDL_EVENT_TEXT_INPUT;
    text.text.text = "-1/8";
    const auto typed = f.harness.frame(std::array{text});
    check_bga(typed.get().input.compose_inputs.size() == 0);
    const auto committed = f.harness.frame(std::array{timing_control_key(SDLK_RETURN, SDL_SCANCODE_RETURN)});
    check_bga(committed.get().compose.kind == WORM_UI_COMPOSE_ACTION_APPLY_AUDIO_OFFSET);
    check_bga(committed.get().compose.chart.audio_offset_numerator == -1);
    check_bga(committed.get().compose.chart.audio_offset_denominator == 8);
    check_bga(committed.get().stamp.chart_id.value == 1);
    check_bga(committed.get().stamp.document_revision == 1);
    check_bga(!worm_ui_nuklear_compose_state().inline_offset.inline_offset_open);
    check_bga(f.harness.frame().get().compose.kind == WORM_UI_COMPOSE_ACTION_NONE);
}

TEST_CASE("Hiding an offset editor owner cancels its draft",
          "[frontend-state][event-harness][timing-controls]") {
    timing_controls_fixture_t f;
    f.edit_offset();
    worm_ui_nuklear_compose_state().dock_layout.layout.show(wormedit::dock_panel_t::inspector, false);
    check_bga(f.harness.frame().get().compose.kind == WORM_UI_COMPOSE_ACTION_NONE);
    check_bga(!worm_ui_nuklear_compose_state().inline_offset.inline_offset_open);
    f.harness.view().compose.timing_focus = true;
    f.edit_offset();
    check_bga(!worm_ui_nuklear_compose_state().inline_offset.inline_offset_in_inspector);
    f.harness.view().compose.timing_focus = false;
    check_bga(f.harness.frame().get().compose.kind == WORM_UI_COMPOSE_ACTION_NONE);
    check_bga(!worm_ui_nuklear_compose_state().inline_offset.inline_offset_open);
}

TEST_CASE("Inspector offset nudges preserve frame precision and disabled locks",
          "[frontend-state][event-harness][timing-controls]") {
    timing_controls_fixture_t f;
    (void)f.reveal("BPM snap");
    const auto value = f.reveal("+0.000 s");
    const auto bounds = f.harness.drawn_text_bounds();
    const auto label = std::ranges::find_if(bounds, [](const auto& item) {
        return std::string_view{item.text} == "Audio offset · seconds";
    });
    REQUIRE(label != bounds.end());
    const float left = label->x, width = label->width;
    constexpr std::array<std::int64_t, 4> deltas{-480, -1, 1, 480};
    for (std::size_t index = 0; index < deltas.size(); ++index) {
        const auto actions = click(f.harness, left + width * (static_cast<float>(index) + 0.5f) / 4,
                                   value.second + 31);
        check_bga(actions.front().get().audio_offset_drag.action == WORM_UI_AUDIO_OFFSET_NUDGE);
        check_bga(actions.front().get().audio_offset_drag.delta_frames == deltas[index]);
        check_bga(actions.front().get().audio_offset_drag.chart_id.value == 1);
        check_bga(actions.front().get().audio_offset_drag.document_revision == 1);
        check_bga(actions.back().get().audio_offset_drag.action == WORM_UI_AUDIO_OFFSET_DRAG_NONE);
    }
    f.harness.view().compose.audio_offset_locked = true;
    (void)f.harness.frame();
    const auto disabled = click(f.harness, left + width * 0.875f, value.second + 31);
    check_bga(disabled.front().get().audio_offset_drag.action == WORM_UI_AUDIO_OFFSET_DRAG_NONE);
}

TEST_CASE("Collapsing Timing cancels an inspector offset draft",
          "[frontend-state][event-harness][timing-controls]") {
    timing_controls_fixture_t f;
    f.edit_offset();
    const auto header = f.harness.drawn_text_center("Timing");
    REQUIRE(header);
    const auto collapsed = click(f.harness, header->first, header->second);
    check_bga(!worm_ui_nuklear_compose_state().inline_offset.inline_offset_open);
    check_bga(collapsed.front().get().compose.kind == WORM_UI_COMPOSE_ACTION_NONE);
    check_bga(!f.harness.drawn_text_center("BPM snap"));
}

TEST_CASE("Switching into timing focus cancels the hidden inspector offset draft",
          "[frontend-state][event-harness][timing-controls]") {
    timing_controls_fixture_t f;
    f.edit_offset();
    f.harness.view().compose.timing_focus = true;
    const auto changed = f.harness.frame();
    check_bga(!worm_ui_nuklear_compose_state().inline_offset.inline_offset_open);
    check_bga(changed.get().compose.kind == WORM_UI_COMPOSE_ACTION_NONE);
    check_bga(f.harness.drawn_text_center("+0.000 s").has_value());
}

TEST_CASE("Inspector BPM snap choice is shared with the timing-focus toolbar",
          "[frontend-state][event-harness][timing-controls]") {
    timing_controls_fixture_t f;
    (void)f.reveal("BPM snap");
    const auto combo = f.harness.drawn_text_center("1");
    REQUIRE(combo);
    (void)click(f.harness, combo->first, combo->second);
    const auto half = f.harness.drawn_text_center("0.5");
    REQUIRE(half);
    const auto selected = click(f.harness, half->first, half->second);
    check_bga(worm_ui_nuklear_compose_state().waveform_drag.beat_fit_snap_step_index == 2);
    check_bga(selected.front().get().compose.kind == WORM_UI_COMPOSE_ACTION_NONE);
    check_bga(selected.front().get().input.compose_inputs.size() == 0);
    check_bga(selected.front().get().audio_offset_drag.action == WORM_UI_AUDIO_OFFSET_DRAG_NONE);
    f.harness.view().compose.timing_focus = true;
    (void)f.harness.frame();
    check_bga(f.harness.drawn_text_center("0.5").has_value());
    check_bga(worm_ui_nuklear_compose_state().waveform_drag.beat_fit_snap_step_index == 2);
}

TEST_CASE("Layers assist icon is independent of lock and sends one stamped session toggle",
          "[frontend-state][event-harness][layer-assist]") {
    nuklear_event_harness_t harness{1280, 900};
    REQUIRE(harness.ready());
    auto& state = worm_ui_nuklear_compose_state();
    state.dock_layout.layout.show(wormedit::dock_panel_t::inspector, false);
    state.dock_layout.layout.show(wormedit::dock_panel_t::sounds, false);
    state.dock_layout.layout.show(wormedit::dock_panel_t::layers, true);
    wormedit::ui::compose_semantic_layer_view_t layer{.layer_id={7},.name="Lead",.red=255,
        .assigned_note_count=3,.visible=true,.locked=true};
    auto& view=harness.view().compose;
    view.semantic_layers={layer};
    view.semantic_layers.resize(view.semantic_layer_total_count=1);
    view.semantic_layer_assist_available=true;
    (void)harness.frame();
    const auto row=harness.drawn_text_center("Lead");
    REQUIRE(row);
    const auto assist=harness.widget_center("Enable layer assist ticks");
    const auto toggled=click(harness,assist.first,assist.second);
    check_bga(toggled.front().get().compose.kind==WORM_UI_COMPOSE_ACTION_TOGGLE_LAYER_ASSIST_TICK);
    check_bga(toggled.front().get().compose.layer.id.value==7);
    check_bga(toggled.front().get().stamp.project_id.value==1);
    check_bga(toggled.front().get().stamp.chart_id.value==1);
    check_bga(toggled.front().get().stamp.document_revision==1);
    check_bga(toggled.front().get().input.compose_inputs.size()==0);
    check_bga(toggled.back().get().compose.kind==WORM_UI_COMPOSE_ACTION_NONE);
    view.semantic_layers[0].assist_tick_enabled=true;
    (void)harness.frame(std::array{mouse_motion(assist.first,assist.second)});
    check_bga(harness.drawn_text().find("Disable layer assist ticks")!=std::string::npos);
    view.semantic_layer_assist_available=false;
    (void)harness.frame();
    check_bga(click(harness,assist.first,assist.second).front().get().compose.kind==WORM_UI_COMPOSE_ACTION_NONE);
    view.semantic_layer_assist_available=true;
    harness.view().close_confirmation.visible=true;
    (void)harness.frame();
    check_bga(click(harness,assist.first,assist.second).front().get().compose.kind==WORM_UI_COMPOSE_ACTION_NONE);
}
