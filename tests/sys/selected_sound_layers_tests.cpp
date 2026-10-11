#include "sys/compose_semantic_layer_commands.h"
#include <catch2/catch_test_macros.hpp>

using namespace wormedit;
namespace {
auto selected_layer_command_song() -> song_document_t {
    return {.id = song_id_t{1}, .revision = 4,
        .audio_assets = {{.id = asset_id_t{50}, .source_path = "sound.wav"}},
        .resources = {{.id = resource_id_t{51}, .asset = asset_id_t{50},
                       .roles = {resource_role_t::sample, resource_role_t::bgm}}},
        .charts = {{.id = chart_id_t{2}, .divisions_per_standard_measure = 960,
            .measures = {{.id = measure_id_t{3},
                .notes = {{.id = object_id_t{20}, .position = {measure_id_t{3}, 0},
                            .lane = lane_id_t{1}, .sound = resource_id_t{51}}},
                .bgm_events = {{.id = object_id_t{23}, .position = {measure_id_t{3}, 240},
                                .source_lane = object_id_t{33}, .sound = resource_id_t{51}}}}},
            .lane_layouts = {{.id = object_id_t{30}, .position = {measure_id_t{3}, 0},
                .layout = {.profile_id = "one-key", .lanes = {{.id = lane_id_t{1}, .name = "Key"}}}}},
            .semantic_layers = {{.id = layer_id_t{7}, .name = "Drums"}},
            .bgm_source_lanes = {{.id = object_id_t{33}, .name = "Music"}},
            .bpms = {{.id = object_id_t{40}, .position = {measure_id_t{3}, 0},
                       .beats_per_minute = {120, 1}}}}}};
}
void check_selected_layer_command(bool value) { CHECK(value); }
}

TEST_CASE("Compose selected sound assignment routes note and BGM IDs without changing selection",
          "[unit][sys][selected-sound-layers]") {
    auto history = make_document_history(selected_layer_command_song());
    REQUIRE(validate_document(history.document).empty());
    editor_interaction_state_t state;
    state.chart = chart_id_t{2};
    state.selected_notes = {object_id_t{20}};
    state.selected_objects = {{editor_selection_object_kind_t::note, object_id_t{20}},
        {editor_selection_object_kind_t::bgm_event, object_id_t{23}}};
    const auto applied = sys::apply_compose_selected_sound_layer_assignment(history, state, 4, layer_id_t{7});
    REQUIRE(applied);
    const auto& chart = history.document.charts.front();
    check_selected_layer_command(chart.sound_layer_defaults.size() == 1);
    check_selected_layer_command(chart.measures.front().notes.front().semantic_layers == std::vector{layer_id_t{7}});
    check_selected_layer_command(chart.measures.front().bgm_events.front().semantic_layers == std::vector{layer_id_t{7}});
    check_selected_layer_command(state.selected_notes == std::vector{object_id_t{20}});
    check_selected_layer_command(state.selected_objects.size() == 2);
    check_selected_layer_command(history.undo.size() == 1);
    const auto stale = sys::apply_compose_selected_sound_layer_assignment(history, state, 4, layer_id_t{7});
    check_selected_layer_command(!stale && !stale.diagnostics.empty());
    check_selected_layer_command(history.undo.size() == 1);
}
