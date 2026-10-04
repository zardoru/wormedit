#include "nuklear_event_harness.h"
#include "ui/nuklear/compose_state.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string_view>
#include <vector>

namespace {

auto mouse_button(bool down, float x, float y) -> SDL_Event {
    SDL_Event event{};
    event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.type = static_cast<SDL_EventType>(event.type);
    event.button.windowID = 0;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.down = down;
    event.button.clicks = 1;
    event.button.x = x;
    event.button.y = y;
    return event;
}

auto mouse_motion(float x, float y) -> SDL_Event {
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.windowID = 0;
    event.motion.x = x;
    event.motion.y = y;
    return event;
}

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

auto click_at(nuklear_event_harness_t& harness, float x, float y)
    -> std::vector<workspace_action_snapshot_t> {
    const std::array<event_step_t, 2> script{{
        {{mouse_motion(x, y), mouse_button(true, x, y)}, SDL_KMOD_NONE},
        {{mouse_motion(x, y), mouse_button(false, x, y)}, SDL_KMOD_NONE}}};
    return harness.run_script(script);
}

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
    -> const WormUiComposeInputAction* {
    for (const auto& frame : frames) {
        const auto& action = frame.get();
        for (std::size_t index = 0; index < action.compose_input_count; ++index) {
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

void set_resource_fixture(WormUiComposeView& view,
                          const WormUiComposeResourceView* resources, std::size_t resource_count) {
    view.resources = resources;
    view.resource_count = resource_count;
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
    if (!state.sound_list_valid) return false;
    (void)click_at(harness, state.sound_list_x + 70.0f, state.sound_list_y + 110.0f);
    if (state.keysound_selection.primary != 40 || state.armed_resource_id != 40 ||
        !state.armed_from_sounds_dock ||
        harness.drawn_text().find("Assign selected") == std::string::npos ||
        harness.drawn_text().find("Disarm") == std::string::npos)
        return false;
    const auto frames = click_at(harness, 300.0f, 400.0f);
    const auto* paint = note_paint_begin(frames);
    return paint && paint->resource_id == 40;
}

auto compact_layout_keeps_sound_picker(nuklear_event_harness_t& harness) -> bool {
    const auto& text = harness.drawn_text();
    return text.find("Sounds  1") != std::string::npos &&
           text.find("Label (0)") == std::string::npos &&
           text.find("Layers  0") == std::string::npos;
}

auto compact_toolbar_sounds_toggle_status(nuklear_event_harness_t& harness) -> std::string_view {
    auto& state = worm_ui_nuklear_compose_state();
    constexpr float sounds_button_x = 407.0f;
    // In the 700x800 harness, title (32), tabs (32), seek row (24), window padding,
    // and the toolbar's 27px row place the icon center at y=122.
    constexpr float toolbar_y = 122.0f;
    const auto hover = mouse_motion(sounds_button_x, toolbar_y);
    (void)harness.frame(std::span{&hover, 1});
    if (harness.drawn_text().find("Hide sounds list") == std::string::npos)
        return "hover did not identify Hide sounds list";
    (void)click_at(harness, sounds_button_x, toolbar_y);
    if (state.sounds_panel_visible || state.sound_list_valid)
        return "click did not hide the Sounds dock";
    const auto show_hover = mouse_motion(sounds_button_x, toolbar_y);
    (void)harness.frame(std::span{&show_hover, 1});
    if (harness.drawn_text().find("Show sounds list") == std::string::npos)
        return "hidden dock did not expose Show sounds list";
    (void)click_at(harness, sounds_button_x, toolbar_y);
    if (!state.sounds_panel_visible || !state.sound_list_valid)
        return "click did not reopen the Sounds dock hit region";
    return harness.drawn_text().find("Sounds  1") != std::string::npos
        ? std::string_view{} : "reopened dock did not render its Sounds row";
}

auto wide_layout_restores_optional_docks(nuklear_event_harness_t& harness) -> bool {
    const auto& text = harness.drawn_text();
    return text.find("Label (0)") != std::string::npos &&
           text.find("Layers  0") != std::string::npos;
}

auto bgm_catalog_is_available(nuklear_event_harness_t& harness) -> bool {
    const auto& text = harness.drawn_text();
    return text.find("music.ogg") != std::string::npos &&
           text.find("Place BGM at cursor") != std::string::npos;
}

auto bga_catalog_is_available(nuklear_event_harness_t& harness) -> bool {
    const auto& text = harness.drawn_text();
    return text.find("background.png") != std::string::npos &&
           text.find("Place BGA background at cursor") != std::string::npos;
}

auto generic_arm_and_disarm_clear_provenance(nuklear_event_harness_t& harness) -> bool {
    auto& state = worm_ui_nuklear_compose_state();
    state.resource_filter = worm_ui_nuklear_compose_resource_filter_t::bgm;
    state.armed_resource_id = 0;
    state.armed_from_bga_dock = true;
    state.bga_auto_increment_on_place = true;
    scroll_inspector(harness, -1.0f, 20);
    if (harness.drawn_text().find("music.ogg") == std::string::npos) return false;
    const auto arm_center = harness.drawn_text_center("Arm");
    if (!arm_center) return false;
    (void)click_at(harness, arm_center->first, arm_center->second);
    if (state.armed_resource_id != 100 || state.armed_from_bga_dock ||
        state.bga_auto_increment_on_place)
        return false;
    state.armed_from_bga_dock = true;
    state.bga_auto_increment_on_place = true;
    const auto disarm_center = harness.drawn_text_center("Disarm");
    if (!disarm_center) return false;
    (void)click_at(harness, disarm_center->first, disarm_center->second);
    return state.armed_resource_id == 0 && !state.armed_from_bga_dock &&
           !state.bga_auto_increment_on_place;
}

} // namespace

TEST_CASE("Inspector delegates sample listing and placement to the Sounds dock",
          "[frontend-state][event-harness][inspector-resources]") {
    nuklear_event_harness_t harness{1600, 900};
    REQUIRE(harness.ready());
    auto& view = harness.view().compose;
    const std::array<WormUiComposeKeysoundRowView, 1> sounds{{
        {.resource_id = 40, .slot = "0A", .name = "kick.wav"}}};
    const std::array<WormUiComposeResourceView, 1> resources{{
        {.resource_id = 40, .asset_id = 400, .roles = WORM_UI_COMPOSE_RESOURCE_ROLE_SAMPLE,
         .audio_asset = true, .target_index = "0A", .name = "kick.wav",
         .source_name = "kick.wav"}}};
    view.keysounds = sounds.data();
    view.keysound_count = view.keysound_total_count = sounds.size();
    view.keysounds_available = true;
    view.note_paint_mode_active = true;
    set_resource_fixture(view, resources.data(), resources.size());
    worm_ui_nuklear_compose_state().sounds_panel_visible = true;
    (void)harness.frame();

    CHECK(sample_workflow_uses_sounds(harness));
}

TEST_CASE("Compact Compose layout keeps Sounds available and restores optional docks when widened",
          "[frontend-state][event-harness][inspector-resources][compact]") {
    const std::array<WormUiComposeKeysoundRowView, 1> sounds{{
        {.resource_id = 40, .slot = "", .name = "kick.wav"}}};
    {
        nuklear_event_harness_t compact{700, 800};
        REQUIRE(compact.ready());
        auto& view = compact.view().compose;
        view.keysounds = sounds.data();
        view.keysound_count = view.keysound_total_count = sounds.size();
        view.keysounds_available = true;
        auto& state = worm_ui_nuklear_compose_state();
        state.layers_panel_visible = true;
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
        view.keysounds = sounds.data();
        view.keysound_count = view.keysound_total_count = sounds.size();
        view.keysounds_available = true;
        worm_ui_nuklear_compose_state().layers_panel_visible = true;
        (void)wide.frame();
        CHECK(wide_layout_restores_optional_docks(wide));
    }
}

TEST_CASE("BGM and BGA inspector catalogs stay available and generic arming clears dock provenance",
          "[frontend-state][event-harness][inspector-resources][bgm-bga]") {
    nuklear_event_harness_t harness{1600, 900};
    REQUIRE(harness.ready());
    auto& view = harness.view().compose;
    const std::array<WormUiComposeResourceView, 2> resources{{
        {.resource_id = 100, .asset_id = 1000, .roles = WORM_UI_COMPOSE_RESOURCE_ROLE_BGM,
         .target_index = "BGM", .name = "music.ogg", .source_name = "music.ogg"},
        {.resource_id = 200, .asset_id = 2000, .roles = WORM_UI_COMPOSE_RESOURCE_ROLE_BGA,
         .target_index = "01", .name = "background.png", .source_name = "background.png"}}};
    set_resource_fixture(view, resources.data(), resources.size());
    auto& state = worm_ui_nuklear_compose_state();
    state.resource_filter = worm_ui_nuklear_compose_resource_filter_t::bgm;
    view.selected_bgm_event_id = 11;
    view.selected_assigned_resource_id = 100;
    (void)harness.frame();
    scroll_inspector(harness, -1.0f, 20);
    INFO(harness.drawn_text());
    CHECK(bgm_catalog_is_available(harness));
    state.resource_filter = worm_ui_nuklear_compose_resource_filter_t::bga;
    (void)harness.frame();
    scroll_inspector(harness, -1.0f, 20);
    INFO(harness.drawn_text());
    CHECK(bga_catalog_is_available(harness));
}

TEST_CASE("Generic inspector Arm and Disarm clear BGA dock provenance",
          "[frontend-state][event-harness][inspector-resources][provenance]") {
    nuklear_event_harness_t harness{1600, 900};
    REQUIRE(harness.ready());
    auto& view = harness.view().compose;
    const std::array<WormUiComposeResourceView, 1> resources{{
        {.resource_id = 100, .asset_id = 1000, .roles = WORM_UI_COMPOSE_RESOURCE_ROLE_BGM,
         .target_index = "BGM", .name = "music.ogg", .source_name = "music.ogg"}}};
    set_resource_fixture(view, resources.data(), resources.size());
    auto& state = worm_ui_nuklear_compose_state();
    view.selected_bgm_event_id = 11;
    view.selected_assigned_resource_id = 100;
    (void)harness.frame();
    const auto provenance_cleared = generic_arm_and_disarm_clear_provenance(harness);
    INFO(harness.drawn_text());
    INFO("armed resource: " << state.armed_resource_id
         << ", BGA provenance: " << state.armed_from_bga_dock
         << ", BGA auto increment: " << state.bga_auto_increment_on_place);
    CHECK(provenance_cleared);
}
