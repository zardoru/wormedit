#include "sys/storyboard/storyboard_assets_view.h"
#include "sys/audio/player_preview_presenter.h"
#include "sys/storyboard/storyboard_scene_presenter.h"
#include "sys/storyboard/storyboard_canvas_view.h"
#include "sys/storyboard/storyboard_hierarchy_view.h"
#include "sys/storyboard/storyboard_inspector_view.h"
#include "sys/storyboard/storyboard_timeline_view.h"
#include "sys/storyboard/storyboard_gizmo_view.h"

#include <wormedit/core/bga_preview.h>
#include <wormedit/core/command.h>
#include <wormedit/core/keyed_cache.h>
#include <wormedit/core/osbx_lua_link.h>
#include <wormedit/core/storyboard_preview.h>
#include <wormedit/core/osbx_evaluator.h>
#include <wormedit/core/osbx_edit.h>
#include <unordered_map>
#include <unordered_set>

#if defined(WORMEDIT_HAS_IMAGE_DECODER)
#include <wormedit/visual/image_job.h>
#endif
#if defined(WORMEDIT_HAS_VIDEO_DECODER)
#include <wormedit/visual/video_job.h>
#endif
#include <wormedit/visual/visual_asset.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <functional>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

namespace wormedit::sys {
namespace {

using wormedit::ui::maximum_preview_notes;
using wormedit::ui::maximum_preview_storyboard_instances;

struct player_preview_cache_key_t {
    chart_stamp_t stamp;
    std::uint32_t sample_rate{};
    std::uint64_t prepared_generation{};
    auto operator==(const player_preview_cache_key_t&) const -> bool = default;
};

} // namespace

struct player_preview_presenter_t::impl_t {
    keyed_cache_t<player_preview_cache_key_t, std::optional<player_preview_index_t>> index;
    auto prepare_index(const document_history_t& history, const chart_document_t& chart,
                       std::uint32_t sample_rate
#if defined(WORMEDIT_HAS_VIDEO_DECODER)
                       , visual::video_frame_job_t& video_job
#endif
    ) -> bool;
    auto build_index(const chart_document_t& chart, const player_preview_cache_key_t& key,
#if defined(WORMEDIT_HAS_VIDEO_DECODER)
                     visual::video_frame_job_t& video_job,
#endif
                     std::optional<player_preview_index_t>& value) -> void;
    std::optional<preview_schedule_t> visual_schedule;
    std::optional<bga_preview_queue_t> visual_queue;
    std::optional<bga_preview_token_t> visual_token;
    storyboard_scene_presenter_t scenes;
    storyboard_hierarchy_projection_t hierarchy;
    storyboard_assets_projection_t assets;
    storyboard_timeline_projection_t timeline;
    void refill_panels(ui::storyboard_view_t&, const song_document_t&,
        const result_t<storyboard_time_t, failure_code_t>&, const storyboard_timeline_timing_t&,
        const editor_interaction_state_t*);
#if defined(WORMEDIT_HAS_VIDEO_DECODER)
    std::optional<video_preview_clock_t> requested_video_clock;
    std::optional<object_id_t> failed_video_event;
    std::string video_diagnostic_message;
    std::shared_ptr<const visual::video_job_frame_t> decoded_video;
    void consume_video_completion(visual::video_frame_job_t& job, song_id_t current_project);
    std::uint64_t decoded_video_content_id{};
#endif
    std::string status;
};

auto player_preview_presenter_t::impl_t::prepare_index(
    const document_history_t& history, const chart_document_t& chart, std::uint32_t sample_rate
#if defined(WORMEDIT_HAS_VIDEO_DECODER)
    , visual::video_frame_job_t& video_job
#endif
) -> bool {
    const player_preview_cache_key_t key{
        .stamp = chart_stamp_t::from(history.document, chart.id), .sample_rate = sample_rate,
        .prepared_generation = osbx_lua_prepared_scope_t::generation()};
    const auto previous_builds = index.build_count();
    (void)index.get_or_build(key, std::bind_front(&impl_t::build_index, this,
        std::cref(chart), std::cref(key)
#if defined(WORMEDIT_HAS_VIDEO_DECODER)
        , std::ref(video_job)
#endif
    ));
    // Existing cache hits return ready even when the retained status is an error;
    // frame preparation publishes that status without rebuilding the failed index.
    return index.build_count() == previous_builds || status.empty();
}

auto player_preview_presenter_t::impl_t::build_index(
    const chart_document_t& chart, const player_preview_cache_key_t& key,
#if defined(WORMEDIT_HAS_VIDEO_DECODER)
    visual::video_frame_job_t& video_job,
#endif
    std::optional<player_preview_index_t>& value) -> void {
    value.reset();
    visual_schedule.reset();
    visual_queue.reset();
    visual_token.reset();
    status.clear();
#if defined(WORMEDIT_HAS_VIDEO_DECODER)
    requested_video_clock.reset();
    failed_video_event.reset();
    video_diagnostic_message.clear();
    decoded_video.reset();
    decoded_video_content_id = 0;
    video_job.cancel();
#endif
    auto built = player_preview_index_t::from(chart, key.stamp.document.revision);
    if (!built.value || !built.diagnostics.empty()) {
        status = built.diagnostics.empty()
                     ? "The player-preview visual schedule could not be compiled."
                     : built.diagnostics.front().message;
        return;
    }
    value = std::move(*built.value);
    auto schedule = preview_schedule_t::from(
        chart, value->timing, {.sample_rate = key.sample_rate, .include_metronome = false});
    if (!schedule.value) {
        status = schedule.diagnostics.empty()
                     ? "The visual preview schedule could not be compiled."
                     : schedule.diagnostics.front().message;
        return;
    }
    visual_schedule = std::move(*schedule.value);
    visual_queue.emplace();
    const auto started = visual_queue->begin(*visual_schedule);
    if (!started) {
        status = "The visual preview schedule could not be started.";
        return;
    }
    visual_token = *started;
}

#if defined(WORMEDIT_HAS_VIDEO_DECODER)
void player_preview_presenter_t::impl_t::consume_video_completion(
    visual::video_frame_job_t& job, song_id_t current_project) {
    auto completion = job.poll();
    if (!completion) return;
    const auto& identity = *completion ? completion->value().identity : completion->error().identity;
    const auto revision = index.key() ? index.key()->stamp.document.revision : 0;
    if (identity.project != current_project || identity.document_revision != revision) return;
    if (*completion && completion->value().payload) {
        const auto& decoded = *completion->value().payload;
        if (decoded) {
            // The outcome control block keeps its immutable success value alive.
            decoded_video = std::shared_ptr<const visual::video_job_frame_t>{
                std::move(completion->value().payload), &*decoded};
            decoded_video_content_id = completion->value().generation;
            failed_video_event.reset();
            video_diagnostic_message.clear();
        } else {
            failed_video_event = decoded.error().context.clock.event;
            video_diagnostic_message = decoded.error().cause.message.empty()
                                           ? "Video frame decoding failed."
                                           : decoded.error().cause.message;
        }
        return;
    }
    if (!requested_video_clock) return;
    failed_video_event = requested_video_clock->event;
    video_diagnostic_message = !*completion && !completion->error().cause.message.empty()
                                  ? completion->error().cause.message
                                  : "The video frame task failed before producing a result.";
}
#endif

namespace {

void append_scene_geometry(const player_preview_scene_view_t& scene,
                           std::vector<wormedit::ui::player_preview_lane_view_t>& lanes,
                           std::vector<wormedit::ui::player_preview_receptor_view_t>& receptors,
                           std::vector<wormedit::ui::player_preview_hold_view_t>& holds,
                           std::vector<wormedit::ui::player_preview_note_view_t>& notes) {
    for (const auto& lane : scene.lanes) {
        lanes.push_back({.lane_id = lane.lane,
                         .role = lane.role,
                         .glyph = lane.glyph,
                         .display_order = lane.display_order,
                         .bounds = lane.bounds});
    }
    for (const auto& receptor : scene.receptors) {
        receptors.push_back({.lane_id = receptor.lane,
                             .glyph = receptor.glyph,
                             .bounds = receptor.bounds});
    }
    for (const auto& hold : scene.hold_bodies) {
        holds.push_back(
            {.note_id = hold.note,
             .lane_id = hold.lane,
             .semantic_color_visible = hold.semantic_color.has_value(),
             .semantic_red =
                 static_cast<std::uint8_t>(hold.semantic_color ? hold.semantic_color->red : 0),
             .semantic_green =
                 static_cast<std::uint8_t>(hold.semantic_color ? hold.semantic_color->green : 0),
             .semantic_blue =
                 static_cast<std::uint8_t>(hold.semantic_color ? hold.semantic_color->blue : 0),
             .semantic_alpha =
                 static_cast<std::uint8_t>(hold.semantic_color ? hold.semantic_color->alpha : 0),
             .bounds = hold.bounds});
    }
    for (const auto& note : scene.note_endpoints) {
        notes.push_back(
            {.note_id = note.note,
             .lane_id = note.lane,
             .kind = note.kind,
             .glyph = note.glyph,
             .tail = note.tail,
             .semantic_color_visible = note.semantic_color.has_value(),
             .semantic_red =
                 static_cast<std::uint8_t>(note.semantic_color ? note.semantic_color->red : 0),
             .semantic_green =
                 static_cast<std::uint8_t>(note.semantic_color ? note.semantic_color->green : 0),
             .semantic_blue =
                 static_cast<std::uint8_t>(note.semantic_color ? note.semantic_color->blue : 0),
             .semantic_alpha =
                 static_cast<std::uint8_t>(note.semantic_color ? note.semantic_color->alpha : 0),
             .bounds = note.bounds});
    }
}

template <typename State>
struct player_preview_projection_context_t {
    State& state;
    wormedit::ui::player_preview_view_t& view;
    const document_history_t& history;
    const chart_document_t& chart;
    player_preview_skin_t skin;
    std::uint64_t sample_frame;
    std::uint32_t sample_rate;
    const std::filesystem::path& project_directory;
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
    visual::image_job_t& image_job;
#endif
#if defined(WORMEDIT_HAS_VIDEO_DECODER)
    visual::video_frame_job_t& video_job;
#endif
};

template <typename Context>
void fail_player_preview(const Context& context, std::string message) {
    context.view.status_text = std::move(message);
}

template <typename Context>
auto advance_player_preview(const Context& context) -> bga_preview_advance_outcome_t {
    auto& state = context.state;
    if (context.sample_frame == std::numeric_limits<std::uint64_t>::max()) {
        fail_player_preview(context, "The player-preview frame exceeds the visual cursor range.");
        return std::unexpected(failure_code_t::not_started);
    }
    auto visual = state.visual_queue->advance_events(*state.visual_token, context.sample_frame + 1);
    if (visual == std::unexpected(failure_code_t::invalid_state)) {
        const auto sought = state.visual_queue->reposition(*state.visual_token, context.sample_frame);
        if (sought) {
            state.visual_token = *sought;
            visual = state.visual_queue->advance_events(*state.visual_token, context.sample_frame + 1);
        }
    }
    if (!visual) {
        fail_player_preview(context, "The visual preview cursor could not resolve this frame.");
        return visual;
    }
    return visual;
}

struct player_preview_frame_t {
    bga_layer_state_t visual;
};

template <typename Context>
auto prepare_player_preview_frame(const Context& context)
    -> std::optional<player_preview_frame_t> {
    auto& state = context.state;
    if (!state.prepare_index(context.history, context.chart, context.sample_rate
#if defined(WORMEDIT_HAS_VIDEO_DECODER)
                             , context.video_job
#endif
    )) {
        fail_player_preview(context, state.status);
        return std::nullopt;
    }
    if (!state.index.value() || !state.visual_schedule || !state.visual_queue || !state.visual_token ||
        !state.status.empty()) {
        fail_player_preview(context, state.status);
        return std::nullopt;
    }
    auto visual = advance_player_preview(context);
    if (!visual) return std::nullopt;
    // The queue accepted this token; no queue operation intervenes.
    auto layers = state.visual_queue->current_layers(*state.visual_token);
    return player_preview_frame_t{.visual = std::move(*layers)};
}

template <typename Context>
void append_player_preview_visual_layer(const Context& context, const auto& event,
                                       const auto& scene) {
    if (!event) return;
#if defined(WORMEDIT_HAS_IMAGE_DECODER) || defined(WORMEDIT_HAS_VIDEO_DECODER)
    auto& state = context.state;
#endif
    wormedit::ui::player_preview_visual_layer_view_t layer{};
    layer.object_id = event->object;
    layer.resource_id = event->resource;
    layer.event_sample_frame = event->sample_frame;
    layer.kind = static_cast<wormedit::visual_event_kind_t>(event->kind);
    layer.draw_order = event->draw_order;
    layer.source_order = event->source_order;
    if (event->kind == visual_event_kind_t::video && scene.video_clock &&
        scene.video_clock->event == event->object && scene.video_clock->resource == event->resource) {
        layer.video_clock_ready = true;
        layer.video_elapsed_sample_frames = scene.video_clock->elapsed_sample_frames;
        layer.video_time_numerator = scene.video_clock->media_time_numerator;
        layer.video_time_denominator = scene.video_clock->media_time_denominator;
    }
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
    const auto resolved = visual::resolved_visual_asset_t::from(
        context.history.document, event->resource, context.project_directory);
    layer.static_image = resolved && resolved->kind == asset_kind_t::image;
    if (layer.static_image) state.scenes.populate_image(layer);
#endif
#if defined(WORMEDIT_HAS_VIDEO_DECODER)
    if (layer.video_clock_ready) {
        layer.video_frame = true;
        if (state.decoded_video && state.decoded_video->context.source.resource == event->resource &&
            state.decoded_video->context.clock.event == scene.video_clock->event &&
            visual::is_video_frame_covering_clock(*state.decoded_video->payload, *scene.video_clock)) {
            layer.image_ready = true;
            layer.image_content_id = state.decoded_video_content_id;
            layer.image_width = state.decoded_video->payload->width;
            layer.image_height = state.decoded_video->payload->height;
            layer.rgba_pixels = state.decoded_video->payload->pixels;
        }
        layer.image_pending = !layer.image_ready && context.video_job.is_busy();
        if (layer.image_pending) {
            layer.diagnostic = wormedit::ui::player_preview_visual_diagnostic_t::video_frame_late;
            layer.diagnostic_text =
                "The video frame missed its presentation deadline and is still decoding.";
        } else if (!layer.image_ready && state.failed_video_event == event->object) {
            layer.diagnostic = wormedit::ui::player_preview_visual_diagnostic_t::video_decode_failed;
            layer.diagnostic_text = state.video_diagnostic_message.c_str();
        }
    }
#endif
    context.view.visual_layers.push_back(layer);
}

template <typename Context>
void append_player_preview_visual_layers(const Context& context, const auto& scene) {
    append_player_preview_visual_layer(context, scene.visual_layers.background, scene);
    append_player_preview_visual_layer(context, scene.visual_layers.poor, scene);
    append_player_preview_visual_layer(context, scene.visual_layers.layer, scene);
    append_player_preview_visual_layer(context, scene.visual_layers.layer2, scene);
    append_player_preview_visual_layer(context, scene.visual_layers.video, scene);
    append_player_preview_visual_layer(context, scene.visual_layers.storyboard, scene);
}

template <typename Context>
void request_player_preview_video(const Context& context) {
#if defined(WORMEDIT_HAS_VIDEO_DECODER)
    auto& state = context.state;
    auto& video_job = context.video_job;
    const auto active_video = std::ranges::find_if(context.view.visual_layers, [](const auto& layer) {
        return layer.video_clock_ready && layer.video_frame;
    });
    if (active_video == context.view.visual_layers.end()) {
        if (video_job.is_busy()) video_job.cancel();
        state.requested_video_clock.reset();
        state.failed_video_event.reset();
        state.video_diagnostic_message.clear();
        state.decoded_video.reset();
        state.decoded_video_content_id = 0;
        return;
    }
    const video_preview_clock_t clock{
        .event = object_id_t{active_video->object_id},
        .resource = resource_id_t{active_video->resource_id},
        .activation_sample_frame = active_video->event_sample_frame,
        .elapsed_sample_frames = active_video->video_elapsed_sample_frames,
        .media_time_numerator = active_video->video_time_numerator,
        .media_time_denominator = active_video->video_time_denominator,
    };
    const auto switched_event = state.requested_video_clock &&
        (state.requested_video_clock->event != clock.event ||
         state.requested_video_clock->resource != clock.resource);
    const auto sought_backward = state.requested_video_clock &&
        state.requested_video_clock->event == clock.event &&
        clock.elapsed_sample_frames < state.requested_video_clock->elapsed_sample_frames;
    if ((switched_event || sought_backward) && video_job.is_busy()) video_job.cancel();
    if (switched_event) {
        state.failed_video_event.reset();
        state.video_diagnostic_message.clear();
    }
    const auto decoded_covers_clock = state.decoded_video &&
        state.decoded_video->context.source.resource == clock.resource &&
        state.decoded_video->context.clock.event == clock.event &&
        visual::is_video_frame_covering_clock(*state.decoded_video->payload, clock);
    if (video_job.is_busy() || state.failed_video_event == clock.event || decoded_covers_clock ||
        (state.requested_video_clock && *state.requested_video_clock == clock)) return;
    const auto resolved = visual::resolved_visual_asset_t::from(
        context.history.document, clock.resource, context.project_directory);
    if (!resolved || resolved->kind != asset_kind_t::video) return;
    const auto started = video_job.request(context.history.document.id,
        context.history.document.revision, *resolved, clock);
    if (started) {
        state.requested_video_clock = clock;
    } else if (started.error().code != failure_code_t::capacity_exceeded) {
        state.failed_video_event = clock.event;
        state.video_diagnostic_message = started.error().message.empty()
                                            ? "The video frame task could not start."
                                            : started.error().message;
    }
#else
    static_cast<void>(context);
#endif
}

template <typename Context>
void publish_player_preview_scene(const Context& context, const auto& scene) {
    auto& view = context.view;
    view.scene_available = true;
    view.pre_zero = scene.pre_zero;
    view.split_stage = scene.split_stage;
    view.visual_fit = static_cast<wormedit::visual_fit_policy_t>(scene.visual_fit);

}

template <typename Context>
auto present_player_preview_frame(const Context& context, const player_preview_frame_t& frame) -> bool {
    auto& state = context.state;
    auto& view = context.view;
    const auto scene = player_preview_scene_view_t::from(
        *state.index.value(), {.sample_frame = context.sample_frame,
                       .sample_rate = context.sample_rate,
                       .viewport_width_pixels = view.viewport_width_pixels,
                       .viewport_height_pixels = view.viewport_height_pixels,
                       .receptor_y_pixels = 52.0,
                       .pixels_per_beat = 82.0,
                       .skin = context.skin,
                       .visual_fit = visual_fit_policy_t::contain,
                       .visual_layers = frame.visual,
                       .maximum_visible_notes = maximum_preview_notes,
                       .maximum_storyboard_instances = maximum_preview_storyboard_instances});
    if (!scene.value || !scene.diagnostics.empty()) {
        fail_player_preview(context,
            scene.diagnostics.empty() ? "No player-preview frame is available at this time."
                                      : scene.diagnostics.front().message);
        return false;
    }
    append_scene_geometry(*scene.value, context.view.lanes, context.view.receptors, context.view.hold_bodies, context.view.note_endpoints);
    append_player_preview_visual_layers(context, *scene.value);
    request_player_preview_video(context);
    publish_player_preview_scene(context, *scene.value);
    return true;
}

template <typename State>
void populate_player_preview_storyboard(const player_preview_projection_context_t<State>& context,
    const result_t<storyboard_time_t, failure_code_t>& time, std::size_t used_instances,
    storyboard_conditional_layer_t conditional_layer) {
    auto& view = context.view;
    if (!time) {
        view.status_text = "The player-preview clock exceeds the storyboard time range.";
        return;
    }
    const auto result = context.state.scenes.refill(view.storyboard_sprites, std::nullopt,
        *time, maximum_preview_storyboard_instances - used_instances, conditional_layer);
    // Binding failures invalidate the scene; image and capacity errors retain notes.
    if (!result.value && std::ranges::any_of(result.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == diagnostic_code_t::invalid_binding;
    })) view.scene_available = false;
    if (!result.diagnostics.empty()) view.status_text = result.diagnostics.front().message;
}

} // namespace

namespace {
auto current_storyboard_preview(const document_history_t& history, const editor_interaction_state_t* interaction)
    -> std::shared_ptr<const osbx_edit_preview_t> {
    if (!interaction) return {};
    const auto* pending = interaction->gesture<pending_osbx_edit_gesture_t>();
    const auto* stamp = interaction->gesture_stamp<pending_osbx_edit_gesture_t>();
    if (!pending || !stamp || !stamp->is_current(history.document) || !pending->preview_owner ||
        pending->preview_owner->prepared_generation != osbx_lua_prepared_scope_t::generation()) return {};
    return pending->preview_owner;
}
} // namespace

void player_preview_presenter_t::impl_t::refill_panels(ui::storyboard_view_t& storyboard,
    const song_document_t& document, const result_t<storyboard_time_t, failure_code_t>& time,
    const storyboard_timeline_timing_t& timing, const editor_interaction_state_t* interaction) {
    const auto selected = storyboard.state.selection.storyboard;
    const auto* scene = scenes.scene(selected);
    refill_storyboard_canvas_outlines(storyboard, scene);
    hierarchy.refill(storyboard, document, &scenes);
    const auto* node = time && storyboard.state.selection.nodes.size() == 1 ?
        scenes.node_state(selected, storyboard.state.selection.nodes.front()) : nullptr;
    build_storyboard_inspector_view(storyboard, document,
        time ? *time : storyboard_time_t{.denominator = 0}, node, scene);
    build_storyboard_gizmo_view(storyboard, scenes);
    storyboard.canvas.gizmo.can_begin = !interaction || !interaction->is_pending();
    timeline.refill(storyboard.timeline, document, storyboard,
        time ? *time : storyboard_time_t{.denominator = 0}, scene, timing);
    storyboard.timeline.status = {};
    for (const auto& id : storyboard.state.selection.nodes) {
        const auto diagnostic = scenes.node_diagnostic(selected, id);
        if (!diagnostic.empty()) { storyboard.timeline.status = diagnostic; break; }
    }
}

player_preview_presenter_t::player_preview_presenter_t() : impl_(std::make_unique<impl_t>()) {}
player_preview_presenter_t::~player_preview_presenter_t() = default;
player_preview_presenter_t::player_preview_presenter_t(player_preview_presenter_t&&) noexcept =
    default;
auto player_preview_presenter_t::operator=(player_preview_presenter_t&&) noexcept
    -> player_preview_presenter_t& = default;

void player_preview_presenter_t::populate(ui::player_preview_view_t& view,
    const document_history_t& history, std::optional<chart_id_t> selected_chart, bool visible,
    player_preview_skin_t skin, std::uint64_t sample_frame, std::uint32_t sample_rate,
    const std::filesystem::path& project_directory
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
    , visual::image_job_t& image_job
#endif
#if defined(WORMEDIT_HAS_VIDEO_DECODER)
    , visual::video_frame_job_t& video_job
#endif
    , ui::storyboard_view_t* storyboard, std::uint64_t session_generation,
    const editor_interaction_state_t* interaction
) {
    auto& state = *impl_;
    // Clear borrowed views before their owners can rebind/reset.
    view.reset();
    if (storyboard) storyboard->canvas.reset_frame();
    view.visible = visible;
    view.skin = skin;
    view.visual_fit = visual_fit_policy_t::contain;
    view.viewport_width_pixels = 300;
    view.viewport_height_pixels = 420;
    view.project_id = history.document.id;
    view.document_revision = history.document.revision;
    view.sample_frame = sample_frame;
    const auto binding_changed = state.scenes.begin(history.document, session_generation, project_directory
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
        , image_job
#endif
        , current_storyboard_preview(history, interaction));
    if (binding_changed) state.index.invalidate();
    const auto time = storyboard_time_t::from_frame(sample_frame, sample_rate);
    const auto conditional_layer = storyboard ? storyboard->state.conditional_layer : storyboard_conditional_layer_t::pass;
    if (storyboard && storyboard->visible && storyboard->selected()) {
        populate_storyboard_canvas(storyboard->canvas, state.scenes,
            storyboard->state.selection.storyboard, time, conditional_layer);
        filter_storyboard_canvas_hidden(*storyboard, state.scenes.scene(storyboard->state.selection.storyboard));
    }
#if defined(WORMEDIT_HAS_VIDEO_DECODER)
    state.consume_video_completion(video_job, history.document.id);
#endif
    const auto* chart = selected_chart ? history.read_chart(*selected_chart) : nullptr;
    if (visible && !chart) view.status_text = selected_chart ? "The selected difficulty is unavailable." : "Choose a difficulty to preview.";
    if (visible && chart) {
        view.chart_id = chart->id;
        player_preview_projection_context_t context{state, view, history, *chart, skin,
            sample_frame, sample_rate, project_directory
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
            , image_job
#endif
#if defined(WORMEDIT_HAS_VIDEO_DECODER)
            , video_job
#endif
        };
        const auto frame = prepare_player_preview_frame(context);
        if (frame && present_player_preview_frame(context, *frame)) {
            const auto used = storyboard ? storyboard->canvas.sprites.size() : 0;
            populate_player_preview_storyboard(context, time, used, conditional_layer);
        }
        if (view.status_text.empty()) view.status_text = osbx_lua_prepared_scope_t::diagnostic();
    }
    std::span<ui::player_preview_visual_layer_view_t* const> thumbnails;
    if (storyboard && storyboard->visible) {
        state.refill_panels(*storyboard, history.document, time,
            storyboard_timeline_timing_t::from(history, selected_chart, interaction), interaction);
        state.assets.refill(*storyboard, history.document, project_directory,
            state.scenes.scene(storyboard->state.selection.storyboard));
        thumbnails = state.assets.prepare_thumbnails(*storyboard, state.scenes);
    }
    state.scenes.finish_images(view, storyboard ? &storyboard->canvas : nullptr
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
        , image_job
#endif
        , thumbnails);
}

} // namespace wormedit::sys
