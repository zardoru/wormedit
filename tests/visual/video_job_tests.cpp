#include <wormedit/visual/video_job.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <stop_token>
#include <vector>

namespace {

using namespace wormedit;
using namespace wormedit::visual;

auto source(resource_id_t resource = resource_id_t{10}) -> resolved_visual_asset_t {
    return {.resource = resource,
            .asset = asset_id_t{20},
            .kind = asset_kind_t::video,
            .path = std::filesystem::path{WORMEDIT_FIXTURE_DIR} / "video" / "three-colors.mkv",
            .declared_width = 16,
            .declared_height = 16};
}

auto clock(resource_id_t resource, std::uint64_t numerator, std::uint32_t denominator = 1)
    -> video_preview_clock_t {
    return {.event = object_id_t{30},
            .resource = resource,
            .activation_sample_frame = 0,
            .elapsed_sample_frames = numerator,
            .media_time_numerator = numerator,
            .media_time_denominator = denominator};
}

auto frame_for(const resolved_visual_asset_t& item, std::size_t byte_count = 16U * 16U * 4U)
    -> std::shared_ptr<const rgba_video_frame_t> {
    return std::make_shared<const rgba_video_frame_t>(
        rgba_video_frame_t{.resource = item.resource,
                           .asset = item.asset,
                           .path = item.path,
                           .width = 16,
                           .height = 16,
                           .pixels = std::vector<std::uint8_t>(byte_count)});
}

auto has_bounded_decode_error() -> bool {
    const auto item = source();
    video_frame_cache_t cache;
    task_scheduler_t scheduler{1, 1};
    video_frame_job_t job{cache, scheduler};
    if (!job.request(song_id_t{9}, 8, item, clock(item.resource, 0), {.maximum_rgba_bytes = 1}))
        return false;
    const auto completion = job.wait();
    if (!completion || !*completion || !completion->value().payload) return false;
    const auto& outcome = *completion->value().payload;
    if (outcome) return false;
    const auto& error = outcome.error();
    const auto is_expected = error.context.identity.project == song_id_t{9} &&
                             error.context.identity.document_revision == 8 &&
                             error.context.source.resource == item.resource && error.context.clock.event == object_id_t{30} &&
                             error.cause.code == failure_code_t::capacity_exceeded && !error.payload &&
                             !error.cause.message.empty();
    return is_expected && cache.size() == 0;
}

} // namespace

TEST_CASE("video frame cache keys exact clocks and enforces byte and entry bounds",
          "[unit][visual][video-cache]") {
    const auto first_source = source(resource_id_t{10});
    const auto second_source = source(resource_id_t{11});
    const auto first_clock = clock(first_source.resource, 1, 2);
    const auto second_clock = clock(second_source.resource, 3, 2);
    video_frame_cache_t cache{1024, 1};

    REQUIRE(cache.insert(song_id_t{1}, 2, first_source, first_clock, frame_for(first_source)));
    CHECK(cache.size() == 1);
    CHECK(cache.rgba_bytes() == 1024);
    CHECK_FALSE(cache.find(song_id_t{1}, 3, first_source, first_clock));
    CHECK_FALSE(cache.find(song_id_t{1}, 2, first_source, clock(first_source.resource, 2, 3)));
    REQUIRE(cache.find(song_id_t{1}, 2, first_source, first_clock));

    REQUIRE(cache.insert(song_id_t{1}, 2, second_source, second_clock, frame_for(second_source)));
    CHECK_FALSE(cache.find(song_id_t{1}, 2, first_source, first_clock));
    REQUIRE(cache.find(song_id_t{1}, 2, second_source, second_clock));
    CHECK(cache.size() == 1);
    CHECK(cache.rgba_bytes() == 1024);
}

TEST_CASE("video frame job publishes only the newest revision-bound request",
          "[unit][visual][video-job]") {
    const auto item = source();
    video_frame_cache_t cache;
    task_scheduler_t scheduler{1, 2};
    video_frame_job_t job{cache, scheduler};

    const auto superseded = job.request(song_id_t{1}, 2, item, clock(item.resource, 0));
    REQUIRE(superseded);
    const auto current = job.request(song_id_t{1}, 3, item, clock(item.resource, 3, 2));
    REQUIRE(current);
    CHECK(*current != *superseded);
    CHECK(job.is_busy());

    const auto completion = job.wait();
    REQUIRE(completion);
    REQUIRE(*completion);
    CHECK(completion->value().generation == *current);
    CHECK(completion->value().identity.project == song_id_t{1});
    CHECK(completion->value().identity.document_revision == 3);
    REQUIRE(completion->value().payload);
    REQUIRE(*completion->value().payload);
    CHECK(completion->value().payload->value().context.clock.media_time_numerator == 3);
    CHECK(completion->value().payload->value().context.clock.media_time_denominator == 2);
    CHECK(completion->value().payload->value().payload->pixels.size() == 1024);
    CHECK_FALSE(job.is_busy());
    CHECK(cache.size() >= 1);
}

TEST_CASE("video jobs and decoder reject invalid bounded or cancelled requests",
          "[unit][visual][video-job][negative]") {
    const auto item = source();
    video_frame_cache_t cache;
    task_scheduler_t scheduler{1, 1};
    video_frame_job_t job{cache, scheduler};

    CHECK(job.request(song_id_t{}, 2, item, clock(item.resource, 0)).error().code ==
          video_job_start_status_t::invalid_request);
    CHECK(job.request(song_id_t{1}, 0, item, clock(item.resource, 0)).error().code ==
          video_job_start_status_t::invalid_request);
    video_frame_job_t disabled{cache, scheduler, 0};
    CHECK(disabled.request(song_id_t{1}, 2, item, clock(item.resource, 0)).error().code ==
          video_job_start_status_t::invalid_request);

    std::stop_source cancelled;
    cancelled.request_stop();
    const auto decoded = decode_video_frame_rgba(item, clock(item.resource, 0), {}, cancelled.get_token());
    REQUIRE_FALSE(decoded.has_value());
    CHECK(decoded.error().code == failure_code_t::cancelled);
}

TEST_CASE("completed video worker preserves bounded decode failure identity",
          "[unit][visual][video-job][negative]") {
    CHECK(has_bounded_decode_error());
}
