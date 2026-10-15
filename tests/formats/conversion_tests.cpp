#include <wormedit/formats/bms.h>
#include <wormedit/formats/bmson.h>
#include <wormedit/formats/conversion.h>
#include <wormedit/formats/native_project.h>
#include <wormedit/formats/osu_mania.h>
#include <wormedit/formats/stepmania.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
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
    REQUIRE_FALSE(!parsed.ok());
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
    for (const auto& diagnostic : result.diagnostics) INFO(diagnostic.message);
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
    outcome.value.asset_copies.push_back({asset_id_t{7}, "assets/a.wav"});
    outcome.value.silent_files.emplace_back("silent.wav");
    outcome.diagnostics.push_back({.severity = diagnostic_severity_t::warning, .loss = true});
    check_conversion(outcome.ok());
    const auto legacy = conversion_result_t::from(std::move(outcome));
    check_conversion(static_cast<bool>(legacy));
    check_conversion(legacy.outputs.front().bytes == "bytes" && legacy.outputs.front().reimport_validated);
    check_conversion(legacy.asset_copies.front() == conversion_asset_copy_t{asset_id_t{7}, "assets/a.wav"});
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
