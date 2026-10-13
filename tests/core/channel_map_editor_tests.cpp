#include <wormedit/core/channel_map_editor.h>
#include <wormedit/core/lane_profiles.h>
#include <wormedit/formats/bms.h>
#include <wormedit/formats/bms_writer.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

namespace {

using namespace wormedit;

auto channel_song() -> song_document_t {
    const auto profile = make_bme_7k_profile();
    return {
        .id = song_id_t{1},
        .revision = 7,
        .charts = {{
            .id = chart_id_t{2},
            .source_family = format_family_t::bms,
            .measures = {{.id = measure_id_t{3}}, {.id = measure_id_t{4}}},
            .lane_layouts = {{.id = object_id_t{10},
                              .position = {measure_id_t{3}, 0},
                              .layout = profile.layout},
                             {.id = object_id_t{11},
                              .position = {measure_id_t{4}, 0},
                              .layout = profile.layout}},
            .input_profile = profile.input,
            .bpms = {{.id = object_id_t{20},
                      .position = {measure_id_t{3}, 0},
                      .beats_per_minute = {120, 1}}},
        }},
    };
}

auto replacement(const lane_definition_t& lane, std::string visible) -> channel_map_replacement_t {
    auto channels = lane.bms_channels;
    channels.visible = std::move(visible);
    return {lane.id, std::move(channels)};
}

auto parse_root_chart() -> std::optional<song_document_t> {
    constexpr std::string_view source =
        "#PLAYER 1\n#TITLE Channel edit\n#ARTIST Tester\n#BPM 120\n"
        "#WAV01 one.wav\n#WAV02 two.wav\n#00011:01\n#00012:02\n";
    bms_parse_options_t options;
    options.lane_profile_id = "bme-7k-outside-scratch";
    const auto parsed = parse_bms_bytes(std::as_bytes(std::span{source.data(), source.size()}),
                                        options);
    return parsed.song;
}

auto batch_swap_scenario() -> bool {
    auto song = channel_song();
    auto& chart = song.charts.front();
    auto lanes = chart.lane_layouts.front().layout.lanes;
    std::ranges::sort(lanes, {}, &lane_definition_t::display_order);
    const auto first = lanes[0];
    const auto second = lanes[1];
    chart.measures[0].notes = {
        {.id = object_id_t{30}, .position = {measure_id_t{3}, 0}, .lane = first.id},
        {.id = object_id_t{31}, .position = {measure_id_t{3}, 8}, .lane = second.id},
    };
    chart.measures[1].notes = {
        {.id = object_id_t{32}, .position = {measure_id_t{4}, 0}, .lane = first.id}};
    auto history = make_document_history(song);
    const std::array replacements{replacement(first, *second.bms_channels.visible),
                                  replacement(second, *first.bms_channels.visible)};
    const auto preview = propose_bms_channel_map_edit(history.document, 7, chart_id_t{2},
                                                       object_id_t{10}, replacements);
    if (!preview || preview.affected_note_count != 2) return false;
    if (!apply_bms_channel_map_edit(history, preview)) return false;
    const auto& changed = history.document.charts.front();
    const auto changed_first = std::ranges::find(changed.lane_layouts[0].layout.lanes, first.id,
                                                  &lane_definition_t::id);
    const auto changed_second = std::ranges::find(changed.lane_layouts[0].layout.lanes, second.id,
                                                   &lane_definition_t::id);
    if (changed_first == changed.lane_layouts[0].layout.lanes.end() ||
        changed_second == changed.lane_layouts[0].layout.lanes.end()) return false;
    if (changed_first->bms_channels.visible != second.bms_channels.visible ||
        changed_second->bms_channels.visible != first.bms_channels.visible) return false;
    if (changed.measures[0].notes[0].lane != first.id ||
        changed.measures[0].notes[0].position.row != 0 ||
        changed.measures[1].notes[0].lane != first.id) return false;
    const auto original_later_first = std::ranges::find(
        chart.lane_layouts[1].layout.lanes, first.id, &lane_definition_t::id);
    const auto changed_later_first = std::ranges::find(
        changed.lane_layouts[1].layout.lanes, first.id, &lane_definition_t::id);
    if (original_later_first == chart.lane_layouts[1].layout.lanes.end() ||
        changed_later_first == changed.lane_layouts[1].layout.lanes.end() ||
        original_later_first->bms_channels.visible != changed_later_first->bms_channels.visible)
        return false;
    if (!undo_document_command(history) ||
        history.document.charts.front().measures[0].notes.size() != 2) return false;
    const auto restored = std::ranges::find(
        history.document.charts.front().lane_layouts.front().layout.lanes, first.id,
        &lane_definition_t::id);
    return restored != history.document.charts.front().lane_layouts.front().layout.lanes.end() &&
           restored->bms_channels.visible == first.bms_channels.visible;
}

auto channel_conflict_scenario() -> bool {
    const auto song = channel_song();
    const auto& chart = song.charts.front();
    const auto& lanes = chart.lane_layouts.front().layout.lanes;
    const auto duplicate = replacement(lanes[1], *lanes[0].bms_channels.visible);
    const auto result = propose_bms_channel_map_edit(song, 7, chart.id,
        chart.lane_layouts.front().id, std::span{&duplicate, 1});
    return result.issue == channel_map_issue_t::duplicate_channel &&
           result.conflict_count == 1 && result.conflicts.size() == 1 &&
           result.conflicts.front().first_lane_name == lanes[0].name &&
           result.conflicts.front().second_lane_name == lanes[1].name;
}

auto casefold_scenario() -> bool {
    const auto song = channel_song();
    const auto& chart = song.charts.front();
    const auto& lanes = chart.lane_layouts.front().layout.lanes;
    auto lower = replacement(lanes.front(), "1a");
    const auto normalized = propose_bms_channel_map_edit(song, 7, chart.id,
        chart.lane_layouts.front().id, std::span{&lower, 1});
    if (!normalized || normalized.replacements.front().channels.visible != "1A" ||
        normalized.affected_note_count != 0) return false;
    auto first = replacement(lanes[0], "1A");
    auto second = replacement(lanes[1], "1a");
    const std::array duplicates{first, second};
    const auto conflict = propose_bms_channel_map_edit(song, 7, chart.id,
        chart.lane_layouts.front().id, duplicates);
    return conflict.issue == channel_map_issue_t::duplicate_channel &&
           conflict.conflicts.size() == 1 && conflict.conflicts.front().channel == "1A";
}

auto invalid_channel_rejected() -> bool {
    const auto song = channel_song();
    const auto& chart = song.charts.front();
    auto invalid = replacement(chart.lane_layouts.front().layout.lanes.front(), "1!");
    const auto malformed = propose_bms_channel_map_edit(song, 7, chart.id,
        chart.lane_layouts.front().id, std::span{&invalid, 1});
    invalid.channels.visible = "31";
    const auto wrong_family = propose_bms_channel_map_edit(song, 7, chart.id,
        chart.lane_layouts.front().id, std::span{&invalid, 1});
    invalid.channels.visible = "10";
    const auto zero_suffix = propose_bms_channel_map_edit(song, 7, chart.id,
        chart.lane_layouts.front().id, std::span{&invalid, 1});
    auto with_note = song;
    with_note.charts.front().measures.front().notes.push_back({
        .id = object_id_t{80}, .position = {measure_id_t{3}, 0},
        .lane = chart.lane_layouts.front().layout.lanes.front().id});
    invalid.channels.visible.reset();
    const auto missing_used = propose_bms_channel_map_edit(with_note, 7, chart.id,
        chart.lane_layouts.front().id, std::span{&invalid, 1});
    return malformed.issue == channel_map_issue_t::invalid_channel &&
           wrong_family.issue == channel_map_issue_t::invalid_channel &&
           zero_suffix.issue == channel_map_issue_t::invalid_channel &&
           missing_used.issue == channel_map_issue_t::missing_export_channel;
}

auto branch_map_rejected() -> bool {
    constexpr std::string_view source =
        "#WAV01 key.wav\n#RANDOM 2\n#IF 1\n#00011:01\n#ELSE\n#00111:01\n"
        "#ENDIF\n#ENDRANDOM\n";
    bms_parse_options_t options;
    options.lane_profile_id = "bme-7k-outside-scratch";
    options.branch_choices = {{1, 1}};
    const auto parsed = parse_bms_bytes(std::as_bytes(std::span{source.data(), source.size()}),
                                        options);
    if (!parsed.song || parsed.song->charts.empty()) return false;
    const auto& chart = parsed.song->charts.front();
    const auto lane = std::ranges::find_if(chart.lane_layouts.front().layout.lanes,
        [](const auto& item) { return item.bms_channels.visible == "11"; });
    if (lane == chart.lane_layouts.front().layout.lanes.end()) return false;
    const auto change = replacement(*lane, "1A");
    const auto proposal = propose_bms_channel_map_edit(*parsed.song, parsed.song->revision,
        chart.id, chart.lane_layouts.front().id, std::span{&change, 1});
    return proposal.issue == channel_map_issue_t::unsupported_branch_channel;
}

auto branch_hold_source_channel_rejected() -> bool {
    constexpr std::string_view source =
        "#WAV01 key.wav\n#RANDOM 2\n#IF 1\n#00011:01\n#ELSE\n#00111:01\n"
        "#ENDIF\n#ENDRANDOM\n";
    bms_parse_options_t options;
    options.lane_profile_id = "bme-7k-outside-scratch";
    options.branch_choices = {{1, 1}};
    auto parsed = parse_bms_bytes(std::as_bytes(std::span{source.data(), source.size()}), options);
    if (!parsed.song || parsed.song->charts.empty()) return false;
    auto& chart = parsed.song->charts.front();
    const auto lane = std::ranges::find_if(chart.lane_layouts.front().layout.lanes,
        [](const auto& item) { return item.bms_channels.visible == "11"; });
    if (lane == chart.lane_layouts.front().layout.lanes.end()) return false;
    note_t* selected{};
    for (auto& measure : chart.measures) {
        const auto found = std::ranges::find(measure.notes, lane->id, &note_t::lane);
        if (found == measure.notes.end()) continue;
        selected = &*found;
        break;
    }
    if (!selected) return false;
    selected->kind = note_kind_t::hold;
    selected->end = local_position_t{selected->position.measure, selected->position.row + 1};
    selected->provenance.source_field = "11 slot 0 object 01";
    const auto change = replacement(*lane, "1A");
    const auto proposal = propose_bms_channel_map_edit(*parsed.song, parsed.song->revision,
        chart.id, chart.lane_layouts.front().id, std::span{&change, 1});
    return proposal.issue == channel_map_issue_t::unsupported_branch_channel;
}

auto root_export_swap_scenario() -> bool {
    auto song = parse_root_chart();
    if (!song) return false;
    auto& chart = song->charts.front();
    const auto& layout = chart.lane_layouts.front();
    const auto first = std::ranges::find_if(layout.layout.lanes, [](const auto& lane) {
        return lane.bms_channels.visible == "11";
    });
    const auto second = std::ranges::find_if(layout.layout.lanes, [](const auto& lane) {
        return lane.bms_channels.visible == "12";
    });
    if (first == layout.layout.lanes.end() || second == layout.layout.lanes.end()) return false;
    auto history = make_document_history(*song);
    const std::array replacements{replacement(*first, "12"), replacement(*second, "11")};
    const auto preview = propose_bms_channel_map_edit(history.document,
        history.document.revision, chart.id, layout.id, replacements);
    if (!preview || preview.affected_note_count != 2 ||
        !apply_bms_channel_map_edit(history, preview)) return false;
    const auto written = write_bms(history.document);
    if (!written.bytes || written.has_errors() ||
        written.bytes->find("#00011:02") == std::string::npos ||
        written.bytes->find("#00012:01") == std::string::npos) return false;
    const auto reparsed = parse_bms_bytes(std::as_bytes(std::span{
        written.bytes->data(), written.bytes->size()}),
        {.lane_profile_id = "bme-7k-outside-scratch"});
    return reparsed.song && reparsed.song->charts.front().measures.front().notes.size() == 2;
}

} // namespace

TEST_CASE("BMS channel-map batch swaps channels and only affects its layout segment",
          "[unit][core][channel-map]") {
    CHECK(batch_swap_scenario());
}

TEST_CASE("BMS channel-map conflicts name both lanes and invalid values reject",
          "[unit][core][channel-map]") {
    CHECK(channel_conflict_scenario());
    CHECK(casefold_scenario());
    CHECK(invalid_channel_rejected());
    CHECK(branch_map_rejected());
    CHECK(branch_hold_source_channel_rejected());
}

TEST_CASE("BMS channel-map edits change ordinary root export channels",
          "[unit][core][channel-map][bms]") {
    CHECK(root_export_swap_scenario());
}

TEST_CASE("BMS channel-map proposals reject stale or altered state",
          "[unit][core][channel-map]") {
    auto song = channel_song();
    const auto& chart = song.charts.front();
    const auto lane = chart.lane_layouts.front().layout.lanes.front();
    const auto change = replacement(lane, "1A");
    const auto proposal = propose_bms_channel_map_edit(song, 7, chart.id,
        chart.lane_layouts.front().id, std::span{&change, 1});
    REQUIRE(proposal);
    auto other = song;
    other.id = song_id_t{9};
    auto history = make_document_history(other);
    CHECK(apply_bms_channel_map_edit(history, proposal).payload.issue ==
          channel_map_issue_t::stale_revision);
    CHECK(history.document.revision == other.revision);
    history = make_document_history(song);
    auto altered = proposal;
    altered.affected_note_count = 42;
    CHECK(apply_bms_channel_map_edit(history, altered).payload.issue ==
          channel_map_issue_t::invalid_replacement);
    CHECK(history.document.charts.front().lane_layouts.front().layout.lanes.front().bms_channels.visible ==
          song.charts.front().lane_layouts.front().layout.lanes.front().bms_channels.visible);
    CHECK(history.document.revision == song.revision);
}
