#include <wormedit/formats/bms.h>
#include <wormedit/formats/bmson.h>
#include <wormedit/formats/conversion.h>
#include <wormedit/formats/osu_mania.h>
#include <wormedit/formats/stepmania.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <vector>

// Windows only treats a path with a drive letter as absolute.
#if defined(_WIN32)
#define TEST_ABSOLUTE_ROOT "C:"
#else
#define TEST_ABSOLUTE_ROOT ""
#endif

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

auto stepmania_song() -> wormedit::song_document_t {
    const auto path = std::filesystem::path(WORMEDIT_FIXTURE_DIR) / "stepmania" /
                      "matched-timing.ssc";
    const auto parsed = wormedit::parse_stepmania_bytes(
        read_binary(path), {.profile = wormedit::stepmania_profile_t::ssc});
    REQUIRE(parsed.song.has_value());
    REQUIRE_FALSE(parsed.has_errors());
    return *parsed.song;
}

} // namespace

TEST_CASE("conversion prepares grouped StepMania output and validates reimport",
          "[unit][format][conversion]") {
    auto song = stepmania_song();
    auto second = song.charts.front();
    second.id = wormedit::chart_id_t{second.id.value + 100};
    second.metadata.name = "Second chart";
    song.charts.push_back(std::move(second));

    const wormedit::conversion_request_t request{
        .target = wormedit::format_family_t::stepmania,
        .stepmania = wormedit::stepmania_export_policy_t{
            .file_profile = wormedit::stepmania_profile_t::ssc,
            .timing_syntax = wormedit::stepmania_timing_syntax_t::native_warps,
        },
        .loss_policy = wormedit::loss_policy_t::warn,
    };
    const auto converted = wormedit::convert_document(song, request);

    REQUIRE(converted);
    REQUIRE(converted.outputs.size() == 1);
    CHECK(converted.outputs.front().chart == std::nullopt);
    CHECK(converted.outputs.front().suggested_filename.ends_with(".ssc"));
    CHECK(converted.outputs.front().reimport_validated);
    const auto parsed = wormedit::parse_stepmania_bytes(
        std::as_bytes(std::span{converted.outputs.front().bytes.data(),
                                converted.outputs.front().bytes.size()}),
        {.profile = wormedit::stepmania_profile_t::ssc,
         .source_timing_syntax = wormedit::stepmania_timing_syntax_t::native_warps});
    REQUIRE(parsed.song.has_value());
    CHECK(parsed.song->charts.size() == 2);
}

TEST_CASE("conversion limits grouped StepMania output to an ordered chart subset",
          "[unit][format][conversion]") {
    auto song = stepmania_song();
    auto second = song.charts.front();
    second.id = wormedit::chart_id_t{second.id.value + 100};
    second.metadata.name = "Selected second chart";
    song.charts.push_back(second);
    const wormedit::conversion_request_t request{.target = wormedit::format_family_t::stepmania,
                                                 .stepmania = wormedit::stepmania_export_policy_t{},
                                                 .charts = {second.id},
                                                 .loss_policy = wormedit::loss_policy_t::warn};
    const auto result = wormedit::convert_document(song, request);
    REQUIRE(result);
    REQUIRE(result.outputs.size() == 1);
    const auto parsed = wormedit::parse_stepmania_bytes(
        std::as_bytes(
            std::span{result.outputs.front().bytes.data(), result.outputs.front().bytes.size()}),
        {.profile = wormedit::stepmania_profile_t::ssc,
         .source_timing_syntax = wormedit::stepmania_timing_syntax_t::native_warps});
    REQUIRE(parsed.song.has_value());
    REQUIRE(parsed.song->charts.size() == 1);
    CHECK(parsed.song->charts.front().metadata.name == "Selected second chart");
}

TEST_CASE("conversion rejects ambiguous chart-set selection", "[unit][format][conversion]") {
    const auto song = stepmania_song();
    wormedit::conversion_request_t request{
        .target = wormedit::format_family_t::stepmania,
        .stepmania = wormedit::stepmania_export_policy_t{},
        .charts = {song.charts.front().id, song.charts.front().id}};
    CHECK_FALSE(wormedit::convert_document(song, request));
    request.charts = {wormedit::chart_id_t{999999}};
    CHECK_FALSE(wormedit::convert_document(song, request));
    request.charts = {song.charts.front().id};
    request.chart = song.charts.front().id;
    CHECK_FALSE(wormedit::convert_document(song, request));
}

TEST_CASE("conversion rejects incompatible policy and publishes no partial output",
          "[unit][format][conversion]") {
    const auto song = stepmania_song();
    const wormedit::conversion_request_t request{
        .target = wormedit::format_family_t::stepmania,
        .stepmania = wormedit::stepmania_export_policy_t{
            .file_profile = wormedit::stepmania_profile_t::sm,
            .timing_syntax = wormedit::stepmania_timing_syntax_t::native_warps,
            .implementation_profile =
                wormedit::stepmania_implementation_profile_t::stepmania_5_1,
        },
    };

    const auto converted = wormedit::convert_document(song, request);
    CHECK_FALSE(converted);
    CHECK(converted.has_errors());
    CHECK(converted.outputs.empty());
    REQUIRE_FALSE(converted.diagnostics.empty());
    CHECK(converted.diagnostics.front().code ==
          wormedit::conversion_diagnostic_code_t::unknown_target_profile);
    CHECK(converted.diagnostics.front().stage == wormedit::conversion_stage_t::request);
}

TEST_CASE("conversion enforces output bounds atomically",
          "[unit][format][conversion]") {
    const auto song = stepmania_song();
    const wormedit::conversion_request_t request{
        .target = wormedit::format_family_t::native_project,
        .loss_policy = wormedit::loss_policy_t::warn,
    };

    const auto converted = wormedit::convert_document(
        song, request, {.maximum_outputs = 1,
                        .maximum_total_output_bytes = 32,
                        .maximum_filename_bytes = 64});
    CHECK_FALSE(converted);
    CHECK(converted.outputs.empty());
    REQUIRE_FALSE(converted.diagnostics.empty());
    CHECK(converted.diagnostics.back().code ==
          wormedit::conversion_diagnostic_code_t::output_limit);
}

TEST_CASE("conversion writes a staged BMSON asset path without changing the source document",
          "[unit][format][conversion]") {
    const auto parsed = wormedit::parse_bmson_bytes(read_binary(
        std::filesystem::path(WORMEDIT_FIXTURE_DIR) / "bmson" / "zero-resolution-default.bmson"));
    REQUIRE(parsed.song.has_value());
    REQUIRE_FALSE(parsed.song->audio_assets.empty());
    const auto original_path = parsed.song->audio_assets.front().source_path;
    const wormedit::conversion_request_t request{
        .target = wormedit::format_family_t::bmson,
        .chart = parsed.song->charts.front().id,
        .loss_policy = wormedit::loss_policy_t::warn,
        .staged_assets = {{.asset = parsed.song->audio_assets.front().id,
                           .relative_path = "assets/12-tone.wav"}}};
    const auto converted = wormedit::convert_document(*parsed.song, request);
    std::string diagnostics;
    for (const auto& diagnostic : converted.diagnostics)
        diagnostics += diagnostic.message + "\n";
    INFO(diagnostics);
    REQUIRE(converted);
    REQUIRE(converted.outputs.size() == 1);
    CHECK(converted.outputs.front().bytes.find("assets/12-tone.wav") != std::string::npos);
    CHECK(parsed.song->audio_assets.front().source_path == original_path);
}

TEST_CASE("conversion rejects invalid staged asset mappings before serialization",
          "[unit][format][conversion]") {
    const auto parsed = wormedit::parse_bmson_bytes(read_binary(
        std::filesystem::path(WORMEDIT_FIXTURE_DIR) / "bmson" / "zero-resolution-default.bmson"));
    REQUIRE(parsed.song.has_value());
    wormedit::conversion_request_t request{
        .target = wormedit::format_family_t::bmson,
        .chart = parsed.song->charts.front().id,
        .staged_assets = {
            {.asset = parsed.song->audio_assets.front().id, .relative_path = "../outside.wav"}}};
    CHECK_FALSE(wormedit::convert_document(*parsed.song, request));
    request.staged_assets.front().relative_path = "assets/";
    CHECK_FALSE(wormedit::convert_document(*parsed.song, request));
    request.staged_assets.front().relative_path = "assets/tone.wav";
    request.staged_assets.front().asset = wormedit::asset_id_t{999'999};
    CHECK_FALSE(wormedit::convert_document(*parsed.song, request));
    request.staged_assets.front().asset = parsed.song->audio_assets.front().id;
    request.target = wormedit::format_family_t::native_project;
    CHECK_FALSE(wormedit::convert_document(*parsed.song, request));

    const auto multiple = wormedit::parse_bmson_bytes(read_binary(
        std::filesystem::path(WORMEDIT_FIXTURE_DIR) / "bmson" / "slicing-and-mixed-sounds.bmson"));
    REQUIRE(multiple.song.has_value());
    REQUIRE(multiple.song->audio_assets.size() >= 2);
    request.target = wormedit::format_family_t::bmson;
    request.chart = multiple.song->charts.front().id;
    request.staged_assets = {
        {.asset = multiple.song->audio_assets[0].id, .relative_path = "assets/shared.wav"},
        {.asset = multiple.song->audio_assets[1].id, .relative_path = "assets/shared.wav"}};
    CHECK_FALSE(wormedit::convert_document(*multiple.song, request));
}

TEST_CASE("conversion rewrites a BMS WAV declaration from a staged asset",
          "[unit][format][conversion]") {
    const auto parsed =
        wormedit::parse_bms_bytes(read_binary(std::filesystem::path(WORMEDIT_FIXTURE_DIR) / "bms" /
                                              "profiles" / "bms-5k-foot-pedal.bms"),
                                  {.file_extension = ".bms"});
    REQUIRE(parsed.song.has_value());
    REQUIRE_FALSE(parsed.song->audio_assets.empty());
    const auto original = parsed.song->resources.front().name;
    const wormedit::conversion_request_t request{
        .target = wormedit::format_family_t::bms,
        .target_profile = "bms-5k-outside-scratch-foot-pedal",
        .chart = parsed.song->charts.front().id,
        .loss_policy = wormedit::loss_policy_t::warn,
        .staged_assets = {
            {.asset = parsed.song->audio_assets.front().id, .relative_path = "assets/5-lane.wav"}}};
    const auto converted = wormedit::convert_document(*parsed.song, request);
    std::string diagnostics;
    for (const auto& diagnostic : converted.diagnostics)
        diagnostics += diagnostic.message + "\n";
    INFO(diagnostics);
    REQUIRE(converted);
    REQUIRE(converted.outputs.size() == 1);
    INFO(converted.outputs.front().bytes.substr(0, 500));
    CHECK(converted.outputs.front().bytes.find("#WAV01 assets/5-lane.wav") != std::string::npos);
    CHECK(parsed.song->resources.front().name == original);
}

TEST_CASE("conversion rewrites an osu!mania music path from a staged asset",
          "[unit][format][conversion]") {
    const auto parsed = wormedit::parse_osu_mania_bytes(read_binary(
        std::filesystem::path(WORMEDIT_FIXTURE_DIR) / "osu" / "mania-tags-bookmarks-kiai.osu"));
    REQUIRE(parsed.song.has_value());
    const auto music = std::ranges::find_if(parsed.song->audio_assets, [](const auto& asset) {
        return asset.source_path == "fixture.mp3";
    });
    REQUIRE(music != parsed.song->audio_assets.end());
    const wormedit::conversion_request_t request{
        .target = wormedit::format_family_t::osu_mania,
        .chart = parsed.song->charts.front().id,
        .loss_policy = wormedit::loss_policy_t::warn,
        .staged_assets = {{.asset = music->id, .relative_path = "assets/7-fixture.mp3"}}};
    const auto converted = wormedit::convert_document(*parsed.song, request);
    std::string diagnostics;
    for (const auto& diagnostic : converted.diagnostics)
        diagnostics += diagnostic.message + "\n";
    INFO(diagnostics);
    REQUIRE(converted);
    REQUIRE(converted.outputs.size() == 1);
    INFO(converted.outputs.front().bytes.substr(0, 500));
    CHECK(converted.outputs.front().bytes.find("AudioFilename: assets/7-fixture.mp3") !=
          std::string::npos);
    CHECK(music->source_path == "fixture.mp3");
}

TEST_CASE("StepMania conversion writes a staged music path for SM and SSC",
          "[unit][format][conversion]") {
    auto song = stepmania_song();
    song.resources.clear();
    song.audio_assets.clear();
    song.charts.front().scrolls.clear();
    song.charts.front().speeds.clear();
    song.charts.front().warps.clear();
    const auto source = std::filesystem::path{TEST_ABSOLUTE_ROOT "/external/music.ogg"};
    song.audio_assets.push_back({.id = wormedit::asset_id_t{900001}, .source_path = source});
    song.resources.push_back({.id = wormedit::resource_id_t{900002},
                              .name = "music.ogg",
                              .asset = wormedit::asset_id_t{900001},
                              .roles = {wormedit::resource_role_t::music}});
    CHECK_FALSE(wormedit::write_stepmania(song, {.profile = wormedit::stepmania_profile_t::ssc}).text);
    for (const auto profile : {wormedit::stepmania_profile_t::sm,
                               wormedit::stepmania_profile_t::ssc}) {
        const wormedit::conversion_request_t request{
            .target = wormedit::format_family_t::stepmania,
            .stepmania = wormedit::stepmania_export_policy_t{.file_profile = profile},
            .loss_policy = wormedit::loss_policy_t::warn,
            .staged_assets = {{.asset = wormedit::asset_id_t{900001},
                               .relative_path = "assets/900001-music.ogg"}}};
        const auto converted = wormedit::convert_document(song, request);
        INFO((profile == wormedit::stepmania_profile_t::sm ? "SM" : "SSC"));
        std::string messages;
        for (const auto& diagnostic : converted.diagnostics)
            messages += diagnostic.message + "\n";
        INFO(messages);
        REQUIRE(converted);
        REQUIRE(converted.outputs.size() == 1);
        CHECK(converted.outputs.front().bytes.find("#MUSIC:assets/900001-music.ogg;") !=
              std::string::npos);
        CHECK(song.audio_assets.back().source_path == source);
    }
}
