// Audit of the BMSON slicing rules in docs/references/bmsone-slicing.md.
// Tests only: they assert the behavior of the current production code.
#include <wormedit/core/sound_slice.h>
#include <wormedit/formats/bmson.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace {

constexpr std::uint32_t sample_rate = 48'000;
constexpr std::uint64_t frames_per_beat = 24'000; // 120 BPM, 240 pulses per beat

auto parse_song(const std::string& channels) -> wormedit::song_document_t {
    const std::string json = R"({"version":"1.0.0","info":{"title":"Slice rules",
      "artist":"Wormedit","genre":"test","chart_name":"rules","level":1,
      "init_bpm":120,"judge_rank":100,"total":100,"resolution":240,"mode_hint":"beat-7k"},
      "lines":[{"y":0},{"y":960},{"y":1920}],"bpm_events":[],"stop_events":[],
      "sound_channels":[)" + channels + "]}";
    const auto parsed = wormedit::parse_bmson_bytes(
        std::as_bytes(std::span{json.data(), json.size()}));
    REQUIRE(parsed.song);
    return *parsed.song;
}

auto derive(const wormedit::song_document_t& song,
            std::optional<std::uint64_t> frame_count = std::nullopt)
    -> wormedit::sound_slice_result_t {
    const auto& chart = song.charts.front();
    auto result = wormedit::derive_sound_channel_slices(
        chart, chart.sound_channels.front(), sample_rate, frame_count);
    REQUIRE(result);
    return result;
}

auto count_events(const wormedit::song_document_t& song, bool playable) -> std::size_t {
    std::size_t count = 0;
    for (const auto& channel : song.charts.front().sound_channels)
        count += static_cast<std::size_t>(std::ranges::count(
            channel.events, playable, &wormedit::sound_channel_event_t::playable));
    return count;
}

auto count_bgm_events(const wormedit::song_document_t& song) -> std::size_t {
    std::size_t count = 0;
    for (const auto& measure : song.charts.front().measures) count += measure.bgm_events.size();
    return count;
}

} // namespace

TEST_CASE("sound_slice rule 1: a restart wins over continue at the same pulse",
          "[unit][core][bmson][slice-rules]") {
    const auto song = parse_song(R"({"name":"a.wav","notes":[
        {"x":0,"y":0,"l":0,"c":false},
        {"x":0,"y":240,"l":0,"c":true},{"x":1,"y":240,"l":0,"c":false}]})");
    const auto result = derive(song, 200'000);
    REQUIRE(result.slices.size() == 2);
    CHECK(result.slices[1].restart);
    CHECK(result.slices[1].source_begin_frame == 0);
    CHECK(result.slices[0].source_begin_frame == 0);
    CHECK(result.slices[0].source_end_frame == frames_per_beat);
}

TEST_CASE("sound_slice rule 2: continue keeps the preceding source position",
          "[unit][core][bmson][slice-rules]") {
    const auto song = parse_song(R"({"name":"a.wav","notes":[
        {"x":0,"y":0,"l":0,"c":false},{"x":0,"y":240,"l":0,"c":true},
        {"x":0,"y":480,"l":0,"c":true}]})");
    const auto result = derive(song, 200'000);
    REQUIRE(result.slices.size() == 3);
    CHECK(result.slices[1].source_begin_frame == frames_per_beat);
    CHECK(result.slices[2].source_begin_frame == 2 * frames_per_beat);
    CHECK_FALSE(result.slices[1].restart);
}

TEST_CASE("sound_slice rule 3: events at one pulse share one slice of polyphony one",
          "[unit][core][bmson][slice-rules]") {
    const auto song = parse_song(R"({"name":"a.wav","notes":[
        {"x":0,"y":0,"l":0,"c":false},
        {"x":1,"y":240,"l":0,"c":true},{"x":2,"y":240,"l":0,"c":true},
        {"x":0,"y":240,"l":0,"c":true}]})");
    const auto result = derive(song, 200'000);
    REQUIRE(result.slices.size() == 2);
    CHECK(result.slices[1].events.size() == 3);
    for (const auto& event : result.slices[1].events)
        CHECK(wormedit::find_sound_slice(result, event) == &result.slices[1]);
}

TEST_CASE("sound_slice rule 4: the final slice is open-ended until a source length is known",
          "[unit][core][bmson][slice-rules]") {
    const auto song = parse_song(R"({"name":"a.wav","notes":[
        {"x":0,"y":0,"l":0,"c":false},{"x":0,"y":240,"l":0,"c":true}]})");
    const auto open_ended = derive(song);
    REQUIRE(open_ended.slices.size() == 2);
    CHECK(open_ended.slices[0].source_end_frame == frames_per_beat);
    CHECK_FALSE(open_ended.slices[1].source_end_frame.has_value());

    const auto bounded = derive(song, 100'000);
    CHECK(bounded.slices[1].source_end_frame == 100'000);

    auto clipped = song;
    clipped.charts.front().sound_channels.front().source_clip.end_frame = 60'000;
    CHECK(derive(clipped, 100'000).slices[1].source_end_frame == 60'000);
    CHECK(derive(clipped).slices[1].source_end_frame == 60'000);
}

TEST_CASE("sound_slice rule 5: a BGM note sharing a pulse with a playable note is discarded",
          "[unit][core][bmson][slice-rules]") {
    const auto song = parse_song(R"({"name":"a.wav","notes":[
        {"x":0,"y":0,"l":0,"c":false},
        {"x":0,"y":240,"l":0,"c":true},{"x":3,"y":240,"l":0,"c":true}]})");
    CHECK(count_events(song, true) == 1);
    CHECK(count_bgm_events(song) == 1);

    const auto written = wormedit::write_bmson(song);
    REQUIRE(written.json);
    CHECK_FALSE(written.has_errors());
    const auto reparsed = wormedit::parse_bmson_bytes(std::as_bytes(
        std::span{written.json->data(), written.json->size()}));
    REQUIRE(reparsed.song);
    CHECK(count_events(*reparsed.song, true) == 1);
    CHECK(count_bgm_events(*reparsed.song) == 1);
}

TEST_CASE("sound_slice rule 6: fused notes at one lane and pulse need equal hold lengths",
          "[unit][core][bmson][slice-rules][negative]") {
    const auto song = parse_song(R"({"name":"a.wav","notes":[
        {"x":1,"y":0,"l":240,"c":false}]},{"name":"b.wav","notes":[
        {"x":1,"y":0,"l":480,"c":false}]})");
    const auto written = wormedit::write_bmson(song);
    CHECK(written.has_errors());
    CHECK_FALSE(written.diagnostics.empty());
}

TEST_CASE("sound_slice rule 7: equal hold lengths fuse across channels and export",
          "[unit][core][bmson][slice-rules]") {
    const auto song = parse_song(R"({"name":"a.wav","notes":[
        {"x":1,"y":0,"l":240,"c":false}]},{"name":"b.wav","notes":[
        {"x":1,"y":0,"l":240,"c":false}]})");
    const auto written = wormedit::write_bmson(song);
    REQUIRE(written.json);
    CHECK_FALSE(written.has_errors());
}

TEST_CASE("sound_slice rule 8: duplicate notes at one lane and pulse in one channel are rejected",
          "[unit][core][bmson][slice-rules][negative]") {
    const auto song = parse_song(R"({"name":"a.wav","notes":[
        {"x":1,"y":0,"l":0,"c":false},{"x":1,"y":0,"l":0,"c":true}]})");
    CHECK(wormedit::write_bmson(song).has_errors());
}

TEST_CASE("sound_slice rule 9: an empty channel has no slices", "[unit][core][bmson][slice-rules]") {
    const auto song = parse_song(R"({"name":"empty.wav","notes":[]})");
    const auto result = derive(song, 100'000);
    CHECK(result);
    CHECK(result.slices.empty());
    CHECK(result.diagnostics.empty());
}
