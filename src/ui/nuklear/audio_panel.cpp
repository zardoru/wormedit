#include "ui/nuklear/audio_panel.h"
#include "ui/nuklear/audio_state.h"
#include "ui/nuklear/chrome.h"

#include "wormedit/nuklear_config.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <span>
#include <string>

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
            const bool selected =
                view.has_selected_asset && view.selected_asset_id == view.asset_ids[index];
            std::snprintf(label, sizeof(label), "%sAsset %llu", selected ? "• " : "",
                          static_cast<unsigned long long>(view.asset_ids[index]));
            if (nk_button_label(context, label) && !selected) {
                audio.selected_resource_id = 0;
                audio.selected_channel_id = 0;
                audio.selected_clip_id = 0;
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

void cancel_stale_offset_drag(const WormUiAudioSlicesView& view, WormUiWorkspaceAction& action,
                              bool window_focused) {
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

namespace {

auto sound_usage_view_valid(const WormUiAudioSlicesView& view) -> bool {
    if (view.usage_row_count > WORM_UI_SOUND_USAGE_ROW_CAPACITY ||
        (view.usage_row_count != 0 && !view.usage_rows) ||
        (view.usage_valid &&
         (view.usage_song_id == 0 || view.usage_document_revision == 0 ||
          view.usage_resource_id == 0 || view.usage_resource_id != view.selected_resource_id)))
        return false;
    for (std::size_t index = 0; index < view.usage_row_count; ++index) {
        const auto& row = view.usage_rows[index];
        if (row.chart_id == 0 || row.kind < 0 || row.kind > 3 || row.scope < 0 || row.scope > 1 ||
            (row.position_valid && row.measure_id == 0) || (!row.position_valid && row.kind != 3) ||
            !row.chart_label || !row.position_label)
            return false;
    }
    return true;
}

} // namespace

int worm_ui_nuklear_audio_slices_view_valid(const WormUiAudioSlicesView* view) {
    if (!view || view->asset_count > WORM_UI_AUDIO_ASSET_CAPACITY ||
        (view->asset_count != 0 && !view->asset_ids) ||
        view->waveform_column_count > WORM_UI_AUDIO_WAVEFORM_COLUMN_CAPACITY ||
        (view->waveform_column_count != 0 && !view->waveform_columns) ||
        view->sound_row_count > WORM_UI_SOUND_ROW_CAPACITY ||
        (view->sound_row_count != 0 && !view->sound_rows) || !sound_usage_view_valid(*view) ||
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

namespace {

void render_waveform_column(struct nk_context* context, const WormUiAudioSlicesView* view,
                            WormUiWorkspaceAction* action) {
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

auto folded(const char* text) -> std::string {
    std::string result = text ? text : "";
    for (auto& c : result) {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c + 32);
    }
    return result;
}

auto sound_kind_name(int kind) -> const char* {
    switch (kind) {
    case 1:
        return "Stem";
    case 2:
        return "Keysound";
    case 3:
        return "Sample";
    default:
        return "Sound";
    }
}

auto sound_usage_kind_name(int kind) -> const char* {
    switch (kind) {
    case 0:
        return "Note";
    case 1:
        return "BGM";
    case 2:
        return "Sound channel event";
    case 3:
        return "Sound channel source";
    default:
        return "Sound use";
    }
}

auto sound_usage_scope_name(int scope) -> const char* {
    return scope == 0 ? "Resource" : "Shared asset";
}

auto row_matches(const WormUiSoundRowView& row, const std::string& needle) -> bool {
    if (needle.empty()) {
        return true;
    }
    return folded(row.name).find(needle) != std::string::npos ||
           folded(row.tags_text).find(needle) != std::string::npos;
}

void render_sound_list_column(struct nk_context* context, const WormUiAudioSlicesView& view,
                              WormUiWorkspaceAction& action) {
    nk_layout_row_dynamic(context, 22, 1);
    nk_label(context, "Sounds", NK_TEXT_LEFT);
    wormedit::ui::chrome::section_separator(context);
    nk_layout_row_dynamic(context, 24, 1);
    wormedit::ui::chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD, audio.search_text,
                                                     static_cast<int>(sizeof(audio.search_text)),
                                                     nk_filter_default);
    wormedit::ui::chrome::section_separator(context);
    const std::string needle = folded(audio.search_text);
    std::size_t shown = 0;
    for (std::size_t index = 0; index < view.sound_row_count; ++index) {
        const auto& row = view.sound_rows[index];
        if (!row_matches(row, needle)) {
            continue;
        }
        ++shown;
        const bool selected =
            audio.selected_resource_id != 0
                ? audio.selected_resource_id == row.resource_id
                : view.has_selected_asset && view.selected_asset_id == row.asset_id;
        nk_bool active = selected ? nk_true : nk_false;
        nk_layout_row_dynamic(context, 24, 1);
        char label[160]{};
        std::snprintf(label, sizeof(label), "%s%s  ·  %s", row.missing ? "! " : "",
                      row.name ? row.name : "(unnamed)", sound_kind_name(row.kind));
        if (nk_selectable_label(context, label, NK_TEXT_LEFT, &active) &&
            action.kind == WORM_UI_WORKSPACE_ACTION_NONE) {
            audio.selected_project_id = view.usage_song_id;
            audio.selected_resource_id = row.resource_id;
            audio.selected_channel_id = 0;
            audio.selected_clip_id = 0;
            action.audio_resource_id = row.resource_id;
            if ((!view.has_selected_asset || view.selected_asset_id != row.asset_id) &&
                row.asset_id != 0) {
                action.kind = WORM_UI_WORKSPACE_ACTION_SELECT_AUDIO_ASSET;
                action.audio_asset_id = row.asset_id;
            } else if (action.kind == WORM_UI_WORKSPACE_ACTION_NONE) {
                action.kind = WORM_UI_WORKSPACE_ACTION_SELECT_SOUND_RESOURCE;
            }
        }
    }
    nk_layout_row_dynamic(context, 20, 1);
    if (shown == 0) {
        nk_label(context,
                 view.sound_row_count == 0 ? "No sounds in this project."
                                           : "No sound matches the search.",
                 NK_TEXT_LEFT);
    } else if (view.sound_rows_truncated) {
        nk_label(context, "More sounds are hidden by the view cap.", NK_TEXT_LEFT);
    }
}

auto selected_sound(const WormUiAudioSlicesView& view) -> const WormUiSoundRowView* {
    if (audio.selected_resource_id != 0 && view.sound_rows) {
        const auto rows =
            std::span<const WormUiSoundRowView>{view.sound_rows, view.sound_row_count};
        const auto found =
            std::ranges::find(rows, audio.selected_resource_id, &WormUiSoundRowView::resource_id);
        if (found != rows.end() && audio.selected_project_id == view.usage_song_id)
            return &*found;
    }
    for (std::size_t index = 0; index < view.sound_row_count; ++index) {
        if (view.has_selected_asset && view.sound_rows[index].asset_id == view.selected_asset_id) {
            audio.selected_project_id = view.usage_song_id;
            audio.selected_resource_id = view.sound_rows[index].resource_id;
            audio.selected_channel_id = 0;
            audio.selected_clip_id = 0;
            return &view.sound_rows[index];
        }
    }
    return nullptr;
}

void dispatch_sound_usage_activation(const WormUiAudioSlicesView& view,
                                     const WormUiSoundUsageRowView& row,
                                     WormUiWorkspaceAction& action) {
    if (action.kind != WORM_UI_WORKSPACE_ACTION_NONE || !view.usage_valid ||
        view.usage_song_id == 0 || view.usage_document_revision == 0 ||
        view.usage_resource_id == 0 || view.usage_resource_id != view.selected_resource_id)
        return;
    action.kind = WORM_UI_WORKSPACE_ACTION_SOUND_USAGE_ACTIVATE;
    action.sound_usage_song_id = view.usage_song_id;
    action.sound_usage_document_revision = view.usage_document_revision;
    action.sound_usage_resource_id = view.usage_resource_id;
    action.sound_usage_asset_id = view.usage_asset_id;
    action.sound_usage_chart_id = row.chart_id;
    action.sound_usage_measure_id = row.measure_id;
    action.sound_usage_row = row.row;
    action.sound_usage_object_id = row.object_id;
    action.sound_usage_channel_id = row.channel_id;
    action.sound_usage_clip_id = row.clip_id;
    action.sound_usage_referenced_resource_id = row.referenced_resource_id;
    action.sound_usage_referenced_asset_id = row.referenced_asset_id;
    action.sound_usage_kind = row.kind;
    action.sound_usage_scope = row.scope;
    action.sound_usage_position_valid = row.position_valid;
}

void render_sound_usage_rows(struct nk_context* context, const WormUiAudioSlicesView& view,
                             WormUiWorkspaceAction& action) {
    if (!view.usage_valid || view.usage_resource_id != view.selected_resource_id) {
        nk_layout_row_dynamic(context, 22, 1);
        nk_label(context, "Usage results are unavailable for this sound.", NK_TEXT_LEFT);
        return;
    }
    nk_layout_row_dynamic(context, 20, 1);
    nk_labelf(context, NK_TEXT_LEFT, "%zu use(s)%s", view.usage_total_count,
              view.usage_rows_truncated ? " · showing first 1,024" : "");
    if (view.usage_row_count == 0) {
        nk_layout_row_dynamic(context, 22, 1);
        nk_label(context, "This sound has no chart uses.", NK_TEXT_LEFT);
        return;
    }
    for (std::size_t index = 0; index < view.usage_row_count; ++index) {
        const auto& row = view.usage_rows[index];
        const char* chart_label = row.chart_label ? row.chart_label : "(unnamed chart)";
        const char* position_label = row.position_label ? row.position_label : "Source clip";
        nk_bool active = nk_false;
        nk_layout_row_dynamic(context, 22, 1);
        if (nk_selectable_label(context, chart_label, NK_TEXT_LEFT, &active))
            dispatch_sound_usage_activation(view, row, action);
        nk_layout_row_dynamic(context, 22, 1);
        if (nk_selectable_label(context, position_label, NK_TEXT_LEFT, &active))
            dispatch_sound_usage_activation(view, row, action);
        nk_layout_row_dynamic(context, 17, 1);
        nk_labelf(context, NK_TEXT_LEFT, "%s · %s", sound_usage_kind_name(row.kind),
                  sound_usage_scope_name(row.scope));
    }
}

auto sound_source_copy_available(const WormUiAudioSlicesView& view, const WormUiSoundRowView& sound)
    -> bool {
    return view.has_chart && view.chart_id != 0 && view.document_revision != 0 &&
           sound.asset_id != 0 && !sound.missing && sound.source_path && sound.source_path[0];
}

void render_sound_source_copy_button(struct nk_context* context, const WormUiAudioSlicesView& view,
                                     const WormUiSoundRowView& sound,
                                     WormUiWorkspaceAction& action) {
    const auto can_copy = sound_source_copy_available(view, sound);
    if (!can_copy)
        nk_widget_disable_begin(context);
    const auto clicked = nk_button_label(context, "Copy source into project");
    if (!can_copy)
        nk_widget_disable_end(context);
    if (!clicked || !can_copy || action.compose_action != WORM_UI_COMPOSE_ACTION_NONE)
        return;
    action.compose_resource_asset_id = sound.asset_id;
    action.compose_input_chart_id = view.chart_id;
    action.compose_input_document_revision = view.document_revision;
    action.compose_action = WORM_UI_COMPOSE_ACTION_COPY_RESOURCE_TO_PROJECT;
}

void render_sound_properties_column(struct nk_context* context, const WormUiAudioSlicesView& view,
                                    WormUiWorkspaceAction& action) {
    nk_layout_row_dynamic(context, 22, 1);
    nk_label(context, "Properties", NK_TEXT_LEFT);
    wormedit::ui::chrome::section_separator(context);
    const auto* selected = selected_sound(view);
    nk_layout_row_dynamic(context, 22, 1);
    if (!selected) {
        nk_label(context, "Select a sound to see its properties.", NK_TEXT_LEFT);
        return;
    }
    nk_label(context, selected->name ? selected->name : "(unnamed)", NK_TEXT_LEFT);
    nk_labelf(context, NK_TEXT_LEFT, "Kind: %s", sound_kind_name(selected->kind));
    nk_labelf(context, NK_TEXT_LEFT, "Used %u time(s)", selected->use_count);
    if (selected->sample_rate != 0) {
        nk_labelf(context, NK_TEXT_LEFT, "%.2f s · %u Hz",
                  static_cast<double>(selected->duration_frames) / selected->sample_rate,
                  selected->sample_rate);
    }
    nk_labelf(context, NK_TEXT_LEFT, "Tags: %s",
              selected->tags_text && selected->tags_text[0] ? selected->tags_text : "(none)");
    if (selected->source_path && selected->source_path[0]) {
        nk_label(context, selected->source_path, NK_TEXT_LEFT);
    }
    if (selected->missing) {
        nk_label_colored(context, "The audio file is missing.", NK_TEXT_LEFT,
                         nk_rgb(255, 120, 120));
    }
    if (view.selected_channel_id != 0 || view.selected_clip_id != 0) {
        nk_layout_row_dynamic(context, 20, 1);
        nk_labelf(context, NK_TEXT_LEFT, "Source channel %llu · clip %llu",
                  static_cast<unsigned long long>(view.selected_channel_id),
                  static_cast<unsigned long long>(view.selected_clip_id));
    }
    nk_layout_row_dynamic(context, 25, 1);
    if (nk_button_label(context, audio.find_uses_open ? "Hide uses" : "Find uses")) {
        if (!audio.find_uses_open && audio.selected_resource_id == 0) {
            audio.selected_project_id = view.usage_song_id;
            audio.selected_resource_id = selected->resource_id;
        }
        audio.find_uses_open = !audio.find_uses_open;
    }
    if (audio.find_uses_open)
        render_sound_usage_rows(context, view, action);
    render_sound_source_copy_button(context, view, *selected, action);
}

void render_sound_columns(struct nk_context* context, const WormUiAudioSlicesView& view,
                          WormUiWorkspaceAction& action) {
    constexpr float margin = 0.02f;
    constexpr float height = 460.0f;
    const float ratios[] = {margin, 0.26f, margin, 0.40f, margin, 0.26f, margin};
    nk_layout_row(context, NK_DYNAMIC, height, 7, ratios);
    nk_spacing(context, 1);
    if (nk_group_begin(context, "sound_list", NK_WINDOW_BORDER)) {
        const auto bounds = nk_window_get_content_region(context);
        audio.sound_list_valid = true;
        audio.sound_list_x = bounds.x;
        audio.sound_list_y = bounds.y;
        audio.sound_list_width = bounds.w;
        audio.sound_list_height = bounds.h;
        render_sound_list_column(context, view, action);
        nk_group_end(context);
    }
    wormedit::ui::chrome::toolbar_separator(context);
    if (nk_group_begin(context, "sound_wave", NK_WINDOW_BORDER)) {
        nk_layout_row_dynamic(context, 22, 1);
        nk_label(context, "Waveform", NK_TEXT_LEFT);
        wormedit::ui::chrome::section_separator(context);
        render_waveform_column(context, &view, &action);
        nk_group_end(context);
    }
    wormedit::ui::chrome::toolbar_separator(context);
    if (nk_group_begin(context, "sound_properties", NK_WINDOW_BORDER)) {
        render_sound_properties_column(context, view, action);
        nk_group_end(context);
    }
    nk_spacing(context, 1);
}

} // namespace

void worm_ui_nuklear_render_audio_slices(struct nk_context* context,
                                         const WormUiAudioSlicesView* view,
                                         WormUiWorkspaceAction* action, bool show_asset_catalog,
                                         bool window_focused) {
    if (!context || !view || !action) {
        return;
    }
    if (view->usage_song_id != 0 && audio.selected_project_id != view->usage_song_id) {
        audio.selected_project_id = view->usage_song_id;
        audio.selected_resource_id = view->selected_resource_id;
        audio.selected_channel_id = view->selected_channel_id;
        audio.selected_clip_id = view->selected_clip_id;
        audio.find_uses_open = false;
    }
    cancel_stale_offset_drag(*view, *action, window_focused);
    audio.sound_list_valid = false;
    if (show_asset_catalog) {
        nk_layout_row_dynamic(context, 25, 1);
        if (nk_button_label(context, "Add sounds…"))
            action->sound_import_pick_requested = true;
        render_sound_columns(context, *view, *action);
        return;
    }
    if (show_asset_catalog) {
        render_audio_asset_catalog(context, *view, *action);
    }
    render_waveform_column(context, view, action);
}

void worm_ui_frontend_sound_usage_request(WormUiSoundUsageRequest* request) {
    if (!request)
        return;
    const auto& state = worm_ui_nuklear_audio_state();
    request->selected_project_id = state.selected_project_id;
    request->selected_resource_id = state.selected_resource_id;
    request->selected_channel_id = state.selected_channel_id;
    request->selected_clip_id = state.selected_clip_id;
    request->find_uses_open = state.find_uses_open;
}

void worm_ui_frontend_select_sound_usage_source(std::uint64_t project_id, std::uint64_t resource_id,
                                                std::uint64_t channel_id, std::uint64_t clip_id) {
    auto& state = worm_ui_nuklear_audio_state();
    state.selected_project_id = project_id;
    state.selected_resource_id = resource_id;
    state.selected_channel_id = channel_id;
    state.selected_clip_id = clip_id;
}
