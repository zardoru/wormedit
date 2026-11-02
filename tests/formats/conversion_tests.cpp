#include <wormedit/formats/bms.h>
#include <wormedit/formats/bms_writer.h>
#include <wormedit/formats/bmson.h>
#include <wormedit/formats/conversion.h>
#include <wormedit/formats/conversion_publication.h>
#include <wormedit/formats/native_project.h>
#include <wormedit/formats/native_project_storage.h>
#include <wormedit/formats/osu_mania.h>
#include <wormedit/formats/stepmania.h>
#include <wormedit/core/timing.h>
#include <wormedit/core/lane_profiles.h>
#include <wormedit/core/rational.h>
#include <wormedit/core/sv_recipe.h>
#include "../support/timing_links_fixture.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <span>
#include <source_location>
#include <string>
#include <string_view>
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
    REQUIRE(parsed.value.has_value());
    REQUIRE(parsed.ok());
    return *parsed.value;
}

} // namespace

namespace {

auto sliced_bmson_song() -> std::optional<wormedit::song_document_t> {
    constexpr std::string_view json = R"({
      "version":"1.0.0",
      "info":{"title":"Slice conversion","artist":"Fixture","genre":"test",
        "chart_name":"Slice chart","level":4,"init_bpm":120,"resolution":240,
        "mode_hint":"beat-7k"},
      "lines":[{"y":0},{"y":960},{"y":1920}],
      "sound_channels":[
        {"name":"stem.wav","notes":[
          {"x":1,"y":0,"c":false},{"x":1,"y":240,"c":true},
          {"x":1,"y":480,"c":false},{"x":1,"y":720,"c":true},
          {"x":1,"y":960,"c":true}]},
        {"name":"packed-one.wav","notes":[{"x":0,"y":120,"c":false}]},
        {"name":"packed-two.wav","notes":[{"x":0,"y":240,"c":false}]}
      ]
    })";
    auto parsed = wormedit::parse_bmson_bytes(std::as_bytes(std::span{json.data(), json.size()}));
    if (!parsed.value || !parsed.ok()) return std::nullopt;
    return std::move(parsed.value);
}

auto sliced_conversion_request(const wormedit::song_document_t& song,
                               wormedit::chart_id_t chart) -> wormedit::conversion_request_t {
    wormedit::conversion_request_t request{
        .target = wormedit::format_family_t::osu_mania,
        .target_profile = "stable-v14",
        .chart = chart,
        .loss_policy = wormedit::loss_policy_t::warn,
    };
    for (const auto& asset : song.audio_assets)
        request.slice_sources.push_back({.asset = asset.id,
                                         .native_sample_rate = 44'100,
                                         .native_frame_count = 44'100,
                                         .output_frame_count = 44'100});
    return request;
}

auto has_transform_error(const wormedit::conversion_result_t& result,
                         std::string_view text) -> bool {
    return std::ranges::any_of(result.diagnostics, [&](const auto& diagnostic) {
        return diagnostic.stage == wormedit::conversion_stage_t::transform &&
               diagnostic.code == wormedit::conversion_diagnostic_code_t::transform_failed &&
               diagnostic.message.find(text) != std::string::npos;
    });
}

auto spans_are_written(const wormedit::conversion_result_t& result) -> bool {
    if (result.outputs.size() != 1 || result.slice_plans.size() != 1) return false;
    for (const auto& span : result.slice_plans.front().value.spans)
        if (result.outputs.front().bytes.find(span.filename) == std::string::npos) return false;
    return true;
}

auto sample_filenames_are_in_target_sections(const wormedit::song_document_t& song,
                                             const wormedit::conversion_result_t& result)
    -> bool {
    if (result.outputs.size() != 1 || result.slice_plans.size() != 1) return false;
    const auto& bytes = result.outputs.front().bytes;
    const auto hit_begin = bytes.find("[HitObjects]");
    if (hit_begin == std::string::npos) return false;
    const auto event_begin = bytes.find("[Events]");
    if (event_begin == std::string::npos) return false;
    const auto stem_asset = song.charts.front().sound_channels.front().source_clip.source;
    bool stem_sample{};
    bool packed_sample{};
    for (const auto& span : result.slice_plans.front().value.spans) {
        if (span.asset == stem_asset)
            stem_sample = stem_sample || bytes.find(span.filename, hit_begin) != std::string::npos;
        else {
            const auto filename_position = bytes.find(span.filename, event_begin);
            packed_sample = packed_sample ||
                            (filename_position != std::string::npos && filename_position < hit_begin);
        }
    }
    const auto sample_event = bytes.find("Sample,", event_begin);
    return stem_sample && packed_sample && sample_event != std::string::npos &&
           sample_event < hit_begin;
}

auto same_span_plans(const wormedit::conversion_result_t& lhs,
                     const wormedit::conversion_result_t& rhs) -> bool {
    if (lhs.slice_plans.size() != rhs.slice_plans.size()) return false;
    for (std::size_t index = 0; index < lhs.slice_plans.size(); ++index)
        if (lhs.slice_plans[index].value.spans != rhs.slice_plans[index].value.spans) return false;
    return true;
}

auto has_silent_mapping(const wormedit::song_document_t& song,
                        const wormedit::conversion_request_t& request) -> bool {
    const auto plan = wormedit::slice_span_plan_payload_t::from(
        song, request.chart.value(), request.slice_sources, request.slice_options);
    return std::ranges::any_of(plan.value.mappings, [](const auto& mapping) {
        return !mapping.span_index.has_value();
    });
}

auto has_sounded_and_silent_notes(const wormedit::song_document_t& song) -> bool {
    bool sounded{};
    bool silent{};
    for (const auto& chart : song.charts)
        for (const auto& measure : chart.measures)
            for (const auto& note : measure.notes) {
                sounded = sounded || note.sound.has_value();
                silent = silent || !note.sound.has_value();
            }
    return sounded && silent;
}

auto check_conversion(bool condition, std::source_location location = std::source_location::current()) -> void {
    INFO("check at " << location.line());
    CHECK(condition);
}
auto require_conversion(bool condition) -> void { REQUIRE(condition); }

} // namespace

TEST_CASE("BMSON conversion exports deduplicated slice samples and preserves silent gameplay",
          "[unit][format][conversion][osu]") {
    auto song = sliced_bmson_song();
    REQUIRE(song.has_value());
    const auto original_bmson = wormedit::write_bmson(*song);
    REQUIRE(original_bmson.value.has_value());
    const auto request = sliced_conversion_request(*song, song->charts.front().id);
    const auto converted = wormedit::convert_document(*song, request);
    REQUIRE(converted);
    REQUIRE(spans_are_written(converted));
    check_conversion(converted.slice_plans.front().value.spans.size() == 4);
    check_conversion(has_silent_mapping(*song, request));
    check_conversion(sample_filenames_are_in_target_sections(*song, converted));
    const auto parsed = wormedit::parse_osu_mania_bytes(std::as_bytes(std::span{
        converted.outputs.front().bytes.data(), converted.outputs.front().bytes.size()}));
    REQUIRE(parsed.value.has_value());
    check_conversion(has_sounded_and_silent_notes(*parsed.value));
    const auto after_bmson = wormedit::write_bmson(*song);
    check_conversion(after_bmson.value == original_bmson.value);
}

TEST_CASE("BMSON slice conversion is deterministic and shares spans across charts",
          "[unit][format][conversion][osu]") {
    auto song = sliced_bmson_song();
    REQUIRE(song.has_value());
    auto second = song->charts.front();
    second.id = wormedit::chart_id_t{second.id.value + 1000};
    second.metadata.name = "Second chart";
    song->charts.push_back(second);
    auto request = sliced_conversion_request(*song, song->charts.front().id);
    request.chart.reset();
    request.charts = {song->charts.front().id, second.id};
    const auto first = wormedit::convert_document(*song, request);
    const auto again = wormedit::convert_document(*song, request);
    REQUIRE(first);
    REQUIRE(again);
    check_conversion(first.outputs.size() == 2);
    check_conversion(first.slice_plans.size() == 1);
    check_conversion(first.outputs.front().bytes == again.outputs.front().bytes);
    check_conversion(first.outputs.back().bytes == again.outputs.back().bytes);
    check_conversion(same_span_plans(first, again));
    check_conversion(first.slice_plans.front().value.spans.size() == 4);
}

TEST_CASE("BMSON slice conversion rejects missing geometry without partial output",
          "[unit][format][conversion][osu]") {
    auto song = sliced_bmson_song();
    REQUIRE(song.has_value());
    auto request = sliced_conversion_request(*song, song->charts.front().id);
    const auto missing_asset = song->charts.front().sound_channels.front().source_clip.source;
    std::erase_if(request.slice_sources, [&](const auto& source) {
        return source.asset == missing_asset;
    });
    const auto converted = wormedit::convert_document(*song, request);
    check_conversion(!converted);
    check_conversion(has_transform_error(converted, "geometry"));
    check_conversion(converted.outputs.empty());
    check_conversion(converted.slice_plans.empty());
    check_conversion(converted.asset_copies.empty());
}

TEST_CASE("BMSON slice conversion rejects exhausted generated IDs atomically",
          "[unit][format][conversion][osu]") {
    auto song = sliced_bmson_song();
    REQUIRE(song.has_value());
    song->audio_assets.push_back({.id = wormedit::asset_id_t{
                                      std::numeric_limits<std::uint64_t>::max() - 1},
                                  .source_path = "unused.wav"});
    auto request = sliced_conversion_request(*song, song->charts.front().id);
    const auto converted = wormedit::convert_document(*song, request);
    check_conversion(!converted);
    check_conversion(has_transform_error(converted, "capacity"));
    check_conversion(converted.outputs.empty());
    check_conversion(converted.slice_plans.empty());
    check_conversion(converted.asset_copies.empty());
}

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
    REQUIRE(parsed.value.has_value());
    CHECK(parsed.value->charts.size() == 2);
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
    REQUIRE(parsed.value.has_value());
    REQUIRE(parsed.value->charts.size() == 1);
    CHECK(parsed.value->charts.front().metadata.name == "Selected second chart");
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
    REQUIRE(parsed.value.has_value());
    REQUIRE_FALSE(parsed.value->audio_assets.empty());
    const auto original_path = parsed.value->audio_assets.front().source_path;
    const wormedit::conversion_request_t request{
        .target = wormedit::format_family_t::bmson,
        .chart = parsed.value->charts.front().id,
        .loss_policy = wormedit::loss_policy_t::warn,
        .staged_assets = {{.asset = parsed.value->audio_assets.front().id,
                           .relative_path = "assets/12-tone.wav"}}};
    const auto converted = wormedit::convert_document(*parsed.value, request);
    std::string diagnostics;
    for (const auto& diagnostic : converted.diagnostics)
        diagnostics += diagnostic.message + "\n";
    INFO(diagnostics);
    REQUIRE(converted);
    REQUIRE(converted.outputs.size() == 1);
    CHECK(converted.outputs.front().bytes.find("assets/12-tone.wav") != std::string::npos);
    CHECK(parsed.value->audio_assets.front().source_path == original_path);
}

TEST_CASE("conversion rejects invalid staged asset mappings before serialization",
          "[unit][format][conversion]") {
    const auto parsed = wormedit::parse_bmson_bytes(read_binary(
        std::filesystem::path(WORMEDIT_FIXTURE_DIR) / "bmson" / "zero-resolution-default.bmson"));
    REQUIRE(parsed.value.has_value());
    wormedit::conversion_request_t request{
        .target = wormedit::format_family_t::bmson,
        .chart = parsed.value->charts.front().id,
        .staged_assets = {
            {.asset = parsed.value->audio_assets.front().id, .relative_path = "../outside.wav"}}};
    CHECK_FALSE(wormedit::convert_document(*parsed.value, request));
    request.staged_assets.front().relative_path = "assets/";
    CHECK_FALSE(wormedit::convert_document(*parsed.value, request));
    request.staged_assets.front().relative_path = "assets/tone.wav";
    request.staged_assets.front().asset = wormedit::asset_id_t{999'999};
    CHECK_FALSE(wormedit::convert_document(*parsed.value, request));
    request.staged_assets.front().asset = parsed.value->audio_assets.front().id;
    request.target = wormedit::format_family_t::native_project;
    CHECK_FALSE(wormedit::convert_document(*parsed.value, request));

    const auto multiple = wormedit::parse_bmson_bytes(read_binary(
        std::filesystem::path(WORMEDIT_FIXTURE_DIR) / "bmson" / "slicing-and-mixed-sounds.bmson"));
    REQUIRE(multiple.value.has_value());
    REQUIRE(multiple.value->audio_assets.size() >= 2);
    request.target = wormedit::format_family_t::bmson;
    request.chart = multiple.value->charts.front().id;
    request.staged_assets = {
        {.asset = multiple.value->audio_assets[0].id, .relative_path = "assets/shared.wav"},
        {.asset = multiple.value->audio_assets[1].id, .relative_path = "assets/shared.wav"}};
    CHECK_FALSE(wormedit::convert_document(*multiple.value, request));
}

TEST_CASE("conversion rewrites a BMS WAV declaration from a staged asset",
          "[unit][format][conversion]") {
    const auto parsed =
        wormedit::parse_bms_bytes(read_binary(std::filesystem::path(WORMEDIT_FIXTURE_DIR) / "bms" /
                                              "profiles" / "bms-5k-foot-pedal.bms"),
                                  {.file_extension = ".bms", .lane_profile_id = "bms-5k-outside-scratch-foot-pedal"});
    REQUIRE(parsed.value.song.has_value());
    REQUIRE_FALSE(parsed.value.song->audio_assets.empty());
    const auto original = parsed.value.song->resources.front().name;
    const wormedit::conversion_request_t request{
        .target = wormedit::format_family_t::bms,
        .target_profile = "bms-5k-outside-scratch-foot-pedal",
        .chart = parsed.value.song->read_charts().front()->id,
        .loss_policy = wormedit::loss_policy_t::warn,
        .staged_assets = {
            {.asset = parsed.value.song->audio_assets.front().id, .relative_path = "assets/5-lane.wav"}}};
    const auto converted = wormedit::convert_document(*parsed.value.song, request);
    std::string diagnostics;
    for (const auto& diagnostic : converted.diagnostics)
        diagnostics += diagnostic.message + "\n";
    INFO(diagnostics);
    REQUIRE(converted);
    REQUIRE(converted.outputs.size() == 1);
    INFO(converted.outputs.front().bytes.substr(0, 500));
    CHECK(converted.outputs.front().bytes.find("#WAV01 assets/5-lane.wav") != std::string::npos);
    CHECK(parsed.value.song->resources.front().name == original);
}

TEST_CASE("conversion rewrites an osu!mania music path from a staged asset",
          "[unit][format][conversion]") {
    const auto parsed = wormedit::parse_osu_mania_bytes(read_binary(
        std::filesystem::path(WORMEDIT_FIXTURE_DIR) / "osu" / "mania-tags-bookmarks-kiai.osu"));
    REQUIRE(parsed.value.has_value());
    const auto music = std::ranges::find_if(parsed.value->audio_assets, [](const auto& asset) {
        return asset.source_path == "fixture.mp3";
    });
    REQUIRE(music != parsed.value->audio_assets.end());
    const wormedit::conversion_request_t request{
        .target = wormedit::format_family_t::osu_mania,
        .chart = parsed.value->charts.front().id,
        .loss_policy = wormedit::loss_policy_t::warn,
        .staged_assets = {{.asset = music->id, .relative_path = "assets/7-fixture.mp3"}}};
    const auto converted = wormedit::convert_document(*parsed.value, request);
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
    CHECK_FALSE(wormedit::write_stepmania(song, {.profile = wormedit::stepmania_profile_t::ssc}).value);
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

TEST_CASE("soundless BMSON conversion needs no decoder geometry",
          "[unit][format][conversion][osu]") {
    constexpr std::string_view text = R"({"version":"1.0.0","info":{"title":"silent","artist":"test","mode_hint":"beat-7k","resolution":240,"init_bpm":120},"lines":[{"y":0},{"y":960}],"sound_channels":[]})";
    const auto parsed = wormedit::parse_bmson_bytes(std::as_bytes(std::span{text.data(), text.size()}));
    REQUIRE(parsed.value.has_value());
    const auto converted = wormedit::convert_document(*parsed.value,
        {.target = wormedit::format_family_t::osu_mania, .target_profile = "lazer-v128",
         .loss_policy = wormedit::loss_policy_t::warn});
    REQUIRE(converted);
    check_conversion(converted.slice_plans.empty());
    check_conversion(converted.asset_copies.empty());
    check_conversion(converted.outputs.size() == 1);
}

namespace {
auto bms_slice_request(const wormedit::song_document_t& song) -> wormedit::conversion_request_t {
    auto request = sliced_conversion_request(song, song.charts.front().id);
    request.target = wormedit::format_family_t::bms;
    request.target_profile.clear();
    return request;
}
void add_retained_wavs(wormedit::song_document_t& song, std::size_t count) {
    for (std::size_t index = 0; index < count; ++index) {
        song.resources.push_back({.id = wormedit::resource_id_t{1000 + index},
            .name = "stem.wav", .asset = song.audio_assets.front().id,
            .roles = {wormedit::resource_role_t::music}});
    }
}
auto occurrence_count(std::string_view text, std::string_view needle) -> std::size_t {
    std::size_t count{}, position{};
    while ((position = text.find(needle, position)) != std::string_view::npos) {
        ++count;
        position += needle.size();
    }
    return count;
}
}

TEST_CASE("BMSON BMS slicing preserves silent gameplay with one shared binding",
          "[unit][format][conversion][bms_writer]") {
    auto song = sliced_bmson_song();
    require_conversion(song.has_value());
    const auto original = wormedit::write_bmson(*song).value;
    const auto result = wormedit::convert_document(*song, bms_slice_request(*song));
    for (const auto& diagnostic : result.diagnostics) { INFO(diagnostic.message); }
    require_conversion(static_cast<bool>(result));
    check_conversion(result.silent_files.size() == 1);
    check_conversion(result.slice_plans.front().value.spans.size() == 4);
    check_conversion(occurrence_count(result.outputs.front().bytes, "#WAV") == 5);
    check_conversion(result.outputs.front().bytes.find("conversion-silence.wav") != std::string::npos);
    check_conversion(result.outputs.front().bytes.find("packed-one.wav") == std::string::npos);
    check_conversion(wormedit::write_bmson(*song).value == original);
}

TEST_CASE("BMSON BMS capacity counts retained sounds and silence before publication",
          "[unit][format][conversion][bms_writer]") {
    auto song = sliced_bmson_song();
    require_conversion(song.has_value());
    add_retained_wavs(*song, 1290);
    const auto fit = wormedit::convert_document(*song, bms_slice_request(*song));
    for (const auto& diagnostic : fit.diagnostics) INFO(diagnostic.message);
    require_conversion(static_cast<bool>(fit));
    check_conversion(occurrence_count(fit.outputs.front().bytes, "#WAV") == 1295);
    add_retained_wavs(*song, 1);
    song->resources.back().id = wormedit::resource_id_t{9999};
    const auto excess = wormedit::convert_document(*song, bms_slice_request(*song));
    check_conversion(!excess);
    check_conversion(has_transform_error(excess, "needs 1296 sounds, BMS holds 1295"));
    check_conversion(excess.outputs.empty());
    check_conversion(excess.slice_plans.empty());
    check_conversion(excess.silent_files.empty());
    check_conversion(excess.asset_copies.empty());
}

TEST_CASE("BMSON BMS soundless gameplay declares silence without decoder geometry",
          "[unit][format][conversion][bms_writer]") {
    auto song = sliced_bmson_song();
    require_conversion(song.has_value());
    auto& chart = song->charts.front();
    chart.sound_channels.clear();
    for (auto& lane : chart.bgm_source_lanes) lane.stem_channel.reset();
    for (auto& measure : chart.measures) {
        measure.bgm_events.clear();
        for (auto& note : measure.notes) {
            note.sound.reset();
            note.sound_channel_event.reset();
        }
    }
    auto request = bms_slice_request(*song);
    request.slice_sources.clear();
    const auto converted = wormedit::convert_document(*song, request);
    require_conversion(static_cast<bool>(converted));
    check_conversion(converted.slice_plans.empty());
    check_conversion(converted.asset_copies.empty());
    check_conversion(converted.silent_files.size() == 1);
    check_conversion(occurrence_count(converted.outputs.front().bytes, "#WAV") == 1);
    song->audio_assets.front().id = wormedit::asset_id_t{std::numeric_limits<std::uint64_t>::max()};
    song->resources.front().asset = song->audio_assets.front().id;
    const auto exhausted = wormedit::convert_document(*song, request);
    check_conversion(!exhausted);
    check_conversion(has_transform_error(exhausted, "silence asset/resource ID capacity"));
    check_conversion(exhausted.silent_files.empty());
}

TEST_CASE("generated BMS silence does not hide same-named retained music",
          "[unit][format][conversion][bms_writer]") {
    auto song = sliced_bmson_song();
    require_conversion(song.has_value());
    auto& music = song->resources.front();
    music.name = "conversion-silence.wav";
    music.missing = false;
    music.roles.push_back(wormedit::resource_role_t::music);
    const auto converted = wormedit::convert_document(*song, bms_slice_request(*song));
    std::string messages;
    for (const auto& diagnostic : converted.diagnostics) messages += diagnostic.message + "\n";
    INFO(messages);
    require_conversion(static_cast<bool>(converted));
    check_conversion(converted.silent_files.size() == 1);
    check_conversion(converted.asset_copies.size() == 1);
    const auto retained = "retained-" + std::to_string(music.asset.value) + ".wav";
    check_conversion(converted.asset_copies.front().relative_path == retained);
    check_conversion(converted.outputs.front().bytes.find(retained) != std::string::npos);
    check_conversion(occurrence_count(converted.outputs.front().bytes, "#WAV") == 6);
}

namespace {

auto stepmania_slice_request(const wormedit::song_document_t& song) -> wormedit::conversion_request_t {
    auto request = sliced_conversion_request(song, song.charts.front().id);
    request.target = wormedit::format_family_t::stepmania;
    request.target_profile.clear();
    request.stepmania = wormedit::stepmania_export_policy_t{
        .file_profile = wormedit::stepmania_profile_t::ssc,
        .timing_syntax = wormedit::stepmania_timing_syntax_t::native_warps,
        .implementation_profile = wormedit::stepmania_implementation_profile_t::stepmania_5_1};
    return request;
}

auto keysound_index_count(const wormedit::song_document_t& song) -> std::pair<std::size_t, std::size_t> {
    std::size_t notes{};
    std::size_t bgm{};
    for (const auto& chart : song.charts)
        for (const auto& measure : chart.measures) {
            for (const auto& note : measure.notes) notes += note.sound.has_value() ? 1U : 0U;
            for (const auto& event : measure.bgm_events) bgm += event.sound.has_value() ? 1U : 0U;
        }
    return {notes, bgm};
}

} // namespace

TEST_CASE("BMSON slice conversion writes StepMania keysounds with one file per distinct span",
          "[unit][format][conversion][slice_span][stepmania]") {
    auto song = sliced_bmson_song();
    require_conversion(song.has_value());
    const auto original_bmson = wormedit::write_bmson(*song);
    const auto request = stepmania_slice_request(*song);
    const auto converted = wormedit::convert_document(*song, request);
    std::string messages;
    for (const auto& diagnostic : converted.diagnostics) messages += diagnostic.message + "\n";
    INFO(messages);
    require_conversion(static_cast<bool>(converted));
    require_conversion(converted.outputs.size() == 1 && converted.slice_plans.size() == 1);
    const auto& bytes = converted.outputs.front().bytes;
    const auto& spans = converted.slice_plans.front().value.spans;
    check_conversion(spans.size() == 4);
    // The stem plays one span twice; every distinct span is listed exactly once.
    for (const auto& span : spans) check_conversion(occurrence_count(bytes, span.filename) == 1);
    const auto parsed = wormedit::parse_stepmania_bytes(
        std::as_bytes(std::span{bytes.data(), bytes.size()}),
        {.profile = wormedit::stepmania_profile_t::ssc});
    require_conversion(parsed.value.has_value());
    check_conversion(parsed.ok());
    // Every voice/onset binding the osu!mania path keeps survives the StepMania export.
    const auto osu = wormedit::convert_document(*song, sliced_conversion_request(*song, song->charts.front().id));
    require_conversion(static_cast<bool>(osu));
    const auto osu_parsed = wormedit::parse_osu_mania_bytes(std::as_bytes(std::span{
        osu.outputs.front().bytes.data(), osu.outputs.front().bytes.size()}));
    require_conversion(osu_parsed.value.has_value());
    const auto expected = keysound_index_count(*osu_parsed.value);
    const auto actual = keysound_index_count(*parsed.value);
    check_conversion(actual.second == expected.second);
    check_conversion(actual.first + actual.second > 0);
    check_conversion(actual.first == expected.first);
    check_conversion(actual.first + actual.second > spans.size());
    check_conversion(wormedit::write_bmson(*song).value == original_bmson.value);
}

TEST_CASE("conversion canonical warnings retain output and atomic metadata", "[format][conversion][results]") {
    using namespace wormedit;
    conversion_outcome_t outcome{};
    outcome.value.outputs.push_back({.suggested_filename = "chart.bms", .bytes = "bytes", .reimport_validated = true});
    outcome.value.asset_copies.push_back({.asset = asset_id_t{7}, .relative_path = "assets/a.wav"});
    outcome.value.silent_files.emplace_back("silent.wav");
    outcome.diagnostics.push_back({.severity = diagnostic_severity_t::warning, .loss = true});
    check_conversion(outcome.ok());
    const auto legacy = conversion_result_t::from(std::move(outcome));
    check_conversion(static_cast<bool>(legacy));
    check_conversion(legacy.outputs.front().bytes == "bytes" && legacy.outputs.front().reimport_validated);
    check_conversion(legacy.asset_copies.front() == conversion_asset_copy_t{.asset = asset_id_t{7}, .relative_path = "assets/a.wav"});
    check_conversion(legacy.silent_files.front() == "silent.wav" && legacy.diagnostics.front().loss);
}

TEST_CASE("conversion empty successful report keeps the legacy output requirement", "[format][conversion][results]") {
    using namespace wormedit;
    conversion_outcome_t outcome{};
    check_conversion(outcome.ok());
    const auto legacy = conversion_result_t::from(std::move(outcome));
    check_conversion(!static_cast<bool>(legacy));
    check_conversion(!legacy.has_errors());
}

TEST_CASE("conversion primary prepares a reimported output atomically", "[format][conversion][results]") {
    using namespace wormedit;
    const auto song = stepmania_song();
    const auto before = write_native_project(song).value;
    require_conversion(before.has_value());
    const auto outcome = conversion_result_t::from(song, {.target = format_family_t::native_project});
    require_conversion(outcome.ok());
    require_conversion(outcome.value.outputs.size() == 1);
    check_conversion(outcome.value.outputs.front().reimport_validated);
    check_conversion(write_native_project(song).value == before);
}

TEST_CASE("Conversion progress counts serialized and reimported artifacts", "[unit][format][conversion-progress]") {
    const auto song = stepmania_song();
    wormedit::job_progress_t progress;
    const auto converted = wormedit::convert_document(
        song, {.target = wormedit::format_family_t::native_project}, {}, {.progress = &progress});
    REQUIRE(converted);
    REQUIRE(converted.outputs.size() == 1);
    REQUIRE(progress.completed.load() == 2);
    REQUIRE(progress.total.load() == 2);
}

TEST_CASE("Cancelled conversion returns no artifacts", "[unit][format][conversion-progress]") {
    const auto song = stepmania_song();
    std::stop_source stop;
    stop.request_stop();
    const auto converted = wormedit::convert_document(
        song, {.target = wormedit::format_family_t::native_project}, {}, {.stop = stop.get_token()});
    REQUIRE_FALSE(converted);
    REQUIRE(converted.outputs.empty());
    REQUIRE(converted.slice_plans.empty());
    REQUIRE(converted.asset_copies.empty());
    REQUIRE(converted.diagnostics.front().code == wormedit::conversion_diagnostic_code_t::cancelled);
}
namespace {
auto linked_export_fixture(wormedit::format_family_t target) -> wormedit::test::timing_links_fixture_t {
    if (target == wormedit::format_family_t::stepmania) {
        return wormedit::test::timing_links_fixture_t::from(wormedit::make_stepmania_pump_single_profile());
    }
    if (target == wormedit::format_family_t::osu_mania) {
        const auto profile = wormedit::make_osu_mania_profile(4);
        REQUIRE(profile);
        return wormedit::test::timing_links_fixture_t::from(*profile);
    }
    return wormedit::test::timing_links_fixture_t::from();
}
auto reimport_linked_output(std::string_view output, wormedit::format_family_t target,
    wormedit::stepmania_profile_t profile) -> std::optional<wormedit::song_document_t> {
    const auto bytes = std::as_bytes(std::span{output.data(), output.size()});
    if (target == wormedit::format_family_t::bms)
        return wormedit::parse_bms_bytes(bytes, {.lane_profile_id = "bme-7k-outside-scratch"}).value.song;
    if (target == wormedit::format_family_t::bmson) return wormedit::parse_bmson_bytes(bytes).value;
    if (target == wormedit::format_family_t::stepmania)
        return wormedit::parse_stepmania_bytes(bytes, {.profile = profile,
            .source_timing_syntax = wormedit::stepmania_timing_syntax_t::native_warps}).value;
    return wormedit::parse_osu_mania_bytes(bytes).value;
}
void check_reimported_link_clock(const wormedit::song_document_t& song) {
    REQUIRE(song.read_charts().size() == 1);
    const auto& chart = *song.read_charts().front();
    const auto timing = wormedit::timing_index_t::from(chart, {.allow_bounded_clock = true});
    REQUIRE(timing.value);
    CHECK(timing.value->seconds_at(chart.divisions_per_standard_measure / 4).seconds == wormedit::signed_rational_t{2, 5});
    CHECK(chart.timing_links == wormedit::timing_links_t{});
}
auto check_linked_conversion_output(const wormedit::conversion_outcome_t& converted,
    wormedit::format_family_t target, wormedit::stepmania_profile_t profile) -> wormedit::song_document_t {
    std::string messages;
    for (const auto& diagnostic : converted.diagnostics) messages += diagnostic.message + "\n";
    INFO(messages);
    REQUIRE(converted.ok());
    REQUIRE(converted.value.outputs.size() == 1);
    const auto& output = converted.value.outputs.front();
    CHECK(output.reimport_validated);
    const auto parsed = reimport_linked_output(output.bytes, target, profile);
    REQUIRE(parsed);
    check_reimported_link_clock(*parsed);
    return *parsed;
}
}
TEST_CASE("Interchange exports reimport linked timing as the resolved clock without native associations", "[format][conversion][timing-links]") {
    using namespace wormedit;
    const auto [target, profile] = GENERATE(Catch::Generators::table<format_family_t, stepmania_profile_t>({
        {format_family_t::bms, stepmania_profile_t::ssc},
        {format_family_t::bmson, stepmania_profile_t::ssc},
        {format_family_t::stepmania, stepmania_profile_t::sm},
        {format_family_t::stepmania, stepmania_profile_t::ssc},
        {format_family_t::osu_mania, stepmania_profile_t::ssc}}));
    CAPTURE(target, profile);
    auto fixture = linked_export_fixture(target);
    REQUIRE(fixture.set_bpm(fixture.source_id, {150, 1}).applied);
    const auto before = write_native_project(fixture.history.document).value;
    conversion_request_t request{.target = target, .chart = fixture.target_id, .loss_policy = loss_policy_t::warn};
    if (target == format_family_t::bms) request.target_profile = "bme-7k-outside-scratch";
    if (target == format_family_t::stepmania)
        request.stepmania = stepmania_export_policy_t{.file_profile = profile, .timing_syntax = stepmania_timing_syntax_t::native_warps};
    const auto converted = conversion_result_t::from(fixture.history.document, request);
    check_linked_conversion_output(converted, target, profile);
    CHECK(write_native_project(fixture.history.document).value == before);
}
namespace {
void add_linked_scroll_recipe(wormedit::test::timing_links_fixture_t& fixture, std::uint64_t end_row) {
    using namespace wormedit;
    document_command_transaction_t transaction{fixture.history};
    const auto& source = fixture.source();
    transaction.document().find_chart(fixture.source_id)->sv_recipes.push_back({.id = recipe_id_t{90001},
        .name = "Linked constant scroll", .range = {.begin = {.measure = source.measures.front().id},
            .end = {.measure = source.measures.front().id, .row = end_row}},
        .output = sv_recipe_output_t::sv, .curve = sv_recipe_curve_t::constant,
        .parameters = {.value_from = {2, 1}, .value_to = {2, 1}}});
    const auto staged = stage_timing_link_edit(transaction, fixture.target_id,
        {.category = timing_category_t::scroll, .source = fixture.source_id});
    REQUIRE(staged.value);
    REQUIRE(transaction.commit("Prepare linked recipe export").applied);
}
void check_reimported_recipe_scroll(const wormedit::song_document_t& song) {
    REQUIRE_FALSE(song.charts.front().scrolls.empty());
    CHECK(song.charts.front().scrolls.front().multiplier == wormedit::signed_rational_t{2, 1});
    CHECK(song.charts.front().sv_recipes.empty());
}
}
TEST_CASE("Linked recipe exports bake the effective source values while keeping live recipes and links", "[format][conversion][timing-links]") {
    using namespace wormedit;
    const auto target = GENERATE(format_family_t::stepmania, format_family_t::osu_mania);
    auto fixture = linked_export_fixture(target);
    REQUIRE(fixture.set_bpm(fixture.source_id, {150, 1}).applied);
    add_linked_scroll_recipe(fixture, fixture.source().divisions_per_standard_measure / 2);
    const auto before = write_native_project(fixture.history.document).value;
    const auto prepared = sv_recipe_prepared_t::from(fixture.target(), fixture.target().sv_recipes.front());
    REQUIRE(prepared.ok());
    CHECK(prepared.value.value_at(fixture.target().divisions_per_standard_measure / 4) == 2.0L);
    conversion_request_t request{.target = target, .chart = fixture.target_id, .loss_policy = loss_policy_t::warn};
    if (target == format_family_t::stepmania)
        request.stepmania = stepmania_export_policy_t{.file_profile = stepmania_profile_t::ssc,
            .timing_syntax = stepmania_timing_syntax_t::native_warps};
    const auto converted = conversion_result_t::from(fixture.history.document, request);
    const auto parsed = check_linked_conversion_output(converted, target, stepmania_profile_t::ssc);
    check_reimported_recipe_scroll(parsed);
    CHECK(write_native_project(fixture.history.document).value == before);
}
TEST_CASE("Unsupported linked recipe export returns no outputs and leaves live timing unchanged", "[format][conversion][timing-links]") {
    using namespace wormedit;
    auto fixture = test::timing_links_fixture_t::from();
    add_linked_scroll_recipe(fixture, fixture.source().divisions_per_standard_measure / 2);
    const auto before = write_native_project(fixture.history.document).value;
    const auto converted = conversion_result_t::from(fixture.history.document,
        {.target = format_family_t::bms, .target_profile = "bme-7k-outside-scratch", .chart = fixture.target_id});
    CHECK_FALSE(converted.ok());
    CHECK(converted.value.outputs.empty());
    CHECK(write_native_project(fixture.history.document).value == before);
}
TEST_CASE("Inexact linked recipe export endpoints reject without rounding or mutating the source", "[format][conversion][timing-links]") {
    using namespace wormedit;
    auto fixture = linked_export_fixture(format_family_t::stepmania);
    add_linked_scroll_recipe(fixture, fixture.source().divisions_per_standard_measure);
    const auto before = write_native_project(fixture.history.document).value;
    const auto converted = conversion_result_t::from(fixture.history.document,
        {.target = format_family_t::stepmania, .stepmania = stepmania_export_policy_t{
            .file_profile = stepmania_profile_t::ssc, .timing_syntax = stepmania_timing_syntax_t::native_warps},
            .chart = fixture.target_id});
    CHECK_FALSE(converted.ok());
    CHECK(converted.value.outputs.empty());
    CHECK(write_native_project(fixture.history.document).value == before);
}

namespace {
void check_native_cut_roundtrip(const wormedit::song_document_t& song) {
    const auto native = wormedit::write_native_project(song);
    REQUIRE((native.ok() && native.value));
    const auto restored = wormedit::parse_native_project(*native.value);
    REQUIRE((restored.ok() && restored.value));
    CHECK(restored.value->export_cuts == song.export_cuts);
}
}
TEST_CASE("Standalone format writers reject export cuts that require the shared audio plan", "[formats][conversion][export-cut]") {
    auto song = stepmania_song();
    song.export_cuts.push_back({.id = wormedit::object_id_t{900000},
        .begin_milliseconds = 1, .end_milliseconds = 2});
    const auto chart = song.read_charts().front()->id;
    CHECK_FALSE(wormedit::write_bms(song, {.chart = chart}).ok());
    CHECK_FALSE(wormedit::write_bmson(song, {.chart = chart}).ok());
    CHECK_FALSE(wormedit::write_osu_mania(song, {.chart = chart}).ok());
    CHECK_FALSE(wormedit::write_stepmania(song).ok());
    check_native_cut_roundtrip(song);
}

TEST_CASE("Native conversion without cuts keeps the direct serialized bytes", "[formats][conversion][export-cut]") {
    const auto song = stepmania_song();
    const auto direct = wormedit::write_native_project(song);
    REQUIRE(direct.ok());
    REQUIRE(direct.value);
    const auto converted = wormedit::convert_document(song, {});
    REQUIRE(converted);
    REQUIRE(converted.outputs.size() == 1);
    CHECK(converted.outputs.front().bytes == *direct.value);
    CHECK(converted.cut_audio_plans.empty());
}

namespace {
auto cut_roundtrip_note_times(const wormedit::chart_document_t& chart, const wormedit::timing_index_t& timing,
    const wormedit::measure_index_t& measures) -> std::optional<std::vector<wormedit::signed_rational_t>> {
    std::vector<wormedit::signed_rational_t> times;
    for (const auto& measure : chart.measures)
        for (const auto& note : measure.notes) {
            const auto time = timing.query_seconds(measures, note.position);
            if (!time) return {};
            times.push_back(time->value);
        }
    return times;
}
void check_cut_roundtrip_clock(const wormedit::song_document_t& song) {
    using namespace wormedit;
    REQUIRE(song.read_charts().size() == 1);
    const auto& chart = *song.read_charts().front();
    const auto timing = timing_index_t::from(chart, {.allow_bounded_clock = true});
    const auto measures = measure_index_t::from(chart);
    REQUIRE(timing.value);
    REQUIRE(measures);
    const auto times = cut_roundtrip_note_times(chart, *timing.value, *measures);
    REQUIRE(times);
    CHECK(*times == std::vector<signed_rational_t>{{1, 1}});
}
}
TEST_CASE("Export cuts reimport one shared clock in BMS osu and StepMania targets", "[formats][conversion][export-cut]") {
    using namespace wormedit;
    const auto [target, profile] = GENERATE(Catch::Generators::table<format_family_t, stepmania_profile_t>({
        {format_family_t::bms, stepmania_profile_t::ssc},
        {format_family_t::osu_mania, stepmania_profile_t::ssc},
        {format_family_t::stepmania, stepmania_profile_t::sm},
        {format_family_t::stepmania, stepmania_profile_t::ssc}}));
    auto fixture = linked_export_fixture(target);
    auto& song = fixture.history.document;
    auto& chart = *song.find_chart(fixture.source_id);
    chart.bpms.front().beats_per_minute = {120, 1};
    const auto lane = chart.input_profile.bindings.front().lane;
    chart.measures.front().notes = {{.id = object_id_t{900001},
        .position = {chart.measures.front().id, chart.divisions_per_standard_measure * 3 / 4}, .lane = lane,
        .sound = resource_id_t{900004}}};
    song.audio_assets.push_back({.id = asset_id_t{900003}, .source_path = "cut-note.wav"});
    song.resources.push_back({.id = resource_id_t{900004}, .name = "cut-note.wav",
        .asset = asset_id_t{900003}, .roles = {resource_role_t::sample}});
    song.export_cuts = {{.id = object_id_t{900002}, .begin_milliseconds = 500, .end_milliseconds = 1000}};
    const auto before = write_native_project(song).value;
    conversion_request_t request{.target = target, .chart = fixture.source_id, .loss_policy = loss_policy_t::warn};
    if (target == format_family_t::stepmania)
        request.stepmania = stepmania_export_policy_t{.file_profile = profile,
            .timing_syntax = stepmania_timing_syntax_t::native_warps};
    const auto converted = conversion_result_t::from(song, request);
    CAPTURE(target, profile);
    for (const auto& diagnostic : converted.diagnostics) UNSCOPED_INFO(diagnostic.message);
    REQUIRE(converted.ok());
    REQUIRE(converted.value.outputs.size() == 1);
    const auto parsed = reimport_linked_output(converted.value.outputs.front().bytes, target, profile);
    REQUIRE(parsed);
    check_cut_roundtrip_clock(*parsed);
    CHECK(write_native_project(song).value == before);
}

namespace {
auto rational_cut_fixture(wormedit::format_family_t target) -> wormedit::test::timing_links_fixture_t {
    using namespace wormedit;
    auto fixture = linked_export_fixture(target);
    auto& song = fixture.history.document;
    song.find_chart(fixture.target_id)->timing_links = {};
    auto& chart = *song.find_chart(fixture.source_id);
    chart.bpms.front().beats_per_minute = {381024000, 2880079};
    chart.beat_zero_audio_offset_seconds = {5539, 44100};
    while (chart.measures.size() < 12)
        chart.measures.push_back({.id = measure_id_t{900010 + chart.measures.size()}});
    const auto lane = chart.input_profile.bindings.front().lane;
    chart.measures.back().notes = {{.id = object_id_t{900001},
        .position = {chart.measures.back().id, chart.divisions_per_standard_measure / 96}, .lane = lane}};
    song.export_cuts = {{.id = object_id_t{900002}, .begin_milliseconds = 7382, .end_milliseconds = 14638}};
    return fixture;
}
auto collapsed_note_time(const wormedit::song_document_t& song, const wormedit::chart_document_t& chart) -> wormedit::signed_rational_t {
    using namespace wormedit;
    const auto timing = timing_index_t::from(chart);
    const auto measures = measure_index_t::from(chart);
    REQUIRE((timing.value && measures));
    const auto original = timing.value->query_seconds(*measures, chart.measures.back().notes.front().position);
    REQUIRE(original);
    const auto timeline = export_cut_timeline_t::from(song.export_cuts);
    const auto expected = timeline.value.collapsed_seconds(original->value);
    REQUIRE(expected);
    return *expected;
}
void check_sparse_cut_song(const wormedit::chart_document_t& output, wormedit::signed_rational_t expected) {
    using namespace wormedit;
    const auto output_timing = timing_index_t::from(output, {.allow_bounded_clock = true});
    const auto output_measures = measure_index_t::from(output);
    REQUIRE((output_timing.value && output_measures));
    const auto times = cut_roundtrip_note_times(output, *output_timing.value, *output_measures);
    REQUIRE(times);
    REQUIRE(times->size() == 1);
    CHECK(std::abs(to_double(times->front()) - to_double(expected)) < 0.006);
    CHECK(output.warps.empty());
}
void check_exported_cut_hold(const wormedit::chart_document_t& output) {
    using namespace wormedit;
    const auto& notes = output.measures.front().notes;
    REQUIRE(notes.size() == 1);
    REQUIRE(notes.front().end);
    const auto timing = timing_index_t::from(output);
    const auto measures = measure_index_t::from(output);
    REQUIRE((timing.value && measures));
    CHECK(timing.value->query_seconds(*measures, notes.front().position)->value == signed_rational_t{1, 4});
    CHECK(timing.value->query_seconds(*measures, *notes.front().end)->value == signed_rational_t{3, 4});
}
}
TEST_CASE("Rational export cuts preserve sparse SSC note rows and osu timestamps", "[formats][conversion][export-cut]") {
    using namespace wormedit;
    const auto target = GENERATE(format_family_t::osu_mania, format_family_t::stepmania);
    auto fixture = rational_cut_fixture(target);
    const auto& song = fixture.history.document;
    const auto expected = collapsed_note_time(song, *song.read_chart(fixture.source_id));
    conversion_request_t request{.target = target, .chart = fixture.source_id, .loss_policy = loss_policy_t::warn};
    if (target == format_family_t::stepmania)
        request.stepmania = stepmania_export_policy_t{.file_profile = stepmania_profile_t::ssc,
            .timing_syntax = stepmania_timing_syntax_t::native_warps};
    const auto converted = conversion_result_t::from(song, request);
    for (const auto& diagnostic : converted.diagnostics) UNSCOPED_INFO(diagnostic.message);
    REQUIRE(converted.ok());
    REQUIRE(converted.value.outputs.size() == 1);
    CHECK(converted.value.outputs.front().bytes.size() < 20'000);
    const auto parsed = reimport_linked_output(converted.value.outputs.front().bytes, target, stepmania_profile_t::ssc);
    REQUIRE(parsed);
    check_sparse_cut_song(*parsed->read_charts().front(), expected);
}
TEST_CASE("Exported holds retain Compose's collapsed duration in osu and SSC", "[formats][conversion][export-cut]") {
    using namespace wormedit;
    const auto target = GENERATE(format_family_t::osu_mania, format_family_t::stepmania);
    auto fixture = linked_export_fixture(target);
    auto& song = fixture.history.document;
    auto& chart = *song.find_chart(fixture.source_id);
    chart.bpms.front().beats_per_minute = {120, 1};
    const auto lane = chart.input_profile.bindings.front().lane;
    const auto measure = chart.measures.front().id;
    chart.measures.front().notes = {{.id = object_id_t{900001}, .position = {measure, 120},
        .lane = lane, .kind = note_kind_t::hold, .end = local_position_t{measure, 600}}};
    song.export_cuts = {{.id = object_id_t{900002}, .begin_milliseconds = 500, .end_milliseconds = 1000}};
    conversion_request_t request{.target = target, .chart = fixture.source_id, .loss_policy = loss_policy_t::warn};
    if (target == format_family_t::stepmania) request.stepmania = stepmania_export_policy_t{};
    const auto converted = conversion_result_t::from(song, request);
    for (const auto& diagnostic : converted.diagnostics) UNSCOPED_INFO(diagnostic.message);
    REQUIRE(converted.ok());
    REQUIRE(converted.value.outputs.size() == 1);
    const auto parsed = reimport_linked_output(converted.value.outputs.front().bytes, target, stepmania_profile_t::ssc);
    REQUIRE(parsed);
    check_exported_cut_hold(*parsed->read_charts().front());
    CHECK(chart.measures.front().notes.front().end->row == 600);
}
TEST_CASE("Conversion preview groups cut omissions without blocking export", "[formats][conversion][export-cut]") {
    using namespace wormedit;
    auto fixture = linked_export_fixture(format_family_t::osu_mania);
    auto& song = fixture.history.document;
    auto& chart = *song.find_chart(fixture.source_id);
    chart.bpms.front().beats_per_minute = {120, 1};
    chart.timing_links = {};
    const auto lane = chart.input_profile.bindings.front().lane;
    const auto measure = chart.measures.front().id;
    chart.measures.front().notes.clear();
    for (std::uint64_t id = 900010; id < 900013; ++id)
        chart.measures.front().notes.push_back({.id = object_id_t{id}, .position = {measure, 120 + (id - 900010) * 10},
            .lane = lane, .sound = resource_id_t{900004}});
    song.audio_assets.push_back({.id = asset_id_t{900003}, .source_path = "sample.wav",
        .stream = {.sample_rate = 1000, .channels = 1, .frame_count = 1000}});
    song.resources.push_back({.id = resource_id_t{900004}, .name = "sample.wav", .asset = asset_id_t{900003}});
    song.export_cuts = {{.id = object_id_t{900002}, .begin_milliseconds = 500, .end_milliseconds = 1000}};
    const conversion_request_t request{.target = format_family_t::native_project};
    const auto diagnostics = diagnose_conversion_preparation(song, request);
    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics.front().severity == diagnostic_severity_t::warning);
    CHECK(diagnostics.front().code == conversion_diagnostic_code_t::target_diagnostic);
    const auto converted = conversion_result_t::from(song, request);
    REQUIRE(converted.ok());
    REQUIRE(converted.value.outputs.size() == 1);
    CHECK(converted.diagnostics.size() == 1);
}

namespace {
void check_conversion_osbx(bool condition) { CHECK(condition); }
void require_conversion_osbx(bool condition) { REQUIRE(condition); }
}

TEST_CASE("OSBX osu packages contain one separate validated storyboard and atomic diagnostics",
          "[unit][format][conversion][osbx]") {
    using namespace wormedit;
    constexpr std::string_view source = "osu file format v14\n[General]\nMode:3\n"
        "[Difficulty]\nCircleSize:4\n[TimingPoints]\n0,500,4,0,0,100,1,0\n"
        "[HitObjects]\n64,192,1000,1,0,0:0:0:0:\n";
    auto parsed = parse_osu_mania_bytes(std::as_bytes(std::span{source.data(), source.size()}));
    require_conversion_osbx(static_cast<bool>(parsed.value));
    auto song = std::move(*parsed.value);
    osbx_document_t scene{
        .images = {{.id = "image", .file = "sprite.png"}},
        .nodes = {{.id = "sprite", .kind = osbx_node_kind_t::sprite, .parent = "parent",
                   .image = "image", .ops = {{.id = "offset", .track = {.default_value = {100, 0}}}}}}};
    osbx_transform_op_t turn{.id = "turn", .kind = osbx_transform_kind_t::rotate,
        .track = {.default_value = {0}, .keys = {{.id = "a", .value = {0}},
            {.id = "b", .time_ms = 2000, .value = {90}}}}};
    scene.nodes.push_back({.id = "parent", .ops = {turn}});
    song.storyboards = {{.id = storyboard_id_t{1}, .osbx = std::move(scene)}};
    conversion_request_t request{.target = format_family_t::osu_mania,
        .osbx_image_sizes = {{.image_id = "image", .width = 64, .height = 48}},
        .osbx_playback_span = osbx_osb_span_t{0, 2000}};

    SECTION("bounded bake is a warning even with the default reject-loss policy") {
        const auto converted = convert_document(song, request);
        for (const auto& diagnostic : converted.diagnostics) INFO(diagnostic.message);
        require_conversion_osbx(static_cast<bool>(converted));
        require_conversion_osbx(static_cast<bool>(converted.outputs.size() == 2));
        std::size_t storyboards{};
        for (const auto& output : converted.outputs) {
            check_conversion_osbx(static_cast<bool>(output.reimport_validated));
            if (std::filesystem::path(output.suggested_filename).extension() == ".osb") {
                ++storyboards;
                const auto model = parse_osb(output.bytes);
                require_conversion_osbx(static_cast<bool>(model.value));
                check_conversion_osbx(static_cast<bool>(model.value->sprites.size() == 1));
            } else check_conversion_osbx(static_cast<bool>(output.bytes.find("Sprite,") == std::string::npos));
        }
        check_conversion_osbx(static_cast<bool>(storyboards == 1));
        check_conversion_osbx(static_cast<bool>(std::ranges::any_of(converted.diagnostics, [](const auto& diagnostic) {
            return diagnostic.severity == diagnostic_severity_t::warning && !diagnostic.loss &&
                diagnostic.message.find("sprite baked; worst normalized error") != std::string::npos;
        })));
    }
    SECTION("skew refuses the entire output set") {
        song.storyboards.front().osbx->nodes.back().ops = {{.id = "bad-skew",
            .kind = osbx_transform_kind_t::skew, .track = {.default_value = {10, 0}}}};
        const auto converted = convert_document(song, request);
        check_conversion_osbx(!static_cast<bool>(converted));
        check_conversion_osbx(static_cast<bool>(converted.outputs.empty()));
        check_conversion_osbx(static_cast<bool>(std::ranges::any_of(converted.diagnostics, [](const auto& diagnostic) {
            return diagnostic.message.find("bad-skew") != std::string::npos;
        })));
    }
    SECTION("multiple generated scenes share one package despite repeated local IDs") {
        auto second = song.storyboards.front();
        second.id = storyboard_id_t{2};
        second.osbx->images.front().file = "second.png";
        song.storyboards.push_back(std::move(second));
        const auto converted = convert_document(song, request);
        for (const auto& diagnostic : converted.diagnostics) INFO(diagnostic.message);
        require_conversion_osbx(static_cast<bool>(converted));
        require_conversion_osbx(converted.outputs.size() == 2);
        const auto output = std::ranges::find_if(converted.outputs, [](const auto& item) {
            return std::filesystem::path(item.suggested_filename).extension() == ".osb";
        });
        require_conversion_osbx(output != converted.outputs.end());
        const auto model = parse_osb(output->bytes);
        require_conversion_osbx(model.value.has_value());
        require_conversion_osbx(model.value->sprites.size() == 2);
        check_conversion_osbx(model.value->sprites[0].path == "sprite.png");
        check_conversion_osbx(model.value->sprites[1].path == "second.png");
    }
    SECTION("storyboard counts against the shared output limit") {
        const auto converted = convert_document(song, request, {.maximum_outputs = 1});
        check_conversion_osbx(!static_cast<bool>(converted));
        check_conversion_osbx(static_cast<bool>(converted.outputs.empty()));
        check_conversion_osbx(static_cast<bool>(std::ranges::any_of(converted.diagnostics, [](const auto& diagnostic) {
            return diagnostic.code == conversion_diagnostic_code_t::output_limit;
        })));
    }
}

TEST_CASE("Legacy OSB-only conversion keeps embedded bytes without OSBX media preflight",
          "[unit][format][conversion][osbx][storyboard]") {
    using namespace wormedit;
    constexpr std::string_view source = "osu file format v14\n[General]\nMode:3\n"
        "[Difficulty]\nCircleSize:4\n[Events]\nSprite,Foreground,Centre,missing.png,320,240\n"
        " F,0,0,2000,1,1\n[TimingPoints]\n0,500,4,0,0,100,1,0\n"
        "[HitObjects]\n64,192,1000,1,0,0:0:0:0:\n";
    const auto parsed = parse_osu_mania_bytes(std::as_bytes(std::span{source.data(), source.size()}));
    require_conversion_osbx(parsed.value.has_value());
    const auto expected = write_osu_mania(*parsed.value);
    require_conversion_osbx(expected.value.has_value());
    conversion_request_t request{.target = format_family_t::osu_mania};
    check_conversion_osbx(!prepare_osu_storyboard_export(*parsed.value, request, {}, {}).has_value());
    check_conversion_osbx(!request.osbx_playback_span.has_value());
    const auto converted = convert_document(*parsed.value, request);
    require_conversion_osbx(static_cast<bool>(converted));
    require_conversion_osbx(converted.outputs.size() == 1);
    check_conversion_osbx(converted.outputs.front().bytes == *expected.value);
    check_conversion_osbx(converted.storyboard_images.empty());
}

namespace {
auto native_storyboard_request() -> wormedit::conversion_request_t {
    using namespace wormedit;
    return {.target = format_family_t::stepmania, .stepmania = stepmania_export_policy_t{},
        .loss_policy = loss_policy_t::reject,
        .osbx_image_sizes = {{.image_id = "image", .width = 64, .height = 48}},
        .osbx_playback_span = osbx_osb_span_t{0, 2000}};
}
auto native_storyboard_song() -> wormedit::song_document_t {
    using namespace wormedit;
    auto song = stepmania_song();
    song.storyboards = {{.id = storyboard_id_t{1}, .osbx = osbx_document_t{
        .images = {{.id = "image", .file = "sprite.png"}},
        .nodes = {{.id = "sprite", .kind = osbx_node_kind_t::sprite, .image = "image",
            .ops = {{.id = "position", .track = {.default_value = {320,240}}}}}}}}};
    return song;
}
class native_children_backend_t final : public wormedit::osbx_lua_backend_t {
    wormedit::osbx_layer_t layer_;
  public:
    explicit native_children_backend_t(wormedit::osbx_layer_t layer = wormedit::osbx_layer_t::background) : layer_(layer) {}
    auto prepare_track(const wormedit::osbx_lua_track_link_t&, std::size_t) const
        -> wormedit::diagnosed_t<std::vector<wormedit::osbx_key_t<>>> override { return {}; }
    auto prepare_children(const wormedit::osbx_lua_link_t&) const
        -> wormedit::diagnosed_t<wormedit::osbx_document_t> override {
        auto scene = *native_storyboard_song().storyboards.front().osbx;
        scene.nodes.front().layer = layer_;
        return {.value = std::move(scene)};
    }
};
}

TEST_CASE("SSC native storyboards carry supplemental Lua outside chart reimport", "[formats][conversion][stepmania][storyboard]") {
    const auto song = native_storyboard_song();
    const auto converted = wormedit::convert_document(song, native_storyboard_request());
    for (const auto& diagnostic : converted.diagnostics) INFO(diagnostic.message);
    require_conversion_osbx(static_cast<bool>(converted));
    require_conversion_osbx(static_cast<bool>(converted.outputs.size() == 1));
    require_conversion_osbx(static_cast<bool>(converted.storyboard_files.size() == 1));
    check_conversion_osbx(static_cast<bool>(converted.outputs.front().reimport_validated));
    check_conversion_osbx(static_cast<bool>(converted.outputs.front().bytes.find("=wormedit-storyboard/background=1=0=0=1") != std::string::npos));
    check_conversion_osbx(static_cast<bool>(converted.storyboard_files.front().bytes.find("GAMESTATE:GetSongPosition()") != std::string::npos));
    require_conversion_osbx(static_cast<bool>(converted.stepmania_storyboard_report.size() == 1));
    check_conversion_osbx(static_cast<bool>(converted.stepmania_storyboard_report.front().classification == wormedit::osbx_osb_class_t::exact));
}

TEST_CASE("SSC native clipping and retained hooks never reject the gameplay chart", "[formats][conversion][stepmania][storyboard]") {
    auto song = native_storyboard_song();
    song.storyboards.front().osbx->nodes.front().ops.front().track.default_value = {-100,0};
    const auto clipped = wormedit::convert_document(song, native_storyboard_request());
    require_conversion_osbx(static_cast<bool>(clipped));
    require_conversion_osbx(static_cast<bool>(clipped.outputs.size() == 1));
    check_conversion_osbx(static_cast<bool>(clipped.storyboard_files.empty()));
    require_conversion_osbx(static_cast<bool>(clipped.stepmania_storyboard_report.size() == 1));
    check_conversion_osbx(static_cast<bool>(clipped.stepmania_storyboard_report.front().classification == wormedit::osbx_osb_class_t::unsupported));
    song = native_storyboard_song();
    song.metadata.format_group_for(wormedit::format_family_t::stepmania).ordered_fields.emplace_back("BGCHANGES", "0=original.png=1=0=0=1");
    const auto retained = wormedit::convert_document(song, native_storyboard_request());
    require_conversion_osbx(static_cast<bool>(retained));
    require_conversion_osbx(static_cast<bool>(retained.outputs.size() == 1));
    check_conversion_osbx(static_cast<bool>(retained.storyboard_files.empty()));
    check_conversion_osbx(static_cast<bool>(retained.outputs.front().bytes.find("#BGCHANGES:0=original.png=1=0=0=1;") != std::string::npos));
    check_conversion_osbx(static_cast<bool>(retained.outputs.front().bytes.find("wormedit-storyboard") == std::string::npos));
}

TEST_CASE("SSC actor bundles include multiple scenes under one hook", "[formats][conversion][stepmania][storyboard]") {
    auto song = native_storyboard_song();
    auto other = song.storyboards.front();
    other.id = wormedit::storyboard_id_t{2};
    song.storyboards.push_back(std::move(other));
    const auto converted = wormedit::convert_document(song, native_storyboard_request());
    require_conversion_osbx(static_cast<bool>(converted));
    require_conversion_osbx(static_cast<bool>(converted.storyboard_files.size() == 1));
    check_conversion_osbx(static_cast<bool>(converted.stepmania_storyboard_report.size() == 2));
    const auto& chart = converted.outputs.front().bytes;
    const auto tag = chart.find("#BGCHANGES:");
    require_conversion_osbx(static_cast<bool>(tag != std::string::npos));
    check_conversion_osbx(static_cast<bool>(chart.find("#BGCHANGES:", tag + 1) == std::string::npos));
    check_conversion_osbx(static_cast<bool>(converted.storyboard_files.front().bytes.find("unpack(layers)") != std::string::npos));
}

TEST_CASE("SSC retained hook losses include immutable prepared subtree children", "[formats][conversion][stepmania][storyboard]") {
    using namespace wormedit;
    const auto implementation = GENERATE(stepmania_implementation_profile_t::stepmania_5_1,
        stepmania_implementation_profile_t::etterna_0_75_1);
    auto request = native_storyboard_request();
    request.stepmania->implementation_profile = implementation;
    auto song = native_storyboard_song();
    osbx_lua_link_t link;
    link.id = "children-link";
    link.source.source_reference = "children.lua";
    link.source.frozen_source = "return function(ctx) end";
    link.source.source_hash = native_project_content_checksum(link.source.frozen_source);
    song.storyboards.front().osbx->nodes = {{.id = "group", .kind = osbx_node_kind_t::group, .children_link = link}};
    const native_children_backend_t backend;
    const auto prepared = prepare_osbx_lua_scenes(song, backend);
    for (const auto& scene : prepared)
        for (const auto& diagnostic : scene.diagnostics) UNSCOPED_INFO(diagnostic.message);
    require_conversion_osbx(prepared.size() == 1 && prepared.front().complete);
    const osbx_lua_prepared_scope_t scope{prepared};
    song.metadata.format_group_for(format_family_t::stepmania).ordered_fields.emplace_back("BGCHANGES", "0=original.png=1=0=0=1");
    const auto converted = convert_document(song, request);
    require_conversion_osbx(static_cast<bool>(converted));
    check_conversion_osbx(converted.storyboard_files.empty());
    require_conversion_osbx(converted.stepmania_storyboard_report.size() == 1);
    check_conversion_osbx(converted.stepmania_storyboard_report.front().classification == osbx_osb_class_t::unsupported);
    check_conversion_osbx(converted.stepmania_storyboard_report.front().reason.find("Retained source") != std::string::npos);
}

TEST_CASE("SSC video mode reports unavailable and none mode omits native artifacts", "[formats][conversion][stepmania][storyboard]") {
    auto request = native_storyboard_request();
    request.stepmania->storyboard = wormedit::stepmania_storyboard_mode_t::video;
    const auto video = wormedit::convert_document(native_storyboard_song(), request);
    require_conversion_osbx(static_cast<bool>(video));
    check_conversion_osbx(static_cast<bool>(video.storyboard_files.empty()));
    require_conversion_osbx(static_cast<bool>(video.stepmania_storyboard_report.size() == 1));
    check_conversion_osbx(static_cast<bool>(video.stepmania_storyboard_report.front().reason.find("not yet available") != std::string::npos));
    request.stepmania->storyboard = wormedit::stepmania_storyboard_mode_t::none;
    const auto none = wormedit::convert_document(native_storyboard_song(), request);
    require_conversion_osbx(static_cast<bool>(none));
    check_conversion_osbx(static_cast<bool>(none.storyboard_files.empty()));
    check_conversion_osbx(static_cast<bool>(none.stepmania_storyboard_report.empty()));
}

TEST_CASE("typed unresolved source promises reject conversion without changing the input", "[conversion][resource_plan]") {
    using namespace wormedit;
    auto song = stepmania_song();
    auto& chart = song.charts.front();
    const auto id = chart.id;
    const auto measure = chart.measures.front().id;
    song.audio_assets.push_back({.id = asset_id_t{90001}, .source_path = "one.wav"});
    song.audio_assets.push_back({.id = asset_id_t{90002}, .source_path = "two.wav"});
    song.resources.push_back({.id = resource_id_t{90003}, .name = "one.wav", .asset = asset_id_t{90001}, .roles = {resource_role_t::bgm}});
    song.resources.push_back({.id = resource_id_t{90004}, .name = "two.wav", .asset = asset_id_t{90002}, .roles = {resource_role_t::bgm}});
    chart.bgm_source_lanes.push_back({.id = object_id_t{90005}, .name = "Typed"});
    for (std::uint64_t i = 0; i < 2; ++i)
        chart.measures.front().bgm_events.push_back({.id = object_id_t{90006 + i}, .position = {measure, i * 120},
            .source_lane = object_id_t{90005}, .sound = resource_id_t{90003 + i},
            .sample_reference = sample_reference_binding_t{.reference = bms_wav_reference_t{.index = "0A"},
                .bound_sound = resource_id_t{90003 + i}, .bound_asset = asset_id_t{90001 + i}}});
    const auto before = write_native_project(song);
    REQUIRE(before.value);
    const auto converted = convert_document(song, {.target = format_family_t::osu_mania,
        .chart = id, .loss_policy = loss_policy_t::allow});
    CHECK_FALSE(converted);
    CHECK(converted.outputs.empty());
    CHECK(std::ranges::any_of(converted.diagnostics, [](const auto& diagnostic) {
        return diagnostic.message.find("BMS sample") != std::string::npos;
    }));
    CHECK(write_native_project(song).value == before.value);
}


namespace {
auto typed_osu_conversion_song() -> wormedit::song_document_t {
    using namespace wormedit;
    const auto profile = make_bms_7k_profile();
    song_document_t song{.id = song_id_t{1},
        .audio_assets = {{.id = asset_id_t{10}, .source_path = "normal-hitnormal.wav"}},
        .resources = {{.id = resource_id_t{11}, .name = "normal-hitnormal.wav", .asset = asset_id_t{10},
            .roles = {resource_role_t::sample, resource_role_t::bgm}}},
        .charts = {{.id = chart_id_t{2}, .measures = {{.id = measure_id_t{30}}},
            .lane_layouts = {{.id = object_id_t{31}, .position = {measure_id_t{30}, 0}, .layout = profile.layout}},
            .input_profile = profile.input,
            .bgm_source_lanes = {{.id = object_id_t{32}, .name = "Typed"}},
            .bpms = {{.id = object_id_t{33}, .position = {measure_id_t{30}, 0}, .beats_per_minute = {120, 1}}}}}};
    const osu_hitsample_t descriptor{.normal_set = osu_sample_set_t::normal, .custom_index = 1, .volume = 100};
    auto& measure = song.charts.front().measures.front();
    measure.notes.push_back({.id = object_id_t{34}, .position = {measure.id, 0}, .lane = profile.layout.lanes.front().id,
        .provenance = {.source_field = std::string(typed_lua_osu_sample_origin)}, .osu_hitsample = descriptor});
    measure.bgm_events.push_back({.id = object_id_t{35}, .position = {measure.id, 120}, .source_lane = object_id_t{32},
        .provenance = {.source_field = std::string(typed_lua_osu_sample_origin)}, .osu_hitsample = descriptor});
    return song;
}
}

namespace {
using namespace wormedit;
void check_detached_typed_sample_lowering(const song_document_t& song, format_family_t target,
    const std::string& before) {
    const auto result = convert_document(song, {.target = target, .chart = chart_id_t{2}, .loss_policy = loss_policy_t::allow});
    INFO(static_cast<int>(target));
    for (const auto& diagnostic : result.diagnostics) UNSCOPED_INFO(diagnostic.message);
    REQUIRE(result);
    REQUIRE(result.outputs.size() == 1);
    CHECK_FALSE(result.outputs.front().bytes.empty());
    CHECK(write_native_project(song).value == before);
    CHECK(song.charts.front().measures.front().notes.front().osu_hitsample.has_value());
    CHECK(song.charts.front().measures.front().bgm_events.front().osu_hitsample.has_value());
}
void check_typed_sample_refusal(const song_document_t& song, format_family_t target, const std::string& before) {
    const auto result = convert_document(song, {.target = target, .chart = chart_id_t{2}, .loss_policy = loss_policy_t::allow});
    CHECK_FALSE(result);
    CHECK(result.outputs.empty());
    CHECK_FALSE(result.diagnostics.empty());
    CHECK(write_native_project(song).value == before);
}
}

TEST_CASE("typed Lua single unit-gain osu note and BGM lower only on the detached target", "[conversion][resource_plan]") {
    using namespace wormedit;
    auto song = typed_osu_conversion_song();
    SECTION("explicit unit gain") {}
    SECTION("inherited unit gain") {
        song.charts.front().measures.front().notes.front().osu_hitsample->volume = 0;
        song.charts.front().measures.front().bgm_events.front().osu_hitsample->volume = 0;
    }
    SECTION("custom registered normal") {
        song.charts.front().measures.front().notes.front().osu_hitsample->filename = "normal-hitnormal.wav";
        song.charts.front().measures.front().bgm_events.front().osu_hitsample->filename = "normal-hitnormal.wav";
    }
    const auto before = write_native_project(song);
    REQUIRE(before.value);
    for (const auto target : {format_family_t::bms, format_family_t::bmson})
        check_detached_typed_sample_lowering(song, target, *before.value);
}

TEST_CASE("typed Lua unrepresentable osu contributions reject without bytes or source changes", "[conversion][resource_plan]") {
    using namespace wormedit;
    auto song = typed_osu_conversion_song();
    auto& descriptor = *song.charts.front().measures.front().bgm_events.front().osu_hitsample;
    SECTION("scaled gain") { descriptor.volume = 80; }
    SECTION("missing component") { descriptor.clap = true; }
    SECTION("two resolved contributions") {
        descriptor.clap = true;
        song.audio_assets.push_back({.id = asset_id_t{20}, .source_path = "normal-hitclap.wav"});
        song.resources.push_back({.id = resource_id_t{21}, .name = "normal-hitclap.wav", .asset = asset_id_t{20}, .roles = {resource_role_t::sample}});
    }
    SECTION("two contributions to the same registered resource") {
        descriptor.clap = true;
        descriptor.filename = "normal-hitclap.wav";
        song.audio_assets.push_back({.id = asset_id_t{20}, .source_path = "normal-hitclap.wav"});
        song.resources.push_back({.id = resource_id_t{21}, .name = "normal-hitclap.wav", .asset = asset_id_t{20}, .roles = {resource_role_t::sample}});
    }
    const auto before = write_native_project(song);
    REQUIRE(before.value);
    for (const auto target : {format_family_t::bms, format_family_t::bmson})
        check_typed_sample_refusal(song, target, *before.value);
}

TEST_CASE("NotITG SM foreground bundles XML and Lua sidecars outside chart reimport", "[formats][conversion][notitg]") {
    using namespace wormedit;
    auto song = native_storyboard_song();
    song.charts.front().scrolls.clear();
    song.charts.front().speeds.clear();
    song.charts.front().warps.clear();
    song.charts.front().pauses.clear();
    song.storyboards.front().osbx->nodes.front().layer = osbx_layer_t::foreground;
    auto request = native_storyboard_request();
    request.stepmania->file_profile = stepmania_profile_t::sm;
    request.stepmania->implementation_profile = stepmania_implementation_profile_t::notitg_v4;
    const auto converted = convert_document(song, request);
    for (const auto& diagnostic : converted.diagnostics) UNSCOPED_INFO(diagnostic.message);
    require_conversion_osbx(static_cast<bool>(converted));
    require_conversion_osbx(converted.outputs.size() == 1);
    check_conversion_osbx(converted.outputs.front().suggested_filename.ends_with(".sm"));
    check_conversion_osbx(converted.outputs.front().reimport_validated);
    check_conversion_osbx(converted.outputs.front().bytes.find("#FGCHANGES:") != std::string::npos);
    check_conversion_osbx(converted.outputs.front().bytes.find("wormedit-storyboard/main.xml") != std::string::npos);
    require_conversion_osbx(converted.storyboard_files.size() == 3);
    check_conversion_osbx(converted.stepmania_storyboard_report.front().classification == osbx_osb_class_t::exact);
}

TEST_CASE("NotITG unverified backgrounds and owned foreground hooks keep chart output", "[formats][conversion][notitg]") {
    using namespace wormedit;
    auto song = native_storyboard_song();
    song.charts.front().scrolls.clear();
    song.charts.front().speeds.clear();
    song.charts.front().warps.clear();
    song.charts.front().pauses.clear();
    auto request = native_storyboard_request();
    request.stepmania->file_profile = stepmania_profile_t::sm;
    request.stepmania->implementation_profile = stepmania_implementation_profile_t::notitg_v4;
    const auto background = convert_document(song, request);
    for (const auto& diagnostic : background.diagnostics) UNSCOPED_INFO(diagnostic.message);
    require_conversion_osbx(static_cast<bool>(background));
    check_conversion_osbx(background.storyboard_files.empty());
    check_conversion_osbx(background.stepmania_storyboard_report.front().reason.find("background lifecycle") != std::string::npos);
    song.storyboards.front().osbx->nodes.front().layer = osbx_layer_t::foreground;
    song.metadata.format_group_for(format_family_t::stepmania).ordered_fields.emplace_back("FGCHANGES", "0=original.xml=1=0=0=1");
    const auto retained = convert_document(song, request);
    require_conversion_osbx(static_cast<bool>(retained));
    check_conversion_osbx(retained.storyboard_files.empty());
    check_conversion_osbx(retained.outputs.front().bytes.find("#FGCHANGES:0=original.xml") != std::string::npos);
}

namespace {
auto etterna_storyboard_request(wormedit::storyboard_conditional_layer_t choice = wormedit::storyboard_conditional_layer_t::pass)
    -> wormedit::conversion_request_t {
    auto request = native_storyboard_request();
    request.stepmania->implementation_profile = wormedit::stepmania_implementation_profile_t::etterna_0_75_1;
    request.stepmania->conditional_layer = choice;
    return request;
}
auto etterna_layered_song() -> wormedit::song_document_t {
    auto song = native_storyboard_song();
    auto& scene = *song.storyboards.front().osbx;
    for (const auto [id, layer] : std::array{
            std::pair{"passed", wormedit::osbx_layer_t::pass}, std::pair{"failed", wormedit::osbx_layer_t::fail},
            std::pair{"front", wormedit::osbx_layer_t::foreground}}) {
        auto node = scene.nodes.front(); node.id = id; node.layer = layer;
        node.ops.front().id = std::string{id} + "-position";
        scene.nodes.push_back(std::move(node));
    }
    return song;
}
void check_etterna_hooks(const wormedit::conversion_result_t& converted) {
    require_conversion_osbx(static_cast<bool>(converted));
    require_conversion_osbx(converted.outputs.size() == 1);
    const auto& output = converted.outputs.front();
    check_conversion_osbx(output.reimport_validated);
    check_conversion_osbx(output.bytes.find("#BGCHANGES:-2=wormedit-storyboard/background=1=0=0=1;") != std::string::npos);
    check_conversion_osbx(output.bytes.find("#FGCHANGES:-2=wormedit-storyboard/foreground=1=0=0=1;") != std::string::npos);
    require_conversion_osbx(converted.storyboard_files.size() == 2);
}
void check_etterna_choice(const wormedit::conversion_result_t& converted, wormedit::storyboard_conditional_layer_t choice) {
    std::string actors;
    for (const auto& file : converted.storyboard_files) actors += file.bytes;
    check_conversion_osbx((actors.find("id=\"passed\"") != std::string::npos) == (choice == wormedit::storyboard_conditional_layer_t::pass));
    check_conversion_osbx((actors.find("id=\"failed\"") != std::string::npos) == (choice == wormedit::storyboard_conditional_layer_t::fail));
    check_conversion_osbx(actors.find("id=\"front\"") != std::string::npos);
    check_conversion_osbx(actors.find("GAMESTATE:GetSongPosition()") != std::string::npos);
}
}

TEST_CASE("Etterna native SSC carries both exact preroll hooks with one fixed conditional choice", "[formats][conversion][stepmania][storyboard][etterna]") {
    const auto choice = GENERATE(wormedit::storyboard_conditional_layer_t::pass,
        wormedit::storyboard_conditional_layer_t::fail, wormedit::storyboard_conditional_layer_t::neither);
    const auto song = etterna_layered_song();
    const auto before = wormedit::write_native_project(song).value;
    const auto converted = wormedit::convert_document(song, etterna_storyboard_request(choice));
    check_etterna_hooks(converted); check_etterna_choice(converted, choice);
    check_conversion_osbx(wormedit::write_native_project(song).value == before);
}

TEST_CASE("Etterna retained BG ownership leaves foreground actors and gameplay available", "[formats][conversion][stepmania][storyboard][etterna]") {
    using namespace wormedit;
    auto song = etterna_layered_song();
    song.metadata.format_group_for(format_family_t::stepmania).ordered_fields.emplace_back("BGCHANGES", "0=original.png=1=0=0=1");
    const auto result = convert_document(song, etterna_storyboard_request());
    require_conversion_osbx(static_cast<bool>(result)); require_conversion_osbx(result.outputs.size() == 1);
    check_conversion_osbx(result.outputs.front().bytes.find("#BGCHANGES:0=original.png=1=0=0=1;") != std::string::npos);
    check_conversion_osbx(result.outputs.front().bytes.find("#FGCHANGES:-2=wormedit-storyboard/foreground") != std::string::npos);
    require_conversion_osbx(result.storyboard_files.size() == 1);
    check_conversion_osbx(result.storyboard_files.front().relative_path == "wormedit-storyboard/foreground/default.lua");
    check_conversion_osbx(std::ranges::any_of(result.stepmania_storyboard_report, [](const auto& report) {
        return report.classification == osbx_osb_class_t::unsupported && report.reason.find("Retained source") != std::string::npos;
    }));
}

TEST_CASE("Etterna unsupported actor motion remains an advisory even with chart loss rejection", "[formats][conversion][stepmania][storyboard][etterna]") {
    auto song = native_storyboard_song();
    song.storyboards.front().osbx->nodes.front().ops.front().track.default_value = {-100,0};
    const auto result = wormedit::convert_document(song, etterna_storyboard_request());
    require_conversion_osbx(static_cast<bool>(result)); require_conversion_osbx(result.outputs.size() == 1);
    check_conversion_osbx(result.outputs.front().reimport_validated);
    check_conversion_osbx(result.storyboard_files.empty());
    require_conversion_osbx(result.stepmania_storyboard_report.size() == 1);
    check_conversion_osbx(result.stepmania_storyboard_report.front().classification == wormedit::osbx_osb_class_t::unsupported);
}

TEST_CASE("Etterna actors none and unavailable video stay separate policies", "[formats][conversion][stepmania][storyboard][etterna]") {
    auto request = etterna_storyboard_request();
    const auto actors = wormedit::convert_document(native_storyboard_song(), request);
    require_conversion_osbx(static_cast<bool>(actors)); check_conversion_osbx(!actors.storyboard_files.empty());
    request.stepmania->storyboard = wormedit::stepmania_storyboard_mode_t::none;
    const auto none = wormedit::convert_document(native_storyboard_song(), request);
    require_conversion_osbx(static_cast<bool>(none)); check_conversion_osbx(none.storyboard_files.empty());
    request.stepmania->storyboard = wormedit::stepmania_storyboard_mode_t::video;
    const auto video = wormedit::convert_document(native_storyboard_song(), request);
    require_conversion_osbx(static_cast<bool>(video)); check_conversion_osbx(video.storyboard_files.empty());
    require_conversion_osbx(video.stepmania_storyboard_report.size() == 1);
    check_conversion_osbx(video.stepmania_storyboard_report.front().reason.find("not yet available") != std::string::npos);
}

namespace {
auto etterna_choice_advisories(const wormedit::conversion_result_t& result) -> std::ptrdiff_t {
    return std::ranges::count_if(result.diagnostics, [](const auto& diagnostic) {
        return diagnostic.message.find("Etterna storyboard conditional state ") != std::string::npos;
    });
}
void check_fixed_etterna_choice(const wormedit::conversion_result_t& result, std::string_view name) {
    require_conversion_osbx(static_cast<bool>(result));
    check_conversion_osbx(etterna_choice_advisories(result) == 1);
    const auto found = std::ranges::find_if(result.diagnostics, [](const auto& diagnostic) {
        return diagnostic.message.find("Etterna storyboard conditional state ") != std::string::npos;
    });
    require_conversion_osbx(found != result.diagnostics.end());
    check_conversion_osbx(found->severity == wormedit::diagnostic_severity_t::warning);
    check_conversion_osbx(found->code == wormedit::conversion_diagnostic_code_t::target_diagnostic);
    check_conversion_osbx(found->message.find(std::string{name} + " is fixed for export") != std::string::npos);
    check_conversion_osbx(found->message.find("no gameplay-trigger interactivity") != std::string::npos);
}
}
TEST_CASE("Etterna fixed conditional choice is advisory under reject policy for every shared state", "[formats][conversion][stepmania][storyboard][etterna]") {
    using namespace wormedit;
    const auto [choice, label] = GENERATE(std::pair{storyboard_conditional_layer_t::pass, "Pass"},
        std::pair{storyboard_conditional_layer_t::fail, "Fail"}, std::pair{storyboard_conditional_layer_t::neither, "Neither"});
    auto song = etterna_layered_song();
    auto second = song.storyboards.front(); second.id = storyboard_id_t{2}; song.storyboards.push_back(std::move(second));
    const auto result = convert_document(song, etterna_storyboard_request(choice));
    check_fixed_etterna_choice(result, label);
    require_conversion_osbx(result.outputs.size() == 1);
    check_conversion_osbx(result.outputs.front().reimport_validated);
    check_conversion_osbx(std::ranges::all_of(result.stepmania_storyboard_report, [](const auto& report) {
        return report.classification == osbx_osb_class_t::exact;
    }));
}
TEST_CASE("Etterna ordinary layers and other routes do not receive a conditional advisory", "[formats][conversion][stepmania][storyboard][etterna]") {
    using namespace wormedit;
    const auto ordinary = convert_document(native_storyboard_song(), etterna_storyboard_request());
    require_conversion_osbx(static_cast<bool>(ordinary)); check_conversion_osbx(etterna_choice_advisories(ordinary) == 0);
    const auto shared = convert_document(etterna_layered_song(), native_storyboard_request());
    require_conversion_osbx(static_cast<bool>(shared)); check_conversion_osbx(etterna_choice_advisories(shared) == 0);
    for (const auto mode : {stepmania_storyboard_mode_t::none, stepmania_storyboard_mode_t::video}) {
        auto request = etterna_storyboard_request(); request.stepmania->storyboard = mode;
        const auto result = convert_document(etterna_layered_song(), request);
        require_conversion_osbx(static_cast<bool>(result)); check_conversion_osbx(etterna_choice_advisories(result) == 0);
    }
}
TEST_CASE("Etterna fixed-choice advisory does not replace retained source loss reasons", "[formats][conversion][stepmania][storyboard][etterna]") {
    using namespace wormedit;
    auto song = etterna_layered_song();
    song.metadata.format_group_for(format_family_t::stepmania).ordered_fields.emplace_back("BGCHANGES", "0=original.png=1=0=0=1");
    const auto result = convert_document(song, etterna_storyboard_request());
    check_fixed_etterna_choice(result, "Pass");
    check_conversion_osbx(std::ranges::any_of(result.stepmania_storyboard_report, [](const auto& report) {
        return report.classification == osbx_osb_class_t::unsupported && report.reason.find("Retained source") != std::string::npos;
    }));
}
TEST_CASE("Etterna conditional advisory sees the canonical prepared subtree", "[formats][conversion][stepmania][storyboard][etterna]") {
    using namespace wormedit;
    auto song = native_storyboard_song();
    osbx_lua_link_t link{.id = "children", .source = {.frozen_source = "return function(ctx) end", .source_reference = "children.lua"}};
    link.source.source_hash = native_project_content_checksum(link.source.frozen_source);
    song.storyboards.front().osbx->nodes = {{.id = "group", .kind = osbx_node_kind_t::group,
        .layer = osbx_layer_t::pass, .children_link = link}};
    const native_children_backend_t backend{osbx_layer_t::pass};
    const auto prepared = prepare_osbx_lua_scenes(song, backend);
    require_conversion_osbx(prepared.size() == 1 && prepared.front().complete);
    const osbx_lua_prepared_scope_t scope{prepared};
    auto request = etterna_storyboard_request();
    request.osbx_image_sizes.clear();
    for (const auto& image : prepared.front().scene->images)
        request.osbx_image_sizes.push_back({.image_id = image.id, .width = 64, .height = 48});
    const auto result = convert_document(song, request);
    check_fixed_etterna_choice(result, "Pass");
    for (const auto& report : result.stepmania_storyboard_report) UNSCOPED_INFO(report.reason);
    check_conversion_osbx(!result.storyboard_files.empty());
}

namespace {
auto animation_conversion_song() -> wormedit::song_document_t {
    using namespace wormedit;
    constexpr std::string_view source = "osu file format v14\n[General]\nMode:3\n[Difficulty]\nCircleSize:4\n"
        "[TimingPoints]\n0,500,4,0,0,100,1,0\n[HitObjects]\n64,192,1000,1,0,0:0:0:0:\n";
    const auto parsed = parse_osu_mania_bytes(std::as_bytes(std::span{source.data(), source.size()}));
    require_conversion_osbx(parsed.value.has_value());
    auto song = *parsed.value;
    osbx_node_t animation{.id = "hero", .kind = osbx_node_kind_t::animation, .image = "frames",
        .lifetime = osbx_lifetime_t{.begin_ms = 0, .end_ms = 2000}};
    animation.animation = storyboard_animation_t{.frame_count = 2, .frame_delay_ms = 100};
    osbx_node_t sample{.id = "cue", .kind = osbx_node_kind_t::sample};
    sample.sample.emplace(); sample.sample->time_ms = 100; sample.sample->path = "tone.wav";
    song.storyboards = {{.id = storyboard_id_t{1}, .osbx = osbx_document_t{
        .images = {{.id = "frames", .file = "hero.png"}}, .nodes = {animation, sample}}}};
    return song;
}
}

TEST_CASE("OSBX media package enumeration is owned file-free and bounded before frame allocation", "[conversion][osbx][animation][sample]") {
    using namespace wormedit;
    auto song = animation_conversion_song();
    auto media = storyboard_media_plan_t::from(song);
    require_conversion_osbx(media.has_value());
    require_conversion_osbx(media->images.size() == 2);
    check_conversion_osbx(media->images[0].file == "hero0.png");
    check_conversion_osbx(media->images[1].file == "hero1.png");
    check_conversion_osbx(media->samples == std::vector<std::filesystem::path>{"tone.wav"});
    check_conversion_osbx(storyboard_media_plan_t::from(song) == media);
    auto moved = std::move(*media);
    check_conversion_osbx(moved.animations.front().frames == std::vector<std::size_t>{0,1});
    song.storyboards.front().osbx->nodes.front().animation->frame_count = INT32_MAX;
    check_conversion_osbx(!storyboard_media_plan_t::from(song));
}

TEST_CASE("OSBX Animation and Sample conversion emits one authored object and concrete asset lists", "[conversion][osbx][animation][sample]") {
    using namespace wormedit;
    const auto song = animation_conversion_song();
    conversion_request_t request{.target = format_family_t::osu_mania,
        .osbx_image_sizes = {{.image_id = "frames", .width = 2, .height = 1, .file = "hero0.png"},
            {.image_id = "frames", .width = 2, .height = 1, .file = "hero1.png"},
            {.image_id = "frames", .width = 2, .height = 1, .file = "hero.png"}},
        .osbx_playback_span = osbx_osb_span_t{0,2000}};
    const auto result = convert_document(song, request);
    require_conversion_osbx(static_cast<bool>(result));
    const auto storyboard = std::ranges::find_if(result.outputs, [](const auto& output) {
        return std::filesystem::path{output.suggested_filename}.extension() == ".osb";
    });
    require_conversion_osbx(storyboard != result.outputs.end());
    const auto parsed = parse_osb(storyboard->bytes);
    require_conversion_osbx(parsed.value.has_value());
    check_conversion_osbx(parsed.value->sprites.size() == 1 && parsed.value->sprites.front().animation.has_value());
    check_conversion_osbx(parsed.value->samples.size() == 1);
    check_conversion_osbx(result.storyboard_images == std::vector<std::filesystem::path>{"hero0.png","hero1.png"});
    check_conversion_osbx(result.storyboard_samples == std::vector<std::filesystem::path>{"tone.wav"});
}

TEST_CASE("OSBX media plan refuses storage exhaustion and cancellation before path generation", "[conversion][osbx][animation][bounds]") {
    using namespace wormedit;
    const auto song = animation_conversion_song();
    check_conversion_osbx(!storyboard_media_plan_t::from(song, {.maximum_expanded_storage_bytes = 1}));
    std::stop_source cancelled;
    cancelled.request_stop();
    const auto refused = storyboard_media_plan_t::from(song, {}, cancelled.get_token());
    require_conversion_osbx(!refused.has_value());
    check_conversion_osbx(refused.error().find("cancelled") != std::string::npos);
}
