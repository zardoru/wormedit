#include <wormedit/formats/osu_mania.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
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
