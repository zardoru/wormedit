#include "sys/player_preview_presenter.h"

#include <wormedit/core/command.h>
#include <wormedit/core/task_graph.h>
#include <wormedit/visual/image_job.h>
#include <wormedit/visual/video_job.h>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <algorithm>
#include <array>
#include <fstream>
#include <cstdint>
#include <filesystem>
#include <latch>
#include <string_view>
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

auto wait_for_diagnostic(sys::player_preview_presenter_t& presenter, WormUiPlayerPreviewView& view,
                         const document_history_t& history, visual::image_job_t& image_job,
                         visual::video_frame_job_t& video_job,
                         WormUiPlayerPreviewVisualDiagnostic diagnostic) -> bool {
    const auto directory = std::filesystem::path{WORMEDIT_FIXTURE_DIR} / "video";
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (std::chrono::steady_clock::now() < deadline) {
        presenter.populate(view, history, chart_id_t{7}, true, player_preview_skin_t::mania, 24'000,
                           48'000, directory, image_job, video_job);
        if (view.visual_layer_count == 1 && view.visual_layers[0].diagnostic == diagnostic) {
            return true;
        }
        std::this_thread::yield();
    }
    return false;
}

struct latch_release_t {
    std::latch& latch;
    ~latch_release_t() {
        latch.count_down();
    }
};

struct temporary_bga_image_t {
    std::filesystem::path path{std::filesystem::temp_directory_path() /
        ("wormedit-bga-layer2-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".bmp")};
    temporary_bga_image_t() {
        constexpr std::array<std::uint8_t, 62> bytes{
            0x42, 0x4d, 0x3e, 0, 0, 0, 0, 0, 0, 0, 0x36, 0, 0, 0, 0x28, 0,
            0, 0, 2, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0x18, 0, 0, 0,
            0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0, 0xff, 0, 0, 0};
        std::ofstream stream{path, std::ios::binary};
        REQUIRE(stream.write(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    }
    ~temporary_bga_image_t() {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }
};

auto wait_for_bga_layers(sys::player_preview_presenter_t& presenter, WormUiPlayerPreviewView& view,
                         const document_history_t& history, visual::image_job_t& image_job,
                         visual::video_frame_job_t& video_job, const std::filesystem::path& directory) -> bool {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (std::chrono::steady_clock::now() < deadline) {
        presenter.populate(view, history, chart_id_t{7}, true, player_preview_skin_t::mania,
                           0, 48'000, directory, image_job, video_job);
        if (view.visual_layer_count == 2 && std::all_of(view.visual_layers, view.visual_layers + 2,
            [](const auto& layer) { return layer.image_ready; })) return true;
        std::this_thread::yield();
    }
    return false;
}

} // namespace

TEST_CASE("player preview publishes decoded Layer 2 and base sharing one image", "[bga][player-preview][layer2]") {
    temporary_bga_image_t image;
    auto song = document();
    song.visual_assets.front().kind = asset_kind_t::image;
    song.visual_assets.front().source_path = image.path.filename();
    song.visual_assets.front().width = 2;
    song.visual_assets.front().height = 1;
    const auto resource = song.resources.front().id;
    song.charts.front().bga_events = {{.id = object_id_t{4}, .position = {measure_id_t{1}, 0},
        .resource = resource, .kind = visual_event_kind_t::background},
        {.id = object_id_t{5}, .position = {measure_id_t{1}, 0},
         .resource = resource, .kind = visual_event_kind_t::layer2}};
    const auto history = document_history_t::from(std::move(song));
    visual::image_cache_t image_cache;
    task_scheduler_t image_scheduler{1, 2};
    visual::image_job_t image_job{image_cache, image_scheduler};
    visual::video_frame_cache_t video_cache;
    task_scheduler_t video_scheduler{1, 2};
    visual::video_frame_job_t video_job{video_cache, video_scheduler};
    sys::player_preview_presenter_t presenter;
    WormUiPlayerPreviewView view{};
    REQUIRE(wait_for_bga_layers(presenter, view, history, image_job, video_job, image.path.parent_path()));
    CHECK(view.visual_layers[0].kind == WORM_UI_PLAYER_PREVIEW_VISUAL_BACKGROUND);
    CHECK(view.visual_layers[1].kind == WORM_UI_PLAYER_PREVIEW_VISUAL_LAYER2);
    CHECK(view.visual_layers[1].image_width == 2);
    CHECK(view.visual_layers[0].rgba_pixels == view.visual_layers[1].rgba_pixels);
}

TEST_CASE("player preview asynchronously publishes and replaces exact video frames",
          "[integration][visual][video][player-preview]") {
    const auto history = document_history_t::from(document());
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

    presenter.populate(view, history, chart_id_t{7}, true, player_preview_skin_t::mania, 36'000,
                       48'000, std::filesystem::path{WORMEDIT_FIXTURE_DIR} / "video", image_job,
                       video_job);
    REQUIRE(view.visual_layers[0].image_ready);
    CHECK(view.visual_layers[0].image_content_id == red_content_id);
    CHECK_FALSE(video_job.is_busy());

    REQUIRE(wait_for_frame(presenter, view, history, image_job, video_job, 72'000, red_content_id));
    CHECK(view.visual_layers[0].rgba_pixels[0] < 16);
    CHECK(view.visual_layers[0].rgba_pixels[1] > 240);
    CHECK(view.visual_layers[0].rgba_pixels[2] < 16);
    const auto green_content_id = view.visual_layers[0].image_content_id;

    REQUIRE(
        wait_for_frame(presenter, view, history, image_job, video_job, 24'000, green_content_id));
    CHECK(view.visual_layers[0].rgba_pixels[0] > 240);
    CHECK(view.visual_layers[0].rgba_pixels[1] < 16);
}

TEST_CASE("player preview links a late video frame diagnostic to its event",
          "[integration][visual][video][player-preview][diagnostic]") {
    const auto history = document_history_t::from(document());
    visual::image_cache_t image_cache;
    task_scheduler_t image_scheduler{1, 2};
    visual::image_job_t image_job{image_cache, image_scheduler};
    visual::video_frame_cache_t video_cache;
    task_scheduler_t video_scheduler{1, 2};
    visual::video_frame_job_t video_job{video_cache, video_scheduler};
    std::latch release_blocker{1};
    latch_release_t release{release_blocker};

    task_graph_t blocker_graph{1};
    const auto blocker_task = blocker_graph.create_task(
        {}, [&](std::span<const task_outcome_t>, std::stop_token) -> task_outcome_t {
            release_blocker.wait();
            return task_outcome_t{};
        });
    REQUIRE(blocker_task);
    auto blocker = video_scheduler.begin(std::move(blocker_graph));
    REQUIRE(blocker);

    sys::player_preview_presenter_t presenter;
    WormUiPlayerPreviewView view{};
    const auto directory = std::filesystem::path{WORMEDIT_FIXTURE_DIR} / "video";
    presenter.populate(view, history, chart_id_t{7}, true, player_preview_skin_t::mania, 24'000,
                       48'000, directory, image_job, video_job);
    REQUIRE(video_job.is_busy());
    presenter.populate(view, history, chart_id_t{7}, true, player_preview_skin_t::mania, 24'000,
                       48'000, directory, image_job, video_job);

    REQUIRE(view.visual_layer_count == 1);
    const auto& layer = view.visual_layers[0];
    CHECK(layer.object_id == 4);
    CHECK(layer.diagnostic == WORM_UI_PLAYER_PREVIEW_VISUAL_DIAGNOSTIC_VIDEO_FRAME_LATE);
    REQUIRE(layer.diagnostic_text);
    CHECK(std::string_view{layer.diagnostic_text}.find("presentation deadline") !=
          std::string_view::npos);
}

TEST_CASE("player preview publishes an event-linked video decoder failure",
          "[integration][visual][video][player-preview][diagnostic]") {
    auto broken_document = document();
    broken_document.visual_assets[0].source_path = "missing-video.mkv";
    const auto history = document_history_t::from(std::move(broken_document));
    visual::image_cache_t image_cache;
    task_scheduler_t image_scheduler{1, 2};
    visual::image_job_t image_job{image_cache, image_scheduler};
    visual::video_frame_cache_t video_cache;
    task_scheduler_t video_scheduler{1, 2};
    visual::video_frame_job_t video_job{video_cache, video_scheduler};
    sys::player_preview_presenter_t presenter;
    WormUiPlayerPreviewView view{};

    REQUIRE(wait_for_diagnostic(presenter, view, history, image_job, video_job,
                                WORM_UI_PLAYER_PREVIEW_VISUAL_DIAGNOSTIC_VIDEO_DECODE_FAILED));
    REQUIRE(view.visual_layer_count == 1);
    const auto& layer = view.visual_layers[0];
    CHECK(layer.object_id == 4);
    CHECK_FALSE(layer.image_pending);
    REQUIRE(layer.diagnostic_text);
    CHECK(std::string_view{layer.diagnostic_text}.find("could not be opened") !=
          std::string_view::npos);
}
