#include "ui/nuklear/player_preview.h"
#include "ui/nuklear/player_preview_texture.h"

#include "wormedit/nuklear_config.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace {

auto rect_valid(const WormUiPlayerPreviewRect* rect, const WormUiPlayerPreviewView* view) -> bool {
    constexpr double geometry_limit = 10'000'000.0;
    return rect && view && std::isfinite(rect->left) && std::isfinite(rect->top) &&
           std::isfinite(rect->right) && std::isfinite(rect->bottom) && rect->right >= rect->left &&
           rect->bottom >= rect->top && std::abs(rect->left) <= geometry_limit &&
           std::abs(rect->top) <= geometry_limit && std::abs(rect->right) <= geometry_limit &&
           std::abs(rect->bottom) <= geometry_limit;
}

auto screen_rect(const WormUiPlayerPreviewRect* source, const WormUiPlayerPreviewView* view,
                 struct nk_rect viewport) -> struct nk_rect {
    const auto scale_x = viewport.w / static_cast<float>(view->viewport_width_pixels);
    const auto scale_y = viewport.h / static_cast<float>(view->viewport_height_pixels);
    return nk_rect(viewport.x + static_cast<float>(source->left) * scale_x,
                   viewport.y + static_cast<float>(source->top) * scale_y,
                   static_cast<float>(source->right - source->left) * scale_x,
                   static_cast<float>(source->bottom - source->top) * scale_y);
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
    } else {
        nk_fill_rect(canvas, bounds, glyph == 1 ? 2.0f : 0.0f, color);
    }
}

} // namespace

int worm_ui_nuklear_player_preview_view_valid(const WormUiPlayerPreviewView* view) {
    if (!view || view->skin < WORM_UI_PLAYER_PREVIEW_SKIN_NEUTRAL ||
        view->skin > WORM_UI_PLAYER_PREVIEW_SKIN_ARROWS) {
        return 0;
    }
    if (!view->visible) {
        return 1;
    }
    if (!std::isfinite(view->viewport_width_pixels) || view->viewport_width_pixels <= 0.0 ||
        !std::isfinite(view->viewport_height_pixels) || view->viewport_height_pixels <= 0.0 ||
        view->lane_count > WORM_UI_PLAYER_PREVIEW_LANE_CAPACITY ||
        view->receptor_count > WORM_UI_PLAYER_PREVIEW_LANE_CAPACITY ||
        view->hold_body_count > WORM_UI_PLAYER_PREVIEW_HOLD_CAPACITY ||
        view->note_endpoint_count > WORM_UI_PLAYER_PREVIEW_NOTE_CAPACITY ||
        view->visual_layer_count > WORM_UI_PLAYER_PREVIEW_VISUAL_LAYER_CAPACITY ||
        (view->lane_count != 0 && !view->lanes) ||
        (view->receptor_count != 0 && !view->receptors) ||
        (view->hold_body_count != 0 && !view->hold_bodies) ||
        (view->note_endpoint_count != 0 && !view->note_endpoints) ||
        (view->visual_layer_count != 0 && !view->visual_layers) ||
        (!view->scene_available && !view->status_text) ||
        (view->scene_available && (view->project_id == 0 || view->document_revision == 0))) {
        return 0;
    }
    for (std::size_t index = 0; index < view->lane_count; ++index) {
        if (view->lanes[index].lane_id == 0 || !rect_valid(&view->lanes[index].bounds, view)) {
            return 0;
        }
    }
    for (std::size_t index = 0; index < view->receptor_count; ++index) {
        if (view->receptors[index].lane_id == 0 ||
            !rect_valid(&view->receptors[index].bounds, view)) {
            return 0;
        }
    }
    for (std::size_t index = 0; index < view->hold_body_count; ++index) {
        if (view->hold_bodies[index].note_id == 0 ||
            !rect_valid(&view->hold_bodies[index].bounds, view)) {
            return 0;
        }
    }
    for (std::size_t index = 0; index < view->note_endpoint_count; ++index) {
        if (view->note_endpoints[index].note_id == 0 ||
            !rect_valid(&view->note_endpoints[index].bounds, view)) {
            return 0;
        }
    }
    for (std::size_t index = 0; index < view->visual_layer_count; ++index) {
        const auto& layer = view->visual_layers[index];
        if (layer.object_id == 0 || layer.resource_id == 0 ||
            layer.event_sample_frame > view->sample_frame ||
            layer.kind < WORM_UI_PLAYER_PREVIEW_VISUAL_BACKGROUND ||
            layer.kind > WORM_UI_PLAYER_PREVIEW_VISUAL_STORYBOARD ||
            (layer.image_ready &&
             (!layer.static_image || layer.image_pending || layer.image_width == 0 ||
              layer.image_height == 0 || !layer.rgba_pixels ||
              layer.image_width > SIZE_MAX / layer.image_height / 4U ||
              layer.rgba_byte_count !=
                  static_cast<std::size_t>(layer.image_width) * layer.image_height * 4U)) ||
            (!layer.image_ready && (layer.image_width != 0 || layer.image_height != 0 ||
                                    layer.rgba_pixels != nullptr || layer.rgba_byte_count != 0))) {
            return 0;
        }
    }
    return 1;
}

void worm_ui_nuklear_render_player_preview(struct nk_context* context,
                                           const WormUiPlayerPreviewView* view,
                                           WormUiWorkspaceAction* action, int window_width,
                                           int window_height) {
    if (!context || !view || !view->visible) {
        return;
    }
    const auto panel_width = std::min(360.0f, std::max(260.0f, window_width * 0.32f));
    const auto panel_height = std::min(580.0f, std::max(300.0f, window_height - 90.0f));
    if (nk_begin(context, "Player preview",
                 nk_rect(static_cast<float>(window_width) - panel_width - 18.0f, 72.0f, panel_width,
                         panel_height),
                 NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE)) {
        nk_layout_row_dynamic(context, 24, 4);
        const struct {
            const char* label;
            WormUiPlayerPreviewSkin skin;
        } skins[] = {{"Neutral", WORM_UI_PLAYER_PREVIEW_SKIN_NEUTRAL},
                     {"Mania", WORM_UI_PLAYER_PREVIEW_SKIN_MANIA},
                     {"Arrows", WORM_UI_PLAYER_PREVIEW_SKIN_ARROWS}};
        for (const auto& skin : skins) {
            if (nk_button_label(context, skin.label) && view->skin != skin.skin && action &&
                action->kind == WORM_UI_WORKSPACE_ACTION_NONE) {
                action->kind = WORM_UI_WORKSPACE_ACTION_SELECT_PLAYER_PREVIEW_SKIN;
                action->player_preview_skin = skin.skin;
            }
        }
        if (nk_button_label(context, "Hide") && action &&
            action->kind == WORM_UI_WORKSPACE_ACTION_NONE) {
            action->kind = WORM_UI_WORKSPACE_ACTION_TOGGLE_PLAYER_PREVIEW;
        }
        nk_layout_row_dynamic(context, 20, 1);
        if (!view->scene_available) {
            nk_label(context, view->status_text ? view->status_text : "Preview unavailable",
                     NK_TEXT_LEFT);
        } else {
            nk_labelf(context, NK_TEXT_LEFT, "Frame %llu · %zu notes%s%s",
                      static_cast<unsigned long long>(view->sample_frame),
                      view->note_endpoint_count, view->pre_zero ? " · count-in" : "",
                      view->split_stage ? " · split stage" : "");
        }
        for (std::size_t index = 0; index < view->visual_layer_count; ++index) {
            const auto& layer = view->visual_layers[index];
            const char* label = "Visual";
            switch (layer.kind) {
            case WORM_UI_PLAYER_PREVIEW_VISUAL_BACKGROUND:
                label = "Background";
                break;
            case WORM_UI_PLAYER_PREVIEW_VISUAL_POOR:
                label = "Poor";
                break;
            case WORM_UI_PLAYER_PREVIEW_VISUAL_LAYER:
                label = "Layer";
                break;
            case WORM_UI_PLAYER_PREVIEW_VISUAL_VIDEO:
                label = "Video";
                break;
            case WORM_UI_PLAYER_PREVIEW_VISUAL_STORYBOARD:
                label = "Storyboard";
                break;
            }
            nk_layout_row_dynamic(context, 18, 1);
            const char* image_state = layer.image_ready     ? " · RGBA ready"
                                      : layer.image_pending ? " · loading"
                                      : layer.static_image  ? " · unavailable"
                                                            : "";
            nk_labelf(context, NK_TEXT_LEFT, "%s resource %llu%s", label,
                      static_cast<unsigned long long>(layer.resource_id), image_state);
        }
        struct nk_rect viewport{};
        const auto visual_label_height = static_cast<float>(view->visual_layer_count) * 18.0f;
        nk_layout_row_dynamic(context, std::max(180.0f, panel_height - 92.0f - visual_label_height),
                              1);
        if (nk_widget(&viewport, context) != NK_WIDGET_INVALID) {
            auto* canvas = nk_window_get_canvas(context);
            if (canvas) {
                const auto previous_clip = canvas->clip;
                nk_push_scissor(canvas, viewport);
                nk_fill_rect(canvas, viewport, 2.0f, nk_rgb(14, 17, 24));
                if (view->scene_available) {
                    const auto visual_composed =
                        worm_ui_nuklear_draw_player_preview_visual_layers(canvas, viewport, view);
                    for (std::size_t index = 0; index < view->lane_count; ++index) {
                        const auto rect = screen_rect(&view->lanes[index].bounds, view, viewport);
                        const auto alternate = (view->lanes[index].display_order & 1U) != 0;
                        const auto alpha = static_cast<nk_byte>(visual_composed ? 196 : 255);
                        nk_fill_rect(canvas, rect, 0.0f,
                                     alternate ? nk_rgba(34, 40, 54, alpha)
                                               : nk_rgba(28, 34, 47, alpha));
                        nk_stroke_rect(canvas, rect, 0.0f, 1.0f, nk_rgb(67, 76, 96));
                    }
                    for (std::size_t index = 0; index < view->hold_body_count; ++index) {
                        const auto& hold = view->hold_bodies[index];
                        const auto rect = screen_rect(&hold.bounds, view, viewport);
                        const auto color = hold.semantic_color_visible
                                               ? nk_rgba(hold.semantic_red, hold.semantic_green,
                                                         hold.semantic_blue, hold.semantic_alpha)
                                               : nk_rgb(75, 148, 189);
                        nk_fill_rect(canvas, rect, 0.0f, color);
                    }
                    for (std::size_t index = 0; index < view->receptor_count; ++index) {
                        const auto& receptor = view->receptors[index];
                        const auto rect = screen_rect(&receptor.bounds, view, viewport);
                        draw_glyph(canvas, rect, receptor.glyph, nk_rgb(94, 108, 137));
                        nk_stroke_rect(canvas, rect, 2.0f, 1.5f, nk_rgb(218, 226, 242));
                    }
                    for (std::size_t index = 0; index < view->note_endpoint_count; ++index) {
                        const auto& note = view->note_endpoints[index];
                        const auto rect = screen_rect(&note.bounds, view, viewport);
                        const auto color =
                            note.semantic_color_visible
                                ? nk_rgba(note.semantic_red, note.semantic_green,
                                          note.semantic_blue, note.semantic_alpha)
                                : (note.tail ? nk_rgb(114, 205, 220) : nk_rgb(231, 184, 86));
                        draw_glyph(canvas, rect, note.glyph, color);
                        nk_stroke_rect(canvas, rect, 1.5f, 1.0f, nk_rgb(239, 245, 255));
                    }
                }
                nk_push_scissor(canvas, previous_clip);
            }
        }
    }
    nk_end(context);
}

void worm_ui_nuklear_shutdown_player_preview(void) {
    worm_ui_nuklear_shutdown_player_preview_textures();
}
