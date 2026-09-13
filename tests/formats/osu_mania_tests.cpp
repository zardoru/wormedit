#include <wormedit/formats/osu_mania.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
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

auto write_inline(const wormedit::song_document_t& song,
                  wormedit::osu_mania_write_options_t options = {})
    -> wormedit::osu_mania_write_result_t {
    return wormedit::write_osu_mania(song, options);
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
    CHECK((chart.measures[2].definition.length == wormedit::rational_t{999, 2000}));
    CHECK_FALSE(chart.measures[2].definition.meter.has_value());
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
    REQUIRE(chart.bpms.size() == 4);
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
        const auto first = lane_for_source(chart, "0,192,0,");
        const auto last = lane_for_source(chart, "512,192,500,");
        REQUIRE(first.has_value());
        REQUIRE(last.has_value());
        CHECK(*first == chart.lane_layouts.front().layout.lanes.front().id);
        CHECK(*last == chart.lane_layouts.front().layout.lanes.back().id);
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
        const auto written = write_inline(*imported.song);
        REQUIRE(written.text.has_value());
        CHECK_FALSE(written.has_errors());
        CHECK(written.text->find("CircleSize:" + std::to_string(keys)) != std::string::npos);

        const auto reimported = parse_inline(*written.text);
        REQUIRE(reimported.song.has_value());
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
