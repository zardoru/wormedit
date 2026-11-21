#include "ui/nuklear/audio/player_preview.h"
#include "ui/nuklear/audio/player_preview_texture.h"
#include "ui/nuklear/render/arrow_glyph.h"

#include "wormedit/nuklear_config.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "ui/nuklear/chrome/chrome.h"
#include "ui/nuklear/chrome/ui_metrics.h"

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
        view->skin > wormedit::player_preview_skin_t::squared ||
        view->layout > wormedit::player_preview_layout_t::focus ||
        view->content > wormedit::player_preview_content_t::bga_only ||
        view->bga_side > wormedit::player_preview_bga_side_t::right ||
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
    // Where the note field and the BGA are drawn inside `viewport`. They coincide in the floating
    // window; elsewhere the field keeps the scene's aspect ratio so notes keep their proportions.
    struct nk_rect field;
    struct nk_rect bga;
    bool draw_field{true};
};

auto aspect_field_width(const wormedit::ui::player_preview_view_t& view, float height) -> float {
    return height * static_cast<float>(view.viewport_width_pixels / view.viewport_height_pixels);
}

auto make_preview_canvas(struct nk_context* context, struct SDL_Renderer* renderer,
                         const wormedit::ui::player_preview_view_t* view, struct nk_rect area)
    -> PreviewCanvas {
    PreviewCanvas canvas{context, renderer, view, area, area, area, true};
    if (view->content == wormedit::player_preview_content_t::bga_only) {
        canvas.draw_field = false;
        return canvas;
    }
    if (view->layout == wormedit::player_preview_layout_t::floating) return canvas;
    const auto field_width = std::min(area.w, aspect_field_width(*view, area.h));
    if (view->layout == wormedit::player_preview_layout_t::focus) {
        canvas.field = nk_rect(area.x + (area.w - field_width) * 0.5f, area.y, field_width, area.h);
        return canvas;
    }
    // Tall windows: keep a quarter of the width for the BGA and shrink the field in proportion.
    const auto full_width = std::min(field_width, area.w * 0.75f);
    const auto full_height = full_width < field_width ? area.h * full_width / field_width : area.h;
    const auto bga_width = std::max(0.0f, area.w - full_width);
    const auto bga_right = view->bga_side == wormedit::player_preview_bga_side_t::right;
    canvas.field = nk_rect(bga_right ? area.x : area.x + bga_width,
                           area.y + (area.h - full_height) * 0.5f, full_width, full_height);
    canvas.bga = nk_rect(bga_right ? area.x + field_width : area.x, area.y, bga_width, area.h);
    return canvas;
}

// Corner rounding of note and receptor outlines; the Square skin has none.
auto preview_outline_radius(const wormedit::ui::player_preview_view_t& view, float rounded) -> float {
    return view.skin == wormedit::player_preview_skin_t::squared ? 0.0f : rounded;
}

// The four skins, layouts, contents and BGA sides, in the order of their enum values.
constexpr const char* skin_labels[] = {"Rounded", "Mania", "Arrows", "Square"};
constexpr const char* layout_labels[] = {"Window", "Full screen", "Focus"};
constexpr const char* content_labels[] = {"Play", "BGA only"};
constexpr const char* side_labels[] = {"BGA left", "BGA right"};

auto preview_settings_of(const wormedit::ui::player_preview_view_t& view)
    -> wormedit::player_preview_settings_t {
    wormedit::player_preview_settings_t settings{view.skin};
    settings.layout = view.layout;
    settings.content = view.content;
    settings.bga_side = view.bga_side;
    return settings;
}

struct PreviewControls {
    struct nk_context* context;
    const wormedit::ui::player_preview_view_t* view;
    WormUiWorkspaceActionKind* kind;
    wormedit::player_preview_settings_t* request;
    wormedit::player_preview_settings_t chosen;
    bool changed{};

    template <typename Value, std::size_t Count>
    void segmented(const char* const (&labels)[Count], Value& field) {
        const auto selected = static_cast<int>(field);
        const auto picked = wormedit::ui::chrome::Segmented{labels, static_cast<int>(Count), selected}
                                .render(context);
        if (picked != selected) {
            field = static_cast<Value>(picked);
            changed = true;
        }
    }

    void hide_button() {
        if (wormedit::ui::chrome::button_label(context, "Hide") && kind &&
            *kind == WORM_UI_WORKSPACE_ACTION_NONE)
            *kind = WORM_UI_WORKSPACE_ACTION_TOGGLE_PLAYER_PREVIEW;
    }

    void publish() const {
        if (changed && kind && *kind == WORM_UI_WORKSPACE_ACTION_NONE && request) {
            *kind = WORM_UI_WORKSPACE_ACTION_SET_PLAYER_PREVIEW_SETTINGS;
            *request = chosen;
        }
    }
};

void render_preview_controls(struct nk_context* context,
                             const wormedit::ui::player_preview_view_t* view,
                             WormUiWorkspaceActionKind* kind,
                             wormedit::player_preview_settings_t* settings_request) {
    PreviewControls controls{context, view, kind, settings_request, preview_settings_of(*view)};
    const auto compact = view->layout != wormedit::player_preview_layout_t::floating;
    if (compact) {
        // One row over the picture, so a full-screen preview keeps almost all of its height.
        nk_layout_row_begin(context, NK_DYNAMIC, wormedit::ui::metrics::control_compact, 5);
        nk_layout_row_push(context, 0.30f);
        controls.segmented(skin_labels, controls.chosen.skin);
        nk_layout_row_push(context, 0.26f);
        controls.segmented(layout_labels, controls.chosen.layout);
        nk_layout_row_push(context, 0.16f);
        controls.segmented(content_labels, controls.chosen.content);
        nk_layout_row_push(context, 0.18f);
        controls.segmented(side_labels, controls.chosen.bga_side);
        nk_layout_row_push(context, 0.10f);
        controls.hide_button();
        nk_layout_row_end(context);
    } else {
        nk_layout_row_dynamic(context, wormedit::ui::metrics::control_compact, 1);
        controls.segmented(skin_labels, controls.chosen.skin);
        nk_layout_row_dynamic(context, wormedit::ui::metrics::control_compact, 1);
        controls.segmented(layout_labels, controls.chosen.layout);
        nk_layout_row_dynamic(context, wormedit::ui::metrics::control_compact, 2);
        controls.segmented(content_labels, controls.chosen.content);
        controls.hide_button();
    }
    controls.publish();
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
    nk_layout_row_dynamic(context, wormedit::ui::metrics::row_text, 1);
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
        nk_layout_row_dynamic(context, wormedit::ui::metrics::row_text, 1);
        nk_labelf(context, NK_TEXT_LEFT, "Event %llu: %s",
                  static_cast<unsigned long long>(layer.object_id.value), layer.diagnostic_text.data());
    }
}

void render_preview_status(struct nk_context* context,
                           const wormedit::ui::player_preview_view_t* view) {
    nk_layout_row_dynamic(context, wormedit::ui::metrics::row_text, 1);
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
        const auto rect = screen_rect(&view->lanes[index].bounds, view, canvas.field);
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
        const auto rect = screen_rect(&hold.bounds, canvas.view, canvas.field);
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
        const auto rect = screen_rect(&receptor.bounds, canvas.view, canvas.field);
        draw_glyph(target, rect, static_cast<std::uint8_t>(receptor.glyph), nk_rgb(94, 108, 137));
        nk_stroke_rect(target, rect, preview_outline_radius(*canvas.view, 2.0f), 1.5f, nk_rgb(218, 226, 242));
    }
}

void draw_preview_notes(struct nk_command_buffer* target, const PreviewCanvas& canvas) {
    for (std::size_t index = 0; index < canvas.view->note_endpoints.size(); ++index) {
        const auto& note = canvas.view->note_endpoints[index];
        const auto rect = screen_rect(&note.bounds, canvas.view, canvas.field);
        const auto color = note.semantic_color_visible
                               ? nk_rgba(note.semantic_red, note.semantic_green,
                                         note.semantic_blue, note.semantic_alpha)
                               : (note.tail ? nk_rgb(114, 205, 220) : nk_rgb(231, 184, 86));
        draw_glyph(target, rect, static_cast<std::uint8_t>(note.glyph), color);
        nk_stroke_rect(target, rect, preview_outline_radius(*canvas.view, 1.5f), 1.0f, nk_rgb(239, 245, 255));
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

auto clip_to(struct nk_rect outer, struct nk_rect inner) -> struct nk_rect {
    const auto left = std::max(outer.x, inner.x);
    const auto top = std::max(outer.y, inner.y);
    const auto right = std::min(outer.x + outer.w, inner.x + inner.w);
    const auto bottom = std::min(outer.y + outer.h, inner.y + inner.h);
    return nk_rect(left, top, std::max(0.0f, right - left), std::max(0.0f, bottom - top));
}

void draw_preview_scene(const PreviewCanvas& canvas) {
    auto result = worm_ui_nuklear_draw_player_preview_visual_layers(
        nk_window_get_canvas(canvas.context), canvas.bga, canvas.renderer, canvas.view);
    auto* target = nk_window_get_canvas(canvas.context);
    // Storyboard sprites belong to the BGA panel: they never spill onto the note field.
    const auto field_clip = target->clip;
    nk_push_scissor(target, clip_to(field_clip, canvas.bga));
    merge_preview_result(result, worm_ui_nuklear_draw_player_preview_storyboard_sprites(
                                    target, canvas.bga, canvas.renderer, canvas.view, false));
    nk_push_scissor(target, field_clip);
    if (canvas.draw_field) {
        draw_preview_lanes(canvas, result.drew_image);
        draw_preview_holds(target, canvas);
        draw_preview_receptors(target, canvas);
        draw_preview_notes(target, canvas);
    }
    nk_push_scissor(target, clip_to(field_clip, canvas.bga));
    merge_preview_result(result, worm_ui_nuklear_draw_player_preview_storyboard_sprites(
                                    target, canvas.bga, canvas.renderer, canvas.view, true));
    nk_push_scissor(target, field_clip);
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
        draw_preview_scene(make_preview_canvas(context, renderer, view, viewport));
    nk_push_scissor(target, previous_clip);
}

void render_preview_window_content(struct nk_context* context, struct SDL_Renderer* renderer,
                                   const wormedit::ui::player_preview_view_t* view,
                                   WormUiWorkspaceActionKind* kind,
                                   wormedit::player_preview_settings_t* settings_request, float panel_height) {
    render_preview_controls(context, view, kind, settings_request);
    render_preview_status(context, view);
    struct nk_rect viewport{};
    nk_layout_row_dynamic(context, preview_viewport_height(*view, panel_height), 1);
    if (nk_widget(&viewport, context) != NK_WIDGET_INVALID)
        draw_preview_canvas(context, renderer, view, viewport);
}

// Full-screen and focus layouts: the picture fills the window and one row of controls floats
// over its top edge.
void render_preview_immersive_content(struct nk_context* context, struct SDL_Renderer* renderer,
                                      const wormedit::ui::player_preview_view_t* view,
                                      WormUiWorkspaceActionKind* kind,
                                      wormedit::player_preview_settings_t* settings_request,
                                      struct nk_rect area) {
    draw_preview_canvas(context, renderer, view, area);
    render_preview_controls(context, view, kind, settings_request);
    if (!view->scene_available) {
        nk_layout_row_dynamic(context, wormedit::ui::metrics::row_text, 1);
        nk_label(context, !view->status_text.empty() ? view->status_text.c_str() : "Preview unavailable",
                 NK_TEXT_LEFT);
    }
}

} // namespace

void worm_ui_nuklear_render_player_preview(struct nk_context* context,
                                           struct SDL_Renderer* renderer,
                                           const wormedit::ui::player_preview_view_t* view,
                                           WormUiWorkspaceActionKind* kind,
                                           wormedit::player_preview_settings_t* settings_request,
                                           int window_width, int window_height) {
    if (!context || !view || !view->visible) return;
    static constexpr const char* window_name = "Player preview";
    // The floating window keeps its own place while a full-window layout borrows the same window.
    static struct nk_rect floating_bounds{};
    static bool had_floating_bounds{};
    static bool was_immersive{};
    const auto immersive = view->layout != wormedit::player_preview_layout_t::floating;
    if (immersive) {
        // Below the title bar: SDL claims clicks in its drag zone before Nuklear sees them.
        const auto top = static_cast<float>(worm_ui_frontend_title_bar_height());
        const auto area = nk_rect(0.0f, top, static_cast<float>(window_width),
                                  std::max(1.0f, static_cast<float>(window_height) - top));
        nk_window_set_bounds(context, window_name, area);
        if (nk_begin(context, window_name, area, NK_WINDOW_NO_SCROLLBAR))
            render_preview_immersive_content(context, renderer, view, kind, settings_request, area);
        nk_end(context);
        was_immersive = true;
        return;
    }
    const auto panel_width = std::min(360.0f, std::max(260.0f, window_width * 0.32f));
    const auto panel_height = std::min(580.0f, std::max(300.0f, window_height - 90.0f));
    const auto default_bounds = nk_rect(static_cast<float>(window_width) - panel_width - 18.0f, 72.0f,
                                        panel_width, panel_height);
    if (was_immersive && had_floating_bounds) nk_window_set_bounds(context, window_name, floating_bounds);
    was_immersive = false;
    if (nk_begin(context, window_name, default_bounds,
                 NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE)) {
        render_preview_window_content(context, renderer, view, kind, settings_request, panel_height);
        floating_bounds = nk_window_get_bounds(context);
        had_floating_bounds = true;
    }
    nk_end(context);
}

void worm_ui_nuklear_shutdown_player_preview(void) {
    worm_ui_nuklear_shutdown_player_preview_textures();
}
