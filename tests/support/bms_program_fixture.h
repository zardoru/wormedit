#pragma once
#include "bms_profile_fixtures.h"
#include <wormedit/formats/bms.h>
#include <wormedit/core/rational.h>
#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <array>
#include <iterator>

namespace wormedit::test::bms_program {
inline auto bytes(std::string_view source) -> std::span<const std::byte> {
    return std::as_bytes(std::span{source.data(), source.size()});
}
inline auto fixture(std::string_view name) -> std::string {
    std::ifstream input(std::filesystem::path(WORMEDIT_FIXTURE_DIR) / "bms" / name, std::ios::binary);
    REQUIRE(input.is_open());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
inline auto prepared(std::string_view source, std::string profile = "bme-7k-outside-scratch") -> bms_prepared_program_t {
    auto result = prepare_bms_program(bytes(source), {.lane_profile_id = std::move(profile)});
    REQUIRE(result.ok());
    REQUIRE(result.value.prepared);
    return std::move(*result.value.prepared);
}
inline auto projected(const bms_prepared_program_t& input, bms_program_evaluation_t choices = {}) -> bms_program_projection_t {
    auto result = bms_program_projection_t::from(input, choices);
    INFO(result.diagnostics.size());
    REQUIRE(result.ok());
    REQUIRE(result.value);
    return std::move(*result.value);
}
inline auto selectors(const bms_program_t& program) -> std::vector<branch_id_t> {
    std::vector<branch_id_t> result;
    for (const auto& node : program.nodes())
        if (std::holds_alternative<bms_random_scope_t>(node.value) || std::holds_alternative<bms_switch_scope_t>(node.value))
            result.push_back(node.id);
    return result;
}
inline auto position_key(const chart_document_t& chart, local_position_t value) -> std::string {
    const auto measure = std::ranges::find(chart.measures, value.measure, &measure_chunk_t::id);
    const auto row = rows_to_fraction(value.row, chart.divisions_per_standard_measure);
    return std::to_string(measure - chart.measures.begin()) + ":" + std::to_string(row.numerator) + "/" + std::to_string(row.denominator);
}
inline auto resource_key(const song_document_t& song, std::optional<resource_id_t> id) -> std::string {
    if (!id) return "unbound";
    const auto entry = std::ranges::find(song.resources, *id, &resource_entry_t::id);
    REQUIRE(entry != song.resources.end());
    if (entry->missing) return "missing:" + entry->target_identities.front().identifier;
    if (const auto* asset = song.find_audio_asset(entry->asset)) return asset->source_path.generic_string();
    const auto asset = std::ranges::find(song.visual_assets, entry->asset, &visual_asset_t::id);
    REQUIRE(asset != song.visual_assets.end());
    return asset->source_path.generic_string();
}
inline auto number_key(signed_rational_t value) -> std::string {
    return std::to_string(value.numerator) + "/" + std::to_string(value.denominator);
}
inline auto note_keys(const song_document_t& song) -> std::vector<std::string> {
    const auto& chart = song.charts.front();
    std::vector<std::string> result;
    for (const auto& measure : chart.measures) for (const auto& note : measure.notes) {
        const auto lane = chart.lane_layouts.front().layout.find(note.lane);
        REQUIRE(lane);
        result.push_back(position_key(chart, note.position) + ":" + lane->bms_channels.visible.value_or("?") + ":" +
            std::to_string(static_cast<int>(note.kind)) + ":" + (note.end ? position_key(chart, *note.end) : "tap") + ":" + resource_key(song, note.sound));
    }
    std::ranges::sort(result);
    return result;
}
inline auto event_keys(const song_document_t& song) -> std::vector<std::string> {
    const auto& chart = song.charts.front();
    std::vector<std::string> result;
    for (const auto& event : chart.bpms) result.push_back("bpm:" + position_key(chart, event.position) + ":" + number_key(event.beats_per_minute));
    for (const auto& event : chart.pauses) result.push_back("stop:" + position_key(chart, event.position) + ":" + number_key(event.duration) + ":" + std::to_string(static_cast<int>(event.source_unit)));
    for (const auto& event : chart.scrolls) result.push_back("scroll:" + position_key(chart, event.position) + ":" + number_key(event.multiplier));
    for (const auto& event : chart.bga_events) result.push_back("bga:" + position_key(chart, event.position) + ":" + std::to_string(static_cast<int>(event.kind)) + ":" + resource_key(song, event.resource));
    std::ranges::sort(result);
    return result;
}
inline auto bgm_keys(const song_document_t& song) -> std::vector<std::string> {
    const auto& chart = song.charts.front();
    std::vector<std::string> result;
    for (const auto& measure : chart.measures) for (const auto& event : measure.bgm_events) {
        const auto lane = std::ranges::find(chart.bgm_source_lanes, event.source_lane, &bgm_source_lane_t::id);
        result.push_back(position_key(chart, event.position) + ":" + std::to_string(lane - chart.bgm_source_lanes.begin()) + ":" +
            std::to_string(event.structural_slot) + ":" + resource_key(song, event.sound));
    }
    std::ranges::sort(result);
    return result;
}
inline auto length_keys(const chart_document_t& chart) -> std::vector<rational_t> {
    std::vector<rational_t> result;
    for (const auto& measure : chart.measures) result.push_back(measure.definition.length);
    return result;
}
inline constexpr auto scoped_source = "#TITLE Root\n#ARTIST Author\n#BPM 120\n#WAV01 common.wav\n#BMP01 scene.png\n#00011:01\n#00051:01\n"
    "#RANDOM 3\n#IF 1\n#WAV01 branch.wav\n#BPM01 150\n#BPM01 160\n#STOP01 192\n#SCROLL01 -0.5\n"
    "#00008:01\n#00009:01\n#000SC:01\n#00002:1.5\n#00001:0100\n#00001:0001\n; split Chart\n#TITLE A\n#00151:01\n"
    "#ELSEIF 2\n#WAV01 b.wav\n#TITLE B\n#00251:01\n#0010A:01\n#ELSE\n#TITLE C\n#00151:01\n#ENDIF\n#ENDRANDOM\n";
} // namespace wormedit::test::bms_program
