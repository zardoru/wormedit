#include <wormedit/media/video_encoder.h>
#include <wormedit/core/atomic_file_storage.h>
#include "../support/temporary_directory.h"
#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <iterator>
#include <limits>

namespace {
using namespace wormedit;
using namespace wormedit::media;
auto read_file(const std::filesystem::path& path) -> std::string {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}
void check_disabled(const video_encoder_options_t& options) {
    bool called{};
    const video_encoder_sources_t sources{.frame = [&](std::uint64_t, storyboard_time_t,
        storyboard_conditional_layer_t, std::stop_token) -> result_t<storyboard_raster_frame_t, wormedit::error_t<failure_code_t>> {
        called = true;
        return std::unexpected(wormedit::error_t{failure_code_t::task_failed, std::string{"Must not run"}});
    }};
    const auto encoded = video_encoder_t::encode(options, sources);
    REQUIRE_FALSE(encoded);
    CHECK(encoded.error().code == failure_code_t::unavailable);
    CHECK_FALSE(called);
    CHECK(read_file(options.destination) == "old");
}
}
TEST_CASE("Disabled video encoder refuses before callbacks and preserves existing output", "[video-encoder][disabled]") {
    auto directory = wormedit::test::temporary_directory_t::from();
    REQUIRE(directory);
    const auto destination = directory->path() / "clip.mp4";
    REQUIRE(wormedit::create_file_atomic(destination, "old", 3));
    CHECK_FALSE(wormedit::media::video_encoder_t::is_available());
    check_disabled({.destination = destination, .frame_count = 1});
    CHECK(std::distance(std::filesystem::directory_iterator(directory->path()), std::filesystem::directory_iterator{}) == 1);
}
TEST_CASE("Disabled encoder probe cannot create a staging file", "[video-encoder][disabled]") {
    auto directory = wormedit::test::temporary_directory_t::from();
    REQUIRE(directory);
    const auto result = wormedit::media::video_encoder_t::probe({.destination = directory->path() / "clip.webm"}, true);
    REQUIRE_FALSE(result);
    CHECK(result.error().code == wormedit::failure_code_t::unavailable);
    CHECK(std::filesystem::is_empty(directory->path()));
}
TEST_CASE("Video timestamps reuse the exact native preview clock at a nonzero selection", "[video-encoder][clock]") {
    wormedit::media::video_encoder_options_t options{.destination = "clip.mp4", .frame_count = 60, .begin_audio_frame = 441};
    CHECK(options.scene_time_at(1) == wormedit::storyboard_time_t{.milliseconds = 26, .remainder = 2, .denominator = 3});
    options.clock.numerator = 30;
    CHECK(options.input_audio_frames() == 88'200);
    CHECK(options.scene_time_at(60) == wormedit::storyboard_time_t{.milliseconds = 2010});
    options.frame_count = std::numeric_limits<std::uint64_t>::max();
    CHECK_FALSE(options.is_valid());
    options.clock.numerator = 0;
    CHECK_FALSE(options.scene_time_at(0));
}
