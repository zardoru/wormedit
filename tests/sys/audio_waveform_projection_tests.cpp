#include "sys/waveform_view.h"
#include "sys/frame_timing.h"
#include <catch2/catch_test_macros.hpp>
#include <limits>

namespace {
auto analysis_fixture() -> wormedit::audio::waveform_analysis_result_t {
    wormedit::audio::waveform_t waveform;
    waveform.key = {.source = {"synthetic", "v1"}, .frames_per_bin = 100};
    waveform.total_frames = 10'000;
    waveform.sample_rate = wormedit::audio::output_sample_rate;
    waveform.bins.resize(100);
    for (std::size_t index = 0; index < waveform.bins.size(); ++index) {
        auto& bin = waveform.bins[index];
        const auto peak = index < 50 ? 0.25F : 0.75F;
        bin.minimum = {-peak, -peak};
        bin.maximum = {peak, peak};
        bin.absolute_peak = {peak, peak};
        bin.rms = {peak, peak};
        bin.frame_count = 100;
    }
    return {.waveform = std::move(waveform)};
}
auto frame_fixture() -> wormedit::sys::frontend_workspace_view_t {
    wormedit::sys::frontend_workspace_view_t frame;
    frame.view.audio_slices.visible = true;
    frame.view.audio_slices.has_selected_asset = true;
    frame.view.audio_slices.selected_asset_id = wormedit::asset_id_t{7};
    return frame;
}
}

TEST_CASE("Sounds waveform projection uses the visible source-frame viewport",
          "[sys][audio-waveform-projection]") {
    auto frame = frame_fixture();
    const auto analysis = analysis_fixture();
    const wormedit::ui::audio_waveform_viewport_request_t request{wormedit::asset_id_t{7}, 10'000, 6'000, 10.0, 100, true};
    wormedit::sys::populate_audio_waveform_views(frame, &analysis, false, 1280, &request);
    REQUIRE(frame.view.audio_slices.waveform_available);
    CHECK(frame.view.audio_slices.waveform_begin_frame == 6'000);
    CHECK(frame.view.audio_slices.waveform_end_frame == 7'000);
    REQUIRE(frame.view.audio_slices.waveform_columns.size() == 100);
    CHECK(frame.view.audio_slices.waveform_columns.front().maximum[0] == 0.75F);
}

TEST_CASE("Sounds waveform projection rejects stale malformed requests and keeps Metadata fitted",
          "[sys][audio-waveform-projection]") {
    auto frame = frame_fixture();
    const auto analysis = analysis_fixture();
    wormedit::ui::audio_waveform_viewport_request_t request{wormedit::asset_id_t{7}, 10'000, 6'000, 10.0, 100, true};
    SECTION("asset changed") { request.asset_id = wormedit::asset_id_t{8}; }
    SECTION("source changed") { request.total_frames = 9'000; }
    SECTION("invalid zoom") { request.frames_per_pixel = std::numeric_limits<double>::quiet_NaN(); }
    SECTION("invalid width") { request.pixel_width = 0; }
    SECTION("inactive") { request.valid = false; }
    SECTION("Metadata") { frame.view.audio_slices.visible = false; frame.view.metadata.visible = true; }
    wormedit::sys::populate_audio_waveform_views(frame, &analysis, false, 1280, &request);
    REQUIRE(frame.view.audio_slices.waveform_available);
    CHECK(frame.view.audio_slices.waveform_begin_frame == 0);
    CHECK(frame.view.audio_slices.waveform_end_frame == 10'000);
}

TEST_CASE("Repeated Sounds waveform refill retains one projection without accumulation",
          "[sys][audio-waveform-projection][steady-state]") {
    auto frame = frame_fixture();
    const auto analysis = analysis_fixture();
    const wormedit::ui::audio_waveform_viewport_request_t request{
        wormedit::asset_id_t{7}, 10'000, 6'000, 10.0, 100, true};
    wormedit::sys::populate_audio_waveform_views(frame, &analysis, false, 1280, &request);
    const auto before = wormedit::sys::frame_heap_allocation_snapshot();
    wormedit::sys::populate_audio_waveform_views(frame, &analysis, false, 1280, &request);
    const auto after = wormedit::sys::frame_heap_allocation_snapshot();
    CHECK(after.count == before.count);
    REQUIRE(frame.view.audio_slices.waveform_columns.size() == 100);
    CHECK(frame.view.audio_slices.waveform_columns.front().maximum[0] == 0.75F);
    CHECK(frame.view.audio_slices.waveform_begin_frame == 6'000);
    CHECK(frame.view.audio_slices.waveform_end_frame == 7'000);
    CHECK(frame.view.metadata.audio_waveform_columns.size() == 100);
}
