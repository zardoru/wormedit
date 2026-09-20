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
    const auto result = evaluate_video_preview_clock(video(), 84'000, 48'000);
    REQUIRE(result);
    CHECK(result.clock->event == object_id_t{7});
    CHECK(result.clock->resource == resource_id_t{11});
    CHECK(result.clock->activation_sample_frame == 48'000);
    CHECK(result.clock->elapsed_sample_frames == 36'000);
    CHECK(result.clock->media_time_numerator == 3);
    CHECK(result.clock->media_time_denominator == 4);

    const auto activation = evaluate_video_preview_clock(video(), 48'000, 48'000);
    REQUIRE(activation);
    CHECK(activation.clock->media_time_numerator == 0);
    CHECK(activation.clock->media_time_denominator == 1);
}

TEST_CASE("video preview clock is seek-stable and restarts on replacement",
          "[unit][core][video-preview]") {
    const auto first = evaluate_video_preview_clock(video(10), 110, 100);
    const auto repeated = evaluate_video_preview_clock(video(10), 110, 100);
    REQUIRE(first);
    REQUIRE(repeated);
    CHECK(first.clock == repeated.clock);
    CHECK(first.clock->media_time_numerator == 1);

    const auto replacement = evaluate_video_preview_clock(video(90), 110, 100);
    REQUIRE(replacement);
    CHECK(replacement.clock->elapsed_sample_frames == 20);
    CHECK(replacement.clock->media_time_numerator == 1);
    CHECK(replacement.clock->media_time_denominator == 5);
}

TEST_CASE("video preview clock reports inactive and malformed inputs",
          "[unit][core][video-preview]") {
    CHECK(evaluate_video_preview_clock(std::nullopt, 0, 48'000).status ==
          video_preview_clock_status_t::inactive);
    CHECK(evaluate_video_preview_clock(video(), 48'000, 0).status ==
          video_preview_clock_status_t::invalid_sample_rate);
    CHECK(evaluate_video_preview_clock(video(101), 100, 48'000).status ==
          video_preview_clock_status_t::event_in_future);

    auto wrong_kind = video();
    wrong_kind.kind = visual_event_kind_t::background;
    CHECK(evaluate_video_preview_clock(wrong_kind, 48'000, 48'000).status ==
          video_preview_clock_status_t::invalid_event);

    auto missing_resource = video();
    missing_resource.resource = {};
    CHECK(evaluate_video_preview_clock(missing_resource, 48'000, 48'000).status ==
          video_preview_clock_status_t::invalid_event);
}

TEST_CASE("video preview clock remains exact across the unsigned transport range",
          "[unit][core][video-preview]") {
    const auto result = evaluate_video_preview_clock(
        video(0), std::numeric_limits<std::uint64_t>::max(), 1);
    REQUIRE(result);
    CHECK(result.clock->elapsed_sample_frames == std::numeric_limits<std::uint64_t>::max());
    CHECK(result.clock->media_time_numerator == std::numeric_limits<std::uint64_t>::max());
    CHECK(result.clock->media_time_denominator == 1);
}
