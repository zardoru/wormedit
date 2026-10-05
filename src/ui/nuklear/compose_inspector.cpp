#include "ui/nuklear/compose_inspector.h"
#include "ui/nuklear/meter_state.h"
#include "ui/nuklear/chrome.h"
#include "ui/nuklear/compose_panel_internal.h"
#include "ui/nuklear/compose_resources.h"
#include "ui/nuklear/compose_markers.h"
#include "ui/nuklear/timing_stage_editor.h"

#include <algorithm>
#include <array>
#include <charconv>
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
    nk_layout_row_dynamic(inspector.context, 28, 1);
    if (nk_button_label(inspector.context, "Timing Settings..."))
        inspector.action->timing_settings.toggle = true;
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
    render_compose_marker_section(inspector);
}

void render_compose_label_search(const ComposeInspectorContext& inspector) {
    const auto* view = inspector.view;
    auto* context = inspector.context;
    auto* action = inspector.action;
    nk_layout_row_dynamic(context, 18, 1);
    nk_label(context, "Find note label", NK_TEXT_LEFT);
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

namespace {

constexpr auto quantize_name_capacity = WORM_UI_COMPOSE_QUANTIZE_PRESET_NAME_BYTES + 1;
std::uint64_t quantize_name_project_id{};
std::array<char, quantize_name_capacity> quantize_preset_name{};
std::uint64_t quantize_grid_project_id{};
std::uint64_t quantize_grid_chart_id{};
std::uint64_t quantize_grid_projected_value{};
bool quantize_grid_editing{};
bool quantize_grid_invalid{};
std::array<char, 32> quantize_grid_text{};

void sync_quantize_grid_text(const WormUiComposeView& view) {
    const auto owner_changed = quantize_grid_project_id != view.project_id ||
                               quantize_grid_chart_id != view.chart_id;
    if (owner_changed || (!quantize_grid_editing &&
                          quantize_grid_projected_value != view.quantize_divisions)) {
        if (owner_changed) quantize_grid_editing = false;
        quantize_grid_project_id = view.project_id;
        quantize_grid_chart_id = view.chart_id;
        quantize_grid_projected_value = view.quantize_divisions;
        std::snprintf(quantize_grid_text.data(), quantize_grid_text.size(), "%llu",
                      static_cast<unsigned long long>(view.quantize_divisions));
        quantize_grid_invalid = false;
    }
}

auto parse_quantize_grid(const char* text) -> std::optional<std::uint64_t> {
    if (!text || text[0] == '\0') return std::nullopt;
    const auto* end = text + std::strlen(text);
    std::uint64_t value{};
    const auto parsed = std::from_chars(text, end, value);
    if (parsed.ec != std::errc{} || parsed.ptr != end || value == 0) return std::nullopt;
    return value;
}

void set_quantize_action(const ComposeInspectorContext& inspector,
                         WormUiComposeActionKind kind, std::uint64_t divisions,
                         std::uint8_t space, std::uint8_t tie, std::uint8_t holds,
                         bool preset_selected = false, std::size_t preset_index = 0) {
    auto* action = inspector.action;
    if (action->compose_action != WORM_UI_COMPOSE_ACTION_NONE) return;
    action->compose_action = kind;
    action->compose_input_chart_id = inspector.view->chart_id;
    action->compose_input_document_revision = inspector.view->document_revision;
    action->compose_quantize_divisions = divisions;
    action->compose_quantize_space = space;
    action->compose_quantize_tie = tie;
    action->compose_quantize_holds = holds;
    action->compose_quantize_preset_selected = preset_selected;
    action->compose_quantize_preset_index = preset_index;
}

auto quantize_preset_labels(const WormUiComposeView& view,
                            std::array<const char*, WORM_UI_COMPOSE_QUANTIZE_PRESET_CAPACITY + 1>& labels)
    -> int {
    labels[0] = "Current settings";
    const auto count = std::min(view.quantize_preset_count,
                                static_cast<std::size_t>(WORM_UI_COMPOSE_QUANTIZE_PRESET_CAPACITY));
    for (std::size_t index = 0; index < count; ++index)
        labels[index + 1] = view.quantize_presets && view.quantize_presets[index].name
                                ? view.quantize_presets[index].name : "(unnamed preset)";
    return static_cast<int>(count + 1);
}

void render_quantize_preset_picker(const ComposeInspectorContext& inspector) {
    const auto& view = *inspector.view;
    auto* context = inspector.context;
    auto* action = inspector.action;
    std::array<const char*, WORM_UI_COMPOSE_QUANTIZE_PRESET_CAPACITY + 1> labels{};
    const auto count = quantize_preset_labels(view, labels);
    auto selected = view.quantize_selected_preset_valid &&
                            view.quantize_selected_preset_index < view.quantize_preset_count
                        ? static_cast<int>(view.quantize_selected_preset_index + 1) : 0;
    nk_layout_row_dynamic(context, 24, 1);
    nk_label(context, "Preset", NK_TEXT_LEFT);
    nk_layout_row_dynamic(context, 24, 1);
    selected = wormedit::ui::chrome::safe_combo(context, labels.data(), count, selected, 22,
                                                nk_vec2(220, 300));
    if (action->compose_action == WORM_UI_COMPOSE_ACTION_NONE &&
        selected != (view.quantize_selected_preset_valid
                         ? static_cast<int>(view.quantize_selected_preset_index + 1) : 0)) {
        quantize_grid_editing = false;
        quantize_grid_invalid = false;
        if (selected > 0) {
            set_quantize_action(inspector, WORM_UI_COMPOSE_ACTION_SELECT_QUANTIZE_PRESET,
                                view.quantize_divisions, view.quantize_space, view.quantize_tie,
                                view.quantize_holds, true,
                                static_cast<std::size_t>(selected - 1));
        } else {
            set_quantize_action(inspector, WORM_UI_COMPOSE_ACTION_SET_QUANTIZE_SETTINGS,
                                view.quantize_divisions, view.quantize_space, view.quantize_tie,
                                view.quantize_holds);
        }
    }
}

void render_quantize_setting_fields(const ComposeInspectorContext& inspector) {
    const auto& view = *inspector.view;
    auto* context = inspector.context;
    auto* action = inspector.action;
    sync_quantize_grid_text(view);
    nk_layout_row_dynamic(context, 24, 2);
    nk_label(context, "Grid divisions", NK_TEXT_LEFT);
    const auto edit_result = wormedit::ui::chrome::worm_ui_nuklear_text_field(
        context, static_cast<nk_flags>(NK_EDIT_FIELD) | NK_EDIT_SIG_ENTER, quantize_grid_text.data(),
        static_cast<int>(quantize_grid_text.size()), nk_filter_decimal);
    quantize_grid_editing = (edit_result & NK_EDIT_ACTIVE) != 0;
    const auto grid_committed = (edit_result & NK_EDIT_COMMITED) != 0;
    const auto divisions = grid_committed ? parse_quantize_grid(quantize_grid_text.data())
                                          : std::optional<std::uint64_t>{};
    if (grid_committed) {
        if (divisions) {
            quantize_grid_invalid = false;
            set_quantize_action(inspector, WORM_UI_COMPOSE_ACTION_SET_QUANTIZE_SETTINGS,
                                *divisions, view.quantize_space, view.quantize_tie,
                                view.quantize_holds);
        } else {
            quantize_grid_invalid = true;
        }
        quantize_grid_editing = false;
    }
    if (quantize_grid_invalid) {
        nk_layout_row_dynamic(context, 18, 1);
        nk_label(context, "Grid must be a positive whole number", NK_TEXT_LEFT);
    }

    constexpr const char* spaces[] = {"Standard measure", "Authored measure"};
    constexpr const char* ties[] = {"Earlier tie", "Later tie", "Reject ties"};
    constexpr const char* holds[] = {"Start only", "Both hold endpoints"};
    auto space = static_cast<int>(std::min<std::uint8_t>(view.quantize_space, 1));
    auto tie = static_cast<int>(std::min<std::uint8_t>(view.quantize_tie, 2));
    auto hold_scope = static_cast<int>(std::min<std::uint8_t>(view.quantize_holds, 1));
    nk_layout_row_dynamic(context, 24, 1);
    nk_label(context, "Grid space", NK_TEXT_LEFT);
    const auto next_space = wormedit::ui::chrome::safe_combo(context, spaces, 2, space, 22,
                                                              nk_vec2(220, 100));
    nk_layout_row_dynamic(context, 24, 1);
    nk_label(context, "Tie policy", NK_TEXT_LEFT);
    const auto next_tie = wormedit::ui::chrome::safe_combo(context, ties, 3, tie, 22,
                                                           nk_vec2(220, 120));
    nk_layout_row_dynamic(context, 24, 1);
    nk_label(context, "Hold scope", NK_TEXT_LEFT);
    const auto next_hold = wormedit::ui::chrome::safe_combo(context, holds, 2, hold_scope, 22,
                                                             nk_vec2(220, 100));
    if (action->compose_action != WORM_UI_COMPOSE_ACTION_NONE ||
        (next_space == space && next_tie == tie && next_hold == hold_scope)) return;
    set_quantize_action(inspector, WORM_UI_COMPOSE_ACTION_SET_QUANTIZE_SETTINGS,
                        view.quantize_divisions, static_cast<std::uint8_t>(next_space),
                        static_cast<std::uint8_t>(next_tie), static_cast<std::uint8_t>(next_hold));
}

auto quantize_preset_name_is_unique(const WormUiComposeView& view, const char* name) -> bool {
    if (!name || name[0] == '\0' || view.quantize_preset_count >=
                                         WORM_UI_COMPOSE_QUANTIZE_PRESET_CAPACITY) return false;
    for (std::size_t index = 0; index < view.quantize_preset_count; ++index)
        if (view.quantize_presets && view.quantize_presets[index].name &&
            std::strcmp(view.quantize_presets[index].name, name) == 0) return false;
    return true;
}

void render_quantize_preset_actions(const ComposeInspectorContext& inspector) {
    const auto& view = *inspector.view;
    auto* context = inspector.context;
    auto* action = inspector.action;
    if (quantize_name_project_id != view.project_id) {
        quantize_name_project_id = view.project_id;
        quantize_preset_name.fill('\0');
    }
    nk_layout_row_dynamic(context, 24, 1);
    (void)wormedit::ui::chrome::worm_ui_nuklear_text_field(
        context, NK_EDIT_FIELD, quantize_preset_name.data(), quantize_preset_name.size(),
        nk_filter_default);
    nk_layout_row_dynamic(context, 24, 2);
    const auto can_save = view.scene_available && !quantize_grid_editing &&
                          !quantize_grid_invalid && quantize_preset_name_is_unique(
                                                        view, quantize_preset_name.data());
    if (!can_save) nk_widget_disable_begin(context);
    const auto save = nk_button_label(context, "Save preset");
    if (!can_save) nk_widget_disable_end(context);
    if (save && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
        set_quantize_action(inspector, WORM_UI_COMPOSE_ACTION_SAVE_QUANTIZE_PRESET,
                            view.quantize_divisions, view.quantize_space, view.quantize_tie,
                            view.quantize_holds);
        std::memcpy(action->compose_quantize_preset_name, quantize_preset_name.data(),
                    quantize_preset_name.size());
    }
    const auto can_delete = view.quantize_selected_preset_valid &&
                            view.quantize_selected_preset_index < view.quantize_preset_count;
    if (!can_delete) nk_widget_disable_begin(context);
    const auto remove = nk_button_label(context, "Delete preset");
    if (!can_delete) nk_widget_disable_end(context);
    if (remove && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
        set_quantize_action(inspector, WORM_UI_COMPOSE_ACTION_DELETE_QUANTIZE_PRESET,
                            view.quantize_divisions, view.quantize_space, view.quantize_tie,
                            view.quantize_holds, true, view.quantize_selected_preset_index);
}

void quantize_report_label(nk_context* context, const char* label) {
    auto height = 40.0f;
    const auto* font = context->style.font;
    if (font && font->width) {
        const auto width = std::max(64.0f, nk_window_get_content_region(context).w - 32.0f);
        const auto text_width = font->width(font->userdata, font->height, label,
                                            static_cast<int>(std::strlen(label)));
        const auto lines = std::ceil(text_width / width) + 1.0f;
        height = std::max(32.0f, lines * font->height + 4.0f);
    }
    nk_layout_row_dynamic(context, height, 1);
    nk_label_wrap(context, label);
}

void render_quantize_report_row(nk_context* context,
                                const WormUiComposeQuantizeReportRowView& row) {
    std::array<char, 192> label{};
    if (row.has_collision_peer) {
        std::snprintf(label.data(), label.size(), "Note %llu · lane %llu · %s, collision with %llu",
                      static_cast<unsigned long long>(row.object_id),
                      static_cast<unsigned long long>(row.lane_id),
                      row.moved ? "moved" : "unchanged",
                      static_cast<unsigned long long>(row.collision_peer_id));
    } else {
        std::snprintf(label.data(), label.size(), "Note %llu · lane %llu · %s%s",
                      static_cast<unsigned long long>(row.object_id),
                      static_cast<unsigned long long>(row.lane_id),
                      row.moved ? "moved" : "unchanged",
                      row.collision ? " · collision" : "");
    }
    quantize_report_label(context, label.data());
    std::snprintf(label.data(), label.size(),
                  "Start m%llu:r%llu -> m%llu:r%llu",
                  static_cast<unsigned long long>(row.start_before.measure_id),
                  static_cast<unsigned long long>(row.start_before.row),
                  static_cast<unsigned long long>(row.start_after.measure_id),
                  static_cast<unsigned long long>(row.start_after.row));
    quantize_report_label(context, label.data());
    std::snprintf(label.data(), label.size(), "Absolute row %llu -> %llu",
                  static_cast<unsigned long long>(row.start_before_absolute_row),
                  static_cast<unsigned long long>(row.start_after_absolute_row));
    quantize_report_label(context, label.data());
    if (row.has_end_before && row.has_end_after) {
        std::snprintf(label.data(), label.size(), "Hold end m%llu:r%llu -> m%llu:r%llu",
                      static_cast<unsigned long long>(row.end_before.measure_id),
                      static_cast<unsigned long long>(row.end_before.row),
                      static_cast<unsigned long long>(row.end_after.measure_id),
                      static_cast<unsigned long long>(row.end_after.row));
        quantize_report_label(context, label.data());
    }
}

// Nuklear keys a tree's open state by its call site, so the two report trees must pass distinct
// ids; sharing one made the applied report follow the collapsed preview tree and look empty.
void render_quantize_report_rows(nk_context* context, const char* title, int tree_id,
                                 const WormUiComposeQuantizeReportRowView* rows,
                                 std::size_t count, std::size_t total, bool truncated,
                                 nk_collapse_states initial = NK_MINIMIZED) {
    if (!nk_tree_push_id(context, NK_TREE_TAB, title, initial, tree_id)) return;
    if (truncated) {
        nk_layout_row_dynamic(context, 18, 1);
        nk_labelf(context, NK_TEXT_LEFT, "%zu rows shown of %zu", count, total);
    }
    const auto visible_count = std::min(count,
        static_cast<std::size_t>(WORM_UI_COMPOSE_QUANTIZE_REPORT_CAPACITY));
    for (std::size_t index = 0; rows && index < visible_count; ++index)
        render_quantize_report_row(context, rows[index]);
    nk_tree_pop(context);
}

} // namespace

void render_compose_quantize_section(const ComposeInspectorContext& inspector) {
    const auto* view = inspector.view;
    auto* context = inspector.context;
    auto* action = inspector.action;
    nk_layout_row_dynamic(context, 18, 1);
    nk_labelf(context, NK_TEXT_LEFT, "Quantize (%zu)", view->selected_note_count);
    if (view->selected_note_count == 0) {
        nk_layout_row_dynamic(context, 18, 1);
        nk_label(context, "Select notes first", NK_TEXT_LEFT);
    } else if (view->quantize_preview_blocked != 0) {
        nk_layout_row_dynamic(context, 18, 1);
        nk_labelf(context, NK_TEXT_LEFT, "Blocked: %zu issue(s), %zu colliding objects",
                  view->quantize_preview_blocked, view->quantize_preview_colliding_objects);
        nk_layout_row_dynamic(context, 32, 1);
        nk_label_wrap(context, view->quantize_preview_message);
    } else {
        nk_layout_row_dynamic(context, 18, 1);
        nk_labelf(context, NK_TEXT_LEFT, "%zu would move, %zu unchanged, %zu colliding",
                  view->quantize_preview_moved, view->quantize_preview_unchanged,
                  view->quantize_preview_colliding_objects);
    }
    const auto enabled = view->scene_available && view->selected_note_count != 0 &&
                         view->quantize_preview_blocked == 0 && view->quantize_preview_moved != 0 &&
                         !quantize_grid_editing && !quantize_grid_invalid;
    if (!enabled) nk_widget_disable_begin(context);
    nk_layout_row_dynamic(context, 25, 1);
    const auto clicked = nk_button_label(context, "Apply quantization");
    if (!enabled) nk_widget_disable_end(context);
    if (clicked && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
        set_quantize_action(inspector, WORM_UI_COMPOSE_ACTION_AREA_QUANTIZE,
                            view->quantize_divisions, view->quantize_space, view->quantize_tie,
                            view->quantize_holds, view->quantize_selected_preset_valid,
                            view->quantize_selected_preset_index);
    if (nk_tree_push(context, NK_TREE_TAB, "Presets and settings", NK_MINIMIZED)) {
        render_quantize_preset_picker(inspector);
        render_quantize_setting_fields(inspector);
        render_quantize_preset_actions(inspector);
        nk_tree_pop(context);
    }
    render_quantize_report_rows(context, "Preview rows", 0, view->quantize_preview_rows,
                                view->quantize_preview_row_count,
                                view->quantize_preview_total_rows,
                                view->quantize_preview_rows_truncated);
    // Only after an apply that moved rows; an empty heading says nothing, and it opens expanded so
    // the moved rows are visible without another click.
    if (view->quantize_applied_row_count != 0)
        render_quantize_report_rows(context, "Last applied report", 1, view->quantize_applied_rows,
                                    view->quantize_applied_row_count,
                                    view->quantize_applied_total_rows,
                                    view->quantize_applied_rows_truncated, NK_MAXIMIZED);
}
