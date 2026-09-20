#include "sys/player_preview_presenter.h"

#include <wormedit/core/bga_preview.h>
#include <wormedit/core/command.h>
#include <wormedit/core/storyboard_preview.h>

#if defined(WORMEDIT_HAS_IMAGE_DECODER)
#include <wormedit/visual/image_job.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

namespace wormedit::sys {
namespace {

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
    song_id_t project;
    chart_id_t chart;
    std::uint64_t revision{};
    bool attempted{};
    std::optional<player_preview_index_t> index;
    std::optional<preview_schedule_t> visual_schedule;
    std::optional<bga_preview_queue_t> visual_queue;
    std::optional<bga_preview_token_t> visual_token;
    std::vector<cached_storyboard_t> storyboards;
    std::uint32_t sample_rate{};
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
    std::vector<resource_id_t> requested_visual_resources;
    std::shared_ptr<const visual::image_job_result_t> decoded_visuals;
#endif
    std::string status;
    std::string view_status;
    std::vector<WormUiPlayerPreviewLaneView> lanes;
    std::vector<WormUiPlayerPreviewReceptorView> receptors;
    std::vector<WormUiPlayerPreviewHoldView> holds;
    std::vector<WormUiPlayerPreviewNoteView> notes;
    std::vector<WormUiPlayerPreviewVisualLayerView> visual_layers;
    std::vector<WormUiPlayerPreviewStoryboardSpriteView> storyboard_sprites;
};

player_preview_presenter_t::player_preview_presenter_t() : impl_(std::make_unique<impl_t>()) {}
player_preview_presenter_t::~player_preview_presenter_t() = default;
player_preview_presenter_t::player_preview_presenter_t(player_preview_presenter_t&&) noexcept =
    default;
auto player_preview_presenter_t::operator=(player_preview_presenter_t&&) noexcept
    -> player_preview_presenter_t& = default;

void player_preview_presenter_t::populate(WormUiPlayerPreviewView& view,
                                          const document_history_t& history,
                                          std::optional<chart_id_t> selected_chart, bool visible,
                                          player_preview_skin_t skin, std::uint64_t sample_frame,
                                          std::uint32_t sample_rate
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
                                          ,
                                          visual::image_job_t& image_job,
                                          const std::filesystem::path& project_directory
#endif
) {
    auto& state = *impl_;
    state.lanes.clear();
    state.receptors.clear();
    state.holds.clear();
    state.notes.clear();
    state.visual_layers.clear();
    state.storyboard_sprites.clear();
    state.view_status.clear();
    view = {};
    view.visible = visible;
    view.skin = static_cast<WormUiPlayerPreviewSkin>(skin);
    view.visual_fit = WORM_UI_VISUAL_FIT_CONTAIN;
    view.viewport_width_pixels = 300.0;
    view.viewport_height_pixels = 420.0;
    view.project_id = history.document.id.value;
    view.lanes = state.lanes.data();
    view.receptors = state.receptors.data();
    view.hold_bodies = state.holds.data();
    view.note_endpoints = state.notes.data();
    view.visual_layers = state.visual_layers.data();
    view.storyboard_sprites = state.storyboard_sprites.data();
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
    if (auto completion = image_job.poll(); completion && *completion &&
                                            completion->project == history.document.id &&
                                            completion->document_revision == state.revision) {
        state.decoded_visuals = std::move(completion->result);
    }
#endif
    if (!visible) {
        return;
    }
    const auto fail = [&](std::string message) {
        state.view_status = std::move(message);
        view.status_text = state.view_status.c_str();
    };
    if (!selected_chart) {
        fail("Choose a difficulty to preview.");
        return;
    }
    const auto chart =
        std::ranges::find(history.document.charts, *selected_chart, &chart_document_t::id);
    if (chart == history.document.charts.end()) {
        fail("The selected difficulty is unavailable.");
        return;
    }
    view.chart_id = chart->id.value;
    view.document_revision = history.document.revision;
    view.sample_frame = sample_frame;
    if (!state.attempted || state.project != history.document.id || state.chart != chart->id ||
        state.revision != history.document.revision || state.sample_rate != sample_rate) {
        state.project = history.document.id;
        state.chart = chart->id;
        state.revision = history.document.revision;
        state.attempted = true;
        state.index.reset();
        state.visual_schedule.reset();
        state.visual_queue.reset();
        state.visual_token.reset();
        state.storyboards.clear();
        state.sample_rate = sample_rate;
        state.status.clear();
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
        state.requested_visual_resources.clear();
        state.decoded_visuals.reset();
        image_job.cancel();
#endif
        auto built = build_player_preview_index(*chart, history.document.revision);
        if (!built) {
            state.status = built.diagnostics.empty()
                               ? "The player-preview visual schedule could not be compiled."
                               : built.diagnostics.front().message;
        } else {
            state.index = std::move(*built.index);
            auto schedule =
                build_preview_schedule(*chart, state.index->timing,
                                       {.sample_rate = sample_rate, .include_metronome = false});
            if (!schedule) {
                state.status = schedule.diagnostics.empty()
                                   ? "The visual preview schedule could not be compiled."
                                   : schedule.diagnostics.front().message;
            } else {
                state.visual_schedule = std::move(*schedule.schedule);
                state.visual_queue.emplace();
                const auto started = state.visual_queue->start(*state.visual_schedule);
                if (started) {
                    state.visual_token = started.token;
                } else {
                    state.status = "The visual preview schedule could not be started.";
                }
            }
        }
        if (state.status.empty()) {
            state.storyboards.reserve(history.document.storyboards.size());
            for (const auto& storyboard : history.document.storyboards) {
                if (!storyboard.model) {
                    continue;
                }
                auto built_storyboard = build_storyboard_preview_index(*storyboard.model);
                if (!built_storyboard) {
                    state.status = built_storyboard.diagnostics.empty()
                                       ? "A storyboard preview index could not be built."
                                       : built_storyboard.diagnostics.front().message;
                    break;
                }
                state.storyboards.emplace_back(storyboard.id, std::move(*built_storyboard.index));
            }
        }
    }
    if (!state.index || !state.visual_schedule || !state.visual_queue || !state.visual_token ||
        !state.status.empty()) {
        fail(state.status);
        return;
    }
    if (sample_frame == std::numeric_limits<std::uint64_t>::max()) {
        fail("The player-preview frame exceeds the visual cursor range.");
        return;
    }
    auto visual = state.visual_queue->advance(*state.visual_token, sample_frame + 1);
    if (visual.status == bga_preview_status_t::non_monotonic_window) {
        const auto sought = state.visual_queue->seek(*state.visual_token, sample_frame);
        if (sought) {
            state.visual_token = sought.token;
            visual = state.visual_queue->advance(*state.visual_token, sample_frame + 1);
        }
    }
    if (visual.status != bga_preview_status_t::ready) {
        fail("The visual preview cursor could not resolve this frame.");
        return;
    }
    const auto time_ms = transport_milliseconds(sample_frame, sample_rate);
    if (!time_ms) {
        fail("The player-preview frame exceeds the storyboard time range.");
        return;
    }
    std::vector<storyboard_draw_instance_t> storyboard_instances;
    storyboard_instances.reserve(WORM_UI_PLAYER_PREVIEW_STORYBOARD_CAPACITY);
    for (auto& cached : state.storyboards) {
        const auto document =
            std::ranges::find(history.document.storyboards, cached.id, &storyboard_document_t::id);
        if (document == history.document.storyboards.end() || !document->model) {
            fail("A cached storyboard preview no longer matches the document.");
            return;
        }
        const auto remaining =
            WORM_UI_PLAYER_PREVIEW_STORYBOARD_CAPACITY - storyboard_instances.size();
        const auto frame =
            evaluate_storyboard_frame(*document->model, cached.index, *time_ms, cached.arena,
                                      {.maximum_instances = std::max<std::size_t>(remaining, 1)});
        if (!frame) {
            fail(frame.diagnostic ? frame.diagnostic->message
                                  : "The storyboard frame exceeded its preview bounds.");
            return;
        }
        if (frame.instances.size() > remaining) {
            fail("The storyboard frame exceeds the frontend sprite cap.");
            return;
        }
        storyboard_instances.insert(storyboard_instances.end(), frame.instances.begin(),
                                    frame.instances.end());
    }
    const auto scene = make_player_preview_scene(
        *state.index, {.sample_frame = sample_frame,
                       .sample_rate = sample_rate,
                       .viewport_width_pixels = view.viewport_width_pixels,
                       .viewport_height_pixels = view.viewport_height_pixels,
                       .receptor_y_pixels = 52.0,
                       .pixels_per_beat = 82.0,
                       .skin = skin,
                       .visual_fit = visual_fit_policy_t::contain,
                       .visual_layers = visual.layers,
                       .storyboard_instances = storyboard_instances,
                       .maximum_visible_notes = WORM_UI_PLAYER_PREVIEW_NOTE_CAPACITY,
                       .maximum_storyboard_instances = WORM_UI_PLAYER_PREVIEW_STORYBOARD_CAPACITY});
    if (!scene) {
        fail(scene.diagnostics.empty() ? "No player-preview frame is available at this time."
                                       : scene.diagnostics.front().message);
        return;
    }
    if (scene.scene->lanes.size() > WORM_UI_PLAYER_PREVIEW_LANE_CAPACITY ||
        scene.scene->receptors.size() > WORM_UI_PLAYER_PREVIEW_LANE_CAPACITY ||
        scene.scene->hold_bodies.size() > WORM_UI_PLAYER_PREVIEW_HOLD_CAPACITY ||
        scene.scene->note_endpoints.size() > WORM_UI_PLAYER_PREVIEW_NOTE_CAPACITY ||
        scene.scene->storyboard_instances.size() > WORM_UI_PLAYER_PREVIEW_STORYBOARD_CAPACITY) {
        view.truncated = true;
        fail("The player-preview frame exceeds the frontend geometry cap.");
        return;
    }
    const auto convert_rect = [](const player_preview_rect_t& rect) {
        return WormUiPlayerPreviewRect{rect.left, rect.top, rect.right, rect.bottom};
    };
    for (const auto& lane : scene.scene->lanes) {
        state.lanes.push_back({.lane_id = lane.lane.value,
                               .role = static_cast<std::uint8_t>(lane.role),
                               .glyph = static_cast<std::uint8_t>(lane.glyph),
                               .display_order = lane.display_order,
                               .bounds = convert_rect(lane.bounds)});
    }
    for (const auto& receptor : scene.scene->receptors) {
        state.receptors.push_back({.lane_id = receptor.lane.value,
                                   .glyph = static_cast<std::uint8_t>(receptor.glyph),
                                   .bounds = convert_rect(receptor.bounds)});
    }
    for (const auto& hold : scene.scene->hold_bodies) {
        state.holds.push_back(
            {.note_id = hold.note.value,
             .lane_id = hold.lane.value,
             .semantic_color_visible = hold.semantic_color.has_value(),
             .semantic_red =
                 static_cast<std::uint8_t>(hold.semantic_color ? hold.semantic_color->red : 0),
             .semantic_green =
                 static_cast<std::uint8_t>(hold.semantic_color ? hold.semantic_color->green : 0),
             .semantic_blue =
                 static_cast<std::uint8_t>(hold.semantic_color ? hold.semantic_color->blue : 0),
             .semantic_alpha =
                 static_cast<std::uint8_t>(hold.semantic_color ? hold.semantic_color->alpha : 0),
             .bounds = convert_rect(hold.bounds)});
    }
    for (const auto& note : scene.scene->note_endpoints) {
        state.notes.push_back(
            {.note_id = note.note.value,
             .lane_id = note.lane.value,
             .kind = static_cast<std::uint8_t>(note.kind),
             .glyph = static_cast<std::uint8_t>(note.glyph),
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
             .bounds = convert_rect(note.bounds)});
    }
    const auto append_visual = [&](const auto& event) {
        if (!event) {
            return;
        }
        WormUiPlayerPreviewVisualLayerView layer{};
        layer.object_id = event->object.value;
        layer.resource_id = event->resource.value;
        layer.event_sample_frame = event->sample_frame;
        layer.kind = static_cast<WormUiPlayerPreviewVisualKind>(event->kind);
        layer.draw_order = event->draw_order;
        layer.source_order = event->source_order;
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
        const auto resolved =
            visual::resolve_visual_asset(history.document, event->resource, project_directory);
        layer.static_image = resolved && resolved.source->kind == asset_kind_t::image;
        if (layer.static_image && state.decoded_visuals) {
            const auto decoded =
                std::ranges::find(state.decoded_visuals->items, event->resource,
                                  [](const auto& item) { return item.source.resource; });
            if (decoded != state.decoded_visuals->items.end() && *decoded) {
                layer.image_ready = true;
                layer.image_width = decoded->image->width;
                layer.image_height = decoded->image->height;
                layer.rgba_pixels = decoded->image->pixels.data();
                layer.rgba_byte_count = decoded->image->pixels.size();
            }
        }
        layer.image_pending = layer.static_image && !layer.image_ready && image_job.busy();
#endif
        state.visual_layers.push_back(layer);
    };
    append_visual(scene.scene->visual_layers.background);
    append_visual(scene.scene->visual_layers.poor);
    append_visual(scene.scene->visual_layers.layer);
    append_visual(scene.scene->visual_layers.video);
    append_visual(scene.scene->visual_layers.storyboard);
    for (const auto& instance : scene.scene->storyboard_instances) {
        const auto origin = origin_fraction(instance.origin);
        WormUiPlayerPreviewStoryboardSpriteView sprite{};
        sprite.stable_id = instance.stable_id;
        sprite.resource_id = storyboard_resource(history.document, instance.path).value;
        sprite.source_order = instance.source_order;
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
        if (sprite.resource_id != 0 && state.decoded_visuals) {
            const auto decoded =
                std::ranges::find(state.decoded_visuals->items, resource_id_t{sprite.resource_id},
                                  [](const auto& item) { return item.source.resource; });
            if (decoded != state.decoded_visuals->items.end() && *decoded) {
                sprite.image_ready = true;
                sprite.image_width = decoded->image->width;
                sprite.image_height = decoded->image->height;
                sprite.rgba_pixels = decoded->image->pixels.data();
                sprite.rgba_byte_count = decoded->image->pixels.size();
            }
        }
        sprite.image_pending = sprite.resource_id != 0 && !sprite.image_ready && image_job.busy();
#endif
        state.storyboard_sprites.push_back(sprite);
    }
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
    std::vector<visual::resolved_visual_asset_t> requested_sources;
    std::vector<resource_id_t> requested_resources;
    requested_sources.reserve(state.visual_layers.size() + state.storyboard_sprites.size());
    requested_resources.reserve(state.visual_layers.size() + state.storyboard_sprites.size());
    const auto append_resource = [&](resource_id_t resource) {
        if (!resource ||
            std::ranges::find(requested_resources, resource) != requested_resources.end()) {
            return;
        }
        const auto resolved =
            visual::resolve_visual_asset(history.document, resource, project_directory);
        if (resolved && resolved.source->kind == asset_kind_t::image) {
            requested_resources.push_back(resource);
            requested_sources.push_back(*resolved.source);
        }
    };
    for (const auto& layer : state.visual_layers) {
        append_resource(resource_id_t{layer.resource_id});
    }
    for (const auto& sprite : state.storyboard_sprites) {
        append_resource(resource_id_t{sprite.resource_id});
    }
    if (requested_resources != state.requested_visual_resources) {
        if (requested_sources.empty()) {
            image_job.cancel();
            state.requested_visual_resources.clear();
            state.decoded_visuals.reset();
        } else if (image_job.request(history.document.id, history.document.revision,
                                     requested_sources)) {
            state.requested_visual_resources = std::move(requested_resources);
        }
    }
#endif
    view.scene_available = true;
    view.pre_zero = scene.scene->pre_zero;
    view.split_stage = scene.scene->split_stage;
    view.visual_fit = static_cast<WormUiVisualFitPolicy>(scene.scene->visual_fit);
    view.lanes = state.lanes.data();
    view.lane_count = state.lanes.size();
    view.receptors = state.receptors.data();
    view.receptor_count = state.receptors.size();
    view.hold_bodies = state.holds.data();
    view.hold_body_count = state.holds.size();
    view.note_endpoints = state.notes.data();
    view.note_endpoint_count = state.notes.size();
    view.visual_layers = state.visual_layers.data();
    view.visual_layer_count = state.visual_layers.size();
    view.storyboard_sprites = state.storyboard_sprites.data();
    view.storyboard_sprite_count = state.storyboard_sprites.size();
}

} // namespace wormedit::sys
