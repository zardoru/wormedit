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

} // namespace

TEST_CASE("video decoder selects frames from the exact preview media clock",
          "[unit][visual][video]") {
    const auto red = decode_video_frame_rgba(video_source(), video_clock(1, 2));
    REQUIRE(red);
    CHECK(first_pixel(*red.frame, 0) > 240);
    CHECK(first_pixel(*red.frame, 1) < 16);
    CHECK(first_pixel(*red.frame, 2) < 16);

    const auto green = decode_video_frame_rgba(video_source(), video_clock(3, 2));
    REQUIRE(green);
    CHECK(first_pixel(*green.frame, 0) < 16);
    CHECK(first_pixel(*green.frame, 1) > 240);
    CHECK(first_pixel(*green.frame, 2) < 16);

    const auto blue = decode_video_frame_rgba(video_source(), video_clock(5, 2));
    REQUIRE(blue);
    CHECK(first_pixel(*blue.frame, 0) < 16);
    CHECK(first_pixel(*blue.frame, 1) < 16);
    CHECK(first_pixel(*blue.frame, 2) > 240);
}

TEST_CASE("video decoder session retains forward state and seeks backward exactly",
          "[unit][visual][video][session]") {
    auto opened = open_video_decoder_session(video_source());
    REQUIRE(opened);

    const auto red = opened.session->decode(video_clock(1, 2));
    REQUIRE(red);
    REQUIRE(red.frame->next_presentation_timestamp);
    CHECK(video_frame_covers_clock(*red.frame, video_clock(3, 4)));
    CHECK_FALSE(video_frame_covers_clock(*red.frame, video_clock(1)));

    const auto green = opened.session->decode(video_clock(3, 2));
    REQUIRE(green);
    CHECK(first_pixel(*green.frame, 1) > 240);
    CHECK(video_frame_covers_clock(*green.frame, video_clock(7, 4)));

    const auto rewound = opened.session->decode(video_clock(1, 2));
    REQUIRE(rewound);
    CHECK(first_pixel(*rewound.frame, 0) > 240);

    const auto blue = opened.session->decode(video_clock(5, 2));
    REQUIRE(blue);
    CHECK_FALSE(blue.frame->next_presentation_timestamp.has_value());
    CHECK(video_frame_covers_clock(*blue.frame, video_clock(100)));
}

TEST_CASE("video decoder reports invalid sources clocks and files",
          "[unit][visual][video][negative]") {
    CHECK(decode_video_frame_rgba(video_source(asset_kind_t::image), video_clock(0)).status ==
          video_frame_status_t::unsupported_asset_kind);

    auto wrong_resource = video_clock(0);
    wrong_resource.resource = resource_id_t{99};
    CHECK(decode_video_frame_rgba(video_source(), wrong_resource).status ==
          video_frame_status_t::invalid_request);

    auto missing = video_source();
    missing.path += ".missing";
    CHECK(decode_video_frame_rgba(missing, video_clock(0)).status ==
          video_frame_status_t::open_failed);

    auto huge = video_clock(0);
    huge.media_time_numerator = std::numeric_limits<std::uint64_t>::max();
    CHECK(decode_video_frame_rgba(video_source(), huge).status ==
          video_frame_status_t::timestamp_out_of_range);
}

TEST_CASE("video decoder enforces geometry packet and declaration bounds",
          "[unit][visual][video][bounds]") {
    auto dimensions = video_frame_decode_limits_t{};
    dimensions.maximum_width = 8;
    CHECK(decode_video_frame_rgba(video_source(), video_clock(0), dimensions).status ==
          video_frame_status_t::dimension_limit_exceeded);

    auto pixels = video_frame_decode_limits_t{};
    pixels.maximum_pixels = 128;
    CHECK(decode_video_frame_rgba(video_source(), video_clock(0), pixels).status ==
          video_frame_status_t::pixel_limit_exceeded);

    auto bytes = video_frame_decode_limits_t{};
    bytes.maximum_rgba_bytes = 1023;
    CHECK(decode_video_frame_rgba(video_source(), video_clock(0), bytes).status ==
          video_frame_status_t::byte_limit_exceeded);

    auto declared = video_source();
    declared.declared_height = 15;
    CHECK(decode_video_frame_rgba(declared, video_clock(0)).status ==
          video_frame_status_t::declared_geometry_mismatch);

    auto packets = video_frame_decode_limits_t{};
    packets.maximum_packets = 1;
    CHECK(decode_video_frame_rgba(video_source(), video_clock(5, 2), packets).status ==
          video_frame_status_t::decode_failed);
}
