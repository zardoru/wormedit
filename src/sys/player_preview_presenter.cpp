#include "sys/player_preview_presenter.h"
#include "sys/frontend_geometry.h"

#include <wormedit/core/bga_preview.h>
#include <wormedit/core/command.h>
#include <wormedit/core/keyed_cache.h>
#include <wormedit/core/storyboard_preview.h>

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
    auto operator==(const player_preview_cache_key_t&) const -> bool = default;
};

struct cached_storyboard_t {
    storyboard_id_t id;
    storyboard_preview_index_t index;
    storyboard_frame_arena_t arena;

    cached_storyboard_t(storyboard_id_t source_id, storyboard_preview_index_t source_index)
        : id(source_id), index(std::move(source_index)), arena() {}
};

auto transport_milliseconds(std::uint64_t sample_frame, std::uint32_t sample_rate)
    -> std::optional<std::int64_t> {
    if (sample_rate == 0) {
        return std::nullopt;
    }
    const auto whole_seconds = sample_frame / sample_rate;
    if (whole_seconds >
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max() / 1000)) {
        return std::nullopt;
    }
    const auto remainder = sample_frame % sample_rate;
    return static_cast<std::int64_t>(whole_seconds * 1000U + remainder * 1000U / sample_rate);
}

auto origin_fraction(std::string_view origin) -> std::array<double, 2> {
    if (origin == "TopLeft") {
        return {0.0, 0.0};
    }
    if (origin == "TopCentre") {
        return {0.5, 0.0};
    }
    if (origin == "TopRight") {
        return {1.0, 0.0};
    }
    if (origin == "CentreLeft") {
        return {0.0, 0.5};
    }
    if (origin == "CentreRight") {
        return {1.0, 0.5};
    }
    if (origin == "BottomLeft") {
        return {0.0, 1.0};
    }
    if (origin == "BottomCentre") {
        return {0.5, 1.0};
    }
    if (origin == "BottomRight") {
        return {1.0, 1.0};
    }
    return {0.5, 0.5};
}

auto storyboard_resource(const song_document_t& document, std::string_view path) -> resource_id_t {
    const auto normalized = std::filesystem::path{path}.lexically_normal();
    const auto asset = std::ranges::find_if(document.visual_assets, [&](const auto& candidate) {
        return candidate.kind == asset_kind_t::image &&
               candidate.source_path.lexically_normal() == normalized;
    });
    if (asset == document.visual_assets.end()) {
        return {};
    }
    const auto resource =
        std::ranges::find(document.resources, asset->id, &resource_entry_t::asset);
    return resource == document.resources.end() ? resource_id_t{} : resource->id;
}

} // namespace

struct player_preview_presenter_t::impl_t {
    keyed_cache_t<player_preview_cache_key_t, std::optional<player_preview_index_t>> index;
    auto prepare_index(const document_history_t& history, const chart_document_t& chart,
                       std::uint32_t sample_rate
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
                       , visual::image_job_t& image_job
#endif
#if defined(WORMEDIT_HAS_VIDEO_DECODER)
                       , visual::video_frame_job_t& video_job
#endif
    ) -> bool;
    auto build_index(const chart_document_t& chart, const player_preview_cache_key_t& key,
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
                     visual::image_job_t& image_job,
#endif
#if defined(WORMEDIT_HAS_VIDEO_DECODER)
                     visual::video_frame_job_t& video_job,
#endif
                     std::optional<player_preview_index_t>& value) -> void;
    std::optional<preview_schedule_t> visual_schedule;
    std::optional<bga_preview_queue_t> visual_queue;
    std::optional<bga_preview_token_t> visual_token;
    std::vector<cached_storyboard_t> storyboards;
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
    std::vector<resource_id_t> requested_visual_resources;
    std::shared_ptr<const visual::image_job_result_t> decoded_visuals;
#endif
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
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
    , visual::image_job_t& image_job
#endif
#if defined(WORMEDIT_HAS_VIDEO_DECODER)
    , visual::video_frame_job_t& video_job
#endif
) -> bool {
    const player_preview_cache_key_t key{
        .stamp = chart_stamp_t::from(history.document, chart.id), .sample_rate = sample_rate};
    const auto previous_builds = index.build_count();
    (void)index.get_or_build(key, std::bind_front(&impl_t::build_index, this,
        std::cref(chart), std::cref(key)
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
        , std::ref(image_job)
#endif
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
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
    visual::image_job_t& image_job,
#endif
#if defined(WORMEDIT_HAS_VIDEO_DECODER)
    visual::video_frame_job_t& video_job,
#endif
    std::optional<player_preview_index_t>& value) -> void {
    value.reset();
    visual_schedule.reset();
    visual_queue.reset();
    visual_token.reset();
    storyboards.clear();
    status.clear();
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
    requested_visual_resources.clear();
    decoded_visuals.reset();
    image_job.cancel();
#endif
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

auto evaluate_storyboards(const song_document_t& document,
                          std::vector<cached_storyboard_t>& storyboards, std::int64_t time_ms,
                          std::vector<storyboard_draw_instance_t>& instances,
                          std::string& error) -> bool {
    instances.reserve(maximum_preview_storyboard_instances);
    for (auto& cached : storyboards) {
        const auto storyboard =
            std::ranges::find(document.storyboards, cached.id, &storyboard_document_t::id);
        if (storyboard == document.storyboards.end() || !storyboard->model) {
            error = "A cached storyboard preview no longer matches the document.";
            return false;
        }
        const auto remaining = maximum_preview_storyboard_instances - instances.size();
        const auto frame = evaluate_storyboard_frame(
            *storyboard->model, cached.index, time_ms, cached.arena,
            {.maximum_instances = std::max<std::size_t>(remaining, 1)});
        if (!frame.is_success()) {
            error = frame.diagnostic ? frame.diagnostic->message
                                     : "The storyboard frame exceeded its preview bounds.";
            return false;
        }
        if (frame.instances.size() > remaining) {
            error = "The storyboard frame exceeds the frontend sprite cap.";
            return false;
        }
        instances.insert(instances.end(), frame.instances.begin(), frame.instances.end());
    }
    return true;
}

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
auto prepare_player_preview_storyboards(const Context& context) -> bool {
    auto& state = context.state;
    if (!state.status.empty()) return false;
    state.storyboards.reserve(context.history.document.storyboards.size());
    for (const auto& storyboard : context.history.document.storyboards) {
        if (!storyboard.model) continue;
        auto built = storyboard_preview_index_t::from(*storyboard.model);
        if (!built.value) {
            state.status = built.diagnostics.empty()
                               ? "A storyboard preview index could not be built."
                               : built.diagnostics.front().message;
            return false;
        }
        state.storyboards.emplace_back(storyboard.id, std::move(*built.value));
    }
    return true;
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
    std::vector<storyboard_draw_instance_t> storyboard_instances;
};

template <typename Context>
auto prepare_player_preview_frame(const Context& context)
    -> std::optional<player_preview_frame_t> {
    auto& state = context.state;
    if (!state.prepare_index(context.history, context.chart, context.sample_rate
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
                             , context.image_job
#endif
#if defined(WORMEDIT_HAS_VIDEO_DECODER)
                             , context.video_job
#endif
    )) {
        fail_player_preview(context, state.status);
        return std::nullopt;
    }
    (void)prepare_player_preview_storyboards(context);
    if (!state.index.value() || !state.visual_schedule || !state.visual_queue || !state.visual_token ||
        !state.status.empty()) {
        fail_player_preview(context, state.status);
        return std::nullopt;
    }
    auto visual = advance_player_preview(context);
    if (!visual) return std::nullopt;
    const auto time_ms = transport_milliseconds(context.sample_frame, context.sample_rate);
    if (!time_ms) {
        fail_player_preview(context, "The player-preview frame exceeds the storyboard time range.");
        return std::nullopt;
    }
    // advance_events accepted this token; no queue operation intervenes before this snapshot.
    auto layers = state.visual_queue->current_layers(*state.visual_token);
    player_preview_frame_t frame{.visual = std::move(*layers), .storyboard_instances = {}};
    std::string storyboard_error;
    if (!evaluate_storyboards(context.history.document, state.storyboards, *time_ms,
                              frame.storyboard_instances, storyboard_error)) {
        fail_player_preview(context, std::move(storyboard_error));
        return std::nullopt;
    }
    return frame;
}

template <typename Context>
void append_player_preview_visual_layer(const Context& context, const auto& event,
                                       const auto& scene) {
    if (!event) return;
    auto& state = context.state;
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
    if (layer.static_image && state.decoded_visuals) {
        const auto* decoded = state.decoded_visuals->find(event->resource);
        if (decoded) {
            layer.image_ready = true;
            layer.image_width = decoded->payload->width;
            layer.image_height = decoded->payload->height;
            layer.rgba_pixels = decoded->payload->pixels;
        }
    }
    layer.image_pending = layer.static_image && !layer.image_ready && context.image_job.is_busy();
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
void append_player_preview_storyboard_sprite(const Context& context, const auto& instance) {
    const auto origin = origin_fraction(instance.origin);
    wormedit::ui::player_preview_storyboard_sprite_view_t sprite{};
    sprite.stable_id = instance.stable_id;
    sprite.resource_id = storyboard_resource(context.history.document, instance.path);
    sprite.source_order = instance.source_order;
    sprite.layer_order = instance.layer_order;
    sprite.x = instance.x;
    sprite.y = instance.y;
    sprite.origin_x = origin[0];
    sprite.origin_y = origin[1];
    sprite.scale_x = instance.scale_x;
    sprite.scale_y = instance.scale_y;
    sprite.rotation_radians = instance.rotation_radians;
    sprite.opacity = instance.opacity;
    sprite.red = instance.red;
    sprite.green = instance.green;
    sprite.blue = instance.blue;
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
    auto& state = context.state;
    if (sprite.resource_id && state.decoded_visuals) {
        const auto* decoded = state.decoded_visuals->find(resource_id_t{sprite.resource_id});
        if (decoded) {
            sprite.image_ready = true;
            sprite.image_width = decoded->payload->width;
            sprite.image_height = decoded->payload->height;
            sprite.rgba_pixels = decoded->payload->pixels;
        }
    }
    sprite.image_pending = sprite.resource_id && !sprite.image_ready && context.image_job.is_busy();
#endif
    context.view.storyboard_sprites.push_back(sprite);
}

template <typename Context>
void request_player_preview_images(const Context& context) {
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
    auto& state = context.state;
    std::vector<visual::resolved_visual_asset_t> requested_sources;
    std::vector<resource_id_t> requested_resources;
    requested_sources.reserve(context.view.visual_layers.size() + context.view.storyboard_sprites.size());
    requested_resources.reserve(context.view.visual_layers.size() + context.view.storyboard_sprites.size());
    const auto append_resource = [&](resource_id_t resource) {
        if (!resource || std::ranges::find(requested_resources, resource) != requested_resources.end())
            return;
        const auto resolved = visual::resolved_visual_asset_t::from(
            context.history.document, resource, context.project_directory);
        if (resolved && resolved->kind == asset_kind_t::image) {
            requested_resources.push_back(resource);
            requested_sources.push_back(*resolved);
        }
    };
    for (const auto& layer : context.view.visual_layers) append_resource(layer.resource_id);
    for (const auto& sprite : context.view.storyboard_sprites)
        append_resource(sprite.resource_id);
    if (requested_resources != state.requested_visual_resources) {
        if (requested_sources.empty()) {
            context.image_job.cancel();
            state.requested_visual_resources.clear();
            state.decoded_visuals.reset();
        } else if (context.image_job.request(context.history.document.id,
                                             context.history.document.revision,
                                             requested_sources)) {
            state.requested_visual_resources = std::move(requested_resources);
        }
    }
#else
    static_cast<void>(context);
#endif
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
                       .storyboard_instances = frame.storyboard_instances,
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
    for (const auto& instance : scene.value->storyboard_instances)
        append_player_preview_storyboard_sprite(context, instance);
    request_player_preview_images(context);
    request_player_preview_video(context);
    publish_player_preview_scene(context, *scene.value);
    return true;
}

} // namespace

player_preview_presenter_t::player_preview_presenter_t() : impl_(std::make_unique<impl_t>()) {}
player_preview_presenter_t::~player_preview_presenter_t() = default;
player_preview_presenter_t::player_preview_presenter_t(player_preview_presenter_t&&) noexcept =
    default;
auto player_preview_presenter_t::operator=(player_preview_presenter_t&&) noexcept
    -> player_preview_presenter_t& = default;

void player_preview_presenter_t::populate(wormedit::ui::player_preview_view_t& view,
                                          const document_history_t& history,
                                          std::optional<chart_id_t> selected_chart, bool visible,
                                          player_preview_skin_t skin, std::uint64_t sample_frame,
                                          std::uint32_t sample_rate,
                                          const std::filesystem::path& project_directory
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
                                          ,
                                          visual::image_job_t& image_job
#endif
#if defined(WORMEDIT_HAS_VIDEO_DECODER)
                                          ,
                                          visual::video_frame_job_t& video_job
#endif
) {
    static_cast<void>(project_directory);
    auto& state = *impl_;
    view.reset();
    view.visible = visible;
    view.skin = static_cast<wormedit::player_preview_skin_t>(skin);
    view.visual_fit = wormedit::visual_fit_policy_t::contain;
    view.viewport_width_pixels = 300.0;
    view.viewport_height_pixels = 420.0;
    view.project_id = history.document.id;
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
    const auto revision = state.index.key() ? state.index.key()->stamp.document.revision : 0;
    if (auto completion = image_job.poll(); completion && *completion &&
                                            completion->value().identity.project == history.document.id &&
                                            completion->value().identity.document_revision == revision) {
        state.decoded_visuals = std::move(completion->value().payload);
    }
#endif
#if defined(WORMEDIT_HAS_VIDEO_DECODER)
    state.consume_video_completion(video_job, history.document.id);
#endif
    if (!visible) {
        return;
    }
    const auto fail = [&](std::string message) {
        view.status_text = std::move(message);
    };
    if (!selected_chart) {
        fail("Choose a difficulty to preview.");
        return;
    }
    const auto chart =
        history.document.find_chart(*selected_chart);
    if (chart == nullptr) {
        fail("The selected difficulty is unavailable.");
        return;
    }
    view.chart_id = chart->id;
    view.document_revision = history.document.revision;
    view.sample_frame = sample_frame;
    player_preview_projection_context_t preview_context{
        state, view, history, *chart, skin, sample_frame, sample_rate, project_directory
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
        , image_job
#endif
#if defined(WORMEDIT_HAS_VIDEO_DECODER)
        , video_job
#endif
    };
    auto frame = prepare_player_preview_frame(preview_context);
    if (!frame) return;
    (void)present_player_preview_frame(preview_context, *frame);
}

} // namespace wormedit::sys
