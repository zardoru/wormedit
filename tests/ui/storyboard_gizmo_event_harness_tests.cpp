#include "nuklear_event_harness.h"
#include "../support/document_assertions.h"
#include "sys/app/application_command_state.h"
#include "sys/storyboard/storyboard_canvas_view.h"
#include "sys/storyboard/storyboard_gizmo_view.h"
#include "sys/storyboard/storyboard_inspector_view.h"
#include "sys/storyboard/storyboard_scene_presenter.h"
#include "ui/nuklear/audio/player_preview_texture.h"
#include "ui/nuklear/render/image_quad.h"
#include <wormedit/core/osbx_edit.h>
#include <wormedit/core/interaction_dispatch.h>
#include <wormedit/core/osbx_lua_link.h>
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
#include <wormedit/visual/image_job.h>
#endif
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <array>
#include <cmath>

namespace {
using namespace wormedit;
using namespace wormedit::ui;
auto gizmo_document() -> song_document_t {
    return {.id = song_id_t{1}, .revision = 1, .storyboards = {{.id = storyboard_id_t{11},
        .name = "Gizmo", .osbx = osbx_document_t{.images = {{.id = "image", .file = "sprite.png"}},
            .nodes = {{.id = "node", .kind = osbx_node_kind_t::sprite, .image = "image", .ops = {
                {.id = "scale", .kind = osbx_transform_kind_t::scale, .track = {.default_value = {1, 1}}},
                {.id = "rotate", .kind = osbx_transform_kind_t::rotate, .track = {.default_value = {0}}},
                {.id = "move", .kind = osbx_transform_kind_t::translate, .track = {.default_value = {320, 240}}}}}}}}}};
}
auto escape_event(bool down) -> SDL_Event {
    SDL_Event result{}; result.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    result.key.down = down; result.key.key = SDLK_ESCAPE; result.key.scancode = SDL_SCANCODE_ESCAPE;
    return result;
}
struct gizmo_workflow_t {
    nuklear_event_harness_t harness{1280, 900};
    document_history_t history{document_history_t::from(gizmo_document())};
    workspace_controller_t workspace;
    sys::application_command_state_t application;
    editor_interaction_state_t editor;
    notification_center_t notifications;
    notification_sink_t sink{notifications};
    sys::storyboard_scene_presenter_t scenes;
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
    visual::image_cache_t cache;
    task_scheduler_t scheduler{1, 2};
    visual::image_job_t images{cache, scheduler};
#endif
    bool playing{true};
    int pauses{};
    std::pair<float, float> point;
    std::vector<std::uint8_t> pixels = std::vector<std::uint8_t>(64 * 64 * 4, 255);

    auto dispatch(const storyboard_action_t& action, bool busy = false) -> bool {
        return application.project.storyboard.dispatch(action,
            {.history = history, .workspace = workspace, .notifications = sink}, application, &editor, busy);
    }
    auto current_preview() const -> std::shared_ptr<const osbx_edit_preview_t> {
        const auto* pending = editor.gesture<pending_osbx_edit_gesture_t>();
        const auto* stamp = editor.gesture_stamp<pending_osbx_edit_gesture_t>();
        if (pending && stamp && stamp->is_current(history.document)) return pending->preview_owner;
        return {};
    }
    void project() {
        auto& root = harness.view();
        root.project_id = history.document.id; root.document_revision = history.document.revision;
        auto& view = root.storyboard;
        view.project = history.document.id; view.revision = history.document.revision; view.session_generation = 7;
        view.storyboards = {{.id = storyboard_id_t{11}, .name = "Gizmo", .kind = storyboard_kind_t::osbx}};
        application.project.storyboard.refill(view, history.document, 7);
        view.canvas.reset_frame();
        scenes.begin(history.document, 7, {}
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
            , images
#endif
            , current_preview());
        sys::populate_storyboard_canvas(view.canvas, scenes, storyboard_id_t{11}, storyboard_time_t{});
        // A deterministic image-metadata fixture; real scene transforms and
        // selection outlines come from the production shared presenter.
        for (auto& sprite : view.canvas.sprites) {
            sprite.image_ready = true; sprite.image_width = sprite.image_height = 64;
            sprite.resource_id = resource_id_t{999}; sprite.image_content_id = 1; sprite.rgba_pixels = pixels;
        }
        sys::refill_storyboard_canvas_outlines(view, scenes.scene(storyboard_id_t{11}));
        sys::build_storyboard_inspector_view(view, history.document, {}, scenes.node_state(storyboard_id_t{11}, "node"),
            scenes.scene(storyboard_id_t{11}));
        sys::build_storyboard_gizmo_view(view, scenes);
        view.canvas.gizmo.can_begin = !editor.is_pending();
    }
    void initialize() {
        REQUIRE(harness.ready());
        const auto bound = workspace_controller_t::from(history); REQUIRE(bound.value);
        workspace = *bound.value; workspace.active_tab = workspace_tab_t::storyboard;
        harness.view().active_tab = WORM_UI_WORKSPACE_TAB_STORYBOARD;
        harness.view().compose.visible = false; harness.view().storyboard.visible = true;
        project();
        auto& view = harness.view().storyboard;
        REQUIRE(dispatch(storyboard_action_t::from(view, storyboard_action_kind_t::select, storyboard_id_t{11})));
        project();
        auto select = storyboard_action_t::from(view, storyboard_action_kind_t::select_nodes, storyboard_id_t{11});
        select.nodes = {"node"}; REQUIRE(dispatch(select));
        application.services.transport_preview_playing = [&] { return playing; };
        application.services.transport_play_pause = [&] { playing = false; ++pauses; return std::string{}; };
        project();
        REQUIRE(view.state.selection.nodes == std::vector<std::string>{"node"});
        (void)harness.frame();
        if (std::ranges::any_of(view.canvas.gizmo.targets(), [](const auto& target) {
                return target.kind == osbx_transform_kind_t::translate;
            })) point = harness.widget_center("Move move");
    }
    auto frame(std::span<const SDL_Event> events, SDL_Keymod modifiers = SDL_KMOD_NONE, bool busy = false)
        -> storyboard_action_t {
        const auto snapshot = harness.frame(events, modifiers);
        auto action = snapshot.get().storyboard;
        if (action.kind != storyboard_action_kind_t::none) REQUIRE(dispatch(action, busy));
        project(); return action;
    }
    void dispatch_frames(std::span<const workspace_action_snapshot_t> frames) {
        for (const auto& frame : frames)
            if (frame.get().storyboard.kind != storyboard_action_kind_t::none) REQUIRE(dispatch(frame.get().storyboard));
    }
    void begin() {
        (void)frame(std::array{test::mouse_motion(point.first, point.second),
            test::mouse_button(true, point.first, point.second)});
        const auto action = frame(std::array{test::mouse_motion(point.first + 20, point.second + 5)}, SDL_KMOD_SHIFT);
        CHECK(action.inspector.phase == storyboard_inspector_phase_t::begin);
        CHECK(editor.is_pending()); CHECK_FALSE(editor.chart);
    }
    void check_visible_proposal() {
        REQUIRE(harness.view().storyboard.canvas.sprites.size() == 1);
        CHECK(harness.view().storyboard.canvas.sprites[0].transform[4] > 320);
        CHECK(harness.view().storyboard.canvas.sprites[0].transform[5] == Catch::Approx(240));
        CHECK(history.undo.empty());
    }
    void check_applied_and_undo(const song_document_t& before) {
        CHECK_FALSE(editor.is_pending());
        REQUIRE(history.undo.size() == 1);
        CHECK(pauses == 1);
        REQUIRE(undo_document_command(history));
        CHECK(history.document.storyboards == before.storyboards);
    }
    void check_keyed_authored_translation(const osbx_track_t<>& track) const {
        REQUIRE(track.keys.size() == 3);
        CHECK(track.keys[1].time_ms == 0);
        CHECK(track.default_value == std::vector<double>{320, 240});
    }
    void check_authored_target(bool keyed) const {
        const auto& track = history.document.storyboards[0].osbx->nodes[0].ops[2].track;
        if (keyed) check_keyed_authored_translation(track);
        else {
            CHECK(track.keys.empty());
            CHECK(track.default_value[0] > 320);
        }
    }
    void check_idle_proposal() {
        const auto preview = current_preview();
        const auto action = frame({}, SDL_KMOD_SHIFT);
        CHECK(action.kind == storyboard_action_kind_t::none);
        CHECK(current_preview() == preview);
    }
};
void check_original(gizmo_workflow_t& fixture, const song_document_t& before) {
    CHECK_FALSE(fixture.editor.is_pending());
    CHECK(fixture.history.document.storyboards == before.storyboards);
    CHECK(fixture.history.undo.empty());
    CHECK(fixture.harness.view().storyboard.canvas.sprites[0].transform[4] == Catch::Approx(320));
}

namespace {
struct gizmo_pixel_renderer_t {
    std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface{nullptr, SDL_DestroySurface};
    std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer{nullptr, SDL_DestroyRenderer};
    gizmo_pixel_renderer_t() {
        surface.reset(SDL_CreateSurface(640, 480, SDL_PIXELFORMAT_RGBA32)); REQUIRE(surface);
        renderer.reset(SDL_CreateSoftwareRenderer(surface.get())); REQUIRE(renderer);
    }
    ~gizmo_pixel_renderer_t() { worm_ui_nuklear_shutdown_player_preview_textures(); }
};
class gizmo_commands_t {
    nk_user_font font_{.height = 13, .width = [](nk_handle, float, const char*, int length) {
        return static_cast<float>(length) * 8.0f;
    }};
    nk_context context_{};
  public:
    gizmo_commands_t() {
        REQUIRE(nk_init_default(&context_, &font_));
        REQUIRE(nk_begin(&context_, "Gizmo pixels", nk_rect(0, 0, 640, 480), NK_WINDOW_NO_SCROLLBAR));
    }
    gizmo_commands_t(const gizmo_commands_t&) = delete;
    auto operator=(const gizmo_commands_t&) -> gizmo_commands_t& = delete;
    ~gizmo_commands_t() { nk_end(&context_); nk_free(&context_); }
    auto canvas() -> nk_command_buffer* { return nk_window_get_canvas(&context_); }
};
void submit_gizmo_quad(SDL_Renderer* renderer, const WormUiNuklearImageQuad& quad) {
    std::array<SDL_Vertex, 4> vertices;
    for (std::size_t i = 0; i < vertices.size(); ++i) vertices[i] = {
        .position = {quad.points[i].x, quad.points[i].y},
        .color = {quad.color.r / 255.0f, quad.color.g / 255.0f, quad.color.b / 255.0f, quad.color.a / 255.0f},
        .tex_coord = {quad.uvs[i].x, quad.uvs[i].y}};
    constexpr std::array indices{0, 1, 2, 2, 3, 0};
    REQUIRE(SDL_RenderGeometry(renderer, static_cast<SDL_Texture*>(quad.image.handle.ptr),
        vertices.data(), 4, indices.data(), 6));
}
auto record_gizmo_quad(SDL_Renderer* renderer, const storyboard_view_t& view, gizmo_commands_t& commands)
    -> const WormUiNuklearImageQuad& {
    auto* canvas = commands.canvas();
    const auto drawn = worm_ui_nuklear_draw_player_preview_storyboard_sprites(canvas,
        nk_rect(0, 0, 640, 480), renderer, view.project, view.revision, view.canvas.sprites, false);
    REQUIRE(drawn.drew_image);
    REQUIRE(drawn.failure_count == 0);
    const auto* memory = static_cast<const nk_byte*>(nk_buffer_memory_const(canvas->base));
    const auto* last = reinterpret_cast<const nk_command*>(memory + canvas->last);
    REQUIRE(last->type == NK_COMMAND_CUSTOM);
    const auto* command = reinterpret_cast<const nk_command_custom*>(last);
    return *static_cast<const WormUiNuklearImageQuad*>(command->callback_data.ptr);
}
auto read_gizmo_pixel(SDL_Renderer* renderer, int x, int y) -> std::array<std::uint8_t, 4> {
    std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> capture{SDL_RenderReadPixels(renderer, nullptr), SDL_DestroySurface};
    REQUIRE(capture);
    std::array<std::uint8_t, 4> pixel{};
    REQUIRE(SDL_ReadSurfacePixel(capture.get(), x, y, &pixel[0], &pixel[1], &pixel[2], &pixel[3]));
    return pixel;
}
auto rendered_gizmo_pixel(SDL_Renderer* renderer, const storyboard_view_t& view, int x, int y)
    -> std::array<std::uint8_t, 4> {
    REQUIRE(SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255));
    REQUIRE(SDL_RenderClear(renderer));
    worm_ui_nuklear_begin_image_quad_frame(); worm_ui_nuklear_begin_player_preview_texture_frame();
    gizmo_commands_t commands;
    submit_gizmo_quad(renderer, record_gizmo_quad(renderer, view, commands));
    return read_gizmo_pixel(renderer, x, y);
}
} // namespace

TEST_CASE("Storyboard gizmo events move submitted sprite pixels before publishing the document",
          "[frontend-state][event-harness][storyboard-gizmo][runtime][pixels]") {
    gizmo_workflow_t fixture; fixture.initialize();
    gizmo_pixel_renderer_t output;
    const auto before = fixture.history.document;
    CHECK(rendered_gizmo_pixel(output.renderer.get(), fixture.harness.view().storyboard, 289, 240) ==
        std::array<std::uint8_t, 4>{255, 255, 255, 255});
    fixture.begin();
    CHECK(rendered_gizmo_pixel(output.renderer.get(), fixture.harness.view().storyboard, 289, 240) ==
        std::array<std::uint8_t, 4>{0, 0, 0, 255});
    test::check_same_document(fixture.history.document, before);
    CHECK(fixture.history.undo.empty());
    (void)fixture.frame(std::array{escape_event(true)});
    (void)fixture.frame(std::array{escape_event(false)});
}
} // namespace

TEST_CASE("Storyboard gizmo events visibly stage a no-chart Shift drag and publish one Undo",
          "[frontend-state][event-harness][storyboard-gizmo][runtime]") {
    const auto keyed = GENERATE(false, true);
    const auto group = GENERATE(false, true);
    gizmo_workflow_t fixture;
    if (group) {
        auto& scene = *fixture.history.document.storyboards[0].osbx;
        scene.nodes[0].kind = osbx_node_kind_t::group; scene.nodes[0].image.reset();
        scene.nodes.push_back({.id = "child", .kind = osbx_node_kind_t::sprite, .parent = "node", .image = "image"});
    }
    if (keyed) fixture.history.document.storyboards[0].osbx->nodes[0].ops[2].track.keys = {
        {.id = "before", .time_ms = -100, .value = {300, 240}, .easing = osbx_linear_easing_t{}},
        {.id = "after", .time_ms = 100, .value = {340, 240}}};
    fixture.initialize();
    const auto before = fixture.history.document;
    fixture.begin(); fixture.check_visible_proposal();
    fixture.check_idle_proposal();
    CHECK(fixture.history.document.storyboards == before.storyboards);
    const auto action = fixture.frame(std::array{test::mouse_motion(fixture.point.first + 35, fixture.point.second + 12)}, SDL_KMOD_SHIFT);
    CHECK(action.inspector.phase == storyboard_inspector_phase_t::revise);
    fixture.check_visible_proposal();
    const auto applied = fixture.frame(std::array{test::mouse_button(false, fixture.point.first + 35, fixture.point.second + 12)}, SDL_KMOD_SHIFT);
    CHECK(applied.inspector.phase == storyboard_inspector_phase_t::apply);
    fixture.check_authored_target(keyed);
    fixture.check_applied_and_undo(before);
}

TEST_CASE("Storyboard gizmo Escape tab focus modal and busy interruptions cancel the shared proposal",
          "[frontend-state][event-harness][storyboard-gizmo][runtime][cancel]") {
    enum class interruption_t { escape, tab, focus, modal, busy };
    const auto interruption = GENERATE(interruption_t::escape, interruption_t::tab, interruption_t::focus,
        interruption_t::modal, interruption_t::busy);
    gizmo_workflow_t fixture; fixture.initialize();
    const auto before = fixture.history.document;
    fixture.begin(); fixture.check_visible_proposal();
    if (interruption == interruption_t::escape) {
        (void)fixture.frame(std::array{escape_event(true)});
        const auto release = fixture.harness.frame(std::array{escape_event(false)});
        CHECK(release.get().shortcut_events.empty());
    } else if (interruption == interruption_t::focus) {
        SDL_Event event{}; event.type = SDL_EVENT_WINDOW_FOCUS_LOST;
        (void)fixture.frame(std::span{&event, 1});
    } else if (interruption == interruption_t::busy) {
        (void)fixture.frame(std::array{test::mouse_motion(fixture.point.first + 30, fixture.point.second)}, SDL_KMOD_NONE, true);
    } else {
        if (interruption == interruption_t::tab) {
            fixture.harness.view().storyboard.visible = false;
            fixture.harness.view().compose.visible = true;
            fixture.workspace.active_tab = workspace_tab_t::compose;
        } else fixture.harness.view().options.visible = true;
        (void)fixture.frame({});
    }
    check_original(fixture, before);
}

TEST_CASE("Storyboard gizmo stale release cancels its preview while preserving an independent edit",
          "[frontend-state][event-harness][storyboard-gizmo][runtime][stale]") {
    gizmo_workflow_t fixture; fixture.initialize();
    fixture.begin(); fixture.check_visible_proposal();
    REQUIRE(apply_osbx_edit(fixture.history, {.storyboard = storyboard_id_t{11},
        .change = osbx_set_order_t{.node = "node", .order = 77}}).applied);
    const auto edited = fixture.history.document;
    fixture.project();
    (void)fixture.frame(std::array{test::mouse_button(false, fixture.point.first + 30, fixture.point.second)});
    CHECK_FALSE(fixture.editor.is_pending());
    CHECK(fixture.history.document.storyboards == edited.storyboards);
    CHECK(fixture.history.undo.size() == 1);
    CHECK(fixture.harness.view().storyboard.canvas.sprites[0].transform[4] == Catch::Approx(320));
}

TEST_CASE("Storyboard missing gizmo op requires an explicit append action before dragging",
          "[frontend-state][event-harness][storyboard-gizmo][runtime]") {
    gizmo_workflow_t fixture;
    auto& ops = fixture.history.document.storyboards[0].osbx->nodes[0].ops;
    ops.erase(ops.begin() + 1);
    fixture.initialize();
    CHECK(fixture.history.undo.empty());
    CHECK(fixture.harness.view().storyboard.canvas.gizmo.missing == std::vector{osbx_transform_kind_t::rotate});
    const auto append = fixture.harness.drawn_text_center("Append Rotate (degrees) op"); REQUIRE(append);
    const auto frames = test::click_at(fixture.harness, append->first, append->second);
    fixture.dispatch_frames(frames);
    CHECK(fixture.history.document.storyboards[0].osbx->nodes[0].ops.back().kind == osbx_transform_kind_t::rotate);
    CHECK(fixture.history.undo.size() == 1);
}

namespace {
auto begin_handle_drag(gizmo_workflow_t& fixture, std::string_view tooltip) -> storyboard_action_t {
    fixture.point = fixture.harness.widget_center(tooltip);
    (void)fixture.frame(std::array{test::mouse_motion(fixture.point.first, fixture.point.second),
        test::mouse_button(true, fixture.point.first, fixture.point.second)});
    return fixture.frame(std::array{test::mouse_motion(fixture.point.first + 20, fixture.point.second + 5)}, SDL_KMOD_SHIFT);
}
void check_rotation_constraint(const std::vector<double>& value) {
    REQUIRE(value.size() == 1);
    CHECK(std::remainder(value[0], 15) == Catch::Approx(0).margin(1e-10));
}
void check_two_axis_constraint(const std::vector<double>& value, osbx_transform_kind_t kind) {
    REQUIRE(value.size() == 2);
    if (kind == osbx_transform_kind_t::scale) CHECK(value[0] == Catch::Approx(value[1]));
    else CHECK(value[1] == Catch::Approx(0));
}
void check_constrained_handle_value(const storyboard_action_t& action, osbx_transform_kind_t kind) {
    if (kind == osbx_transform_kind_t::rotate) check_rotation_constraint(action.inspector.value);
    else check_two_axis_constraint(action.inspector.value, kind);
}

} // namespace

TEST_CASE("Storyboard rotate scale and pivot handles route constrained gestures to their specific op",
          "[frontend-state][event-harness][storyboard-gizmo][runtime][modifiers]") {
    const auto kind = GENERATE(osbx_transform_kind_t::rotate, osbx_transform_kind_t::scale, osbx_transform_kind_t::pivot);
    gizmo_workflow_t fixture;
    if (kind == osbx_transform_kind_t::pivot) fixture.history.document.storyboards[0].osbx->nodes[0].ops.insert(
        fixture.history.document.storyboards[0].osbx->nodes[0].ops.begin(), {.id = "pivot", .kind = kind,
            .children = {{.id = "pivot-rotate", .kind = osbx_transform_kind_t::rotate, .track = {.default_value = {45}}}}});
    fixture.initialize();
    const auto op = kind == osbx_transform_kind_t::rotate ? "rotate" : kind == osbx_transform_kind_t::scale ? "scale" : "pivot";
    const auto tooltip = kind == osbx_transform_kind_t::rotate ? "Rotate rotate" :
        kind == osbx_transform_kind_t::scale ? "Scale scale corner 2" : "Pivot pivot";
    const auto begin = begin_handle_drag(fixture, tooltip);
    CHECK(begin.inspector.phase == storyboard_inspector_phase_t::begin);
    CHECK(begin.inspector.op == op);
    check_constrained_handle_value(begin, kind);
    (void)fixture.frame(std::array{test::mouse_button(false, fixture.point.first + 20, fixture.point.second + 5)}, SDL_KMOD_SHIFT);
    CHECK_FALSE(fixture.editor.is_pending());
    CHECK(fixture.history.undo.size() == 1);
}

TEST_CASE("Storyboard gizmo cannot take over an existing gesture on the same op",
          "[frontend-state][event-harness][storyboard-gizmo][runtime][busy]") {
    gizmo_workflow_t fixture; fixture.initialize();
    REQUIRE(dispatch_editor_action(fixture.history, fixture.editor, begin_osbx_edit_gesture_action_t{
        .edit = {.storyboard = storyboard_id_t{11}, .change = osbx_set_default_t{
            .track = {.node = "node", .kind = osbx_track_kind_t::transform, .op = "move"},
            .value = std::vector<double>{330, 250}}}}));
    fixture.project(); (void)fixture.harness.frame();
    const auto owner = fixture.current_preview();
    const auto point = fixture.harness.widget_center("Move move");
    (void)fixture.frame(std::array{test::mouse_motion(point.first, point.second), test::mouse_button(true, point.first, point.second)});
    (void)fixture.frame(std::array{test::mouse_motion(point.first + 20, point.second + 5)});
    (void)fixture.frame(std::array{test::mouse_button(false, point.first + 20, point.second + 5)});
    CHECK(fixture.current_preview() == owner);
    CHECK(fixture.editor.is_pending());
    CHECK(fixture.history.undo.empty());
    REQUIRE(dispatch_editor_action(fixture.history, fixture.editor, cancel_editor_gesture_action_t{}));
    fixture.project();
}
