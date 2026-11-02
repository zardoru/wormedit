#include "sys/audio/player_preview_presenter.h"
#include "sys/storyboard/osbx_lua_link_runtime.h"
#include "sys/storyboard/storyboard_scene_presenter.h"
#include <wormedit/formats/native_project_storage.h>

#include <wormedit/core/command.h>
#include <wormedit/core/task_graph.h>
#include <wormedit/formats/native_project.h>
#include "../support/temporary_directory.h"
#include <wormedit/visual/image_job.h>
#include <wormedit/visual/video_job.h>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <atomic>
#include <algorithm>
#include <array>
#include <fstream>
#include <cstdint>
#include <filesystem>
#include <latch>
#include <source_location>
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

auto wait_for_frame(sys::player_preview_presenter_t& presenter, wormedit::ui::player_preview_view_t& view,
                    const document_history_t& history, visual::image_job_t& image_job,
                    visual::video_frame_job_t& video_job, std::uint64_t sample_frame,
                    std::uint64_t different_content_id = 0,
                    const std::filesystem::path& directory = std::filesystem::path{WORMEDIT_FIXTURE_DIR} / "video",
                    std::uint64_t session = 0) -> bool {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (std::chrono::steady_clock::now() < deadline) {
        presenter.populate(view, history, chart_id_t{7}, true, player_preview_skin_t::mania,
                           sample_frame, 48'000, directory, image_job, video_job, nullptr, session);
        if (view.visual_layers.size() == 1 && view.visual_layers[0].image_ready &&
            view.visual_layers[0].image_content_id != different_content_id) {
            return true;
        }
        std::this_thread::yield();
    }
    return false;
}

auto wait_for_diagnostic(sys::player_preview_presenter_t& presenter, wormedit::ui::player_preview_view_t& view,
                         const document_history_t& history, visual::image_job_t& image_job,
                         visual::video_frame_job_t& video_job,
                         wormedit::ui::player_preview_visual_diagnostic_t diagnostic,
                         const std::filesystem::path& directory = std::filesystem::path{WORMEDIT_FIXTURE_DIR} / "video",
                         std::uint64_t session = 0) -> bool {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (std::chrono::steady_clock::now() < deadline) {
        presenter.populate(view, history, chart_id_t{7}, true, player_preview_skin_t::mania, 24'000,
                           48'000, directory, image_job, video_job, nullptr, session);
        if (view.visual_layers.size() == 1 && view.visual_layers[0].diagnostic == diagnostic) {
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

struct storyboard_delivery_fixture_t {
    visual::image_cache_t cache;
    task_scheduler_t image_scheduler{1, 2};
    visual::image_job_t images{cache, image_scheduler};
    visual::video_frame_cache_t video_cache;
    task_scheduler_t video_scheduler{1, 2};
    visual::video_frame_job_t videos{video_cache, video_scheduler};
    sys::player_preview_presenter_t presenter;
    wormedit::ui::player_preview_view_t view;

    void populate(const document_history_t& history, const std::filesystem::path& directory) {
        presenter.populate(view, history, chart_id_t{7}, true, player_preview_skin_t::mania,
            24'000, 48'000, directory, images, videos);
    }
    auto wait(const document_history_t& history, const std::filesystem::path& directory) -> bool {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
        do {
            populate(history, directory);
            if (view.scene_available && view.storyboard_sprites.size() == 5 &&
                std::ranges::all_of(view.storyboard_sprites, [](const auto& sprite) { return sprite.image_ready; }))
                return true;
            std::this_thread::yield();
        } while (std::chrono::steady_clock::now() < deadline);
        return false;
    }
    void check_video_binding_cleared() const {
        REQUIRE(view.visual_layers.size() == 1);
        CHECK_FALSE(view.visual_layers[0].image_ready);
        CHECK(view.visual_layers[0].rgba_pixels.empty());
    }
    void check_missing_video_source(const document_history_t& history, const std::filesystem::path& directory) {
        REQUIRE(wait_for_diagnostic(presenter, view, history, images, videos,
            ui::player_preview_visual_diagnostic_t::video_decode_failed, directory));
        CHECK_FALSE(view.visual_layers[0].image_ready);
    }
    void check_new_session_video(const document_history_t& history, const std::filesystem::path& directory,
        std::uint64_t session, std::uint64_t previous_content) {
        REQUIRE(wait_for_frame(presenter, view, history, images, videos, 24'000,
            previous_content, directory, session));
        CHECK(view.visual_layers[0].rgba_pixels[0] > 240);
    }
};

struct temporary_bga_image_t {
    std::filesystem::path path{std::filesystem::temp_directory_path() /
        ("wormedit-bga-layer2-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".bmp")};
    temporary_bga_image_t(std::array<std::uint8_t, 3> color = {255, 0, 255}) {
        std::array<std::uint8_t, 62> bytes{
            0x42, 0x4d, 0x3e, 0, 0, 0, 0, 0, 0, 0, 0x36, 0, 0, 0, 0x28, 0,
            0, 0, 2, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0x18, 0, 0, 0,
            0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0, 0xff, 0, 0, 0};
        bytes[54] = color[2];
        bytes[55] = color[1];
        bytes[56] = color[0];
        std::ofstream stream{path, std::ios::binary};
        REQUIRE(stream.write(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    }
    ~temporary_bga_image_t() {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }
};

auto wait_for_bga_layers(sys::player_preview_presenter_t& presenter, wormedit::ui::player_preview_view_t& view,
                         const document_history_t& history, visual::image_job_t& image_job,
                         visual::video_frame_job_t& video_job, const std::filesystem::path& directory) -> bool {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (std::chrono::steady_clock::now() < deadline) {
        presenter.populate(view, history, chart_id_t{7}, true, player_preview_skin_t::mania,
                           0, 48'000, directory, image_job, video_job);
        if (view.visual_layers.size() == 2 && std::all_of(view.visual_layers.begin(), view.visual_layers.end(),
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
    wormedit::ui::player_preview_view_t view{};
    REQUIRE(wait_for_bga_layers(presenter, view, history, image_job, video_job, image.path.parent_path()));
    CHECK(view.visual_layers[0].kind == visual_event_kind_t::background);
    CHECK(view.visual_layers[1].kind == visual_event_kind_t::layer2);
    CHECK(view.visual_layers[1].image_width == 2);
    CHECK(view.visual_layers[0].rgba_pixels.data() == view.visual_layers[1].rgba_pixels.data());
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
    wormedit::ui::player_preview_view_t view{};

    REQUIRE(wait_for_frame(presenter, view, history, image_job, video_job, 24'000));
    REQUIRE(view.visual_layers[0].video_frame);
    REQUIRE(view.visual_layers[0].rgba_pixels.size() == 16U * 16U * 4U);
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

TEST_CASE("player preview clears video on same-clock source and session rebinding",
          "[integration][visual][video][player-preview][rebind]") {
    const auto history = document_history_t::from(document());
    storyboard_delivery_fixture_t fixture;
    fixture.view = {};
    REQUIRE(wait_for_frame(fixture.presenter, fixture.view, history, fixture.images, fixture.videos, 24'000));
    const auto previous_content = fixture.view.visual_layers[0].image_content_id;
    auto directory = std::filesystem::path{WORMEDIT_FIXTURE_DIR} / "video";
    auto empty_directory = test::temporary_directory_t::from();
    REQUIRE(empty_directory);
    std::uint64_t session{};
    SECTION("a new project directory cannot retain the old decoded video") {
        directory = empty_directory->path();
    }
    SECTION("a new session cannot retain a frame from the previous binding") {
        session = 1;
    }
    fixture.presenter.populate(fixture.view, history, chart_id_t{7}, true, player_preview_skin_t::mania,
                       24'000, 48'000, directory, fixture.images, fixture.videos, nullptr, session);
    fixture.check_video_binding_cleared();
    if (session == 0) {
        fixture.check_missing_video_source(history, directory);
    } else {
        fixture.check_new_session_video(history, directory, session, previous_content);
    }
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
    wormedit::ui::player_preview_view_t view{};
    const auto directory = std::filesystem::path{WORMEDIT_FIXTURE_DIR} / "video";
    presenter.populate(view, history, chart_id_t{7}, true, player_preview_skin_t::mania, 24'000,
                       48'000, directory, image_job, video_job);
    REQUIRE(video_job.is_busy());
    presenter.populate(view, history, chart_id_t{7}, true, player_preview_skin_t::mania, 24'000,
                       48'000, directory, image_job, video_job);

    REQUIRE(view.visual_layers.size() == 1);
    const auto& layer = view.visual_layers[0];
    CHECK(layer.object_id == object_id_t{4});
    CHECK(layer.diagnostic == wormedit::ui::player_preview_visual_diagnostic_t::video_frame_late);
    REQUIRE_FALSE(layer.diagnostic_text.empty());
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
    wormedit::ui::player_preview_view_t view{};

    REQUIRE(wait_for_diagnostic(presenter, view, history, image_job, video_job,
                                wormedit::ui::player_preview_visual_diagnostic_t::video_decode_failed));
    REQUIRE(view.visual_layers.size() == 1);
    const auto& layer = view.visual_layers[0];
    CHECK(layer.object_id == object_id_t{4});
    CHECK_FALSE(layer.image_pending);
    REQUIRE_FALSE(layer.diagnostic_text.empty());
    CHECK(std::string_view{layer.diagnostic_text}.find("could not be opened") !=
          std::string_view::npos);
}

namespace {
void require_storyboard_projection(bool value,
        std::source_location where = std::source_location::current()) {
    INFO(where.file_name() << ':' << where.line());
    REQUIRE(value);
}
void check_storyboard_projection(bool value,
        std::source_location where = std::source_location::current()) {
    INFO(where.file_name() << ':' << where.line());
    CHECK(value);
}

auto mixed_storyboard_document() -> song_document_t {
    auto song = document();
    song.charts.front().bga_events.clear();
    song.visual_assets.clear();
    song.resources = {{.id = resource_id_t{11}, .name = "geometry-only image",
        .asset = asset_id_t{21}, .missing = true}};
    song.visual_assets = {{.id = asset_id_t{21}, .kind = asset_kind_t::image,
        .source_path = "new.png"}};
    storyboard_sprite_t osb{.stable_id = 10, .source_order = 1,
        .layer = "Foreground", .origin = "BottomRight", .path = "old.png", .x = 123, .y = 87};
    storyboard_command_t fade{.opcode = storyboard_opcode_t::fade, .start_time_ms = 0,
        .end_time_ms = 1000, .from = {1}, .to = {1}, .value_count = 1, .source_order = 1};
    osb.nodes.push_back({.kind = storyboard_node_kind_t::command, .command = fade, .source_order = 1});
    auto same_order = osb;
    same_order.stable_id = 9;
    song.storyboards = {
        {.id = storyboard_id_t{30}, .name = "OSB foreground",
         .model = storyboard_document_model_t{.sprites = {osb, same_order}}},
        {.id = storyboard_id_t{31}, .name = "OSBX background",
         .osbx = osbx_document_t{.canvas = {.width = 800, .height = 600},
            .images = {{.id = "image", .file = "new.png"}},
            .nodes = {{.id = "new", .kind = osbx_node_kind_t::sprite, .image = "image",
                .ops = {{.id = "matrix", .kind = osbx_transform_kind_t::matrix,
                    .track = {.default_value = {1, 0, 0.5, 1, 10, 20}}}}}}}}};
    return song;
}
} // namespace

TEST_CASE("player preview merges OSB and OSBX by layer and refreshes borrowed scene indices",
          "[player-preview][osbx][storyboard]") {
    auto history = document_history_t::from(mixed_storyboard_document());
    visual::image_cache_t image_cache;
    task_scheduler_t image_scheduler{1, 2};
    visual::image_job_t image_job{image_cache, image_scheduler};
    visual::video_frame_cache_t video_cache;
    task_scheduler_t video_scheduler{1, 2};
    visual::video_frame_job_t video_job{video_cache, video_scheduler};
    sys::player_preview_presenter_t presenter;
    wormedit::ui::player_preview_view_t view;
    const auto populate = [&] {
        presenter.populate(view, history, chart_id_t{7}, true, player_preview_skin_t::mania,
                           24'000, 48'000, {}, image_job, video_job);
    };
    populate();
    require_storyboard_projection(view.scene_available);
    require_storyboard_projection(view.storyboard_sprites.size() == 3);
    check_storyboard_projection(view.storyboard_sprites[0].layer_order == 0);
    check_storyboard_projection(view.storyboard_sprites[0].transform ==
        std::array<double, 6>{1, 0, 0.5, 1, 10, 20});
    check_storyboard_projection(view.storyboard_sprites[0].logical_width == 800);
    check_storyboard_projection(view.storyboard_sprites[0].blend == wormedit::ui::image_blend_t::normal);
    check_storyboard_projection(view.storyboard_sprites[1].layer_order == 3);
    check_storyboard_projection(view.storyboard_sprites[1].stable_id == 10);
    check_storyboard_projection(view.storyboard_sprites[2].stable_id == 9);
    check_storyboard_projection(view.storyboard_sprites[1].origin_x == 1);
    check_storyboard_projection(view.storyboard_sprites[1].origin_y == 1);
    check_storyboard_projection(view.storyboard_sprites[1].blend == wormedit::ui::image_blend_t::inherit);
    populate();
    require_storyboard_projection(view.storyboard_sprites.size() == 3);
    history.document.storyboards[1].osbx->nodes[0].ops[0].track.default_value[4] = 60;
    ++history.document.revision;
    populate();
    require_storyboard_projection(view.scene_available);
    require_storyboard_projection(view.storyboard_sprites.size() == 3);
    check_storyboard_projection(view.storyboard_sprites[0].transform[4] == 60);
}

namespace {
auto catalog_storyboard_document() -> song_document_t {
    auto song = document();
    song.charts.front().bga_events.clear();
    song.visual_assets.front() = {.id = asset_id_t{20}, .kind = asset_kind_t::image,
        .source_path = "registered.bmp", .width = 2, .height = 1};
    // A valid persistent ID at the top of the domain must never be reused by the fallback.
    song.resources.front().id = resource_id_t{UINT64_MAX};
    osbx_document_t scene;
    for (const auto& [id, path] : std::array<std::pair<const char*, const char*>, 5>{
        {{"a", "a.bmp"}, {"alias", "./a.bmp"}, {"b", "b.bmp"},
         {"registered", "registered.bmp"}, {"registered-alias", "./registered.bmp"}}}) {
        scene.images.push_back({.id = id, .file = path});
        scene.nodes.push_back({.id = std::string{id} + "-sprite",
            .kind = osbx_node_kind_t::sprite, .image = id});
    }
    song.storyboards = {{.id = storyboard_id_t{30}, .osbx = std::move(scene)}};
    return song;
}

void check_delivery_colors(const wormedit::ui::player_preview_view_t& view,
                            std::array<std::uint8_t, 3> a, std::array<std::uint8_t, 3> b,
                            std::array<std::uint8_t, 3> registered) {
    require_storyboard_projection(view.storyboard_sprites.size() == 5);
    for (std::size_t i = 0; i < 3; ++i) {
        check_storyboard_projection(view.storyboard_sprites[0].rgba_pixels[i] == a[i]);
        check_storyboard_projection(view.storyboard_sprites[1].rgba_pixels[i] == a[i]);
        check_storyboard_projection(view.storyboard_sprites[2].rgba_pixels[i] == b[i]);
        check_storyboard_projection(view.storyboard_sprites[3].rgba_pixels[i] == registered[i]);
    }
}
} // namespace

TEST_CASE("OSBX project images decode with registered images and rebind without stale pixels",
          "[player-preview][osbx][image-fallback]") {
    auto first = test::temporary_directory_t::from();
    auto second = test::temporary_directory_t::from();
    require_storyboard_projection(static_cast<bool>(first) && static_cast<bool>(second));
    temporary_bga_image_t red{{255, 0, 0}}, green{{0, 255, 0}}, blue{{0, 0, 255}};
    std::filesystem::copy_file(red.path, first->path() / "a.bmp");
    std::filesystem::copy_file(green.path, first->path() / "b.bmp");
    std::filesystem::copy_file(blue.path, first->path() / "registered.bmp");
    std::filesystem::copy_file(blue.path, second->path() / "a.bmp");
    std::filesystem::copy_file(red.path, second->path() / "b.bmp");
    std::filesystem::copy_file(green.path, second->path() / "registered.bmp");
    auto history = document_history_t::from(catalog_storyboard_document());
    const auto before = write_native_project(history.document);
    require_storyboard_projection(before.value.has_value());
    storyboard_delivery_fixture_t fixture;
    require_storyboard_projection(fixture.wait(history, first->path()));
    check_delivery_colors(fixture.view, {255, 0, 0}, {0, 255, 0}, {0, 0, 255});
    const auto& sprites = fixture.view.storyboard_sprites;
    check_storyboard_projection(sprites[0].resource_id == sprites[1].resource_id);
    check_storyboard_projection(sprites[0].resource_id != sprites[2].resource_id);
    check_storyboard_projection(sprites[0].resource_id != sprites[3].resource_id);
    check_storyboard_projection(sprites[3].resource_id == resource_id_t{UINT64_MAX});
    check_storyboard_projection(sprites[3].resource_id == sprites[4].resource_id);
    check_storyboard_projection(fixture.cache.size() == 3);
    const auto first_content = sprites[0].image_content_id;
    require_storyboard_projection(fixture.wait(history, second->path()));
    check_delivery_colors(fixture.view, {0, 0, 255}, {255, 0, 0}, {0, 255, 0});
    check_storyboard_projection(fixture.view.storyboard_sprites[0].image_content_id != first_content);
    std::filesystem::copy_file(green.path, second->path() / "a.bmp", std::filesystem::copy_options::overwrite_existing);
    ++history.document.revision;
    require_storyboard_projection(fixture.wait(history, second->path()));
    check_delivery_colors(fixture.view, {0, 255, 0}, {255, 0, 0}, {0, 255, 0});
    std::filesystem::copy_file(red.path, second->path() / "a.bmp", std::filesystem::copy_options::overwrite_existing);
    history.document.id = song_id_t{2};
    require_storyboard_projection(fixture.wait(history, second->path()));
    check_delivery_colors(fixture.view, {255, 0, 0}, {255, 0, 0}, {0, 255, 0});
    history.document.id = song_id_t{1};
    --history.document.revision;
    const auto after = write_native_project(history.document);
    require_storyboard_projection(after.value.has_value());
    check_storyboard_projection(*before.value == *after.value);
}

TEST_CASE("OSBX missing unsafe and undecodable project images report preview failures",
          "[player-preview][osbx][image-fallback][negative]") {
    auto directory = test::temporary_directory_t::from();
    require_storyboard_projection(static_cast<bool>(directory));
    auto song = catalog_storyboard_document();
    song.storyboards[0].osbx->nodes.resize(1);
    song.storyboards[0].osbx->images.resize(1);
    bool invalid_graph = false;
    SECTION("missing") { song.storyboards[0].osbx->images[0].file = "missing.bmp"; }
    SECTION("unsafe") {
        song.storyboards[0].osbx->images[0].file = "../outside.bmp";
        invalid_graph = true;
    }
    SECTION("symlink") {
        temporary_bga_image_t outside;
        std::filesystem::create_symlink(outside.path, directory->path() / "a.bmp");
    }
    SECTION("decode failure") { std::ofstream{directory->path() / "a.bmp"} << "broken image"; }
    auto history = document_history_t::from(std::move(song));
    storyboard_delivery_fixture_t fixture;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
    do {
        fixture.populate(history, directory->path());
        if (!fixture.view.status_text.empty()) break;
        std::this_thread::yield();
    } while (std::chrono::steady_clock::now() < deadline);
    INFO("image path: " << history.document.storyboards[0].osbx->images[0].file);
    CAPTURE(invalid_graph, fixture.view.scene_available, fixture.view.status_text);
    check_storyboard_projection(fixture.view.scene_available != invalid_graph);
    check_storyboard_projection(!fixture.view.status_text.empty());
    check_storyboard_projection(std::ranges::none_of(fixture.view.storyboard_sprites,
        [](const auto& sprite) { return sprite.image_ready; }));
}

TEST_CASE("OSBX fallback does not bypass a registered image marked missing",
          "[player-preview][osbx][image-fallback][registered]") {
    auto directory = test::temporary_directory_t::from();
    require_storyboard_projection(static_cast<bool>(directory));
    temporary_bga_image_t image;
    std::filesystem::copy_file(image.path, directory->path() / "registered.bmp");
    auto song = catalog_storyboard_document();
    song.resources.front().missing = true;
    song.storyboards.front().osbx->nodes.resize(1);
    song.storyboards.front().osbx->images.resize(1);
    song.storyboards.front().osbx->images[0].file = "registered.bmp";
    const auto history = document_history_t::from(std::move(song));
    storyboard_delivery_fixture_t fixture;
    fixture.populate(history, directory->path());
    require_storyboard_projection(fixture.view.scene_available);
    require_storyboard_projection(fixture.view.storyboard_sprites.size() == 1);
    check_storyboard_projection(fixture.view.storyboard_sprites[0].resource_id == resource_id_t{UINT64_MAX});
    check_storyboard_projection(!fixture.view.storyboard_sprites[0].image_ready);
    check_storyboard_projection(!fixture.images.is_busy());
    check_storyboard_projection(fixture.cache.size() == 0);
}

#if defined(WORMEDIT_HAS_LUA)
TEST_CASE("saved OSBX links become ready at the same revision through the app worker contract",
          "[player-preview][osbx-link]") {
    auto directory = test::temporary_directory_t::from(); require_storyboard_projection(static_cast<bool>(directory));
    temporary_bga_image_t image;
    std::filesystem::copy_file(image.path,directory->path()/"linked.bmp");
    auto song = document(); song.charts.front().bga_events.clear();
    const std::string source = "return function(t,ctx) return 0.5 end";
    osbx_node_t node{.id = "sprite", .kind = osbx_node_kind_t::sprite, .image = "image"};
    node.tracks.opacity.lua = {.link = {.id = "opacity-link", .source = {.frozen_source = source,
        .source_reference = "not-read.lua", .source_hash = native_project_content_checksum(source)}},
        .begin_ms = 0, .end_ms = 1000};
    song.storyboards = {{.id = storyboard_id_t{30},
        .osbx = osbx_document_t{.images = {{.id = "image", .file = "linked.bmp"}}, .nodes = {node}}}};
    const auto saved = write_native_project(song); require_storyboard_projection(static_cast<bool>(saved.value));
    auto loaded = parse_native_project(*saved.value); require_storyboard_projection(static_cast<bool>(loaded.value));
    const auto history = document_history_t::from(std::move(*loaded.value));
    storyboard_delivery_fixture_t fixture;
    task_scheduler_t scheduler{1,2};
    sys::osbx_lua_link_runtime_t runtime{scheduler};
    runtime.update(history.document,1);
    fixture.populate(history,directory->path());
    check_storyboard_projection(!static_cast<bool>(fixture.view.scene_available));
    runtime.wait(history.document,1);
    require_storyboard_projection(static_cast<bool>(runtime.scenes(history.document).size() == 1)); require_storyboard_projection(static_cast<bool>(runtime.scenes(history.document).front().complete));
    const osbx_lua_prepared_scope_t prepared{runtime.scenes(history.document),runtime.generation()};
    const auto deadline = std::chrono::steady_clock::now()+std::chrono::seconds{3};
    do {
        fixture.populate(history,directory->path());
        if (fixture.view.storyboard_sprites.size() == 1 && fixture.view.storyboard_sprites.front().image_ready) break;
        std::this_thread::yield();
    } while (std::chrono::steady_clock::now() < deadline);
    require_storyboard_projection(static_cast<bool>(fixture.view.scene_available)); require_storyboard_projection(static_cast<bool>(fixture.view.storyboard_sprites.size() == 1));
    check_storyboard_projection(static_cast<bool>(fixture.view.storyboard_sprites.front().image_ready));
    check_storyboard_projection(static_cast<bool>(fixture.view.storyboard_sprites.front().opacity == 0.5));
    check_storyboard_projection(static_cast<bool>(fixture.view.document_revision == history.document.revision));
}
#endif

TEST_CASE("interactive storyboard preview defaults to the shared Pass scenario for both models",
          "[player-preview][storyboard][conditional]") {
    auto song = mixed_storyboard_document();
    auto& legacy = song.storyboards[0].model->sprites;
    legacy[0].layer = "Pass";
    legacy[1].layer = "Fail";
    auto& scene = *song.storyboards[1].osbx;
    scene.nodes[0].layer = osbx_layer_t::pass;
    auto fail = scene.nodes[0];
    fail.id = "fail";
    fail.layer = osbx_layer_t::fail;
    fail.ops[0].id = "fail-matrix";
    scene.nodes.push_back(fail);
    const auto history = document_history_t::from(std::move(song));
    storyboard_delivery_fixture_t fixture;
    fixture.populate(history, {});
    require_storyboard_projection(fixture.view.scene_available);
    require_storyboard_projection(fixture.view.storyboard_sprites.size() == 2);
    for (const auto& sprite : fixture.view.storyboard_sprites)
        check_storyboard_projection(sprite.layer_order == 2);
}

TEST_CASE("Storyboard canvas and auxiliary player use one BGA image request union without churn",
          "[storyboard-canvas][player-preview][image-fallback]") {
    auto directory = test::temporary_directory_t::from();
    require_storyboard_projection(static_cast<bool>(directory));
    temporary_bga_image_t red{{255, 0, 0}}, green{{0, 255, 0}}, blue{{0, 0, 255}};
    std::filesystem::copy_file(red.path, directory->path() / "a.bmp");
    std::filesystem::copy_file(green.path, directory->path() / "b.bmp");
    std::filesystem::copy_file(blue.path, directory->path() / "registered.bmp");
    auto song = catalog_storyboard_document();
    song.charts.front().bga_events = {{.id = object_id_t{4}, .position = {measure_id_t{1}, 0},
        .resource = resource_id_t{UINT64_MAX}, .kind = visual_event_kind_t::background}};
    auto history = document_history_t::from(std::move(song));
    storyboard_delivery_fixture_t fixture;
    ui::storyboard_view_t storyboard;
    storyboard.visible = true;
    storyboard.storyboards = {{.id = storyboard_id_t{30}, .kind = ui::storyboard_kind_t::osbx}};
    storyboard.state.selection.storyboard = storyboard_id_t{30};
    const auto populate = [&](bool player_visible) {
        fixture.presenter.populate(fixture.view, history, chart_id_t{7}, player_visible,
            player_preview_skin_t::mania, 24'000, 48'000, directory->path(), fixture.images,
            fixture.videos, &storyboard, 7);
    };
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
    do {
        populate(true);
        if (storyboard.canvas.sprites.size() == 5 && storyboard.canvas.sprites[0].image_ready &&
            fixture.view.storyboard_sprites.size() == 5 && fixture.view.storyboard_sprites[0].image_ready) break;
        std::this_thread::yield();
    } while (std::chrono::steady_clock::now() < deadline);
    require_storyboard_projection(storyboard.canvas.sprites.size() == 5);
    require_storyboard_projection(fixture.view.storyboard_sprites.size() == 5);
    require_storyboard_projection(storyboard.canvas.sprites[0].image_ready);
    check_delivery_colors(fixture.view, {255, 0, 0}, {0, 255, 0}, {0, 0, 255});
    require_storyboard_projection(!fixture.view.visual_layers.empty());
    check_storyboard_projection(fixture.view.visual_layers.front().image_ready);
    const auto content = storyboard.canvas.sprites[0].image_content_id;
    const auto* pixels = storyboard.canvas.sprites[0].rgba_pixels.data();
    populate(true);
    check_storyboard_projection(storyboard.canvas.sprites[0].image_content_id == content);
    check_storyboard_projection(storyboard.canvas.sprites[0].rgba_pixels.data() == pixels);
    check_storyboard_projection(fixture.view.storyboard_sprites[0].rgba_pixels.data() == pixels);
    // Hiding the auxiliary player retains the selected canvas without requiring a chart.
    fixture.presenter.populate(fixture.view, history, std::nullopt, false, player_preview_skin_t::mania,
        24'000, 48'000, directory->path(), fixture.images, fixture.videos, &storyboard, 7);
    check_storyboard_projection(storyboard.canvas.available);
    check_storyboard_projection(storyboard.canvas.sprites[0].image_ready);
    check_storyboard_projection(!fixture.view.visible);
}
TEST_CASE("Storyboard canvas first aggregate cap reports auxiliary overflow without hiding its notes",
          "[storyboard-canvas][player-preview][cap]") {
    auto song = catalog_storyboard_document();
    auto& scene = *song.storyboards.front().osbx;
    scene.nodes.clear();
    for (std::size_t i = 0; i < 513; ++i)
        scene.nodes.push_back({.id = "sprite-" + std::to_string(i), .kind = osbx_node_kind_t::sprite, .image = "a"});
    auto history = document_history_t::from(std::move(song));
    storyboard_delivery_fixture_t fixture;
    ui::storyboard_view_t storyboard;
    storyboard.visible = true;
    storyboard.storyboards = {{.id = storyboard_id_t{30}, .kind = ui::storyboard_kind_t::osbx}};
    storyboard.state.selection.storyboard = storyboard_id_t{30};
    fixture.presenter.populate(fixture.view, history, chart_id_t{7}, true, player_preview_skin_t::mania,
        24'000, 48'000, {}, fixture.images, fixture.videos, &storyboard, 7);
    check_storyboard_projection(storyboard.canvas.available);
    check_storyboard_projection(storyboard.canvas.sprites.size() == 513);
    check_storyboard_projection(fixture.view.scene_available);
    check_storyboard_projection(fixture.view.storyboard_sprites.empty());
    check_storyboard_projection(fixture.view.status_text.find("budget") != std::string::npos);
    check_storyboard_projection(!fixture.view.lanes.empty());
}

TEST_CASE("Shared image job outer failures honor identity and preserve aggregate overflow status",
          "[storyboard-canvas][player-preview][image-fallback][negative]") {
    auto directory = test::temporary_directory_t::from();
    require_storyboard_projection(static_cast<bool>(directory));
    temporary_bga_image_t image{{255, 0, 0}};
    std::filesystem::copy_file(image.path, directory->path() / "a.bmp");
    auto song = catalog_storyboard_document();
    auto& scene = *song.storyboards.front().osbx;
    scene.nodes.clear();
    for (std::size_t i = 0; i < 513; ++i)
        scene.nodes.push_back({.id = "sprite-" + std::to_string(i), .kind = osbx_node_kind_t::sprite, .image = "a"});
    auto history = document_history_t::from(std::move(song));
    storyboard_delivery_fixture_t fixture;
    ui::storyboard_view_t storyboard;
    storyboard.visible = true;
    storyboard.storyboards = {{.id = storyboard_id_t{30}, .kind = ui::storyboard_kind_t::osbx}};
    storyboard.state.selection.storyboard = storyboard_id_t{30};
    const auto populate = [&] {
        fixture.presenter.populate(fixture.view, history, chart_id_t{7}, true, player_preview_skin_t::mania,
            24'000, 48'000, directory->path(), fixture.images, fixture.videos, &storyboard, 7);
    };
    populate(); // Bind the current document and its resource union before injecting worker outcomes.
    using base_t = async_job_t<visual::image_request_t, visual::image_job_result_t, async_job_project_identity_t>;
    for (const bool stale : {true, false}) {
        const auto project = stale ? song_id_t{999} : history.document.id;
        // Exercise the image job's existing public generic worker seam, without a production test hook.
        const auto started = static_cast<base_t&>(fixture.images).request([&] {
            return visual::image_request_t{.cache = fixture.cache, .project = project,
                .document_revision = history.document.revision};
        }, {.project = project, .document_revision = history.document.revision},
        +[](const visual::image_request_t&, std::stop_token) -> base_t::work_outcome_t {
            return std::unexpected(wormedit::error_t<failure_code_t>{failure_code_t::task_failed,
                "Injected decode worker failure"});
        });
        require_storyboard_projection(static_cast<bool>(started));
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
        do { populate(); std::this_thread::yield(); }
        while (fixture.images.is_busy() && std::chrono::steady_clock::now() < deadline);
        populate();
        check_storyboard_projection(fixture.view.status_text.find("budget") != std::string::npos);
        check_storyboard_projection((fixture.view.status_text.find("Injected decode worker failure") != std::string::npos) == !stale);
        check_storyboard_projection((storyboard.canvas.status.find("Injected decode worker failure") != std::string::npos) == !stale);
        check_storyboard_projection(storyboard.canvas.sprites.size() == 513);
        check_storyboard_projection(fixture.view.storyboard_sprites.empty());
    }
}

namespace {
auto thumbnail_catalog_song() -> song_document_t {
    osbx_document_t scene;
    std::string path{"a.bmp"};
    for (std::size_t i = 0; i < ui::maximum_preview_storyboard_instances + 16; ++i) {
        scene.images.push_back({.id = "thumbnail-" + std::to_string(i), .file = path});
        path.insert(0, "./"); // Distinct authored aliases, one actual decoded source.
    }
    scene.images.push_back({.id = "primary-b", .file = "b.bmp"});
    scene.nodes = {{.id = "primary-a", .kind = osbx_node_kind_t::sprite, .image = "thumbnail-0"},
        {.id = "node-primary-b", .kind = osbx_node_kind_t::sprite, .image = "primary-b",
         .lifetime = osbx_lifetime_t{.begin_ms = 1, .end_ms = 1}}};
    return {.id = song_id_t{1}, .revision = 3,
        .storyboards = {{.id = storyboard_id_t{11}, .osbx = std::move(scene)}}};
}
std::atomic<bool> thumbnail_primary_entered{}, thumbnail_primary_release{}, thumbnail_primary_cancelled{}, thumbnail_primary_finished{};
auto held_primary_decode(const visual::image_request_t& request, std::stop_token stop)
    -> result_t<visual::image_job_result_t, wormedit::error_t<failure_code_t>> {
    thumbnail_primary_entered = true;
    while (!thumbnail_primary_release && !stop.stop_requested()) std::this_thread::yield();
    thumbnail_primary_cancelled = stop.stop_requested();
    thumbnail_primary_finished = true;
    return visual::image_job_result_t{.project = request.project, .document_revision = request.document_revision};
}
auto wait_for_primary_worker(const std::atomic<bool>& flag) -> bool {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
    while (!flag && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
    return flag;
}
class thumbnail_catalog_delivery_t {
  public:
    explicit thumbnail_catalog_delivery_t(std::filesystem::path directory, std::size_t count = 2)
        : directory_(std::move(directory)), images_(cache_, scheduler_, count) {}
    void fill_thumbnail_catalog() {
        (void)scenes_.begin(song_, 7, directory_, images_);
        REQUIRE(scenes_.prepare_image_thumbnail(thumbnail_, storyboard_id_t{11}, "thumbnail-0"));
        identity_ = thumbnail_.resource_id;
        for (std::size_t i = 1; i < ui::maximum_preview_storyboard_instances; ++i) check_alias(i);
    }
    void check_refused_thumbnail_admissions() {
        for (std::size_t i = 0; i < 16; ++i) {
            const auto result = scenes_.prepare_image_thumbnail(thumbnail_, storyboard_id_t{11},
                "thumbnail-" + std::to_string(ui::maximum_preview_storyboard_instances + i));
            REQUIRE_FALSE(result);
            CHECK(result.error().code == failure_code_t::capacity_exceeded);
            CHECK_FALSE(thumbnail_.resource_id);
        }
    }
    void check_thumbnail_changes_preserve_primary_work() {
        (void)scenes_.begin(song_, 7, directory_, images_);
        REQUIRE(scenes_.refill(player_.storyboard_sprites, storyboard_id_t{11}, {},
            ui::maximum_preview_storyboard_instances).ok());
        scenes_.finish_images(player_, nullptr, images_);
        thumbnail_primary_entered = false; thumbnail_primary_release = false; thumbnail_primary_cancelled = false;
        thumbnail_primary_finished = false;
        using job_t = async_job_t<visual::image_request_t, visual::image_job_result_t, async_job_project_identity_t>;
        REQUIRE(static_cast<job_t&>(images_).request([&] {
            return visual::image_request_t{.cache = cache_, .project = song_.id, .document_revision = song_.revision};
        }, {.project = song_.id, .document_revision = song_.revision}, held_primary_decode));
        REQUIRE(wait_for_primary_worker(thumbnail_primary_entered));
        REQUIRE(prepare_visible_thumbnails());
        scenes_.finish_images(player_, nullptr, images_, thumbnail_views_);
        scenes_.finish_images(player_, nullptr, images_); // Hiding the optional rows also preserves the same work.
        thumbnail_primary_release = true;
        REQUIRE(wait_for_primary_worker(thumbnail_primary_finished));
        scheduler_.shutdown();
        CHECK_FALSE(thumbnail_primary_cancelled);
    }
    auto wait_for_primary(std::int64_t time, bool with_thumbnails = false) -> bool {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
        do {
            (void)scenes_.begin(song_, 7, directory_, images_);
            const auto frame = scenes_.refill(player_.storyboard_sprites, storyboard_id_t{11},
                {.milliseconds = time}, ui::maximum_preview_storyboard_instances);
            if (!frame.ok() || !frame.value) return false;
            std::span<ui::player_preview_visual_layer_view_t* const> demand;
            if (with_thumbnails) {
                if (!prepare_visible_thumbnails()) return false;
                demand = thumbnail_views_;
            }
            scenes_.finish_images(player_, nullptr, images_, demand);
            if (!player_.storyboard_sprites.empty() && std::ranges::all_of(player_.storyboard_sprites,
                [](const auto& sprite) { return sprite.image_ready; }) &&
                (!with_thumbnails || thumbnail_.image_ready)) return true;
            std::this_thread::yield();
        } while (std::chrono::steady_clock::now() < deadline);
        return false;
    }
    void check_promoted_primary() const {
        REQUIRE(player_.storyboard_sprites.size() == 1);
        CHECK(player_.storyboard_sprites[0].resource_id == identity_);
        CHECK(player_.storyboard_sprites[0].rgba_pixels[0] == 255);
        CHECK(player_.storyboard_sprites[0].rgba_pixels[1] == 0);
    }
    void check_new_primary_preserves_alias() const {
        REQUIRE(player_.storyboard_sprites.size() == 2);
        CHECK(player_.storyboard_sprites[0].resource_id == identity_);
        CHECK(player_.storyboard_sprites[0].rgba_pixels[0] == 255);
        CHECK(player_.storyboard_sprites[1].resource_id != identity_);
        CHECK(player_.storyboard_sprites[1].rgba_pixels[1] == 255);
    }
    void check_shared_request_refusal() const {
        REQUIRE(player_.storyboard_sprites.size() == 1);
        CHECK(images_.maximum_sources_per_request() == 1);
        CHECK(thumbnail_.resource_id == player_.storyboard_sprites[0].resource_id);
        CHECK(thumbnail_.image_ready);
        CHECK_FALSE(refused_thumbnail_.image_ready);
        CHECK(refused_thumbnail_.diagnostic_text == "Thumbnail exceeds the shared image request budget.");
    }
  private:
    auto prepare_visible_thumbnails() -> bool {
        const auto alias = scenes_.prepare_image_thumbnail(thumbnail_, storyboard_id_t{11}, "thumbnail-0");
        const auto extra = scenes_.prepare_image_thumbnail(refused_thumbnail_, storyboard_id_t{11}, "primary-b");
        return alias.has_value() && extra.has_value();
    }
    void check_alias(std::size_t index) {
        REQUIRE(scenes_.prepare_image_thumbnail(thumbnail_, storyboard_id_t{11}, "thumbnail-" + std::to_string(index)));
        CHECK(thumbnail_.resource_id == identity_);
    }
    song_document_t song_{thumbnail_catalog_song()};
    std::filesystem::path directory_;
    visual::image_cache_t cache_;
    task_scheduler_t scheduler_{1, 4};
    visual::image_job_t images_;
    sys::storyboard_scene_presenter_t scenes_;
    ui::player_preview_view_t player_{.visible = true};
    ui::player_preview_visual_layer_view_t thumbnail_;
    ui::player_preview_visual_layer_view_t refused_thumbnail_;
    std::array<ui::player_preview_visual_layer_view_t*, 2> thumbnail_views_{&thumbnail_, &refused_thumbnail_};
    resource_id_t identity_;
};
}
TEST_CASE("Storyboard thumbnail catalog yields to a new decoded primary and preserves promoted aliases",
          "[storyboard-canvas][player-preview][image-fallback][thumbnail]") {
    auto directory = test::temporary_directory_t::from(); REQUIRE(directory);
    temporary_bga_image_t red{{255, 0, 0}}, green{{0, 255, 0}};
    std::filesystem::copy_file(red.path, directory->path() / "a.bmp");
    std::filesystem::copy_file(green.path, directory->path() / "b.bmp");
    thumbnail_catalog_delivery_t delivery{directory->path()};
    delivery.fill_thumbnail_catalog();
    delivery.check_refused_thumbnail_admissions();
    REQUIRE(delivery.wait_for_primary(0));
    delivery.check_promoted_primary();
    REQUIRE(delivery.wait_for_primary(1));
    delivery.check_new_primary_preserves_alias();
}
TEST_CASE("Storyboard thumbnails deduplicate against primary demand and explicitly refuse excess request slots",
          "[storyboard-canvas][player-preview][image-fallback][thumbnail]") {
    auto directory = test::temporary_directory_t::from(); REQUIRE(directory);
    temporary_bga_image_t red{{255, 0, 0}}, green{{0, 255, 0}};
    std::filesystem::copy_file(red.path, directory->path() / "a.bmp");
    std::filesystem::copy_file(green.path, directory->path() / "b.bmp");
    thumbnail_catalog_delivery_t delivery{directory->path(), 1};
    REQUIRE(delivery.wait_for_primary(0, true));
    delivery.check_shared_request_refusal();
}

TEST_CASE("Optional thumbnail demand changes never cancel an already admitted primary decode",
          "[storyboard-canvas][player-preview][image-fallback][thumbnail][cancel]") {
    auto directory = test::temporary_directory_t::from(); REQUIRE(directory);
    temporary_bga_image_t red{{255, 0, 0}}, green{{0, 255, 0}};
    std::filesystem::copy_file(red.path, directory->path() / "a.bmp");
    std::filesystem::copy_file(green.path, directory->path() / "b.bmp");
    thumbnail_catalog_delivery_t delivery{directory->path()};
    delivery.check_thumbnail_changes_preserve_primary_work();
}

namespace {
auto animation_preview_song() -> song_document_t {
    auto song = document();
    song.charts.front().bga_events.clear();
    song.visual_assets.front() = {.id = asset_id_t{20}, .kind = asset_kind_t::image,
        .source_path = "registered0.bmp", .width = 2, .height = 1};
    const auto parsed = parse_osb("Animation,Foreground,Centre,a.bmp,0,0,3,100,LoopForever\n F,0,0,1000,1,1\n"
        "Animation,Foreground,Centre,./a.bmp,0,0,3,100,LoopForever\n F,0,0,1000,1,1\n"
        "Animation,Foreground,Centre,registered.bmp,0,0,3,100,LoopForever\n F,0,0,1000,1,1\n");
    require_storyboard_projection(parsed.value.has_value());
    song.storyboards = {{.id = storyboard_id_t{30}, .model = *parsed.value}};
    return song;
}

auto wait_animation_frame(storyboard_delivery_fixture_t& fixture, const document_history_t& history,
    const std::filesystem::path& directory, std::int64_t milliseconds, bool missing = false) -> bool {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
    do {
        fixture.presenter.populate(fixture.view, history, chart_id_t{7}, true, player_preview_skin_t::mania,
            milliseconds * 48, 48'000, directory, fixture.images, fixture.videos);
        const auto& sprites = fixture.view.storyboard_sprites;
        if (sprites.size() == 3 && (!missing || !fixture.view.status_text.empty()) &&
            std::ranges::all_of(sprites, [missing](const auto& sprite) {
            return missing ? !sprite.image_ready && !sprite.image_pending : sprite.image_ready;
        })) return true;
        std::this_thread::yield();
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}
}

TEST_CASE("Animation preview shares registered and project frame catalog across seeks", "[storyboard][player-preview][animation]") {
    auto directory = test::temporary_directory_t::from();
    require_storyboard_projection(static_cast<bool>(directory));
    temporary_bga_image_t red{{255, 0, 0}}, green{{0, 255, 0}};
    std::filesystem::copy_file(red.path, directory->path() / "a0.bmp");
    std::filesystem::copy_file(green.path, directory->path() / "a1.bmp");
    std::filesystem::copy_file(red.path, directory->path() / "registered0.bmp");
    std::filesystem::copy_file(green.path, directory->path() / "registered1.bmp");
    const auto history = document_history_t::from(animation_preview_song());
    storyboard_delivery_fixture_t fixture;
    require_storyboard_projection(wait_animation_frame(fixture, history, directory->path(), 0));
    check_storyboard_projection(fixture.view.storyboard_sprites[2].resource_id == history.document.resources.front().id);
    check_storyboard_projection(fixture.view.storyboard_sprites[0].resource_id == fixture.view.storyboard_sprites[1].resource_id);
    require_storyboard_projection(wait_animation_frame(fixture, history, directory->path(), 100));
    check_storyboard_projection(fixture.view.storyboard_sprites[0].rgba_pixels[1] == 255);
    require_storyboard_projection(wait_animation_frame(fixture, history, directory->path(), 200, true));
    require_storyboard_projection(wait_animation_frame(fixture, history, directory->path(), 0));
    check_storyboard_projection(fixture.view.storyboard_sprites[0].rgba_pixels[0] == 255);
    check_storyboard_projection(fixture.cache.size() == 4);
}

TEST_CASE("Animation selected paths respect the existing bounded image catalog", "[storyboard][player-preview][animation][bounds]") {
    auto directory = test::temporary_directory_t::from();
    require_storyboard_projection(static_cast<bool>(directory));
    auto song = animation_preview_song();
    auto& sprites = song.storyboards.front().model->sprites;
    sprites.resize(1);
    sprites.front().animation = storyboard_animation_t{
        .frame_count = static_cast<std::uint32_t>(ui::maximum_preview_storyboard_instances + 2), .frame_delay_ms = 1};
    sprites.front().nodes.front().command.end_time_ms = ui::maximum_preview_storyboard_instances + 1;
    storyboard_delivery_fixture_t fixture;
    sys::storyboard_scene_presenter_t scenes;
    (void)scenes.begin(song, 1, directory->path(), fixture.images);
    std::vector<ui::player_preview_storyboard_sprite_view_t> output;
    diagnosed_t<bool> frame;
    for (std::size_t i = 0; i <= ui::maximum_preview_storyboard_instances; ++i) {
        output.clear();
        frame = scenes.refill(output, std::nullopt, {.milliseconds = static_cast<std::int64_t>(i)}, 1);
    }
    require_storyboard_projection(output.size() == 1);
    check_storyboard_projection(std::ranges::any_of(frame.diagnostics, [](const auto& diagnostic) {
        return diagnostic.message.find("catalog exceeds its bounds") != std::string::npos;
    }));
    check_storyboard_projection(fixture.cache.size() == 0);
}

namespace {
auto osbx_animation_preview_song() -> song_document_t {
    auto song = animation_preview_song();
    osbx_document_t scene{.images = {{.id = "frames", .file = "a.bmp"},
        {.id = "alias", .file = "./a.bmp"}, {.id = "registered", .file = "registered.bmp"}}};
    for (const auto& image : scene.images) {
        osbx_node_t node{.id = image.id + "-node", .kind = osbx_node_kind_t::animation, .image = image.id,
            .lifetime = osbx_lifetime_t{.begin_ms = 0, .end_ms = 1000}};
        node.animation = storyboard_animation_t{.frame_count = 3, .frame_delay_ms = 100};
        scene.nodes.push_back(std::move(node));
    }
    song.storyboards.front().model.reset(); song.storyboards.front().osbx = std::move(scene);
    return song;
}
}

TEST_CASE("OSBX Animation uses the same registered frame catalog across seeks", "[storyboard][player-preview][osbx][animation]") {
    auto directory = test::temporary_directory_t::from();
    require_storyboard_projection(static_cast<bool>(directory));
    temporary_bga_image_t red{{255, 0, 0}}, green{{0, 255, 0}};
    std::filesystem::copy_file(red.path, directory->path() / "a0.bmp");
    std::filesystem::copy_file(green.path, directory->path() / "a1.bmp");
    std::filesystem::copy_file(red.path, directory->path() / "registered0.bmp");
    std::filesystem::copy_file(green.path, directory->path() / "registered1.bmp");
    const auto history = document_history_t::from(osbx_animation_preview_song());
    storyboard_delivery_fixture_t fixture;
    require_storyboard_projection(wait_animation_frame(fixture, history, directory->path(), 0));
    check_storyboard_projection(fixture.view.storyboard_sprites[0].resource_id == fixture.view.storyboard_sprites[1].resource_id);
    require_storyboard_projection(wait_animation_frame(fixture, history, directory->path(), 100));
    check_storyboard_projection(fixture.view.storyboard_sprites[0].rgba_pixels[1] == 255);
    require_storyboard_projection(wait_animation_frame(fixture, history, directory->path(), 200, true));
    require_storyboard_projection(wait_animation_frame(fixture, history, directory->path(), 0));
    check_storyboard_projection(fixture.view.storyboard_sprites[0].rgba_pixels[0] == 255);
    check_storyboard_projection(fixture.cache.size() == 4);
}
