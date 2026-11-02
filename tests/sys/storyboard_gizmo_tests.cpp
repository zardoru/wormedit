#include "sys/storyboard/storyboard_gizmo_view.h"
#include "sys/storyboard/storyboard_canvas_view.h"
#include "sys/storyboard/storyboard_inspector_view.h"
#include "sys/storyboard/storyboard_scene_presenter.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <algorithm>
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
#include <wormedit/visual/image_job.h>
#endif

using namespace wormedit;
namespace {
auto gizmo_song() -> song_document_t {
    return {.id = song_id_t{1}, .revision = 1, .storyboards = {{.id = storyboard_id_t{11},
        .osbx = osbx_document_t{.nodes = {{.id = "node", .ops = {
            {.id = "move", .kind = osbx_transform_kind_t::translate, .track = {.default_value = {10, 20}}},
            {.id = "later-move", .kind = osbx_transform_kind_t::translate, .track = {.default_value = {40, 50}}},
            {.id = "rotate", .kind = osbx_transform_kind_t::rotate, .track = {.default_value = {90}}}}}}}}}};
}
struct gizmo_projection_t {
    sys::storyboard_scene_presenter_t scenes;
    ui::storyboard_view_t view;
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
    visual::image_cache_t cache;
    task_scheduler_t scheduler{1, 2};
    visual::image_job_t images{cache, scheduler};
#endif
    void project(const song_document_t& song, storyboard_time_t time = {}, std::string_view node = "node") {
        view.state.selection = {.storyboard = storyboard_id_t{11}, .nodes = {std::string(node)}};
        scenes.begin(song, 7, {}
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
            , images
#endif
        );
        view.canvas.reset_frame();
        sys::populate_storyboard_canvas(view.canvas, scenes, storyboard_id_t{11}, time);
        sys::build_storyboard_inspector_view(view, song, time, scenes.node_state(storyboard_id_t{11}, node),
            scenes.scene(storyboard_id_t{11}));
        sys::build_storyboard_gizmo_view(view, scenes);
    }
};
void check_move_context(const ui::storyboard_gizmo_target_t& target) {
    CHECK(target.op == "move");
    CHECK(target.world_side.apply({0, 0}).x == Catch::Approx(-50));
    CHECK(target.world_side.apply({0, 0}).y == Catch::Approx(40));
}
enum class gizmo_refusal_t { generated, osb, linked_track, linked_children, singular, nonfinite };
void configure_refusal(storyboard_document_t& board, gizmo_refusal_t refusal) {
    if (refusal == gizmo_refusal_t::generated) { board.owning_generator_recipe = recipe_id_t{3}; return; }
    if (refusal == gizmo_refusal_t::osb) { board.osbx.reset(); board.model.emplace(); return; }
    auto& node = board.osbx->nodes[0];
    if (refusal == gizmo_refusal_t::linked_track) { node.ops[0].track.lua = osbx_lua_track_link_t{}; return; }
    if (refusal == gizmo_refusal_t::linked_children) {
        node.parent = "parent";
        board.osbx->nodes.push_back({.id = "parent", .children_link = osbx_lua_link_t{}});
        return;
    }
    const auto scale = refusal == gizmo_refusal_t::singular ? 0.0 : 1e308;
    node.ops.push_back({.id = "zero", .kind = osbx_transform_kind_t::scale, .track = {.default_value = {scale, 1}}});
    if (refusal == gizmo_refusal_t::nonfinite)
        node.ops.push_back({.id = "overflow", .kind = osbx_transform_kind_t::scale, .track = {.default_value = {scale, 1}}});
}
} // namespace

TEST_CASE("Storyboard gizmo projection picks the first matching op and shared evaluated world-side space",
          "[sys][storyboard-gizmo]") {
    auto song = gizmo_song();
    gizmo_projection_t fixture;
    fixture.project(song);
    const auto& gizmo = fixture.view.canvas.gizmo;
    REQUIRE(gizmo.targets().size() == 2);
    check_move_context(gizmo.targets().front());
    CHECK(gizmo.targets().back().op == "rotate");
    CHECK(gizmo.missing == std::vector{osbx_transform_kind_t::scale});
}

TEST_CASE("Storyboard gizmo projection retains fractional sampling and snapped auto-key identity",
          "[sys][storyboard-gizmo]") {
    auto song = gizmo_song();
    song.storyboards[0].osbx->nodes[0].ops[0].track.keys = {
        {.id = "key-a", .time_ms = -2, .value = {0, 0}, .easing = osbx_linear_easing_t{}},
        {.id = "key-b", .time_ms = 0, .value = {20, 40}}};
    gizmo_projection_t fixture;
    fixture.project(song, {.milliseconds = -2, .remainder = 1, .denominator = 2});
    const auto targets = fixture.view.canvas.gizmo.targets();
    REQUIRE_FALSE(targets.empty());
    CHECK(targets.front().value == std::vector<double>{5, 10});
    CHECK(targets.front().key == "key-a");
    CHECK(targets.front().key_time_ms == -2);
}

TEST_CASE("Storyboard gizmo projection refuses generated linked and singular nodes",
          "[sys][storyboard-gizmo][negative]") {
    const auto refusal = GENERATE(gizmo_refusal_t::generated, gizmo_refusal_t::osb, gizmo_refusal_t::linked_track,
        gizmo_refusal_t::linked_children, gizmo_refusal_t::singular, gizmo_refusal_t::nonfinite);
    auto song = gizmo_song();
    configure_refusal(song.storyboards[0], refusal);
    gizmo_projection_t fixture;
    fixture.project(song);
    const auto& gizmo = fixture.view.canvas.gizmo;
    if (refusal == gizmo_refusal_t::linked_track)
        CHECK(std::ranges::none_of(gizmo.targets(), [](const auto& target) { return target.op == "move"; }));
    else CHECK(gizmo.targets().empty());
    CHECK_FALSE(gizmo.reason.empty());
}

#if defined(WORMEDIT_HAS_IMAGE_DECODER) && defined(WORMEDIT_HAS_VIDEO_DECODER)
#include "sys/audio/player_preview_presenter.h"
#include "../support/temporary_directory.h"
#include <wormedit/core/interaction_dispatch.h>
#include <wormedit/core/osbx_edit.h>
#include <wormedit/formats/native_project.h>
#include <wormedit/visual/video_job.h>
#include <chrono>
#include <fstream>
#include <thread>

namespace {
auto gizmo_media_song() -> song_document_t {
    chart_document_t chart{.id = chart_id_t{7}, .divisions_per_standard_measure = 960,
        .measures = {{.id = measure_id_t{1}, .definition = {{1, 1}, meter_t{4, 4}}}},
        .lane_layouts = {{.id = object_id_t{2}, .position = {measure_id_t{1}, 0},
            .layout = {.profile_id = "test-single", .display_name = "Test Single",
                .lanes = {{.id = lane_id_t{1}, .name = "Lane", .role = lane_role_t::playable}}}}},
        .bpms = {{.id = object_id_t{3}, .position = {measure_id_t{1}, 0}, .beats_per_minute = {120, 1}}},
        .bga_events = {{.id = object_id_t{4}, .position = {measure_id_t{1}, 0},
            .resource = resource_id_t{10}, .kind = visual_event_kind_t::video}}};
    osbx_document_t scene{.images = {{.id = "image", .file = "white.bmp"}},
        .nodes = {{.id = "node", .kind = osbx_node_kind_t::sprite, .image = "image", .ops = {
            {.id = "move", .track = {.default_value = {120, 100}}}}}}};
    return {.id = song_id_t{1}, .revision = 1,
        .visual_assets = {{.id = asset_id_t{20}, .kind = asset_kind_t::video,
            .source_path = "three-colors.mkv", .width = 16, .height = 16}},
        .resources = {{.id = resource_id_t{10}, .name = "video", .asset = asset_id_t{20}, .roles = {resource_role_t::bga}}},
        .storyboards = {{.id = storyboard_id_t{11}, .name = "Gizmo media", .osbx = std::move(scene)}},
        .charts = {std::move(chart)}};
}
void write_gizmo_bitmap(const std::filesystem::path& path) {
    std::array<std::uint8_t, 62> bytes{
        0x42, 0x4d, 0x3e, 0, 0, 0, 0, 0, 0, 0, 0x36, 0, 0, 0, 0x28, 0,
        0, 0, 2, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0x18, 0, 0, 0,
        0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0, 0};
    std::ofstream output{path, std::ios::binary};
    REQUIRE(output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}
struct gizmo_media_binding_t {
    const std::uint8_t* image{};
    const std::uint8_t* video{};
    std::uint64_t image_content{}, video_content{};
};
struct gizmo_media_fixture_t {
    result_t<test::temporary_directory_t, std::string> directory{test::temporary_directory_t::from()};
    document_history_t history{document_history_t::from(gizmo_media_song())};
    editor_interaction_state_t interaction;
    visual::image_cache_t image_cache;
    task_scheduler_t image_scheduler{1, 2};
    visual::image_job_t images{image_cache, image_scheduler};
    visual::video_frame_cache_t video_cache;
    task_scheduler_t video_scheduler{1, 2};
    visual::video_frame_job_t videos{video_cache, video_scheduler};
    sys::player_preview_presenter_t presenter;
    ui::player_preview_view_t player;
    ui::storyboard_view_t storyboard;
    ~gizmo_media_fixture_t() { storyboard.canvas.reset_frame(); player.reset(); }
    void initialize() {
        REQUIRE(directory);
        write_gizmo_bitmap(directory->path() / "white.bmp");
        std::filesystem::copy_file(std::filesystem::path{WORMEDIT_FIXTURE_DIR} / "video" / "three-colors.mkv",
            directory->path() / "three-colors.mkv");
        storyboard.visible = true; storyboard.project = history.document.id; storyboard.revision = history.document.revision;
        storyboard.storyboards = {{.id = storyboard_id_t{11}, .name = "Gizmo media", .kind = ui::storyboard_kind_t::osbx}};
        storyboard.state.selection = {.storyboard = storyboard_id_t{11}, .nodes = {"node"}};
    }
    void populate() {
        presenter.populate(player, history, chart_id_t{7}, true, player_preview_skin_t::mania,
            24'000, 48'000, directory->path(), images, videos, &storyboard, 7, &interaction);
    }
    auto wait_ready() -> bool {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
        do {
            populate();
            if (storyboard.canvas.sprites.size() == 1 && storyboard.canvas.sprites[0].image_ready &&
                player.visual_layers.size() == 1 && player.visual_layers[0].image_ready &&
                !images.is_busy() && !videos.is_busy()) return true;
            std::this_thread::yield();
        } while (std::chrono::steady_clock::now() < deadline);
        return false;
    }
    auto binding() const -> gizmo_media_binding_t {
        return {.image = storyboard.canvas.sprites[0].rgba_pixels.data(), .video = player.visual_layers[0].rgba_pixels.data(),
            .image_content = storyboard.canvas.sprites[0].image_content_id, .video_content = player.visual_layers[0].image_content_id};
    }
    void check_binding(const gizmo_media_binding_t& expected, double x) const {
        REQUIRE(storyboard.canvas.sprites.size() == 1);
        REQUIRE(storyboard.canvas.sprites[0].image_ready);
        CHECK(storyboard.canvas.sprites[0].rgba_pixels.data() == expected.image);
        CHECK(storyboard.canvas.sprites[0].image_content_id == expected.image_content);
        CHECK(storyboard.canvas.sprites[0].transform[4] == Catch::Approx(x));
    }
    void check_video(const gizmo_media_binding_t& expected) const {
        REQUIRE(player.visual_layers.size() == 1);
        CHECK(player.visual_layers[0].image_ready);
        CHECK(player.visual_layers[0].rgba_pixels.data() == expected.video);
        CHECK(player.visual_layers[0].image_content_id == expected.video_content);
        CHECK_FALSE(images.is_busy()); CHECK_FALSE(videos.is_busy());
    }
    void stage(double x, bool begin) {
        osbx_edit_change_t change = osbx_set_default_t{
            .track = {.node = "node", .kind = osbx_track_kind_t::transform, .op = "move"}, .value = std::vector<double>{x, 100}};
        if (begin) REQUIRE(dispatch_editor_action(history, interaction, begin_osbx_edit_gesture_action_t{
            .edit = {.storyboard = storyboard_id_t{11}, .change = std::move(change)}}));
        else REQUIRE(dispatch_editor_action(history, interaction, update_osbx_edit_gesture_action_t{.change = std::move(change)}));
    }
    void check_document_and_media_unchanged(const std::string& before, std::size_t image_entries,
        std::size_t video_entries) const {
        const auto after = write_native_project(history.document); REQUIRE(after.value);
        CHECK(*after.value == before);
        CHECK(history.undo.empty());
        CHECK(image_cache.size() == image_entries);
        CHECK(video_cache.size() == video_entries);
    }
    void cancel_preview_and_check_lifetime() {
        const auto* pending = interaction.gesture<pending_osbx_edit_gesture_t>(); REQUIRE(pending);
        std::weak_ptr<const osbx_edit_preview_t> owner = pending->preview_owner;
        REQUIRE(dispatch_editor_action(history, interaction, cancel_editor_gesture_action_t{}));
        CHECK_FALSE(owner.expired()); // Borrowed canvas/source views have not reset yet.
        populate();
        CHECK(owner.expired());
    }
};
} // namespace

TEST_CASE("Storyboard geometry previews retain paused video and decoded images across revisions and cancel",
          "[sys][storyboard-gizmo][media][rebind]") {
    gizmo_media_fixture_t fixture; fixture.initialize(); REQUIRE(fixture.wait_ready());
    const auto binding = fixture.binding();
    const auto before = write_native_project(fixture.history.document); REQUIRE(before.value);
    const auto image_entries = fixture.image_cache.size(), video_entries = fixture.video_cache.size();
    for (const auto x : {160.0, 180.0, 200.0}) {
        fixture.stage(x, x == 160); fixture.populate();
        fixture.check_binding(binding, x); fixture.check_video(binding);
    }
    fixture.cancel_preview_and_check_lifetime();
    fixture.check_binding(binding, 120); fixture.check_video(binding);
    fixture.check_document_and_media_unchanged(*before.value, image_entries, video_entries);
}
#endif

#if defined(WORMEDIT_FRAME_ALLOCATION_HOOK)
#include "sys/app/frame_timing.h"
TEST_CASE("Warmed selected storyboard gizmo projection retains nested values without frame allocations",
          "[sys][storyboard-gizmo][allocation]") {
    auto song = gizmo_song();
    auto& node = song.storyboards[0].osbx->nodes[0];
    node.id = "selected-gizmo-node-with-long-stable-id";
    node.ops[0].id = "selected-translation-op-with-long-stable-id";
    node.ops[0].track.keys = {
        {.id = "a", .time_ms = 0, .value = {10, 20}, .easing = osbx_linear_easing_t{}},
        {.id = "b", .time_ms = 1000, .value = {1010, 20}}};
    gizmo_projection_t fixture;
    fixture.project(song, {.milliseconds = 1}, node.id);
    const auto targets = fixture.view.canvas.gizmo.targets(); REQUIRE(targets.size() == 2);
    const auto* storage = targets.data(); const auto* values = targets[0].value.data();
    const auto before = sys::frame_heap_allocation_snapshot();
    for (std::int64_t i = 1; i <= 100; ++i) {
        fixture.view.canvas.reset_frame();
        const storyboard_time_t time{.milliseconds = i};
        sys::populate_storyboard_canvas(fixture.view.canvas, fixture.scenes, storyboard_id_t{11}, time);
        fixture.view.inspector.time = time; fixture.view.inspector.key_time_ms = i;
        fixture.view.inspector.world = fixture.scenes.node_state(storyboard_id_t{11}, node.id)->matrix;
        sys::build_storyboard_gizmo_view(fixture.view, fixture.scenes);
    }
    const auto after = sys::frame_heap_allocation_snapshot();
    CHECK(after.count == before.count);
    CHECK(fixture.view.canvas.gizmo.targets().data() == storage);
    CHECK(fixture.view.canvas.gizmo.targets()[0].value.data() == values);
    CHECK(fixture.view.canvas.gizmo.targets()[0].value[0] == Catch::Approx(110));
}
#endif

TEST_CASE("Storyboard gizmo view copies own active target storage across frame refill",
          "[sys][storyboard-gizmo][lifetime]") {
    const auto song = gizmo_song();
    gizmo_projection_t fixture; fixture.project(song);
    const auto copy = fixture.view.canvas.gizmo;
    const auto* original = fixture.view.canvas.gizmo.targets().data();
    fixture.view.canvas.gizmo.reset_frame();
    fixture.view.canvas.gizmo.append_target().value = {42, 43};
    REQUIRE(copy.targets().size() == 2);
    CHECK(copy.targets().data() != original);
    CHECK(copy.targets()[0].value == std::vector<double>{10, 20});
    CHECK(fixture.view.canvas.gizmo.targets()[0].value == std::vector<double>{42, 43});
}
