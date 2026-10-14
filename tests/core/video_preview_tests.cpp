#include <wormedit/core/video_preview.h>

#include <catch2/catch_test_macros.hpp>

#include <limits>

using namespace wormedit;

namespace {

auto video(std::uint64_t sample_frame = 48'000) -> preview_visual_event_t {
    return {.sample_frame = sample_frame,
            .object = object_id_t{7},
            .resource = resource_id_t{11},
            .kind = visual_event_kind_t::video};
}

} // namespace

TEST_CASE("video preview clock evaluates exact media time from its activation",
          "[unit][core][video-preview]") {
    const auto result = video_preview_clock_t::from(video(), 84'000, 48'000);
    REQUIRE(result);
    CHECK(result->event == object_id_t{7});
    CHECK(result->resource == resource_id_t{11});
    CHECK(result->activation_sample_frame == 48'000);
    CHECK(result->elapsed_sample_frames == 36'000);
    CHECK(result->media_time_numerator == 3);
    CHECK(result->media_time_denominator == 4);

    const auto activation = video_preview_clock_t::from(video(), 48'000, 48'000);
    REQUIRE(activation);
    CHECK(activation->media_time_numerator == 0);
    CHECK(activation->media_time_denominator == 1);
}

TEST_CASE("video preview clock is seek-stable and restarts on replacement",
          "[unit][core][video-preview]") {
    const auto first = video_preview_clock_t::from(video(10), 110, 100);
    const auto repeated = video_preview_clock_t::from(video(10), 110, 100);
    REQUIRE(first);
    REQUIRE(repeated);
    CHECK(*first == *repeated);
    CHECK(first->media_time_numerator == 1);

    const auto replacement = video_preview_clock_t::from(video(90), 110, 100);
    REQUIRE(replacement);
    CHECK(replacement->elapsed_sample_frames == 20);
    CHECK(replacement->media_time_numerator == 1);
    CHECK(replacement->media_time_denominator == 5);
}

TEST_CASE("video preview clock reports inactive and malformed inputs",
          "[unit][core][video-preview]") {
    CHECK(video_preview_clock_t::from(std::nullopt, 0, 48'000).error() ==
          video_preview_clock_status_t::inactive);
    CHECK(video_preview_clock_t::from(video(), 48'000, 0).error() ==
          video_preview_clock_status_t::invalid_sample_rate);
    CHECK(video_preview_clock_t::from(video(101), 100, 48'000).error() ==
          video_preview_clock_status_t::event_in_future);

    auto wrong_kind = video();
    wrong_kind.kind = visual_event_kind_t::background;
    CHECK(video_preview_clock_t::from(wrong_kind, 48'000, 48'000).error() ==
          video_preview_clock_status_t::invalid_event);

    auto missing_resource = video();
    missing_resource.resource = {};
    CHECK(video_preview_clock_t::from(missing_resource, 48'000, 48'000).error() ==
          video_preview_clock_status_t::invalid_event);
}

TEST_CASE("video preview clock remains exact across the unsigned transport range",
          "[unit][core][video-preview]") {
    const auto result = video_preview_clock_t::from(
        video(0), std::numeric_limits<std::uint64_t>::max(), 1);
    REQUIRE(result);
    CHECK(result->elapsed_sample_frames == std::numeric_limits<std::uint64_t>::max());
    CHECK(result->media_time_numerator == std::numeric_limits<std::uint64_t>::max());
    CHECK(result->media_time_denominator == 1);
}
