#include "nuklear_event_harness.h"
#include "ui/nuklear/compose/compose_state.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string_view>
#include <vector>

namespace {

using wormedit::test::mouse_button;
using wormedit::test::mouse_motion;

auto mouse_wheel(float x, float y, float delta) -> SDL_Event {
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_WHEEL;
    event.wheel.type = SDL_EVENT_MOUSE_WHEEL;
    event.wheel.windowID = 0;
    event.wheel.mouse_x = x;
    event.wheel.mouse_y = y;
    event.wheel.y = delta;
    return event;
}

using wormedit::test::click_at;

auto text_occurrences(std::string_view text, std::string_view needle) -> std::size_t {
    std::size_t count{};
    while (!needle.empty()) {
        const auto found = text.find(needle);
        if (found == std::string_view::npos) return count;
        ++count;
        text.remove_prefix(found + needle.size());
    }
    return count;
}

auto note_paint_begin(const std::vector<workspace_action_snapshot_t>& frames)
    -> const wormedit::ui::compose_input_action_t* {
    for (const auto& frame : frames) {
        const auto& action = frame.get();
        for (std::size_t index = 0; index < action.compose_inputs.size(); ++index) {
            if (action.compose_inputs[index].kind == WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN)
                return &action.compose_inputs[index];
        }
    }
    return nullptr;
}

void scroll_inspector(nuklear_event_harness_t& harness, float delta, int count) {
    const auto motion = mouse_motion(1500.0f, 800.0f);
    (void)harness.frame(std::span{&motion, 1});
    const auto wheel = mouse_wheel(1500.0f, 800.0f, delta);
    for (int index = 0; index < count; ++index) (void)harness.frame(std::span{&wheel, 1});
}

void set_resource_fixture(wormedit::ui::compose_view_t& view,
                          const wormedit::ui::compose_resource_view_t* resources, std::size_t resource_count) {
    view.resources.assign(resources, resources + resource_count);
    view.resources.resize(resource_count);
    view.resource_total_count = resource_count;
}

auto sample_workflow_uses_sounds(nuklear_event_harness_t& harness) -> bool {
    scroll_inspector(harness, -1.0f, 20);
    const auto& initial_text = harness.drawn_text();
    if (text_occurrences(initial_text, "kick.wav") != 1 ||
        initial_text.find("Place Sample at cursor") != std::string::npos ||
        initial_text.find("Create Sample from waveform") != std::string::npos ||
        initial_text.find("Link audio as Sample") != std::string::npos)
        return false;
    auto& state = worm_ui_nuklear_compose_state();
    if (!state.sounds_dock.sound_list_valid) return false;
    const auto sound_row = harness.drawn_text_center("kick.wav");
    if (!sound_row) return false;
    (void)click_at(harness, sound_row->first, sound_row->second, true);
    if (state.keysound_catalog.keysound_selection.primary != 40 || state.resource_picker.armed_resource_id != 40 ||
        !state.keysound_catalog.armed_from_sounds_dock ||
        // The inspector's resource list is retired, so its Disarm is gone. ("Assign selected" is
        // not checked: the semantic-layer section draws "Assign selected objects".)
        harness.drawn_text().find("Disarm") != std::string::npos)
        return false;
    const auto frames = click_at(harness, 300.0f, 400.0f);
    const auto* paint = note_paint_begin(frames);
    return paint && paint->resource_id.value == 40;
}

auto compact_layout_keeps_sound_picker(nuklear_event_harness_t& harness) -> bool {
    const auto& text = harness.drawn_text();
    return text.find("Sounds  1") != std::string::npos &&
           text.find("Find note label") == std::string::npos &&
           text.find("Layers  0") == std::string::npos;
}

auto compact_toolbar_sounds_toggle_status(nuklear_event_harness_t& harness) -> std::string_view {
    auto& state = worm_ui_nuklear_compose_state();
    (void)harness.hover_widget("Hide sounds list");
    if (harness.drawn_text().find("Hide sounds list") == std::string::npos)
        return "hover did not identify Hide sounds list";
    (void)harness.click_widget("Hide sounds list");
    if (state.dock_layout.sounds_panel_visible || state.sounds_dock.sound_list_valid)
        return "click did not hide the Sounds dock";
    (void)harness.hover_widget("Show sounds list");
    if (harness.drawn_text().find("Show sounds list") == std::string::npos)
        return "hidden dock did not expose Show sounds list";
    (void)harness.click_widget("Show sounds list");
    if (!state.dock_layout.sounds_panel_visible || !state.sounds_dock.sound_list_valid)
        return "click did not reopen the Sounds dock hit region";
    return harness.drawn_text().find("Sounds  1") != std::string::npos
        ? std::string_view{} : "reopened dock did not render its Sounds row";
}

auto wide_layout_restores_optional_docks(nuklear_event_harness_t& harness) -> bool {
    const auto& text = harness.drawn_text();
    return text.find("Find note label") != std::string::npos &&
           text.find("Layers  0") != std::string::npos;
}

} // namespace

TEST_CASE("Inspector delegates sample listing and placement to the Sounds dock",
          "[frontend-state][event-harness][inspector-resources]") {
    nuklear_event_harness_t harness{1600, 900};
    REQUIRE(harness.ready());
    auto& view = harness.view().compose;
    const std::array<wormedit::ui::compose_keysound_row_view_t, 1> sounds{{
        {.resource_id = wormedit::resource_id_t{40}, .slot = "0A", .name = "kick.wav"}}};
    const std::array<wormedit::ui::compose_resource_view_t, 1> resources{{
        {.resource_id = wormedit::resource_id_t{40}, .asset_id = wormedit::asset_id_t{400}, .roles = WORM_UI_COMPOSE_RESOURCE_ROLE_SAMPLE,
         .audio_asset = true, .target_index = "0A", .name = "kick.wav",
         .source_name = "kick.wav"}}};
    view.keysounds.assign(std::begin(sounds), std::end(sounds));
    view.keysounds.resize(view.keysound_total_count = sounds.size());
    view.keysounds_available = true;
    view.note_paint_mode_active = true;
    set_resource_fixture(view, resources.data(), resources.size());
    worm_ui_nuklear_compose_state().dock_layout.sounds_panel_visible = true;
    (void)harness.frame();

    CHECK(sample_workflow_uses_sounds(harness));
}

TEST_CASE("Compact Compose layout keeps Sounds available and restores optional docks when widened",
          "[frontend-state][event-harness][inspector-resources][compact]") {
    const std::array<wormedit::ui::compose_keysound_row_view_t, 1> sounds{{
        {.resource_id = wormedit::resource_id_t{40}, .slot = "", .name = "kick.wav"}}};
    {
        nuklear_event_harness_t compact{700, 800};
        REQUIRE(compact.ready());
        auto& view = compact.view().compose;
        view.keysounds.assign(std::begin(sounds), std::end(sounds));
        view.keysounds.resize(view.keysound_total_count = sounds.size());
        view.keysounds_available = true;
        auto& state = worm_ui_nuklear_compose_state();
        state.dock_layout.layers_panel_visible = true;
        (void)compact.frame();
        CHECK(compact_layout_keeps_sound_picker(compact));
        const auto toggle_status = compact_toolbar_sounds_toggle_status(compact);
        INFO(toggle_status);
        INFO(compact.drawn_text());
        CHECK(toggle_status.empty());
    }
    {
        nuklear_event_harness_t wide{1600, 900};
        REQUIRE(wide.ready());
        auto& view = wide.view().compose;
        view.keysounds.assign(std::begin(sounds), std::end(sounds));
        view.keysounds.resize(view.keysound_total_count = sounds.size());
        view.keysounds_available = true;
        worm_ui_nuklear_compose_state().dock_layout.layers_panel_visible = true;
        (void)wide.frame();
        CHECK(wide_layout_restores_optional_docks(wide));
    }
}
