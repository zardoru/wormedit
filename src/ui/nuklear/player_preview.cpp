#include "ui/nuklear/player_preview.h"
#include "ui/nuklear/player_preview_texture.h"
#include "ui/nuklear/arrow_glyph.h"

#include "wormedit/nuklear_config.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

auto rect_valid(const wormedit::rect_t* rect, const wormedit::ui::player_preview_view_t* view) -> bool {
    constexpr double geometry_limit = 10'000'000.0;
    return rect && view && std::isfinite(rect->left) && std::isfinite(rect->top) &&
           std::isfinite(rect->right) && std::isfinite(rect->bottom) && rect->right >= rect->left &&
           rect->bottom >= rect->top && std::abs(rect->left) <= geometry_limit &&
           std::abs(rect->top) <= geometry_limit && std::abs(rect->right) <= geometry_limit &&
           std::abs(rect->bottom) <= geometry_limit;
}

auto screen_rect(const wormedit::rect_t* source, const wormedit::ui::player_preview_view_t* view,
                 struct nk_rect viewport) -> struct nk_rect {
    const auto scale_x = viewport.w / static_cast<float>(view->viewport_width_pixels);
    const auto scale_y = viewport.h / static_cast<float>(view->viewport_height_pixels);
    return nk_rect(viewport.x + static_cast<float>(source->left) * scale_x,
                   viewport.y + static_cast<float>(source->top) * scale_y,
                   static_cast<float>(source->right - source->left) * scale_x,
                   static_cast<float>(source->bottom - source->top) * scale_y);
}

void draw_extended_arrow(struct nk_command_buffer* canvas, struct nk_rect bounds,
                          std::uint8_t glyph, struct nk_color color) {
    const auto center_x = bounds.x + bounds.w * 0.5f;
    const auto center_y = bounds.y + bounds.h * 0.5f;
    if (glyph == wormedit::ui::last_arrow_glyph) {
        nk_fill_triangle(canvas, center_x, bounds.y, bounds.x + bounds.w, center_y,
                          center_x, bounds.y + bounds.h, color);
        nk_fill_triangle(canvas, center_x, bounds.y, center_x, bounds.y + bounds.h,
                          bounds.x, center_y, color);
        return;
    }
    const auto left = glyph == 6 || glyph == 8;
    const auto up = glyph == 6 || glyph == 7;
    nk_fill_triangle(canvas, left ? bounds.x : bounds.x + bounds.w,
                      up ? bounds.y : bounds.y + bounds.h,
                      left ? bounds.x + bounds.w : bounds.x, center_y,
                      center_x, up ? bounds.y + bounds.h : bounds.y, color);
}

void draw_glyph(struct nk_command_buffer* canvas, struct nk_rect bounds, std::uint8_t glyph,
                struct nk_color color) {
    if (glyph == 2) {
        nk_fill_triangle(canvas, bounds.x, bounds.y + bounds.h * 0.5f, bounds.x + bounds.w,
                         bounds.y, bounds.x + bounds.w, bounds.y + bounds.h, color);
    } else if (glyph == 3) {
        nk_fill_triangle(canvas, bounds.x, bounds.y, bounds.x + bounds.w, bounds.y,
                         bounds.x + bounds.w * 0.5f, bounds.y + bounds.h, color);
    } else if (glyph == 4) {
        nk_fill_triangle(canvas, bounds.x + bounds.w * 0.5f, bounds.y, bounds.x,
                         bounds.y + bounds.h, bounds.x + bounds.w, bounds.y + bounds.h, color);
    } else if (glyph == 5) {
        nk_fill_triangle(canvas, bounds.x + bounds.w, bounds.y + bounds.h * 0.5f, bounds.x,
                         bounds.y, bounds.x, bounds.y + bounds.h, color);
    } else if (wormedit::ui::is_arrow_glyph(glyph)) {
        draw_extended_arrow(canvas, bounds, glyph, color);
    } else {
        nk_fill_rect(canvas, bounds, glyph == 1 ? 2.0f : 0.0f, color);
    }
}

} // namespace

namespace {

auto player_preview_header_valid(const wormedit::ui::player_preview_view_t* view) -> bool {
    if (!view || view->skin < wormedit::player_preview_skin_t::neutral ||
        view->skin > wormedit::player_preview_skin_t::arrows ||
        view->visual_fit < wormedit::visual_fit_policy_t::stretch ||
        view->visual_fit > wormedit::visual_fit_policy_t::cover) {
        return false;
    }
    return true;
}

auto player_preview_storage_valid(const wormedit::ui::player_preview_view_t& view) -> bool {
    if (!view.visible) return true;
    return std::isfinite(view.viewport_width_pixels) && view.viewport_width_pixels > 0.0 &&
        std::isfinite(view.viewport_height_pixels) && view.viewport_height_pixels > 0.0 &&
        view.visual_layers.size() <= wormedit::ui::player_preview_visual_layer_limit &&
        (view.scene_available || !view.status_text.empty()) &&
        (!view.scene_available || (bool(view.project_id) && view.document_revision != 0));
}

auto player_preview_geometry_valid(const wormedit::ui::player_preview_view_t& view) -> bool {
    for (std::size_t index = 0; index < view.lanes.size(); ++index)
        if (!view.lanes[index].lane_id || !rect_valid(&view.lanes[index].bounds, &view))
            return false;
    for (std::size_t index = 0; index < view.receptors.size(); ++index)
        if (!view.receptors[index].lane_id ||
            !rect_valid(&view.receptors[index].bounds, &view)) return false;
    for (std::size_t index = 0; index < view.hold_bodies.size(); ++index)
        if (!view.hold_bodies[index].note_id ||
            !rect_valid(&view.hold_bodies[index].bounds, &view)) return false;
    for (std::size_t index = 0; index < view.note_endpoints.size(); ++index)
        if (!view.note_endpoints[index].note_id ||
            !rect_valid(&view.note_endpoints[index].bounds, &view)) return false;
    return true;
}

auto preview_image_payload_valid(bool ready, bool pending, bool is_visual,
                                 std::uint64_t content_id, std::uint32_t width,
                                 std::uint32_t height, const std::uint8_t* pixels,
                                 std::size_t byte_count) -> bool {
    if (ready) return is_visual && !pending && (content_id != 0 || is_visual) && width != 0 &&
                      height != 0 && width <= WORM_UI_PLAYER_PREVIEW_IMAGE_MAX_DIMENSION &&
                      height <= WORM_UI_PLAYER_PREVIEW_IMAGE_MAX_DIMENSION && pixels &&
                      width <= SIZE_MAX / height / 4U &&
                      byte_count == static_cast<std::size_t>(width) * height * 4U;
    return content_id == 0 && width == 0 && height == 0 && pixels == nullptr && byte_count == 0;
}

auto preview_video_clock_valid(const wormedit::ui::player_preview_visual_layer_view_t& layer,
                               const wormedit::ui::player_preview_view_t& view) -> bool {
    if (layer.video_clock_ready)
        return layer.kind == wormedit::visual_event_kind_t::video &&
               layer.video_time_denominator != 0 &&
               layer.video_elapsed_sample_frames == view.sample_frame - layer.event_sample_frame;
    return layer.video_elapsed_sample_frames == 0 && layer.video_time_numerator == 0 &&
           layer.video_time_denominator == 0;
}

auto preview_visual_layer_valid(const wormedit::ui::player_preview_visual_layer_view_t& layer,
                                const wormedit::ui::player_preview_view_t& view) -> bool {
    if (!layer.object_id || !layer.resource_id ||
        layer.event_sample_frame > view.sample_frame ||
        layer.kind < wormedit::visual_event_kind_t::background ||
        layer.kind > wormedit::visual_event_kind_t::layer2 ||
        layer.diagnostic < wormedit::ui::player_preview_visual_diagnostic_t::none ||
        layer.diagnostic > wormedit::ui::player_preview_visual_diagnostic_t::video_decode_failed ||
        !preview_video_clock_valid(layer, view) ||
        (layer.video_frame && (layer.kind != wormedit::visual_event_kind_t::video ||
                               !layer.video_clock_ready)) ||
        (layer.static_image && layer.video_frame)) return false;
    const auto is_visual = layer.static_image || layer.video_frame;
    if (!preview_image_payload_valid(layer.image_ready, layer.image_pending, is_visual,
                                     layer.image_content_id, layer.image_width,
                                     layer.image_height, layer.rgba_pixels.data(),
                                     layer.rgba_pixels.size())) return false;
    if (layer.diagnostic == wormedit::ui::player_preview_visual_diagnostic_t::none)
        return layer.diagnostic_text.empty();
    if (!layer.video_frame || layer.diagnostic_text.empty() || layer.image_ready) return false;
    if (layer.diagnostic == wormedit::ui::player_preview_visual_diagnostic_t::video_frame_late)
        return layer.image_pending;
    if (layer.diagnostic == wormedit::ui::player_preview_visual_diagnostic_t::video_decode_failed)
        return !layer.image_pending;
    return false;
}

auto player_preview_visual_layers_valid(const wormedit::ui::player_preview_view_t& view) -> bool {
    for (std::size_t index = 0; index < view.visual_layers.size(); ++index)
        if (!preview_visual_layer_valid(view.visual_layers[index], view)) return false;
    return true;
}

auto player_preview_storyboard_sprite_valid(const wormedit::ui::player_preview_storyboard_sprite_view_t& sprite)
    -> bool {
    if (sprite.stable_id == 0 || sprite.layer_order > 4 || !std::isfinite(sprite.x) ||
        !std::isfinite(sprite.y) || !std::isfinite(sprite.origin_x) || sprite.origin_x < 0.0 ||
        sprite.origin_x > 1.0 || !std::isfinite(sprite.origin_y) || sprite.origin_y < 0.0 ||
        sprite.origin_y > 1.0 || !std::isfinite(sprite.scale_x) ||
        !std::isfinite(sprite.scale_y) || !std::isfinite(sprite.rotation_radians) ||
        !std::isfinite(sprite.opacity) || !std::isfinite(sprite.red) ||
        !std::isfinite(sprite.green) || !std::isfinite(sprite.blue)) return false;
    return preview_image_payload_valid(sprite.image_ready, sprite.image_pending,
                                       bool(sprite.resource_id), sprite.image_content_id,
                                       sprite.image_width, sprite.image_height, sprite.rgba_pixels.data(),
                                       sprite.rgba_pixels.size());
}

auto player_preview_storyboards_valid(const wormedit::ui::player_preview_view_t& view) -> bool {
    for (std::size_t index = 0; index < view.storyboard_sprites.size(); ++index)
        if (!player_preview_storyboard_sprite_valid(view.storyboard_sprites[index])) return false;
    return true;
}

} // namespace

int worm_ui_nuklear_player_preview_view_valid(const wormedit::ui::player_preview_view_t* view) {
    if (!player_preview_header_valid(view)) return 0;
    if (!view->visible) return 1;
    return player_preview_storage_valid(*view) && player_preview_geometry_valid(*view) &&
           player_preview_visual_layers_valid(*view) && player_preview_storyboards_valid(*view);
}

namespace {

struct PreviewCanvas {
    struct nk_context* context;
    struct SDL_Renderer* renderer;
    const wormedit::ui::player_preview_view_t* view;
    struct nk_rect viewport;
};

void render_preview_skin_buttons(struct nk_context* context,
                                 const wormedit::ui::player_preview_view_t* view,
                                 wormedit::ui::workspace_action_t* action) {
    nk_layout_row_dynamic(context, 24, 4);
    const struct {
        const char* label;
        wormedit::player_preview_skin_t skin;
    } skins[] = {{"Neutral", wormedit::player_preview_skin_t::neutral},
                 {"Mania", wormedit::player_preview_skin_t::mania},
                 {"Arrows", wormedit::player_preview_skin_t::arrows}};
    for (const auto& skin : skins) {
        if (nk_button_label(context, skin.label) && view->skin != skin.skin && action &&
            action->kind == WORM_UI_WORKSPACE_ACTION_NONE) {
            action->kind = WORM_UI_WORKSPACE_ACTION_SELECT_PLAYER_PREVIEW_SKIN;
            action->player_preview_skin = skin.skin;
        }
    }
    if (nk_button_label(context, "Hide") && action &&
        action->kind == WORM_UI_WORKSPACE_ACTION_NONE)
        action->kind = WORM_UI_WORKSPACE_ACTION_TOGGLE_PLAYER_PREVIEW;
}

const char* preview_visual_label(wormedit::visual_event_kind_t kind) {
    switch (kind) {
    case wormedit::visual_event_kind_t::background: return "Background";
    case wormedit::visual_event_kind_t::poor: return "Poor";
    case wormedit::visual_event_kind_t::layer: return "Layer";
    case wormedit::visual_event_kind_t::layer2: return "Layer 2";
    case wormedit::visual_event_kind_t::video: return "Video";
    case wormedit::visual_event_kind_t::storyboard: return "Storyboard";
    }
    return "Visual";
}

const char* preview_image_state(const wormedit::ui::player_preview_visual_layer_view_t& layer) {
    if (layer.image_ready) return " · RGBA ready";
    if (layer.diagnostic == wormedit::ui::player_preview_visual_diagnostic_t::video_frame_late)
        return " · late frame";
    if (layer.diagnostic == wormedit::ui::player_preview_visual_diagnostic_t::video_decode_failed)
        return " · decode failed";
    if (layer.image_pending) return " · loading";
    if (layer.static_image || layer.video_frame) return " · unavailable";
    return "";
}

void render_preview_visual_layer(struct nk_context* context,
                                 const wormedit::ui::player_preview_visual_layer_view_t& layer) {
    nk_layout_row_dynamic(context, 18, 1);
    const auto* label = preview_visual_label(layer.kind);
    const auto* image_state = preview_image_state(layer);
    if (layer.video_clock_ready) {
        nk_labelf(context, NK_TEXT_LEFT, "%s resource %llu · t=%llu/%u s%s", label,
                  static_cast<unsigned long long>(layer.resource_id.value),
                  static_cast<unsigned long long>(layer.video_time_numerator),
                  layer.video_time_denominator, image_state);
    } else {
        nk_labelf(context, NK_TEXT_LEFT, "%s resource %llu%s", label,
                  static_cast<unsigned long long>(layer.resource_id.value), image_state);
    }
    if (layer.diagnostic != wormedit::ui::player_preview_visual_diagnostic_t::none) {
        nk_layout_row_dynamic(context, 18, 1);
        nk_labelf(context, NK_TEXT_LEFT, "Event %llu: %s",
                  static_cast<unsigned long long>(layer.object_id.value), layer.diagnostic_text.data());
    }
}

void render_preview_status(struct nk_context* context,
                           const wormedit::ui::player_preview_view_t* view) {
    nk_layout_row_dynamic(context, 20, 1);
    if (!view->scene_available) {
        nk_label(context, !view->status_text.empty() ? view->status_text.c_str() : "Preview unavailable",
                 NK_TEXT_LEFT);
    } else {
        nk_labelf(context, NK_TEXT_LEFT, "Frame %llu · %zu notes · %zu sprites%s%s",
                  static_cast<unsigned long long>(view->sample_frame),
                  view->note_endpoints.size(), view->storyboard_sprites.size(),
                  view->pre_zero ? " · count-in" : "",
                  view->split_stage ? " · split stage" : "");
    }
    for (std::size_t index = 0; index < view->visual_layers.size(); ++index)
        render_preview_visual_layer(context, view->visual_layers[index]);
}

std::size_t preview_diagnostic_count(const wormedit::ui::player_preview_view_t& view) {
    std::size_t count{};
    for (std::size_t index = 0; index < view.visual_layers.size(); ++index)
        count += view.visual_layers[index].diagnostic !=
                 wormedit::ui::player_preview_visual_diagnostic_t::none;
    return count;
}

auto preview_viewport_height(const wormedit::ui::player_preview_view_t& view, float panel_height) -> float {
    const auto label_count = view.visual_layers.size() + preview_diagnostic_count(view);
    return std::max(180.0f, panel_height - 92.0f - static_cast<float>(label_count) * 18.0f);
}

void merge_preview_result(worm_ui_nuklear_preview_visual_result_t& result,
                          const worm_ui_nuklear_preview_visual_result_t& additional) {
    result.drew_image = result.drew_image || additional.drew_image;
    if (result.first_failure == worm_ui_nuklear_preview_visual_failure_t::none &&
        additional.first_failure != worm_ui_nuklear_preview_visual_failure_t::none) {
        result.first_failed_resource_id = additional.first_failed_resource_id;
        result.first_failure = additional.first_failure;
    }
    result.failure_count += additional.failure_count;
}

void draw_preview_lanes(const PreviewCanvas& canvas, bool background_visible) {
    const auto* view = canvas.view;
    for (std::size_t index = 0; index < view->lanes.size(); ++index) {
        const auto rect = screen_rect(&view->lanes[index].bounds, view, canvas.viewport);
        const auto alternate = (view->lanes[index].display_order & 1U) != 0;
        const auto alpha = static_cast<nk_byte>(background_visible ? 196 : 255);
        nk_fill_rect(canvas.context ? nk_window_get_canvas(canvas.context) : nullptr, rect, 0.0f,
                     alternate ? nk_rgba(34, 40, 54, alpha) : nk_rgba(28, 34, 47, alpha));
        nk_stroke_rect(nk_window_get_canvas(canvas.context), rect, 0.0f, 1.0f,
                       nk_rgb(67, 76, 96));
    }
}

void draw_preview_holds(struct nk_command_buffer* target, const PreviewCanvas& canvas) {
    for (std::size_t index = 0; index < canvas.view->hold_bodies.size(); ++index) {
        const auto& hold = canvas.view->hold_bodies[index];
        const auto rect = screen_rect(&hold.bounds, canvas.view, canvas.viewport);
        const auto color = hold.semantic_color_visible
                               ? nk_rgba(hold.semantic_red, hold.semantic_green,
                                         hold.semantic_blue, hold.semantic_alpha)
                               : nk_rgb(75, 148, 189);
        nk_fill_rect(target, rect, 0.0f, color);
    }
}

void draw_preview_receptors(struct nk_command_buffer* target, const PreviewCanvas& canvas) {
    for (std::size_t index = 0; index < canvas.view->receptors.size(); ++index) {
        const auto& receptor = canvas.view->receptors[index];
        const auto rect = screen_rect(&receptor.bounds, canvas.view, canvas.viewport);
        draw_glyph(target, rect, static_cast<std::uint8_t>(receptor.glyph), nk_rgb(94, 108, 137));
        nk_stroke_rect(target, rect, 2.0f, 1.5f, nk_rgb(218, 226, 242));
    }
}

void draw_preview_notes(struct nk_command_buffer* target, const PreviewCanvas& canvas) {
    for (std::size_t index = 0; index < canvas.view->note_endpoints.size(); ++index) {
        const auto& note = canvas.view->note_endpoints[index];
        const auto rect = screen_rect(&note.bounds, canvas.view, canvas.viewport);
        const auto color = note.semantic_color_visible
                               ? nk_rgba(note.semantic_red, note.semantic_green,
                                         note.semantic_blue, note.semantic_alpha)
                               : (note.tail ? nk_rgb(114, 205, 220) : nk_rgb(231, 184, 86));
        draw_glyph(target, rect, static_cast<std::uint8_t>(note.glyph), color);
        nk_stroke_rect(target, rect, 1.5f, 1.0f, nk_rgb(239, 245, 255));
    }
}

void draw_preview_warning(struct nk_command_buffer* target, const PreviewCanvas& canvas,
                          const worm_ui_nuklear_preview_visual_result_t& result) {
    if (result.failure_count == 0 || !canvas.context->style.font) return;
    char warning[192]{};
    std::snprintf(warning, sizeof(warning), "Visual resource %llu: %s%s",
                  static_cast<unsigned long long>(result.first_failed_resource_id),
                  worm_ui_nuklear_preview_visual_failure_message(result.first_failure),
                  result.failure_count > 1 ? " (additional failures)" : "");
    const auto bounds = nk_rect(canvas.viewport.x + 6.0f, canvas.viewport.y + 6.0f,
                                std::max(1.0f, canvas.viewport.w - 12.0f), 22.0f);
    const auto background = nk_rgba(78, 25, 31, 232);
    nk_fill_rect(target, bounds, 2.0f, background);
    nk_draw_text(target, bounds, warning, static_cast<int>(std::strlen(warning)),
                 canvas.context->style.font, background, nk_rgb(255, 218, 222));
}

void draw_preview_scene(const PreviewCanvas& canvas) {
    auto result = worm_ui_nuklear_draw_player_preview_visual_layers(
        nk_window_get_canvas(canvas.context), canvas.viewport, canvas.renderer, canvas.view);
    merge_preview_result(result, worm_ui_nuklear_draw_player_preview_storyboard_sprites(
                                    nk_window_get_canvas(canvas.context), canvas.viewport,
                                    canvas.renderer, canvas.view, false));
    auto* target = nk_window_get_canvas(canvas.context);
    draw_preview_lanes(canvas, result.drew_image);
    draw_preview_holds(target, canvas);
    draw_preview_receptors(target, canvas);
    draw_preview_notes(target, canvas);
    merge_preview_result(result, worm_ui_nuklear_draw_player_preview_storyboard_sprites(
                                    target, canvas.viewport, canvas.renderer, canvas.view, true));
    draw_preview_warning(target, canvas, result);
}

void draw_preview_canvas(struct nk_context* context, struct SDL_Renderer* renderer,
                         const wormedit::ui::player_preview_view_t* view, struct nk_rect viewport) {
    auto* target = nk_window_get_canvas(context);
    if (!target) return;
    const auto previous_clip = target->clip;
    nk_push_scissor(target, viewport);
    nk_fill_rect(target, viewport, 2.0f, nk_rgb(14, 17, 24));
    if (view->scene_available)
        draw_preview_scene({context, renderer, view, viewport});
    nk_push_scissor(target, previous_clip);
}

void render_preview_window_content(struct nk_context* context, struct SDL_Renderer* renderer,
                                   const wormedit::ui::player_preview_view_t* view,
                                   wormedit::ui::workspace_action_t* action, float panel_height) {
    render_preview_skin_buttons(context, view, action);
    render_preview_status(context, view);
    struct nk_rect viewport{};
    nk_layout_row_dynamic(context, preview_viewport_height(*view, panel_height), 1);
    if (nk_widget(&viewport, context) != NK_WIDGET_INVALID)
        draw_preview_canvas(context, renderer, view, viewport);
}

} // namespace

void worm_ui_nuklear_render_player_preview(struct nk_context* context,
                                           struct SDL_Renderer* renderer,
                                           const wormedit::ui::player_preview_view_t* view,
                                           wormedit::ui::workspace_action_t* action, int window_width,
                                           int window_height) {
    if (!context || !view || !view->visible) return;
    const auto panel_width = std::min(360.0f, std::max(260.0f, window_width * 0.32f));
    const auto panel_height = std::min(580.0f, std::max(300.0f, window_height - 90.0f));
    if (nk_begin(context, "Player preview",
                 nk_rect(static_cast<float>(window_width) - panel_width - 18.0f, 72.0f,
                         panel_width, panel_height),
                 NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE))
        render_preview_window_content(context, renderer, view, action, panel_height);
    nk_end(context);
}

void worm_ui_nuklear_shutdown_player_preview(void) {
    worm_ui_nuklear_shutdown_player_preview_textures();
}
