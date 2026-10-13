#include <wormedit/core/bmson_sound_channel_editor.h>
#include <wormedit/core/interaction.h>
#include <wormedit/core/note_to_bgm.h>
#include <wormedit/formats/bmson.h>
#include <wormedit/formats/bms.h>
#include <wormedit/formats/bms_writer.h>
#include <wormedit/formats/native_project.h>
#include "sys/compose_note_to_bgm.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <span>
#include <source_location>
#include <string>

using namespace wormedit;

namespace {
template <typename T>
void check_move(const T& value, std::source_location where = std::source_location::current()) {
    INFO("assertion caller line " << where.line());
    CHECK(static_cast<bool>(value));
}

template <typename T>
void require_move(const T& value, std::source_location where = std::source_location::current()) {
    INFO("assertion caller line " << where.line());
    REQUIRE(static_cast<bool>(value));
}

auto plain_song() -> song_document_t {
    song_document_t song;
    song.id = song_id_t{1};
    song.revision = 3;
    chart_document_t chart;
    chart.id = chart_id_t{1};
    chart.divisions_per_standard_measure = 960;
    chart.measures = {{.id = measure_id_t{1}}, {.id = measure_id_t{2}}};
    chart.lane_layouts.push_back({.id = object_id_t{100}, .position = {measure_id_t{1}, 0},
        .layout = {.profile_id = "t", .lanes = {{.id = lane_id_t{1}, .name = "A"}, {.id = lane_id_t{2}, .name = "B"}}}});
    chart.bgm_source_lanes = {{.id = object_id_t{9}, .name = "BGM 1", .source_order = 0},
                              {.id = object_id_t{10}, .name = "BGM 2", .source_order = 1}};
    chart.measures[0].notes = {{.id = object_id_t{1}, .position = {measure_id_t{1}, 240}, .lane = lane_id_t{1}},
                               {.id = object_id_t{2}, .position = {measure_id_t{1}, 240}, .lane = lane_id_t{2}}};
    chart.measures[1].notes = {{.id = object_id_t{3}, .position = {measure_id_t{2}, 0}, .lane = lane_id_t{1}}};
    song.charts.push_back(std::move(chart));
    return song;
}

auto grouped_song() -> song_document_t {
    auto song = plain_song();
    song.resources = {
        {.id = resource_id_t{1}, .asset = asset_id_t{10}, .sound_group = sound_group_id_t{7}},
        {.id = resource_id_t{2}, .asset = asset_id_t{11}, .sound_group = sound_group_id_t{7}},
        {.id = resource_id_t{3}, .asset = asset_id_t{10}}};
    auto& chart = song.charts.front();
    chart.measures[0].notes[0].sound = resource_id_t{1};
    chart.measures[0].notes[1].sound = resource_id_t{2};
    chart.measures[1].notes[0].sound = resource_id_t{2};
    chart.measures[1].bgm_events = {
        {.id = object_id_t{20}, .position = {measure_id_t{2}, 100}, .source_lane = object_id_t{9},
         .sound = resource_id_t{1}},
        {.id = object_id_t{21}, .position = {measure_id_t{2}, 200}, .source_lane = object_id_t{10},
         .sound = resource_id_t{1}},
        {.id = object_id_t{22}, .position = {measure_id_t{2}, 300}, .source_lane = object_id_t{10},
         .sound = resource_id_t{2}}};
    return song;
}

auto hold_restore_rejected(note_t obstacle) -> bool {
    auto song = plain_song();
    auto& chart = song.charts.front();
    chart.lane_layouts.front().layout.lanes.push_back({.id = lane_id_t{3}, .name = "C"});
    chart.measures.front().bgm_events = {{.id = object_id_t{90},
        .position = {measure_id_t{1}, 240}, .source_lane = object_id_t{9},
        .end = local_position_t{measure_id_t{2}, 120}}};
    obstacle.lane = lane_id_t{3};
    const auto destination = std::ranges::find(chart.measures, obstacle.position.measure,
                                                &measure_chunk_t::id);
    if (destination == chart.measures.end()) return false;
    destination->notes.push_back(std::move(obstacle));
    if (!validate_document(song).empty()) return false;
    auto history = document_history_t::from(song);
    const auto before = history.document;
    const std::array events{object_id_t{90}};
    const auto result = move_bgm_events_to_lane(history, history.document.revision,
        chart_id_t{1}, events, lane_id_t{3});
    const auto before_bytes = write_native_project(before);
    const auto after_bytes = write_native_project(history.document);
    return !result.applied && before_bytes.bytes && after_bytes.bytes &&
           *before_bytes.bytes == *after_bytes.bytes && history.undo.empty();
}

auto find_test_note(const chart_document_t& chart, object_id_t id) -> const note_t* {
    for (const auto& measure : chart.measures) {
        const auto found = std::ranges::find(measure.notes, id, &note_t::id);
        if (found != measure.notes.end()) return &*found;
    }
    return nullptr;
}

auto same_native_document(const song_document_t& left, const song_document_t& right) -> bool {
    const auto left_bytes = write_native_project(left);
    const auto right_bytes = write_native_project(right);
    return left_bytes.bytes && right_bytes.bytes && *left_bytes.bytes == *right_bytes.bytes;
}

} // namespace

TEST_CASE("automatic BGM planning groups sounds and retains deterministic event identities",
          "[unit][core][note-to-bgm]") {
    const auto song = grouped_song();
    const auto& chart = song.charts.front();
    const std::array selected{object_id_t{1}, object_id_t{3}};
    const auto plan = plan_notes_to_bgm(chart, song.resources, selected, std::nullopt);
    require_move(plan.accepted());
    require_move(plan.assignments.size() == 2);
    check_move(plan.assignments[0].note == object_id_t{1});
    check_move(plan.assignments[0].lane == object_id_t{10});
    check_move(plan.assignments[1].note == object_id_t{3});
    check_move(plan.assignments[1].lane == object_id_t{10});
    check_move(plan.retained_event_ids == std::vector<object_id_t>{object_id_t{1}, object_id_t{3}});

    const std::array reversed{object_id_t{3}, object_id_t{1}};
    const auto permuted = plan_notes_to_bgm(chart, song.resources, reversed, std::nullopt);
    require_move(permuted.accepted());
    check_move(permuted.assignments == plan.assignments);
}

TEST_CASE("same-asset ungrouped resources do not join a tagged sound group",
          "[unit][core][note-to-bgm]") {
    auto song = grouped_song();
    song.charts.front().measures[1].notes[0].sound = resource_id_t{3};
    const auto& chart = song.charts.front();
    const std::array selected{object_id_t{1}, object_id_t{3}};
    const auto plan = plan_notes_to_bgm(chart, song.resources, selected, std::nullopt);
    require_move(plan.accepted());
    check_move(plan.assignments[0].lane == object_id_t{10});
    check_move(plan.assignments[1].lane == object_id_t{9});
}

TEST_CASE("tagged groups and implicit resource keys stay distinct with deterministic tie ranking",
          "[unit][core][note-to-bgm]") {
    auto song = plain_song();
    song.resources = {
        {.id = resource_id_t{1}, .asset = asset_id_t{10}},
        {.id = resource_id_t{2}, .asset = asset_id_t{11}, .sound_group = sound_group_id_t{1}}};
    auto& chart = song.charts.front();
    chart.measures[0].notes[0].sound = resource_id_t{1};
    chart.measures[0].notes[1].sound = resource_id_t{2};
    chart.measures[0].bgm_events = {
        {.id = object_id_t{20}, .position = {measure_id_t{1}, 40}, .source_lane = object_id_t{9},
         .sound = resource_id_t{1}},
        {.id = object_id_t{21}, .position = {measure_id_t{1}, 80}, .source_lane = object_id_t{9},
         .sound = resource_id_t{1}},
        {.id = object_id_t{22}, .position = {measure_id_t{1}, 120}, .source_lane = object_id_t{10},
         .sound = resource_id_t{2}},
        {.id = object_id_t{23}, .position = {measure_id_t{1}, 160}, .source_lane = object_id_t{10},
         .sound = resource_id_t{2}},
        {.id = object_id_t{24}, .position = {measure_id_t{1}, 200}, .source_lane = object_id_t{10},
         .sound = resource_id_t{1}},
        {.id = object_id_t{25}, .position = {measure_id_t{1}, 220}, .source_lane = object_id_t{10},
         .sound = resource_id_t{1}}};
    const std::array selected{object_id_t{1}, object_id_t{2}};
    const auto plan = plan_notes_to_bgm(chart, song.resources, selected, std::nullopt);
    require_move(plan.accepted());
    check_move(plan.assignments[0].lane == object_id_t{9});
    check_move(plan.assignments[1].lane == object_id_t{10});
}

TEST_CASE("automatic BGM planning spills same-row notes and keeps silent notes on the lowest lane",
          "[unit][core][note-to-bgm]") {
    auto song = grouped_song();
    auto& chart = song.charts.front();
    chart.measures[0].notes[0].position.row = 480;
    chart.measures[0].notes[1].position.row = 480;
    chart.measures[0].notes[0].sound = resource_id_t{1};
    chart.measures[0].notes[1].sound = resource_id_t{2};
    const std::array both{object_id_t{2}, object_id_t{1}};
    const auto plan = plan_notes_to_bgm(chart, song.resources, both, std::nullopt);
    require_move(plan.accepted());
    check_move(plan.assignments[0].lane == object_id_t{10});
    check_move(plan.assignments[1].lane == object_id_t{9});

    chart.measures[0].notes[0].sound.reset();
    const std::array silent{object_id_t{1}};
    const auto silent_plan = plan_notes_to_bgm(chart, song.resources, silent, std::nullopt);
    require_move(silent_plan.accepted());
    check_move(silent_plan.assignments.front().lane == object_id_t{9});
}

TEST_CASE("automatic allocation keeps first-appearance cohorts and silent notes in document order",
          "[unit][core][note-to-bgm]") {
    auto song = grouped_song();
    auto& chart = song.charts.front();
    chart.bgm_source_lanes.resize(1);
    chart.measures[0].notes[0].position.row = 360;
    chart.measures[0].notes[1].position.row = 360;
    chart.measures[0].notes[0].sound.reset();
    chart.measures[0].notes[1].sound = resource_id_t{1};
    const std::array both{object_id_t{1}, object_id_t{2}};
    const auto silent_first = plan_notes_to_bgm(chart, song.resources, both, std::nullopt);
    require_move(silent_first.accepted());
    check_move(silent_first.assignments[0].note == object_id_t{1});
    check_move(silent_first.assignments[0].lane == object_id_t{9});
    check_move(silent_first.assignments[1].lane == object_id_t{101});

    std::swap(chart.measures[0].notes[0], chart.measures[0].notes[1]);
    const auto keyed_first = plan_notes_to_bgm(chart, song.resources, both, std::nullopt);
    require_move(keyed_first.accepted());
    check_move(keyed_first.assignments[0].note == object_id_t{2});
    check_move(keyed_first.assignments[0].lane == object_id_t{9});
    check_move(keyed_first.assignments[1].lane == object_id_t{101});
}

TEST_CASE("a remembered spill lane outranks a newly free lower lane for later group notes",
          "[unit][core][note-to-bgm]") {
    auto song = plain_song();
    song.resources = {{.id = resource_id_t{1}, .asset = asset_id_t{10},
                       .sound_group = sound_group_id_t{7}}};
    auto& chart = song.charts.front();
    chart.bgm_source_lanes = {{.id = object_id_t{8}, .source_order = 0},
                              {.id = object_id_t{9}, .source_order = 1},
                              {.id = object_id_t{10}, .source_order = 2}};
    chart.measures[0].notes[0].sound = resource_id_t{1};
    chart.measures[1].notes[0].sound = resource_id_t{1};
    chart.measures[0].bgm_events = {
        {.id = object_id_t{20}, .position = {measure_id_t{1}, 40}, .source_lane = object_id_t{10},
         .sound = resource_id_t{1}},
        {.id = object_id_t{21}, .position = {measure_id_t{1}, 240}, .source_lane = object_id_t{8}},
        {.id = object_id_t{22}, .position = {measure_id_t{1}, 240}, .source_lane = object_id_t{10},
         .sound = resource_id_t{1}}};
    chart.measures[1].bgm_events = {
        {.id = object_id_t{23}, .position = {measure_id_t{2}, 0}, .source_lane = object_id_t{10},
         .sound = resource_id_t{1}}};
    const std::array selected{object_id_t{1}, object_id_t{3}};
    const auto plan = plan_notes_to_bgm(chart, song.resources, selected, std::nullopt);
    require_move(plan.accepted());
    check_move(plan.assignments[0].lane == object_id_t{9});
    check_move(plan.assignments[1].lane == object_id_t{9});
}

TEST_CASE("BGM source-order exhaustion only rejects required growth",
          "[unit][core][note-to-bgm]") {
    auto song = plain_song();
    auto& chart = song.charts.front();
    chart.bgm_source_lanes = {{.id = object_id_t{9},
        .source_order = std::numeric_limits<std::size_t>::max() - 1}};
    const std::array two{object_id_t{1}, object_id_t{2}};
    const auto final_order = plan_notes_to_bgm(chart, song.resources, two, std::nullopt);
    require_move(final_order.accepted());
    require_move(final_order.new_lanes.size() == 1);
    check_move(final_order.new_lanes.front().source_order == std::numeric_limits<std::size_t>::max());

    chart.bgm_source_lanes.front().source_order = std::numeric_limits<std::size_t>::max();
    const std::array one{object_id_t{1}};
    const auto existing_lane = plan_notes_to_bgm(chart, song.resources, one, std::nullopt);
    require_move(existing_lane.accepted());
    check_move(existing_lane.new_lanes.empty());
    const auto exhausted = plan_notes_to_bgm(chart, song.resources, two, std::nullopt);
    check_move(!exhausted.accepted());
}

TEST_CASE("manual BGM placement keeps the right-then-left fallback and new-lane policy is atomic",
          "[unit][core][note-to-bgm]") {
    auto song = plain_song();
    auto& chart = song.charts.front();
    const std::array reversed{object_id_t{2}, object_id_t{1}};
    const auto manual = plan_notes_to_bgm(chart, song.resources, reversed, object_id_t{9});
    require_move(manual.accepted());
    check_move(manual.assignments[0].note == object_id_t{2});
    check_move(manual.assignments[0].lane == object_id_t{9});
    check_move(manual.assignments[1].lane == object_id_t{10});
    chart.measures[0].bgm_events = {
        {.id = object_id_t{20}, .position = {measure_id_t{1}, 240}, .source_lane = object_id_t{10}}};
    const std::array one{object_id_t{1}};
    const auto fallback = plan_notes_to_bgm(chart, song.resources, one, object_id_t{10});
    require_move(fallback.accepted());
    check_move(fallback.assignments.front().lane == object_id_t{9});
    const auto invalid_target = plan_notes_to_bgm(chart, song.resources, one, object_id_t{0});
    require_move(invalid_target.accepted());
    check_move(invalid_target.assignments.front().lane == object_id_t{9});

    chart.measures[0].notes.push_back(
        {.id = object_id_t{4}, .position = {measure_id_t{1}, 240}, .lane = lane_id_t{3}});
    chart.lane_layouts.front().layout.lanes.push_back({.id = lane_id_t{3}, .name = "C"});
    const std::array three{object_id_t{1}, object_id_t{2}, object_id_t{4}};
    const auto denied = plan_notes_to_bgm(chart, song.resources, three, std::nullopt,
                                           {.allow_new_lanes = false, .max_lanes = 2});
    check_move(!denied.accepted());
    auto history = document_history_t::from(song);
    const auto before = history.document;
    const auto rejected = move_notes_to_bgm(history, history.document.revision, chart_id_t{1}, three,
                                             std::nullopt, {.allow_new_lanes = false, .max_lanes = 2});
    check_move(!rejected.applied);
    check_move(history.document.revision == before.revision);
    check_move(history.document.charts.front().measures[0].notes.size() ==
               before.charts.front().measures[0].notes.size());
    check_move(history.document.charts.front().measures[0].bgm_events.size() ==
               before.charts.front().measures[0].bgm_events.size());
    check_move(history.undo.empty());
}

TEST_CASE("ordinary BGM conversion preserves provenance and uses one undo entry",
          "[unit][core][note-to-bgm]") {
    auto song = plain_song();
    auto& chart = song.charts.front();
    chart.semantic_layers.push_back({.id = layer_id_t{5}, .name = "accent"});
    chart.measures[0].notes[0].semantic_layers = {layer_id_t{5}};
    chart.measures[0].notes[0].branch = branch_id_t{9};
    chart.measures[0].notes[0].provenance = {
        .family = format_family_t::bms, .profile = "bms", .source_field = "wav",
        .source_spelling = "01", .source_order = 17};
    const auto expected = chart.measures[0].notes[0].provenance;
    auto history = document_history_t::from(song);
    const std::array selected{object_id_t{1}};
    const auto moved = move_notes_to_bgm(history, history.document.revision, chart_id_t{1}, selected, std::nullopt);
    require_move(moved.applied);
    const auto& events = history.document.charts.front().measures[0].bgm_events;
    const auto event = std::ranges::find(events, object_id_t{1}, &bgm_event_t::id);
    require_move(event != events.end());
    check_move(event->provenance.family == expected.family);
    check_move(event->provenance.profile == expected.profile);
    check_move(event->provenance.source_field == expected.source_field);
    check_move(event->provenance.source_spelling == expected.source_spelling);
    check_move(event->provenance.source_order == expected.source_order);
    check_move(event->semantic_layers == std::vector<layer_id_t>{layer_id_t{5}});
    check_move(event->branch == branch_id_t{9});
    check_move(history.undo.size() == 1);
    require_move(undo_document_command(history));
    check_move(redo_document_command(history));
    const auto restored = std::ranges::find(history.document.charts.front().measures[0].bgm_events,
                                             object_id_t{1}, &bgm_event_t::id);
    require_move(restored != history.document.charts.front().measures[0].bgm_events.end());
    check_move(restored->provenance.profile == expected.profile);
    check_move(restored->provenance.source_order == expected.source_order);
}

TEST_CASE("cross-measure holds round-trip through BGM and retain one-step history",
          "[unit][core][note-to-bgm][bgm-to-lane]") {
    auto song = plain_song();
    song.audio_assets.push_back({.id = asset_id_t{10}, .source_path = "hold.wav"});
    song.resources.push_back({.id = resource_id_t{20}, .asset = asset_id_t{10},
                              .roles = {resource_role_t::sample, resource_role_t::bgm}});
    auto& chart = song.charts.front();
    chart.lane_layouts.front().layout.lanes.push_back({.id = lane_id_t{3}, .name = "C"});
    chart.semantic_layers.push_back({.id = layer_id_t{5}, .name = "accent"});
    auto& source = chart.measures.front().notes.front();
    source.kind = note_kind_t::hold;
    source.end = local_position_t{measure_id_t{2}, 120};
    source.sound = resource_id_t{20};
    source.semantic_layers = {layer_id_t{5}};
    source.branch = branch_id_t{9};
    source.provenance = {.family = format_family_t::bms, .profile = "bms",
                         .source_field = "11 slot 0 object 01", .source_order = 8};
    const auto provenance = source.provenance;
    require_move(validate_document(song).empty());
    auto history = document_history_t::from(song);
    const std::array selected{source.id};
    require_move(move_notes_to_bgm(history, history.document.revision, chart_id_t{1}, selected,
                                   std::nullopt).applied);
    const auto& bgm = history.document.charts.front().measures.front().bgm_events.front();
    check_move(bgm.id == source.id);
    check_move(bgm.position == source.position);
    check_move(bgm.sound == source.sound);
    check_move(bgm.semantic_layers == source.semantic_layers);
    check_move(bgm.branch == source.branch);
    check_move(bgm.provenance.family == source.provenance.family);
    check_move(bgm.provenance.profile == source.provenance.profile);
    check_move(bgm.provenance.source_field == source.provenance.source_field);
    check_move(bgm.provenance.source_spelling == source.provenance.source_spelling);
    check_move(bgm.provenance.source_order == source.provenance.source_order);
    check_move(bgm.end == local_position_t{measure_id_t{2}, 120});
    const std::array bgm_ids{source.id};
    require_move(move_bgm_events_to_lane(history, history.document.revision, chart_id_t{1},
                                         bgm_ids, lane_id_t{3}).applied);
    const auto* restored = find_test_note(history.document.charts.front(), source.id);
    require_move(restored);
    check_move(restored->kind == note_kind_t::hold);
    check_move(restored->id == source.id);
    check_move(restored->position == source.position);
    check_move(restored->sound == source.sound);
    check_move(restored->semantic_layers == source.semantic_layers);
    check_move(restored->branch == source.branch);
    check_move(restored->end == local_position_t{measure_id_t{2}, 120});
    check_move(restored->provenance.family == provenance.family);
    check_move(restored->provenance.profile == provenance.profile);
    check_move(restored->provenance.source_field == provenance.source_field);
    check_move(restored->provenance.source_spelling == provenance.source_spelling);
    check_move(restored->provenance.source_order == provenance.source_order);
    require_move(history.undo.size() == 2);
    require_move(undo_document_command(history));
    check_move(std::ranges::any_of(history.document.charts.front().measures, [&](const auto& measure) {
        return std::ranges::any_of(measure.bgm_events, [&](const auto& event) {
            return event.id == source.id && event.end == local_position_t{measure_id_t{2}, 120};
        });
    }));
    require_move(undo_document_command(history));
    require_move(redo_document_command(history));
    require_move(redo_document_command(history));
    restored = find_test_note(history.document.charts.front(), source.id);
    require_move(restored);
    check_move(restored->end == local_position_t{measure_id_t{2}, 120});
}

TEST_CASE("BGM hold restoration rejects occupied and unavailable intervals atomically",
          "[unit][core][note-to-bgm][bgm-to-lane]") {
    auto song = plain_song();
    auto& chart = song.charts.front();
    chart.lane_layouts.front().layout.lanes.push_back({.id = lane_id_t{3}, .name = "C"});
    chart.measures.front().bgm_events = {{.id = object_id_t{90},
        .position = {measure_id_t{1}, 240}, .source_lane = object_id_t{9},
        .end = local_position_t{measure_id_t{2}, 120}}};
    chart.measures.front().notes.push_back({.id = object_id_t{91},
        .position = {measure_id_t{1}, 400}, .lane = lane_id_t{3}});
    auto history = document_history_t::from(song);
    const auto before = history.document;
    const std::array event_ids{object_id_t{90}};
    const auto result = move_bgm_events_to_lane(history, history.document.revision,
        chart_id_t{1}, event_ids, lane_id_t{3});
    check_move(!result.applied);
    check_move(same_native_document(history.document, before));
    check_move(history.undo.empty());

    auto lost = plain_song();
    auto& lost_chart = lost.charts.front();
    lost_chart.lane_layouts.front().layout.lanes.push_back({.id = lane_id_t{3}, .name = "C"});
    auto middle = lost_chart.lane_layouts.front();
    middle.id = object_id_t{101};
    middle.position = {measure_id_t{1}, 360};
    std::erase_if(middle.layout.lanes, [](const auto& lane) { return lane.id == lane_id_t{3}; });
    auto restored_layout = lost_chart.lane_layouts.front();
    restored_layout.id = object_id_t{102};
    restored_layout.position = {measure_id_t{2}, 0};
    lost_chart.lane_layouts.push_back(middle);
    lost_chart.lane_layouts.push_back(restored_layout);
    lost_chart.measures.front().bgm_events = {{.id = object_id_t{92},
        .position = {measure_id_t{1}, 240}, .source_lane = object_id_t{9},
        .end = local_position_t{measure_id_t{2}, 120}}};
    auto unavailable = document_history_t::from(lost);
    const std::array lost_ids{object_id_t{92}};
    check_move(!move_bgm_events_to_lane(unavailable, unavailable.document.revision,
        chart_id_t{1}, lost_ids, lane_id_t{3}).applied);
    check_move(unavailable.undo.empty());

    auto hidden = lost;
    auto& hidden_chart = hidden.charts.front();
    const auto transition = std::ranges::find(hidden_chart.lane_layouts,
        local_position_t{measure_id_t{1}, 360}, &lane_layout_segment_t::position);
    require_move(transition != hidden_chart.lane_layouts.end());
    transition->layout.lanes.push_back({.id = lane_id_t{3}, .name = "C",
                                        .role = lane_role_t::mine});
    auto hidden_history = document_history_t::from(hidden);
    const std::array hidden_ids{object_id_t{92}};
    check_move(!move_bgm_events_to_lane(hidden_history, hidden_history.document.revision,
        chart_id_t{1}, hidden_ids, lane_id_t{3}).applied);
    check_move(hidden_history.undo.empty());
}

TEST_CASE("restored hold spans reject inclusive and nested note collisions",
          "[unit][core][note-to-bgm][bgm-to-lane]") {
    check_move(hold_restore_rejected({.id = object_id_t{101},
        .position = {measure_id_t{1}, 240}, .lane = lane_id_t{3}}));
    check_move(hold_restore_rejected({.id = object_id_t{102},
        .position = {measure_id_t{2}, 120}, .lane = lane_id_t{3}}));
    check_move(hold_restore_rejected({.id = object_id_t{103},
        .position = {measure_id_t{1}, 400}, .lane = lane_id_t{3}}));
    check_move(hold_restore_rejected({.id = object_id_t{104},
        .position = {measure_id_t{1}, 120}, .lane = lane_id_t{3},
        .kind = note_kind_t::hold, .end = local_position_t{measure_id_t{1}, 260}}));
    check_move(hold_restore_rejected({.id = object_id_t{105},
        .position = {measure_id_t{1}, 300}, .lane = lane_id_t{3},
        .kind = note_kind_t::hold, .end = local_position_t{measure_id_t{1}, 400}}));
    check_move(hold_restore_rejected({.id = object_id_t{106},
        .position = {measure_id_t{1}, 100}, .lane = lane_id_t{3},
        .kind = note_kind_t::hold, .end = local_position_t{measure_id_t{2}, 200}}));
    auto song = plain_song();
    song.charts.front().lane_layouts.front().layout.lanes.push_back(
        {.id = lane_id_t{3}, .name = "C"});
    song.charts.front().measures.front().bgm_events = {
        {.id = object_id_t{110}, .position = {measure_id_t{1}, 240},
         .source_lane = object_id_t{9}, .end = local_position_t{measure_id_t{1}, 500}},
        {.id = object_id_t{111}, .position = {measure_id_t{1}, 400},
         .source_lane = object_id_t{9}, .end = local_position_t{measure_id_t{1}, 600}}};
    auto history = document_history_t::from(song);
    const auto before = history.document;
    const std::array both{object_id_t{110}, object_id_t{111}};
    check_move(!move_bgm_events_to_lane(history, history.document.revision, chart_id_t{1},
                                        both, lane_id_t{3}).applied);
    check_move(same_native_document(history.document, before));
    check_move(history.undo.empty());
}

TEST_CASE("restoring a legacy BGM tap keeps its head-only occupancy rule",
          "[unit][core][note-to-bgm][bgm-to-lane]") {
    auto song = plain_song();
    auto& chart = song.charts.front();
    chart.lane_layouts.front().layout.lanes.push_back({.id = lane_id_t{3}, .name = "C"});
    chart.measures.front().notes.push_back({.id = object_id_t{120},
        .position = {measure_id_t{1}, 240}, .lane = lane_id_t{3},
        .kind = note_kind_t::hold, .end = local_position_t{measure_id_t{1}, 400}});
    chart.measures.front().bgm_events = {{.id = object_id_t{121},
        .position = {measure_id_t{1}, 300}, .source_lane = object_id_t{9}}};
    auto history = document_history_t::from(song);
    const std::array selected{object_id_t{121}};
    check_move(move_bgm_events_to_lane(history, history.document.revision, chart_id_t{1},
                                       selected, lane_id_t{3}).applied);
}

TEST_CASE("moving an imported BMS note writes only its BGM occurrence",
          "[unit][core][note-to-bgm][bms-writer]") {
    const std::string source = "#TITLE allocator\n#PLAYER 1\n#BPM 120\n#WAV01 kick.wav\n#00011:01\n";
    const auto parsed = parse_bms_bytes(std::as_bytes(std::span{source.data(), source.size()}),
        {.lane_profile_id = "bms-5k-outside-scratch"});
    require_move(parsed.song.has_value());
    auto history = document_history_t::from(*parsed.song);
    const auto chart_id = history.document.charts.front().id;
    const auto& chart_before = history.document.charts.front();
    require_move(!chart_before.measures.empty());
    require_move(!chart_before.measures.front().notes.empty());
    const auto note_id = chart_before.measures.front().notes.front().id;
    const std::array selected{note_id};
    const auto moved = move_notes_to_bgm(history, history.document.revision,
                                         chart_id, selected, std::nullopt);
    require_move(moved.applied);
    const auto written = write_bms(history.document,
        {.lane_profile_id = "bms-5k-outside-scratch", .preserve_unchanged_source = true});
    require_move(written.bytes.has_value());
    const auto reparsed = parse_bms_bytes(std::as_bytes(std::span{written.bytes->data(), written.bytes->size()}),
        {.lane_profile_id = "bms-5k-outside-scratch"});
    require_move(reparsed.song.has_value());
    const auto& roundtrip = reparsed.song->charts.front();
    std::size_t note_count{};
    std::size_t event_count{};
    for (const auto& measure : roundtrip.measures) {
        note_count += measure.notes.size();
        event_count += measure.bgm_events.size();
    }
    check_move(note_count == 0);
    check_move(event_count == 1);
}

TEST_CASE("sparse BGM plan handles many selected notes in one lane",
          "[unit][core][note-to-bgm]") {
    auto song = plain_song();
    auto& notes = song.charts.front().measures[0].notes;
    notes.clear();
    std::vector<object_id_t> selected;
    for (std::uint64_t i = 0; i < 512; ++i) {
        const object_id_t id{1000 + i};
        selected.push_back(id);
        notes.push_back({.id = id, .position = {measure_id_t{1}, i}, .lane = lane_id_t{1}});
    }
    const auto& chart = song.charts.front();
    const auto plan = plan_notes_to_bgm(chart, song.resources, selected, std::nullopt);
    require_move(plan.accepted());
    check_move(plan.assignments.size() == selected.size());
    check_move(std::ranges::all_of(plan.assignments, [](const auto& item) {
        return item.lane == object_id_t{9};
    }));
}

TEST_CASE("notes moved to the BGM area take the preferred lane and shift over when it is taken",
          "[unit][core][note-to-bgm]") {
    auto history = document_history_t::from(plain_song());
    const std::array both{object_id_t{1}, object_id_t{2}};
    const auto moved = move_notes_to_bgm(history, history.document.revision, chart_id_t{1}, both, object_id_t{9});
    for (const auto& d : moved.diagnostics) WARN(d.message);
    REQUIRE(moved.applied);
    const auto& chart = history.document.charts.front();
    CHECK(chart.measures[0].notes.empty());
    REQUIRE(chart.measures[0].bgm_events.size() == 2);
    // Same row: the first takes BGM 1, the second shifts to BGM 2.
    CHECK(chart.measures[0].bgm_events[0].source_lane == object_id_t{9});
    CHECK(chart.measures[0].bgm_events[1].source_lane == object_id_t{10});
    CHECK(history.undo.size() == 1);
    REQUIRE(undo_document_command(history));
    CHECK(history.document.charts.front().measures[0].notes.size() == 2);
}

TEST_CASE("a third note at the same row makes a new BGM lane; unknown notes reject atomically",
          "[unit][core][note-to-bgm]") {
    auto song = plain_song();
    song.charts.front().measures[0].notes.push_back(
        {.id = object_id_t{4}, .position = {measure_id_t{1}, 240}, .lane = lane_id_t{1}});
    auto history = document_history_t::from(song);
    const std::array three{object_id_t{1}, object_id_t{2}, object_id_t{4}};
    REQUIRE(move_notes_to_bgm(history, history.document.revision, chart_id_t{1}, three, std::nullopt).applied);
    CHECK(history.document.charts.front().bgm_source_lanes.size() == 3);

    auto other = document_history_t::from(plain_song());
    const std::array bad{object_id_t{1}, object_id_t{999}};
    const auto revision = other.document.revision;
    CHECK_FALSE(move_notes_to_bgm(other, revision, chart_id_t{1}, bad, std::nullopt).applied);
    CHECK(other.document.revision == revision);
    CHECK(other.document.charts.front().measures[0].notes.size() == 2);
    CHECK_FALSE(move_notes_to_bgm(other, revision + 5, chart_id_t{1}, std::array{object_id_t{1}}, std::nullopt).applied);
}

TEST_CASE("a BMSON stem tap goes back to its stem's BGM lane and keeps its slice point",
          "[unit][core][note-to-bgm][bmson]") {
    std::ifstream input(std::filesystem::path(WORMEDIT_FIXTURE_DIR) / "bmson" / "published-vox-slicing.bmson",
                        std::ios::binary);
    const std::string bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    const auto parsed = parse_bmson_bytes(std::as_bytes(std::span{bytes.data(), bytes.size()}));
    REQUIRE(parsed.song.has_value());
    auto song = *parsed.song;
    song.audio_assets.front().stream = {48'000, 2, 200'000, true};
    auto history = document_history_t::from(std::move(song));
    const auto chart_id = history.document.charts.front().id;
    const auto& chart = history.document.charts.front();
    const auto measure_it = std::ranges::find_if(chart.measures, [](const auto& m) { return !m.bgm_events.empty(); });
    REQUIRE(measure_it != chart.measures.end());
    const auto measure_index = static_cast<std::size_t>(measure_it - chart.measures.begin());
    const auto bgm = measure_it->bgm_events.front();
    const auto lane = chart.lane_layouts.front().layout.lanes.front().id;
    REQUIRE(assign_bmson_sound_event(history, history.document.revision, chart_id, bgm.id, lane).applied);
    const auto undo_before = history.undo.size();

    const std::array note{bgm.id};
    const auto back = move_notes_to_bgm(history, history.document.revision, chart_id, note, std::nullopt);
    REQUIRE(back.applied);
    const auto& restored = history.document.charts.front().measures[measure_index];
    REQUIRE(restored.bgm_events.size() == 1);
    CHECK(restored.bgm_events.front().sound_channel_event == bgm.sound_channel_event);
    CHECK(history.undo.size() == undo_before + 1);
}

TEST_CASE("pure ordinary planning rejects linked BMSON notes while mixed command reserves stem rows",
          "[unit][core][note-to-bgm][bmson]") {
    auto unlinked_song = plain_song();
    unlinked_song.charts.front().source_family = format_family_t::bmson;
    const std::array unlinked_ids{object_id_t{1}};
    const auto unlinked = plan_notes_to_bgm(unlinked_song.charts.front(), unlinked_song.resources,
        unlinked_ids, std::nullopt);
    require_move(unlinked.accepted());

    std::ifstream input(std::filesystem::path(WORMEDIT_FIXTURE_DIR) / "bmson" / "published-vox-slicing.bmson",
                        std::ios::binary);
    const std::string bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    const auto parsed = parse_bmson_bytes(std::as_bytes(std::span{bytes.data(), bytes.size()}));
    require_move(parsed.song.has_value());
    auto song = *parsed.song;
    song.audio_assets.front().stream = {48'000, 2, 200'000, true};
    auto& setup_chart = song.charts.front();
    const auto bgm_measure = std::ranges::find_if(setup_chart.measures,
        [](const auto& measure) { return !measure.bgm_events.empty(); });
    require_move(bgm_measure != setup_chart.measures.end());
    const auto marker = bgm_measure->bgm_events.front();
    const auto measure_index = static_cast<std::size_t>(bgm_measure - setup_chart.measures.begin());
    setup_chart.lane_layouts.front().layout.lanes.push_back({.id = lane_id_t{9999}, .name = "ordinary"});
    const auto ordinary_id = object_id_t{999999};
    setup_chart.measures[measure_index].notes.push_back({.id = ordinary_id,
        .position = marker.position, .lane = lane_id_t{9999}, .sound = marker.sound});

    auto history = document_history_t::from(std::move(song));
    auto& chart = history.document.charts.front();
    const auto chart_id = chart.id;
    const auto stem_lane = chart.lane_layouts.front().layout.lanes.front().id;
    require_move(assign_bmson_sound_event(history, history.document.revision,
                                         chart_id, marker.id, stem_lane).applied);
    const std::array mixed{marker.id, ordinary_id};
    const auto& linked_chart = history.document.charts.front();
    const auto pure = plan_notes_to_bgm(linked_chart, history.document.resources, mixed, std::nullopt);
    check_move(!pure.accepted());
    check_move(pure.diagnostics.front().code == diagnostic_code_t::unsupported_feature);

    const auto before_undo = history.undo.size();
    const auto moved = move_notes_to_bgm(history, history.document.revision, chart_id, mixed, std::nullopt);
    require_move(moved.applied);
    const auto& result_measure = history.document.charts.front().measures[measure_index];
    const auto stem = std::ranges::find(result_measure.bgm_events, marker.id, &bgm_event_t::id);
    const auto ordinary = std::ranges::find(result_measure.bgm_events, ordinary_id, &bgm_event_t::id);
    require_move(stem != result_measure.bgm_events.end());
    require_move(ordinary != result_measure.bgm_events.end());
    check_move(stem->source_lane != ordinary->source_lane);
    check_move(history.undo.size() == before_undo + 1);
}

#include <wormedit/core/sound_slice.h>

TEST_CASE("dragging a stem tap moves its slice point and leaves the audio where it is",
          "[unit][core][note-to-bgm][bmson][stem-move]") {
    std::ifstream input(std::filesystem::path(WORMEDIT_FIXTURE_DIR) / "bmson" / "published-vox-slicing.bmson",
                        std::ios::binary);
    const std::string bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    const auto parsed = parse_bmson_bytes(std::as_bytes(std::span{bytes.data(), bytes.size()}));
    REQUIRE(parsed.song.has_value());
    auto song = *parsed.song;
    song.audio_assets.front().stream = {48'000, 2, 400'000, true};
    auto history = document_history_t::from(std::move(song));
    const auto& chart = history.document.charts.front();
    const auto tap = std::ranges::find_if(chart.measures.front().notes, [](const auto& note) {
        return note.position.row == 840 && note.sound_channel_event.has_value();
    });
    REQUIRE(tap != chart.measures.front().notes.end());
    const auto note_id = tap->id;
    const auto event_id = *tap->sound_channel_event;
    const auto lane = tap->lane;
    const auto channel_before = chart.sound_channels.front();
    const auto slices_before = derive_sound_channel_slices(chart, channel_before, 48'000, 400'000);
    REQUIRE(slices_before);
    const auto before_at = std::ranges::find(slices_before.slices, 840U, &derived_sound_slice_t::absolute_row);
    REQUIRE(before_at != slices_before.slices.end());

    constexpr std::array snaps{std::uint64_t{4}, std::uint64_t{8}};
    auto setup = editor_interaction_state_t::from(history, chart.id, lane, {measure_id_t{chart.measures.front().id.value}, 840}, snaps, 4);
    REQUIRE(setup);
    auto state = std::move(*setup.state);
    REQUIRE(dispatch_editor_action(history, state, select_editor_notes_action_t{{note_id}}));
    REQUIRE(dispatch_editor_action(history, state, begin_note_move_gesture_action_t{note_id}));
    const auto next_measure = history.document.charts.front().measures[1].id;
    const auto released = dispatch_editor_action(
        history, state,
        update_note_move_gesture_action_t{.reference_position = {next_measure, 0},
                                          .reference_lane = lane,
                                          .phase = wormedit::editor_gesture_phase_t::commit});
    REQUIRE(released);
    REQUIRE(released.document_changed);

    const auto& moved_chart = history.document.charts.front();
    const auto& channel = moved_chart.sound_channels.front();
    const auto event = std::ranges::find(channel.events, event_id, &sound_channel_event_t::id);
    REQUIRE(event != channel.events.end());
    CHECK(event->position == (local_position_t{next_measure, 0}));
    const auto slices = derive_sound_channel_slices(moved_chart, channel, 48'000, 400'000);
    REQUIRE(slices);
    const auto after_at = std::ranges::find(slices.slices, 960U, &derived_sound_slice_t::absolute_row);
    REQUIRE(after_at != slices.slices.end());
    // The cut moved later, so the slice starts further into the same audio: nothing was re-anchored.
    CHECK(after_at->source_begin_frame > before_at->source_begin_frame);
    REQUIRE(undo_document_command(history));
    const auto restored = std::ranges::find(history.document.charts.front().sound_channels.front().events,
                                            event_id, &sound_channel_event_t::id);
    CHECK(restored->position.row == 840);
}

TEST_CASE("BGM events move into a playfield lane keeping their row",
          "[unit][core][note-to-bgm][bgm-to-lane]") {
    auto song = plain_song();
    auto& chart = song.charts.front();
    chart.measures[1].bgm_events = {{.id = object_id_t{50}, .position = {measure_id_t{2}, 480}, .source_lane = object_id_t{9}}};
    auto history = document_history_t::from(song);
    const std::array event{object_id_t{50}};
    const auto moved = move_bgm_events_to_lane(history, history.document.revision, chart_id_t{1}, event, lane_id_t{2});
    REQUIRE(moved.applied);
    const auto& after = history.document.charts.front().measures[1];
    CHECK(after.bgm_events.empty());
    REQUIRE(after.notes.size() == 2);
    const auto note = std::ranges::find(after.notes, object_id_t{50}, &note_t::id);
    REQUIRE(note != after.notes.end());
    CHECK(note->lane == lane_id_t{2});
    CHECK(note->position == (local_position_t{measure_id_t{2}, 480}));
    CHECK(history.undo.size() == 1);

    // A lane that already holds a note at the row rejects without changing anything.
    auto other = plain_song();
    other.charts.front().measures[0].bgm_events = {{.id = object_id_t{51}, .position = {measure_id_t{1}, 240}, .source_lane = object_id_t{9}}};
    auto blocked = document_history_t::from(other);
    const auto revision = blocked.document.revision;
    const std::array clash{object_id_t{51}};
    CHECK_FALSE(move_bgm_events_to_lane(blocked, revision, chart_id_t{1}, clash, lane_id_t{1}).applied);
    CHECK(blocked.document.revision == revision);
    CHECK_FALSE(move_bgm_events_to_lane(blocked, revision, chart_id_t{1}, clash, lane_id_t{99}).applied);
}

namespace {

auto overlapping_linked_hold_batch_is_atomic() -> bool {
    std::ifstream input(std::filesystem::path(WORMEDIT_FIXTURE_DIR) / "bmson" /
                            "published-vox-slicing.bmson", std::ios::binary);
    const std::string bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    const auto parsed = parse_bmson_bytes(std::as_bytes(std::span{bytes.data(), bytes.size()}));
    REQUIRE(parsed.song);
    auto song = *parsed.song;
    auto& chart = song.charts.front();
    const auto lane = std::ranges::find_if(chart.lane_layouts.front().layout.lanes,
        [&](const auto& candidate) {
            return candidate.role != lane_role_t::autoplay &&
                candidate.role != lane_role_t::invisible && candidate.role != lane_role_t::mine &&
                std::ranges::none_of(chart.measures, [&](const auto& measure) {
                return std::ranges::any_of(measure.notes, [&](const auto& note) {
                    const auto begin = note.position.row;
                    const auto end = note.end ? note.end->row : begin;
                    return note.lane == candidate.id && note.position.measure ==
                           chart.measures.front().id && begin <= 720 && end >= 120;
                });
            });
        });
    REQUIRE(lane != chart.lane_layouts.front().layout.lanes.end());
    const auto lane_id = lane->id;
    const auto source_lane = chart.bgm_source_lanes.front().id;
    const auto source_channel = chart.sound_channels.front().id;
    const auto exemplar = chart.sound_channels.front().events.front();
    const auto linked_resource = std::ranges::find(song.resources,
        chart.sound_channels.front().source_clip.source, &resource_entry_t::asset);
    require_move(linked_resource != song.resources.end());
    const std::array ids{object_id_t{900'101}, object_id_t{900'102}};
    const std::array source_ids{object_id_t{900'201}, object_id_t{900'202}};
    const std::array positions{local_position_t{chart.measures.front().id, 120},
                               local_position_t{chart.measures.front().id, 180}};
    const std::array ends{local_position_t{chart.measures.front().id, 600},
                          local_position_t{chart.measures.front().id, 720}};
    for (std::size_t index = 0; index < ids.size(); ++index) {
        chart.sound_channels.front().events.push_back({.id = source_ids[index], .position = positions[index],
            .end = ends[index], .lane = {}, .continuation = false, .playable = false,
            .provenance = exemplar.provenance});
        chart.measures.front().bgm_events.push_back({.id = ids[index], .position = positions[index],
            .source_lane = source_lane, .sound = linked_resource->id,
            .sound_channel_event = source_ids[index], .end = ends[index]});
    }
    const auto chart_id = chart.id;
    const auto channel_id = source_channel;
    auto history = document_history_t::from(std::move(song));
    require_move(validate_document(history.document).empty());
    for (const auto id : ids) {
        auto single_history = document_history_t::from(history.document);
        const std::array one_event{id};
        const auto single = move_bgm_events_to_lane(single_history,
            single_history.document.revision, chart_id, one_event, lane_id);
        require_move(single.applied);
    }
    const auto before = write_native_project(history.document);
    require_move(before.bytes.has_value());
    const auto revision = history.document.revision;
    const auto result = move_bgm_events_to_lane(history, revision, chart_id, ids, lane_id);
    check_move(!result.applied && !result.diagnostics.empty() &&
               result.diagnostics.front().code == diagnostic_code_t::collision);
    check_move(history.document.revision == revision);
    check_move(history.undo.empty());
    const auto after = write_native_project(history.document);
    require_move(after.bytes.has_value());
    check_move(*after.bytes == *before.bytes);
    const auto& retained = history.document.charts.front();
    check_move(retained.sound_channels.front().id == channel_id);
    check_move(std::ranges::all_of(ids, [&](const auto id) {
        return std::ranges::any_of(retained.measures.front().bgm_events,
            [&](const auto& event) { return event.id == id && event.end.has_value(); });
    }));
    return true;
}

} // namespace

TEST_CASE("overlapping linked BMSON hold batch leaves the live document unchanged",
          "[unit][core][note-to-bgm][bgm-to-lane][bmson][hold]") {
    require_move(overlapping_linked_hold_batch_is_atomic());
}

TEST_CASE("Shift lane assignment moves mixed selected objects one at a time in document order",
          "[unit][core][note-to-bgm][bgm-to-lane][successive]") {
    auto song = plain_song();
    auto& chart = song.charts.front();
    chart.lane_layouts.front().layout.lanes.push_back({.id = lane_id_t{3}, .name = "C"});
    chart.measures[0].bgm_events = {
        {.id = object_id_t{50}, .position = {measure_id_t{1}, 300}, .source_lane = object_id_t{9}}};
    auto history = document_history_t::from(song);
    editor_interaction_state_t state;
    state.chart = chart_id_t{1};
    state.cursor = {measure_id_t{1}, 0};
    state.active_lane = lane_id_t{1};
    state.active_snap_denominator = 4;
    state.supported_snap_denominators = {4};
    state.selected_notes = {object_id_t{1}, object_id_t{3}};
    state.selected_objects = {{editor_selection_object_kind_t::bgm_event, object_id_t{50}},
                              {editor_selection_object_kind_t::bgm_event, object_id_t{51}},
                              {editor_selection_object_kind_t::bgm_event, object_id_t{52}},
                              {editor_selection_object_kind_t::note, object_id_t{1}},
                              {editor_selection_object_kind_t::note, object_id_t{3}}};
    const auto move_next = [&] {
        return sys::move_selected_bgm_to_lane(history, state, lane_id_t{3}, std::nullopt, true);
    };

    auto moved = move_next();
    require_move(moved.command.applied);
    check_move(state.selected_notes == std::vector<object_id_t>{object_id_t{3}});
    check_move(std::ranges::none_of(state.selected_objects, [](const auto& object) {
        return object.id == object_id_t{1};
    }));
    require_move(move_next().command.applied);
    check_move(std::ranges::none_of(state.selected_objects, [](const auto& object) {
        return object.kind == editor_selection_object_kind_t::bgm_event &&
               object.id == object_id_t{50};
    }));
    require_move(move_next().command.applied);
    check_move(state.selected_notes.empty());
    require_move(history.undo.size() == 3);
    require_move(undo_document_command(history));
    check_move(history.document.charts.front().measures[0].bgm_events.empty());

    auto collision_song = plain_song();
    collision_song.charts.front().measures[0].bgm_events = {
        {.id = object_id_t{60}, .position = {measure_id_t{1}, 240}, .source_lane = object_id_t{9}}};
    auto collision_history = document_history_t::from(std::move(collision_song));
    editor_interaction_state_t collision_state;
    collision_state.chart = chart_id_t{1};
    collision_state.selected_objects = {
        {editor_selection_object_kind_t::bgm_event, object_id_t{60}}};
    const auto rejected = sys::move_selected_bgm_to_lane(
        collision_history, collision_state, lane_id_t{1}, std::nullopt, true);
    check_move(!rejected.command.applied);
    check_move(collision_state.selected_objects.size() == 1);
    check_move(collision_history.undo.empty());
}

TEST_CASE("three selected BGM markers move on successive presses with one undo each",
          "[unit][core][note-to-bgm][bgm-to-lane][successive]") {
    auto song = plain_song();
    song.charts.front().measures[0].bgm_events = {
        {.id = object_id_t{70}, .position = {measure_id_t{1}, 40}, .source_lane = object_id_t{9}},
        {.id = object_id_t{71}, .position = {measure_id_t{1}, 80}, .source_lane = object_id_t{9}},
        {.id = object_id_t{72}, .position = {measure_id_t{1}, 120}, .source_lane = object_id_t{9}}};
    auto history = document_history_t::from(song);
    editor_interaction_state_t state;
    state.chart = chart_id_t{1};
    state.cursor = {measure_id_t{1}, 0};
    state.active_lane = lane_id_t{1};
    state.active_snap_denominator = 4;
    state.supported_snap_denominators = {4};
    state.selected_objects = {{editor_selection_object_kind_t::bgm_event, object_id_t{70}},
                              {editor_selection_object_kind_t::bgm_event, object_id_t{71}},
                              {editor_selection_object_kind_t::bgm_event, object_id_t{72}}};
    const std::array expected_ids{object_id_t{70}, object_id_t{71}, object_id_t{72}};
    for (std::size_t index = 0; index < expected_ids.size(); ++index) {
        const auto moved = sys::move_selected_bgm_to_lane(
            history, state, lane_id_t{2}, std::nullopt, true);
        require_move(moved.command.applied);
        check_move(std::ranges::none_of(state.selected_objects, [&](const auto& object) {
            return object.id == expected_ids[index];
        }));
        check_move(history.undo.size() == index + 1);
    }
    check_move(state.selected_objects.empty());
    require_move(history.undo.size() == 3);
    require_move(undo_document_command(history));
    check_move(history.document.charts.front().measures[0].bgm_events.size() == 1);
    check_move(history.document.charts.front().measures[0].bgm_events.front().id == expected_ids.back());
}

TEST_CASE("plain lane assignment moves a mixed selection in one undo and rejects atomically",
          "[unit][core][note-to-bgm][bgm-to-lane]") {
    auto song = plain_song();
    song.charts.front().lane_layouts.front().layout.lanes.push_back(
        {.id = lane_id_t{3}, .name = "C"});
    song.charts.front().measures[0].bgm_events = {
        {.id = object_id_t{80}, .position = {measure_id_t{1}, 300}, .source_lane = object_id_t{9}}};
    auto history = document_history_t::from(song);
    editor_interaction_state_t state;
    state.chart = chart_id_t{1};
    state.cursor = {measure_id_t{1}, 0};
    state.active_lane = lane_id_t{1};
    state.active_snap_denominator = 4;
    state.supported_snap_denominators = {4};
    state.selected_notes = {object_id_t{1}};
    state.selected_objects = {{editor_selection_object_kind_t::note, object_id_t{1}},
                              {editor_selection_object_kind_t::bgm_event, object_id_t{80}}};
    const std::array notes{object_id_t{1}};
    const std::array events{object_id_t{80}};
    const auto moved = move_selected_objects_to_lane(
        history, history.document.revision, state, notes, events, lane_id_t{3});
    require_move(moved.applied);
    check_move(history.undo.size() == 1);
    const auto& chart = history.document.charts.front();
    check_move(std::ranges::find(chart.measures[0].notes, object_id_t{1}, &note_t::id)->lane == lane_id_t{3});
    check_move(std::ranges::find(chart.measures[0].notes, object_id_t{80}, &note_t::id)->lane == lane_id_t{3});

    auto colliding_song = plain_song();
    colliding_song.charts.front().lane_layouts.front().layout.lanes.push_back(
        {.id = lane_id_t{3}, .name = "C"});
    colliding_song.charts.front().measures[0].bgm_events = {
        {.id = object_id_t{81}, .position = {measure_id_t{1}, 240}, .source_lane = object_id_t{9}}};
    auto blocked = document_history_t::from(colliding_song);
    const auto before = blocked.document;
    state.selected_objects = {{editor_selection_object_kind_t::note, object_id_t{1}},
                              {editor_selection_object_kind_t::bgm_event, object_id_t{81}}};
    const std::array collision_events{object_id_t{81}};
    const auto rejected = move_selected_objects_to_lane(
        blocked, blocked.document.revision, state, notes, collision_events, lane_id_t{3});
    check_move(!rejected.applied);
    check_move(blocked.document.revision == before.revision);
    check_move(blocked.document.charts.front().measures[0].notes[0].lane ==
          before.charts.front().measures[0].notes[0].lane);
    check_move(blocked.document.charts.front().measures[0].bgm_events.size() == 1);
    check_move(blocked.undo.empty());
}

TEST_CASE("a BMSON stem marker moves to a playfield lane and back without losing its slice point",
          "[unit][core][note-to-bgm][bgm-to-lane][bmson]") {
    std::ifstream input(std::filesystem::path(WORMEDIT_FIXTURE_DIR) / "bmson" / "published-vox-slicing.bmson",
                        std::ios::binary);
    const std::string bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    const auto parsed = parse_bmson_bytes(std::as_bytes(std::span{bytes.data(), bytes.size()}));
    REQUIRE(parsed.song.has_value());
    auto song = *parsed.song;
    song.audio_assets.front().stream = {48'000, 2, 200'000, true};
    auto history = document_history_t::from(std::move(song));
    const auto& chart = history.document.charts.front();
    const auto measure = std::ranges::find_if(chart.measures, [](const auto& m) { return !m.bgm_events.empty(); });
    REQUIRE(measure != chart.measures.end());
    const auto marker = measure->bgm_events.front();
    const auto lane = chart.lane_layouts.front().layout.lanes.front().id;
    const std::array events{marker.id};
    REQUIRE(move_bgm_events_to_lane(history, history.document.revision, chart.id, events, lane).applied);
    const auto& playable = history.document.charts.front();
    const auto note = std::ranges::find_if(playable.measures, [&](const auto& m) {
        return std::ranges::any_of(m.notes, [&](const auto& n) { return n.id == marker.id; });
    });
    REQUIRE(note != playable.measures.end());
    const auto tap = std::ranges::find(note->notes, marker.id, &note_t::id);
    CHECK(tap->sound_channel_event == marker.sound_channel_event);
    CHECK(tap->lane == lane);
}

TEST_CASE("Core Move to BGM owns mixed selection and successive document order",
          "[unit][core][note-to-bgm][selection-to-bgm]") {
    const auto successive = GENERATE(false, true);
    auto song = plain_song();
    song.charts.front().measures.front().bgm_events.push_back({.id = object_id_t{50},
        .position = {measure_id_t{1}, 0}, .source_lane = object_id_t{9}});
    auto history = document_history_t::from(std::move(song));
    const auto before = write_native_project(history.document).bytes;
    editor_interaction_state_t state;
    state.chart = chart_id_t{1};
    state.selected_notes = {object_id_t{3}, object_id_t{2}, object_id_t{1}};
    state.selected_objects = {{editor_selection_object_kind_t::bgm_event, object_id_t{50}},
        {editor_selection_object_kind_t::note, object_id_t{3}},
        {editor_selection_object_kind_t::note, object_id_t{2}},
        {editor_selection_object_kind_t::note, object_id_t{1}}};
    const auto result = move_editor_selection_to_bgm(history, history.document.revision,
                                                     state, std::nullopt, successive);
    require_move(result.payload.handled && result.command.applied);
    check_move(history.undo.size() == 1);
    check_move(state.selected_objects.front() == editor_selection_object_t{editor_selection_object_kind_t::bgm_event, object_id_t{50}});
    check_move(history.document.charts.front().measures.front().bgm_events.front().id == object_id_t{50});
    check_move(history.document.charts.front().measures.front().bgm_events.front().position.row == 0);
    if (successive) {
        check_move(state.selected_notes == std::vector{object_id_t{3}, object_id_t{2}});
        check_move(state.selected_objects.size() == 3);
        check_move(history.document.charts.front().measures.front().notes.front().id == object_id_t{2});
    } else {
        check_move(state.selected_notes.empty() && state.selected_objects.size() == 4);
        check_move(history.document.charts.front().measures.front().notes.empty());
        check_move(history.document.charts.front().measures.back().notes.empty());
    }
    require_move(undo_document_command(history));
    check_move(write_native_project(history.document).bytes == before);
}

TEST_CASE("Core Move to BGM rejects missing notes stale input and active gestures atomically",
          "[unit][core][note-to-bgm][selection-to-bgm]") {
    const auto rejected_kind = GENERATE(0, 1, 2);
    auto history = document_history_t::from(plain_song());
    const auto before = write_native_project(history.document).bytes;
    editor_interaction_state_t state;
    state.chart = chart_id_t{1};
    state.selected_notes = {object_id_t{1}, object_id_t{2}};
    if (rejected_kind == 0) state.selected_notes.push_back(object_id_t{999});
    if (rejected_kind == 2) state.pending_note_move_gesture.emplace();
    const auto selection = state.selected_notes;
    const auto revision = history.document.revision - (rejected_kind == 1 ? 1 : 0);
    const auto result = move_editor_selection_to_bgm(history, revision, state, std::nullopt, true);
    require_move(result.payload.handled && !result.command.applied && !result.command.diagnostics.empty());
    check_move(history.undo.empty() && state.selected_notes == selection);
    check_move(write_native_project(history.document).bytes == before);
}
