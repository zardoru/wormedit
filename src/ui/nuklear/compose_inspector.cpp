#include "ui/nuklear/compose_inspector.h"
#include "ui/nuklear/meter_state.h"
#include "ui/nuklear/chrome.h"
#include "ui/nuklear/compose_panel_internal.h"
#include "ui/nuklear/compose_resources.h"
#include "ui/nuklear/compose_markers.h"
#include "ui/nuklear/timing_stage_editor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ranges>

static void render_initial_bpm_control(const ComposeInspectorContext& inspector) {
    auto* context = inspector.context;
    const auto* timing_view = inspector.timing_view;
    auto* action = inspector.action;
    nk_layout_row_begin(context, NK_DYNAMIC, 25, 2);
    nk_layout_row_push(context, wormedit::ui::chrome::property_label_ratio);
    if (timing_view->initial_bpm_present && std::isfinite(timing_view->initial_bpm_value))
        nk_labelf(context, NK_TEXT_LEFT, "BPM: %.6g", timing_view->initial_bpm_value);
    else
        nk_label(context, "BPM: unset", NK_TEXT_LEFT);
    nk_layout_row_push(context, 1.0f - wormedit::ui::chrome::property_label_ratio);
    const auto can_edit_initial_bpm = timing_view->initial_bpm_editable &&
        action->timing_stage_action == WORM_UI_TIMING_STAGE_NONE;
    if (!can_edit_initial_bpm) nk_widget_disable_begin(context);
    const auto clicked = nk_button_label(context, "Set BPM");
    if (!can_edit_initial_bpm) nk_widget_disable_end(context);
    nk_layout_row_end(context);
    if (clicked && can_edit_initial_bpm) {
        action->timing_stage_action = WORM_UI_TIMING_STAGE_BEGIN;
        action->timing_chart_id = timing_view->chart_id;
        action->timing_document_revision = timing_view->document_revision;
        action->timing_lane = WORM_UI_TIMING_LANE_BPM;
        action->timing_object_id = timing_view->initial_bpm_event_id;
        action->timing_has_target_y = timing_view->initial_bpm_event_id == 0;
        action->timing_target = WormUiTimingTarget{0.0, 1.0, 0.0, 1.0, false};
    }
}

static void render_timing_lane_visibility(const ComposeInspectorContext& inspector) {
    auto* context = inspector.context;
    const auto* view = inspector.view;
    const auto* timing_view = inspector.timing_view;
    auto* action = inspector.action;
    if (!nk_tree_push(context, NK_TREE_TAB, "Timing lanes", NK_MINIMIZED)) return;
    constexpr std::array<const char*, WORM_UI_TIMING_LANE_COUNT> labels{
        "BPM", "SV", "SPD", "STP", "DLY", "WRP", "KIA", "ANC"};
    for (std::size_t row = 0; row < 4; ++row) {
        nk_layout_row_begin(context, NK_DYNAMIC, 24, 2);
        for (std::size_t column = 0; column < 2; ++column) {
            const auto lane = row * 2 + column;
            const auto bit = static_cast<std::uint8_t>(1U << lane);
            const auto visible = (view->timing_lane_visibility_mask & bit) != 0;
            const auto editing_lane = timing_view->stage_active &&
                                      static_cast<std::size_t>(timing_view->stage_lane) == lane;
            char label[16]{};
            (void)snprintf(label, sizeof(label), "%s: %s", labels[lane], visible ? "On" : "Off");
            nk_layout_row_push(context, wormedit::ui::chrome::property_label_ratio);
            const auto disabled = !view->scene_available || (editing_lane && visible);
            if (disabled) nk_widget_disable_begin(context);
            const auto clicked = nk_button_label(context, label);
            if (disabled) nk_widget_disable_end(context);
            if (clicked && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
                action->compose_timing_lane = static_cast<std::uint8_t>(lane);
                action->compose_action = WORM_UI_COMPOSE_ACTION_TOGGLE_TIMING_LANE_VISIBILITY;
            }
        }
        nk_layout_row_end(context);
    }
    nk_tree_pop(context);
}

static void render_selected_bmson_slice_controls(const ComposeInspectorContext& inspector) {
    const auto& view = *inspector.view;
    auto* context = inspector.context;
    auto* action = inspector.action;
    const bool enabled = view.scene_available && view.selected_bmson_bgm_event_id != 0 &&
                         view.selected_bmson_source_lane_id != 0;
    nk_layout_row_dynamic(context, 18, 1);
    nk_label(context, "BMSON slice", NK_TEXT_LEFT);
    nk_layout_row_dynamic(context, 24, 2);
    if (!enabled) nk_widget_disable_begin(context);
    const auto restart_clicked = nk_button_label(context, "Restart");
    const auto continue_clicked = nk_button_label(context, "Continue");
    if (!enabled) nk_widget_disable_end(context);
    if (!enabled || action->compose_action != WORM_UI_COMPOSE_ACTION_NONE ||
        (!restart_clicked && !continue_clicked)) return;
    action->compose_action = restart_clicked ? WORM_UI_COMPOSE_ACTION_SET_BMSON_BGM_RESTART
                                            : WORM_UI_COMPOSE_ACTION_SET_BMSON_BGM_CONTINUE;
    action->compose_bmson_bgm_event_id = view.selected_bmson_bgm_event_id;
    action->compose_input_chart_id = view.chart_id;
    action->compose_input_document_revision = view.document_revision;
}

static void render_staged_bmson_regions(const ComposeInspectorContext& inspector) {
    const auto* view = inspector.view;
    auto* context = inspector.context;
    auto* action = inspector.action;
    if (view->staged_bmson_marker_count == 0 ||
        !nk_tree_push(context, NK_TREE_TAB, "Staged BMSON regions", NK_MAXIMIZED))
        return;
    state.bmson_audition_index = std::clamp(state.bmson_audition_index, 1,
                                            static_cast<int>(view->staged_bmson_marker_count));
    nk_layout_row_dynamic(context, 24, 1);
    nk_property_int(context, "Region", 1, &state.bmson_audition_index,
                    static_cast<int>(view->staged_bmson_marker_count), 1, 1.0f);
    const auto selected_index = static_cast<std::size_t>(state.bmson_audition_index - 1);
    const auto assigned_lane = view->staged_bmson_marker_playable_lanes[selected_index];
    nk_labelf(context, NK_TEXT_LEFT, "%s · %s",
              view->staged_bmson_marker_restarts[selected_index] ? "Restart" : "Continue",
              assigned_lane == 0 ? "BGM" : "Playable key lane");
    const auto selected = static_cast<std::uint64_t>(state.bmson_audition_index);
    if (nk_button_label(context, "Audition staged region"))
        append_compose_input(action, WORM_UI_COMPOSE_INPUT_BMSON_AUDITION_DRAFT,
                             0.0, 0.0, 0, false, WORM_UI_COMPOSE_SELECTION_REPLACE,
                             WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD, selected);
    nk_layout_row_dynamic(context, 24, 2);
    if (nk_button_label(context, "Assign key"))
        append_compose_input(action, WORM_UI_COMPOSE_INPUT_BMSON_ASSIGN_DRAFT_TO_ACTIVE_LANE,
                             0.0, 0.0, 0, false, WORM_UI_COMPOSE_SELECTION_REPLACE,
                             WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD, selected);
    if (nk_button_label(context, "Assign BGM"))
        append_compose_input(action, WORM_UI_COMPOSE_INPUT_BMSON_ASSIGN_DRAFT_TO_BGM,
                             0.0, 0.0, 0, false, WORM_UI_COMPOSE_SELECTION_REPLACE,
                             WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD, selected);
    if (nk_button_label(context, "Restart"))
        append_compose_input(action, WORM_UI_COMPOSE_INPUT_BMSON_TOGGLE_DRAFT_RESTART,
                             0.0, 0.0, 0, false, WORM_UI_COMPOSE_SELECTION_REPLACE,
                             WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD, selected);
    if (nk_button_label(context, "Remove"))
        append_compose_input(action, WORM_UI_COMPOSE_INPUT_BMSON_REMOVE_DRAFT,
                             0.0, 0.0, 0, false, WORM_UI_COMPOSE_SELECTION_REPLACE,
                             WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD, selected);
    nk_tree_pop(context);
}

static void render_compose_marker_section(const ComposeInspectorContext& inspector) {
    const auto* view = inspector.view;
    auto* context = inspector.context;
    auto* action = inspector.action;
    if (!nk_tree_push(context, NK_TREE_TAB, "Markers / sections", NK_MINIMIZED)) return;
    nk_layout_row_begin(context, NK_DYNAMIC, 24, 2);
    const auto disabled = !view->scene_available || !view->beat_marker_navigation_available;
    if (disabled) nk_widget_disable_begin(context);
    nk_layout_row_push(context, wormedit::ui::chrome::property_label_ratio);
    const auto previous_clicked = nk_button_label(context, "Prev marker");
    nk_layout_row_push(context, wormedit::ui::chrome::property_label_ratio);
    const auto next_clicked = nk_button_label(context, "Next marker");
    if (disabled) nk_widget_disable_end(context);
    nk_layout_row_end(context);
    nk_layout_row_dynamic(context, 36, 1);
    nk_label_wrap(context, view->beat_marker_navigation_available
                          ? "Navigate bookmarks and sections"
                          : "No bookmarks or sections");
    if (action->kind == WORM_UI_WORKSPACE_ACTION_NONE) {
        if (previous_clicked)
            action->kind = WORM_UI_WORKSPACE_ACTION_NAVIGATE_PREVIOUS_MARKER;
        else if (next_clicked)
            action->kind = WORM_UI_WORKSPACE_ACTION_NAVIGATE_NEXT_MARKER;
    }
    worm_ui_nuklear_render_compose_markers(context, view, action);
    nk_tree_pop(context);
}

// Lets the layout at the cursor switch between profiles with the same lane count.
static void render_lane_profile_section(const ComposeInspectorContext& inspector) {
    const auto* view = inspector.view;
    auto* context = inspector.context;
    auto* action = inspector.action;
    if (view->lane_profile_choice_count == 0) return;
    if (!nk_tree_push(context, NK_TREE_TAB, "Lane profile", NK_MAXIMIZED)) return;
    std::array<const char*, WORM_UI_COMPOSE_LANE_PROFILE_CAPACITY> names{};
    for (std::size_t index = 0; index < view->lane_profile_choice_count; ++index)
        names[index] = view->lane_profile_names[index];
    const int current = std::max<int>(view->lane_profile_current, 0);
    nk_layout_row_dynamic(context, 26, 1);
    const int selected = wormedit::ui::chrome::safe_combo(
        context, names.data(), static_cast<int>(view->lane_profile_choice_count), current, 24,
        nk_vec2(220.0f, 120.0f));
    if (selected != current && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
        action->compose_action = WORM_UI_COMPOSE_ACTION_SET_LANE_PROFILE;
        action->compose_lane_profile_index = static_cast<uint32_t>(selected);
        action->compose_input_chart_id = view->chart_id;
        action->compose_input_document_revision = view->document_revision;
    }
    nk_tree_pop(context);
}

static void render_lane_layout_editor(const ComposeInspectorContext& inspector) {
    const auto* view = inspector.view;
    auto* context = inspector.context;
    auto* action = inspector.action;
    if (!nk_tree_push(context, NK_TREE_TAB, "Lane layout segment", NK_MAXIMIZED)) return;
    if (!view->lane_layout_edit_available) {
        nk_layout_row_dynamic(context, 30, 1);
        nk_label_wrap(context, "Layout changes are unavailable at a virtual-only cursor measure.");
        nk_tree_pop(context);
        return;
    }
    if (view->lane_layout_profile_choice_count == 0) {
        nk_layout_row_dynamic(context, 30, 1);
        nk_label_wrap(context, "No built-in layout profiles are available for this chart family.");
        nk_tree_pop(context);
        return;
    }
    struct selection_state_t {
        std::uint64_t chart{};
        std::uint64_t revision{};
        std::uint32_t measure{};
        int selected{};
    };
    static selection_state_t selection;
    if (selection.chart != view->chart_id || selection.revision != view->document_revision ||
        selection.measure != view->lane_layout_edit_measure_number) {
        selection.chart = view->chart_id;
        selection.revision = view->document_revision;
        selection.measure = view->lane_layout_edit_measure_number;
        selection.selected = std::max<int>(view->lane_layout_profile_current, 0);
    }
    std::array<const char*, WORM_UI_COMPOSE_LAYOUT_PROFILE_CAPACITY> names{};
    for (std::size_t index = 0; index < view->lane_layout_profile_choice_count; ++index)
        names[index] = view->lane_layout_profile_names[index];
    nk_layout_row_dynamic(context, 18, 1);
    nk_labelf(context, NK_TEXT_LEFT, "Measure %u · %s", view->lane_layout_edit_measure_number,
              view->lane_layout_edit_is_update ? "Update existing segment" : "Insert new segment");
    nk_layout_row_dynamic(context, 26, 1);
    selection.selected = std::clamp(selection.selected, 0,
                                    static_cast<int>(view->lane_layout_profile_choice_count) - 1);
    selection.selected = wormedit::ui::chrome::safe_combo(
        context, names.data(), static_cast<int>(view->lane_layout_profile_choice_count),
        selection.selected, 24, nk_vec2(220.0f, 180.0f));
    char button[64]{};
    (void)snprintf(button, sizeof(button), "%s layout at measure %u",
                   view->lane_layout_edit_is_update ? "Update" : "Insert",
                   view->lane_layout_edit_measure_number);
    const auto disabled = action->compose_action != WORM_UI_COMPOSE_ACTION_NONE;
    if (disabled) nk_widget_disable_begin(context);
    const auto clicked = nk_button_label(context, button);
    if (disabled) nk_widget_disable_end(context);
    if (clicked && !disabled) {
        action->compose_action = WORM_UI_COMPOSE_ACTION_APPLY_LANE_LAYOUT_PROFILE;
        action->compose_input_chart_id = view->chart_id;
        action->compose_input_document_revision = view->document_revision;
        action->compose_measure_number = view->lane_layout_edit_measure_number;
        std::snprintf(action->compose_lane_layout_profile_id,
                      sizeof(action->compose_lane_layout_profile_id), "%s",
                      view->lane_layout_profile_ids[static_cast<std::size_t>(selection.selected)]);
    }
    nk_tree_pop(context);
}

void render_compose_timing_inspector(const ComposeInspectorContext& inspector) {
    render_lane_layout_editor(inspector);
    render_lane_profile_section(inspector);
    const auto& timing = *inspector.timing_view;
    nk_layout_row_dynamic(inspector.context, 18, 1);
    nk_labelf(inspector.context, NK_TEXT_LEFT, "%zu measures · %zu BPM · %zu stop/delay",
              timing.measure_count, timing.bpm_count, timing.pause_count);
    nk_layout_row_dynamic(inspector.context, 18, 1);
    nk_labelf(inspector.context, NK_TEXT_LEFT, "%zu warp · %zu SPEEDS · %zu anchors",
              timing.warp_count, timing.speed_total_count, timing.anchor_count);
    render_initial_bpm_control(inspector);
    render_timing_lane_visibility(inspector);
    if (!state.inline_timing_open && inspector.timing_view->stage_active &&
        inspector.action->timing_stage_action == WORM_UI_TIMING_STAGE_NONE &&
        nk_tree_push(inspector.context, NK_TREE_TAB, "Timing event", NK_MAXIMIZED)) {
        worm_ui_nuklear_render_timing_stage_editor(inspector.context, inspector.timing_view,
                                                   inspector.action);
        nk_tree_pop(inspector.context);
    }
    worm_ui_nuklear_render_compose_resources(inspector.context, inspector.view,
                                             inspector.action, &state);
    if (inspector.view->waveform_analysis_pending) {
        nk_layout_row_dynamic(inspector.context, 18, 1);
        nk_label(inspector.context, "Building waveform...", NK_TEXT_LEFT);
    }
    render_selected_bmson_slice_controls(inspector);
    render_staged_bmson_regions(inspector);
    render_compose_marker_section(inspector);
}

void render_compose_note_label_editor(const ComposeInspectorContext& inspector) {
    const auto* view = inspector.view;
    auto* context = inspector.context;
    auto* action = inspector.action;
    nk_layout_row_dynamic(context, 18, 1);
    nk_labelf(context, NK_TEXT_LEFT, "Label (%zu)", view->selected_note_count);
    const auto disabled = !view->scene_available || view->selected_note_count == 0;
    if (disabled) nk_widget_disable_begin(context);
    nk_layout_row_dynamic(context, 25, 1);
    (void)wormedit::ui::chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD, state.note_label.data(),
                                         state.note_label.size(), nk_filter_default);
    if (view->focus_note_label_editor) {
        nk_edit_focus(context, NK_EDIT_GOTO_END_ON_ACTIVATE);
        action->compose_note_label_focus_consumed = true;
    }
    nk_layout_row_dynamic(context, 25, 2);
    if (nk_button_label(context, "Apply") &&
        action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
        memcpy(action->compose_note_label, state.note_label.data(),
               sizeof(action->compose_note_label));
        action->compose_action = WORM_UI_COMPOSE_ACTION_APPLY_NOTE_LABEL;
    }
    if (nk_button_label(context, "Clear") &&
        action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
        action->compose_action = WORM_UI_COMPOSE_ACTION_CLEAR_NOTE_LABEL;
    if (disabled) nk_widget_disable_end(context);
}

void render_compose_label_search(const ComposeInspectorContext& inspector) {
    const auto* view = inspector.view;
    auto* context = inspector.context;
    auto* action = inspector.action;
    nk_layout_row_dynamic(context, 18, 1);
    nk_label(context, "Find label", NK_TEXT_LEFT);
    nk_layout_row_dynamic(context, 25, 1);
    (void)wormedit::ui::chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD, state.label_search.data(),
                                         state.label_search.size(), nk_filter_default);
    const auto disabled = !view->scene_available || state.label_search[0] == '\0';
    if (disabled) nk_widget_disable_begin(context);
    nk_layout_row_dynamic(context, 25, 3);
    const auto submit = [&](const char* title, WormUiComposeActionKind kind) {
        if (!nk_button_label(context, title) ||
            action->compose_action != WORM_UI_COMPOSE_ACTION_NONE)
            return;
        memcpy(action->compose_note_label, state.label_search.data(),
               state.label_search.size());
        action->compose_action = kind;
        action->compose_input_chart_id = view->chart_id;
        action->compose_input_document_revision = view->document_revision;
    };
    submit("Select", WORM_UI_COMPOSE_ACTION_SEARCH_NOTE_LABEL);
    submit("Previous", WORM_UI_COMPOSE_ACTION_SEARCH_PREVIOUS_NOTE_LABEL);
    submit("Next", WORM_UI_COMPOSE_ACTION_SEARCH_NEXT_NOTE_LABEL);
    if (disabled) nk_widget_disable_end(context);
}

// Time signature of the cursor measure: typed numerator and denominator, an Apply button and the
// equal-length signatures the typed one can be written as (3/4 also reads 6/8 and 12/16).
static void render_compose_meter_control(const ComposeInspectorContext& inspector) {
    const auto* view = inspector.view;
    auto* context = inspector.context;
    auto* action = inspector.action;
    nk_layout_row_begin(context, NK_DYNAMIC, 24, 5);
    nk_layout_row_push(context, 0.2f);
    nk_label(context, "Meter", NK_TEXT_LEFT);
    nk_layout_row_push(context, 0.2f);
    (void)wormedit::ui::chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD, state.meter_numerator.data(),
                                         state.meter_numerator.size(), nk_filter_decimal);
    nk_layout_row_push(context, 0.06f);
    nk_label(context, "/", NK_TEXT_CENTERED);
    nk_layout_row_push(context, 0.2f);
    (void)wormedit::ui::chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD, state.meter_denominator.data(),
                                         state.meter_denominator.size(), nk_filter_decimal);
    nk_layout_row_push(context, 0.34f);
    const auto apply = nk_button_label(context, "Apply");
    nk_layout_row_end(context);
    const auto suggestions = worm_ui_nuklear_suggest_meters(state.meter_numerator.data(),
                                                            state.meter_denominator.data());
    if (suggestions.count != 0) {
        nk_layout_row_dynamic(context, 24, static_cast<int>(suggestions.count) + 1);
        nk_label(context, "Suggest", NK_TEXT_LEFT);
        for (std::size_t index = 0; index < suggestions.count; ++index) {
            const auto& item = suggestions.items[index];
            std::array<char, 16> label{};
            std::snprintf(label.data(), label.size(), "%u/%u", item.numerator, item.denominator);
            if (!nk_button_label(context, label.data())) continue;
            std::snprintf(state.meter_numerator.data(), state.meter_numerator.size(), "%u", item.numerator);
            std::snprintf(state.meter_denominator.data(), state.meter_denominator.size(), "%u",
                          item.denominator);
        }
    }
    if (!apply || action->compose_action != WORM_UI_COMPOSE_ACTION_NONE) return;
    action->compose_action = WORM_UI_COMPOSE_ACTION_SET_MEASURE_METER;
    action->compose_input_chart_id = view->chart_id;
    action->compose_input_document_revision = view->document_revision;
    std::snprintf(action->timing_meter_numerator_text, sizeof(action->timing_meter_numerator_text), "%s",
                  state.meter_numerator.data());
    std::snprintf(action->timing_meter_denominator_text, sizeof(action->timing_meter_denominator_text), "%s",
                  state.meter_denominator.data());
}

void render_compose_measure_section(const ComposeInspectorContext& inspector) {
    const auto* view = inspector.view;
    auto* context = inspector.context;
    auto* action = inspector.action;
    if (!nk_tree_push(context, NK_TREE_TAB, "Measure at cursor", NK_MINIMIZED)) return;
    if (!view->scene_available) nk_widget_disable_begin(context);
    const auto entry = [&](const char* title, WormUiComposeActionKind kind) {
        nk_layout_row_dynamic(context, 24, 1);
        if (!nk_button_label(context, title) ||
            action->compose_action != WORM_UI_COMPOSE_ACTION_NONE)
            return;
        action->compose_action = kind;
        action->compose_input_chart_id = view->chart_id;
        action->compose_input_document_revision = view->document_revision;
    };
    entry("Insert measure before", WORM_UI_COMPOSE_ACTION_INSERT_MEASURE_BEFORE);
    entry("Insert measure after", WORM_UI_COMPOSE_ACTION_INSERT_MEASURE_AFTER);
    entry("Remove empty measure", WORM_UI_COMPOSE_ACTION_REMOVE_EMPTY_MEASURE);
    render_compose_meter_control(inspector);
    if (!view->scene_available) nk_widget_disable_end(context);
    nk_tree_pop(context);
}

void render_compose_transform_section(const ComposeInspectorContext& inspector) {
    const auto* view = inspector.view;
    auto* context = inspector.context;
    auto* action = inspector.action;
    if (!nk_tree_push(context, NK_TREE_TAB, "Transform", NK_MINIMIZED)) return;
    const auto enabled = view->scene_available && view->selected_note_count != 0;
    if (!enabled) nk_widget_disable_begin(context);
    const auto entry = [&](const char* title, WormUiComposeActionKind kind) {
        nk_layout_row_dynamic(context, 24, 1);
        if (!nk_button_label(context, title) ||
            action->compose_action != WORM_UI_COMPOSE_ACTION_NONE)
            return;
        action->compose_action = kind;
        action->compose_input_chart_id = view->chart_id;
        action->compose_input_document_revision = view->document_revision;
    };
    entry("Mirror lanes", WORM_UI_COMPOSE_ACTION_TRANSFORM_MIRROR);
    entry("Rotate lanes left", WORM_UI_COMPOSE_ACTION_TRANSFORM_ROTATE_LEFT);
    entry("Rotate lanes right", WORM_UI_COMPOSE_ACTION_TRANSFORM_ROTATE_RIGHT);
    entry("Shift earlier by one snap", WORM_UI_COMPOSE_ACTION_TRANSFORM_SHIFT_EARLIER);
    entry("Shift later by one snap", WORM_UI_COMPOSE_ACTION_TRANSFORM_SHIFT_LATER);
    if (!enabled) nk_widget_disable_end(context);
    nk_tree_pop(context);
}

void render_compose_quantize_section(const ComposeInspectorContext& inspector) {
    const auto* view = inspector.view;
    auto* context = inspector.context;
    auto* action = inspector.action;
    nk_layout_row_dynamic(context, 18, 1);
    nk_labelf(context, NK_TEXT_LEFT, "Quantize (%zu)", view->selected_note_count);
    nk_layout_row_dynamic(context, 18, 1);
    if (view->selected_note_count == 0) {
        nk_label(context, "Select notes first", NK_TEXT_LEFT);
    } else if (view->quantize_preview_blocked != 0) {
        nk_labelf(context, NK_TEXT_LEFT, "Blocked: %zu issue(s)", view->quantize_preview_blocked);
        nk_layout_row_dynamic(context, 32, 1);
        nk_label_wrap(context, view->quantize_preview_message);
    } else {
        nk_labelf(context, NK_TEXT_LEFT, "%zu would move, %zu unchanged",
                  view->quantize_preview_moved, view->quantize_preview_unchanged);
    }
    const auto enabled = view->scene_available && view->selected_note_count != 0 &&
                         view->quantize_preview_blocked == 0 && view->quantize_preview_moved != 0;
    if (!enabled) nk_widget_disable_begin(context);
    nk_layout_row_dynamic(context, 25, 1);
    const auto clicked = nk_button_label(context, "Quantize to snap");
    if (!enabled) nk_widget_disable_end(context);
    if (clicked && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
        action->compose_action = WORM_UI_COMPOSE_ACTION_AREA_QUANTIZE;
        action->compose_input_chart_id = view->chart_id;
        action->compose_input_document_revision = view->document_revision;
    }
}

auto semantic_layer_target_active(const WormUiComposeView* view) -> bool {
    for (size_t index = 0; index < view->semantic_layer_count; ++index)
        if (view->semantic_layers[index].selected) return true;
    return false;
}

static void render_semantic_layer_page_controls(const ComposeInspectorContext& inspector) {
    const auto* view = inspector.view;
    auto* context = inspector.context;
    auto* action = inspector.action;
    nk_layout_row_dynamic(context, 24, 1);
    if (nk_button_label(context, "Add semantic layer") &&
        action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
        action->compose_action = WORM_UI_COMPOSE_ACTION_ADD_SEMANTIC_LAYER;
    nk_layout_row_dynamic(context, 24, 1);
    if (view->semantic_layer_page_count == 0) {
        nk_label(context, "Palette empty", NK_TEXT_CENTERED);
    } else {
        nk_labelf(context, NK_TEXT_CENTERED, "Page %zu/%zu · %zu layers",
                  view->semantic_layer_page + 1, view->semantic_layer_page_count,
                  view->semantic_layer_total_count);
    }
    nk_layout_row_dynamic(context, 24, 2);
    if (view->semantic_layer_page == 0) nk_widget_disable_begin(context);
    const auto previous_clicked = nk_button_label(context, "Previous");
    if (view->semantic_layer_page == 0) nk_widget_disable_end(context);
    const auto last_page = view->semantic_layer_page_count == 0 ||
                           view->semantic_layer_page + 1 >= view->semantic_layer_page_count;
    if (last_page) nk_widget_disable_begin(context);
    const auto next_clicked = nk_button_label(context, "Next");
    if (last_page) nk_widget_disable_end(context);
    if (action->compose_action != WORM_UI_COMPOSE_ACTION_NONE) return;
    if (previous_clicked)
        action->compose_action = WORM_UI_COMPOSE_ACTION_PREVIOUS_SEMANTIC_LAYER_PAGE;
    else if (next_clicked)
        action->compose_action = WORM_UI_COMPOSE_ACTION_NEXT_SEMANTIC_LAYER_PAGE;
}

static void fit_semantic_layer_label(nk_context* context, char* label,
                                     std::size_t capacity) {
    const auto* font = context->style.font;
    const auto width = nk_widget_bounds(context).w -
                       2.0f * (context->style.button.padding.x + context->style.button.border +
                               context->style.button.rounding);
    const auto length = static_cast<int>(strlen(label));
    if (font->width(font->userdata, font->height, label, length) <= width) return;
    const auto dots_width = font->width(font->userdata, font->height, "...", 3);
    auto end = length;
    while (end > 0 && font->width(font->userdata, font->height, label, end) + dots_width > width)
        --end;
    // Keep the ellipsis on a UTF-8 boundary when names contain non-ASCII text.
    while (end > 0 && (static_cast<unsigned char>(label[end]) & 0xc0U) == 0x80U)
        --end;
    snprintf(label + end, capacity - static_cast<std::size_t>(end), "...");
}

static void render_semantic_layer_button(const ComposeInspectorContext& inspector,
                                         const auto& layer, std::size_t index) {
    const auto* view = inspector.view;
    auto* context = inspector.context;
    auto* action = inspector.action;
    const bool assign_selection = view->selected_note_count != 0 && !layer.locked;
    std::array<char, 96> label{};
    auto name_length = std::min<std::size_t>(strlen(layer.name), 24);
    while (name_length > 0 &&
           (static_cast<unsigned char>(layer.name[name_length]) & 0xc0U) == 0x80U)
        --name_length;
    snprintf(label.data(), label.size(), "%zu:%.*s #%llu%s%s", index + 1,
             static_cast<int>(name_length), layer.name,
             static_cast<unsigned long long>(layer.layer_id),
             layer.visible ? "" : " [hidden]", layer.selected ? " *" : "");
    nk_layout_row_dynamic(context, 25, 1);
    const auto palette_color = nk_rgba(layer.red, layer.green, layer.blue, layer.alpha);
    nk_style_push_style_item(context, &context->style.button.normal,
                             nk_style_item_color(palette_color));
    nk_style_push_style_item(context, &context->style.button.hover,
                             nk_style_item_color(palette_color));
    nk_style_push_style_item(context, &context->style.button.active,
                             nk_style_item_color(palette_color));
    if (!view->scene_available) nk_widget_disable_begin(context);
    std::array<char, 256> full_label{};
    snprintf(full_label.data(), full_label.size(), "%s #%llu%s%s%s", layer.name,
             static_cast<unsigned long long>(layer.layer_id),
             layer.visible ? "" : " [hidden]", layer.selected ? " *" : "",
             assign_selection ? "\nClick to add selected notes to this layer" : "");
    fit_semantic_layer_label(context, label.data(), label.size());
    const auto clicked = nk_button_label(context, label.data());
    if ((context->last_widget_state & NK_WIDGET_STATE_HOVER) != 0)
        nk_tooltip(context, full_label.data());
    if (!view->scene_available) nk_widget_disable_end(context);
    nk_style_pop_style_item(context);
    nk_style_pop_style_item(context);
    nk_style_pop_style_item(context);
    if (clicked && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
        action->compose_action = assign_selection ? WORM_UI_COMPOSE_ACTION_APPLY_SEMANTIC_LAYER
                                                  : WORM_UI_COMPOSE_ACTION_SELECT_SEMANTIC_LAYER;
        action->compose_semantic_layer_id = layer.layer_id;
    }
}

static void render_semantic_layer_palette(const ComposeInspectorContext& inspector) {
    const auto* view = inspector.view;
    auto* context = inspector.context;
    if (view->semantic_layer_count == 0) {
        nk_layout_row_dynamic(context, 22, 1);
        nk_label_wrap(context, "No visible semantic layers");
        return;
    }
    nk_layout_row_dynamic(context, 18, 1);
    nk_label(context, view->semantic_layers_truncated ? "Layers+" : "Layers", NK_TEXT_LEFT);
    if (view->selected_note_count != 0) {
        nk_layout_row_dynamic(context, 32, 1);
        nk_label_wrap(context, "Click an unlocked layer to add selected notes");
    }
    for (size_t index = 0; index < view->semantic_layer_count; ++index)
        render_semantic_layer_button(inspector, view->semantic_layers[index], index);
}

auto render_semantic_layer_palette_panel(const ComposeInspectorContext& inspector) -> bool {
    if (!nk_tree_push(inspector.context, NK_TREE_TAB, "Semantic layers", NK_MAXIMIZED)) return false;
    render_semantic_layer_page_controls(inspector);
    render_semantic_layer_palette(inspector);
    return true;
}

static void render_semantic_layer_color_editor(const ComposeInspectorContext& inspector) {
    auto* context = inspector.context;
    auto* action = inspector.action;
    if (!state.semantic_layer_color_editor_visible) return;
    nk_layout_row_dynamic(context, 110, 1);
    auto color = nk_colorf{state.semantic_layer_color.r, state.semantic_layer_color.g,
                           state.semantic_layer_color.b, state.semantic_layer_color.a};
    color = nk_color_picker(context, color, NK_RGBA);
    state.semantic_layer_color = {color.r, color.g, color.b, color.a};
    const auto byte_component = [](float component) {
        return static_cast<uint8_t>(std::lround(std::clamp(component, 0.0f, 1.0f) * 255.0f));
    };
    const auto red = byte_component(state.semantic_layer_color.r);
    const auto green = byte_component(state.semantic_layer_color.g);
    const auto blue = byte_component(state.semantic_layer_color.b);
    const auto alpha = byte_component(state.semantic_layer_color.a);
    nk_layout_row_dynamic(context, 24, 1);
    nk_labelf(context, NK_TEXT_LEFT, "RGBA %u %u %u %u", static_cast<unsigned>(red),
              static_cast<unsigned>(green), static_cast<unsigned>(blue),
              static_cast<unsigned>(alpha));
    nk_layout_row_dynamic(context, 24, 2);
    const auto apply_clicked = nk_button_label(context, "Apply");
    const auto cancel_clicked = nk_button_label(context, "Cancel");
    if (apply_clicked && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
        action->compose_semantic_layer_red = red;
        action->compose_semantic_layer_green = green;
        action->compose_semantic_layer_blue = blue;
        action->compose_semantic_layer_alpha = alpha;
        action->compose_action = WORM_UI_COMPOSE_ACTION_SET_SEMANTIC_LAYER_COLOR;
        state.semantic_layer_color_editor_visible = false;
    } else if (cancel_clicked) {
        state.semantic_layer_color_editor_visible = false;
    }
}

static void render_semantic_layer_selection_fields(const ComposeInspectorContext& inspector,
                                                   const auto& layer) {
    auto* context = inspector.context;
    auto* action = inspector.action;
    const auto* view = inspector.view;
    if (state.semantic_layer_name_id != layer.layer_id ||
        state.semantic_layer_name_revision != view->document_revision) {
        state.semantic_layer_name.fill('\0');
        const auto length = std::min(strlen(layer.name), state.semantic_layer_name.size() - 1);
        memcpy(state.semantic_layer_name.data(), layer.name, length);
        state.semantic_layer_name_id = layer.layer_id;
        state.semantic_layer_name_revision = view->document_revision;
    }
    if (state.semantic_layer_color_id != layer.layer_id) {
        state.semantic_layer_color_id = layer.layer_id;
        state.semantic_layer_color_editor_visible = false;
    }
    if (!state.semantic_layer_color_editor_visible) {
        const auto color = nk_color_cf(nk_rgba(layer.red, layer.green, layer.blue, layer.alpha));
        state.semantic_layer_color = {color.r, color.g, color.b, color.a};
    }
    nk_layout_row_dynamic(context, 24, 1);
    (void)wormedit::ui::chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD, state.semantic_layer_name.data(),
                                         state.semantic_layer_name.size(), nk_filter_default);
    nk_layout_row_dynamic(context, 24, 2);
    if (nk_button_label(context, "Rename") && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
        memcpy(action->compose_semantic_layer_name, state.semantic_layer_name.data(),
               sizeof(action->compose_semantic_layer_name));
        action->compose_action = WORM_UI_COMPOSE_ACTION_RENAME_SEMANTIC_LAYER;
    }
    if (nk_button_label(context, "Color..."))
        state.semantic_layer_color_editor_visible = !state.semantic_layer_color_editor_visible;
}

static void render_semantic_layer_membership_details(const ComposeInspectorContext& inspector,
                                                     const auto& layer);

static void render_semantic_layer_selected_actions(const ComposeInspectorContext& inspector,
                                                   const auto& layer) {
    auto* context = inspector.context;
    auto* action = inspector.action;
    const auto* view = inspector.view;
    nk_layout_row_dynamic(context, 24, 2);
    if (layer.locked) nk_widget_disable_begin(context);
    const auto delete_clicked = nk_button_label(context, "Delete");
    if (layer.locked) nk_widget_disable_end(context);
    if (delete_clicked && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
        action->compose_action = WORM_UI_COMPOSE_ACTION_DELETE_SEMANTIC_LAYER;
    if (nk_button_label(context, "Up") && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
        action->compose_action = WORM_UI_COMPOSE_ACTION_MOVE_SEMANTIC_LAYER_UP;
    if (nk_button_label(context, "Down") && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
        action->compose_action = WORM_UI_COMPOSE_ACTION_MOVE_SEMANTIC_LAYER_DOWN;
    if (nk_button_label(context, view->semantic_layer_target_visible ? "Hide" : "Show") &&
        action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
        action->compose_action = WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_VISIBILITY;
    render_semantic_layer_color_editor(inspector);
    render_semantic_layer_membership_details(inspector, layer);
}

static void render_semantic_layer_membership_details(const ComposeInspectorContext& inspector,
                                                     const auto& layer) {
    auto* context = inspector.context;
    auto* action = inspector.action;
    const auto* view = inspector.view;
    if (layer.assigned_note_count != 0) {
        nk_layout_row_dynamic(context, 24, 1);
        nk_labelf(context, NK_TEXT_LEFT, "Assigned to %zu note%s", layer.assigned_note_count,
                  layer.assigned_note_count == 1 ? "" : "s");
        if (layer.locked) nk_widget_disable_begin(context);
        const auto unlink_clicked = nk_button_label(context, "Delete + unlink");
        if (layer.locked) nk_widget_disable_end(context);
        if (unlink_clicked && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
            action->compose_action = WORM_UI_COMPOSE_ACTION_DELETE_SEMANTIC_LAYER_WITH_MEMBERSHIPS;
    }
    if (!layer.visible) return;
    nk_layout_row_begin(context, NK_DYNAMIC, 22, 2);
    nk_layout_row_push(context, wormedit::ui::chrome::property_label_ratio);
    if (nk_button_label(context, view->semantic_layer_target_required_filter ? "Require: On"
                                                                            : "Require") &&
        action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
        action->compose_action = WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_REQUIRE_FILTER;
    nk_layout_row_push(context, wormedit::ui::chrome::property_label_ratio);
    if (nk_button_label(context, view->semantic_layer_target_excluded_filter ? "Exclude: On"
                                                                            : "Exclude") &&
        action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
        action->compose_action = WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_EXCLUDE_FILTER;
    nk_layout_row_end(context);
}

void render_semantic_layer_selected_panel(const ComposeInspectorContext& inspector,
                                                std::size_t layer_count) {
    const auto* layers = inspector.view->semantic_layers;
    const auto selected = std::ranges::find_if(layers, layers + layer_count,
                                               [](const auto& layer) { return layer.selected; });
    if (selected == layers + layer_count) return;
    render_semantic_layer_selection_fields(inspector, *selected);
    render_semantic_layer_selected_actions(inspector, *selected);
}

static auto semantic_layer_membership_hint(const WormUiComposeView& view,
                                          bool target_active) -> const char* {
    if (!target_active) return "Choose a layer";
    if (view.selected_note_count == 0) return "Select notes first";
    if (view.semantic_layer_target_locked) return "Unlock the layer first";
    if (!view.scene_available) return "Open a Compose chart";
    return nullptr;
}

void render_semantic_layer_membership_buttons(const ComposeInspectorContext& inspector,
                                                     bool target_active) {
    auto* context = inspector.context;
    auto* action = inspector.action;
    const auto* view = inspector.view;
    nk_layout_row_dynamic(context, 18, 1);
    nk_labelf(context, NK_TEXT_LEFT, "Layer (%zu)", view->selected_note_count);
    const auto membership_enabled = view->scene_available && target_active &&
                                    !view->semantic_layer_target_locked &&
                                    view->selected_note_count != 0;
    const auto button = [&](const char* label, WormUiComposeActionKind kind) {
        if (!membership_enabled) nk_widget_disable_begin(context);
        const auto clicked = nk_button_label(context, label);
        if (!membership_enabled) nk_widget_disable_end(context);
        if (clicked && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
            action->compose_action = kind;
    };
    nk_layout_row_dynamic(context, 24, 1);
    if (!view->scene_available || !target_active) nk_widget_disable_begin(context);
    const auto select_clicked = nk_button_label(context, "Select notes in layer");
    if (!view->scene_available || !target_active) nk_widget_disable_end(context);
    if (select_clicked && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
        action->compose_action = WORM_UI_COMPOSE_ACTION_SELECT_SEMANTIC_LAYER_NOTES;
    button("Add selected notes to layer", WORM_UI_COMPOSE_ACTION_APPLY_SEMANTIC_LAYER);
    button("Remove selected notes", WORM_UI_COMPOSE_ACTION_REMOVE_SEMANTIC_LAYER);
    button("Toggle selected notes", WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER);
    if (const auto* hint = semantic_layer_membership_hint(*view, target_active)) {
        nk_layout_row_dynamic(context, 18, 1);
        nk_label(context, hint, NK_TEXT_LEFT);
    }
}

void render_semantic_layer_target_buttons(const ComposeInspectorContext& inspector,
                                                 bool target_active) {
    auto* context = inspector.context;
    auto* action = inspector.action;
    const auto* view = inspector.view;
    const auto layer_paint_enabled = view->scene_available && target_active &&
                                     !view->semantic_layer_target_locked;
    nk_layout_row_dynamic(context, 24, 2);
    if (!layer_paint_enabled) nk_widget_disable_begin(context);
    const auto paint_clicked = nk_button_label(
        context, view->semantic_layer_paint_mode_active ? "Paint: Armed" : "Paint: Off");
    if (!layer_paint_enabled) nk_widget_disable_end(context);
    if (paint_clicked && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
        action->compose_action = WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_PAINT_MODE;
    if (!target_active) nk_widget_disable_begin(context);
    const auto solo_clicked =
        nk_button_label(context, view->semantic_layer_solo_active ? "Solo: On" : "Solo: Off");
    if (!target_active) nk_widget_disable_end(context);
    if (solo_clicked && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
        action->compose_action = WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_SOLO;
    nk_layout_row_dynamic(context, 24, 2);
    if (!target_active) nk_widget_disable_begin(context);
    const auto lock_clicked =
        nk_button_label(context, view->semantic_layer_target_locked ? "Unlock" : "Lock");
    if (!target_active) nk_widget_disable_end(context);
    if (lock_clicked && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
        action->compose_action = WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_LOCK;
    if (!target_active) nk_widget_disable_begin(context);
    const auto clear_clicked = nk_button_label(context, "Clear target");
    if (!target_active) nk_widget_disable_end(context);
    if (clear_clicked && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
        action->compose_action = WORM_UI_COMPOSE_ACTION_CLEAR_SEMANTIC_LAYER;
}
