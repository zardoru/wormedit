#include "../support/stale_action_assertions.h"
#include <wormedit/core/lane_migration.h>
#include <wormedit/formats/bmson.h>
#include <wormedit/formats/bms.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace wormedit;

auto ordered(const lane_layout_t& layout) -> std::vector<lane_definition_t> {
    auto lanes = layout.lanes;
    std::ranges::stable_sort(lanes, {}, &lane_definition_t::display_order);
    return lanes;
}

auto migration_song() -> song_document_t {
    const auto profile = make_osu_mania_profile(7);
    REQUIRE(profile);
    song_document_t song{
        .id = song_id_t{1},
        .revision = 5,
        .charts = {{
            .id = chart_id_t{2},
            .measures = {{.id = measure_id_t{3}}, {.id = measure_id_t{4}}},
            .lane_layouts = {{.id = object_id_t{21},
                              .position = {measure_id_t{3}, 0},
                              .layout = profile->layout},
                             {.id = object_id_t{22},
                              .position = {measure_id_t{4}, 0},
                              .layout = profile->layout}},
            .input_profile = profile->input,
            .bpms = {{.id = object_id_t{23},
                      .position = {measure_id_t{3}, 0},
                      .beats_per_minute = {120, 1}}},
        }},
    };
    REQUIRE(validate_document(song).empty());
    return song;
}

auto add_note(chart_document_t& chart, object_id_t id, lane_id_t lane,
              local_position_t position, std::optional<local_position_t> end = std::nullopt)
    -> void {
    chart.measures[position.measure == measure_id_t{3} ? 0 : 1].notes.push_back({
        .id = id, .position = position, .lane = lane,
        .kind = end ? note_kind_t::hold : note_kind_t::tap, .end = end});
}

auto explicit_merge(const lane_migration_proposal_t& defaults, lane_id_t from,
                    lane_id_t to) -> std::vector<lane_migration_mapping_t> {
    auto mapping = defaults.mapping;
    const auto found = std::ranges::find(mapping, from, &lane_migration_mapping_t::source_lane);
    REQUIRE(found != mapping.end());
    found->target_lane = to;
    return mapping;
}

auto parse_bmson_fixture(std::string_view name) -> bmson_parse_outcome_t {
    std::ifstream stream(std::filesystem::path(WORMEDIT_FIXTURE_DIR) / "bmson" / name,
                         std::ios::binary);
    const std::string source{std::istreambuf_iterator<char>(stream),
                             std::istreambuf_iterator<char>()};
    std::vector<std::byte> bytes;
    bytes.reserve(source.size());
    for (const auto ch : source)
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
    return parse_bmson_bytes(bytes);
}

auto profile_with_new_lane_ids(const chart_document_t& chart, std::uint64_t offset)
    -> lane_input_profile_t {
    auto profile = lane_input_profile_t{.layout = chart.lane_layouts.front().layout,
                                        .input = chart.input_profile};
    profile.layout.profile_id += "-migration-test";
    profile.input.profile_id += "-migration-test";
    for (auto& lane : profile.layout.lanes) lane.id.value += offset;
    for (auto& binding : profile.input.bindings) binding.lane.value += offset;
    return profile;
}

auto rank_mapping(const chart_document_t& chart, const lane_layout_t& target,
                  std::optional<lane_id_t> dropped = {},
                  std::optional<std::pair<lane_id_t, lane_id_t>> merged = {})
    -> std::vector<lane_migration_mapping_t> {
    auto source = chart.lane_layouts.front().layout.lanes;
    auto destinations = target.lanes;
    std::ranges::stable_sort(source, {}, &lane_definition_t::display_order);
    std::ranges::stable_sort(destinations, {}, &lane_definition_t::display_order);
    std::vector<lane_migration_mapping_t> result;
    result.reserve(source.size());
    for (std::size_t i = 0; i < source.size(); ++i) {
        auto destination = std::optional{destinations[i].id};
        if (dropped == source[i].id) destination.reset();
        if (merged && merged->first == source[i].id) destination = merged->second;
        result.push_back({source[i].id, destination});
    }
    return result;
}

auto find_linked_note(chart_document_t& chart) -> note_t* {
    for (auto& measure : chart.measures)
        for (auto& note : measure.notes)
            if (note.sound_channel_event) return &note;
    return nullptr;
}

auto find_retained_event(chart_document_t& chart, object_id_t id) -> sound_channel_event_t* {
    for (auto& channel : chart.sound_channels)
        for (auto& event : channel.events)
            if (event.id == id) return &event;
    return nullptr;
}

auto find_retained_event(const chart_document_t& chart, object_id_t id)
    -> const sound_channel_event_t* {
    for (const auto& channel : chart.sound_channels)
        for (const auto& event : channel.events)
            if (event.id == id) return &event;
    return nullptr;
}


void check_default_drop_preview(const lane_migration_proposal_t& preview) {
    CHECK(preview.dropped_notes == std::vector<object_id_t>{object_id_t{32}, object_id_t{33}});
    CHECK(preview.mapped_note_count == 1);
    CHECK(preview.remapped_note_count == 0);
    CHECK(preview.changed_bindings == 2);
}

void check_drop_requires_confirmation(document_history_t& history,
                                      const lane_migration_proposal_t& preview) {
    CHECK(apply_lane_migration(history, preview, false).payload.issue ==
          lane_migration_issue_t::drop_confirmation_required);
    CHECK(history.document.charts.front().measures[0].notes.size() == 2);
}

void check_migrated_segments(const chart_document_t& changed,
                             const lane_input_profile_t& target) {
    CHECK(changed.measures[0].notes.size() == 1);
    CHECK(changed.measures[1].notes.empty());
    CHECK(changed.input_profile.profile_id == target.input.profile_id);
    CHECK(changed.lane_layouts[0].layout.profile_id == target.layout.profile_id);
    CHECK(changed.lane_layouts[1].layout.profile_id == target.layout.profile_id);
}

void check_drop_undo(document_history_t& history) {
    CHECK(undo_document_command(history));
    CHECK(history.document.charts.front().measures[0].notes.size() == 2);
    CHECK(history.document.charts.front().measures[1].notes.size() == 1);
}

void check_noncolliding_merge_preview(const lane_migration_proposal_t& preview) {
    CHECK(preview.collision_count == 0);
    CHECK(preview.collisions.empty());
    CHECK(preview.mapped_note_count == 2);
    CHECK(preview.notes_in_merged_lanes == 2);
}

void check_merged_notes(const chart_document_t& chart, lane_id_t lane) {
    const auto& notes = chart.measures.front().notes;
    REQUIRE(notes.size() == 2);
    CHECK(notes[0].lane == lane);
    CHECK(notes[1].lane == lane);
}

void check_hold_tail_witness(const lane_migration_proposal_t& preview) {
    CHECK(preview.collision_count == 1);
    REQUIRE(preview.collisions.size() == 1);
    CHECK(preview.collisions.front().first_note == object_id_t{51});
    CHECK(preview.collisions.front().second_note == object_id_t{52});
}

auto require_note(const chart_document_t& chart, object_id_t id) -> const note_t& {
    const auto measure = std::ranges::find_if(chart.measures, [id](const auto& item) {
        return std::ranges::any_of(item.notes, [id](const auto& candidate) {
            return candidate.id == id;
        });
    });
    REQUIRE(measure != chart.measures.end());
    const auto note = std::ranges::find(measure->notes, id, &note_t::id);
    REQUIRE(note != measure->notes.end());
    return *note;
}

void check_no_linked_drops(const lane_migration_proposal_t& preview) {
    CHECK(preview.dropped_notes.empty());
    CHECK(preview.dropped_sound_channel_events.empty());
}

void check_linked_pair(const chart_document_t& chart, const note_t& note,
                       object_id_t event_id, lane_id_t old_lane) {
    CHECK(note.lane.value == old_lane.value + 10'000);
    const auto* event = find_retained_event(chart, event_id);
    REQUIRE(event != nullptr);
    CHECK(event->lane == note.lane);
    CHECK(event->position == note.position);
    CHECK(event->end == note.end);
}

void check_linked_drop_preview(const lane_migration_proposal_t& preview,
                               object_id_t note_id, object_id_t event_id) {
    CHECK(std::ranges::find(preview.dropped_notes, note_id) != preview.dropped_notes.end());
    CHECK(std::ranges::find(preview.dropped_sound_channel_events, event_id) !=
          preview.dropped_sound_channel_events.end());
}

void check_linked_pair_removed(const chart_document_t& chart, object_id_t note_id,
                               object_id_t event_id) {
    CHECK(std::ranges::none_of(chart.measures, [note_id](const auto& measure) {
        return std::ranges::any_of(measure.notes, [note_id](const auto& item) {
            return item.id == note_id;
        });
    }));
    CHECK(find_retained_event(chart, event_id) == nullptr);
}

struct retained_collision_fixture_t {
    object_id_t note;
    lane_id_t note_lane;
    lane_id_t event_lane;
    object_id_t event;
};

auto add_unlinked_collision_event(chart_document_t& chart) -> retained_collision_fixture_t {
    const auto* note = find_linked_note(chart);
    REQUIRE(note != nullptr);
    REQUIRE(!chart.sound_channels.empty());
    const auto* linked = find_retained_event(chart, *note->sound_channel_event);
    REQUIRE(linked != nullptr);
    const auto& lanes = chart.lane_layouts.front().layout.lanes;
    const auto other = std::ranges::find_if(lanes, [note](const auto& lane) {
        return lane.id != note->lane;
    });
    REQUIRE(other != lanes.end());
    auto event = *linked;
    event.id = object_id_t{900'000'001};
    event.lane = other->id;
    event.position = note->position;
    event.end = note->end;
    const auto fixture = retained_collision_fixture_t{note->id, note->lane, event.lane, event.id};
    chart.sound_channels.front().events.push_back(event);
    return fixture;
}

auto has_retained_collision_witness(const lane_migration_proposal_t& preview,
                                    const retained_collision_fixture_t& fixture) -> bool {
    return std::ranges::any_of(preview.collisions, [&](const auto& collision) {
        return (collision.first_note == fixture.note && collision.second_note == fixture.event &&
                collision.second_is_sound_channel_event) ||
               (collision.second_note == fixture.note && collision.first_note == fixture.event &&
                collision.first_is_sound_channel_event);
    });
}

void check_retained_collision_rejected(document_history_t& history,
                                       const lane_migration_proposal_t& preview,
                                       const retained_collision_fixture_t& fixture) {
    CHECK(preview.collision_count > 0);
    CHECK(has_retained_collision_witness(preview, fixture));
    CHECK(apply_lane_migration(history, preview, false).payload.issue ==
          lane_migration_issue_t::lane_collision);
}

auto require_branch_hold(const chart_document_t& chart) -> const note_t& {
    const auto hold = std::ranges::find_if(chart.measures.front().notes, [](const auto& note) {
        return note.kind == note_kind_t::hold;
    });
    REQUIRE(hold != chart.measures.front().notes.end());
    REQUIRE(!chart.active_branch_selection.empty());
    return *hold;
}

} // namespace

TEST_CASE("lane migration previews drops and changes all segments in one undoable command",
          "[unit][core][lane-migration]") {
    auto song = migration_song();
    const auto source = ordered(song.charts.front().lane_layouts.front().layout);
    const auto target = make_osu_mania_profile(5);
    REQUIRE(target);
    add_note(song.charts.front(), object_id_t{31}, source[0].id, {measure_id_t{3}, 0});
    add_note(song.charts.front(), object_id_t{32}, source[5].id, {measure_id_t{3}, 4});
    add_note(song.charts.front(), object_id_t{33}, source[6].id, {measure_id_t{4}, 8});
    REQUIRE(validate_document(song).empty());
    auto history = document_history_t::from(song);

    const auto preview = propose_lane_migration(history.document, 5, chart_id_t{2}, *target);
    REQUIRE(preview);
    check_default_drop_preview(preview);
    check_drop_requires_confirmation(history, preview);

    const auto applied = apply_lane_migration(history, preview, true);
    REQUIRE(applied);
    check_migrated_segments(history.document.charts.front(), *target);
    check_drop_undo(history);
}

TEST_CASE("lane migration reports and applies a noncolliding many-to-one mapping",
          "[unit][core][lane-migration]") {
    auto song = migration_song();
    const auto source = ordered(song.charts.front().lane_layouts.front().layout);
    const auto target = make_osu_mania_profile(5);
    REQUIRE(target);
    const auto target_lanes = ordered(target->layout);
    add_note(song.charts.front(), object_id_t{41}, source[0].id, {measure_id_t{3}, 0});
    add_note(song.charts.front(), object_id_t{42}, source[1].id, {measure_id_t{3}, 8});
    auto defaults = propose_lane_migration(song, 5, chart_id_t{2}, *target);
    REQUIRE(defaults);
    auto mapping = explicit_merge(defaults, source[1].id, target_lanes[0].id);
    const auto preview = propose_lane_migration(song, 5, chart_id_t{2}, *target, mapping);
    REQUIRE(preview);
    check_noncolliding_merge_preview(preview);
    auto history = document_history_t::from(song);
    REQUIRE(apply_lane_migration(history, preview, false));
    check_merged_notes(history.document.charts.front(), target_lanes[0].id);
}

TEST_CASE("lane migration rejects inclusive hold-tail collisions created by a lane merge",
          "[unit][core][lane-migration]") {
    auto song = migration_song();
    const auto source = ordered(song.charts.front().lane_layouts.front().layout);
    const auto target = make_osu_mania_profile(5);
    REQUIRE(target);
    const auto target_lanes = ordered(target->layout);
    add_note(song.charts.front(), object_id_t{51}, source[0].id,
             {measure_id_t{3}, 0}, local_position_t{measure_id_t{3}, 8});
    add_note(song.charts.front(), object_id_t{52}, source[1].id, {measure_id_t{3}, 8});
    const auto defaults = propose_lane_migration(song, 5, chart_id_t{2}, *target);
    REQUIRE(defaults);
    const auto mapping = explicit_merge(defaults, source[1].id, target_lanes[0].id);
    const auto preview = propose_lane_migration(song, 5, chart_id_t{2}, *target, mapping);
    REQUIRE(preview);
    check_hold_tail_witness(preview);
    auto history = document_history_t::from(song);
    CHECK(apply_lane_migration(history, preview, false).payload.issue == lane_migration_issue_t::lane_collision);
    CHECK(history.document.charts.front().measures.front().notes.size() == 2);
}

TEST_CASE("lane migration rejects modified previews and another song at matching revision",
          "[unit][core][lane-migration]") {
    auto song = migration_song();
    const auto target = make_osu_mania_profile(5);
    REQUIRE(target);
    auto preview = propose_lane_migration(song, 5, chart_id_t{2}, *target);
    REQUIRE(preview);

    auto other = song;
    other.id = song_id_t{9};
    test::check_stale_action_message(propose_lane_migration(song, song.revision + 1, chart_id_t{2}, *target).message(), "Lane migration");
    auto other_history = document_history_t::from(other);
    test::check_stale_action_message(apply_lane_migration(other_history, preview, false).payload.message, "Lane migration");
    CHECK(apply_lane_migration(other_history, preview, false).payload.issue ==
          lane_migration_issue_t::stale_revision);

    auto history = document_history_t::from(song);
    preview.target.layout.display_name = "tampered";
    CHECK(apply_lane_migration(history, preview, false).payload.issue ==
          lane_migration_issue_t::invalid_mapping);
    CHECK(history.document.charts.front().lane_layouts.front().layout.display_name ==
          song.charts.front().lane_layouts.front().layout.display_name);
}

TEST_CASE("lane migration rejects source drift even when document revision is reused",
          "[unit][core][lane-migration]") {
    auto song = migration_song();
    const auto target = make_osu_mania_profile(5);
    REQUIRE(target);
    const auto preview = propose_lane_migration(song, 5, chart_id_t{2}, *target);
    REQUIRE(preview);
    auto history = document_history_t::from(song);
    const auto source_lane = ordered(history.document.charts.front().lane_layouts.front().layout).front().id;
    add_note(history.document.charts.front(), object_id_t{61}, source_lane,
             {measure_id_t{3}, 0});
    CHECK(apply_lane_migration(history, preview, false).payload.issue ==
          lane_migration_issue_t::stale_revision);
    CHECK(history.document.charts.front().measures.front().notes.size() == 1);
}

TEST_CASE("lane migration rejects zero or duplicate target lane IDs",
          "[unit][core][lane-migration]") {
    const auto song = migration_song();
    auto target = make_osu_mania_profile(5);
    REQUIRE(target);
    target->layout.lanes.front().id = {};
    CHECK(propose_lane_migration(song, 5, chart_id_t{2}, *target).issue ==
          lane_migration_issue_t::invalid_target_profile);
    target = make_osu_mania_profile(5);
    REQUIRE(target);
    target->layout.lanes.back().id = target->layout.lanes.front().id;
    CHECK(propose_lane_migration(song, 5, chart_id_t{2}, *target).issue ==
          lane_migration_issue_t::invalid_target_profile);
}

TEST_CASE("lane migration requires explicit mapping when a lane changes rank across segments",
          "[unit][core][lane-migration]") {
    auto song = migration_song();
    auto& later = song.charts.front().lane_layouts[1].layout.lanes;
    std::swap(later[0].display_order, later[1].display_order);
    const auto target = make_osu_mania_profile(7);
    REQUIRE(target);
    CHECK(propose_lane_migration(song, 5, chart_id_t{2}, *target).issue ==
          lane_migration_issue_t::ambiguous_default_mapping);

    const auto source = ordered(song.charts.front().lane_layouts.front().layout);
    const auto target_lanes = ordered(target->layout);
    std::vector<lane_migration_mapping_t> mapping;
    for (std::size_t i = 0; i < source.size(); ++i)
        mapping.push_back({source[i].id, target_lanes[i].id});
    const auto explicit_preview = propose_lane_migration(song, 5, chart_id_t{2}, *target, mapping);
    CHECK(explicit_preview);
}

TEST_CASE("lane migration collision examples stay bounded for a dense many-to-one merge",
          "[unit][core][lane-migration]") {
    auto song = migration_song();
    const auto source = ordered(song.charts.front().lane_layouts.front().layout);
    const auto target = make_osu_mania_profile(5);
    REQUIRE(target);
    const auto target_lanes = ordered(target->layout);
    for (std::uint64_t row = 0; row < 100; ++row) {
        add_note(song.charts.front(), object_id_t{1000 + row}, source[0].id,
                 {measure_id_t{3}, row});
        add_note(song.charts.front(), object_id_t{2000 + row}, source[1].id,
                 {measure_id_t{3}, row});
    }
    const auto defaults = propose_lane_migration(song, 5, chart_id_t{2}, *target);
    REQUIRE(defaults);
    const auto mapping = explicit_merge(defaults, source[1].id, target_lanes[0].id);
    const auto preview = propose_lane_migration(song, 5, chart_id_t{2}, *target, mapping);
    REQUIRE(preview);
    CHECK(preview.collision_count == 100);
    CHECK(preview.collisions.size() == 64);
}

TEST_CASE("lane migration handles many ordered notes without reporting collisions",
          "[unit][core][lane-migration]") {
    auto song = migration_song();
    const auto source = ordered(song.charts.front().lane_layouts.front().layout);
    const auto target = make_osu_mania_profile(5);
    REQUIRE(target);
    for (std::uint64_t index = 0; index < 2000; ++index) {
        const auto measure = index % 2 == 0 ? measure_id_t{3} : measure_id_t{4};
        const auto row = (index / 2) % 480;
        add_note(song.charts.front(), object_id_t{3000 + index}, source[0].id,
                 {measure, row});
    }
    const auto preview = propose_lane_migration(song, 5, chart_id_t{2}, *target);
    REQUIRE(preview);
    CHECK(preview.mapped_note_count == 2000);
    CHECK(preview.collision_count == 0);
    CHECK(preview.collisions.empty());
}

TEST_CASE("lane migration keeps an imported BMSON note and retained event together",
          "[unit][core][lane-migration][bmson]") {
    auto parsed = parse_bmson_fixture("slicing-and-mixed-sounds.bmson");
    REQUIRE(parsed.value.has_value());
    auto& source = parsed.value->charts.front();
    auto* note = find_linked_note(source);
    REQUIRE(note != nullptr);
    const auto note_id = note->id;
    const auto event_id = *note->sound_channel_event;
    const auto old_lane = note->lane;
    const auto target = profile_with_new_lane_ids(source, 10'000);
    const auto mapping = rank_mapping(source, target.layout);
    auto history = document_history_t::from(*parsed.value);
    const auto preview = propose_lane_migration(history.document, history.document.revision,
                                                source.id, target, mapping);
    REQUIRE(preview);
    check_no_linked_drops(preview);

    const auto applied = apply_lane_migration(history, preview, false);
    REQUIRE(applied);
    const auto& migrated = history.document.charts.front();
    const auto& moved_note = require_note(migrated, note_id);
    check_linked_pair(migrated, moved_note, event_id, old_lane);
    CHECK(validate_document(history.document).empty());
}

TEST_CASE("lane migration confirms dropping a linked BMSON pair together",
          "[unit][core][lane-migration][bmson]") {
    auto parsed = parse_bmson_fixture("slicing-and-mixed-sounds.bmson");
    REQUIRE(parsed.value.has_value());
    auto& source = parsed.value->charts.front();
    const auto* note = find_linked_note(source);
    REQUIRE(note != nullptr);
    const auto note_id = note->id;
    const auto event_id = *note->sound_channel_event;
    const auto target = profile_with_new_lane_ids(source, 20'000);
    const auto mapping = rank_mapping(source, target.layout, note->lane);
    auto history = document_history_t::from(*parsed.value);
    const auto preview = propose_lane_migration(history.document, history.document.revision,
                                                source.id, target, mapping);
    REQUIRE(preview);
    check_linked_drop_preview(preview, note_id, event_id);
    CHECK(apply_lane_migration(history, preview, false).payload.issue ==
          lane_migration_issue_t::drop_confirmation_required);
    REQUIRE(apply_lane_migration(history, preview, true));
    const auto& migrated = history.document.charts.front();
    check_linked_pair_removed(migrated, note_id, event_id);
    CHECK(validate_document(history.document).empty());
}

TEST_CASE("lane migration collision sweep includes unlinked playable BMSON events",
          "[unit][core][lane-migration][bmson]") {
    auto parsed = parse_bmson_fixture("slicing-and-mixed-sounds.bmson");
    REQUIRE(parsed.value.has_value());
    auto& chart = parsed.value->charts.front();
    const auto fixture = add_unlinked_collision_event(chart);
    REQUIRE(validate_document(*parsed.value).empty());

    const auto target = profile_with_new_lane_ids(chart, 30'000);
    auto mapping = rank_mapping(chart, target.layout, {},
                                std::pair{fixture.event_lane, target.layout.lanes.front().id});
    auto note_mapping = std::ranges::find(mapping, fixture.note_lane,
                                          &lane_migration_mapping_t::source_lane);
    REQUIRE(note_mapping != mapping.end());
    note_mapping->target_lane = target.layout.lanes.front().id;
    auto history = document_history_t::from(*parsed.value);
    const auto preview = propose_lane_migration(history.document, history.document.revision,
                                                chart.id, target, mapping);
    REQUIRE(preview);
    check_retained_collision_rejected(history, preview, fixture);
}

TEST_CASE("lane migration preserves parsed BMS branch hold channel identity",
          "[unit][core][lane-migration][bms][branch]") {
    constexpr std::string_view source =
        "#WAV01 key.wav\n#WAV02 key2.wav\n#RANDOM 2\n#IF 1\n#00051:0100\n#00051:0002\n"
        "#ELSE\n#00151:0100\n#00151:0002\n#ENDIF\n#ENDRANDOM\n";
    bms_parse_options_t options;
    options.lane_profile_id = "bme-7k-outside-scratch";
    options.branch_choices = {{2, 1}};
    auto parsed = parse_bms_bytes(std::as_bytes(std::span{source.data(), source.size()}), options);
    REQUIRE(parsed.value.song.has_value());
    auto& chart = parsed.value.song->charts.front();
    const auto& hold = require_branch_hold(chart);
    const auto hold_id = hold.id;
    const auto old_lane = hold.lane;
    auto target = profile_with_new_lane_ids(chart, 40'000);
    const auto safe_mapping = rank_mapping(chart, target.layout);
    auto safe_history = document_history_t::from(*parsed.value.song);
    const auto safe_preview = propose_lane_migration(
        safe_history.document, safe_history.document.revision, chart.id, target, safe_mapping);
    REQUIRE(safe_preview);
    REQUIRE(apply_lane_migration(safe_history, safe_preview, false));
    const auto& safe_chart = safe_history.document.charts.front();
    const auto safe_note = std::ranges::find(safe_chart.measures.front().notes, hold_id,
                                             &note_t::id);
    REQUIRE(safe_note != safe_chart.measures.front().notes.end());
    CHECK(safe_note->lane.value == old_lane.value + 40'000);
    CHECK(validate_document(safe_history.document).empty());
}

TEST_CASE("lane migration rejects parsed BMS branch holds moved to another channel slot",
          "[unit][core][lane-migration][bms][branch]") {
    constexpr std::string_view source =
        "#WAV01 key.wav\n#WAV02 key2.wav\n#RANDOM 2\n#IF 1\n#00051:0100\n#00051:0002\n"
        "#ELSE\n#00151:0100\n#00151:0002\n#ENDIF\n#ENDRANDOM\n";
    bms_parse_options_t options;
    options.lane_profile_id = "bme-7k-outside-scratch";
    options.branch_choices = {{2, 1}};
    auto parsed = parse_bms_bytes(std::as_bytes(std::span{source.data(), source.size()}), options);
    REQUIRE(parsed.value.song.has_value());
    const auto& chart = parsed.value.song->charts.front();
    const auto hold = std::ranges::find_if(chart.measures.front().notes, [](const auto& note) {
        return note.kind == note_kind_t::hold;
    });
    REQUIRE(hold != chart.measures.front().notes.end());
    auto target = make_bme_7k_profile();
    const auto defaults = rank_mapping(chart, target.layout);
    const auto source_lane = std::ranges::find(chart.lane_layouts.front().layout.lanes, hold->lane,
                                               &lane_definition_t::id);
    REQUIRE(source_lane != chart.lane_layouts.front().layout.lanes.end());
    const auto destination = std::ranges::find_if(target.layout.lanes, [&](const auto& lane) {
        return lane.bms_channels.long_note != source_lane->bms_channels.long_note;
    });
    REQUIRE(destination != target.layout.lanes.end());
    auto mapping = defaults;
    auto explicit_lane = std::ranges::find(mapping, hold->lane,
                                           &lane_migration_mapping_t::source_lane);
    REQUIRE(explicit_lane != mapping.end());
    explicit_lane->target_lane = destination->id;
    CHECK(propose_lane_migration(*parsed.value.song, parsed.value.song->revision, chart.id, target, mapping)
              .issue == lane_migration_issue_t::unsupported_branch_identity);
}

TEST_CASE("Sealed lane migration stamp changes stay tamper errors before staleness",
          "[unit][core][lane-migration][proposal-stamp]") {
    auto history = document_history_t::from(migration_song());
    const auto target = make_osu_mania_profile(5);
    REQUIRE(target);
    auto proposal = propose_lane_migration(history.document, history.document.revision,
        chart_id_t{2}, *target);
    ++proposal.stamp.document.revision;
    const auto result = apply_lane_migration(history, proposal, false);
    CHECK(result.payload.issue == lane_migration_issue_t::invalid_mapping);
    CHECK(result.payload.message == "Lane migration preview data was changed after proposal.");
    CHECK(result.command.diagnostics.empty());
    CHECK(history.undo.empty());
}

TEST_CASE("Lane migration diagnostics own the existing proposal failure message",
          "[unit][core][lane-migration][proposal-stamp]") {
    auto history = document_history_t::from(migration_song());
    const auto target = make_osu_mania_profile(5);
    REQUIRE(target);
    const auto proposal = propose_lane_migration(history.document, history.document.revision + 1,
        chart_id_t{2}, *target);
    REQUIRE(proposal.diagnostics.size() == 1);
    test::check_stale_action_message(proposal.diagnostics, "Lane migration");
    const auto result = apply_lane_migration(history, proposal, false);
    CHECK(result.payload.message == proposal.message());
    CHECK(result.command.diagnostics.empty());
    CHECK(history.undo.empty());
}
