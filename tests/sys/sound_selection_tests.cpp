#include "sys/application_command_dispatch.h"
#include <wormedit/core/command_registry.h>
#include <wormedit/core/interaction_dispatch.h>
#include "sys/compose_sound_selection.h"
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <source_location>
using namespace wormedit;
namespace {
void expect_sound(bool value, std::source_location location = std::source_location::current()) {
    INFO("caller line " << location.line());
    CHECK(value);
}
auto sound_song() -> song_document_t {
    song_document_t song;
    song.id = song_id_t{1};
    song.audio_assets = {{.id = asset_id_t{1}, .source_path = "kick.wav"},
                         {.id = asset_id_t{2}, .source_path = "snare.wav"}};
    song.resources = {{.id = resource_id_t{1},
                       .name = "kick.wav",
                       .asset = asset_id_t{1},
                       .roles = {resource_role_t::sample}},
                      {.id = resource_id_t{2},
                       .name = "kick.wav",
                       .asset = asset_id_t{1},
                       .roles = {resource_role_t::sample}},
                      {.id = resource_id_t{3},
                       .name = "snare.wav",
                       .asset = asset_id_t{2},
                       .roles = {resource_role_t::sample}}};
    chart_document_t chart;
    chart.id = chart_id_t{1};
    chart.divisions_per_standard_measure = 960;
    chart.measures = {{.id = measure_id_t{1}}, {.id = measure_id_t{2}}};
    chart.lane_layouts = {{.id = object_id_t{100},
                           .position = {measure_id_t{1}, 0},
                           .layout = {.profile_id = "test",
                                      .lanes = {{.id = lane_id_t{1}, .name = "one"},
                                                {.id = lane_id_t{2}, .name = "two"}}}}};
    chart.semantic_layers = {{.id = layer_id_t{1}, .name = "hidden", .visible = false}};
    chart.bgm_source_lanes = {{.id = object_id_t{90}, .name = "bgm"}};
    chart.measures[0].notes = {
        {.id = object_id_t{10},
         .position = {measure_id_t{1}, 100},
         .lane = lane_id_t{1},
         .sound = resource_id_t{1}},
        {.id = object_id_t{11},
         .position = {measure_id_t{1}, 0},
         .lane = lane_id_t{1},
         .kind = note_kind_t::hold,
         .end = local_position_t{measure_id_t{1}, 800},
         .sound = resource_id_t{1}},
        {.id = object_id_t{12},
         .position = {measure_id_t{1}, 300},
         .lane = lane_id_t{2},
         .sound = resource_id_t{1}},
        {.id = object_id_t{13},
         .position = {measure_id_t{1}, 350},
         .lane = lane_id_t{1},
         .sound = resource_id_t{1},
         .semantic_layers = {layer_id_t{1}}},
        {.id = object_id_t{14},
         .position = {measure_id_t{1}, 400},
         .lane = lane_id_t{1},
         .sound = resource_id_t{2}},
        {.id = object_id_t{15}, .position = {measure_id_t{1}, 450}, .lane = lane_id_t{1}}};
    chart.measures[1].notes = {{.id = object_id_t{16},
                                .position = {measure_id_t{2}, 0},
                                .lane = lane_id_t{1},
                                .sound = resource_id_t{3}}};
    chart.measures[0].bgm_events = {{.id = object_id_t{20},
                                     .position = {measure_id_t{1}, 300},
                                     .source_lane = object_id_t{90},
                                     .sound = resource_id_t{1}},
                                    {.id = object_id_t{21},
                                     .position = {measure_id_t{2}, 0},
                                     .source_lane = object_id_t{90},
                                     .sound = resource_id_t{1}}};
    chart.measures[1].bgm_events.push_back(chart.measures[0].bgm_events.back());
    chart.measures[0].bgm_events.pop_back();
    song.charts.push_back(std::move(chart));
    return song;
}
auto ids(const std::vector<editor_selection_object_t>& objects) -> std::vector<std::uint64_t> {
    std::vector<std::uint64_t> result;
    for (const auto& object : objects)
        result.push_back(object.id.value);
    return result;
}
} // namespace
TEST_CASE("same sound selection respects visible spans layers lanes and asset keys",
          "[sys][select-same-sound]") {
    auto song = sound_song();
    auto& chart = song.charts[0];
    const std::array resource{sound_selection_key_t{sound_selection_key_kind_t::resource, 1}};
    const std::array hidden{lane_id_t{2}};
    const sound_selection_query_t query{
        .keys = resource, .visible = sound_selection_range_t{250, 500}, .hidden_lanes = hidden};
    // File labels select the asset; explicit resource keys still select that one resource.
    expect_sound(sound_selection_object_key(song, chart, object_id_t{15}) == std::nullopt);
    const auto asset = sound_selection_object_key(song, chart, object_id_t{10});
    REQUIRE(asset);
    expect_sound(asset->kind == sound_selection_key_kind_t::asset);
    const std::array asset_keys{*asset};
    expect_sound(
        ids(select_same_sound(
            song, chart, {.keys = asset_keys, .visible = query.visible, .hidden_lanes = hidden})) ==
        std::vector<std::uint64_t>{11, 14, 20});
    for (auto& entry : song.resources)
        entry.target_identities.push_back({.family = format_family_t::bms, .identifier = "01"});
    expect_sound(ids(select_same_sound(song, chart, query)) == std::vector<std::uint64_t>{11, 20});
    expect_sound(ids(select_same_sound(song, chart, {.keys = resource})) ==
                 std::vector<std::uint64_t>{10, 11, 12, 13, 20, 21});
}
TEST_CASE("same sound command adds uses hover selection then dock without history",
          "[sys][select-same-sound]") {
    auto history = document_history_t::from(sound_song());
    std::optional<editor_interaction_state_t> state{editor_interaction_state_t{}};
    const std::array snaps{std::uint64_t{4}};
    const auto setup = editor_interaction_state_t::from(history, chart_id_t{1}, lane_id_t{1},
                                               {measure_id_t{1}, 0}, snaps, 4, 192);
    REQUIRE(setup);
    state = *setup.state;
    workspace_controller_t workspace;
    workspace.active_tab = workspace_tab_t::compose;
    workspace.selected_chart = chart_id_t{1};
    wormedit::sys::application_command_state_t application;
    application.project.sound_selection.sound_selection_chart = chart_id_t{1};
    application.project.sound_selection.sound_selection_revision = history.document.revision;
    application.project.sound_selection.sound_selection_dock = {resource_id_t{3}};
    application.project.sound_selection.sound_selection_hover = object_id_t{10};
    const auto& chart = history.document.charts[0];
    auto keys =
        wormedit::sys::compose_sound_selection_keys(history.document, chart, *state, application);
    expect_sound(keys.size() == 1 && keys[0].value == 1);
    application.project.sound_selection.sound_selection_hover.reset();
    state->selected_notes = {object_id_t{10}};
    keys =
        wormedit::sys::compose_sound_selection_keys(history.document, chart, *state, application);
    expect_sound(keys.size() == 1 && keys[0].value == 1);
    state->selected_notes.clear();
    keys =
        wormedit::sys::compose_sound_selection_keys(history.document, chart, *state, application);
    expect_sound(keys.size() == 1 && keys[0].value == 2);
    const auto revision = history.document.revision;
    const auto objects = wormedit::sys::compose_same_sound_objects(history.document, chart, *state,
                                                                   application, keys, false);
    const auto result = dispatch_editor_action(
        history, *state,
        select_editor_objects_action_t{objects, editor_area_selection_mode_t::add, false});
    for (const auto& diagnostic : result.diagnostics)
        UNSCOPED_INFO(diagnostic.message);
    expect_sound(result.accepted);
    expect_sound(state->selected_notes == std::vector<object_id_t>{object_id_t{16}});
    expect_sound(history.document.revision == revision);
    expect_sound(!undo_document_command(history));
}
TEST_CASE("BMSON same sound selection follows three retained channels across notes BGM and cuts",
          "[sys][select-same-sound][bmson]") {
    auto song = sound_song();
    auto& chart = song.charts[0];
    chart.source_family = format_family_t::bmson;
    chart.sound_channels = {
        {.id = object_id_t{200},
         .source_clip = {.source = asset_id_t{1}},
         .events = {{.id = object_id_t{210}}, {.id = object_id_t{211}}, {.id = object_id_t{212}}}},
        {.id = object_id_t{201},
         .source_clip = {.source = asset_id_t{1}},
         .events = {{.id = object_id_t{220}}}},
        {.id = object_id_t{202},
         .source_clip = {.source = asset_id_t{2}},
         .events = {{.id = object_id_t{230}}}}};
    chart.measures[0].notes[0].sound_channel_event = object_id_t{210};
    chart.measures[0].notes[1].sound_channel_event = object_id_t{211};
    chart.measures[0].notes[4].sound_channel_event = object_id_t{220};
    chart.measures[1].notes[0].sound_channel_event = object_id_t{230};
    chart.measures[0].bgm_events[0].sound_channel_event = object_id_t{212};
    const auto key = sound_selection_object_key(song, chart, object_id_t{10});
    REQUIRE(key);
    const std::array keys{*key};
    expect_sound(sound_selection_object_key(song, chart, object_id_t{212}) == key);
    expect_sound(ids(select_same_sound(song, chart, {.keys = keys})) ==
                 std::vector<std::uint64_t>{10, 11, 20});
    expect_sound(ids(select_same_sound(
                     song, chart, {.keys = keys, .visible = sound_selection_range_t{250, 500}})) ==
                 std::vector<std::uint64_t>{11, 20});
    expect_sound(!sound_selection_object_key(song, chart, object_id_t{12}));
    const auto second = sound_selection_resource_key(song, chart, resource_id_t{2});
    expect_sound(second == sound_selection_key_t{sound_selection_key_kind_t::channel, 201});
    chart.measures[0].notes[4].sound = resource_id_t{1};
    expect_sound(!sound_selection_resource_key(song, chart, resource_id_t{1})); // Ambiguous reuse must not guess a stem.

}

TEST_CASE("same sound select command has a remappable primary Shift A binding", "[sys][select-same-sound]") {
    std::string invoked;
    auto registry = command_registry_t::from([&](const auto& context) {
        invoked = context.command.value;
        return dispatch_outcome_t{.value = true};
    });
    const auto* command = find_command(registry, {"selection.select_same_sound_chart"});
    REQUIRE(command);
    REQUIRE(command->definition.default_shortcuts.size() == 1);
    const auto chord = command->definition.default_shortcuts.front();
    expect_sound(chord.key == shortcut_key_t::a);
    expect_sound((chord.modifiers & static_cast<shortcut_modifiers_t>(shortcut_modifier_t::shift)) != 0);
    command_focus_t focus;
    focus.workspace_active = true;
    focus.active_tab = workspace_tab_t::compose;
    focus.visible_tabs = {workspace_tab_t::compose};
    const auto primary = shortcut_primary_modifier_t::control;
    const shortcut_chord_t event_chord{shortcut_key_t::a,
        shortcut_modifier_t::control | shortcut_modifier_t::shift};
    (void)dispatch_shortcut(registry, {.chord = event_chord}, focus, primary);
    expect_sound(invoked == "selection.select_same_sound_chart");
    const auto parsed = wormedit::sys::parse_application_command(invoked);
    expect_sound(parsed && parsed->kind == wormedit::sys::application_command_kind_t::selection_select_same_sound_chart);
    expect_sound(remap_command(registry, {"selection.select_same_sound_chart"}, {{shortcut_key_t::f6, 0}}).value);
    invoked.clear();
    (void)dispatch_shortcut(registry, {.chord = event_chord}, focus, primary);
    expect_sound(invoked.empty());
    (void)dispatch_shortcut(registry, {.chord = {shortcut_key_t::f6, 0}}, focus, primary);
    expect_sound(invoked == "selection.select_same_sound_chart");
}
