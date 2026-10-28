#include <wormedit/core/export_cut_transform.h>
#include <wormedit/core/timing.h>
#include <wormedit/core/measure_editor.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <algorithm>

namespace {
using namespace wormedit;
auto export_song() -> song_document_t {
    chart_document_t chart;
    chart.id = chart_id_t{2};
    chart.divisions_per_standard_measure = 960;
    chart.measures = {{.id = measure_id_t{3}}, {.id = measure_id_t{4}}, {.id = measure_id_t{5}}};
    chart.bpms = {{.id = object_id_t{10}, .position = {measure_id_t{3}, 0}, .beats_per_minute = {120, 1}}};
    return {.id = song_id_t{1}, .charts = {chart},
        .export_cuts = {{.id = object_id_t{50}, .begin_milliseconds = 500, .end_milliseconds = 1000}}};
}
auto time_of(const chart_document_t& chart, local_position_t position) -> signed_rational_t {
    const auto index = measure_index_t::from(chart);
    const auto timing = timing_index_t::from(chart);
    REQUIRE(index);
    REQUIRE(timing.value);
    const auto time = timing.value->query_seconds(*index, position);
    REQUIRE(time);
    return time->value;
}
void check_cut_success(const export_cut_transform_outcome_t& result) {
    REQUIRE((result.ok() && result.value));
}
void check_measure_identity(const chart_document_t& a, const chart_document_t& b) {
    REQUIRE(a.measures.size() == b.measures.size());
    for (std::size_t i = 0; i < a.measures.size(); ++i)
        CHECK((a.measures[i].id == b.measures[i].id && a.measures[i].definition == b.measures[i].definition));
}
auto note_with_id(const chart_document_t& chart, object_id_t id) -> const note_t* {
    for (const auto& measure : chart.measures)
        for (const auto& note : measure.notes)
            if (note.id == id) return &note;
    return nullptr;
}
void check_rational_cut_chart(const chart_document_t& chart, std::optional<signed_rational_t> expected) {
    const auto* retained = note_with_id(chart, object_id_t{20});
    REQUIRE(retained);
    CHECK(chart.divisions_per_standard_measure > 1'000'000'000);
    CHECK(time_of(chart, retained->position) == expected);
    CHECK(chart.warps.empty());
}
}
TEST_CASE("Export cuts remove their union from detached charts and retain exact source data", "[core][export-cut][export]") {
    auto song = export_song();
    song.export_cuts.push_back({.id = object_id_t{51}, .begin_milliseconds = 750, .end_milliseconds = 1500});
    song.charts.front().measures.front().notes = {
        {.id = object_id_t{20}, .position = {measure_id_t{3}, 360}, .lane = lane_id_t{1}},
        {.id = object_id_t{21}, .position = {measure_id_t{3}, 840}, .lane = lane_id_t{1}}};
    const auto original = song.charts.front();
    const auto result = export_cut_transform_t::from(song, {});
    check_cut_success(result);
    const auto& chart = result.value->document.charts.front();
    const auto* retained = note_with_id(chart, object_id_t{21});
    REQUIRE(retained);
    CHECK(time_of(chart, retained->position) == signed_rational_t{3, 4});
    CHECK((!note_with_id(chart, object_id_t{20}) && result.value->document.export_cuts.empty() &&
        song.charts.front().measures.front().notes == original.measures.front().notes &&
        song.charts.front().bpms == original.bpms &&
        song.charts.front().divisions_per_standard_measure == original.divisions_per_standard_measure &&
        song.export_cuts.size() == 2));
    CHECK(std::ranges::any_of(result.diagnostics, [](const auto& item) {
        return item.severity == diagnostic_severity_t::info && item.location.object == object_id_t{20};
    }));
}
TEST_CASE("Export cuts refine fractional cut rows without rounding", "[core][export-cut][export]") {
    auto song = export_song();
    song.export_cuts.front().begin_milliseconds = 1;
    song.export_cuts.front().end_milliseconds = 2;
    song.charts.front().measures.front().notes = {
        {.id = object_id_t{20}, .position = {measure_id_t{3}, 240}, .lane = lane_id_t{1}}};
    const auto result = export_cut_transform_t::from(song, {});
    check_cut_success(result);
    const auto& chart = result.value->document.charts.front();
    const auto* retained = note_with_id(chart, object_id_t{20});
    REQUIRE(retained);
    CHECK(chart.divisions_per_standard_measure > 960);
    CHECK(time_of(chart, retained->position) == signed_rational_t{499, 1000});
}
TEST_CASE("Export cuts retain exact rational clocks beyond a billion divisions", "[core][export-cut][export]") {
    auto song = export_song();
    auto& source = song.charts.front();
    source.bpms.front().beats_per_minute = {381024000, 2880079};
    source.beat_zero_audio_offset_seconds = {5539, 44100};
    for (std::uint64_t id = 6; id < 15; ++id)
        source.measures.push_back({.id = measure_id_t{id}});
    const local_position_t position{source.measures.back().id, 10};
    source.measures.back().notes = {{.id = object_id_t{20}, .position = position, .lane = lane_id_t{1}}};
    song.export_cuts.front().begin_milliseconds = 7382;
    song.export_cuts.front().end_milliseconds = 14638;
    const auto timeline = export_cut_timeline_t::from(song.export_cuts);
    const auto expected = timeline.value.collapsed_seconds(time_of(source, position));
    const auto result = export_cut_transform_t::from(song, {});
    for (const auto& diagnostic : result.diagnostics) UNSCOPED_INFO(diagnostic.message);
    check_cut_success(result);
    check_rational_cut_chart(result.value->document.charts.front(), expected);
    CHECK(source.divisions_per_standard_measure == 960);
    CHECK(source.warps.empty());
}
TEST_CASE("Export cuts reject holds crossing boundaries and unknown sounding extents", "[core][export-cut][export]") {
    auto song = export_song();
    song.charts.front().measures.front().notes = {{.id = object_id_t{20},
        .position = {measure_id_t{3}, 120}, .lane = lane_id_t{1}, .kind = note_kind_t::hold,
        .end = local_position_t{measure_id_t{3}, 600}}};
    SECTION("hold") {
        const auto result = export_cut_transform_t::from(song, {});
        CHECK((!result.ok() && !result.value));
    }
    SECTION("unknown audio") {
        auto& note = song.charts.front().measures.front().notes.front();
        note.end.reset();
        note.kind = note_kind_t::tap;
        note.sound = resource_id_t{40};
        const auto result = export_cut_transform_t::from(song, {});
        CHECK_FALSE(result.ok());
        REQUIRE((!result.diagnostics.empty() && result.diagnostics.front().location.object == note.id));
    }
}
TEST_CASE("Export cuts carry BPM state and rebase different charts on one audio clock", "[core][export-cut][export]") {
    auto song = export_song();
    auto slower = song.charts.front();
    slower.id = chart_id_t{6};
    slower.bpms.front().beats_per_minute = {60, 1};
    slower.measures.front().notes = {{.id = object_id_t{22}, .position = {measure_id_t{3}, 480}, .lane = lane_id_t{1}}};
    song.charts.front().measures.front().notes = {{.id = object_id_t{20}, .position = {measure_id_t{3}, 720}, .lane = lane_id_t{1}}};
    song.charts.push_back(slower);
    const auto result = export_cut_transform_t::from(song, {});
    check_cut_success(result);
    for (const auto& chart : result.value->document.charts) {
        const auto id = chart.id == chart_id_t{2} ? object_id_t{20} : object_id_t{22};
        const auto* note = note_with_id(chart, id);
        REQUIRE(note);
        const auto expected = chart.id == chart_id_t{2} ? signed_rational_t{1, 1} : signed_rational_t{3, 2};
        CHECK(time_of(chart, note->position) == expected);
    }
}
TEST_CASE("No-cut transformation preserves all authored chart data", "[core][export-cut][export]") {
    auto song = export_song();
    song.export_cuts.clear();
    const auto result = export_cut_transform_t::from(song, {});
    check_cut_success(result);
    REQUIRE(result.value->document.charts.size() == song.charts.size());
    const auto& chart = result.value->document.charts.front();
    CHECK((chart.id == song.charts.front().id && chart.bpms == song.charts.front().bpms &&
        chart.divisions_per_standard_measure == song.charts.front().divisions_per_standard_measure));
    check_measure_identity(chart, song.charts.front());
    CHECK(result.value->audio_plans.empty());
}
TEST_CASE("Export cuts split SPEED ramps without changing their original progression", "[core][export-cut][export]") {
    auto song = export_song();
    song.charts.front().speeds = {{.id = object_id_t{30}, .position = {measure_id_t{3}, 0},
        .multiplier = {3, 1}, .transition_beats = {4, 1}}};
    const auto result = export_cut_transform_t::from(song, {});
    check_cut_success(result);
    const auto& chart = result.value->document.charts.front();
    const auto timing = timing_index_t::from(chart);
    REQUIRE(timing.value);
    const auto curves = scroll_speed_index_t::from(chart, *timing.value);
    REQUIRE(curves.value);
    const auto at_seam = timing.value->row_at({1, 2});
    REQUIRE(at_seam.absolute_row);
    const auto state = scroll_speed_state_t::from(*curves.value, *timing.value, *at_seam.absolute_row);
    REQUIRE(state);
    CHECK(state->value.speed_multiplier == signed_rational_t{2, 1});
}
TEST_CASE("Export cuts remove a complete stop while retaining the following event clock", "[core][export-cut][export]") {
    auto song = export_song();
    song.charts.front().pauses = {{.id = object_id_t{25}, .position = {measure_id_t{3}, 240},
        .kind = pause_kind_t::stop, .duration = {1, 2}}};
    song.charts.front().measures.front().notes = {{.id = object_id_t{20},
        .position = {measure_id_t{3}, 480}, .lane = lane_id_t{1}}};
    const auto result = export_cut_transform_t::from(song, {});
    check_cut_success(result);
    const auto& chart = result.value->document.charts.front();
    CHECK(chart.pauses.empty());
    const auto* note = note_with_id(chart, object_id_t{20});
    REQUIRE(note);
    CHECK(time_of(chart, note->position) == signed_rational_t{1, 1});
}
TEST_CASE("Export cuts retain the BPM state at their seam", "[core][export-cut][export]") {
    auto song = export_song();
    song.charts.front().bpms.push_back({.id = object_id_t{26}, .position = {measure_id_t{3}, 360},
        .beats_per_minute = {60, 1}});
    song.charts.front().measures.front().notes = {{.id = object_id_t{20},
        .position = {measure_id_t{3}, 480}, .lane = lane_id_t{1}}};
    const auto result = export_cut_transform_t::from(song, {});
    check_cut_success(result);
    const auto& chart = result.value->document.charts.front();
    const auto* note = note_with_id(chart, object_id_t{20});
    REQUIRE(note);
    CHECK(time_of(chart, note->position) == signed_rational_t{3, 4});
}
TEST_CASE("Export cuts inside a stop or delay retain its exact remaining duration", "[core][export-cut][export]") {
    auto song = export_song();
    song.charts.front().pauses = {{.id = object_id_t{25}, .position = {measure_id_t{3}, 240},
        .kind = GENERATE(pause_kind_t::stop, pause_kind_t::delay), .duration = {1, 1}}};
    song.charts.front().measures.front().notes = {{.id = object_id_t{20},
        .position = {measure_id_t{3}, 480}, .lane = lane_id_t{1}}};
    song.export_cuts.front().begin_milliseconds = 750;
    const auto result = export_cut_transform_t::from(song, {});
    check_cut_success(result);
    const auto& chart = result.value->document.charts.front();
    REQUIRE(chart.pauses.size() == 1);
    CHECK(chart.pauses.front().duration == signed_rational_t{3, 4});
    const auto* note = note_with_id(chart, object_id_t{20});
    REQUIRE(note);
    CHECK(time_of(chart, note->position) == signed_rational_t{7, 4});
}
TEST_CASE("Cut music is planned once on the canonical clock and anchors follow it", "[core][export-cut][export]") {
    auto song = export_song();
    song.audio_assets = {{.id = asset_id_t{60}, .source_path = "music.wav",
        .stream = {.sample_rate = 44100, .channels = 2, .frame_count = 176400, .seekable = true}}};
    song.resources = {{.id = resource_id_t{61}, .name = "music.wav", .asset = asset_id_t{60},
        .roles = {resource_role_t::music}}};
    song.charts.front().timing_anchors = {{.id = object_id_t{70}, .position = {measure_id_t{3}, 720},
        .source_sample_frame = 66150, .locked = true}};
    const auto result = export_cut_transform_t::from(song, {});
    check_cut_success(result);
    REQUIRE(result.value->audio_plans.size() == 1);
    CHECK((result.value->audio_plans.front().asset == asset_id_t{60} &&
        result.value->audio_plans.front().output_frames == 168000 &&
        result.value->document.resources.front().asset != asset_id_t{60}));
    REQUIRE(result.value->document.charts.front().timing_anchors.size() == 1);
    CHECK((result.value->document.charts.front().timing_anchors.front().source_sample_frame == 48000 &&
        song.audio_assets.front().stream.sample_rate == 44100 &&
        song.charts.front().timing_anchors.front().source_sample_frame == 66150));
}
namespace {
auto storyboard_cut_song() -> song_document_t {
    auto song = export_song();
    storyboard_document_t storyboard{.id = storyboard_id_t{80}, .name = "Test", .model = storyboard_document_model_t{}};
    storyboard.model->sprites = {{.stable_id = 1, .path = "image.png", .nodes = {{
        .kind = storyboard_node_kind_t::command, .command = {.start_time_ms = 1500, .end_time_ms = 1750,
            .from = {0, 0, 0, 0}, .to = {1, 0, 0, 0}, .value_count = 1}}}}};
    song.storyboards.push_back(storyboard);
    return song;
}
}
TEST_CASE("Storyboard visual envelopes rebase with export cuts", "[core][export-cut][export]") {
    const auto result = export_cut_transform_t::from(storyboard_cut_song(), {});
    check_cut_success(result);
    const auto& command = result.value->document.storyboards.front().model->sprites.front().nodes.front().command;
    CHECK((command.start_time_ms == 1000 && command.end_time_ms == 1250));
}
TEST_CASE("Storyboard visual envelopes crossing export cuts reject", "[core][export-cut][export]") {
    auto song = storyboard_cut_song();
    song.storyboards.front().model->sprites.front().nodes.front().command.start_time_ms = 250;
    const auto result = export_cut_transform_t::from(song, {});
    CHECK((!result.ok() && !result.value));
}
TEST_CASE("Video cues fully bounded by replacement inside an export cut drop with a diagnostic", "[core][export-cut][export]") {
    auto song = export_song();
    song.visual_assets = {{.id = asset_id_t{80}, .kind = asset_kind_t::video, .source_path = "clip.mp4"},
        {.id = asset_id_t{82}, .kind = asset_kind_t::image, .source_path = "next.png"}};
    song.resources = {{.id = resource_id_t{81}, .name = "clip.mp4", .asset = asset_id_t{80}, .roles = {resource_role_t::bga}},
        {.id = resource_id_t{83}, .name = "next.png", .asset = asset_id_t{82}, .roles = {resource_role_t::bga}}};
    song.charts.front().bga_events = {{.id = object_id_t{84}, .position = {measure_id_t{3}, 240}, .resource = resource_id_t{81}},
        {.id = object_id_t{85}, .position = {measure_id_t{3}, 480}, .resource = resource_id_t{83}}};
    const auto result = export_cut_transform_t::from(song, {});
    check_cut_success(result);
    const auto& events = result.value->document.charts.front().bga_events;
    REQUIRE(events.size() == 1);
    CHECK(events.front().id == object_id_t{85});
    CHECK(std::ranges::any_of(result.diagnostics, [](const auto& item) {
        return item.severity == diagnostic_severity_t::info && item.location.object == object_id_t{84};
    }));
}
namespace {
auto warped_cut_song(std::int64_t begin, std::int64_t end) -> song_document_t {
    auto song = export_song();
    song.export_cuts.front().begin_milliseconds = begin;
    song.export_cuts.front().end_milliseconds = end;
    song.charts.front().warps = {{.id = object_id_t{90},
        .span = {{measure_id_t{3}, 240}, {measure_id_t{3}, 480}}}};
    song.charts.front().measures.front().notes = {{.id = object_id_t{91},
        .position = {measure_id_t{3}, 360}, .lane = lane_id_t{1}}};
    return song;
}
}
TEST_CASE("A half-open cut ending at warp time retains its interior notes", "[core][export-cut][export]") {
    const auto result = export_cut_transform_t::from(warped_cut_song(250, 500), {});
    check_cut_success(result);
    const auto& chart = result.value->document.charts.front();
    const auto* note = note_with_id(chart, object_id_t{91});
    REQUIRE(note);
    CHECK(time_of(chart, note->position) == signed_rational_t{1, 4});
}
TEST_CASE("A cut beginning at warp time removes its interior notes", "[core][export-cut][export]") {
    const auto result = export_cut_transform_t::from(warped_cut_song(500, 750), {});
    check_cut_success(result);
    CHECK_FALSE(note_with_id(result.value->document.charts.front(), object_id_t{91}));
}
namespace {
auto speed_at_note(const chart_document_t& chart, const note_t& note) -> signed_rational_t {
    const auto timing = timing_index_t::from(chart);
    const auto measures = measure_index_t::from(chart);
    REQUIRE((timing.value && measures));
    const auto curves = scroll_speed_index_t::from(chart, *timing.value);
    const auto row = measures->row_of(note.position);
    REQUIRE((curves.value && row));
    const auto value = scroll_speed_state_t::from(*curves.value, *timing.value,
        {static_cast<std::int64_t>(*row), 1});
    REQUIRE(value);
    return value->value.speed_multiplier;
}
}
TEST_CASE("Beat SPEED carry inside a retained warp refuses with the authored target", "[core][export-cut][export]") {
    auto song = warped_cut_song(250, 500);
    song.charts.front().speeds = {{.id = object_id_t{92}, .position = {measure_id_t{3}, 0},
        .multiplier = {3, 1}, .transition_beats = {4, 1}}};
    const auto result = export_cut_transform_t::from(song, {});
    CHECK((!result.ok() && !result.value));
    CHECK(std::ranges::any_of(result.diagnostics, [](const auto& item) {
        return item.location.object == object_id_t{92} && item.message.find("warp or stop phase") != std::string::npos;
    }));
}
TEST_CASE("Beat SPEED ramps retain warp progression when the carry is outside the warp", "[core][export-cut][export]") {
    auto song = warped_cut_song(0, 250);
    song.charts.front().speeds = {{.id = object_id_t{92}, .position = {measure_id_t{3}, 0},
        .multiplier = {3, 1}, .transition_beats = {4, 1}}};
    const auto result = export_cut_transform_t::from(song, {});
    check_cut_success(result);
    const auto& chart = result.value->document.charts.front();
    const auto* note = note_with_id(chart, object_id_t{91});
    REQUIRE(note);
    const auto speed = speed_at_note(chart, *note);
    CHECK((speed.numerator == 7 && speed.denominator == 4));
}
TEST_CASE("Constant SPEED transitions remain representable at a retained warp seam", "[core][export-cut][export]") {
    auto song = warped_cut_song(250, 500);
    song.charts.front().speeds = {{.id = object_id_t{92}, .position = {measure_id_t{3}, 0},
        .multiplier = {1, 1}, .transition_beats = {4, 1}}};
    const auto result = export_cut_transform_t::from(song, {});
    check_cut_success(result);
}

TEST_CASE("Generated cut music reserves authored and planned asset basenames", "[core][export-cut][export]") {
    auto song = export_song();
    song.audio_assets = {{.id = asset_id_t{60}, .source_path = "export-cut-music-60.mp3",
        .stream = {.sample_rate = 48000, .channels = 2, .frame_count = 192000, .seekable = true}}};
    song.visual_assets = {{.id = asset_id_t{62}, .source_path = "export-cut-music-60-1.mp3"}};
    song.resources = {{.id = resource_id_t{61}, .name = "export-cut-music-60.mp3", .asset = asset_id_t{60},
        .roles = {resource_role_t::music}}};
    const auto result = export_cut_transform_t::from(song, {});
    check_cut_success(result);
    REQUIRE(result.value->audio_plans.size() == 1);
    CHECK(result.value->audio_plans.front().filename == "export-cut-music-60-2.mp3");
    CHECK(song.audio_assets.front().source_path == "export-cut-music-60.mp3");
}
TEST_CASE("Export audio defaults cap source bitrate and retain explicit encoding choices", "[core][export-cut][export]") {
    const auto [source, expected] = GENERATE(Catch::Generators::table<std::uint32_t, std::uint32_t>({
        {128'000, 128}, {320'000, 192}, {0, 192}}));
    export_audio_settings_t settings;
    CHECK(settings.codec == export_audio_codec_t::mp3);
    CHECK(settings.bitrate_for(source) == expected);
    settings.mp3_bitrate_kbps = 320;
    CHECK(settings.is_valid());
    CHECK(settings.bitrate_for(128'000) == 320);
}
TEST_CASE("Export audio rejects unsupported bitrate choices", "[core][export-cut][export]") {
    export_audio_settings_t settings;
    settings.mp3_bitrate_kbps = 321;
    CHECK_FALSE(settings.is_valid());
}
TEST_CASE("Standard export bars preserve exact object rows across a long measure", "[core][export-cut][export]") {
    auto song = export_song();
    auto& chart = song.charts.front();
    chart.measures.resize(1);
    chart.measures.front().definition = {{2, 1}, std::nullopt};
    chart.measures.front().notes = {{.id = object_id_t{20},
        .position = {measure_id_t{3}, 1440}, .lane = lane_id_t{1}}};
    const auto diagnostics = rebar_standard_measures(chart);
    REQUIRE(diagnostics.empty());
    REQUIRE(chart.measures.size() == 2);
    const auto* note = note_with_id(chart, object_id_t{20});
    REQUIRE(note);
    CHECK(time_of(chart, note->position) == signed_rational_t{3, 1});
    CHECK(std::ranges::all_of(chart.measures, [](const auto& measure) {
        return measure.definition.length == rational_t{1, 1};
    }));
}
