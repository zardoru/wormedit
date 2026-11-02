#include "sys/app/application_command_state.h"
#include "sys/audio/player_preview_presenter.h"
#include "sys/storyboard/storyboard_canvas_view.h"
#include "sys/storyboard/storyboard_hierarchy_view.h"
#include "sys/storyboard/storyboard_scene_presenter.h"
#include <wormedit/core/lane_profiles.h>
#include <wormedit/core/storyboard.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <algorithm>
#include <array>
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
#include <wormedit/visual/image_job.h>
#include "../support/temporary_directory.h"
#include <chrono>
#include <fstream>
#include <thread>
#endif
#if defined(WORMEDIT_HAS_VIDEO_DECODER)
#include <wormedit/visual/video_job.h>
#endif

using namespace wormedit;
namespace {
auto scenario_song() -> song_document_t {
    const auto osb = parse_osb(
        "Sprite,Background,Centre,static.bmp,0,0\n F,0,0,1000,1,1\n"
        "Sprite,Pass,Centre,static.bmp,0,0\n F,0,0,1000,1,1\n"
        "Sprite,Fail,Centre,static.bmp,0,0\n F,0,0,1000,1,1\n"
        "Animation,Foreground,Centre,anim.bmp,0,0,2,100,LoopForever\n F,0,0,1000,1,1\n");
    REQUIRE(osb.value);
    osbx_document_t scene{.images = {{.id = "image", .file = "static.bmp"}}, .nodes = {
        {.id = "base", .kind = osbx_node_kind_t::sprite, .image = "image"},
        {.id = "pass", .kind = osbx_node_kind_t::sprite, .layer = osbx_layer_t::pass, .image = "image"},
        {.id = "fail", .kind = osbx_node_kind_t::sprite, .layer = osbx_layer_t::fail, .image = "image"}}};
    chart_document_t chart{.id = chart_id_t{7}, .divisions_per_standard_measure = 192,
        .measures = {{.id = measure_id_t{1}}},
        .lane_layouts = {{.id = object_id_t{1}, .position = {.measure = measure_id_t{1}},
            .layout = make_bms_5k_profile().layout}},
        .bpms = {{.id = object_id_t{2}, .position = {.measure = measure_id_t{1}}, .beats_per_minute = {120, 1}}}};
    return {.id = song_id_t{1}, .revision = 1,
        .visual_assets = {{.id = asset_id_t{20}, .kind = asset_kind_t::image,
            .source_path = "static.bmp", .width = 2, .height = 1}},
        .resources = {{.id = resource_id_t{21}, .name = "static", .asset = asset_id_t{20},
            .roles = {resource_role_t::bga}}},
        .storyboards = {{.id = storyboard_id_t{11}, .name = "OSBX", .osbx = std::move(scene)},
                        {.id = storyboard_id_t{12}, .name = "OSB", .model = *osb.value}}, .charts = {std::move(chart)}};
}
struct scenario_state_fixture_t {
    document_history_t history{document_history_t::from(scenario_song())};
    workspace_controller_t workspace;
    sys::application_command_state_t application;
    notification_center_t notifications{};
    notification_sink_t sink{notifications};
    ui::storyboard_view_t root;
    std::uint64_t session{7};
    unsigned transport_calls{};
    void initialize(bool overlay_first = false) {
        const auto bound = workspace_controller_t::from(history); REQUIRE(bound.value);
        workspace = *bound.value;
        workspace.active_tab = overlay_first ? workspace_tab_t::compose : workspace_tab_t::storyboard;
        application.project.compose_view.player_preview_visible = overlay_first;
        application.services.transport_play_pause = [this] { ++transport_calls; return std::string{}; };
        root.visible = !overlay_first;
        project();
    }
    void project() {
        root.storyboards = {{.id = storyboard_id_t{11}, .name = "OSBX", .kind = ui::storyboard_kind_t::osbx},
            {.id = storyboard_id_t{12}, .name = "OSB", .kind = ui::storyboard_kind_t::osb}};
        application.project.storyboard.refill(root, history.document, session);
    }
    auto dispatch(const ui::storyboard_action_t& action, bool busy = false) -> bool {
        return application.project.storyboard.dispatch(action,
            {.history = history, .workspace = workspace, .notifications = sink}, application, nullptr, busy);
    }
    void choose(storyboard_conditional_layer_t choice) {
        auto action = ui::storyboard_action_t::from(root, ui::storyboard_action_kind_t::set_conditional_layer);
        action.conditional_layer = choice;
        REQUIRE(dispatch(action));
        project();
    }
    void select(storyboard_id_t board) {
        REQUIRE(dispatch(ui::storyboard_action_t::from(root, ui::storyboard_action_kind_t::select, board)));
        project();
    }
};
struct scenario_media_t {
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
    visual::image_cache_t cache;
    task_scheduler_t scheduler{1, 2};
    visual::image_job_t images{cache, scheduler};
#endif
#if defined(WORMEDIT_HAS_VIDEO_DECODER)
    visual::video_frame_cache_t video_cache;
    task_scheduler_t video_scheduler{1, 2};
    visual::video_frame_job_t videos{video_cache, video_scheduler};
#endif
    auto begin(sys::storyboard_scene_presenter_t& scenes, const song_document_t& song) -> bool {
        return scenes.begin(song, 7, {}
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
            , images
#endif
        );
    }
    void populate(sys::player_preview_presenter_t& presenter, ui::player_preview_view_t& player,
        scenario_state_fixture_t& flow, const std::filesystem::path& directory = {}) {
        presenter.populate(player, flow.history, chart_id_t{7}, true, player_preview_skin_t::mania,
            4801, 48000, directory
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
            , images
#endif
#if defined(WORMEDIT_HAS_VIDEO_DECODER)
            , videos
#endif
            , &flow.root, flow.session);
    }
};
void check_visible_layers(std::span<const ui::player_preview_storyboard_sprite_view_t> sprites,
    storyboard_conditional_layer_t choice) {
    CHECK(std::ranges::all_of(sprites, [choice](const auto& sprite) {
        return sprite.layer_order != 1 || choice == storyboard_conditional_layer_t::fail;
    }));
    CHECK(std::ranges::all_of(sprites, [choice](const auto& sprite) {
        return sprite.layer_order != 2 || choice == storyboard_conditional_layer_t::pass;
    }));
}
void check_population(const ui::storyboard_canvas_view_t& canvas, const ui::player_preview_view_t& player,
    storyboard_id_t board, storyboard_conditional_layer_t choice) {
    const auto conditional = choice == storyboard_conditional_layer_t::neither ? 0U : 1U;
    CHECK(canvas.sprites.size() == (board == storyboard_id_t{11} ? 1U : 2U) + conditional);
    CHECK(player.storyboard_sprites.size() == 3U + 2U * conditional);
}
void check_retained_media_identity(std::span<const ui::player_preview_storyboard_sprite_view_t> first,
    std::span<const ui::player_preview_storyboard_sprite_view_t> second) {
    REQUIRE(first.size() == second.size());
    CHECK(first.back().resource_id == second.back().resource_id);
    CHECK(first.front().resource_id == second.front().resource_id);
}
void check_unchanged_project(const scenario_state_fixture_t& flow) {
    CHECK(flow.history.document.revision == 1);
    CHECK(flow.history.undo.empty());
    CHECK(flow.transport_calls == 0);
}
} // namespace

TEST_CASE("Storyboard scenario defaults to Pass and survives board and tab selection",
          "[sys][storyboard-scenario]") {
    scenario_state_fixture_t flow; flow.initialize();
    CHECK(flow.root.state.conditional_layer == storyboard_conditional_layer_t::pass);
    flow.choose(storyboard_conditional_layer_t::fail); flow.select(storyboard_id_t{12});
    flow.workspace.active_tab = workspace_tab_t::compose; flow.root.visible = false; flow.project();
    CHECK(flow.root.state.conditional_layer == storyboard_conditional_layer_t::fail);
    check_unchanged_project(flow);
}
TEST_CASE("Storyboard scenario resets through the existing project and session lifecycle",
          "[sys][storyboard-scenario]") {
    const bool new_project = GENERATE(false, true);
    scenario_state_fixture_t flow; flow.initialize(); flow.choose(storyboard_conditional_layer_t::neither);
    if (new_project) flow.history.document.id = song_id_t{2};
    else ++flow.session;
    flow.project();
    CHECK(flow.root.state.conditional_layer == storyboard_conditional_layer_t::pass);
}
TEST_CASE("Storyboard scenario changes preserve independent saved video export policy",
          "[sys][storyboard-scenario]") {
    scenario_state_fixture_t flow; flow.initialize();
    flow.root.video_export.settings.layers = storyboard_conditional_layer_t::neither;
    const auto settings = flow.root.video_export.settings;
    flow.choose(storyboard_conditional_layer_t::fail);
    CHECK(flow.root.video_export.settings == settings);
    check_unchanged_project(flow);
}
TEST_CASE("Storyboard scenario overlay action projects current state while the tab is hidden and unselected",
          "[sys][storyboard-scenario]") {
    scenario_state_fixture_t flow; flow.initialize(true);
    flow.choose(storyboard_conditional_layer_t::neither);
    // Simulate the next frame's empty frontend snapshot, then use the real state owner.
    flow.root = {}; flow.project();
    CHECK(flow.root.state.conditional_layer == storyboard_conditional_layer_t::neither);
    CHECK_FALSE(flow.root.state.selection.storyboard);
    scenario_media_t media; sys::player_preview_presenter_t presenter; ui::player_preview_view_t player;
    media.populate(presenter, player, flow);
    REQUIRE(player.scene_available);
    CHECK(player.storyboard_sprites.size() == 3);
    check_visible_layers(player.storyboard_sprites, storyboard_conditional_layer_t::neither);
}
TEST_CASE("Storyboard scenario uses one exact fractional frame for OSB OSBX canvas and auxiliary player",
          "[sys][storyboard-scenario]") {
    const auto choice = GENERATE(storyboard_conditional_layer_t::pass, storyboard_conditional_layer_t::fail,
        storyboard_conditional_layer_t::neither);
    const auto board = GENERATE(storyboard_id_t{11}, storyboard_id_t{12});
    scenario_state_fixture_t flow; flow.initialize(); flow.select(board); flow.choose(choice);
    scenario_media_t media; sys::player_preview_presenter_t presenter; ui::player_preview_view_t player;
    media.populate(presenter, player, flow);
    REQUIRE(flow.root.canvas.available);
    REQUIRE(player.scene_available);
    CHECK(flow.root.canvas.time_ms == Catch::Approx(100.0 + 1.0 / 48));
    CHECK(player.sample_frame == 4801);
    check_population(flow.root.canvas, player, board, choice);
    check_visible_layers(flow.root.canvas.sprites, choice);
    check_visible_layers(player.storyboard_sprites, choice);
}
TEST_CASE("Storyboard scenario hierarchy visibility is an overlay over unchanged topology and selection",
          "[sys][storyboard-scenario]") {
    scenario_state_fixture_t flow; flow.initialize(); flow.select(storyboard_id_t{11});
    flow.root.state.selection.nodes = {"pass"};
    sys::storyboard_hierarchy_projection_t hierarchy;
    hierarchy.refill(flow.root, flow.history.document);
    const auto builds = hierarchy.build_count();
    flow.root.state.conditional_layer = storyboard_conditional_layer_t::fail;
    hierarchy.refill(flow.root, flow.history.document);
    CHECK(hierarchy.build_count() == builds);
    CHECK_FALSE(flow.root.hierarchy.layers[2].shown_in_state);
    CHECK(flow.root.hierarchy.layers[1].shown_in_state);
    REQUIRE(flow.root.hierarchy.layers[2].rows.size() == 1);
    CHECK(flow.root.hierarchy.layers[2].rows[0].selected);
    CHECK(flow.root.hierarchy.layers[2].rows[0].editable);
}
TEST_CASE("Storyboard scenario switches preserve the Animation frame path catalog and static resource identity",
          "[sys][storyboard-scenario][animation]") {
    const auto song = scenario_song(); scenario_media_t media; sys::storyboard_scene_presenter_t scenes;
    REQUIRE(media.begin(scenes, song));
    std::vector<ui::player_preview_storyboard_sprite_view_t> first, second;
    const storyboard_time_t time{.milliseconds = 100, .remainder = 1, .denominator = 48};
    REQUIRE(scenes.refill(first, storyboard_id_t{12}, time, 100, storyboard_conditional_layer_t::pass).value);
    CHECK_FALSE(media.begin(scenes, song));
    REQUIRE(scenes.refill(second, storyboard_id_t{12}, time, 100, storyboard_conditional_layer_t::fail).value);
    check_retained_media_identity(first, second);
}

#if defined(WORMEDIT_HAS_IMAGE_DECODER)
namespace {
void write_scenario_image(const std::filesystem::path& path) {
    // Same small BMP fixture form used by the existing preview delivery tests.
    constexpr std::array<std::uint8_t, 62> bytes{
        0x42, 0x4d, 0x3e, 0, 0, 0, 0, 0, 0, 0, 0x36, 0, 0, 0, 0x28, 0,
        0, 0, 2, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0x18, 0, 0, 0,
        0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0xff, 0, 0xff, 0, 0, 0};
    std::ofstream output{path, std::ios::binary};
    REQUIRE(output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}
auto wait_scenario_images(scenario_media_t& media, sys::player_preview_presenter_t& presenter,
    ui::player_preview_view_t& player, scenario_state_fixture_t& flow, const std::filesystem::path& directory) -> bool {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
    do {
        media.populate(presenter, player, flow, directory);
        if (player.scene_available && !player.storyboard_sprites.empty() &&
            std::ranges::all_of(player.storyboard_sprites, [](const auto& sprite) { return sprite.image_ready; })) return true;
        std::this_thread::yield();
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}
void check_retained_rgba(const ui::player_preview_view_t& player, const std::uint8_t* base,
    const std::uint8_t* animation) {
    REQUIRE(player.storyboard_sprites.size() == 3);
    CHECK(player.storyboard_sprites.front().rgba_pixels.data() == base);
    CHECK(player.storyboard_sprites.back().rgba_pixels.data() == animation);
}
} // namespace
TEST_CASE("Storyboard scenario changes retain decoded static and exact Animation frame cache payloads",
          "[sys][storyboard-scenario][animation][image]") {
    auto directory = test::temporary_directory_t::from(); REQUIRE(directory);
    write_scenario_image(directory->path() / "static.bmp");
    write_scenario_image(directory->path() / "anim0.bmp");
    write_scenario_image(directory->path() / "anim1.bmp");
    scenario_state_fixture_t flow; flow.initialize();
    scenario_media_t media; sys::player_preview_presenter_t presenter; ui::player_preview_view_t player;
    REQUIRE(wait_scenario_images(media, presenter, player, flow, directory->path()));
    const auto* base = player.storyboard_sprites.front().rgba_pixels.data();
    const auto* animation = player.storyboard_sprites.back().rgba_pixels.data();
    const auto cached = media.cache.size();
    flow.choose(storyboard_conditional_layer_t::fail);
    REQUIRE(wait_scenario_images(media, presenter, player, flow, directory->path()));
    flow.choose(storyboard_conditional_layer_t::neither);
    REQUIRE(wait_scenario_images(media, presenter, player, flow, directory->path()));
    check_retained_rgba(player, base, animation);
    CHECK(media.cache.size() == cached);
}
#endif
