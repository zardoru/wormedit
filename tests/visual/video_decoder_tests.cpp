#include <wormedit/visual/video_decoder.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <limits>

namespace {

using namespace wormedit;
using namespace wormedit::visual;

auto fixture_path() -> std::filesystem::path {
    return std::filesystem::path{WORMEDIT_FIXTURE_DIR} / "video" / "three-colors.mkv";
}

auto video_source(asset_kind_t kind = asset_kind_t::video) -> resolved_visual_asset_t {
    return {.resource = resource_id_t{10},
            .asset = asset_id_t{20},
            .kind = kind,
            .path = fixture_path(),
            .declared_width = 16,
            .declared_height = 16};
}

auto video_clock(std::uint64_t numerator, std::uint32_t denominator = 1) -> video_preview_clock_t {
    return {.event = object_id_t{30},
            .resource = resource_id_t{10},
            .activation_sample_frame = 0,
            .elapsed_sample_frames = numerator,
            .media_time_numerator = numerator,
            .media_time_denominator = denominator};
}

auto first_pixel(const rgba_video_frame_t& frame, std::size_t channel) -> std::uint8_t {
    REQUIRE(frame.pixels.size() == 16U * 16U * 4U);
    return frame.pixels[channel];
}

auto check_decoded_pixel(const video_frame_decode_result_t& decoded, std::size_t channel) -> void {
    REQUIRE(decoded);
    CHECK(first_pixel(*decoded, channel) > 240);
}

auto check_primary_pixel(const video_frame_decode_result_t& decoded, std::size_t primary) -> void {
    check_decoded_pixel(decoded, primary);
    for (std::size_t channel = 0; channel < 3; ++channel) {
        if (channel != primary)
            CHECK(first_pixel(*decoded, channel) < 16);
    }
}

auto check_video_error(const video_frame_decode_result_t& decoded, failure_code_t code) -> void {
    REQUIRE_FALSE(decoded.has_value());
    CHECK(decoded.error().code == code);
}

auto check_first_frame_interval(const rgba_video_frame_t& frame) -> void {
    REQUIRE(frame.next_presentation_timestamp);
    CHECK(is_video_frame_covering_clock(frame, video_clock(3, 4)));
    CHECK_FALSE(is_video_frame_covering_clock(frame, video_clock(1)));
}

} // namespace

TEST_CASE("video decoder selects frames from the exact preview media clock",
          "[unit][visual][video]") {
    const auto red = decode_video_frame_rgba(video_source(), video_clock(1, 2));
    check_primary_pixel(red, 0);

    const auto green = decode_video_frame_rgba(video_source(), video_clock(3, 2));
    check_primary_pixel(green, 1);

    const auto blue = decode_video_frame_rgba(video_source(), video_clock(5, 2));
    check_primary_pixel(blue, 2);
}

TEST_CASE("video decoder session retains forward state and seeks backward exactly",
          "[unit][visual][video][session]") {
    auto opened = open_video_decoder_session(video_source());
    REQUIRE(opened);

    const auto red = (*opened)->decode(video_clock(1, 2));
    check_decoded_pixel(red, 0);
    check_first_frame_interval(*red);

    const auto green = (*opened)->decode(video_clock(3, 2));
    check_decoded_pixel(green, 1);
    CHECK(is_video_frame_covering_clock(*green, video_clock(7, 4)));

    const auto rewound = (*opened)->decode(video_clock(1, 2));
    check_decoded_pixel(rewound, 0);

    const auto blue = (*opened)->decode(video_clock(5, 2));
    check_decoded_pixel(blue, 2);
    CHECK_FALSE(blue->next_presentation_timestamp.has_value());
    CHECK(is_video_frame_covering_clock(*blue, video_clock(100)));
}

TEST_CASE("video decoder reports invalid sources clocks and files",
          "[unit][visual][video][negative]") {
    check_video_error(decode_video_frame_rgba(video_source(asset_kind_t::image), video_clock(0)), failure_code_t::unsupported);

    auto wrong_resource = video_clock(0);
    wrong_resource.resource = resource_id_t{99};
    check_video_error(decode_video_frame_rgba(video_source(), wrong_resource), failure_code_t::invalid_request);

    auto missing = video_source();
    missing.path += ".missing";
    check_video_error(decode_video_frame_rgba(missing, video_clock(0)), failure_code_t::io_failed);

    auto huge = video_clock(0);
    huge.media_time_numerator = std::numeric_limits<std::uint64_t>::max();
    check_video_error(decode_video_frame_rgba(video_source(), huge), failure_code_t::invalid_request);
}

TEST_CASE("video decoder enforces geometry packet and declaration bounds",
          "[unit][visual][video][bounds]") {
    auto dimensions = video_frame_decode_limits_t{};
    dimensions.maximum_width = 8;
    check_video_error(decode_video_frame_rgba(video_source(), video_clock(0), dimensions), failure_code_t::capacity_exceeded);

    auto pixels = video_frame_decode_limits_t{};
    pixels.maximum_pixels = 128;
    check_video_error(decode_video_frame_rgba(video_source(), video_clock(0), pixels), failure_code_t::capacity_exceeded);

    auto bytes = video_frame_decode_limits_t{};
    bytes.maximum_rgba_bytes = 1023;
    check_video_error(decode_video_frame_rgba(video_source(), video_clock(0), bytes), failure_code_t::capacity_exceeded);

    auto declared = video_source();
    declared.declared_height = 15;
    check_video_error(decode_video_frame_rgba(declared, video_clock(0)), failure_code_t::invalid_geometry);

    auto packets = video_frame_decode_limits_t{};
    packets.maximum_packets = 1;
    check_video_error(decode_video_frame_rgba(video_source(), video_clock(5, 2), packets), failure_code_t::decode_failed);
}
