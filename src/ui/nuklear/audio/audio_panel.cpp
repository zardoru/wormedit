#include "core/text_util.h"
#include "ui/nuklear/content_layout.h"
#include "ui/field/rect.h"
#include "ui/nuklear/bmson_lane_mode_ui.h"
#include "ui/nuklear/audio/audio_panel.h"

#include <wormedit/core/rational.h>
#include <wormedit/core/catalogs.h>
#include "ui/nuklear/audio/audio_state.h"
#include "ui/nuklear/compose_drag_capture.h"
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
#include <string_view>
#include <vector>

namespace {
using wormedit::text_util::ascii_lowercase;

auto& audio = worm_ui_nuklear_audio_state();

struct audio_input_scope_t {
    nk_window* window;
    nk_flags saved_flags;
    nk_panel* panel;
    nk_flags saved_panel_flags;
    bool blocked;
    audio_input_scope_t(nk_context* context, bool allowed)
        : window{context->current}, saved_flags{window->flags}, panel{window->layout},
          saved_panel_flags{panel->flags}, blocked{!allowed} {
        if (blocked) {
            constexpr auto input_flags = static_cast<nk_flags>(NK_WINDOW_NO_INPUT) |
                                         static_cast<nk_flags>(NK_WINDOW_ROM);
            window->flags |= input_flags;
            panel->flags |= input_flags;
        }
    }
    ~audio_input_scope_t() {
        if (!blocked) return;
        window->flags = saved_flags;
        panel->flags = saved_panel_flags;
    }
};

void cancel_offset_pointer_drag() {
    audio.offset_drag_active = false;
    audio.offset_drag_anchor_x = 0.0f;
    audio.offset_drag_width = 0.0f;
    audio.offset_drag_frames_per_pixel = 0.0;
    audio.offset_drag_chart_id = 0;
    audio.offset_drag_document_revision = 0;
    audio.offset_drag_asset_id = 0;
    audio.offset_drag_total_frames = 0;
    audio.offset_drag_sample_rate = 0;
    audio.offset_drag_delta_frames = 0;
}

auto offset_pointer_drag_matches(const wormedit::ui::audio_slices_view_t* view) -> bool {
    return view && audio.offset_drag_active && view->visible && view->waveform_available &&
           view->has_chart && view->has_selected_asset && view->offset_drag_active &&
           !view->timing_locked && view->chart_id.value == audio.offset_drag_chart_id &&
           view->document_revision == audio.offset_drag_document_revision &&
           view->selected_asset_id.value == audio.offset_drag_asset_id &&
           view->total_frames == audio.offset_drag_total_frames &&
           view->sample_rate == audio.offset_drag_sample_rate;
}

auto offset_pointer_delta(float pointer_x) -> std::int64_t {
    if (audio.offset_drag_width <= 0.0f || audio.offset_drag_total_frames == 0) {
        return 0;
    }
    const auto frames = (static_cast<long double>(pointer_x) - audio.offset_drag_anchor_x) *
                        audio.offset_drag_frames_per_pixel;
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

void set_offset_drag_action(wormedit::ui::workspace_action_t* action, WormUiAudioOffsetDragAction kind,
                            std::int64_t delta_frames) {
    if (!action || action->audio_offset_drag_action != WORM_UI_AUDIO_OFFSET_DRAG_NONE) {
        return;
    }
    action->audio_offset_drag_action = kind;
    action->audio_offset_drag_chart_id = wormedit::chart_id_t{audio.offset_drag_chart_id};
    action->audio_offset_drag_document_revision = audio.offset_drag_document_revision;
    action->audio_offset_drag_sample_rate = audio.offset_drag_sample_rate;
    action->audio_offset_drag_delta_frames = delta_frames;
}

void render_audio_asset_catalog(struct nk_context* context, const wormedit::ui::audio_slices_view_t& view,
                                wormedit::ui::workspace_action_t& action) {
    nk_layout_row_dynamic(context, 22, 1);
    if (view.asset_ids.size() == 0) {
        nk_label(context, "No audio assets are registered in this project.", NK_TEXT_LEFT);
        return;
    }
    nk_label(context, "Source assets", NK_TEXT_LEFT);
    constexpr int buttons_per_row = 4;
    for (std::size_t begin = 0; begin < view.asset_ids.size(); begin += buttons_per_row) {
        nk_layout_row_dynamic(context, 26, buttons_per_row);
        for (std::size_t slot = 0; slot < buttons_per_row; ++slot) {
            const auto index = begin + slot;
            if (index >= view.asset_ids.size()) {
                nk_label(context, "", NK_TEXT_LEFT);
                continue;
            }
            char label[48]{};
            const bool selected =
                view.has_selected_asset && view.selected_asset_id.value == view.asset_ids[index].value;
            std::snprintf(label, sizeof(label), "%sAsset %llu", selected ? "• " : "",
                          static_cast<unsigned long long>(view.asset_ids[index].value));
            if (nk_button_label(context, label) && !selected) {
                audio.selected_resource_id = 0;
                audio.selected_channel_id = 0;
                audio.selected_clip_id = 0;
                action.kind = WORM_UI_WORKSPACE_ACTION_SELECT_AUDIO_ASSET;
                action.audio_asset_id.value = view.asset_ids[index].value;
            }
        }
    }

}

void update_offset_pointer_drag(struct nk_context* context, const wormedit::ui::audio_slices_view_t& view,
                                wormedit::ui::workspace_action_t& action, struct nk_rect bounds) {
    if (context->current->flags & NK_WINDOW_NO_INPUT) return;
    const bool can_drag_offset = view.has_chart && !view.timing_locked;
    const auto& left_button = context->input.mouse.buttons[NK_BUTTON_LEFT];
    const bool left_released = nk_input_is_mouse_released(&context->input, NK_BUTTON_LEFT);
    if (audio.offset_drag_active) {
        const auto delta = offset_pointer_delta(context->input.mouse.pos.x);
        audio.offset_drag_delta_frames = delta;
        // Source/runtime/focus validity is checked by cancel_stale_offset_drag
        // before rendering. This channel commits even a zero-distance release.
        const compose_drag_capture_t anchor{.anchor_x = audio.offset_drag_anchor_x};
        const auto& pointer = context->input.mouse.pos;
        const auto continuation = anchor.advance(
            {.down = left_button.down != 0, .released = left_released, .x = pointer.x, .y = pointer.y},
            true, compose_drag_release_policy_t::commit_on_release);
        if (continuation.phase == compose_drag_phase_t::commit) {
            set_offset_drag_action(&action, WORM_UI_AUDIO_OFFSET_DRAG_COMMIT, delta);
            cancel_offset_pointer_drag();
        } else if (continuation.phase == compose_drag_phase_t::cancel) {
            set_offset_drag_action(&action, WORM_UI_AUDIO_OFFSET_DRAG_CANCEL, delta);
            cancel_offset_pointer_drag();
        } else if (nk_input_is_mouse_moved(&context->input)) {
            set_offset_drag_action(&action, WORM_UI_AUDIO_OFFSET_DRAG_UPDATE, delta);
        }
    } else if (can_drag_offset && nk_input_is_key_down(&context->input, NK_KEY_SHIFT) &&
               action.audio_offset_drag_action == WORM_UI_AUDIO_OFFSET_DRAG_NONE &&
               nk_input_is_mouse_click_down_in_rect(&context->input, NK_BUTTON_LEFT, bounds, nk_true)) {
        audio.offset_drag_active = true;
        audio.offset_drag_anchor_x = context->input.mouse.pos.x;
        audio.offset_drag_width = bounds.w;
        audio.offset_drag_frames_per_pixel = audio.waveform_viewport.frames_per_pixel;
        audio.offset_drag_chart_id = view.chart_id.value;
        audio.offset_drag_document_revision = view.document_revision;
        audio.offset_drag_asset_id = view.selected_asset_id.value;
        audio.offset_drag_total_frames = view.total_frames;
        audio.offset_drag_sample_rate = view.sample_rate;
        audio.offset_drag_delta_frames = 0;
        set_offset_drag_action(&action, WORM_UI_AUDIO_OFFSET_DRAG_BEGIN, 0);
    }
}

void cancel_stale_offset_drag(const wormedit::ui::audio_slices_view_t& view, wormedit::ui::workspace_action_t& action,
                              bool window_focused) {
    if (audio.offset_drag_active &&
        (!offset_pointer_drag_matches(&view) || audio.offset_escape_pending ||
         audio.offset_cancel_pending || !window_focused)) {
        set_offset_drag_action(&action, WORM_UI_AUDIO_OFFSET_DRAG_CANCEL,
                               audio.offset_drag_delta_frames);
        cancel_offset_pointer_drag();
    }
    audio.offset_cancel_pending = false;
}

} // namespace

namespace {

auto sound_usage_view_valid(const wormedit::ui::audio_slices_view_t& view) -> bool {
    if ((view.usage_valid &&
         (view.usage_song_id.value == 0 || view.usage_document_revision == 0 ||
          view.usage_resource_id.value == 0 || view.usage_resource_id.value != view.selected_resource_id.value)))
        return false;
    for (std::size_t index = 0; index < view.usage_rows.size(); ++index) {
        const auto& row = view.usage_rows[index];
        if (row.chart_id.value == 0 || row.kind > wormedit::sound_usage_kind_t::sound_channel_source ||
            row.scope > wormedit::sound_usage_scope_t::shared_asset_reference ||
            (row.position_valid && row.measure_id.value == 0) ||
            (!row.position_valid && row.kind != wormedit::sound_usage_kind_t::sound_channel_source) ||
            !row.chart_label.c_str() || !row.position_label.c_str())
            return false;
    }
    return true;
}

} // namespace

int worm_ui_nuklear_audio_slices_view_valid(const wormedit::ui::audio_slices_view_t* view) {
    if (!view || view->waveform_columns.size() > wormedit::ui::audio_waveform_column_limit ||
        view->sound_rows.size() > wormedit::ui::audio_sound_row_limit ||
        !sound_usage_view_valid(*view) ||
        (view->has_selected_asset && view->selected_asset_id.value == 0) ||
        (view->analysis_pending && view->waveform_available) ||
        (!view->waveform_available && view->waveform_columns.size() != 0) ||
        (view->waveform_available && (view->waveform_columns.size() == 0 || view->total_frames == 0 ||
                                      view->sample_rate == 0 || !view->has_selected_asset)) ||
        (view->has_chart && (view->chart_id.value == 0 || view->offset_denominator == 0)) ||
        (view->offset_drag_active &&
         (!view->has_chart || !view->waveform_available || view->timing_locked ||
          view->proposed_offset_denominator == 0))) {
        return 0;
    }
    for (std::size_t index = 0; index < view->asset_ids.size(); ++index) {
        if (view->asset_ids[index].value == 0) {
            return 0;
        }
    }
    for (std::size_t index = 0; index < view->waveform_columns.size(); ++index) {
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

void update_waveform_viewport(nk_context* context, const wormedit::ui::audio_slices_view_t& view,
                              struct nk_rect bounds, bool wheel_zoom) {
    using namespace wormedit::audio;
    const auto width = static_cast<std::uint32_t>(std::max(1.0f, bounds.w));
    auto& viewport = audio.waveform_viewport;
    if (audio.waveform_asset_id != view.selected_asset_id.value || viewport.total_frames != view.total_frames) {
        viewport = fit_waveform_viewport(view.total_frames, width);
        audio.waveform_asset_id = view.selected_asset_id.value;
        audio.waveform_pan_active = false;
    } else if (viewport.pixel_width != width) {
        audio.waveform_pan_active = false;
        const bool fitted = viewport.begin_frame == 0 &&
            waveform_viewport_end_frame(viewport) == viewport.total_frames;
        viewport.pixel_width = width;
        viewport = fitted ? fit_waveform_viewport(view.total_frames, width)
                          : clamp_waveform_viewport(viewport);
    }
    const auto& input = context->input;
    if (context->current->flags & NK_WINDOW_NO_INPUT) return;
    if (wheel_zoom && !audio.waveform_pan_active && !audio.offset_drag_active &&
        nk_input_is_mouse_hovering_rect(&input, bounds) && input.mouse.scroll_delta.y != 0.0f)
        viewport = zoom_waveform_viewport(viewport, input.mouse.pos.x - bounds.x,
                                          std::pow(1.25, input.mouse.scroll_delta.y));
    if (audio.waveform_pan_active) {
        viewport = pan_waveform_viewport(audio.waveform_pan_origin,
                                         audio.waveform_pan_anchor_x - input.mouse.pos.x);
        if (!nk_input_is_mouse_down(&input, NK_BUTTON_LEFT)) audio.waveform_pan_active = false;
    } else if (!audio.offset_drag_active && !nk_input_is_key_down(&input, NK_KEY_SHIFT) &&
               nk_input_is_mouse_click_down_in_rect(&input, NK_BUTTON_LEFT, bounds, nk_true)) {
        audio.waveform_pan_active = true;
        audio.waveform_pan_anchor_x = input.mouse.pos.x;
        audio.waveform_pan_origin = viewport;
    }
    audio.waveform_revision = view.document_revision;
}

auto waveform_navigation_hint(bool embedded) -> const char* {
    return embedded ? "Wheel: scroll page · Drag: pan" : "Wheel: zoom · Drag: pan";
}

auto waveform_height(nk_context* context, bool embedded) -> float {
    return embedded ? 180.0f : wormedit::ui::layout::remaining_content_height(context,
        {.minimum = 80.0f, .reserved = 82.0f});
}

void render_waveform_column(struct nk_context* context, const wormedit::ui::audio_slices_view_t* view,
                            wormedit::ui::workspace_action_t* action, bool embedded,
                            std::span<const wormedit::ui::audio_waveform_column_view_t> columns) {
    nk_layout_row_dynamic(context, 22, 1);
    if (!view->has_selected_asset) {
        nk_label(context, "Select an asset to build its waveform overview.", NK_TEXT_LEFT);
        return;
    }
    if (view->analysis_pending) {
        nk_labelf(context, NK_TEXT_LEFT, "Analyzing asset %llu…",
                  static_cast<unsigned long long>(view->selected_asset_id.value));
        return;
    }
    if (!view->waveform_available) {
        nk_label(context, "Waveform unavailable for the selected asset.", NK_TEXT_LEFT);
        return;
    }

    const auto seconds = wormedit::to_double(wormedit::rational_t{view->total_frames, view->sample_rate});
    nk_labelf(context, NK_TEXT_LEFT, "Stereo overview · %.2f seconds · %u Hz", seconds,
              view->sample_rate);
    nk_layout_row_dynamic(context, 26, 3);
    if (nk_button_label(context, "−"))
        audio.waveform_viewport = wormedit::audio::zoom_waveform_viewport(audio.waveform_viewport,
            audio.waveform_viewport.pixel_width * 0.5, 0.8);
    if (nk_button_label(context, "+"))
        audio.waveform_viewport = wormedit::audio::zoom_waveform_viewport(audio.waveform_viewport,
            audio.waveform_viewport.pixel_width * 0.5, 1.25);
    if (nk_button_label(context, "Fit"))
        audio.waveform_viewport = wormedit::audio::fit_waveform_viewport(view->total_frames,
            audio.waveform_viewport.pixel_width);
    nk_layout_row_dynamic(context, waveform_height(context, embedded), 1);
    struct nk_rect bounds{};
    if (nk_widget(&bounds, context) == NK_WIDGET_INVALID || bounds.w <= 1.0f || bounds.h <= 1.0f) {
        return;
    }
    update_waveform_viewport(context, *view, bounds, !embedded);
    update_offset_pointer_drag(context, *view, *action, bounds);
    auto* canvas = nk_window_get_canvas(context);
    if (!canvas) {
        return;
    }
    const auto parent_clip = canvas->clip;
    // Clip to the visible part of the widget: the page scrolls, and a scissor of the full
    // bounds would paint the waveform over the tabs above the content group.
    const auto overlap = wormedit::ui::rect_intersection(
        {bounds.x, bounds.y, bounds.w, bounds.h},
        {parent_clip.x, parent_clip.y, parent_clip.w, parent_clip.h});
    nk_push_scissor(canvas, nk_rect(overlap.x, overlap.y, overlap.w, overlap.h));
    nk_fill_rect(canvas, bounds, 2.0f, wormedit::ui::theme::ui(wormedit::ui::theme::UiColor::waveform_background));
    const auto channel_height = bounds.h * 0.5f;
    const auto column_width = bounds.w / static_cast<float>(columns.size());
    const auto displayed_delta = audio.offset_drag_active   ? audio.offset_drag_delta_frames
                                 : view->offset_drag_active ? view->offset_drag_delta_frames
                                                            : 0;
    const auto waveform_shift = static_cast<float>(static_cast<long double>(displayed_delta) /
                                                   std::max<std::uint64_t>(1,
                                                       view->waveform_end_frame > view->waveform_begin_frame
                                                           ? view->waveform_end_frame - view->waveform_begin_frame
                                                           : view->total_frames) * bounds.w);
    for (std::size_t channel = 0; channel < 2; ++channel) {
        const auto center = bounds.y + channel_height * (static_cast<float>(channel) + 0.5f);
        const auto amplitude = channel_height * 0.42f;
        nk_stroke_line(canvas, bounds.x, center, bounds.x + bounds.w, center, 1.0f,
                       wormedit::ui::theme::ui(wormedit::ui::theme::UiColor::waveform_guide));
        for (std::size_t index = 0; index < columns.size(); ++index) {
            const auto& column = columns[index];
            const auto x =
                bounds.x + (static_cast<float>(index) + 0.5f) * column_width + waveform_shift;
            const auto minimum = std::clamp(column.minimum[channel], -1.0f, 1.0f);
            const auto maximum = std::clamp(column.maximum[channel], -1.0f, 1.0f);
            const auto rms = std::clamp(column.rms[channel], 0.0f, 1.0f);
            nk_stroke_line(canvas, x, center - rms * amplitude, x, center + rms * amplitude,
                           std::max(1.0f, column_width), wormedit::ui::theme::ui(wormedit::ui::theme::UiColor::waveform_rms));
            nk_stroke_line(canvas, x, center - maximum * amplitude, x, center - minimum * amplitude,
                           std::max(1.0f, column_width), wormedit::ui::theme::ui(wormedit::ui::theme::UiColor::waveform_peak));
        }
    }
    if (audio.offset_drag_active || view->offset_drag_active) {
        nk_stroke_line(canvas, audio.offset_drag_anchor_x, bounds.y, audio.offset_drag_anchor_x,
                       bounds.y + bounds.h, 1.0f, wormedit::ui::theme::ui(wormedit::ui::theme::UiColor::waveform_playhead));
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
        nk_label(context, waveform_navigation_hint(embedded), NK_TEXT_LEFT);
        nk_layout_row_dynamic(context, 20, 1);
        nk_label(context, "Timing locked", NK_TEXT_LEFT);
    } else if (view->has_chart) {
        nk_layout_row_dynamic(context, 20, 1);
        nk_label(context, waveform_navigation_hint(embedded), NK_TEXT_LEFT);
        nk_layout_row_dynamic(context, 20, 1);
        nk_label(context, "Shift-drag: align", NK_TEXT_LEFT);
    } else {
        nk_layout_row_dynamic(context, 20, 1);
        nk_label(context, waveform_navigation_hint(embedded), NK_TEXT_LEFT);
    }
}

auto sound_usage_kind_name(wormedit::sound_usage_kind_t kind) -> const char* {
    switch (kind) {
    case wormedit::sound_usage_kind_t::note:
        return "Note";
    case wormedit::sound_usage_kind_t::bgm:
        return "BGM";
    case wormedit::sound_usage_kind_t::sound_channel_event:
        return "Sound channel event";
    case wormedit::sound_usage_kind_t::sound_channel_source:
        return "Sound channel source";
    }
    return "Sound use";
}

auto sound_usage_scope_name(wormedit::sound_usage_scope_t scope) -> const char* {
    return scope == wormedit::sound_usage_scope_t::resource_reference ? "Resource" : "Shared asset";
}

auto row_matches(const wormedit::ui::sound_row_view_t& row, const std::string& needle) -> bool {
    if (needle.empty()) {
        return true;
    }
    return ascii_lowercase(row.name.c_str() ? row.name.c_str() : "").find(needle) != std::string::npos ||
           ascii_lowercase(row.tags_text.c_str() ? row.tags_text.c_str() : "").find(needle) != std::string::npos;
}

void select_sound(const wormedit::ui::audio_slices_view_t& view, const wormedit::ui::sound_row_view_t& row,
                  wormedit::ui::workspace_action_t& action) {
    if (action.kind != WORM_UI_WORKSPACE_ACTION_NONE) return;
    audio.selected_project_id = view.usage_song_id.value;
    audio.selected_resource_id = row.resource_id.value;
    audio.selected_channel_id = 0;
    audio.selected_clip_id = 0;
    action.audio_resource_id.value = row.resource_id.value;
    if ((!view.has_selected_asset || view.selected_asset_id.value != row.asset_id.value) && row.asset_id.value != 0) {
        action.kind = WORM_UI_WORKSPACE_ACTION_SELECT_AUDIO_ASSET;
        action.audio_asset_id.value = row.asset_id.value;
    } else action.kind = WORM_UI_WORKSPACE_ACTION_SELECT_SOUND_RESOURCE;
}

void audition_sound(const wormedit::ui::audio_slices_view_t& view, const wormedit::ui::sound_row_view_t& row,
                    wormedit::ui::workspace_action_t& action) {
    if (row.missing || row.asset_id.value == 0 || action.compose_action != WORM_UI_COMPOSE_ACTION_NONE)
        return;
    action.compose_action = WORM_UI_COMPOSE_ACTION_AUDITION_SOUND;
    action.compose_resource_id.value = row.resource_id.value;
    action.compose_input_project_id.value = view.usage_song_id.value;
    action.compose_input_chart_id.value = view.chart_id.value;
    action.compose_input_document_revision = view.document_revision;
}

using sound_rows_t = std::vector<const wormedit::ui::sound_row_view_t*>;

auto filtered_sound_rows(const wormedit::ui::audio_slices_view_t& view) -> sound_rows_t {
    sound_rows_t rows;
    const auto needle = ascii_lowercase(audio.search_text);
    for (std::size_t index = 0; index < view.sound_rows.size(); ++index)
        if (row_matches(view.sound_rows[index], needle) && audio.matches_tags(view.sound_rows[index].tags))
            rows.push_back(&view.sound_rows[index]);
    return rows;
}

auto handle_sound_list_keys(nk_context* context, const wormedit::ui::audio_slices_view_t& view,
                            const sound_rows_t& rows, wormedit::ui::workspace_action_t& action) -> int {
    const auto found = std::ranges::find_if(rows, [](const auto* row) {
        return row->resource_id.value == audio.selected_resource_id;
    });
    int selected = found == rows.end() ? -1 : static_cast<int>(found - rows.begin());
    if (!rows.empty() && audio.sound_list_focused) {
        if (audio.list_move_pending != 0) {
            selected = selected < 0 ? (audio.list_move_pending < 0 ? static_cast<int>(rows.size()) - 1 : 0)
                : std::clamp(selected + audio.list_move_pending, 0, static_cast<int>(rows.size()) - 1);
            select_sound(view, *rows[static_cast<std::size_t>(selected)], action);
            nk_group_set_scroll(context, "Sounds rows", 0,
                static_cast<nk_uint>(selected * (28 + context->style.window.spacing.y)));
        }
        if (audio.list_audition_pending && selected >= 0)
            audition_sound(view, *rows[static_cast<std::size_t>(selected)], action);
    }
    audio.list_move_pending = 0;
    audio.list_audition_pending = false;
    return selected;
}

void render_sound_list_row(nk_context* context, const wormedit::ui::audio_slices_view_t& view,
                           const wormedit::ui::sound_row_view_t& row, wormedit::ui::workspace_action_t& action) {
    nk_bool active = row.resource_id.value == audio.selected_resource_id ? nk_true : nk_false;
    nk_layout_row_dynamic(context, 28, 1);
    const auto bounds = nk_widget_bounds(context);
    char label[320]{};
    std::snprintf(label, sizeof(label), "%s%s  ·  %s", row.missing ? "! " : "",
                  row.name.c_str() ? row.name.c_str() : "(unnamed)", view.sound_kinds.label(static_cast<wormedit::sound_kind_t>(row.kind), "Sound"));
    if (nk_selectable_label(context, label, NK_TEXT_LEFT, &active)) select_sound(view, row, action);
    if (!(context->current->flags & NK_WINDOW_NO_INPUT) &&
        nk_input_is_mouse_click_down_in_rect(&context->input, NK_BUTTON_DOUBLE, bounds, nk_true)) {
        select_sound(view, row, action);
        audition_sound(view, row, action);
    }
}

void render_sound_list_column(struct nk_context* context, const wormedit::ui::audio_slices_view_t& view,
                              wormedit::ui::workspace_action_t& action) {
    nk_layout_row_dynamic(context, 22, 1);
    nk_label(context, "Sounds", NK_TEXT_LEFT);
    wormedit::ui::chrome::section_separator(context);
    nk_layout_row_dynamic(context, 26, 1);
    const auto search_bounds = nk_widget_bounds(context);
    wormedit::ui::chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD, audio.search_text,
                                                     static_cast<int>(sizeof(audio.search_text)),
                                                     nk_filter_default);
    if (nk_input_is_mouse_click_in_rect(&context->input, NK_BUTTON_LEFT, search_bounds))
        audio.sound_list_focused = false;
    nk_layout_row_dynamic(context, 20, 1);
    nk_label(context, "Filter by tag", NK_TEXT_LEFT);
    nk_layout_row_dynamic(context, 26, 1);
    const auto tag_bounds = nk_widget_bounds(context);
    wormedit::ui::chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD, audio.tag_filter,
        sizeof(audio.tag_filter), nk_filter_default);
    if (nk_input_is_mouse_click_in_rect(&context->input, NK_BUTTON_LEFT, tag_bounds))
        audio.sound_list_focused = false;
    wormedit::ui::chrome::section_separator(context);
    const auto rows = filtered_sound_rows(view);
    (void)handle_sound_list_keys(context, view, rows, action);
    nk_layout_row_dynamic(context, wormedit::ui::layout::remaining_content_height(context,
        {.minimum = 80.0f, .reserved = 28.0f}), 1);
    const auto bounds = nk_widget_bounds(context);
    audio.sound_list_x = bounds.x; audio.sound_list_y = bounds.y;
    audio.sound_list_width = bounds.w; audio.sound_list_height = bounds.h;
    audio.sound_list_valid = true;
    if (!(context->current->flags & NK_WINDOW_NO_INPUT) &&
        nk_input_is_mouse_pressed(&context->input, NK_BUTTON_LEFT))
        audio.sound_list_focused = nk_input_is_mouse_hovering_rect(&context->input, bounds);
    struct nk_list_view list{};
    if (nk_list_view_begin(context, &list, "Sounds rows", 0, 28,
                            static_cast<int>(std::max<std::size_t>(1, rows.size())))) {
        if (rows.empty()) {
            nk_layout_row_dynamic(context, 28, 1);
            nk_label(context, view.sound_rows.size() ? "No matches" : "No sounds yet", NK_TEXT_LEFT);
        } else {
            for (int index = list.begin; index < list.end && index < static_cast<int>(rows.size()); ++index)
                render_sound_list_row(context, view, *rows[static_cast<std::size_t>(index)], action);
        }
        nk_list_view_end(&list);
    }
    nk_layout_row_dynamic(context, 20, 1);
    nk_label(context, "↑/↓ Select · Space / double-click Play", NK_TEXT_LEFT);
}

auto selected_sound(const wormedit::ui::audio_slices_view_t& view) -> const wormedit::ui::sound_row_view_t* {
    if (audio.selected_resource_id != 0 && !view.sound_rows.empty()) {
        const auto rows =
            std::span<const wormedit::ui::sound_row_view_t>{view.sound_rows};
        const auto found =
            std::ranges::find(rows, wormedit::resource_id_t{audio.selected_resource_id}, &wormedit::ui::sound_row_view_t::resource_id);
        if (found != rows.end() && audio.selected_project_id == view.usage_song_id.value)
            return &*found;
    }
    for (std::size_t index = 0; index < view.sound_rows.size(); ++index) {
        if (view.has_selected_asset && view.sound_rows[index].asset_id.value == view.selected_asset_id.value) {
            audio.selected_project_id = view.usage_song_id.value;
            audio.selected_resource_id = view.sound_rows[index].resource_id.value;
            audio.selected_channel_id = 0;
            audio.selected_clip_id = 0;
            return &view.sound_rows[index];
        }
    }
    return nullptr;
}

void dispatch_sound_usage_activation(const wormedit::ui::audio_slices_view_t& view,
                                     const wormedit::ui::sound_usage_row_view_t& row,
                                     wormedit::ui::workspace_action_t& action) {
    if (action.kind != WORM_UI_WORKSPACE_ACTION_NONE || !view.usage_valid ||
        view.usage_song_id.value == 0 || view.usage_document_revision == 0 ||
        view.usage_resource_id.value == 0 || view.usage_resource_id.value != view.selected_resource_id.value)
        return;
    action.kind = WORM_UI_WORKSPACE_ACTION_SOUND_USAGE_ACTIVATE;
    action.sound_usage_song_id.value = view.usage_song_id.value;
    action.sound_usage_document_revision = view.usage_document_revision;
    action.sound_usage_resource_id.value = view.usage_resource_id.value;
    action.sound_usage_asset_id.value = view.usage_asset_id.value;
    action.sound_usage_chart_id.value = row.chart_id.value;
    action.sound_usage_measure_id.value = row.measure_id.value;
    action.sound_usage_row = row.row;
    action.sound_usage_object_id.value = row.object_id.value;
    action.sound_usage_channel_id.value = row.channel_id.value;
    action.sound_usage_clip_id.value = row.clip_id.value;
    action.sound_usage_referenced_resource_id.value = row.referenced_resource_id.value;
    action.sound_usage_referenced_asset_id.value = row.referenced_asset_id.value;
    action.sound_usage_kind = row.kind;
    action.sound_usage_scope = row.scope;
    action.sound_usage_position_valid = row.position_valid;
}

void render_sound_usage_rows(struct nk_context* context, const wormedit::ui::audio_slices_view_t& view,
                             wormedit::ui::workspace_action_t& action) {
    if (!view.usage_valid || view.usage_resource_id.value != view.selected_resource_id.value) {
        nk_layout_row_dynamic(context, 22, 1);
        nk_label(context, "Usage results are unavailable for this sound.", NK_TEXT_LEFT);
        return;
    }
    nk_layout_row_dynamic(context, 20, 1);
    nk_labelf(context, NK_TEXT_LEFT, "%zu use(s)%s", view.usage_total_count,
              view.usage_rows_truncated ? " · showing first 1,024" : "");
    if (view.usage_rows.size() == 0) {
        nk_layout_row_dynamic(context, 22, 1);
        nk_label(context, "This sound has no chart uses.", NK_TEXT_LEFT);
        return;
    }
    for (std::size_t index = 0; index < view.usage_rows.size(); ++index) {
        const auto& row = view.usage_rows[index];
        const char* chart_label = row.chart_label.c_str() ? row.chart_label.c_str() : "(unnamed chart)";
        const char* position_label = row.position_label.c_str() ? row.position_label.c_str() : "Source clip";
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

auto sound_source_copy_available(const wormedit::ui::audio_slices_view_t& view, const wormedit::ui::sound_row_view_t& sound)
    -> bool {
    return view.has_chart && view.chart_id.value != 0 && view.document_revision != 0 &&
           sound.asset_id.value != 0 && !sound.missing && sound.source_path.c_str() && sound.source_path.c_str()[0];
}

void render_sound_source_copy_button(struct nk_context* context, const wormedit::ui::audio_slices_view_t& view,
                                     const wormedit::ui::sound_row_view_t& sound,
                                     wormedit::ui::workspace_action_t& action) {
    const auto can_copy = sound_source_copy_available(view, sound);
    if (!can_copy)
        nk_widget_disable_begin(context);
    const auto clicked = nk_button_label(context, "Copy source into project");
    if (!can_copy)
        nk_widget_disable_end(context);
    if (!clicked || !can_copy || action.compose_action != WORM_UI_COMPOSE_ACTION_NONE)
        return;
    action.compose_resource_asset_id.value = sound.asset_id.value;
    action.compose_input_chart_id.value = view.chart_id.value;
    action.compose_input_document_revision = view.document_revision;
    action.compose_action = WORM_UI_COMPOSE_ACTION_COPY_RESOURCE_TO_PROJECT;
}

void reset_sound_properties(const wormedit::ui::audio_slices_view_t& view, const wormedit::ui::sound_row_view_t& sound) {
    audio.properties_project_id = view.usage_song_id.value;
    audio.properties_resource_id = sound.resource_id.value;
    audio.properties_revision = view.document_revision;
    std::snprintf(audio.properties_name, sizeof(audio.properties_name), "%s", sound.name.c_str() ? sound.name.c_str() : "");
    audio.properties_kind = std::clamp(static_cast<int>(sound.kind), 0, 3);
    audio.new_tag[0] = '\0';
}

void submit_sound_tags(const wormedit::ui::audio_slices_view_t& view, const wormedit::ui::sound_row_view_t& sound,
                       std::vector<std::string> tags, wormedit::ui::workspace_action_t& action) {
    if (action.sound_properties_apply) return;
    action.sound_properties_apply = true;
    action.sound_properties_project_id = view.usage_song_id;
    action.sound_properties_chart_id = view.has_chart ? view.chart_id : wormedit::chart_id_t{};
    action.sound_properties_resource_id = sound.resource_id;
    action.sound_properties_document_revision = view.document_revision;
    action.sound_properties_name = sound.name.c_str();
    action.sound_properties_kind = sound.kind;
    action.sound_properties_tags = std::move(tags);
}

void render_sound_tags(nk_context* context, const wormedit::ui::audio_slices_view_t& view,
                       const wormedit::ui::sound_row_view_t& sound, wormedit::ui::workspace_action_t& action) {
    nk_layout_row_dynamic(context, 20, 1);
    nk_label(context, "Tags (click to remove)", NK_TEXT_LEFT);
    nk_layout_row_dynamic(context, 25, 2);
    for (std::size_t index = 0; index < sound.tags.size(); ++index) {
        const auto label = std::string{sound.tags[index]} + "  ×";
        if (!wormedit::ui::chrome::Chip{.label = label.c_str(), .tooltip = "Remove tag"}.render(context)) continue;
        std::vector<std::string> tags(sound.tags.begin(), sound.tags.end());
        tags.erase(tags.begin() + static_cast<std::ptrdiff_t>(index));
        submit_sound_tags(view, sound, std::move(tags), action);
    }
    nk_layout_row_dynamic(context, 26, 2);
    const auto edit = wormedit::ui::chrome::worm_ui_nuklear_text_field(context,
        static_cast<nk_flags>(NK_EDIT_FIELD) | NK_EDIT_SIG_ENTER,
        audio.new_tag, sizeof(audio.new_tag), nk_filter_default);
    const bool add = nk_button_label(context, "Add tag") || (edit & NK_EDIT_COMMITED);
    if (add && audio.new_tag[0]) {
        std::vector<std::string> tags(sound.tags.begin(), sound.tags.end());
        tags.emplace_back(audio.new_tag);
        submit_sound_tags(view, sound, std::move(tags), action);
    }
}

void render_sound_kind_properties(nk_context* context, const wormedit::ui::kind_catalog_view_t<wormedit::sound_kind_t>& catalog) {
    std::array<const char*, wormedit::sound_kind_count> labels{};
    if (catalog.entries.size() == 0 || catalog.entries.size() > labels.size()) return;
    int current = 0;
    for (std::size_t index = 0; index < catalog.entries.size(); ++index) {
        labels[index] = catalog.entries[index].label.data();
        if (catalog.entries[index].id == static_cast<wormedit::sound_kind_t>(audio.properties_kind))
            current = static_cast<int>(index);
    }
    const auto selected = wormedit::ui::chrome::safe_combo(context, labels.data(),
        static_cast<int>(catalog.entries.size()), current, 26, nk_vec2(220, 160));
    audio.properties_kind = static_cast<int>(catalog.entries[static_cast<std::size_t>(selected)].id);
}

void render_editable_sound_properties(nk_context* context, const wormedit::ui::audio_slices_view_t& view,
                                      const wormedit::ui::sound_row_view_t& sound, wormedit::ui::workspace_action_t& action) {
    if (audio.properties_project_id != view.usage_song_id.value ||
        audio.properties_resource_id != sound.resource_id.value ||
        audio.properties_revision != view.document_revision)
        reset_sound_properties(view, sound);
    nk_layout_row_dynamic(context, 20, 1);
    nk_label(context, "Name", NK_TEXT_LEFT);
    nk_layout_row_dynamic(context, 28, 1);
    wormedit::ui::chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD,
        audio.properties_name, sizeof(audio.properties_name), nk_filter_default);
    nk_layout_row_dynamic(context, 20, 1);
    nk_label(context, "Kind", NK_TEXT_LEFT);
    nk_layout_row_dynamic(context, 28, 1);
    render_sound_kind_properties(context, view.sound_kinds);
    const bool changed = std::string_view{audio.properties_name} != (sound.name.c_str() ? sound.name.c_str() : "") ||
                         audio.properties_kind != static_cast<int>(sound.kind);
    const bool can_apply = changed && audio.properties_name[0] && view.usage_song_id.value &&
                           sound.resource_id.value && view.document_revision;
    nk_layout_row_dynamic(context, 28, 2);
    if (!can_apply) nk_widget_disable_begin(context);
    const auto apply = nk_button_label(context, "Apply");
    if (!can_apply) nk_widget_disable_end(context);
    if (apply && can_apply && !action.sound_properties_apply) {
        action.sound_properties_apply = true;
        action.sound_properties_project_id = view.usage_song_id;
        action.sound_properties_chart_id = view.has_chart ? view.chart_id : wormedit::chart_id_t{};
        action.sound_properties_resource_id = sound.resource_id;
        action.sound_properties_document_revision = view.document_revision;
        action.sound_properties_name = audio.properties_name;
        action.sound_properties_kind = static_cast<wormedit::sound_kind_t>(audio.properties_kind);
    }
    if (nk_button_label(context, "Revert")) reset_sound_properties(view, sound);
    nk_layout_row_dynamic(context, 22, 1);
}

void render_sound_properties_column(struct nk_context* context, const wormedit::ui::audio_slices_view_t& view,
                                    wormedit::ui::workspace_action_t& action) {
    nk_layout_row_dynamic(context, 22, 1);
    nk_label(context, "Properties", NK_TEXT_LEFT);
    wormedit::ui::chrome::section_separator(context);
    const auto* selected = selected_sound(view);
    nk_layout_row_dynamic(context, 22, 1);
    if (!selected) {
        nk_label(context, "Select a sound to see its properties.", NK_TEXT_LEFT);
        return;
    }
    render_editable_sound_properties(context, view, *selected, action);
    if (selected->bmson_channel_id && view.has_chart && !view.timing_locked) {
        const bool stem = selected->kind == wormedit::sound_kind_t::stem;
        if (nk_button_label(context, stem ? "Kind: Keysound (pack into columns)" : "Kind: Stem (make stem)"))
            request_bmson_lane_mode(action, view.usage_song_id.value, view.chart_id.value, view.document_revision,
                selected->bmson_channel_id.value, !stem, selected->bmson_channel_continues, true);
    }
    nk_labelf(context, NK_TEXT_LEFT, "Used %llu time(s)", static_cast<unsigned long long>(selected->use_count));
    if (selected->sample_rate != 0) {
        nk_labelf(context, NK_TEXT_LEFT, "%.2f s · %u Hz",
                  static_cast<double>(selected->duration_frames) / selected->sample_rate,
                  selected->sample_rate);
    }
    render_sound_tags(context, view, *selected, action);
    if (!selected->source_path.empty()) {
        nk_label(context, selected->source_path.c_str(), NK_TEXT_LEFT);
    }
    if (selected->missing) {
        nk_label_colored(context, "The audio file is missing.", NK_TEXT_LEFT,
                         nk_rgb(255, 120, 120));
    }
    if (view.selected_channel_id.value != 0 || view.selected_clip_id.value != 0) {
        nk_layout_row_dynamic(context, 20, 1);
        nk_labelf(context, NK_TEXT_LEFT, "Source channel %llu · clip %llu",
                  static_cast<unsigned long long>(view.selected_channel_id.value),
                  static_cast<unsigned long long>(view.selected_clip_id.value));
    }
    nk_layout_row_dynamic(context, 25, 1);
    if (nk_button_label(context, audio.find_uses_open ? "Hide uses" : "Find uses")) {
        if (!audio.find_uses_open && audio.selected_resource_id == 0) {
            audio.selected_project_id = view.usage_song_id.value;
            audio.selected_resource_id = selected->resource_id.value;
        }
        audio.find_uses_open = !audio.find_uses_open;
    }
    if (audio.find_uses_open)
        render_sound_usage_rows(context, view, action);
    render_sound_source_copy_button(context, view, *selected, action);
}

void render_sound_columns(struct nk_context* context, const wormedit::ui::audio_slices_view_t& view,
                          wormedit::ui::workspace_action_t& action) {
    const auto height = wormedit::ui::layout::remaining_content_height(context,
        {.minimum = 80.0f, .reserved = 8.0f});
    constexpr auto gutter = wormedit::ui::metrics::gap_large;
    const auto available = std::max(1.0f, nk_window_get_content_region(context).w -
        gutter * 2 - context->style.window.spacing.x * 4);
    const float widths[] = {gutter, available * 0.27f, available * 0.43f,
                             available * 0.30f, gutter};
    nk_layout_row(context, NK_STATIC, height, 5, widths);
    nk_label(context, "", NK_TEXT_LEFT);
    if (nk_group_begin(context, "sound_list", NK_WINDOW_BORDER)) {
        render_sound_list_column(context, view, action);
        nk_group_end(context);
    }
    if (nk_group_begin(context, "sound_wave", NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) {
        nk_layout_row_dynamic(context, 22, 1);
        nk_label(context, "Waveform", NK_TEXT_LEFT);
        wormedit::ui::chrome::section_separator(context);
        render_waveform_column(context, &view, &action, false, view.waveform_columns);
        nk_group_end(context);
    }
    if (nk_group_begin(context, "sound_properties", NK_WINDOW_BORDER)) {
        render_sound_properties_column(context, view, action);
        nk_group_end(context);
    }
    nk_label(context, "", NK_TEXT_LEFT);
}

} // namespace

void worm_ui_nuklear_render_audio_slices(struct nk_context* context,
                                         const wormedit::ui::audio_slices_view_t* view,
                                         wormedit::ui::workspace_action_t* action, bool show_asset_catalog,
                                         bool window_focused,
                                         std::span<const wormedit::ui::audio_waveform_column_view_t> columns) {
    if (!context || !view || !action) {
        return;
    }
    if (view->usage_song_id.value != 0 && audio.selected_project_id != view->usage_song_id.value) {
        audio.selected_project_id = view->usage_song_id.value;
        audio.selected_resource_id = view->selected_resource_id.value;
        audio.selected_channel_id = view->selected_channel_id.value;
        audio.selected_clip_id = view->selected_clip_id.value;
        audio.find_uses_open = false;
        audio.waveform_asset_id = 0;
        audio.sound_list_focused = false;
        audio.list_move_pending = 0;
        audio.list_audition_pending = false;
    }
    const bool input_allowed = window_focused && context->current &&
        !(context->current->flags & NK_WINDOW_NO_INPUT);
    if (!input_allowed || audio.sound_list_document_revision != view->document_revision) {
        audio.waveform_pan_active = false;
        audio.sound_list_focused = false;
        audio.list_move_pending = 0;
        audio.list_audition_pending = false;
    }
    if (!input_allowed) audio.offset_cancel_pending = true;
    cancel_stale_offset_drag(*view, *action, input_allowed);
    const audio_input_scope_t input_scope{context, input_allowed};
    audio.sound_list_document_revision = view->document_revision;
    audio.sound_list_valid = false;
    audio.import_drop_valid = false;
    if (show_asset_catalog) {
        const auto content = nk_window_get_content_region(context);
        const auto clip = nk_window_get_canvas(context)->clip;
        const auto overlap = wormedit::ui::rect_intersection(
            {content.x, content.y, content.w, content.h}, {clip.x, clip.y, clip.w, clip.h});
        audio.import_drop_x = overlap.x; audio.import_drop_y = overlap.y;
        audio.import_drop_width = overlap.w;
        audio.import_drop_height = overlap.h;
        audio.import_drop_valid = audio.import_drop_width > 0 && audio.import_drop_height > 0;
        nk_layout_row_dynamic(context, 28, 2);
        if (nk_button_label(context, "Add sounds…"))
            action->sound_import_pick_requested = true;
        nk_label(context, "Drop audio files anywhere here to import", NK_TEXT_RIGHT);
        render_sound_columns(context, *view, *action);
        render_bmson_lane_mode_confirmation(context, *action, view->usage_song_id.value, view->chart_id.value,
            view->document_revision, window_focused, true);
        return;
    }
    if (show_asset_catalog) {
        render_audio_asset_catalog(context, *view, *action);
    }
    render_waveform_column(context, view, action, !show_asset_catalog,
        columns.empty() ? std::span<const wormedit::ui::audio_waveform_column_view_t>{view->waveform_columns} : columns);
}

void worm_ui_frontend_sound_usage_request(wormedit::ui::sound_usage_request_t* request) {
    if (!request)
        return;
    const auto& state = worm_ui_nuklear_audio_state();
    request->selected_project_id = wormedit::song_id_t{state.selected_project_id};
    request->selected_resource_id = wormedit::resource_id_t{state.selected_resource_id};
    request->selected_channel_id = wormedit::object_id_t{state.selected_channel_id};
    request->selected_clip_id = wormedit::clip_id_t{state.selected_clip_id};
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

void worm_ui_nuklear_audio_waveform_viewport_request(wormedit::ui::audio_waveform_viewport_request_t& request) noexcept {
    request = {};
    if (!audio.waveform_asset_id || !wormedit::audio::is_waveform_viewport_valid(audio.waveform_viewport)) return;
    const auto& viewport = audio.waveform_viewport;
    request.asset_id = wormedit::asset_id_t{audio.waveform_asset_id};
    request.total_frames = viewport.total_frames;
    request.begin_frame = viewport.begin_frame;
    request.frames_per_pixel = viewport.frames_per_pixel;
    request.pixel_width = viewport.pixel_width;
    request.valid = true;
}
