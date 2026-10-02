#include "ui/nuklear/audio_panel.h"
#include "ui/nuklear/audio_state.h"

#include "wormedit/nuklear_config.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>

namespace {

auto& audio = worm_ui_nuklear_audio_state();

void cancel_offset_pointer_drag() {
    audio.offset_drag_active = false;
    audio.offset_drag_anchor_x = 0.0f;
    audio.offset_drag_width = 0.0f;
    audio.offset_drag_chart_id = 0;
    audio.offset_drag_document_revision = 0;
    audio.offset_drag_asset_id = 0;
    audio.offset_drag_total_frames = 0;
    audio.offset_drag_sample_rate = 0;
    audio.offset_drag_delta_frames = 0;
}

auto offset_pointer_drag_matches(const WormUiAudioSlicesView* view) -> bool {
    return view && audio.offset_drag_active && view->visible && view->waveform_available &&
           view->has_chart && view->has_selected_asset && view->offset_drag_active &&
           !view->timing_locked && view->chart_id == audio.offset_drag_chart_id &&
           view->document_revision == audio.offset_drag_document_revision &&
           view->selected_asset_id == audio.offset_drag_asset_id &&
           view->total_frames == audio.offset_drag_total_frames &&
           view->sample_rate == audio.offset_drag_sample_rate;
}

auto offset_pointer_delta(float pointer_x) -> std::int64_t {
    if (audio.offset_drag_width <= 0.0f || audio.offset_drag_total_frames == 0) {
        return 0;
    }
    const auto frames = (static_cast<long double>(pointer_x) - audio.offset_drag_anchor_x) /
                        audio.offset_drag_width *
                        static_cast<long double>(audio.offset_drag_total_frames);
    const auto minimum = static_cast<long double>(std::numeric_limits<std::int64_t>::min());
    const auto maximum = static_cast<long double>(std::numeric_limits<std::int64_t>::max());
    if (frames <= minimum) {
        return std::numeric_limits<std::int64_t>::min();
    }
    if (frames >= maximum) {
        return std::numeric_limits<std::int64_t>::max();
    }
    return static_cast<std::int64_t>(std::round(frames));
}

void set_offset_drag_action(WormUiWorkspaceAction* action, WormUiAudioOffsetDragAction kind,
                            std::int64_t delta_frames) {
    if (!action || action->audio_offset_drag_action != WORM_UI_AUDIO_OFFSET_DRAG_NONE) {
        return;
    }
    action->audio_offset_drag_action = kind;
    action->audio_offset_drag_chart_id = audio.offset_drag_chart_id;
    action->audio_offset_drag_document_revision = audio.offset_drag_document_revision;
    action->audio_offset_drag_sample_rate = audio.offset_drag_sample_rate;
    action->audio_offset_drag_delta_frames = delta_frames;
}

void render_audio_asset_catalog(struct nk_context* context, const WormUiAudioSlicesView& view,
                                WormUiWorkspaceAction& action) {
    nk_layout_row_dynamic(context, 22, 1);
    if (view.asset_count == 0) {
        nk_label(context, "No audio assets are registered in this project.", NK_TEXT_LEFT);
        return;
    }
    nk_label(context, "Source assets", NK_TEXT_LEFT);
    constexpr int buttons_per_row = 4;
    for (std::size_t begin = 0; begin < view.asset_count; begin += buttons_per_row) {
        nk_layout_row_dynamic(context, 26, buttons_per_row);
        for (std::size_t slot = 0; slot < buttons_per_row; ++slot) {
            const auto index = begin + slot;
            if (index >= view.asset_count) {
                nk_label(context, "", NK_TEXT_LEFT);
                continue;
            }
            char label[48]{};
            const bool selected = view.has_selected_asset &&
                                  view.selected_asset_id == view.asset_ids[index];
            std::snprintf(label, sizeof(label), "%sAsset %llu", selected ? "• " : "",
                          static_cast<unsigned long long>(view.asset_ids[index]));
            if (nk_button_label(context, label) && !selected) {
                action.kind = WORM_UI_WORKSPACE_ACTION_SELECT_AUDIO_ASSET;
                action.audio_asset_id = view.asset_ids[index];
            }
        }
    }
    if (view.assets_truncated) {
        nk_layout_row_dynamic(context, 18, 1);
        nk_label(context, "Additional audio assets are hidden by the view cap.", NK_TEXT_LEFT);
    }
}

void update_offset_pointer_drag(struct nk_context* context, const WormUiAudioSlicesView& view,
                                WormUiWorkspaceAction& action, struct nk_rect bounds) {
    const bool can_drag_offset = view.has_chart && !view.timing_locked;
    const auto& left_button = context->input.mouse.buttons[NK_BUTTON_LEFT];
    const bool left_released = nk_input_is_mouse_released(&context->input, NK_BUTTON_LEFT);
    if (audio.offset_drag_active) {
        const auto delta = offset_pointer_delta(context->input.mouse.pos.x);
        audio.offset_drag_delta_frames = delta;
        if (left_released) {
            set_offset_drag_action(&action, WORM_UI_AUDIO_OFFSET_DRAG_COMMIT, delta);
            cancel_offset_pointer_drag();
        } else if (!left_button.down) {
            set_offset_drag_action(&action, WORM_UI_AUDIO_OFFSET_DRAG_CANCEL, delta);
            cancel_offset_pointer_drag();
        } else if (nk_input_is_mouse_moved(&context->input)) {
            set_offset_drag_action(&action, WORM_UI_AUDIO_OFFSET_DRAG_UPDATE, delta);
        }
    } else if (can_drag_offset &&
               action.audio_offset_drag_action == WORM_UI_AUDIO_OFFSET_DRAG_NONE &&
               nk_input_is_mouse_click_in_rect(&context->input, NK_BUTTON_LEFT, bounds)) {
        audio.offset_drag_active = true;
        audio.offset_drag_anchor_x = context->input.mouse.pos.x;
        audio.offset_drag_width = bounds.w;
        audio.offset_drag_chart_id = view.chart_id;
        audio.offset_drag_document_revision = view.document_revision;
        audio.offset_drag_asset_id = view.selected_asset_id;
        audio.offset_drag_total_frames = view.total_frames;
        audio.offset_drag_sample_rate = view.sample_rate;
        audio.offset_drag_delta_frames = 0;
        set_offset_drag_action(&action, WORM_UI_AUDIO_OFFSET_DRAG_BEGIN, 0);
    }
}

void cancel_stale_offset_drag(const WormUiAudioSlicesView& view,
                              WormUiWorkspaceAction& action, bool window_focused) {
    if (audio.offset_drag_active &&
        (!offset_pointer_drag_matches(&view) || audio.offset_escape_pending ||
         audio.offset_cancel_pending || !window_focused)) {
        set_offset_drag_action(&action, WORM_UI_AUDIO_OFFSET_DRAG_CANCEL,
                               audio.offset_drag_delta_frames);
        cancel_offset_pointer_drag();
        audio.offset_cancel_pending = false;
    }
}

} // namespace

int worm_ui_nuklear_audio_slices_view_valid(const WormUiAudioSlicesView* view) {
    if (!view || view->asset_count > WORM_UI_AUDIO_ASSET_CAPACITY ||
        (view->asset_count != 0 && !view->asset_ids) ||
        view->waveform_column_count > WORM_UI_AUDIO_WAVEFORM_COLUMN_CAPACITY ||
        (view->waveform_column_count != 0 && !view->waveform_columns) ||
        (view->has_selected_asset && view->selected_asset_id == 0) ||
        (view->analysis_pending && view->waveform_available) ||
        (!view->waveform_available && view->waveform_column_count != 0) ||
        (view->waveform_available && (view->waveform_column_count == 0 || view->total_frames == 0 ||
                                      view->sample_rate == 0 || !view->has_selected_asset)) ||
        (view->has_chart && (view->chart_id == 0 || view->offset_denominator == 0)) ||
        (view->offset_drag_active &&
         (!view->has_chart || !view->waveform_available || view->timing_locked ||
          view->proposed_offset_denominator == 0))) {
        return 0;
    }
    for (std::size_t index = 0; index < view->asset_count; ++index) {
        if (view->asset_ids[index] == 0) {
            return 0;
        }
    }
    for (std::size_t index = 0; index < view->waveform_column_count; ++index) {
        for (std::size_t channel = 0; channel < 2; ++channel) {
            const auto& column = view->waveform_columns[index];
            if (!std::isfinite(column.minimum[channel]) ||
                !std::isfinite(column.maximum[channel]) ||
                !std::isfinite(column.absolute_peak[channel]) ||
                !std::isfinite(column.rms[channel]) ||
                column.minimum[channel] > column.maximum[channel] ||
                column.absolute_peak[channel] < 0.0f || column.rms[channel] < 0.0f) {
                return 0;
            }
        }
    }
    return 1;
}

void worm_ui_nuklear_render_audio_slices(struct nk_context* context,
                                         const WormUiAudioSlicesView* view,
                                         WormUiWorkspaceAction* action, bool show_asset_catalog,
                                         bool window_focused) {
    if (!context || !view || !action) {
        return;
    }
    cancel_stale_offset_drag(*view, *action, window_focused);
    if (show_asset_catalog) {
        render_audio_asset_catalog(context, *view, *action);
    }

    nk_layout_row_dynamic(context, 22, 1);
    if (!view->has_selected_asset) {
        nk_label(context, "Select an asset to build its waveform overview.", NK_TEXT_LEFT);
        return;
    }
    if (view->analysis_pending) {
        nk_labelf(context, NK_TEXT_LEFT, "Analyzing asset %llu…",
                  static_cast<unsigned long long>(view->selected_asset_id));
        return;
    }
    if (!view->waveform_available) {
        nk_label(context, "Waveform unavailable for the selected asset.", NK_TEXT_LEFT);
        return;
    }

    const auto seconds = static_cast<double>(view->total_frames) / view->sample_rate;
    nk_labelf(context, NK_TEXT_LEFT, "Stereo overview · %.2f seconds · %u Hz", seconds,
              view->sample_rate);
    nk_layout_row_dynamic(context, 190, 1);
    struct nk_rect bounds{};
    if (nk_widget(&bounds, context) == NK_WIDGET_INVALID || bounds.w <= 1.0f || bounds.h <= 1.0f) {
        return;
    }
    update_offset_pointer_drag(context, *view, *action, bounds);
    auto* canvas = nk_window_get_canvas(context);
    if (!canvas) {
        return;
    }
    const auto parent_clip = canvas->clip;
    // Clip to the visible part of the widget: the page scrolls, and a scissor of the full
    // bounds would paint the waveform over the tabs above the content group.
    const auto clip_left = std::max(bounds.x, parent_clip.x);
    const auto clip_top = std::max(bounds.y, parent_clip.y);
    const auto clip_right = std::min(bounds.x + bounds.w, parent_clip.x + parent_clip.w);
    const auto clip_bottom = std::min(bounds.y + bounds.h, parent_clip.y + parent_clip.h);
    nk_push_scissor(canvas, nk_rect(clip_left, clip_top, std::max(0.0f, clip_right - clip_left),
                                    std::max(0.0f, clip_bottom - clip_top)));
    nk_fill_rect(canvas, bounds, 2.0f, nk_rgb(22, 25, 33));
    const auto channel_height = bounds.h * 0.5f;
    const auto column_width = bounds.w / static_cast<float>(view->waveform_column_count);
    const auto displayed_delta = audio.offset_drag_active   ? audio.offset_drag_delta_frames
                                 : view->offset_drag_active ? view->offset_drag_delta_frames
                                                            : 0;
    const auto waveform_shift = static_cast<float>(static_cast<long double>(displayed_delta) /
                                                   view->total_frames * bounds.w);
    for (std::size_t channel = 0; channel < 2; ++channel) {
        const auto center = bounds.y + channel_height * (static_cast<float>(channel) + 0.5f);
        const auto amplitude = channel_height * 0.42f;
        nk_stroke_line(canvas, bounds.x, center, bounds.x + bounds.w, center, 1.0f,
                       nk_rgb(57, 63, 77));
        for (std::size_t index = 0; index < view->waveform_column_count; ++index) {
            const auto& column = view->waveform_columns[index];
            const auto x =
                bounds.x + (static_cast<float>(index) + 0.5f) * column_width + waveform_shift;
            const auto minimum = std::clamp(column.minimum[channel], -1.0f, 1.0f);
            const auto maximum = std::clamp(column.maximum[channel], -1.0f, 1.0f);
            const auto rms = std::clamp(column.rms[channel], 0.0f, 1.0f);
            nk_stroke_line(canvas, x, center - rms * amplitude, x, center + rms * amplitude,
                           std::max(1.0f, column_width), nk_rgb(59, 122, 147));
            nk_stroke_line(canvas, x, center - maximum * amplitude, x, center - minimum * amplitude,
                           std::max(1.0f, column_width), nk_rgb(107, 214, 235));
        }
    }
    if (audio.offset_drag_active || view->offset_drag_active) {
        nk_stroke_line(canvas, audio.offset_drag_anchor_x, bounds.y, audio.offset_drag_anchor_x,
                       bounds.y + bounds.h, 1.0f, nk_rgb(255, 213, 99));
    }
    nk_push_scissor(canvas, parent_clip);

    if (audio.offset_drag_active || view->offset_drag_active) {
        nk_layout_row_dynamic(context, 20, 1);
        nk_labelf(context, NK_TEXT_LEFT, "Offset preview: %lld frames (%+.6f s)",
                  static_cast<long long>(displayed_delta),
                  static_cast<double>(displayed_delta) / view->sample_rate);
        if (view->offset_drag_active) {
            nk_layout_row_dynamic(context, 20, 1);
            nk_labelf(context, NK_TEXT_LEFT, "Proposed beat-zero offset: %lld/%llu s",
                      static_cast<long long>(view->proposed_offset_numerator),
                      static_cast<unsigned long long>(view->proposed_offset_denominator));
        }
    } else if (view->timing_locked) {
        nk_layout_row_dynamic(context, 20, 1);
        nk_label(context, "Unlock timing to drag waveform alignment.", NK_TEXT_LEFT);
    } else if (view->has_chart) {
        nk_layout_row_dynamic(context, 20, 1);
        nk_label(context, "Drag the waveform horizontally to adjust beat-zero offset.",
                 NK_TEXT_LEFT);
    }
}
