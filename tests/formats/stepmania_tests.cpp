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

auto fixture(std::string_view name, wormedit::stepmania_profile_t profile)
    -> wormedit::stepmania_parse_result_t {
    auto options = wormedit::stepmania_parse_options_t{};
    options.profile = profile;
    return wormedit::parse_stepmania_bytes(
        read_binary(std::filesystem::path(WORMEDIT_FIXTURE_DIR) / "stepmania" / name), options);
}

auto parse_ssc(std::string_view source) -> wormedit::stepmania_parse_result_t {
    auto options = wormedit::stepmania_parse_options_t{};
    options.profile = wormedit::stepmania_profile_t::ssc;
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

TEST_CASE("StepMania legacy negative timing encodings become canonical beat warps",
          "[unit][format][stepmania]") {
    const auto bpm = fixture("legacy-negative-bpm.sm", wormedit::stepmania_profile_t::sm);
    REQUIRE(bpm.song.has_value());
    REQUIRE_FALSE(bpm.has_errors());
    REQUIRE(bpm.song->charts.front().bpms.size() == 2);
    REQUIRE(bpm.song->charts.front().warps.size() == 1);
    CHECK(bpm.song->charts.front().warps.front().source_encoding ==
          wormedit::warp_provenance_t::negative_bpm);
    CHECK(at(bpm.song->charts.front().warps.front().span.begin, 2, 0));
    CHECK(at(bpm.song->charts.front().warps.front().span.end, 3, 0));

    const auto stop = fixture("legacy-negative-stop.sm", wormedit::stepmania_profile_t::sm);
    REQUIRE(stop.song.has_value());
    REQUIRE_FALSE(stop.has_errors());
    REQUIRE(stop.song->charts.front().warps.size() == 1);
    CHECK(stop.song->charts.front().warps.front().source_encoding ==
          wormedit::warp_provenance_t::negative_stop);
    CHECK(at(stop.song->charts.front().warps.front().span.begin, 2, 0));
    CHECK(at(stop.song->charts.front().warps.front().span.end, 3, 0));
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
