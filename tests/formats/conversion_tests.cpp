#include <wormedit/formats/conversion.h>
#include <wormedit/formats/stepmania.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
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
