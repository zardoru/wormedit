#include "nuklear_event_harness.h"
#include "ui/nuklear/audio_state.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <optional>
#include <source_location>
#include <string_view>

namespace {

void require_sound_usage_ui(bool value,
                            std::source_location source = std::source_location::current()) {
    INFO("Sound usage harness expectation at line " << source.line());
    REQUIRE(value);
}
void check_sound_usage_ui(bool value,
                          std::source_location source = std::source_location::current()) {
    INFO("Sound usage harness expectation at line " << source.line());
    CHECK(value);
}

auto button_event(bool down, float x, float y) -> SDL_Event {
    SDL_Event event{};
    const auto type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    event.type = static_cast<Uint32>(type);
    event.button.type = type;
    event.button.windowID = 0;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.down = down;
    event.button.clicks = 1;
    event.button.x = x;
    event.button.y = y;
    return event;
}

auto motion_event(float x, float y) -> SDL_Event {
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.windowID = 0;
    event.motion.x = x;
    event.motion.y = y;
    return event;
}

auto click_at(nuklear_event_harness_t& harness, float x, float y)
    -> std::vector<workspace_action_snapshot_t> {
    std::array<event_step_t, 2> script{};
    script[0].events = {motion_event(x, y), button_event(true, x, y)};
    script[1].events = {motion_event(x, y), button_event(false, x, y)};
    return harness.run_script(script);
}

void prepare_audio_view(nuklear_event_harness_t& harness, const WormUiSoundRowView* sounds,
                        std::size_t sound_count, const WormUiSoundUsageRowView* uses,
                        std::size_t use_count, std::uint64_t selected_resource) {
    auto& workspace = harness.view();
    workspace.active_tab = WORM_UI_WORKSPACE_TAB_AUDIO_SLICES;
    workspace.setup.visible = false;
    workspace.metadata.visible = false;
    workspace.timing.visible = false;
    workspace.compose.visible = false;
    workspace.compare.visible = false;
    workspace.audio_slices.visible = true;
    workspace.audio_slices.sound_rows = sounds;
    workspace.audio_slices.sound_row_count = sound_count;
    workspace.audio_slices.selected_resource_id = selected_resource;
    workspace.audio_slices.has_selected_asset = true;
    workspace.audio_slices.selected_asset_id = 10;
    workspace.audio_slices.usage_rows = uses;
    workspace.audio_slices.usage_row_count = use_count;
    workspace.audio_slices.usage_total_count = use_count;
    workspace.audio_slices.usage_valid = true;
    workspace.audio_slices.usage_song_id = workspace.project_id;
    workspace.audio_slices.usage_document_revision = workspace.document_revision;
    workspace.audio_slices.usage_resource_id = selected_resource;
    workspace.audio_slices.usage_asset_id = 10;
    worm_ui_nuklear_audio_state().selected_project_id = workspace.project_id;
    worm_ui_nuklear_audio_state().selected_resource_id = selected_resource;
}

auto click_text(nuklear_event_harness_t& harness, std::string_view text,
                std::source_location source = std::source_location::current())
    -> std::vector<workspace_action_snapshot_t> {
    const auto center = harness.drawn_text_center(text);
    if (!center) {
        FAIL("Visible text was not drawn at line " << source.line() << ": " << text
                                                   << "\nRendered text:\n"
                                                   << harness.drawn_text());
    }
    return click_at(harness, center->first, center->second);
}

auto click_action(const std::vector<workspace_action_snapshot_t>& snapshots,
                  WormUiWorkspaceActionKind expected,
                  std::source_location source = std::source_location::current())
    -> WormUiWorkspaceAction {
    std::optional<WormUiWorkspaceAction> matched;
    std::size_t emitted = 0;
    for (const auto& snapshot : snapshots) {
        const auto& action = snapshot.get();
        if (action.kind == WORM_UI_WORKSPACE_ACTION_NONE)
            continue;
        ++emitted;
        if (action.kind == expected)
            matched = action;
    }
    if (!matched || emitted != 1)
        FAIL("Expected one workspace action of kind " << static_cast<int>(expected) << " at line "
                                                      << source.line() << ", emitted " << emitted);
    return *matched;
}

} // namespace

TEST_CASE("Sounds rows preserve distinct resource selection for shared assets",
          "[event-harness][sounds][usage]") {
    nuklear_event_harness_t harness{1280, 900};
    const std::array sounds{
        WormUiSoundRowView{.resource_id = 101, .asset_id = 10, .name = "Primary", .kind = 2},
        WormUiSoundRowView{.resource_id = 102, .asset_id = 10, .name = "Alias", .kind = 2}};
    prepare_audio_view(harness, sounds.data(), sounds.size(), nullptr, 0, 101);
    check_sound_usage_ui(harness.ready());
    (void)harness.frame();
    const auto alias = click_action(click_text(harness, "Alias  ·  Keysound"),
                                    WORM_UI_WORKSPACE_ACTION_SELECT_SOUND_RESOURCE);
    check_sound_usage_ui(alias.audio_resource_id == 102);
    check_sound_usage_ui(alias.audio_asset_id == 0);
}

TEST_CASE("Find uses activates positioned references and source rows without inventing a position",
          "[event-harness][sounds][usage]") {
    nuklear_event_harness_t harness{1280, 900};
    const std::array sounds{
        WormUiSoundRowView{.resource_id = 101, .asset_id = 10, .name = "Loop", .kind = 1}};
    const std::array uses{WormUiSoundUsageRowView{.chart_id = 7,
                                                  .measure_id = 8,
                                                  .row = 12,
                                                  .object_id = 99,
                                                  .referenced_resource_id = 101,
                                                  .referenced_asset_id = 10,
                                                  .kind = 0,
                                                  .scope = 0,
                                                  .position_valid = true,
                                                  .chart_label = "Chart A",
                                                  .position_label = "Measure 3 · row 12"},
                          WormUiSoundUsageRowView{.chart_id = 9,
                                                  .channel_id = 50,
                                                  .clip_id = 51,
                                                  .referenced_asset_id = 10,
                                                  .kind = 3,
                                                  .scope = 1,
                                                  .position_valid = false,
                                                  .chart_label = "Chart B",
                                                  .position_label = "Source clip"}};
    prepare_audio_view(harness, sounds.data(), sounds.size(), uses.data(), uses.size(), 101);
    check_sound_usage_ui(harness.ready());
    (void)harness.frame();
    (void)click_text(harness, "Find uses");
    check_sound_usage_ui(worm_ui_nuklear_audio_state().find_uses_open);
    (void)harness.frame();

    (void)click_text(harness, "Chart A");
    const auto note_action = click_action(click_text(harness, "Measure 3 · row 12"),
                                          WORM_UI_WORKSPACE_ACTION_SOUND_USAGE_ACTIVATE);
    check_sound_usage_ui(note_action.kind == WORM_UI_WORKSPACE_ACTION_SOUND_USAGE_ACTIVATE);
    check_sound_usage_ui(note_action.sound_usage_song_id == harness.view().project_id);
    check_sound_usage_ui(note_action.sound_usage_document_revision ==
                         harness.view().document_revision);
    check_sound_usage_ui(note_action.sound_usage_resource_id == 101);
    check_sound_usage_ui(note_action.sound_usage_chart_id == 7);
    check_sound_usage_ui(note_action.sound_usage_measure_id == 8);
    check_sound_usage_ui(note_action.sound_usage_row == 12);
    check_sound_usage_ui(note_action.sound_usage_object_id == 99);
    check_sound_usage_ui(note_action.sound_usage_position_valid);

    (void)harness.frame();
    (void)click_text(harness, "Chart B");
    const auto source_action = click_action(click_text(harness, "Source clip"),
                                            WORM_UI_WORKSPACE_ACTION_SOUND_USAGE_ACTIVATE);
    check_sound_usage_ui(source_action.kind == WORM_UI_WORKSPACE_ACTION_SOUND_USAGE_ACTIVATE);
    check_sound_usage_ui(source_action.sound_usage_chart_id == 9);
    check_sound_usage_ui(source_action.sound_usage_channel_id == 50);
    check_sound_usage_ui(source_action.sound_usage_clip_id == 51);
    check_sound_usage_ui(!source_action.sound_usage_position_valid);
    check_sound_usage_ui(source_action.sound_usage_measure_id == 0);
    const auto& state = worm_ui_nuklear_audio_state();
    check_sound_usage_ui(state.selected_channel_id == 0);
    check_sound_usage_ui(state.selected_clip_id == 0);
}

TEST_CASE("Find uses activation keeps the resource asset when transport shows another asset",
          "[event-harness][sounds][usage]") {
    nuklear_event_harness_t harness{1280, 900};
    const std::array sounds{WormUiSoundRowView{
        .resource_id = 201, .asset_id = 0, .name = "Detached", .kind = 2, .missing = true}};
    const std::array uses{WormUiSoundUsageRowView{.chart_id = 7,
                                                  .measure_id = 8,
                                                  .row = 3,
                                                  .object_id = 200,
                                                  .referenced_resource_id = 201,
                                                  .kind = 0,
                                                  .scope = 0,
                                                  .position_valid = true,
                                                  .chart_label = "Chart C",
                                                  .position_label = "Measure 2 · row 3"}};
    prepare_audio_view(harness, sounds.data(), sounds.size(), uses.data(), uses.size(), 201);
    harness.view().audio_slices.usage_asset_id = 0;
    check_sound_usage_ui(harness.ready());
    (void)harness.frame();
    (void)click_text(harness, "Find uses");
    (void)harness.frame();
    (void)click_text(harness, "Chart C");
    const auto action = click_action(click_text(harness, "Measure 2 · row 3"),
                                     WORM_UI_WORKSPACE_ACTION_SOUND_USAGE_ACTIVATE);
    check_sound_usage_ui(action.kind == WORM_UI_WORKSPACE_ACTION_SOUND_USAGE_ACTIVATE);
    check_sound_usage_ui(action.sound_usage_resource_id == 201);
    check_sound_usage_ui(action.sound_usage_asset_id == 0);
    check_sound_usage_ui(action.sound_usage_chart_id == 7);
    check_sound_usage_ui(action.sound_usage_object_id == 200);
    check_sound_usage_ui(action.sound_usage_position_valid);
}

TEST_CASE("verified source selection setter retains exact channel and clip identity",
          "[frontend-state][sounds][usage]") {
    worm_ui_nuklear_reset_audio_state();
    worm_ui_frontend_select_sound_usage_source(1, 101, 50, 51);
    WormUiSoundUsageRequest request{};
    worm_ui_frontend_sound_usage_request(&request);
    check_sound_usage_ui(request.selected_project_id == 1);
    check_sound_usage_ui(request.selected_resource_id == 101);
    check_sound_usage_ui(request.selected_channel_id == 50);
    check_sound_usage_ui(request.selected_clip_id == 51);
    check_sound_usage_ui(!request.find_uses_open);
    worm_ui_nuklear_reset_audio_state();
}
