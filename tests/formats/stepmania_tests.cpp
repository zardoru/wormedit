#include <wormedit/core/timing.h>
#include <wormedit/formats/stepmania.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

auto read_binary(const std::filesystem::path& path) -> std::vector<std::byte> {
    std::ifstream stream(path, std::ios::binary);
    const std::string source{std::istreambuf_iterator<char>(stream),
                             std::istreambuf_iterator<char>()};
    std::vector<std::byte> bytes;
    bytes.reserve(source.size());
    for (const auto character : source)
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
    return bytes;
}

auto fixture(std::string_view name, wormedit::stepmania_profile_t profile,
             wormedit::stepmania_implementation_profile_t implementation =
                 wormedit::stepmania_implementation_profile_t::unspecified)
    -> wormedit::stepmania_parse_result_t {
    auto options = wormedit::stepmania_parse_options_t{};
    options.profile = profile;
    options.source_implementation_profile = implementation;
    return wormedit::parse_stepmania_bytes(
        read_binary(std::filesystem::path(WORMEDIT_FIXTURE_DIR) / "stepmania" / name), options);
}

auto parse_ssc(std::string_view source,
               wormedit::stepmania_implementation_profile_t implementation =
                   wormedit::stepmania_implementation_profile_t::unspecified)
    -> wormedit::stepmania_parse_result_t {
    auto options = wormedit::stepmania_parse_options_t{};
    options.profile = wormedit::stepmania_profile_t::ssc;
    options.source_implementation_profile = implementation;
    return wormedit::parse_stepmania_bytes(std::as_bytes(std::span{source.data(), source.size()}),
                                           options);
}

auto at(wormedit::local_position_t position, std::uint64_t measure, std::uint64_t row) -> bool {
    return position.measure.value == measure && position.row == row;
}

auto roundtrip(const wormedit::song_document_t& song, wormedit::stepmania_profile_t profile)
    -> wormedit::stepmania_parse_result_t {
    const auto output = wormedit::write_stepmania(song, {.profile = profile});
    REQUIRE(output.text.has_value());
    auto options = wormedit::stepmania_parse_options_t{};
    options.profile = profile;
    return wormedit::parse_stepmania_bytes(
        std::as_bytes(std::span{output.text->data(), output.text->size()}), options);
}

} // namespace

TEST_CASE("StepMania matched timing and chart notes parse transactionally",
          "[unit][format][stepmania]") {
    SECTION("SM song timing scope") {
        const auto result = fixture("matched-timing.sm", wormedit::stepmania_profile_t::sm);
        REQUIRE(result.song.has_value());
        REQUIRE_FALSE(result.has_errors());
        REQUIRE(result.song->charts.size() == 1);
        const auto& chart = result.song->charts.front();
        CHECK((chart.beat_zero_audio_offset_seconds == wormedit::signed_rational_t{-1, 8}));
        const auto timing = wormedit::build_timing_index(chart);
        REQUIRE(timing.index.has_value());
        const auto beat_zero = wormedit::timing_at(*timing.index, 0);
        REQUIRE(beat_zero.seconds.has_value());
        CHECK((*beat_zero.seconds == wormedit::signed_rational_t{-1, 8}));
        REQUIRE(chart.bpms.size() == 2);
        CHECK(at(chart.bpms[0].position, 1, 0));
        CHECK(at(chart.bpms[1].position, 3, 0));
        REQUIRE(chart.pauses.size() == 2);
        CHECK(chart.pauses[0].kind == wormedit::pause_kind_t::stop);
        CHECK(at(chart.pauses[0].position, 1, 480));
        CHECK(chart.pauses[1].kind == wormedit::pause_kind_t::delay);
        CHECK(at(chart.pauses[1].position, 1, 720));
        REQUIRE(chart.warps.size() == 1);
        CHECK(chart.warps[0].source_encoding == wormedit::warp_provenance_t::native);
        CHECK(at(chart.warps[0].span.begin, 2, 0));
        CHECK(at(chart.warps[0].span.end, 2, 480));
        REQUIRE(chart.scrolls.size() == 2);
        REQUIRE(chart.speeds.size() == 2);
        CHECK(chart.measures.size() == 3);
        CHECK(chart.measures[0].notes.size() == 3);
        CHECK(chart.measures[1].notes.size() == 1);
    }
    SECTION("SSC song fields and chart block remain separately addressable") {
        const auto result = fixture("matched-timing.ssc", wormedit::stepmania_profile_t::ssc);
        REQUIRE(result.song.has_value());
        REQUIRE_FALSE(result.has_errors());
        REQUIRE(result.song->metadata.format_groups.size() == 1);
        REQUIRE(result.song->charts.size() == 1);
        const auto& chart = result.song->charts.front();
        REQUIRE(chart.metadata.format_groups.size() == 1);
        CHECK(chart.metadata.format_groups.front().ordered_fields.front().first == "NOTEDATA");
        CHECK(chart.bpms.size() == 2);
        CHECK(chart.warps.size() == 1);
        CHECK(chart.speeds.size() == 2);
    }
}

TEST_CASE("StepMania fixture preserves 5.1 same-row timing phases",
          "[unit][format][stepmania][timing][discontinuity]") {
    const auto parsed = fixture("discontinuity-order.ssc", wormedit::stepmania_profile_t::ssc,
                                wormedit::stepmania_implementation_profile_t::native_warps);
    REQUIRE(parsed.song.has_value());
    REQUIRE_FALSE(parsed.has_errors());
    const auto& chart = parsed.song->charts.front();
    const auto timing = wormedit::build_timing_index(chart);
    REQUIRE(timing);
    CHECK(timing.index->discontinuity_profile ==
          wormedit::timing_discontinuity_profile_t::stepmania_5_1);

    const auto built =
        wormedit::build_preview_schedule(chart, *timing.index, {.sample_rate = 48'000});
    REQUIRE(built);
    std::vector<wormedit::preview_discontinuity_kind_t> before_delay;
    std::vector<wormedit::preview_discontinuity_kind_t> at_event;
    for (const auto& event : built.schedule->discontinuities) {
        if (event.sample_frame == 48'000) {
            before_delay.push_back(event.kind);
        } else if (event.sample_frame == 60'000) {
            at_event.push_back(event.kind);
        }
    }
    REQUIRE(before_delay.size() == 2);
    CHECK(before_delay[0] == wormedit::preview_discontinuity_kind_t::bpm);
    CHECK(before_delay[1] == wormedit::preview_discontinuity_kind_t::delay);
    REQUIRE(at_event.size() == 2);
    CHECK(at_event[0] == wormedit::preview_discontinuity_kind_t::metronome);
    CHECK(at_event[1] == wormedit::preview_discontinuity_kind_t::stop);
}

TEST_CASE("StepMania legacy negative timing encodings become canonical beat warps",
          "[unit][format][stepmania]") {
    const auto bpm = fixture("legacy-negative-bpm.sm", wormedit::stepmania_profile_t::sm,
                             wormedit::stepmania_implementation_profile_t::legacy_negative_bpm);
    REQUIRE(bpm.song.has_value());
    REQUIRE_FALSE(bpm.has_errors());
    REQUIRE(bpm.song->charts.front().bpms.size() == 2);
    REQUIRE(bpm.song->charts.front().warps.size() == 1);
    CHECK(bpm.song->charts.front().warps.front().source_encoding ==
          wormedit::warp_provenance_t::negative_bpm);
    CHECK(at(bpm.song->charts.front().warps.front().span.begin, 2, 0));
    CHECK(at(bpm.song->charts.front().warps.front().span.end, 3, 0));

    const auto stop = fixture("legacy-negative-stop.sm", wormedit::stepmania_profile_t::sm,
                              wormedit::stepmania_implementation_profile_t::legacy_negative_stop);
    REQUIRE(stop.song.has_value());
    REQUIRE_FALSE(stop.has_errors());
    REQUIRE(stop.song->charts.front().warps.size() == 1);
    CHECK(stop.song->charts.front().warps.front().source_encoding ==
          wormedit::warp_provenance_t::negative_stop);
    CHECK(at(stop.song->charts.front().warps.front().span.begin, 2, 0));
    CHECK(at(stop.song->charts.front().warps.front().span.end, 3, 0));
}

TEST_CASE("StepMania source profiles gate legacy warp syntax and summarize diagnostics",
          "[unit][format][stepmania]") {
    using implementation_t = wormedit::stepmania_implementation_profile_t;
    const auto native = fixture("native-warp.ssc", wormedit::stepmania_profile_t::ssc,
                                implementation_t::native_warps);
    REQUIRE(native.song.has_value());
    CHECK(native.song->charts.front().warps.front().source_encoding ==
          wormedit::warp_provenance_t::native);
    CHECK(native.diagnostics.empty());

    const auto native_as_legacy = fixture("native-warp.ssc", wormedit::stepmania_profile_t::ssc,
                                          implementation_t::legacy_negative_bpm);
    CHECK_FALSE(native_as_legacy.song.has_value());
    REQUIRE(native_as_legacy.diagnostics.size() == 1);
    CHECK(native_as_legacy.diagnostics.front().code ==
          wormedit::stepmania_parse_diagnostic_code_t::implementation_profile_mismatch);

    const auto mismatched = fixture("legacy-negative-bpm.sm", wormedit::stepmania_profile_t::sm,
                                    implementation_t::native_warps);
    CHECK_FALSE(mismatched.song.has_value());
    REQUIRE(mismatched.diagnostics.size() == 1);
    CHECK(mismatched.diagnostics.front().code ==
          wormedit::stepmania_parse_diagnostic_code_t::implementation_profile_mismatch);
    const auto stop_mismatched =
        fixture("legacy-negative-stop.sm", wormedit::stepmania_profile_t::sm,
                implementation_t::legacy_negative_bpm);
    CHECK_FALSE(stop_mismatched.song.has_value());
    REQUIRE(stop_mismatched.diagnostics.size() == 1);
    CHECK(stop_mismatched.diagnostics.front().code ==
          wormedit::stepmania_parse_diagnostic_code_t::implementation_profile_mismatch);

    const auto repeated =
        fixture("legacy-negative-bpm-multiple.sm", wormedit::stepmania_profile_t::sm,
                implementation_t::legacy_negative_bpm);
    REQUIRE(repeated.song.has_value());
    const auto& chart = repeated.song->charts.front();
    REQUIRE(chart.warps.size() == 2);
    CHECK(at(chart.warps[0].span.begin, 2, 0));
    CHECK(at(chart.warps[0].span.end, 3, 0));
    CHECK(at(chart.warps[1].span.begin, 4, 0));
    CHECK(at(chart.warps[1].span.end, 5, 0));
    REQUIRE(repeated.diagnostics.size() == 1);
    CHECK(repeated.diagnostics.front().code ==
          wormedit::stepmania_parse_diagnostic_code_t::legacy_warp_encoding);
    CHECK(repeated.song->metadata.format_groups.front().ordered_fields.front().first == "TITLE");
    CHECK(std::ranges::find(repeated.song->metadata.format_groups.front().ordered_fields,
                            std::pair{std::string{"BPMS"},
                                      std::string{"0.000=120.000,4.000=-120.000,8.000=120.000,"
                                                  "12.000=-120.000,16.000=120.000"}}) !=
          repeated.song->metadata.format_groups.front().ordered_fields.end());
}

TEST_CASE("Versioned SSC metadata remains editable without losing format-only fields",
          "[unit][format][stepmania]") {
    auto parsed = fixture("versioned-metadata.ssc", wormedit::stepmania_profile_t::ssc);
    REQUIRE(parsed.song.has_value());
    REQUIRE_FALSE(parsed.has_errors());
    CHECK(parsed.song->metadata.title == "Versioned metadata fixture");
    CHECK(parsed.song->metadata.artist == "Wormedit fixture authors");
    REQUIRE(parsed.song->charts.size() == 1);
    auto& chart = parsed.song->charts.front();
    CHECK(chart.metadata.name == "Editable chart name");
    CHECK(chart.metadata.author == "Chart Author");
    CHECK((chart.metadata.rating == wormedit::signed_rational_t{11, 1}));

    const auto& song_fields = parsed.song->metadata.format_groups.front().ordered_fields;
    CHECK(std::ranges::find(song_fields, std::pair{std::string{"VERSION"}, std::string{"0.83"}}) !=
          song_fields.end());
    CHECK(std::ranges::find(song_fields,
                            std::pair{std::string{"SUBTITLE"}, std::string{"Retained subtitle"}}) !=
          song_fields.end());
    CHECK(std::ranges::find(song_fields, std::pair{std::string{"TITLETRANSLIT"},
                                                   std::string{"Versioned Metadata Fixture"}}) !=
          song_fields.end());
    CHECK(std::ranges::find(song_fields, std::pair{std::string{"ARTISTTRANSLIT"},
                                                   std::string{"Wormedit Fixture Authors"}}) !=
          song_fields.end());

    parsed.song->metadata.title = "Edited title";
    chart.metadata.name = "Edited chart";
    chart.metadata.author = "Edited Author";
    chart.metadata.rating = {25, 2};
    const auto written =
        wormedit::write_stepmania(*parsed.song, {.profile = wormedit::stepmania_profile_t::ssc});
    REQUIRE(written.text.has_value());
    CHECK(written.text->find("#VERSION:0.83;") != std::string::npos);
    CHECK(written.text->find("#SUBTITLE:Retained subtitle;") != std::string::npos);
    CHECK(written.text->find("#TITLETRANSLIT:Versioned Metadata Fixture;") != std::string::npos);
    CHECK(written.text->find("#ARTISTTRANSLIT:Wormedit Fixture Authors;") != std::string::npos);
    CHECK(written.text->find("#GENRE:Test Genre;") != std::string::npos);
    CHECK(written.text->find("#ORIGIN:Wormedit;") != std::string::npos);
    CHECK(written.text->find("#CREDIT:Fixture maintainers;") != std::string::npos);
    CHECK(written.text->find("#BANNER:banner.png;") != std::string::npos);
    CHECK(written.text->find("#BACKGROUND:background.png;") != std::string::npos);
    CHECK(written.text->find("#CDTITLE:cdtitle.png;") != std::string::npos);
    CHECK(written.text->find("#SAMPLESTART:12.500;") != std::string::npos);
    CHECK(written.text->find("#SAMPLELENGTH:15.000;") != std::string::npos);
    CHECK(written.text->find("#SELECTABLE:YES;") != std::string::npos);
    CHECK(written.text->find("#DISPLAYBPM:120:180;") != std::string::npos);
    CHECK(written.text->find("#TITLE:Edited title;") != std::string::npos);
    CHECK(written.text->find("#DESCRIPTION:Retained chart description;") != std::string::npos);
    CHECK(written.text->find("#CHARTNAME:Edited chart;") != std::string::npos);
    CHECK(written.text->find("#CREDIT:Edited Author;") != std::string::npos);
    CHECK(written.text->find("#CREDIT:Chart Author;") == std::string::npos);
    CHECK(written.text->find("#MUSIC:chart.ogg;") != std::string::npos);
    CHECK(written.text->find("#METER:12.5;") != std::string::npos);

    const auto reimported = parse_ssc(*written.text);
    REQUIRE(reimported.song.has_value());
    REQUIRE_FALSE(reimported.has_errors());
    const auto& reparsed_chart = reimported.song->charts.front();
    CHECK(reparsed_chart.metadata.name == "Edited chart");
    CHECK(reparsed_chart.metadata.author == "Edited Author");
    CHECK((reparsed_chart.metadata.rating == wormedit::signed_rational_t{25, 2}));
}

TEST_CASE("SM global CREDIT maps to chart authors and conflicting edits reject",
          "[unit][format][stepmania]") {
    constexpr std::string_view source =
        "#TITLE:Credit scope;#CREDIT:Global Author;#BPMS:0=120;"
        "#NOTES:dance-single:Easy chart:Easy:2:0,0,0,0,0:\n1000\n0000\n0000\n0000\n;"
        "#NOTES:dance-single:Hard chart:Hard:8:0,0,0,0,0:\n0100\n0000\n0000\n0000\n;";
    auto options = wormedit::stepmania_parse_options_t{};
    options.profile = wormedit::stepmania_profile_t::sm;
    auto parsed = wormedit::parse_stepmania_bytes(
        std::as_bytes(std::span{source.data(), source.size()}), options);
    REQUIRE(parsed.song.has_value());
    REQUIRE(parsed.song->charts.size() == 2);
    CHECK(parsed.song->charts[0].metadata.author == "Global Author");
    CHECK(parsed.song->charts[1].metadata.author == "Global Author");

    auto written =
        wormedit::write_stepmania(*parsed.song, {.profile = wormedit::stepmania_profile_t::sm});
    REQUIRE(written.text.has_value());
    CHECK(written.text->find("#CREDIT:Global Author;") != std::string::npos);

    parsed.song->charts[1].metadata.author = "Another Author";
    written =
        wormedit::write_stepmania(*parsed.song, {.profile = wormedit::stepmania_profile_t::sm});
    CHECK_FALSE(written.text.has_value());
    CHECK(std::ranges::any_of(written.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code ==
                   wormedit::stepmania_write_diagnostic_code_t::conflicting_song_metadata &&
               diagnostic.field == "CREDIT";
    }));
}

TEST_CASE("Native warp timing, notes, and unknown metadata write deterministically and reimport",
          "[unit][format][stepmania]") {
    const auto input = fixture("matched-timing.ssc", wormedit::stepmania_profile_t::ssc);
    REQUIRE(input.song.has_value());
    const auto output =
        wormedit::write_stepmania(*input.song, {.profile = wormedit::stepmania_profile_t::ssc});
    REQUIRE(output.text.has_value());
    CHECK(output.text->find("#OFFSET:0.125;") != std::string::npos);
    CHECK(output.text->find("#WARPS:4=2;") != std::string::npos);
    const auto again = roundtrip(*input.song, wormedit::stepmania_profile_t::ssc);
    REQUIRE(again.song.has_value());
    const auto& original = input.song->charts.front();
    const auto& reimported = again.song->charts.front();
    CHECK((reimported.beat_zero_audio_offset_seconds == original.beat_zero_audio_offset_seconds));
    CHECK(reimported.bpms.size() == original.bpms.size());
    CHECK(reimported.pauses.size() == original.pauses.size());
    CHECK(reimported.warps.size() == original.warps.size());
    CHECK(reimported.scrolls.size() == original.scrolls.size());
    CHECK(reimported.speeds.size() == original.speeds.size());
    CHECK(reimported.measures[0].notes.size() == original.measures[0].notes.size());
}

TEST_CASE("StepMania empty timing lists and common column mappings are accepted",
          "[unit][format][stepmania]") {
    const auto empty =
        parse_ssc("#BPMS:;#STOPS:;#DELAYS:;#WARPS:;#SCROLLS:;#SPEEDS:;"
                  "#NOTEDATA:;#STEPSTYPE:dance-single;#NOTES:\n0000\n0000\n0000\n0000\n;");
    REQUIRE(empty.song.has_value());
    REQUIRE_FALSE(empty.has_errors());
    CHECK(empty.song->charts.front().bpms.empty());
    CHECK(empty.song->charts.front().speeds.empty());

    const auto parse_columns = [](std::string_view step_type, std::size_t columns) {
        const std::string row(columns, '0');
        const std::string source = "#NOTEDATA:;#STEPSTYPE:" + std::string(step_type) +
                                   ";#NOTES:\n" + row + "\n" + row + "\n" + row + "\n" + row +
                                   "\n;";
        return parse_ssc(source);
    };
    for (const auto [type, count] :
         {std::pair{"dance-threepanel", 3U}, std::pair{"dance-single", 4U},
          std::pair{"pump-single", 5U}, std::pair{"ez2-single", 5U}, std::pair{"dance-solo", 6U},
          std::pair{"pump-halfdouble", 6U}, std::pair{"ez2-real", 7U},
          std::pair{"dance-double", 8U}, std::pair{"dance-couple", 8U},
          std::pair{"pump-double", 10U}, std::pair{"pump-couple", 10U},
          std::pair{"ez2-double", 10U}}) {
        const auto parsed = parse_columns(type, count);
        REQUIRE(parsed.song.has_value());
        REQUIRE(parsed.song->charts.front().lane_layouts.size() == 1);
        CHECK(parsed.song->charts.front().lane_layouts.front().layout.lanes.size() == count);
    }
    CHECK_FALSE(parse_columns("made-up", 4).song.has_value());
}

TEST_CASE("SSC shared song timing remains at song scope across charts",
          "[unit][format][stepmania]") {
    constexpr std::string_view source = "#OFFSET:0.125;#BPMS:0=120;"
                                        "#NOTEDATA:;#STEPSTYPE:dance-single;#DIFFICULTY:Easy;"
                                        "#NOTES:\n1000\n0000\n0000\n0000\n;"
                                        "#NOTEDATA:;#STEPSTYPE:dance-single;#DIFFICULTY:Hard;"
                                        "#NOTES:\n0100\n0000\n0000\n0000\n;";
    const auto parsed = parse_ssc(source);
    REQUIRE(parsed.song.has_value());
    REQUIRE(parsed.song->charts.size() == 2);
    const auto written =
        wormedit::write_stepmania(*parsed.song, {.profile = wormedit::stepmania_profile_t::ssc});
    REQUIRE(written.text.has_value());
    const auto first_chart = written.text->find("#NOTEDATA:");
    REQUIRE(first_chart != std::string::npos);
    CHECK(written.text->find("#OFFSET:0.125;") < first_chart);
    CHECK(written.text->find("#BPMS:0=120;") < first_chart);
    CHECK(written.text->find("#BPMS:", first_chart) == std::string::npos);
    CHECK(written.text->find("#OFFSET:", first_chart) == std::string::npos);
}

TEST_CASE("SM writer rejects timing it cannot faithfully represent", "[unit][format][stepmania]") {
    const auto parsed = fixture("matched-timing.sm", wormedit::stepmania_profile_t::sm);
    REQUIRE(parsed.song.has_value());
    const auto native =
        wormedit::write_stepmania(*parsed.song, {.profile = wormedit::stepmania_profile_t::sm});
    CHECK_FALSE(native.text.has_value());
    CHECK(std::ranges::any_of(native.diagnostics, [](const auto& diagnostic) {
        return diagnostic.field == "WARPS" &&
               diagnostic.code == wormedit::stepmania_write_diagnostic_code_t::unsupported_feature;
    }));
    CHECK(std::ranges::any_of(native.diagnostics, [](const auto& diagnostic) {
        return diagnostic.field == "SCROLLS" || diagnostic.field == "SPEEDS";
    }));

    auto legacy_song = *parsed.song;
    legacy_song.charts.front().scrolls.clear();
    legacy_song.charts.front().speeds.clear();
    const auto legacy = wormedit::write_stepmania(
        legacy_song, {.profile = wormedit::stepmania_profile_t::sm,
                      .warp_encoding = wormedit::warp_encoding_t::negative_stop});
    REQUIRE(legacy.text.has_value());
    CHECK(legacy.text->find("#WARPS:") == std::string::npos);
    CHECK(legacy.text->find("4=-1") != std::string::npos);
}

TEST_CASE("StepMania writer rejects unsafe metadata and invisible notes",
          "[unit][format][stepmania]") {
    auto parsed = fixture("matched-timing.ssc", wormedit::stepmania_profile_t::ssc);
    REQUIRE(parsed.song.has_value());
    parsed.song->metadata.title = "bad;#NOTES:injected";
    const auto unsafe =
        wormedit::write_stepmania(*parsed.song, {.profile = wormedit::stepmania_profile_t::ssc});
    CHECK_FALSE(unsafe.text.has_value());
    CHECK(std::ranges::any_of(unsafe.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == wormedit::stepmania_write_diagnostic_code_t::invalid_metadata;
    }));

    parsed = fixture("matched-timing.ssc", wormedit::stepmania_profile_t::ssc);
    REQUIRE(parsed.song.has_value());
    REQUIRE_FALSE(parsed.song->charts.front().measures.front().notes.empty());
    parsed.song->charts.front().measures.front().notes.front().kind =
        wormedit::note_kind_t::invisible;
    const auto invisible =
        wormedit::write_stepmania(*parsed.song, {.profile = wormedit::stepmania_profile_t::ssc});
    CHECK_FALSE(invisible.text.has_value());
    CHECK(std::ranges::any_of(invisible.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == wormedit::stepmania_write_diagnostic_code_t::invalid_note_layout;
    }));
}

TEST_CASE("StepMania negative timing and malformed charts fail atomically",
          "[unit][format][stepmania]") {
    const auto invalid = fixture("negative-invalid-timing.ssc", wormedit::stepmania_profile_t::ssc);
    CHECK_FALSE(invalid.song.has_value());
    CHECK(invalid.has_errors());

    constexpr std::string_view missing_tail =
        "#BPMS:0=120;#NOTEDATA:;#STEPSTYPE:dance-single;#NOTES:\n2000\n;";
    auto options = wormedit::stepmania_parse_options_t{};
    options.profile = wormedit::stepmania_profile_t::ssc;
    const auto malformed = wormedit::parse_stepmania_bytes(
        std::as_bytes(std::span{missing_tail.data(), missing_tail.size()}), options);
    CHECK_FALSE(malformed.song.has_value());
    CHECK(malformed.has_errors());
}

TEST_CASE("Legacy warp writers diagnose and preserve representable conversions",
          "[unit][format][stepmania]") {
    const auto input = fixture("native-warp.ssc", wormedit::stepmania_profile_t::ssc);
    REQUIRE(input.song.has_value());
    const auto legacy_bpm = wormedit::write_stepmania(
        *input.song, {.profile = wormedit::stepmania_profile_t::ssc,
                      .warp_encoding = wormedit::warp_encoding_t::negative_bpm});
    REQUIRE(legacy_bpm.text.has_value());
    CHECK(legacy_bpm.text->find("= -") == std::string::npos);
    CHECK(legacy_bpm.text->find("#BPMS:0=120,4=-120,8=120;") != std::string::npos);

    const auto legacy_stop = wormedit::write_stepmania(
        *input.song, {.profile = wormedit::stepmania_profile_t::ssc,
                      .warp_encoding = wormedit::warp_encoding_t::negative_stop});
    REQUIRE(legacy_stop.text.has_value());
    CHECK(legacy_stop.text->find("#STOPS:4=-2;") != std::string::npos);
}

TEST_CASE("StepMania target implementation profiles choose exact warp encodings",
          "[unit][format][stepmania]") {
    using implementation_t = wormedit::stepmania_implementation_profile_t;
    const auto input = fixture("native-warp.ssc", wormedit::stepmania_profile_t::ssc,
                               implementation_t::native_warps);
    REQUIRE(input.song.has_value());

    const auto native_output = wormedit::write_stepmania(
        *input.song, {.profile = wormedit::stepmania_profile_t::ssc,
                      .target_implementation_profile = implementation_t::native_warps});
    REQUIRE(native_output.text.has_value());
    CHECK(native_output.text->find("#WARPS:4=4;") != std::string::npos);
    CHECK(native_output.text->find("#STOPS:") == std::string::npos);

    const auto legacy_bpm = wormedit::write_stepmania(
        *input.song, {.profile = wormedit::stepmania_profile_t::ssc,
                      .target_implementation_profile = implementation_t::legacy_negative_bpm});
    REQUIRE(legacy_bpm.text.has_value());
    CHECK(legacy_bpm.text->find("#WARPS:") == std::string::npos);
    CHECK(legacy_bpm.text->find("#BPMS:0=120,4=-120,8=120;") != std::string::npos);
    const auto bpm_reimported = parse_ssc(*legacy_bpm.text, implementation_t::legacy_negative_bpm);
    REQUIRE(bpm_reimported.song.has_value());
    const auto& bpm_warp = bpm_reimported.song->charts.front().warps.front();
    CHECK(bpm_warp.source_encoding == wormedit::warp_provenance_t::negative_bpm);
    CHECK(at(bpm_warp.span.begin, 2, 0));
    CHECK(at(bpm_warp.span.end, 3, 0));

    const auto legacy_stop = wormedit::write_stepmania(
        *input.song, {.profile = wormedit::stepmania_profile_t::ssc,
                      .target_implementation_profile = implementation_t::legacy_negative_stop});
    REQUIRE(legacy_stop.text.has_value());
    CHECK(legacy_stop.text->find("#WARPS:") == std::string::npos);
    CHECK(legacy_stop.text->find("#STOPS:4=-2;") != std::string::npos);
    const auto stop_reimported =
        parse_ssc(*legacy_stop.text, implementation_t::legacy_negative_stop);
    REQUIRE(stop_reimported.song.has_value());
    const auto& stop_warp = stop_reimported.song->charts.front().warps.front();
    CHECK(stop_warp.source_encoding == wormedit::warp_provenance_t::negative_stop);
    CHECK(at(stop_warp.span.begin, 2, 0));
    CHECK(at(stop_warp.span.end, 3, 0));

    const auto repeated =
        fixture("legacy-negative-bpm-multiple.sm", wormedit::stepmania_profile_t::sm,
                implementation_t::legacy_negative_bpm);
    REQUIRE(repeated.song.has_value());
    const auto repeated_output = wormedit::write_stepmania(
        *repeated.song, {.profile = wormedit::stepmania_profile_t::ssc,
                         .target_implementation_profile = implementation_t::legacy_negative_stop});
    REQUIRE(repeated_output.text.has_value());
    CHECK(repeated_output.text->find("#STOPS:4=-2,12=-2;") != std::string::npos);
    CHECK(std::ranges::count_if(repeated_output.diagnostics, [](const auto& diagnostic) {
              return diagnostic.code ==
                         wormedit::stepmania_write_diagnostic_code_t::legacy_warp_conversion &&
                     diagnostic.severity == wormedit::diagnostic_severity_t::warning;
          }) == 1);
    const auto repeated_reimported =
        parse_ssc(*repeated_output.text, implementation_t::legacy_negative_stop);
    REQUIRE(repeated_reimported.song.has_value());
    REQUIRE(repeated_reimported.song->charts.front().warps.size() == 2);
    CHECK(at(repeated_reimported.song->charts.front().warps[0].span.begin, 2, 0));
    CHECK(at(repeated_reimported.song->charts.front().warps[0].span.end, 3, 0));
    CHECK(at(repeated_reimported.song->charts.front().warps[1].span.begin, 4, 0));
    CHECK(at(repeated_reimported.song->charts.front().warps[1].span.end, 5, 0));
}

TEST_CASE("StepMania export bakes parametric SV recipes into SCROLLS",
          "[unit][format][stepmania][sv-recipe]") {
    const auto parsed = fixture("matched-timing.ssc", wormedit::stepmania_profile_t::ssc);
    REQUIRE(parsed.song.has_value());
    auto song = *parsed.song;
    auto& chart = song.charts.front();
    REQUIRE(chart.measures.size() >= 2);
    chart.scrolls.clear();
    chart.sv_recipes.push_back({
        .id = wormedit::recipe_id_t{900001},
        .name = "export guard",
        .range = {{chart.measures[0].id, 0}, {chart.measures[1].id, 0}},
        .output = wormedit::sv_recipe_output_t::scrolls,
        .curve = wormedit::sv_recipe_curve_t::constant,
        .parameters = {.value_from = {2, 1}},
    });
    const auto written =
        wormedit::write_stepmania(song, {.profile = wormedit::stepmania_profile_t::ssc});
    REQUIRE(written.text.has_value());
    CHECK(written.text->find("#SCROLLS:0=2,4=1;") != std::string::npos);
    CHECK(chart.scrolls.empty());
    CHECK(chart.sv_recipes.size() == 1);
}
