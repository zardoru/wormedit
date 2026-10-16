#include <wormedit/core/bmson_lane_mode.h>
#include <wormedit/formats/bmson.h>
#include <wormedit/formats/native_project.h>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <source_location>
#include <string_view>

namespace {
using namespace wormedit;
void require_mode(bool condition, std::source_location at = std::source_location::current()) {
    INFO("Lane mode requirement at " << at.line());
    REQUIRE(condition);
}
void check_mode(bool condition, std::source_location at = std::source_location::current()) {
    INFO("Lane mode check at " << at.line());
    CHECK(condition);
}
auto fixture() -> song_document_t {
    constexpr std::string_view json = R"({"version":"1.0.0","info":{"title":"Modes","artist":"test","init_bpm":120,"resolution":240,"mode_hint":"beat-7k"},"lines":[{"y":960},{"y":1920}],"sound_channels":[{"name":"kick.wav","notes":[{"x":0,"y":0,"c":false},{"x":0,"y":240,"l":120,"c":false},{"x":1,"y":480,"c":false}]},{"name":"snare.wav","notes":[{"x":0,"y":0,"c":false},{"x":0,"y":720,"c":false}]},{"name":"vox.wav","notes":[{"x":0,"y":0,"c":false},{"x":0,"y":960,"c":true},{"x":2,"y":1200,"c":true}]}]})";
    const auto parsed = parse_bmson_bytes(std::as_bytes(std::span{json.data(), json.size()}));
    require_mode(parsed.value.has_value());
    return *parsed.value;
}
auto change(document_history_t& history, std::size_t channel, bmson_channel_lane_mode_t mode,
            bool restart = false, note_to_bgm_policy_t policy = {}) -> document_command_result_t {
    const auto& chart = history.document.charts.front();
    return set_bmson_channel_lane_mode(history, chart.id,
        chart.sound_channels.at(channel).id, mode, restart, policy);
}
auto snapshot(const document_history_t& history) -> std::string {
    const auto written = write_native_project(history.document);
    require_mode(written.value.has_value());
    return *written.value;
}
}

TEST_CASE("BMSON lane_mode Make stem and Pack preserve channels and round trip", "[core][bmson][lane_mode]") {
    using namespace wormedit;
    auto history = document_history_t::from(fixture());
    const auto before = history.document;
    const auto json = write_bmson(before).value;
    require_mode(json.has_value());
    require_mode(change(history, 0, bmson_channel_lane_mode_t::stem).applied);
    check_mode(history.undo.size() == 1);
    check_mode(history.document.resources.front().sound_kind == sound_kind_t::stem);
    check_mode(write_bmson(history.document).value == json);
    check_mode(validate_document(history.document).empty());
    require_mode(change(history, 0, bmson_channel_lane_mode_t::packed).applied);
    check_mode(history.undo.size() == 2);
    check_mode(write_bmson(history.document).value == json);
    auto restored = history.document;
    restored.revision = before.revision;
    check_mode(write_native_project(restored).value == write_native_project(before).value);
    require_mode(undo_document_command(history));
    check_mode(history.document.resources.front().sound_kind == sound_kind_t::stem);
    require_mode(redo_document_command(history));
    check_mode(history.document.resources.front().sound_kind == sound_kind_t::keysound);
}

TEST_CASE("BMSON lane_mode continuation refusal is atomic and conversion is undoable", "[core][bmson][lane_mode]") {
    using namespace wormedit;
    auto history = document_history_t::from(fixture());
    const auto before = snapshot(history);
    const auto refusal = change(history, 2, bmson_channel_lane_mode_t::packed);
    require_mode(!refusal.applied && !refusal.diagnostics.empty());
    check_mode(refusal.diagnostics.front().message == "continuation points need a stem lane");
    check_mode(snapshot(history) == before && history.undo.empty());
    require_mode(change(history, 2, bmson_channel_lane_mode_t::packed, true).applied);
    check_mode(history.undo.size() == 1);
    check_mode(std::ranges::none_of(history.document.charts.front().sound_channels.back().events,
        [](const auto& event) { return event.continuation; }));
    check_mode(validate_document(history.document).empty());
    check_mode(write_bmson(history.document).value != write_bmson(history.undo.front().before).value);
    require_mode(undo_document_command(history));
    auto restored = history.document;
    restored.revision = history.undo.empty() ? history.redo.back().before.revision : restored.revision;
    check_mode(write_native_project(restored).value == before);
}

TEST_CASE("BMSON lane_mode format unknown and capacity failures change nothing", "[core][bmson][lane_mode]") {
    using namespace wormedit;
    auto history = document_history_t::from(fixture());
    require_mode(change(history, 0, bmson_channel_lane_mode_t::stem).applied);
    const auto before = snapshot(history);
    const auto& chart = history.document.charts.front();
    check_mode(!set_bmson_channel_lane_mode(history,
        chart.id, object_id_t{99999}, bmson_channel_lane_mode_t::packed).applied);
    check_mode(!change(history, 0, bmson_channel_lane_mode_t::packed, false, {.max_lanes = 0}).applied);
    check_mode(snapshot(history) == before && history.undo.size() == 1);
    history.document.charts.front().source_family = format_family_t::bms;
    for (auto& lane : history.document.charts.front().bgm_source_lanes) lane.stem_channel.reset();
    const auto bms = snapshot(history);
    check_mode(!change(history, 0, bmson_channel_lane_mode_t::packed).applied);
    check_mode(snapshot(history) == bms);
}

TEST_CASE("BMSON lane_mode repeated mode changes create no undo entry", "[core][bmson][lane_mode]") {
    using namespace wormedit;
    auto history = document_history_t::from(fixture());
    check_mode(!change(history, 0, bmson_channel_lane_mode_t::packed).applied);
    check_mode(history.undo.empty());
    require_mode(change(history, 0, bmson_channel_lane_mode_t::stem).applied);
    const auto before = snapshot(history);
    check_mode(!change(history, 0, bmson_channel_lane_mode_t::stem).applied);
    check_mode(snapshot(history) == before && history.undo.size() == 1);
}

TEST_CASE("BMSON lane_mode empty and playable-only channels round trip", "[core][bmson][lane_mode]") {
    using namespace wormedit;
    for (const bool empty : {false, true}) {
        auto song = fixture();
        auto& chart = song.charts.front();
        const auto ids = chart.sound_channels.front().events;
        const auto owned = [&](const auto& event) {
            return event.sound_channel_event && std::ranges::any_of(ids,
                [&](const auto& source) { return source.id == *event.sound_channel_event; });
        };
        for (auto& measure : chart.measures) {
            std::erase_if(measure.bgm_events, owned);
            if (empty) std::erase_if(measure.notes, owned);
        }
        std::erase_if(chart.sound_channels.front().events,
            [empty](const auto& event) { return empty || !event.playable; });
        auto history = document_history_t::from(std::move(song));
        const auto before = history.document;
        require_mode(change(history, 0, bmson_channel_lane_mode_t::stem).applied);
        require_mode(change(history, 0, bmson_channel_lane_mode_t::packed).applied);
        auto after = history.document;
        after.revision = before.revision;
        check_mode(write_native_project(after).value == write_native_project(before).value);
    }
}

TEST_CASE("BMSON lane_mode allocator rejection preserves document and history", "[core][bmson][lane_mode]") {
    using namespace wormedit;
    auto history = document_history_t::from(fixture());
    require_mode(change(history, 0, bmson_channel_lane_mode_t::stem).applied);
    auto& chart = history.document.charts.front();
    const auto lane = std::ranges::find_if(chart.bgm_source_lanes,
        [](const auto& candidate) { return !candidate.stem_channel; });
    require_mode(lane != chart.bgm_source_lanes.end());
    chart.measures.front().bgm_events.push_back({.id = object_id_t{999},
        .position = {chart.measures.front().id, 0}, .source_lane = lane->id,
        .sound = history.document.resources[1].id});
    require_mode(validate_document(history.document).empty());
    const auto before = snapshot(history);
    const auto failed = change(history, 0, bmson_channel_lane_mode_t::packed, false,
                               {.allow_new_lanes = false});
    require_mode(!failed.applied && !failed.diagnostics.empty());
    check_mode(snapshot(history) == before && history.undo.size() == 1 && history.redo.empty());
}

TEST_CASE("BMSON lane_mode rejects resources shared by multiple channels", "[core][bmson][lane_mode]") {
    using namespace wormedit;
    auto song = fixture();
    const auto resource = song.resources.front().id;
    const auto asset = song.resources.front().asset;
    song.charts.front().sound_channels[1].source_clip.source = asset;
    for (auto& measure : song.charts.front().measures)
        for (auto& event : measure.bgm_events)
            if (event.sound == song.resources[1].id) event.sound = resource;
    auto history = document_history_t::from(std::move(song));
    const auto before = snapshot(history);
    const auto result = change(history, 0, bmson_channel_lane_mode_t::stem);
    require_mode(!result.applied && !result.diagnostics.empty());
    check_mode(snapshot(history) == before && history.undo.empty());
}

TEST_CASE("BMSON lane_mode packs a whole channel beyond the interactive selection bound", "[core][bmson][lane_mode]") {
    // Slow: 4,097 is the smallest channel beyond the fixed 4,096-object interactive selection bound.
    using namespace wormedit;
    std::string json = R"({"version":"1.0.0","info":{"title":"Large channel","init_bpm":120,"resolution":240,"mode_hint":"beat-7k"},"lines":[{"y":5000}],"sound_channels":[{"name":"kick.wav","notes":[)";
    for (int row = 0; row < 4097; ++row) {
        if (row) json += ',';
        json += "{\"x\":0,\"y\":" + std::to_string(row) + ",\"c\":false}";
    }
    json += "]}]}";
    const auto parsed = parse_bmson_bytes(std::as_bytes(std::span{json.data(), json.size()}));
    require_mode(parsed.value.has_value());
    auto history = document_history_t::from(*parsed.value);
    const auto before = write_bmson(history.document).value;
    require_mode(before.has_value());
    require_mode(change(history, 0, bmson_channel_lane_mode_t::stem).applied);
    require_mode(change(history, 0, bmson_channel_lane_mode_t::packed).applied);
    check_mode(write_bmson(history.document).value == before && history.undo.size() == 2);
}
