#include "sys/player_preview_presenter.h"

#include <wormedit/core/command.h>
#include <wormedit/core/task_graph.h>
#include <wormedit/visual/image_job.h>
#include <wormedit/visual/video_job.h>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <thread>

namespace {

using namespace wormedit;

auto document() -> song_document_t {
    chart_document_t chart{
        .id = chart_id_t{7},
        .divisions_per_standard_measure = 960,
        .measures = {{.id = measure_id_t{1}, .definition = {{1, 1}, meter_t{4, 4}}}},
        .lane_layouts = {{.id = object_id_t{2},
                          .position = {measure_id_t{1}, 0},
                          .layout = {.profile_id = "test-single",
                                     .display_name = "Test Single",
                                     .lanes = {{.id = lane_id_t{1},
                                                .name = "Lane",
                                                .role = lane_role_t::playable,
                                                .display_order = 0}}}}},
        .bpms = {{.id = object_id_t{3},
                  .position = {measure_id_t{1}, 0},
                  .beats_per_minute = {120, 1}}},
        .bga_events = {{.id = object_id_t{4},
                        .position = {measure_id_t{1}, 0},
                        .resource = resource_id_t{10},
                        .kind = visual_event_kind_t::video}},
    };
    return {.id = song_id_t{1},
            .revision = 9,
            .visual_assets = {{.id = asset_id_t{20},
                               .kind = asset_kind_t::video,
                               .source_path = "three-colors.mkv",
                               .width = 16,
                               .height = 16}},
            .resources = {{.id = resource_id_t{10},
                           .name = "video",
                           .asset = asset_id_t{20},
                           .roles = {resource_role_t::bga}}},
            .charts = {std::move(chart)}};
}

auto wait_for_frame(sys::player_preview_presenter_t& presenter, WormUiPlayerPreviewView& view,
                    const document_history_t& history, visual::image_job_t& image_job,
                    visual::video_frame_job_t& video_job, std::uint64_t sample_frame,
                    std::uint64_t different_content_id = 0) -> bool {
    const auto directory = std::filesystem::path{WORMEDIT_FIXTURE_DIR} / "video";
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (std::chrono::steady_clock::now() < deadline) {
        presenter.populate(view, history, chart_id_t{7}, true, player_preview_skin_t::mania,
                           sample_frame, 48'000, directory, image_job, video_job);
        if (view.visual_layer_count == 1 && view.visual_layers[0].image_ready &&
            view.visual_layers[0].image_content_id != different_content_id) {
            return true;
        }
        std::this_thread::yield();
    }
    return false;
}

} // namespace

TEST_CASE("player preview asynchronously publishes and replaces exact video frames",
          "[integration][visual][video][player-preview]") {
    const auto history = make_document_history(document());
    visual::image_cache_t image_cache;
    task_scheduler_t image_scheduler{1, 2};
    visual::image_job_t image_job{image_cache, image_scheduler};
    visual::video_frame_cache_t video_cache;
    task_scheduler_t video_scheduler{1, 2};
    visual::video_frame_job_t video_job{video_cache, video_scheduler};
    sys::player_preview_presenter_t presenter;
    WormUiPlayerPreviewView view{};

    REQUIRE(wait_for_frame(presenter, view, history, image_job, video_job, 24'000));
    REQUIRE(view.visual_layers[0].video_frame);
    REQUIRE(view.visual_layers[0].rgba_byte_count == 16U * 16U * 4U);
    CHECK(view.visual_layers[0].rgba_pixels[0] > 240);
    CHECK(view.visual_layers[0].rgba_pixels[1] < 16);
    CHECK(view.visual_layers[0].rgba_pixels[2] < 16);
    const auto red_content_id = view.visual_layers[0].image_content_id;

    REQUIRE(wait_for_frame(presenter, view, history, image_job, video_job, 72'000, red_content_id));
    CHECK(view.visual_layers[0].rgba_pixels[0] < 16);
    CHECK(view.visual_layers[0].rgba_pixels[1] > 240);
    CHECK(view.visual_layers[0].rgba_pixels[2] < 16);
}
