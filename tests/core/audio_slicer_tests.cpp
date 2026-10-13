#include <wormedit/core/audio_slicer.h>

#include <catch2/catch_test_macros.hpp>

using namespace wormedit;

namespace {

auto base_request() -> audio_slicer_request_t {
    return {.beats_per_minute = {120, 1},
            .division = 4,
            .offset_seconds = {0, 1},
            .sample_rate = 48'000,
            .source_frames = 10 * 48'000};
}

} // namespace

TEST_CASE("slicer cuts 120 BPM sixteenths into exact frame ranges", "[unit][core][slicer]") {
    const auto result = compute_audio_slices(base_request());
    REQUIRE(result);
    // One sixteenth at 120 BPM is 0.125 s = 6000 frames; 10 s gives 80 slices.
    REQUIRE(result.ranges.size() == 80);
    CHECK(result.ranges.front() == audio_slice_range_t{0, 6000});
    CHECK(result.ranges[1] == audio_slice_range_t{6000, 12000});
    CHECK(result.ranges.back() == audio_slice_range_t{474'000, 480'000});
}

TEST_CASE("slicer ranges are contiguous and cover the source", "[unit][core][slicer]") {
    auto request = base_request();
    request.beats_per_minute = {174'123, 1'000};
    request.division = 3;
    request.offset_seconds = {-17, 1000};
    const auto result = compute_audio_slices(request);
    REQUIRE(result);
    std::uint64_t cursor = 0;
    for (const auto& range : result.ranges) {
        CHECK(range.begin_frame == cursor);
        CHECK(range.end_frame > range.begin_frame);
        cursor = range.end_frame;
    }
    CHECK(cursor == request.source_frames);
}

TEST_CASE("positive offset makes a lead-in slice unless disabled", "[unit][core][slicer]") {
    auto request = base_request();
    request.offset_seconds = {1, 4}; // 12000 frames
    auto result = compute_audio_slices(request);
    REQUIRE(result);
    CHECK(result.ranges[0] == audio_slice_range_t{0, 12'000});
    CHECK(result.ranges[1] == audio_slice_range_t{12'000, 18'000});
    request.include_lead_in = false;
    result = compute_audio_slices(request);
    REQUIRE(result);
    CHECK(result.ranges[0] == audio_slice_range_t{12'000, 18'000});
}

TEST_CASE("negative offset moves the grid earlier", "[unit][core][slicer]") {
    auto request = base_request();
    request.offset_seconds = {-1, 16}; // -3000 frames: boundaries at 3000, 9000, ...
    const auto result = compute_audio_slices(request);
    REQUIRE(result);
    CHECK(result.ranges[0] == audio_slice_range_t{0, 3000});
    CHECK(result.ranges[1] == audio_slice_range_t{3000, 9000});
}

TEST_CASE("slicer boundaries do not drift on an 80 minute source", "[unit][core][slicer]") {
    auto request = base_request();
    request.beats_per_minute = {128'000'001, 1'000'000}; // irrational-looking tempo
    request.division = 1;
    request.source_frames = 80ULL * 60ULL * 48'000ULL;
    request.maximum_slices = 20'000;
    const auto result = compute_audio_slices(request);
    REQUIRE(result);
    // 80 min at 128.000001 BPM: 10240.0001 beats, so 10241 slices with a short tail.
    REQUIRE(result.ranges.size() == 10'241);
    // Boundary 10240 = 10240 * 60/128.000001 s * 48000 frames, rounded to nearest.
    const auto expected = 10240ULL * 60ULL * 1'000'000ULL * 48'000ULL / 128'000'001ULL;
    CHECK(result.ranges[10'240].begin_frame >= expected);
    CHECK(result.ranges[10'240].begin_frame <= expected + 1);
}

TEST_CASE("tiny tail and ties follow the rounding mode", "[unit][core][slicer]") {
    auto request = base_request();
    request.sample_rate = 44'100;
    request.source_frames = 44'100;
    request.beats_per_minute = {90, 1};
    request.division = 1; // 2/3 s = 29400 frames
    auto result = compute_audio_slices(request);
    REQUIRE(result);
    REQUIRE(result.ranges.size() == 2);
    CHECK(result.ranges[0] == audio_slice_range_t{0, 29'400});
    CHECK(result.ranges[1].end_frame == 44'100);

    request.beats_per_minute = {1, 1};
    request.division = 7; // 60/7 s * 44100 = 378000 frames: exact
    request.source_frames = 1'000'000;
    result = compute_audio_slices(request);
    REQUIRE(result);
    CHECK(result.ranges[0].end_frame == 378'000);
    request.sample_rate = 44'101;
    request.rounding = sample_rounding_t::floor;
    const auto floored = compute_audio_slices(request);
    request.rounding = sample_rounding_t::ceil;
    const auto ceiled = compute_audio_slices(request);
    REQUIRE(floored);
    REQUIRE(ceiled);
    CHECK(ceiled.ranges[0].end_frame == floored.ranges[0].end_frame + 1);
}

TEST_CASE("invalid slicer inputs return diagnostics and no ranges", "[unit][core][slicer]") {
    const auto rejects = [](audio_slicer_request_t request, audio_slicer_error_code_t code) {
        const auto result = compute_audio_slices(request);
        CHECK_FALSE(result);
        CHECK(result.ranges.empty());
        REQUIRE_FALSE(result.diagnostics.empty());
        CHECK(result.diagnostics.front().code == code);
    };
    auto request = base_request();
    request.beats_per_minute = {0, 1};
    rejects(request, audio_slicer_error_code_t::invalid_tempo);
    request = base_request();
    request.division = 0;
    rejects(request, audio_slicer_error_code_t::invalid_division);
    request = base_request();
    request.offset_seconds = {1, 0};
    rejects(request, audio_slicer_error_code_t::invalid_offset);
    request = base_request();
    request.sample_rate = 0;
    rejects(request, audio_slicer_error_code_t::invalid_sample_rate);
    request = base_request();
    request.source_frames = 0;
    rejects(request, audio_slicer_error_code_t::invalid_source_length);
    request = base_request();
    request.beats_per_minute = {1ULL << 40, 1};
    request.division = 1024;
    rejects(request, audio_slicer_error_code_t::slice_shorter_than_one_frame);
    request = base_request();
    request.maximum_slices = 10;
    rejects(request, audio_slicer_error_code_t::capacity_exceeded);
}

namespace {

auto diagnosed_slice_plans_preserve_error_only_reports() -> bool {
    const auto ready = audio_slice_plan_t::from(base_request());
    if (!ready.ok() || ready.value.ranges.size() != 80)
        return false;
    auto request = base_request();
    request.sample_rate = 0;
    const auto rejected = audio_slice_plan_t::from(request);
    return !rejected.ok() && rejected.value.ranges.empty() &&
           rejected.diagnostics.size() == 1 && rejected.diagnostics.front().is_error() &&
           rejected.diagnostics.front().code == audio_slicer_error_code_t::invalid_sample_rate &&
           rejected.diagnostics.front().message == "sample rate is out of range" &&
           ready.value.ranges.front() == audio_slice_range_t{0, 6000} &&
           ready.value.ranges.back() == audio_slice_range_t{474'000, 480'000};
}

} // namespace

TEST_CASE("diagnosed slicer plans preserve exact ranges and error-only diagnostic metadata",
          "[unit][core][slicer][result]") {
    CHECK(diagnosed_slice_plans_preserve_error_only_reports());
}
