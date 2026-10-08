#include <wormedit/formats/osu_mania.h>
#include <wormedit/formats/native_project.h>
#include <wormedit/core/timing.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

auto bytes(std::string_view source) -> std::vector<std::byte> {
    std::vector<std::byte> result;
    result.reserve(source.size());
    for (const auto character : source) {
        result.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
    }
    return result;
}

auto read_fixture(std::string_view name) -> std::vector<std::byte> {
    std::ifstream stream(std::filesystem::path(WORMEDIT_FIXTURE_DIR) / "osu" / name,
                         std::ios::binary);
    const std::string source{std::istreambuf_iterator<char>(stream),
                             std::istreambuf_iterator<char>()};
    return bytes(source);
}

auto fixture(std::string_view name) -> wormedit::osu_mania_parse_result_t {
    return wormedit::parse_osu_mania_bytes(read_fixture(name));
}

auto parse_inline(std::string_view source) -> wormedit::osu_mania_parse_result_t {
    return wormedit::parse_osu_mania_bytes(bytes(source));
}

auto timing_fixture(std::string_view timing, std::string_view hits,
                     std::string_view version = "14") -> std::string {
    return "osu file format v" + std::string(version) +
           "\n[General]\nMode:3\n[Difficulty]\nCircleSize:4\n[TimingPoints]\n" +
           std::string(timing) + "[HitObjects]\n" + std::string(hits);
}

auto source_note(const wormedit::chart_document_t& chart, std::string_view prefix)
    -> const wormedit::note_t* {
    for (const auto& measure : chart.measures)
        for (const auto& note : measure.notes)
            if (note.provenance.source_spelling.starts_with(prefix)) return &note;
    return nullptr;
}

auto timing_errors(const wormedit::timing_build_result_t& built) -> std::string {
    std::string result;
    for (const auto& diagnostic : built.diagnostics) result += diagnostic.message + "\n";
    return result;
}

void check_core_note_time(const wormedit::chart_document_t& chart, std::string_view source_prefix,
                          long double milliseconds, long double tolerance) {
    const auto built = wormedit::build_timing_index(chart);
    INFO(timing_errors(built));
    REQUIRE(built);
    const auto indexed = wormedit::build_measure_index(chart);
    const auto* measures = std::get_if<wormedit::measure_index_t>(&indexed);
    REQUIRE(measures);
    const auto* note = source_note(chart, source_prefix);
    REQUIRE(note);
    const auto time = wormedit::timing_at(*built.index, *measures, note->position);
    REQUIRE(time);
    const auto actual_ms = static_cast<long double>(time.seconds->numerator) * 1000.0L /
                           time.seconds->denominator;
    CHECK(std::fabs(actual_ms - milliseconds) <= tolerance);
    if (actual_ms >= 0) CHECK(wormedit::sample_frame_at(*time.seconds, 48'000));
}

auto native_reopen(const wormedit::song_document_t& song) -> wormedit::song_document_t {
    const auto written = wormedit::write_native_project(song);
    REQUIRE(written.bytes);
    auto parsed = wormedit::parse_native_project(*written.bytes);
    REQUIRE(parsed.song);
    return std::move(*parsed.song);
}

void check_hold_endpoint(const wormedit::note_t& note, const wormedit::measure_index_t& measures,
                          const wormedit::timing_index_t& timing) {
    REQUIRE(note.end);
    CHECK(wormedit::absolute_row(measures, *note.end) > wormedit::absolute_row(measures, note.position));
    CHECK(wormedit::timing_at(timing, measures, *note.end));
}

void check_positive_holds(const wormedit::chart_document_t& chart, std::size_t expected) {
    const auto timing = wormedit::build_timing_index(chart);
    REQUIRE(timing);
    const auto indexed = wormedit::build_measure_index(chart);
    const auto* measures = std::get_if<wormedit::measure_index_t>(&indexed);
    REQUIRE(measures);
    std::size_t count{};
    for (const auto& measure : chart.measures)
        for (const auto& note : measure.notes) {
            check_hold_endpoint(note, *measures, *timing.index);
            ++count;
        }
    CHECK(count == expected);
}

void check_simple_bpm(std::string_view beat, std::int64_t expected) {
    const auto source = timing_fixture("0," + std::string(beat) + ",4,0,0,100,1,0\n",
                                      "64,192,1000,1,0,0:0:0:0:\n");
    const auto parsed = parse_inline(source);
    REQUIRE(parsed.song);
    const auto& chart = parsed.song->charts.front();
    REQUIRE(chart.bpms.size() == 1);
    CHECK((chart.bpms.front().beats_per_minute == wormedit::signed_rational_t{expected, 1}));
    CHECK(wormedit::build_timing_index(chart));
}

auto write_inline(const wormedit::song_document_t& song,
                  wormedit::osu_mania_write_options_t options = {})
    -> wormedit::osu_mania_write_result_t {
    return wormedit::write_osu_mania(song, options);
}

void check_origin_roundtrip(int origin) {
    const auto time = std::to_string(origin);
    const auto source = timing_fixture(time + ",500,4,0,0,100,1,0\n",
        "64,192," + time + ",1,0,0:0:0:0:\n");
    const auto parsed = parse_inline(source);
    REQUIRE(parsed.song);
    check_core_note_time(parsed.song->charts.front(), "64,192," + time + ",", origin, 1.0e-9L);
    const auto reopened = native_reopen(*parsed.song);
    CHECK(reopened.charts.front().beat_zero_audio_offset_seconds ==
          parsed.song->charts.front().beat_zero_audio_offset_seconds);
    const auto written = write_inline(reopened);
    REQUIRE(written.text);
    const auto reimported = parse_inline(*written.text);
    REQUIRE(reimported.song);
    check_core_note_time(reimported.song->charts.front(), "64,192," + time + ",", origin, 1.0e-9L);
}

void check_zero_origin_output(const std::string& text) {
    const auto reimported = parse_inline(text);
    REQUIRE(reimported.song);
    CHECK(reimported.song->charts.front().beat_zero_audio_offset_seconds.numerator == 0);
    check_core_note_time(reimported.song->charts.front(), "64,192,0,", 0, 1.0e-9L);
}

constexpr std::string_view rewiring_source = R"osu(osu file format v14

[General]
AudioFilename: music.ogg
Mode: 3
PreviewTime: 12500

[Editor]
Bookmarks: 1000,2500

[Metadata]
Title:Rewiring check
Artist:Wormedit
Creator:Test author
Version:Timing reset fixture
Tags:one two-three
UnknownField:retain me

[Difficulty]
HPDrainRate:5
CircleSize:4
OverallDifficulty:7
ApproachRate:9

[Events]
0,0,"background.jpg",0,0
Video,1000,"background.mp4",0,0

[TimingPoints]
0,500,4,2,1,70,1,0
1000,-50,4,3,2,65,0,9
1500,500,2,2,1,70,1,0
2499,400,2,2,1,70,1,0
2499,400,3,2,1,70,1,0

[HitObjects]
64,192,-500,1,0,0:0:0:0:
128,192,1000,1,0,0:0:0:0:
192,192,1400,128,0,2500:0:0:0:
256,192,1500,1,0,0:0:0:0:
320,192,2499,1,0,0:0:0:0:
384,192,2500,1,0,0:0:0:0:
)osu";

auto has_code(const wormedit::osu_mania_parse_result_t& result,
              wormedit::osu_mania_parse_diagnostic_code_t code) -> bool {
    return std::ranges::any_of(result.diagnostics, [code](const auto& diagnostic) {
        return diagnostic.code == code &&
               diagnostic.severity == wormedit::diagnostic_severity_t::error;
    });
}

auto position_for_source(const wormedit::chart_document_t& chart, std::string_view prefix)
    -> std::optional<wormedit::local_position_t> {
    for (const auto& measure : chart.measures) {
        const auto found = std::ranges::find_if(measure.notes, [prefix](const auto& note) {
            return note.provenance.source_spelling.starts_with(prefix);
        });
        if (found != measure.notes.end())
            return found->position;
    }
    return std::nullopt;
}

auto position_for_source_time(const wormedit::chart_document_t& chart, std::int64_t time)
    -> std::optional<wormedit::local_position_t> {
    const auto marker = ",192," + std::to_string(time) + ",";
    for (const auto& measure : chart.measures) {
        const auto found = std::ranges::find_if(measure.notes, [&](const auto& note) {
            return note.provenance.source_spelling.find(marker) != std::string::npos;
        });
        if (found != measure.notes.end())
            return found->position;
    }
    return std::nullopt;
}

auto lane_for_source(const wormedit::chart_document_t& chart, std::string_view prefix)
    -> std::optional<wormedit::lane_id_t> {
    for (const auto& measure : chart.measures) {
        const auto found = std::ranges::find_if(measure.notes, [prefix](const auto& note) {
            return note.provenance.source_spelling.starts_with(prefix);
        });
        if (found != measure.notes.end())
            return found->lane;
    }
    return std::nullopt;
}

auto lane_for_source_time(const wormedit::chart_document_t& chart, std::int64_t time)
    -> std::optional<wormedit::lane_id_t> {
    const auto marker = ",192," + std::to_string(time) + ",";
    for (const auto& measure : chart.measures) {
        const auto found = std::ranges::find_if(measure.notes, [&](const auto& note) {
            return note.provenance.source_spelling.find(marker) != std::string::npos;
        });
        if (found != measure.notes.end())
            return found->lane;
    }
    return std::nullopt;
}

auto lane_near_source_time(const wormedit::chart_document_t& chart, std::int64_t x,
                          std::int64_t time, std::int64_t maximum_error)
    -> std::optional<wormedit::lane_id_t> {
    const auto prefix = std::to_string(x) + ",192,";
    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes) {
            const auto& source = note.provenance.source_spelling;
            if (!source.starts_with(prefix)) continue;
            const auto begin = prefix.size();
            const auto end = source.find(',', begin);
            if (end == std::string::npos) continue;
            std::int64_t candidate{};
            const auto parsed = std::from_chars(source.data() + begin, source.data() + end,
                                                candidate);
            if (parsed.ec == std::errc{} && parsed.ptr == source.data() + end &&
                std::llabs(candidate - time) <= maximum_error)
                return note.lane;
        }
    }
    return std::nullopt;
}

auto note_source_times(const wormedit::chart_document_t& chart) -> std::vector<std::int64_t> {
    std::vector<std::int64_t> result;
    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes) {
            const auto& source = note.provenance.source_spelling;
            const auto first = source.find(',');
            const auto second = first == std::string::npos ? first : source.find(',', first + 1);
            const auto begin = second == std::string::npos ? second : second + 1;
            const auto end = begin == std::string::npos ? begin : source.find(',', begin);
            if (begin == std::string::npos || end == std::string::npos)
                continue;
            std::int64_t time{};
            const auto parsed = std::from_chars(source.data() + begin, source.data() + end, time);
            if (parsed.ec == std::errc{} && parsed.ptr == source.data() + end)
                result.push_back(time);
        }
    }
    std::ranges::sort(result);
    return result;
}

void check_quantized_source_times(const wormedit::chart_document_t& before,
                                  const wormedit::chart_document_t& after) {
    const auto original = note_source_times(before);
    const auto exported = note_source_times(after);
    REQUIRE(exported.size() == original.size());
    for (std::size_t index = 0; index < original.size(); ++index)
        CHECK(std::llabs(exported[index] - original[index]) <= 1);
}

auto position_near_source_time(const wormedit::chart_document_t& chart, std::int64_t time)
    -> std::optional<wormedit::local_position_t> {
    const auto exact = position_for_source_time(chart, time);
    return exact ? exact : position_for_source_time(chart, time + 1);
}

} // namespace

TEST_CASE("osu!mania import rewires measures at effective timing points only",
          "[unit][format][osu-mania]") {
    const auto result = wormedit::parse_osu_mania_bytes(bytes(rewiring_source));
    REQUIRE(result.song.has_value());
    CHECK_FALSE(result.has_errors());
    REQUIRE(result.song->charts.size() == 1);
    const auto& chart = result.song->charts.front();
    CHECK(chart.source_family == wormedit::format_family_t::osu_mania);
    CHECK(chart.divisions_per_standard_measure >= 960);
    REQUIRE(chart.measures.size() == 4);
    CHECK((chart.measures[1].definition.length == wormedit::rational_t{3, 4}));
    CHECK_FALSE(chart.measures[1].definition.meter.has_value());
    CHECK((chart.measures[2].definition.length == wormedit::rational_t{1, 2}));
    CHECK((chart.measures[2].definition.meter ==
           std::optional<wormedit::meter_t>{wormedit::meter_t{2, 4}}));
    CHECK((chart.measures[3].definition.length == wormedit::rational_t{3, 4}));
    REQUIRE(chart.measures[3].definition.meter.has_value());
    CHECK((*chart.measures[3].definition.meter == wormedit::meter_t{3, 4}));

    const auto at_zero = position_for_source(chart, "128,192,1000,");
    const auto at_first_reset = position_for_source(chart, "256,192,1500,");
    const auto at_second_reset = position_for_source(chart, "320,192,2499,");
    const auto after_second_reset = position_for_source(chart, "384,192,2500,");
    REQUIRE(at_zero.has_value());
    REQUIRE(at_first_reset.has_value());
    REQUIRE(at_second_reset.has_value());
    REQUIRE(after_second_reset.has_value());
    CHECK(at_zero->measure == chart.measures[1].id);
    CHECK(at_first_reset->measure == chart.measures[2].id);
    CHECK(at_first_reset->row == 0);
    CHECK(at_second_reset->measure == chart.measures[3].id);
    CHECK(at_second_reset->row == 0);
    CHECK(after_second_reset->measure == chart.measures[3].id);
    CHECK(after_second_reset->row > 0);

    const auto negative_note = position_for_source(chart, "64,192,-500,");
    REQUIRE(negative_note.has_value());
    CHECK(negative_note->measure == chart.measures[0].id);
    const auto spanning_hold = position_for_source(chart, "192,192,1400,");
    REQUIRE(spanning_hold.has_value());
    const auto& hold_measure =
        chart.measures[static_cast<std::size_t>(spanning_hold->measure.value - 1)];
    const auto hold = std::ranges::find_if(hold_measure.notes, [](const auto& note) {
        return note.provenance.source_spelling.starts_with("192,192,1400,");
    });
    REQUIRE(hold != hold_measure.notes.end());
    REQUIRE(hold->end.has_value());
    CHECK(hold->end->measure == chart.measures[3].id);

    REQUIRE(chart.scrolls.size() == 1);
    CHECK(chart.scrolls.front().position.measure == chart.measures[1].id);
    CHECK((chart.scrolls.front().multiplier == wormedit::signed_rational_t{2, 1}));
    REQUIRE(chart.bpms.size() == 5);
    CHECK(chart.bpms[0].position.measure == chart.measures[1].id);
    CHECK(chart.bpms[1].position.measure == chart.measures[2].id);
    CHECK(chart.bpms[2].position == chart.bpms[3].position);
    CHECK((chart.bpms[2].beats_per_minute == wormedit::signed_rational_t{150, 1}));
    REQUIRE(chart.kiai.size() == 3);
    CHECK(chart.kiai[1].enabled);
    CHECK(chart.kiai[1].retained_effect_bits == 9);
    CHECK_FALSE(chart.kiai[2].enabled);

    REQUIRE(chart.bookmarks.size() == 2);
    CHECK(chart.bookmarks[0].anchor == wormedit::marker_anchor_t::audio_time);
    CHECK(chart.bookmarks[0].audio_milliseconds == 1000);
    CHECK(chart.bookmarks[1].audio_milliseconds == 2500);
    CHECK(result.song->metadata.ordered_search_tags ==
          std::vector<std::string>{"one", "two-three"});
    CHECK(result.song->metadata.title == "Rewiring check");
    CHECK(chart.metadata.name == "Timing reset fixture");
    REQUIRE(chart.lane_layouts.size() == 1);
    CHECK(chart.lane_layouts.front().layout.lanes.size() == 4);
    CHECK(std::ranges::any_of(
        chart.metadata.format_groups.front().ordered_fields, [](const auto& field) {
            return field.first == "Metadata.UnknownField" && field.second == "retain me";
        }));
    CHECK(std::ranges::any_of(chart.metadata.format_groups.front().ordered_fields,
                              [](const auto& field) {
                                  return field.first == "osu.suppressed_bar_line_displacement_ms";
                              }));
    CHECK(std::ranges::any_of(chart.metadata.format_groups.front().ordered_fields,
                              [](const auto& field) {
                                  return field.first.ends_with(".time") && field.second == "1000";
                              }));
    CHECK(result.song->resources.size() == 3);
    CHECK(result.song->visual_assets.size() == 2);
    CHECK(chart.bga_events.size() == 2);
    const auto diagnostics = wormedit::validate_document(*result.song);
    CHECK(diagnostics.empty());
}

TEST_CASE("osu!mania accepts decimal resets and preserves positive tiny hold spans",
          "[unit][format][osu-mania][timing][quantization]") {
    constexpr std::string_view source = R"osu(osu file format v128
[General]
Mode:3
[Difficulty]
CircleSize:4
[TimingPoints]
0,500,4,0,0,100,1,0
1000.1234,499.875,4,0,0,100,1,0
2000.2345,500.125,4,0,0,100,1,0
3000.3456,499.75,4,0,0,100,1,0
[HitObjects]
64,192,1000.1,128,0,1000.2:0:0:0:
)osu";
    const auto imported = parse_inline(source);
    REQUIRE(imported.song.has_value());
    CHECK_FALSE(imported.has_errors());
    CHECK(std::ranges::any_of(imported.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == wormedit::osu_mania_parse_diagnostic_code_t::timing_quantization &&
               diagnostic.severity == wormedit::diagnostic_severity_t::warning;
    }));
    const auto& chart = imported.song->charts.front();
    CHECK(chart.divisions_per_standard_measure == 960);
    check_positive_holds(chart, 1);
    CHECK(wormedit::validate_document(*imported.song).empty());
}

TEST_CASE("osu!mania preserves colliding positive holds including the last editable row",
          "[unit][format][osu-mania][timing][quantization]") {
    const auto source = timing_fixture("0,500,4,0,0,100,1,0\n",
        "64,192,20.1,128,0,20.2:0:0:0:\n"
        "128,192,1999.8,128,0,1999.9:0:0:0:\n", "128");
    const auto parsed = parse_inline(source);
    REQUIRE(parsed.song);
    CHECK(wormedit::validate_document(*parsed.song).empty());
    const auto& chart = parsed.song->charts.front();
    REQUIRE(chart.measures.size() == 2);
    check_positive_holds(chart, 2);
    check_core_note_time(chart, "64,192,20.1,", 20.1L, 3.0L);
    check_core_note_time(chart, "128,192,1999.8,", 1999.8L, 3.0L);
    const auto limited = wormedit::parse_osu_mania_bytes(bytes(source), {.max_measures = 1});
    CHECK_FALSE(limited.song);
    CHECK(has_code(limited, wormedit::osu_mania_parse_diagnostic_code_t::measure_limit));
}

TEST_CASE("osu!mania retains source origin and initializes a prefix with the source tempo",
          "[unit][format][osu-mania][timing][native-project]") {
    const auto source = timing_fixture("1000,400,4,0,0,100,1,0\n",
        "64,192,0,1,0,0:0:0:0:\n128,192,1000,1,0,0:0:0:0:\n");
    const auto parsed = parse_inline(source);
    REQUIRE(parsed.song);
    const auto& chart = parsed.song->charts.front();
    CHECK((chart.beat_zero_audio_offset_seconds == wormedit::signed_rational_t{-3, 5}));
    const auto initial = std::ranges::find_if(chart.bpms, [&](const auto& bpm) {
        return bpm.position == wormedit::local_position_t{chart.measures.front().id, 0};
    });
    REQUIRE(initial != chart.bpms.end());
    CHECK((initial->beats_per_minute == wormedit::signed_rational_t{150, 1}));
    check_core_note_time(chart, "64,192,0,", 0, 1.0e-9L);
    check_core_note_time(chart, "128,192,1000,", 1000, 1.0e-9L);
    const auto reopened = native_reopen(*parsed.song);
    check_core_note_time(reopened.charts.front(), "64,192,0,", 0, 1.0e-9L);
}

TEST_CASE("osu!mania source origins survive native and osu writing without legacy inference",
          "[unit][format][osu-mania][timing][native-project][writer]") {
    for (const auto origin : {-1500, 0, 1000}) check_origin_roundtrip(origin);
}

TEST_CASE("osu!mania preserves ordinary exact constant BPM values",
          "[unit][format][osu-mania][timing]") {
    check_simple_bpm("500", 120);
    check_simple_bpm("400", 150);
    check_simple_bpm("379.74683544304", 158);
}

TEST_CASE("osu!mania bounds many short decimal measures without losing a positive hold",
          "[unit][format][osu-mania][timing][quantization]") {
    std::string timing;
    for (int index = 0; index < 256; ++index)
        timing += std::to_string(index * 7.123456789) + "," +
                  std::to_string(500.0 + index * 0.0003) + ",4,0,0,100,1,0\n";
    const auto parsed = parse_inline(timing_fixture(timing,
        "64,192,0,1,0,0:0:0:0:\n192,192,2000,128,0,2001:0:0:0:\n"));
    REQUIRE(parsed.song);
    const auto& chart = parsed.song->charts.front();
    CHECK(chart.divisions_per_standard_measure == 960);
    CHECK(chart.measures.size() >= 256);
    CHECK(wormedit::validate_document(*parsed.song).empty());
    check_core_note_time(chart, "64,192,0,", 0, 1.0e-9L);
    check_core_note_time(chart, "192,192,2000,", 2000, 3.0L);
}

TEST_CASE("osu!mania fast and near-stop reset geometry has bounded playback timing",
          "[unit][format][osu-mania][timing][quantization]") {
    constexpr std::string_view timing =
        "-1518,379.74683544304,4,2,0,10,1,0\n"
        "82222.110759493,0.059990521497603,4,2,0,10,1,0\n"
        "82223.070607837,379.74683544304,4,2,0,10,1,0\n"
        "106710.83112752,60000000,4,2,0,10,1,0\n"
        "107470.3247984,379.74683544304,4,2,0,10,1,0\n";
    const auto parsed = parse_inline(timing_fixture(timing,
        "64,192,82223,1,0,0:0:0:0:\n"
        "128,192,106716,1,0,0:0:0:0:\n"
        "192,192,106715,128,0,107660:0:0:0:\n"));
    REQUIRE(parsed.song);
    CHECK(wormedit::validate_document(*parsed.song).empty());
    const auto& chart = parsed.song->charts.front();
    CHECK(chart.divisions_per_standard_measure == 960);
    check_core_note_time(chart, "64,192,82223,", 82223, 0.001L);
    check_core_note_time(chart, "128,192,106716,", 106716, 6.0L);
    check_core_note_time(chart, "192,192,106715,", 106715, 6.0L);
    const auto written = write_inline(*parsed.song);
    REQUIRE(written.text);
    const auto reimported = parse_inline(*written.text);
    REQUIRE(reimported.song);
    CHECK(wormedit::build_timing_index(reimported.song->charts.front()));
}

TEST_CASE("osu!mania keeps zero source origin after a BPM edit with retained provenance",
          "[unit][format][osu-mania][timing][writer]") {
    auto parsed = parse_inline(timing_fixture(
        "0,500,4,0,0,100,1,0\n1000,500,4,0,0,100,1,0\n",
        "64,192,0,1,0,0:0:0:0:\n192,192,1000,1,0,0:0:0:0:\n"));
    REQUIRE(parsed.song);
    auto& chart = parsed.song->charts.front();
    REQUIRE(chart.bpms.size() == 2);
    chart.bpms.front().beats_per_minute = {240, 1};
    const auto written = write_inline(*parsed.song);
    REQUIRE(written.text);
    CHECK(written.text->find("64,192,0,1,") != std::string::npos);
    CHECK(written.text->find("192,192,500,1,") != std::string::npos);
    check_zero_origin_output(*written.text);
}

TEST_CASE("osu!mania keeps near-stop timing distinct inside a suppressed-barline measure",
          "[unit][format][osu-mania][timing][quantization]") {
    const auto parsed = parse_inline(timing_fixture(
        "0,500,4,0,0,100,1,0\n"
        "1000,60000000,4,0,0,100,1,8\n"
        "1759.493,500,4,0,0,100,1,8\n",
        "64,192,1005,1,0,0:0:0:0:\n"
        "192,192,1004,128,0,1800:0:0:0:\n"));
    REQUIRE(parsed.song);
    const auto& chart = parsed.song->charts.front();
    REQUIRE(chart.bpms.size() == 3);
    CHECK(chart.bpms[1].position != chart.bpms[2].position);
    check_core_note_time(chart, "64,192,1005,", 1005, 6.0L);
    check_core_note_time(chart, "192,192,1004,", 1004, 6.0L);
    CHECK(wormedit::validate_document(*parsed.song).empty());
}

TEST_CASE("osu!mania rejects source-grid bounds and effective timing product overflow",
          "[unit][format][osu-mania][timing][negative][bounds]") {
    for (const auto time : {"9223372036854776", "-9223372036854776"}) {
        INFO(time);
        const auto parsed = parse_inline(timing_fixture(std::string(time) + ",500,4,0,0,100,1,0\n", ""));
        CHECK_FALSE(parsed.song);
        CHECK(has_code(parsed, wormedit::osu_mania_parse_diagnostic_code_t::invalid_timing_point));
    }
    const auto large_span = parse_inline(timing_fixture(
        "-9223372036854774,10000000000000000,4,0,0,100,1,0\n"
        "9223372036854774,10000000000000000,4,0,0,100,1,0\n", ""));
    CHECK_FALSE(large_span.song);
    CHECK(has_code(large_span, wormedit::osu_mania_parse_diagnostic_code_t::invalid_timing_point));
}

TEST_CASE("osu!mania preserves exact source origins near both microsecond grid bounds",
          "[unit][format][osu-mania][timing][bounds]") {
    const std::array cases{
        std::pair{std::int64_t{-9'223'372'036'854'774},
                  wormedit::signed_rational_t{-4'611'686'018'427'387, 500}},
        std::pair{std::int64_t{9'223'372'036'854'772},
                  wormedit::signed_rational_t{2'305'843'009'213'693, 250}}};
    for (const auto& [milliseconds, expected] : cases) {
        INFO(milliseconds);
        const auto parsed = parse_inline(timing_fixture(
            std::to_string(milliseconds) + ",0.5,4,0,0,100,1,0\n", ""));
        REQUIRE(parsed.song);
        const auto& chart = parsed.song->charts.front();
        CHECK(chart.beat_zero_audio_offset_seconds == expected);
        CHECK(wormedit::validate_document(*parsed.song).empty());
    }
}

TEST_CASE("osu!mania source origins keep fractional microsecond half ties away from zero",
          "[unit][format][osu-mania][timing][bounds]") {
    const std::array cases{
        std::pair{"-1.2345", wormedit::signed_rational_t{-247, 200'000}},
        std::pair{"1.2345", wormedit::signed_rational_t{247, 200'000}},
        std::pair{"-0.0005", wormedit::signed_rational_t{-1, 1'000'000}},
        std::pair{"0.0005", wormedit::signed_rational_t{1, 1'000'000}}};
    for (const auto& [milliseconds, expected] : cases) {
        INFO(milliseconds);
        const auto parsed = parse_inline(timing_fixture(
            std::string(milliseconds) + ",500,4,0,0,100,1,0\n", ""));
        REQUIRE(parsed.song);
        CHECK(parsed.song->charts.front().beat_zero_audio_offset_seconds == expected);
    }
}

TEST_CASE("osu!mania fixtures preserve source bytes, tags, bookmarks, kiai and samples",
          "[unit][format][osu-mania]") {
    const auto result = fixture("mania-tags-bookmarks-kiai.osu");
    REQUIRE(result.song.has_value());
    CHECK_FALSE(result.has_errors());
    REQUIRE(result.song->metadata.format_groups.size() == 1);
    CHECK(result.song->metadata.format_groups.front().encoding.detected ==
          wormedit::text_encoding_t::utf8);
    CHECK(result.song->metadata.format_groups.front().encoding.unchanged_since_import);
    REQUIRE(result.song->metadata.format_groups.front().encoding.original_bytes);
    CHECK_FALSE(result.song->metadata.format_groups.front().encoding.original_bytes->empty());
    REQUIRE(result.song->charts.size() == 1);
    const auto& chart = result.song->charts.front();
    CHECK(chart.metadata.name == "4K fixture");
    CHECK(chart.metadata.author == "Wormedit");
    CHECK(result.song->metadata.ordered_search_tags ==
          std::vector<std::string>{"fixture", "semantic-roundtrip", "tags-with-hyphen"});
    REQUIRE(chart.bookmarks.size() == 3);
    CHECK(chart.bookmarks[0].audio_milliseconds == 1000);
    REQUIRE(chart.kiai.size() >= 2);
    CHECK(std::ranges::any_of(chart.kiai, [](const auto& event) {
        return event.enabled && event.retained_effect_bits == 9;
    }));
    CHECK(std::ranges::any_of(result.song->resources, [](const auto& resource) {
        return resource.name == "custom-hit.wav" &&
               std::ranges::find(resource.roles, wormedit::resource_role_t::sample) !=
                   resource.roles.end();
    }));
    REQUIRE(chart.measures.size() >= 3);
    CHECK(std::ranges::any_of(chart.measures, [](const auto& measure) {
        return std::ranges::any_of(measure.notes, [](const auto& note) {
            return note.kind == wormedit::note_kind_t::hold && note.end.has_value();
        });
    }));
    const auto diagnostics = wormedit::validate_document(*result.song);
    CHECK(diagnostics.empty());
}

TEST_CASE("osu!mania version profiles preserve stable-v14 and lazer-v128 time semantics",
          "[unit][format][osu-mania][version]") {
    const auto stable = fixture("versioned-stable-v14.osu");
    REQUIRE(stable.song.has_value());
    REQUIRE_FALSE(stable.has_errors());
    const auto& fields = stable.song->metadata.format_groups.front().ordered_fields;
    CHECK(std::ranges::find(fields, std::pair{std::string{"osu.file_format"}, std::string{"14"}}) !=
          fields.end());

    const auto written = write_inline(*stable.song);
    REQUIRE(written.text.has_value());
    CHECK(written.text->starts_with("osu file format v14\n"));

    const auto lazer = fixture("lazer-v128-fractional.osu");
    REQUIRE(lazer.song.has_value());
    REQUIRE_FALSE(lazer.has_errors());
    const auto& lazer_fields = lazer.song->metadata.format_groups.front().ordered_fields;
    CHECK(std::ranges::find(lazer_fields, std::pair{std::string{"osu.file_format"},
                                                    std::string{"128"}}) != lazer_fields.end());
    constexpr std::array expected_lazer_fields{
        std::pair{"General.AudioLeadIn", "750"},
        std::pair{"General.PreviewTime", "12500"},
        std::pair{"General.SpecialStyle", "1"},
        std::pair{"General.WidescreenStoryboard", "1"},
        std::pair{"General.SamplesMatchPlaybackRate", "1"},
        std::pair{"Editor.DistanceSpacing", "1.25"},
        std::pair{"Editor.BeatDivisor", "6"},
        std::pair{"Editor.GridSize", "8"},
        std::pair{"Editor.TimelineZoom", "2.5"},
        std::pair{"Editor.VelocityPresets", "0.5,1,2"},
        std::pair{"Metadata.TitleUnicode", "Lazer v128 metadata fixture"},
        std::pair{"Metadata.Source", "Official encoder inventory audit"},
        std::pair{"Metadata.BeatmapID", "123"},
        std::pair{"Metadata.BeatmapSetID", "456"},
        std::pair{"Difficulty.HPDrainRate", "5"},
        std::pair{"Difficulty.OverallDifficulty", "8"},
        std::pair{"Difficulty.ApproachRate", "9"},
        std::pair{"Difficulty.SliderMultiplier", "1.4"},
        std::pair{"Difficulty.SliderTickRate", "1"},
    };
    std::size_t previous_field_index{};
    for (const auto& expected : expected_lazer_fields) {
        const auto found = std::ranges::find(lazer_fields, std::pair<std::string, std::string>{expected});
        CAPTURE(expected.first, expected.second);
        REQUIRE(found != lazer_fields.end());
        const auto field_index = static_cast<std::size_t>(found - lazer_fields.begin());
        CHECK(field_index >= previous_field_index);
        previous_field_index = field_index;
    }
    REQUIRE(lazer.song->charts.front().measures.size() >= 1);
    std::size_t lazer_note_count{};
    bool found_hold{};
    for (const auto& measure : lazer.song->charts.front().measures) {
        lazer_note_count += measure.notes.size();
        found_hold = found_hold || std::ranges::any_of(measure.notes, [](const auto& note) {
                         return note.kind == wormedit::note_kind_t::hold && note.end.has_value();
                     });
    }
    CHECK(lazer_note_count == 2);
    CHECK(found_hold);

    const auto lazer_written = write_inline(*lazer.song);
    REQUIRE(lazer_written.text.has_value());
    CHECK(lazer_written.text->starts_with("osu file format v128\n"));
    CHECK(lazer_written.text->find(",192,1000.5,1,") != std::string::npos);
    CHECK(lazer_written.text->find(",192,1200.5,128,0,\"1400.5:") != std::string::npos);
    CHECK(lazer_written.text->find("VelocityPresets:0.5,1,2") != std::string::npos);
    CHECK(lazer_written.text->find("SamplesMatchPlaybackRate:1") != std::string::npos);
    CHECK_FALSE(std::ranges::any_of(lazer_written.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code ==
               wormedit::osu_mania_write_diagnostic_code_t::timestamp_quantization;
    }));
    const auto lazer_reparsed = parse_inline(*lazer_written.text);
    REQUIRE(lazer_reparsed.song.has_value());
    REQUIRE_FALSE(lazer_reparsed.has_errors());
    const auto& reparsed_fields = lazer_reparsed.song->metadata.format_groups.front().ordered_fields;
    for (const auto& expected : expected_lazer_fields) {
        CAPTURE(expected.first, expected.second);
        CHECK(std::ranges::find(reparsed_fields, std::pair<std::string, std::string>{expected}) != reparsed_fields.end());
    }

    auto edited_lazer = *lazer.song;
    auto& edited_note = edited_lazer.charts.front().measures.front().notes.front();
    ++edited_note.position.row;
    const auto edited_lazer_written = write_inline(edited_lazer);
    REQUIRE(edited_lazer_written.text.has_value());
    CHECK(edited_lazer_written.text->starts_with("osu file format v128\n"));
    CHECK(edited_lazer_written.text->find(",192,1002.5,1,") != std::string::npos);
    const auto edited_lazer_reparsed = parse_inline(*edited_lazer_written.text);
    REQUIRE(edited_lazer_reparsed.song.has_value());
    REQUIRE_FALSE(edited_lazer_reparsed.has_errors());

    const auto stable_conversion =
        write_inline(*lazer.song, {.profile = wormedit::osu_mania_file_profile_t::stable_v14});
    REQUIRE(stable_conversion.text.has_value());
    CHECK(stable_conversion.text->starts_with("osu file format v14\n"));
    CHECK(stable_conversion.text->find(",192,1001,1,") != std::string::npos);
    CHECK(stable_conversion.text->find(",192,1201,128,0,\"1401:") != std::string::npos);
    CHECK(std::ranges::any_of(stable_conversion.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code ==
               wormedit::osu_mania_write_diagnostic_code_t::timestamp_quantization;
    }));

    const auto future = parse_inline("osu file format v129\n");
    CHECK_FALSE(future.song.has_value());
    CHECK(has_code(future, wormedit::osu_mania_parse_diagnostic_code_t::unsupported_version));

    constexpr std::string_view fractional_v14 =
        "osu file format v14\n[General]\nMode:3\n[Difficulty]\nCircleSize:4\n"
        "[TimingPoints]\n0,500,4,0,0,100,1,0\n"
        "[HitObjects]\n64,192,1000.5,1,0,0:0:0:0:\n";
    const auto stable_fraction = parse_inline(fractional_v14);
    CHECK_FALSE(stable_fraction.song.has_value());
    CHECK(
        has_code(stable_fraction, wormedit::osu_mania_parse_diagnostic_code_t::invalid_hit_object));

    constexpr std::string_view nonfinite_v128 =
        "osu file format v128\n[General]\nMode:3\n[Difficulty]\nCircleSize:4\n"
        "[TimingPoints]\n0,500,4,0,0,100,1,0\n"
        "[HitObjects]\n64,192,nan,1,0,0:0:0:0:\n";
    const auto lazer_nonfinite = parse_inline(nonfinite_v128);
    CHECK_FALSE(lazer_nonfinite.song.has_value());
    CHECK(
        has_code(lazer_nonfinite, wormedit::osu_mania_parse_diagnostic_code_t::invalid_hit_object));

    const auto legacy = parse_inline("osu file format v4\n");
    CHECK_FALSE(legacy.song.has_value());
    CHECK(has_code(legacy, wormedit::osu_mania_parse_diagnostic_code_t::unsupported_version));

    const auto malformed = parse_inline("osu file format vfuture\n");
    CHECK_FALSE(malformed.song.has_value());
    CHECK(has_code(malformed, wormedit::osu_mania_parse_diagnostic_code_t::malformed_field));

    const auto missing = parse_inline("[General]\nMode:3\n");
    CHECK_FALSE(missing.song.has_value());
    CHECK(has_code(missing, wormedit::osu_mania_parse_diagnostic_code_t::malformed_field));
}

TEST_CASE("osu!mania reader rejects non-mania and malformed sources atomically",
          "[unit][format][osu-mania][negative]") {
    const auto non_mania = fixture("negative-non-mania.osu");
    CHECK_FALSE(non_mania.song.has_value());
    CHECK(has_code(non_mania, wormedit::osu_mania_parse_diagnostic_code_t::unsupported_mode));

    constexpr std::string_view malformed = R"osu(osu file format v14
[General]
Mode:3
[Difficulty]
CircleSize:4
[TimingPoints]
0,0,4,2,1,70,1,0
[HitObjects]
64,192,1000,1,0
)osu";
    const auto invalid = wormedit::parse_osu_mania_bytes(bytes(malformed));
    CHECK_FALSE(invalid.song.has_value());
    CHECK(invalid.has_errors());
}

TEST_CASE("osu!mania reader enforces caller-provided source and event bounds",
          "[unit][format][osu-mania][limits]") {
    const auto source = bytes(rewiring_source);
    const auto byte_limited =
        wormedit::parse_osu_mania_bytes(source, {.max_source_bytes = source.size() - 1});
    CHECK_FALSE(byte_limited.song.has_value());
    CHECK(has_code(byte_limited, wormedit::osu_mania_parse_diagnostic_code_t::source_limit));

    const auto event_limited = wormedit::parse_osu_mania_bytes(source, {.max_events = 1});
    CHECK_FALSE(event_limited.song.has_value());
    CHECK(has_code(event_limited, wormedit::osu_mania_parse_diagnostic_code_t::event_limit));

    // The source has thirteen timing, hit-object and event rows. Its two expanded
    // bookmarks must be included in the same caller-provided event budget.
    const auto bookmark_limited = wormedit::parse_osu_mania_bytes(source, {.max_events = 13});
    CHECK_FALSE(bookmark_limited.song.has_value());
    CHECK(has_code(bookmark_limited, wormedit::osu_mania_parse_diagnostic_code_t::event_limit));
}

TEST_CASE("osu!mania keeps a valid old barline immediately before a timing reset",
          "[unit][format][osu-mania][timing]") {
    constexpr std::string_view source = R"osu(osu file format v14
[General]
Mode:3
[Difficulty]
CircleSize:4
[TimingPoints]
0,500,4,0,0,100,1,0
2001,500,4,0,0,100,1,0
[HitObjects]
128,192,2000,1,0,0:0:0:0:
256,192,2001,1,0,0:0:0:0:
)osu";
    const auto result = wormedit::parse_osu_mania_bytes(bytes(source));
    REQUIRE(result.song.has_value());
    const auto& chart = result.song->charts.front();
    REQUIRE(chart.measures.size() == 3);
    const auto before_reset = position_for_source(chart, "128,192,2000,");
    const auto at_reset = position_for_source(chart, "256,192,2001,");
    REQUIRE(before_reset.has_value());
    REQUIRE(at_reset.has_value());
    CHECK(before_reset->measure == chart.measures[1].id);
    CHECK(before_reset->row == 0);
    CHECK(at_reset->measure == chart.measures[2].id);
    CHECK(at_reset->row == 0);
}

TEST_CASE("osu!mania omitted first barline changes measure geometry and x=512 clamps",
          "[unit][format][osu-mania][timing]") {
    constexpr std::string_view source = R"osu(osu file format v14
[General]
Mode:3
[Difficulty]
CircleSize:4
[TimingPoints]
0,500,4,0,0,100,1,8
[HitObjects]
512,192,0,1,0,0:0:0:0:
0,192,2000,1,0,0:0:0:0:
)osu";
    const auto result = wormedit::parse_osu_mania_bytes(bytes(source));
    REQUIRE(result.song.has_value());
    const auto& chart = result.song->charts.front();
    REQUIRE(chart.measures.size() == 2);
    const auto at_omitted_barline = position_for_source(chart, "512,192,0,");
    const auto at_next_barline = position_for_source(chart, "0,192,2000,");
    REQUIRE(at_omitted_barline.has_value());
    REQUIRE(at_next_barline.has_value());
    CHECK(at_omitted_barline->measure == chart.measures.front().id);
    CHECK(at_omitted_barline->row > 0);
    CHECK(at_next_barline->measure == chart.measures.back().id);
    CHECK(at_next_barline->row == 0);
    const auto omitted_boundary_note =
        std::ranges::find_if(chart.measures.front().notes, [](const auto& note) {
            return note.provenance.source_spelling.starts_with("512,192,0,");
        });
    REQUIRE(omitted_boundary_note != chart.measures.front().notes.end());
    CHECK(omitted_boundary_note->lane == chart.lane_layouts.front().layout.lanes.back().id);
    CHECK(std::ranges::any_of(chart.kiai,
                              [](const auto& effect) { return effect.retained_effect_bits == 8; }));
}

TEST_CASE("osu!mania integrates beat distance across an omitted mid-measure tempo reset",
          "[unit][format][osu-mania][timing]") {
    constexpr std::string_view source = R"osu(osu file format v14
[General]
Mode:3
[Difficulty]
CircleSize:4
[TimingPoints]
0,500,4,0,0,100,1,0
1000,250,4,0,0,100,1,8
[HitObjects]
64,192,1000,1,0,0:0:0:0:
192,192,1500,1,0,0:0:0:0:
320,192,2000,1,0,0:0:0:0:
)osu";
    const auto result = wormedit::parse_osu_mania_bytes(bytes(source));
    REQUIRE(result.song.has_value());
    const auto& chart = result.song->charts.front();
    REQUIRE(chart.measures.size() == 2);
    CHECK((chart.measures[0].definition.length == wormedit::rational_t{3, 2}));
    CHECK_FALSE(chart.measures[0].definition.meter.has_value());

    const auto at_tempo_reset = position_for_source(chart, "64,192,1000,");
    const auto within_new_tempo = position_for_source(chart, "192,192,1500,");
    const auto at_next_boundary = position_for_source(chart, "320,192,2000,");
    REQUIRE(at_tempo_reset.has_value());
    REQUIRE(within_new_tempo.has_value());
    REQUIRE(at_next_boundary.has_value());
    CHECK(at_tempo_reset->measure == chart.measures[0].id);
    CHECK(chart.divisions_per_standard_measure % 2 == 0);
    CHECK(at_tempo_reset->row == chart.divisions_per_standard_measure / 2);
    CHECK(within_new_tempo->measure == chart.measures[0].id);
    CHECK(within_new_tempo->row == chart.divisions_per_standard_measure);
    CHECK(at_next_boundary->measure == chart.measures[1].id);
    CHECK(at_next_boundary->row == 0);

    const auto written = write_inline(*result.song);
    REQUIRE(written.text.has_value());
    const auto reimported = parse_inline(*written.text);
    REQUIRE(reimported.song.has_value());
    const auto& round_tripped = reimported.song->charts.front();
    INFO(*written.text);
    REQUIRE(round_tripped.measures.size() == chart.measures.size());
    CHECK(round_tripped.measures[0].definition.length == chart.measures[0].definition.length);
    CHECK(note_source_times(round_tripped) == note_source_times(chart));
}

TEST_CASE("osu!mania supports every layout from 1K through 18K",
          "[unit][format][osu-mania][lanes]") {
    const auto source_for_keys = [](std::int64_t keys) {
        return std::string{"osu file format v14\n[General]\nMode:3\n[Difficulty]\nCircleSize:"} +
               std::to_string(keys) +
               "\n[TimingPoints]\n0,500,4,0,0,100,1,0\n[HitObjects]\n"
               "0,192,0,1,0,0:0:0:0:\n512,192,500,1,0,0:0:0:0:\n";
    };
    for (std::int64_t keys = 1; keys <= 18; ++keys) {
        CAPTURE(keys);
        const auto source = source_for_keys(keys);
        const auto result = wormedit::parse_osu_mania_bytes(bytes(source));
        REQUIRE(result.song.has_value());

        const auto& chart = result.song->charts.front();
        REQUIRE(chart.lane_layouts.size() == 1);
        REQUIRE(chart.lane_layouts.front().layout.lanes.size() == static_cast<std::size_t>(keys));
        CHECK(wormedit::validate_document(*result.song).empty());
        const auto first = lane_for_source(chart, "0,192,0,");
        const auto last = lane_for_source(chart, "512,192,500,");
        REQUIRE(first.has_value());
        REQUIRE(last.has_value());
        CHECK(*first == chart.lane_layouts.front().layout.lanes.front().id);
        CHECK(*last == chart.lane_layouts.front().layout.lanes.back().id);
    }
}

TEST_CASE("osu!mania rejects key counts outside 1K through 18K",
          "[unit][format][osu-mania][lanes][negative]") {
    for (const auto keys : {0, 19}) {
        CAPTURE(keys);
        const auto source =
            std::string{"osu file format v14\n[General]\nMode:3\n[Difficulty]\nCircleSize:"} +
            std::to_string(keys) + "\n[TimingPoints]\n0,500,4,0,0,100,1,0\n[HitObjects]\n";
        const auto result = parse_inline(source);
        CHECK_FALSE(result.song.has_value());
        CHECK(has_code(result, wormedit::osu_mania_parse_diagnostic_code_t::invalid_difficulty));
    }
}

TEST_CASE("osu!mania writer preserves every lane from 1K through 18K",
          "[unit][format][osu-mania][writer][lanes]") {
    for (std::int64_t keys = 1; keys <= 18; ++keys) {
        CAPTURE(keys);
        std::string source = "osu file format v14\n[General]\nMode:3\n[Metadata]\nTitle:Keys\n"
                             "[Difficulty]\nCircleSize:" +
                             std::to_string(keys) +
                             "\n[TimingPoints]\n0,500,4,0,0,100,1,0\n[HitObjects]\n";
        source += "0,192,0,1,0,\n";
        source += "512,192,1000,1,0,\n";
        for (std::int64_t lane = 0; lane < keys; ++lane) {
            const auto x = ((2 * lane + 1) * 256) / keys;
            source += std::to_string(x) + ",192," + std::to_string(2000 + lane * 100) + ",1,0,\n";
        }
        const auto imported = parse_inline(source);
        REQUIRE(imported.song.has_value());
        CHECK(wormedit::validate_document(*imported.song).empty());
        const auto written = write_inline(*imported.song);
        REQUIRE(written.text.has_value());
        CHECK_FALSE(written.has_errors());
        CHECK(written.text->find("CircleSize:" + std::to_string(keys)) != std::string::npos);

        const auto reimported = parse_inline(*written.text);
        REQUIRE(reimported.song.has_value());
        CHECK(wormedit::validate_document(*reimported.song).empty());
        const auto& chart = reimported.song->charts.front();
        REQUIRE(chart.lane_layouts.size() == 1);
        REQUIRE(chart.lane_layouts.front().layout.lanes.size() == static_cast<std::size_t>(keys));
        const auto first_boundary = lane_for_source_time(chart, 0);
        const auto last_boundary = lane_for_source_time(chart, 1000);
        REQUIRE(first_boundary.has_value());
        REQUIRE(last_boundary.has_value());
        CHECK(*first_boundary == chart.lane_layouts.front().layout.lanes.front().id);
        CHECK(*last_boundary == chart.lane_layouts.front().layout.lanes.back().id);
        for (std::int64_t lane = 0; lane < keys; ++lane) {
            const auto x = ((2 * lane + 1) * 256) / keys;
            const auto position =
                "" + std::to_string(x) + ",192," + std::to_string(2000 + lane * 100) + ",";
            const auto actual_lane = lane_for_source(chart, position);
            REQUIRE(actual_lane.has_value());
            CHECK(*actual_lane ==
                  chart.lane_layouts.front().layout.lanes[static_cast<std::size_t>(lane)].id);
        }
    }
}

TEST_CASE("osu!mania key counts 1 through 18 preserve rewired measure geometry",
          "[unit][format][osu-mania][writer][lanes][rewiring]") {
    constexpr std::array<std::int64_t, 4> boundary_times{1000, 1500, 2499, 2500};

    for (std::int64_t keys = 1; keys <= 18; ++keys) {
        CAPTURE(keys);
        std::string source{rewiring_source};
        const auto circle_size = source.find("CircleSize:4");
        REQUIRE(circle_size != std::string::npos);
        source.replace(circle_size, std::string_view{"CircleSize:4"}.size(),
                       "CircleSize:" + std::to_string(keys));

        std::string lane_objects;
        for (std::int64_t lane = 0; lane < keys; ++lane) {
            const auto x = ((2 * lane + 1) * 256) / keys;
            for (const auto time : {400 + lane * 10, 2000 + lane * 10, 3000 + lane * 10}) {
                lane_objects += std::to_string(x) + ",192," + std::to_string(time) + ",1,0,\n";
            }
        }
        const auto hit_objects = source.find("[HitObjects]\n");
        REQUIRE(hit_objects != std::string::npos);
        source.insert(hit_objects + std::string_view{"[HitObjects]\n"}.size(), lane_objects);

        const auto imported = parse_inline(source);
        REQUIRE(imported.song.has_value());
        CHECK_FALSE(imported.has_errors());
        CHECK(wormedit::validate_document(*imported.song).empty());
        const auto& source_chart = imported.song->charts.front();
        REQUIRE(source_chart.lane_layouts.size() == 1);
        REQUIRE(source_chart.lane_layouts.front().layout.lanes.size() ==
                static_cast<std::size_t>(keys));

        const auto written = write_inline(*imported.song);
        REQUIRE(written.text.has_value());
        CHECK_FALSE(written.has_errors());
        const auto reimported = parse_inline(*written.text);
        REQUIRE(reimported.song.has_value());
        CHECK_FALSE(reimported.has_errors());
        CHECK(wormedit::validate_document(*reimported.song).empty());
        const auto& round_tripped = reimported.song->charts.front();
        REQUIRE(round_tripped.lane_layouts.size() == 1);
        REQUIRE(round_tripped.lane_layouts.front().layout.lanes.size() ==
                static_cast<std::size_t>(keys));
        check_quantized_source_times(source_chart, round_tripped);

        REQUIRE(round_tripped.measures.size() == source_chart.measures.size());
        for (std::size_t measure = 0; measure < source_chart.measures.size(); ++measure) {
            const auto& before = source_chart.measures[measure].definition;
            const auto& after = round_tripped.measures[measure].definition;
            CHECK(after.length == before.length);
        }

        for (std::int64_t lane = 0; lane < keys; ++lane) {
            const auto x = ((2 * lane + 1) * 256) / keys;
            const auto time = 400 + lane * 10;
            const auto prefix = std::to_string(x) + ",192," + std::to_string(time) + ",";
            const auto before = lane_for_source(source_chart, prefix);
            const auto after = lane_near_source_time(round_tripped, x, time, 1);
            REQUIRE(before.has_value());
            REQUIRE(after.has_value());
            CHECK(
                *before ==
                source_chart.lane_layouts.front().layout.lanes[static_cast<std::size_t>(lane)].id);
            CHECK(
                *after ==
                round_tripped.lane_layouts.front().layout.lanes[static_cast<std::size_t>(lane)].id);
        }

        for (const auto time : boundary_times) {
            CAPTURE(time);
            const auto before = position_for_source_time(source_chart, time);
            const auto after = position_near_source_time(round_tripped, time);
            REQUIRE(before.has_value());
            REQUIRE(after.has_value());
            CHECK(after->measure == before->measure);
            CHECK(after->row == before->row);
        }
    }
}

TEST_CASE("osu!mania writer rejects layouts outside 1K through 18K while the model stays generic",
          "[unit][format][osu-mania][writer][lanes][negative]") {
    constexpr std::string_view source = R"osu(osu file format v14
[General]
Mode:3
[Difficulty]
CircleSize:1
[TimingPoints]
0,500,4,0,0,100,1,0
[HitObjects]
256,192,0,1,0,
)osu";
    const auto imported = parse_inline(source);
    REQUIRE(imported.song.has_value());

    auto empty_layout = *imported.song;
    empty_layout.charts.front().lane_layouts.front().layout.lanes.clear();
    empty_layout.charts.front().input_profile = {};
    empty_layout.charts.front().measures.front().notes.clear();
    const auto empty_result = write_inline(empty_layout);
    CHECK_FALSE(empty_result.text.has_value());
    CHECK(std::ranges::any_of(empty_result.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == wormedit::osu_mania_write_diagnostic_code_t::invalid_layout;
    }));

    auto nineteen_keys = *imported.song;
    auto& layout = nineteen_keys.charts.front().lane_layouts.front().layout;
    for (std::uint32_t order = 1; order < 19; ++order) {
        layout.lanes.push_back({.id = wormedit::lane_id_t{10'000 + order},
                                .name = "Key " + std::to_string(order + 1),
                                .role = wormedit::lane_role_t::playable,
                                .display_order = order});
    }
    CHECK(wormedit::validate_document(nineteen_keys).empty());
    const auto nineteen_result = write_inline(nineteen_keys);
    CHECK_FALSE(nineteen_result.text.has_value());
    CHECK(std::ranges::any_of(nineteen_result.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == wormedit::osu_mania_write_diagnostic_code_t::invalid_layout;
    }));
}

TEST_CASE("osu!mania writer round-trips metadata, timing, holds, bookmarks and assets",
          "[unit][format][osu-mania][writer]") {
    constexpr std::string_view source = R"osu(osu file format v14
[General]
AudioFilename: music.ogg
Mode:3
PreviewTime:12500
[Editor]
Bookmarks:-1000,2500
[Metadata]
Title:Writer roundtrip
Artist:Wormedit
Creator:Codec tests
Version:Layered fixture
Tags:one two-three
BeatmapID:57
[Difficulty]
CircleSize:4
OverallDifficulty:8
[Events]
0,2000,"background,one.jpg",0,0
Video,3000,"preview.mp4",0,0
Sample,2300,0,"clap.wav",100
[TimingPoints]
0,500,3,2,1,70,1,1
1000,-50,4,3,2,80,0,9
1500,400,3,2,1,70,1,0
[HitObjects]
64,192,0,1,2,"0:0:0:0:tap,custom.wav"
384,192,1000,128,0,4500:0:0:0:0:hold.wav
512,192,2500,1,0,
)osu";
    auto imported = parse_inline(source);
    REQUIRE(imported.song.has_value());
    auto& imported_chart = imported.song->charts.front();
    const auto hold_resource = std::ranges::find_if(
        imported.song->resources, [](const auto& item) { return item.name == "hold.wav"; });
    REQUIRE(hold_resource != imported.song->resources.end());
    const auto hold_asset = std::ranges::find(imported.song->audio_assets, hold_resource->asset,
                                              &wormedit::audio_asset_t::id);
    REQUIRE(hold_asset != imported.song->audio_assets.end());
    const auto hold_note = std::ranges::find_if(imported_chart.measures, [](const auto& measure) {
        return std::ranges::any_of(measure.notes, [](const auto& note) {
            return note.kind == wormedit::note_kind_t::hold;
        });
    });
    REQUIRE(hold_note != imported_chart.measures.end());
    const auto projected_note = std::ranges::find_if(hold_note->notes, [](const auto& note) {
        return note.kind == wormedit::note_kind_t::hold;
    });
    REQUIRE(projected_note != hold_note->notes.end());
    imported_chart.sound_channels.push_back(
        {.id = wormedit::object_id_t{900'000},
         .name = "hold.wav",
         .source_clip = {.id = wormedit::clip_id_t{900'001}, .source = hold_asset->id},
         .events = {{.id = wormedit::object_id_t{900'002},
                     .position = projected_note->position,
                     .end = projected_note->end,
                     .lane = projected_note->lane,
                     .playable = true},
                    {.id = wormedit::object_id_t{900'003},
                     .position = projected_note->position,
                     .lane = wormedit::lane_id_t{},
                     .playable = false}}});
    const auto written = write_inline(*imported.song);
    REQUIRE(written.text.has_value());
    CHECK_FALSE(written.has_errors());
    CHECK(written.text->find("Title:Writer roundtrip") != std::string::npos);
    CHECK(written.text->find("Artist:Wormedit") != std::string::npos);
    CHECK(written.text->find("Creator:Codec tests") != std::string::npos);
    CHECK(written.text->find("Version:Layered fixture") != std::string::npos);
    CHECK(written.text->find("Tags:one two-three") != std::string::npos);
    CHECK(written.text->find("BeatmapID:57") != std::string::npos);
    CHECK(written.text->find("Bookmarks: -1000,2500") != std::string::npos);
    CHECK(written.text->find("PreviewTime:12500") != std::string::npos);
    CHECK(written.text->find(",3,2,1,70,1,1") != std::string::npos);
    CHECK(written.text->find("4500:0:0:0:0:hold.wav") != std::string::npos);
    CHECK(written.text->find("Sample,1000,1,\"hold.wav\",100") != std::string::npos);
    CHECK(written.text->find("\"0:0:0:0:tap,custom.wav\"") != std::string::npos);
    CHECK(written.text->find("\"background,one.jpg\"") != std::string::npos);

    const auto reimported = parse_inline(*written.text);
    REQUIRE(reimported.song.has_value());
    CHECK_FALSE(reimported.has_errors());
    const auto& song = *reimported.song;
    const auto& chart = song.charts.front();
    CHECK(song.metadata.title == "Writer roundtrip");
    CHECK(song.metadata.artist == "Wormedit");
    CHECK(song.metadata.ordered_search_tags == std::vector<std::string>{"one", "two-three"});
    REQUIRE(chart.bookmarks.size() == 2);
    CHECK(chart.bookmarks[0].audio_milliseconds == -1000);
    CHECK(chart.bookmarks[1].audio_milliseconds == 2500);
    REQUIRE(chart.bpms.size() >= 2);
    REQUIRE(chart.scrolls.size() == 1);
    CHECK((chart.scrolls.front().multiplier == wormedit::signed_rational_t{2, 1}));
    CHECK(std::ranges::any_of(chart.kiai, [](const auto& event) {
        return event.enabled && (event.retained_effect_bits & 1U) != 0;
    }));
    CHECK(std::ranges::any_of(chart.measures, [](const auto& measure) {
        return std::ranges::any_of(measure.notes, [](const auto& note) {
            return note.kind == wormedit::note_kind_t::hold && note.end.has_value();
        });
    }));
    std::size_t bgm_event_count{};
    for (const auto& measure : chart.measures)
        bgm_event_count += measure.bgm_events.size();
    CHECK(bgm_event_count == 2);
    CHECK(std::ranges::any_of(chart.bga_events, [](const auto& event) {
        return event.kind == wormedit::visual_event_kind_t::background;
    }));
    CHECK(std::ranges::any_of(chart.bga_events, [](const auto& event) {
        return event.kind == wormedit::visual_event_kind_t::video;
    }));
    CHECK(std::ranges::any_of(
        song.resources, [](const auto& resource) { return resource.name == "tap,custom.wav"; }));
    CHECK(std::ranges::any_of(song.resources,
                              [](const auto& resource) { return resource.name == "hold.wav"; }));
    CHECK(std::ranges::any_of(song.resources,
                              [](const auto& resource) { return resource.name == "clap.wav"; }));

    const auto count_notes = [](const wormedit::chart_document_t& value) {
        std::size_t count{};
        for (const auto& measure : value.measures)
            count += measure.notes.size();
        return count;
    };
    CHECK(count_notes(imported.song->charts.front()) == 3);
    CHECK(count_notes(chart) == 3);
    CHECK(note_source_times(chart) == note_source_times(imported.song->charts.front()));
}

TEST_CASE("osu!mania writer uses nearest integer milliseconds with ties away from zero",
          "[unit][format][osu-mania][writer][timing]") {
    constexpr std::string_view source = R"osu(osu file format v14
[General]
Mode:3
[Difficulty]
CircleSize:1
[TimingPoints]
0,500,4,0,0,100,1,0
[HitObjects]
256,192,0,1,0,
)osu";
    auto imported = parse_inline(source);
    REQUIRE(imported.song.has_value());
    auto song = *imported.song;
    auto& chart = song.charts.front();
    chart.source_family = wormedit::format_family_t::native_project;
    chart.metadata.format_groups.clear();
    song.metadata.format_groups.clear();
    chart.divisions_per_standard_measure = 4000;
    auto& note = chart.measures.front().notes.front();
    note.id = wormedit::object_id_t{999};
    note.position.row = 1; // 0.5 ms at 120 BPM and 4000 rows per measure
    chart.bpms.front().beats_per_minute = {120, 1};

    const auto positive_tie = write_inline(song);
    REQUIRE(positive_tie.text.has_value());
    CHECK(positive_tie.text->find(",1,1,0,") != std::string::npos);
    CHECK(std::ranges::any_of(positive_tie.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code ==
               wormedit::osu_mania_write_diagnostic_code_t::timestamp_quantization;
    }));

    chart.beat_zero_audio_offset_seconds = {-1, 1000};
    const auto negative_tie = write_inline(song);
    REQUIRE(negative_tie.text.has_value());
    CHECK(negative_tie.text->find(",-1,1,0,") != std::string::npos);
}

TEST_CASE("osu!mania embeds structured sprites and nested OSB loops beside ordinary events",
          "[unit][format][osu-mania][storyboard]") {
    constexpr std::string_view source = R"osu(osu file format v14
[General]
AudioFilename: music.ogg
Mode:3
[Metadata]
Version:Storyboard difficulty
[Difficulty]
CircleSize:4
[Events]
0,0,"background.jpg",0,0
Video,2000,"preview.mp4",0,0
Sample,500,0,"clap.wav",100
Sprite,Foreground,Centre,"sprites/panel.png",320,240
 L,100,2
  F,0,0,100,0,1
  L,25,3
   M,0,0,50,0,0,10,20
   _P,0,1,2,H
   Sample,12,0,"nested.wav",80
UnknownDirective,keep-me
Sprite,Foreground,Centre,"sprites/second.png",320,240
 F,0,200,300,1,0
[TimingPoints]
0,500,4,0,0,100,1,0
[HitObjects]
256,192,1000,1,0,
)osu";
    const auto imported = parse_inline(source);
    REQUIRE(imported.song.has_value());
    CHECK_FALSE(imported.has_errors());
    const auto& song = *imported.song;
    REQUIRE(song.storyboards.size() == 1);
    CHECK(song.storyboards.front().name == "Storyboard difficulty");
    REQUIRE(song.storyboards.front().model.has_value());
    const auto& model = *song.storyboards.front().model;
    REQUIRE(model.sprites.size() == 2);
    REQUIRE(model.sprites.front().nodes.size() == 1);
    const auto& outer_loop = model.sprites.front().nodes.front();
    REQUIRE(outer_loop.kind == wormedit::storyboard_node_kind_t::loop);
    REQUIRE(outer_loop.children.size() == 2);
    CHECK(outer_loop.children.back().kind == wormedit::storyboard_node_kind_t::loop);
    CHECK(model.sprites.front().source_order < outer_loop.source_order);
    const auto retained_directive =
        std::ranges::find_if(model.retained_top_level_lines, [](const auto& line) {
            return line.text == "UnknownDirective,keep-me";
        });
    REQUIRE(retained_directive != model.retained_top_level_lines.end());
    CHECK(model.sprites.front().source_order < retained_directive->source_order);
    CHECK(retained_directive->source_order < model.sprites.back().source_order);

    const auto& chart = song.charts.front();
    CHECK(std::ranges::count_if(chart.bga_events, [](const auto& event) {
              return event.kind == wormedit::visual_event_kind_t::background ||
                     event.kind == wormedit::visual_event_kind_t::video;
          }) == 2);
    std::size_t bgm_events{};
    for (const auto& measure : chart.measures)
        bgm_events += measure.bgm_events.size();
    CHECK(bgm_events == 1);
    CHECK(std::ranges::none_of(song.resources,
                               [](const auto& resource) { return resource.name == "nested.wav"; }));
    CHECK(std::ranges::any_of(
        song.resources, [](const auto& resource) { return resource.name == "sprites/panel.png"; }));
    const auto panel_resource =
        std::ranges::find(song.resources, "sprites/panel.png", &wormedit::resource_entry_t::name);
    REQUIRE(panel_resource != song.resources.end());
    const auto panel_asset =
        std::ranges::find(song.visual_assets, panel_resource->asset, &wormedit::visual_asset_t::id);
    REQUIRE(panel_asset != song.visual_assets.end());
    CHECK(panel_asset->kind == wormedit::asset_kind_t::image);

    const auto written = write_inline(song);
    REQUIRE(written.text.has_value());
    CHECK_FALSE(written.has_errors());
    const auto count_text = [](std::string_view text, std::string_view needle) {
        std::size_t count{};
        std::size_t offset{};
        while ((offset = text.find(needle, offset)) != std::string_view::npos) {
            ++count;
            offset += needle.size();
        }
        return count;
    };
    CHECK(count_text(*written.text, "Sprite,Foreground,Centre") == 2);
    CHECK(count_text(*written.text, " L,100,2") == 1);
    CHECK(count_text(*written.text, "UnknownDirective,keep-me") == 1);
    CHECK(count_text(*written.text, "Sample,12,0,\"nested.wav\",80") == 1);
    CHECK(count_text(*written.text, "[Events]") == 1);
    CHECK(written.text->find("0,0,\"background.jpg\",0,0") != std::string::npos);
    CHECK(written.text->find("Video,2000,\"preview.mp4\",0,0") != std::string::npos);
    CHECK(written.text->find("Sample,500,0,\"clap.wav\",100") != std::string::npos);
    const auto written_again = write_inline(song);
    REQUIRE(written_again.text.has_value());
    CHECK(*written_again.text == *written.text);

    const auto reimported = parse_inline(*written.text);
    REQUIRE(reimported.song.has_value());
    CHECK_FALSE(reimported.has_errors());
    REQUIRE(reimported.song->storyboards.size() == 1);
    REQUIRE(reimported.song->storyboards.front().model.has_value());
    CHECK(wormedit::storyboard_semantically_equal(model,
                                                  *reimported.song->storyboards.front().model));
    CHECK(std::ranges::count_if(reimported.song->charts.front().bga_events, [](const auto& event) {
              return event.kind == wormedit::visual_event_kind_t::background ||
                     event.kind == wormedit::visual_event_kind_t::video;
          }) == 2);

    auto zero_order_source = *imported.song;
    zero_order_source.storyboards.front().model->sprites.front().source_order = 0;
    const auto remove_format_version = [](wormedit::format_metadata_t& group) {
        const auto found = std::ranges::find_if(group.ordered_fields, [](const auto& field) {
            return field.first == "osu.file_format";
        });
        if (found == group.ordered_fields.end())
            return;
        const auto index =
            static_cast<std::size_t>(std::distance(group.ordered_fields.begin(), found));
        group.ordered_fields.erase(found);
        if (index < group.ordered_field_source_orders.size())
            group.ordered_field_source_orders.erase(group.ordered_field_source_orders.begin() +
                                                    static_cast<std::ptrdiff_t>(index));
    };
    const auto set_sprite_order_zero = [](wormedit::format_metadata_t& group) {
        bool updated{};
        for (std::size_t index = 0; index < group.ordered_fields.size(); ++index) {
            const auto& [identity, value] = group.ordered_fields[index];
            if (index < group.ordered_field_source_orders.size() && identity == "Events[]" &&
                value.starts_with("Sprite,Foreground,Centre") && !updated) {
                group.ordered_field_source_orders[index] = 0;
                updated = true;
            }
        }
    };
    remove_format_version(zero_order_source.metadata.format_groups.front());
    remove_format_version(zero_order_source.charts.front().metadata.format_groups.front());
    set_sprite_order_zero(zero_order_source.metadata.format_groups.front());
    set_sprite_order_zero(zero_order_source.charts.front().metadata.format_groups.front());
    const auto zero_order_written = write_inline(zero_order_source);
    REQUIRE(zero_order_written.text.has_value());
    CHECK(count_text(*zero_order_written.text, "Sprite,Foreground,Centre") == 2);
}

TEST_CASE("osu!mania storyboard import and export reject malformed or out-of-bound content",
          "[unit][format][osu-mania][storyboard][negative]") {
    constexpr std::string_view malformed = R"osu(osu file format v14
[General]
Mode:3
[Difficulty]
CircleSize:1
[Events]
Sprite,Foreground,Centre,"broken.png"
[TimingPoints]
0,500,4,0,0,100,1,0
)osu";
    const auto invalid = parse_inline(malformed);
    CHECK_FALSE(invalid.song.has_value());
    CHECK(has_code(invalid, wormedit::osu_mania_parse_diagnostic_code_t::invalid_storyboard));

    constexpr std::string_view unsupported_trigger = R"osu(osu file format v14
[General]
Mode:3
[Difficulty]
CircleSize:1
[Events]
Sprite,Foreground,Centre,"a.png",0,0
 T,HitSound,0,100
  F,0,0,100,0,1
[TimingPoints]
0,500,4,0,0,100,1,0
)osu";
    const auto trigger_block = parse_inline(unsupported_trigger);
    CHECK_FALSE(trigger_block.song.has_value());
    CHECK(has_code(trigger_block,
                   wormedit::osu_mania_parse_diagnostic_code_t::unsupported_storyboard));

    std::string deeply_nested = "osu file format v14\n[General]\nMode:3\n[Difficulty]\n"
                                "CircleSize:1\n[Events]\nSprite,Foreground,Centre,\"a.png\",0,0\n";
    for (std::size_t depth = 1; depth <= 130; ++depth) {
        deeply_nested.append(depth, ' ');
        deeply_nested += "L,0,2\n";
    }
    deeply_nested += " F,0,0,1,0,1\n[TimingPoints]\n0,500,4,0,0,100,1,0\n";
    const auto bounded = parse_inline(deeply_nested);
    CHECK_FALSE(bounded.song.has_value());
    CHECK(has_code(bounded, wormedit::osu_mania_parse_diagnostic_code_t::storyboard_limit));

    constexpr std::string_view expanded_commands = R"osu(osu file format v14
[General]
Mode:3
[Difficulty]
CircleSize:1
[Events]
Sprite,Foreground,Centre,"a.png",0,0
 M,0,0,100,0,0,10,10,20,20
[TimingPoints]
0,500,4,0,0,100,1,0
)osu";
    const auto expanded_at_boundary =
        wormedit::parse_osu_mania_bytes(bytes(expanded_commands), {.max_events = 3});
    CHECK_FALSE(expanded_at_boundary.song.has_value());
    CHECK(has_code(expanded_at_boundary, wormedit::osu_mania_parse_diagnostic_code_t::event_limit));
    const auto expanded_with_room =
        wormedit::parse_osu_mania_bytes(bytes(expanded_commands), {.max_events = 4});
    REQUIRE(expanded_with_room.song.has_value());
    REQUIRE(expanded_with_room.song->storyboards.front().model.has_value());
    REQUIRE(expanded_with_room.song->storyboards.front().model->sprites.front().nodes.size() == 2);

    constexpr std::string_view valid = R"osu(osu file format v14
[General]
Mode:3
[Difficulty]
CircleSize:1
[Events]
Sprite,Foreground,Centre,"a.png",0,0
 F,0,0,100,0,1
[TimingPoints]
0,500,4,0,0,100,1,0
)osu";
    const auto imported = parse_inline(valid);
    REQUIRE(imported.song.has_value());
    const auto bounded_write = write_inline(*imported.song, {.max_output_bytes = 64});
    CHECK_FALSE(bounded_write.text.has_value());
    CHECK(std::ranges::any_of(bounded_write.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == wormedit::osu_mania_write_diagnostic_code_t::output_limit;
    }));

    auto unscoped_storyboard = *imported.song;
    unscoped_storyboard.metadata.format_groups.clear();
    unscoped_storyboard.charts.front().metadata.format_groups.clear();
    auto without_storyboard = unscoped_storyboard;
    without_storyboard.storyboards.clear();
    std::size_t baseline_event_budget{};
    wormedit::osu_mania_write_result_t baseline;
    for (; baseline_event_budget < 32; ++baseline_event_budget) {
        baseline = write_inline(without_storyboard, {.max_events = baseline_event_budget});
        if (baseline.text)
            break;
    }
    REQUIRE(baseline.text.has_value());
    const auto storyboard_event_limited =
        write_inline(unscoped_storyboard, {.max_events = baseline_event_budget + 1});
    CHECK_FALSE(storyboard_event_limited.text.has_value());
    CHECK(std::ranges::any_of(storyboard_event_limited.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == wormedit::osu_mania_write_diagnostic_code_t::output_limit;
    }));

    auto legacy = *imported.song;
    legacy.storyboards.front().model.reset();
    legacy.storyboards.front().retained_source_lines = {"Sprite,Foreground,Centre,a.png,0,0"};
    const auto unsupported = write_inline(legacy);
    CHECK_FALSE(unsupported.text.has_value());
    CHECK(std::ranges::any_of(unsupported.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == wormedit::osu_mania_write_diagnostic_code_t::unsupported_feature;
    }));

    auto section_injection = *imported.song;
    section_injection.storyboards.front().model->retained_top_level_lines.push_back(
        {.source_order = 999, .text = "[Events]"});
    const auto rejected_section = write_inline(section_injection);
    CHECK_FALSE(rejected_section.text.has_value());
    CHECK(std::ranges::any_of(rejected_section.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == wormedit::osu_mania_write_diagnostic_code_t::unsupported_feature;
    }));

    auto multi_chart = *imported.song;
    const auto second_chart_id = wormedit::chart_id_t{50'000};
    const auto second_measure_id = wormedit::measure_id_t{50'100};
    multi_chart.charts.push_back({
        .id = second_chart_id,
        .metadata = {.name = "Second"},
        .measures = {{.id = second_measure_id}},
        .lane_layouts = {{.id = wormedit::object_id_t{50'200},
                          .position = {second_measure_id, 0},
                          .layout = {.profile_id = "osu-mania-1k",
                                     .display_name = "1K",
                                     .lanes = {{.id = wormedit::lane_id_t{50'300},
                                                .name = "Key 1"}}}}},
        .bpms = {{.id = wormedit::object_id_t{50'400},
                  .position = {second_measure_id, 0},
                  .beats_per_minute = {120, 1}}},
    });
    REQUIRE(wormedit::validate_document(multi_chart).empty());
    const auto selected_chart_write =
        write_inline(multi_chart, {.chart = multi_chart.charts.front().id});
    CHECK_FALSE(selected_chart_write.text.has_value());
    CHECK(std::ranges::any_of(selected_chart_write.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == wormedit::osu_mania_write_diagnostic_code_t::unsupported_feature;
    }));
}

TEST_CASE("osu!mania writer diagnoses layer loss and rejects unsupported chart state",
          "[unit][format][osu-mania][writer][negative]") {
    const auto imported = parse_inline(rewiring_source);
    REQUIRE(imported.song.has_value());
    auto layered = *imported.song;
    auto& chart = layered.charts.front();
    chart.semantic_layers.push_back({.id = wormedit::layer_id_t{1}, .name = "accent"});
    auto& note = chart.measures.front().notes.front();
    note.semantic_layers.push_back(wormedit::layer_id_t{1});
    const auto flattened = write_inline(layered);
    std::string diagnostic_messages;
    for (const auto& diagnostic : flattened.diagnostics)
        diagnostic_messages += diagnostic.message + "\n";
    INFO(diagnostic_messages);
    REQUIRE(flattened.text.has_value());
    CHECK(std::ranges::any_of(flattened.diagnostics, [](const auto& diagnostic) {
        return diagnostic.severity == wormedit::diagnostic_severity_t::warning &&
               diagnostic.code == wormedit::osu_mania_write_diagnostic_code_t::lossy_transform;
    }));

    auto unsupported = *imported.song;
    unsupported.charts.front().measures.front().notes.front().kind = wormedit::note_kind_t::mine;
    const auto rejected = write_inline(unsupported);
    CHECK_FALSE(rejected.text.has_value());
    CHECK(rejected.has_errors());
    CHECK(std::ranges::any_of(rejected.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == wormedit::osu_mania_write_diagnostic_code_t::unsupported_feature;
    }));

    auto bounded = write_inline(*imported.song, {.max_output_bytes = 32});
    CHECK_FALSE(bounded.text.has_value());
    CHECK(std::ranges::any_of(bounded.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == wormedit::osu_mania_write_diagnostic_code_t::output_limit;
    }));
}

TEST_CASE("osu!mania writer validates the full multi-chart song and selected output",
          "[unit][format][osu-mania][writer]") {
    constexpr std::string_view source = R"osu(osu file format v14
[General]
Mode:3
[Difficulty]
CircleSize:1
[TimingPoints]
0,500,4,0,0,100,1,0
[HitObjects]
256,192,0,1,0,
)osu";
    auto imported = parse_inline(source);
    REQUIRE(imported.song.has_value());
    auto song = *imported.song;
    const auto selected = song.charts.front().id;
    song.charts.push_back({
        .id = wormedit::chart_id_t{90'000},
        .metadata = {.name = "Second"},
        .measures = {{.id = wormedit::measure_id_t{90'001}}},
        .lane_layouts = {{.id = wormedit::object_id_t{90'002},
                          .position = {wormedit::measure_id_t{90'001}, 0},
                          .layout = {.profile_id = "osu-mania-1k",
                                     .display_name = "1K",
                                     .lanes = {{.id = wormedit::lane_id_t{90'003},
                                                .name = "Key 1"}}}}},
        .bpms = {{.id = wormedit::object_id_t{90'004},
                  .position = {wormedit::measure_id_t{90'001}, 0},
                  .beats_per_minute = {120, 1}}},
    });
    song.output_groups.push_back({.name = "Both",
                                  .family = wormedit::format_family_t::osu_mania,
                                  .charts = {selected, wormedit::chart_id_t{90'000}},
                                  .relative_output_path = "charts"});
    REQUIRE(validate_document(song).empty());
    const auto written = write_inline(song, {.chart = selected});
    REQUIRE(written.text.has_value());
    CHECK_FALSE(written.has_errors());
}

TEST_CASE("osu!mania writer retains distinct song and difficulty format fields",
          "[unit][format][osu-mania][writer][metadata]") {
    constexpr std::string_view source = R"osu(osu file format v14
[General]
Mode:3
[Difficulty]
CircleSize:1
[TimingPoints]
0,500,4,0,0,100,1,0
[HitObjects]
256,192,0,1,0,
)osu";
    auto imported = parse_inline(source);
    REQUIRE(imported.song.has_value());
    auto& song_group = imported.song->metadata.format_groups.front();
    auto& chart_group = imported.song->charts.front().metadata.format_groups.front();
    song_group.ordered_fields.emplace_back("General.SongOnly", "song-value");
    song_group.ordered_fields.emplace_back("Metadata.TitleUnicode", "曲名");
    chart_group.ordered_fields.emplace_back("Difficulty.ChartOnly", "chart-value");
    chart_group.ordered_fields.emplace_back("Metadata.ArtistUnicode", "作曲者");

    const auto written = write_inline(*imported.song);
    REQUIRE(written.text.has_value());
    CHECK(written.text->find("SongOnly:song-value") != std::string::npos);
    CHECK(written.text->find("ChartOnly:chart-value") != std::string::npos);
    CHECK(written.text->find("TitleUnicode:曲名") != std::string::npos);
    CHECK(written.text->find("ArtistUnicode:作曲者") != std::string::npos);

    auto conflicting = *imported.song;
    conflicting.charts.front().metadata.format_groups.front().ordered_fields.front().second = "4";
    const auto rejected = write_inline(conflicting);
    CHECK_FALSE(rejected.text.has_value());
    CHECK(std::ranges::any_of(rejected.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == wormedit::osu_mania_write_diagnostic_code_t::invalid_metadata;
    }));
}

TEST_CASE("osu!mania round-trips the standard metadata inventory",
          "[unit][format][osu-mania][writer][metadata]") {
    constexpr std::string_view source = R"osu(osu file format v14
[General]
AudioFilename:music.ogg
AudioLeadIn:1500
AudioHash:legacy-hash
PreviewTime:12500
Countdown:1
SampleSet:Soft
StackLeniency:0.7
Mode:3
LetterboxInBreaks:1
StoryFireInFront:0
UseSkinSprites:1
AlwaysShowPlayfield:0
OverlayPosition:Below
SkinPreference:metadata-test
EpilepsyWarning:1
CountdownOffset:2
SpecialStyle:1
WidescreenStoryboard:1
SamplesMatchPlaybackRate:1
[Editor]
Bookmarks:-1000,2500
DistanceSpacing:1.25
BeatDivisor:6
GridSize:8
TimelineZoom:2.5
[Metadata]
Title:Roman title
TitleUnicode:Unicode title
Artist:Roman artist
ArtistUnicode:Unicode artist
Creator:Mapper
Version:Challenge
Source:Original work
Tags:alpha beta
BeatmapID:123
BeatmapSetID:456
[Difficulty]
HPDrainRate:5
CircleSize:7
OverallDifficulty:8
ApproachRate:9
SliderMultiplier:1.4
SliderTickRate:1
[Colours]
Combo1:255,0,0
SliderTrackOverride:1,2,3
SliderBorder:4,5,6
[TimingPoints]
0,500,4,1,0,100,1,0
[HitObjects]
36,192,0,1,0,
)osu";
    const auto imported = parse_inline(source);
    REQUIRE(imported.song.has_value());
    CHECK_FALSE(imported.has_errors());

    const auto written = write_inline(*imported.song);
    REQUIRE(written.text.has_value());
    CHECK_FALSE(written.has_errors());

    constexpr std::array expected_lines{
        "AudioFilename: music.ogg",
        "AudioLeadIn:1500",
        "AudioHash:legacy-hash",
        "PreviewTime:12500",
        "Countdown:1",
        "SampleSet:Soft",
        "StackLeniency:0.7",
        "LetterboxInBreaks:1",
        "StoryFireInFront:0",
        "UseSkinSprites:1",
        "AlwaysShowPlayfield:0",
        "OverlayPosition:Below",
        "SkinPreference:metadata-test",
        "EpilepsyWarning:1",
        "CountdownOffset:2",
        "SpecialStyle:1",
        "WidescreenStoryboard:1",
        "SamplesMatchPlaybackRate:1",
        "Bookmarks: -1000,2500",
        "DistanceSpacing:1.25",
        "BeatDivisor:6",
        "GridSize:8",
        "TimelineZoom:2.5",
        "Title:Roman title",
        "TitleUnicode:Unicode title",
        "Artist:Roman artist",
        "ArtistUnicode:Unicode artist",
        "Creator:Mapper",
        "Version:Challenge",
        "Source:Original work",
        "Tags:alpha beta",
        "BeatmapID:123",
        "BeatmapSetID:456",
        "HPDrainRate:5",
        "CircleSize:7",
        "OverallDifficulty:8",
        "ApproachRate:9",
        "SliderMultiplier:1.4",
        "SliderTickRate:1",
        "Combo1:255,0,0",
        "SliderTrackOverride:1,2,3",
        "SliderBorder:4,5,6",
    };
    for (const auto line : expected_lines) {
        CAPTURE(line);
        CHECK(written.text->find(line) != std::string::npos);
    }

    const auto reimported = parse_inline(*written.text);
    REQUIRE(reimported.song.has_value());
    CHECK_FALSE(reimported.has_errors());
    const auto& fields = reimported.song->metadata.format_groups.front().ordered_fields;
    const auto has_field = [&](std::string_view identity, std::string_view value) {
        return std::ranges::any_of(fields, [&](const auto& field) {
            return field.first == identity && field.second == value;
        });
    };
    CHECK(has_field("General.AudioLeadIn", "1500"));
    CHECK(has_field("Editor.TimelineZoom", "2.5"));
    CHECK(has_field("Metadata.TitleUnicode", "Unicode title"));
    CHECK(has_field("Metadata.ArtistUnicode", "Unicode artist"));
    CHECK(has_field("Metadata.Source", "Original work"));
    CHECK(has_field("Metadata.BeatmapID", "123"));
    CHECK(has_field("Metadata.BeatmapSetID", "456"));
    CHECK(has_field("Difficulty.SliderTickRate", "1"));
    CHECK(has_field("Colours.SliderBorder", "4,5,6"));
}

TEST_CASE("osu!mania enforces metadata-specific read and write bounds",
          "[unit][format][osu-mania][metadata][bounds]") {
    constexpr std::string_view source = R"osu(osu file format v14
[General]
Mode:3
[Difficulty]
CircleSize:1
[TimingPoints]
0,500,4,0,0,100,1,0
[HitObjects]
256,192,0,1,0,
)osu";
    const auto source_bytes = bytes(source);

    wormedit::osu_mania_parse_options_t field_options;
    field_options.max_metadata_fields = 1;
    const auto field_limited = wormedit::parse_osu_mania_bytes(source_bytes, field_options);
    CHECK_FALSE(field_limited.song.has_value());
    CHECK(has_code(field_limited, wormedit::osu_mania_parse_diagnostic_code_t::metadata_limit));

    wormedit::osu_mania_parse_options_t byte_options;
    byte_options.max_metadata_bytes = 5;
    const auto byte_limited = wormedit::parse_osu_mania_bytes(source_bytes, byte_options);
    CHECK_FALSE(byte_limited.song.has_value());
    CHECK(has_code(byte_limited, wormedit::osu_mania_parse_diagnostic_code_t::metadata_limit));

    const auto imported = parse_inline(source);
    REQUIRE(imported.song.has_value());
    const auto write_field_limited = write_inline(*imported.song, {.max_metadata_fields = 1});
    CHECK_FALSE(write_field_limited.text.has_value());
    CHECK(std::ranges::any_of(write_field_limited.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == wormedit::osu_mania_write_diagnostic_code_t::metadata_limit;
    }));

    const auto write_byte_limited = write_inline(*imported.song, {.max_metadata_bytes = 5});
    CHECK_FALSE(write_byte_limited.text.has_value());
    CHECK(std::ranges::any_of(write_byte_limited.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == wormedit::osu_mania_write_diagnostic_code_t::metadata_limit;
    }));
}

TEST_CASE("osu!mania export bakes parametric SV recipes into inherited timing points",
          "[unit][format][osu-mania][sv-recipe]") {
    const auto parsed = parse_inline(rewiring_source);
    REQUIRE(parsed.song.has_value());
    auto song = *parsed.song;
    auto& chart = song.charts.front();
    REQUIRE(chart.measures.size() >= 2);
    chart.scrolls.clear();
    chart.sv_recipes.push_back({
        .id = wormedit::recipe_id_t{900002},
        .name = "export guard",
        .range = {{chart.measures[0].id, 0}, {chart.measures[1].id, 0}},
        .output = wormedit::sv_recipe_output_t::sv,
        .curve = wormedit::sv_recipe_curve_t::constant,
        .parameters = {.value_from = {2, 1}},
    });
    const auto written = write_inline(song);
    REQUIRE(written.text.has_value());
    const auto reparsed = parse_inline(*written.text);
    REQUIRE(reparsed.song.has_value());
    CHECK(std::ranges::any_of(reparsed.song->charts.front().scrolls, [](const auto& event) {
        return event.multiplier == wormedit::signed_rational_t{2, 1};
    }));
    CHECK(chart.scrolls.empty());
    CHECK(chart.sv_recipes.size() == 1);
}

TEST_CASE("osu mania imports bind lanes without changing exported chart bytes", "[osu][default-input]") {
    auto parsed = fixture("mania-tags-bookmarks-kiai.osu");
    REQUIRE(parsed.song);
    const auto& input = parsed.song->charts.front().input_profile;
    CHECK(input.bindings.size() == parsed.song->charts.front().lane_layouts.front().layout.lanes.size());
    CHECK(input.bindings.front().key == wormedit::physical_key_t::digit_1);
    const auto bound = write_inline(*parsed.song);
    parsed.song->charts.front().input_profile = {};
    const auto unbound = write_inline(*parsed.song);
    CHECK(bound.text == unbound.text);
}

namespace {

auto text_lines(const std::string& text) -> std::vector<std::string> {
    std::vector<std::string> lines;
    std::size_t begin = 0;
    while (begin < text.size()) {
        const auto end = text.find('\n', begin);
        lines.push_back(text.substr(begin, end == std::string::npos ? end : end - begin));
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return lines;
}

auto line_index(const std::vector<std::string>& lines, std::string_view wanted) -> std::ptrdiff_t {
    const auto found = std::ranges::find(lines, wanted);
    return found == lines.end() ? -1 : found - lines.begin();
}

auto has_line(const std::vector<std::string>& lines, std::string_view wanted) -> bool {
    return line_index(lines, wanted) >= 0;
}

} // namespace

TEST_CASE("osu!mania keeps events, samples, colours and unknown keys through an edit",
          "[unit][format][osu-mania][writer][round-trip][non-gameplay]") {
    auto parsed = fixture("non-gameplay-roundtrip.osu");
    REQUIRE(parsed.song.has_value());
    REQUIRE_FALSE(parsed.has_errors());

    // Edit: move the first tap to the last lane. Nothing else changes.
    auto& measures = parsed.song->charts.front().measures;
    const auto with_notes = std::ranges::find_if(
        measures, [](const auto& measure) { return !measure.notes.empty(); });
    REQUIRE(with_notes != measures.end());
    auto& first_note = with_notes->notes.front();
    REQUIRE(first_note.kind == wormedit::note_kind_t::tap);
    first_note.lane = wormedit::lane_id_t{4};

    const auto written = write_inline(*parsed.song);
    REQUIRE(written.text.has_value());
    const auto lines = text_lines(*written.text);

    SECTION("general, colours and unknown keys") {
        for (const auto wanted :
             {"AudioLeadIn:1200", "PreviewTime:15000", "Countdown:2", "SampleSet:Drum",
              "SpecialStyle:1", "WidescreenStoryboard:1", "FutureGeneralKey:kept-verbatim",
              "Combo1:255,128,0", "Combo2:0,255,128", "SliderBorder:10,20,30"})
            CHECK(has_line(lines, wanted));
    }

    SECTION("events keep offsets, breaks, sample layer and volume, and storyboard order") {
        CHECK(has_line(lines, "0,0,\"first-background.jpg\",10,20"));
        CHECK(has_line(lines, "Video,500,\"intro-video.mp4\",5,6"));
        CHECK(has_line(lines, "2,3000,4000"));
        CHECK(has_line(lines, "2,8000,9000"));
        CHECK(has_line(lines, "Sample,5000,0,\"storyboard-hit.wav\",80"));
        const auto background = line_index(lines, "Sprite,Background,Centre,\"layer0.png\",320,240");
        const auto foreground = line_index(lines, "Sprite,Foreground,TopLeft,\"layer3.png\",0,0");
        REQUIRE(background >= 0);
        REQUIRE(foreground >= 0);
        CHECK(background < foreground);
        CHECK(lines[static_cast<std::size_t>(background) + 1] == " F,0,0,1000,0,1");
    }

    SECTION("timing points keep sample sets, volumes and effects, and synthesized lines inherit") {
        for (const auto wanted :
             {"0,500,4,1,0,60,1,0", "1000,-50,4,3,4,80,0,9", "2500,-100,4,2,3,70,0,1",
              "4000,500,4,2,3,65,1,0", "5000,-80,4,1,0,50,0,8", "8000,400,3,2,1,90,1,0"})
            CHECK(has_line(lines, wanted));
        // Measure starts without a source red line reuse the active point's mix.
        CHECK(has_line(lines, "2000,500,4,3,4,80,1,1"));
        CHECK(has_line(lines, "6000,500,4,1,0,50,1,0"));
    }

    SECTION("hit samples and the edited note") {
        CHECK(has_line(lines, "448,192,2000,1,0,"));
        CHECK(has_line(lines, "192,192,2500,1,2,\"1:2:3:40:chime.wav\""));
        CHECK(has_line(lines, "320,192,3000,128,4,\"5000:2:0:0:55:slider.wav\""));
        CHECK(has_line(lines, "448,192,5500,1,8,\"3:3:2:100:\""));
    }

    SECTION("the exported file re-imports and re-exports byte for byte") {
        const auto reparsed = wormedit::parse_osu_mania_bytes(bytes(*written.text));
        REQUIRE(reparsed.song.has_value());
        REQUIRE_FALSE(reparsed.has_errors());
        const auto again = write_inline(*reparsed.song);
        REQUIRE(again.text.has_value());
        CHECK(*again.text == *written.text);
    }
}

namespace {
void check_osu_split_layout(const wormedit::lane_layout_t& actual,
                            const wormedit::lane_layout_t& expected) {
    CHECK(actual.profile_id == expected.profile_id);
    CHECK(actual.split_stage == expected.split_stage);
    CHECK(actual.split_after_column == expected.split_after_column);
    REQUIRE(actual.lanes.size() == expected.lanes.size());
    for (std::size_t index = 0; index < expected.lanes.size(); ++index) {
        CHECK(actual.lanes[index].id == expected.lanes[index].id);
        CHECK(actual.lanes[index].role == expected.lanes[index].role);
        CHECK(actual.lanes[index].stage == expected.lanes[index].stage);
        CHECK(actual.lanes[index].display_order == expected.lanes[index].display_order);
        CHECK(actual.lanes[index].key_color == expected.lanes[index].key_color);
    }
}
} // namespace

TEST_CASE("osu SpecialStyle 12K and 16K survive native and osu roundtrips",
          "[unit][format][osu-mania][lanes]") {
    using namespace wormedit;
    for (const auto keys : {12U, 16U}) {
        for (const auto style : {"", "SpecialStyle:0\n", "SpecialStyle:1\n", "SpecialStyle:2\n"}) {
            CAPTURE(keys, style);
            std::string source = "osu file format v14\n[General]\nMode:3\n";
            source += style;
            source += "[Difficulty]\nCircleSize:" + std::to_string(keys) +
                "\n[TimingPoints]\n0,500,4,0,0,100,1,0\n[HitObjects]\n";
            for (std::uint32_t column = 0; column < keys; ++column)
                source += std::to_string((2 * column + 1) * 512 / (2 * keys)) +
                    ",192," + std::to_string(column * 100) + ",1,0,0:0:0:0:\n";
            auto imported = parse_inline(source);
            REQUIRE(imported.song);
            const auto& chart = imported.song->charts.front();
            const auto& layout = chart.lane_layouts.front().layout;
            const bool special = std::string_view(style) == "SpecialStyle:1\n";
            CHECK(layout.split_stage == special);
            REQUIRE(layout.lanes.size() == keys);
            for (std::uint32_t column = 0; column < keys; ++column) {
                const auto spelling = std::to_string((2 * column + 1) * 512 / (2 * keys)) + ",192,";
                CHECK(lane_for_source(chart, spelling) == layout.lanes[column].id);
                CHECK(layout.lanes[column].role == lane_role_t::playable);
                if (!special) CHECK(layout.lanes[column].key_color ==
                    (column % 2 == 0 ? lane_key_color_t::white : lane_key_color_t::black));
            }
            auto reopened = native_reopen(*imported.song);
            check_osu_split_layout(reopened.charts.front().lane_layouts.front().layout, layout);
            CHECK(reopened.charts.front().input_profile.profile_id == chart.input_profile.profile_id);
            CHECK(reopened.charts.front().input_profile.bindings.size() == chart.input_profile.bindings.size());
            auto exported = write_inline(reopened);
            REQUIRE(exported.text);
            auto reimported = parse_inline(*exported.text);
            REQUIRE(reimported.song);
            check_osu_split_layout(reimported.song->charts.front().lane_layouts.front().layout, layout);
            CHECK(validate_document(*reimported.song).empty());
        }
    }
}
