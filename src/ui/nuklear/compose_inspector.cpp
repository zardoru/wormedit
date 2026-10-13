#include "ui/field/rect.h"
#include "ui/nuklear/compose_menu_command.h"
#include "ui/nuklear/compose_inspector.h"
#include "ui/nuklear/compose_toolbar.h"
#include "ui/nuklear/meter_state.h"
#include "ui/nuklear/chrome.h"
#include "ui/nuklear/ui_metrics.h"
#include "ui/nuklear/context_menu.h"
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
#include <string>
#include <vector>

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
    if (!nk_tree_push(inspector.context, NK_TREE_TAB, "Bookmarks and sections", NK_MINIMIZED)) return;
    worm_ui_nuklear_render_compose_markers(inspector.context, inspector.view, inspector.action);
    nk_tree_pop(inspector.context);
}

// Lets the layout at the cursor switch between profiles with the same lane count.
static void render_lane_profile_section(const ComposeInspectorContext& inspector) {
    const auto* view = inspector.view;
    auto* context = inspector.context;
    auto* action = inspector.action;
    if (view->lane_profile_choice_count == 0) return;
    if (!nk_tree_push(context, NK_TREE_TAB, "Lane profile", NK_MAXIMIZED)) return;
    std::array<const char*, WORM_UI_COMPOSE_LANE_PROFILE_CAPACITY> names{};
    const auto selecting_initial = view->lane_profile_current < 0;
    const auto offset = selecting_initial ? std::size_t{1} : std::size_t{0};
    const auto count = std::min<std::size_t>(view->lane_profile_choice_count,
                                             names.size() - offset);
    if (selecting_initial) names[0] = "Choose a lane profile...";
    for (std::size_t index = 0; index < view->lane_profile_choice_count; ++index)
        if (index < count) names[index + offset] = view->lane_profile_names[index];
    const int current = selecting_initial ? 0 : view->lane_profile_current;
    nk_layout_row_dynamic(context, 26, 1);
    const int selected = wormedit::ui::chrome::safe_combo(
        context, names.data(), static_cast<int>(count + offset), current, 24,
        nk_vec2(220.0f, 120.0f));
    if (selected != current && (!selecting_initial || selected > 0) &&
        action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
        action->compose_action = WORM_UI_COMPOSE_ACTION_SET_LANE_PROFILE;
        action->compose_lane_profile_index = static_cast<uint32_t>(
            selected - (selecting_initial ? 1 : 0));
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
    if (nk_tree_push(inspector.context, NK_TREE_TAB, "Timing", NK_MAXIMIZED)) {
        const auto& timing = *inspector.timing_view;
        nk_layout_row_dynamic(inspector.context, 18, 1);
        nk_labelf(inspector.context, NK_TEXT_LEFT, "%zu measures · %zu BPM · %zu stop/delay",
                  timing.measure_count, timing.bpm_count, timing.pause_count);
        nk_layout_row_dynamic(inspector.context, 18, 1);
        nk_labelf(inspector.context, NK_TEXT_LEFT, "%zu warp · %zu SPEEDS · %zu anchors",
                  timing.warp_count, timing.speed_total_count, timing.anchor_count);
        render_initial_bpm_control(inspector);
        render_compose_inspector_timing_controls({inspector.context, inspector.view, inspector.action});
        nk_layout_row_dynamic(inspector.context, 28, 1);
        if (nk_button_label(inspector.context, "Timing Settings..."))
            inspector.action->timing_settings.toggle = true;
        nk_tree_pop(inspector.context);
    } else if (state.inline_offset.inline_offset_open && state.inline_offset.inline_offset_in_inspector) {
        worm_ui_nuklear_cancel_inline_offset(state);
        nk_edit_unfocus(inspector.context);
    }
    if (!state.inline_timing.inline_timing_open && inspector.timing_view->stage_active &&
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
    (void)wormedit::ui::chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD, state.note_label.label_search.data(),
                                         state.note_label.label_search.size(), nk_filter_default);
    const auto disabled = !view->scene_available || state.note_label.label_search[0] == '\0';
    if (disabled) nk_widget_disable_begin(context);
    nk_layout_row_dynamic(context, 25, 3);
    const auto submit = [&](const char* title, WormUiComposeActionKind kind) {
        if (!nk_button_label(context, title) ||
            action->compose_action != WORM_UI_COMPOSE_ACTION_NONE)
            return;
        memcpy(action->compose_note_label, state.note_label.label_search.data(),
               state.note_label.label_search.size());
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
    (void)wormedit::ui::chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD, state.recipe_editor.meter_numerator.data(),
                                         state.recipe_editor.meter_numerator.size(), nk_filter_decimal);
    nk_layout_row_push(context, 0.06f);
    nk_label(context, "/", NK_TEXT_CENTERED);
    nk_layout_row_push(context, 0.2f);
    (void)wormedit::ui::chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD, state.recipe_editor.meter_denominator.data(),
                                         state.recipe_editor.meter_denominator.size(), nk_filter_decimal);
    nk_layout_row_push(context, 0.34f);
    const auto apply = nk_button_label(context, "Apply");
    nk_layout_row_end(context);
    const auto suggestions = worm_ui_nuklear_suggest_meters(state.recipe_editor.meter_numerator.data(),
                                                            state.recipe_editor.meter_denominator.data());
    if (suggestions.count != 0) {
        nk_layout_row_dynamic(context, 24, static_cast<int>(suggestions.count) + 1);
        nk_label(context, "Suggest", NK_TEXT_LEFT);
        for (std::size_t index = 0; index < suggestions.count; ++index) {
            const auto& item = suggestions.items[index];
            std::array<char, 16> label{};
            std::snprintf(label.data(), label.size(), "%u/%u", item.numerator, item.denominator);
            if (!nk_button_label(context, label.data())) continue;
            std::snprintf(state.recipe_editor.meter_numerator.data(), state.recipe_editor.meter_numerator.size(), "%u", item.numerator);
            std::snprintf(state.recipe_editor.meter_denominator.data(), state.recipe_editor.meter_denominator.size(), "%u",
                          item.denominator);
        }
    }
    if (!apply || action->compose_action != WORM_UI_COMPOSE_ACTION_NONE) return;
    action->compose_action = WORM_UI_COMPOSE_ACTION_SET_MEASURE_METER;
    action->compose_input_chart_id = view->chart_id;
    action->compose_input_document_revision = view->document_revision;
    std::snprintf(action->timing_meter_numerator_text, sizeof(action->timing_meter_numerator_text), "%s",
                  state.recipe_editor.meter_numerator.data());
    std::snprintf(action->timing_meter_denominator_text, sizeof(action->timing_meter_denominator_text), "%s",
                  state.recipe_editor.meter_denominator.data());
}

// The confirmation belongs to one measure at one document revision; a move or edit closes it.
struct measure_removal_confirmation_t {
    bool open{};
    std::uint64_t measure{};
    std::uint64_t revision{};
};
measure_removal_confirmation_t measure_removal_confirmation;

void render_measure_removal_summary(const ComposeInspectorContext& inspector) {
    const auto* view = inspector.view;
    auto* context = inspector.context;
    // Wrapped, not clipped: the inspector is narrow and the counts must stay readable.
    char text[160];
    nk_layout_row_dynamic(context, 38, 1);
    std::snprintf(text, sizeof(text), "Deletes %zu notes, %zu BGM and %zu BGA events.",
                  view->measure_removal_deleted_notes, view->measure_removal_deleted_bgm_events,
                  view->measure_removal_deleted_bga_events);
    nk_label_wrap(context, text);
    nk_layout_row_dynamic(context, 56, 1);
    std::snprintf(text, sizeof(text), "Shortens %zu holds and moves %zu timing events to the next measure.",
                  view->measure_removal_shortened_holds, view->measure_removal_moved_positions);
    nk_label_wrap(context, text);
    if (view->measure_removal_blocked) {
        nk_layout_row_dynamic(context, 56, 1);
        nk_label_wrap(context, view->measure_removal_message);
    }
}

void render_measure_removal_confirmation(const ComposeInspectorContext& inspector) {
    const auto* view = inspector.view;
    auto* context = inspector.context;
    auto* action = inspector.action;
    auto& confirmation = measure_removal_confirmation;
    if (!confirmation.open) return;
    const auto stale = !view->measure_removal_available ||
                       view->measure_removal_measure_id != confirmation.measure ||
                       view->document_revision != confirmation.revision;
    if (stale) {
        confirmation.open = false;
        return;
    }
    nk_layout_row_dynamic(context, 38, 1);
    nk_label_wrap(context, "Remove this measure and its contents?");
    render_measure_removal_summary(inspector);
    nk_layout_row_dynamic(context, 24, 2);
    const auto blocked = view->measure_removal_blocked;
    if (blocked) nk_widget_disable_begin(context);
    const auto confirmed = nk_button_label(context, "Confirm removal");
    if (blocked) nk_widget_disable_end(context);
    if (nk_button_label(context, "Cancel")) confirmation.open = false;
    if (!confirmed || blocked || action->compose_action != WORM_UI_COMPOSE_ACTION_NONE) return;
    confirmation.open = false;
    action->compose_action = WORM_UI_COMPOSE_ACTION_REMOVE_MEASURE_WITH_CONTENTS;
    action->compose_input_chart_id = view->chart_id;
    action->compose_input_document_revision = view->document_revision;
}

// Empty measures are removed at once; a nonempty one first opens the confirmation.
void render_measure_removal_control(const ComposeInspectorContext& inspector) {
    const auto* view = inspector.view;
    auto* context = inspector.context;
    auto* action = inspector.action;
    nk_layout_row_dynamic(context, 24, 1);
    if (!nk_button_label(context, "Remove measure") || action->compose_action != WORM_UI_COMPOSE_ACTION_NONE)
        return;
    if (view->measure_removal_has_contents) {
        measure_removal_confirmation = {.open = true,
                                        .measure = view->measure_removal_measure_id,
                                        .revision = view->document_revision};
        return;
    }
    emit_compose_menu_command(*action, *view, "measures.remove_empty");
    action->compose_input_chart_id = view->chart_id;
    action->compose_input_document_revision = view->document_revision;
}

void render_compose_measure_section(const ComposeInspectorContext& inspector) {
    const auto* view = inspector.view;
    auto* context = inspector.context;
    auto* action = inspector.action;
    if (!nk_tree_push(context, NK_TREE_TAB, "Measure at cursor", NK_MINIMIZED)) return;
    if (!view->scene_available) nk_widget_disable_begin(context);
    const auto entry = [&](const char* title, const char* command) {
        nk_layout_row_dynamic(context, 24, 1);
        if (!nk_button_label(context, title) ||
            action->compose_action != WORM_UI_COMPOSE_ACTION_NONE)
            return;
        emit_compose_menu_command(*action, *view, command);
    };
    entry("Insert measure before", "measures.insert_before");
    entry("Insert measure after", "measures.insert_after");
    render_measure_removal_control(inspector);
    render_measure_removal_confirmation(inspector);
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
bool quantize_section_visible_this_frame{};

auto quantize_bounds_intersect(struct nk_rect first, struct nk_rect last, struct nk_rect clip) -> bool {
    const auto left = std::min(first.x, last.x);
    const auto top = first.y;
    const auto right = std::max(first.x + first.w, last.x + last.w);
    const auto bottom = last.y + last.h;
    return wormedit::ui::has_rect_overlap({left, top, right - left, bottom - top},
                                       {clip.x, clip.y, clip.w, clip.h});
}


constexpr auto quantize_name_capacity = WORM_UI_COMPOSE_QUANTIZE_PRESET_NAME_BYTES + 1;
std::array<char, quantize_name_capacity> quantize_preset_name{};
std::uint64_t quantize_grid_project_id{};
std::uint64_t quantize_grid_chart_id{};
std::uint64_t quantize_grid_projected_value{};
bool quantize_grid_editing{};
bool quantize_grid_invalid{};
std::array<char, 32> quantize_grid_text{};
enum class QuantizePopup { custom, name, preset };
wormedit::ui::chrome::ContextMenu quantize_menu{.title = "Quantize settings"};
QuantizePopup quantize_popup{};
std::size_t quantize_menu_preset{};
bool quantize_rename{};
struct QuantizeMenuPreset {
    std::string name;
    std::uint64_t divisions{};
    std::uint8_t space{}, tie{}, holds{};
};
std::vector<QuantizeMenuPreset> quantize_menu_presets;

void sync_quantize_grid_text(const WormUiComposeView& view) {
    const auto owner_changed =
        quantize_grid_project_id != view.project_id || quantize_grid_chart_id != view.chart_id;
    if (owner_changed ||
        (!quantize_grid_editing && quantize_grid_projected_value != view.quantize_divisions)) {
        if (owner_changed)
            quantize_grid_editing = false;
        quantize_grid_project_id = view.project_id;
        quantize_grid_chart_id = view.chart_id;
        quantize_grid_projected_value = view.quantize_divisions;
        std::snprintf(quantize_grid_text.data(), quantize_grid_text.size(), "%llu",
                      static_cast<unsigned long long>(view.quantize_divisions));
        quantize_grid_invalid = false;
    }
}

auto parse_quantize_grid(const char* text) -> std::optional<std::uint64_t> {
    if (!text || text[0] == '\0')
        return std::nullopt;
    const auto* end = text + std::strlen(text);
    std::uint64_t value{};
    const auto parsed = std::from_chars(text, end, value);
    if (parsed.ec != std::errc{} || parsed.ptr != end || value == 0)
        return std::nullopt;
    return value;
}

void set_quantize_action(const ComposeInspectorContext& inspector, WormUiComposeActionKind kind,
                         std::uint64_t divisions, std::uint8_t space, std::uint8_t tie,
                         std::uint8_t holds, bool preset_selected = false,
                         std::size_t preset_index = 0) {
    auto* action = inspector.action;
    if (action->compose_action != WORM_UI_COMPOSE_ACTION_NONE)
        return;
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

void open_quantize_popup(const ComposeInspectorContext& i, QuantizePopup kind) {
    quantize_popup = kind;
    quantize_menu_presets.clear();
    const auto count = std::min(i.view->quantize_preset_count,
                                static_cast<std::size_t>(WORM_UI_COMPOSE_QUANTIZE_PRESET_CAPACITY));
    for (std::size_t index = 0; index < count && i.view->quantize_presets; ++index) {
        const auto& preset = i.view->quantize_presets[index];
        quantize_menu_presets.push_back({preset.name ? preset.name : "", preset.divisions,
                                         preset.space, preset.tie, preset.holds});
    }
    wormedit::ui::chrome::context_menu_open(quantize_menu, i.context->input.mouse.pos,
                                            wormedit::ui::chrome::PopupContextStamp::from(*i.view, quantize_menu_preset));
    quantize_menu.shared_keyboard_navigation = kind == QuantizePopup::preset;
}
auto quantize_segments(nk_context* c, const char* const* labels, int count, int selected,
                       const char* const* tips, bool dim = false) -> int {
    nk_layout_row_dynamic(c, 25, 1);
    const auto bounds = nk_widget_bounds(c);
    const auto hover = wormedit::ui::chrome::segment_at(bounds, count, c->input.mouse.pos.x);
    const auto result = wormedit::ui::chrome::Segmented{labels, count, selected, nullptr,
                                                        hover >= 0 && !compose_quantize_owns_input() ? tips[hover] : nullptr}
                            .render(c);
    if (dim) {
        auto tint = wormedit::ui::chrome::color(wormedit::ui::theme::UiColor::surface);
        tint.a = 80;
        nk_fill_rect(nk_window_get_canvas(c), bounds, 4, tint);
    }
    return result;
}
auto snap_supported(const WormUiComposeView& v, std::uint64_t denominator) -> bool {
    return std::find(v.supported_snap_denominators,
                     v.supported_snap_denominators + v.supported_snap_denominator_count,
                     denominator) != v.supported_snap_denominators + v.supported_snap_denominator_count;
}

// Chips outside the chart's snap list set only the quantize target; say so on hover.
void quantize_grid_tooltip(nk_context* c, const WormUiComposeView& v, struct nk_rect grid,
                           const std::uint64_t* values, const char* const* labels, int count,
                           int columns) {
    if (compose_quantize_owns_input()) return;
    const auto cell_w = grid.w / static_cast<float>(columns);
    const auto row_h = wormedit::ui::metrics::control_compact + c->style.window.spacing.y;
    const auto& mouse = c->input.mouse.pos;
    if (mouse.x < grid.x || mouse.y < grid.y || mouse.x >= grid.x + grid.w) return;
    const auto slot = static_cast<int>((mouse.y - grid.y) / row_h) * columns +
                      static_cast<int>((mouse.x - grid.x) / cell_w);
    if (slot < 0 || slot >= count || snap_supported(v, values[slot])) return;
    char text[64];
    std::snprintf(text, sizeof(text), "Quantize only: this chart cannot snap to %s.", labels[slot]);
    wormedit::ui::nuklear::tooltip::text(c, text);
}

void render_quantize_grid(const ComposeInspectorContext& i) {
    const auto& v = *i.view;
    auto* c = i.context;
    constexpr const char* grids[]{"1/4", "1/8",  "1/16", "1/32", "1/64",
                                  "1/6", "1/12", "1/24", "1/48", "1/96",
                                  "1/5", "1/10", "1/20", "1/40", "1/80"};
    constexpr std::uint64_t values[]{4, 8, 16, 32, 64, 6, 12, 24, 48, 96, 5, 10, 20, 40, 80};
    auto selected = -1;
    for (int x = 0; x < 15; ++x)
        if (values[x] == v.quantize_divisions)
            selected = x;
    nk_layout_row_dynamic(c, 18, 1);
    auto grid_bounds = nk_widget_bounds(c);
    nk_label(c, "Grid", NK_TEXT_LEFT);
    // The chip rows start one row spacing below the label.
    grid_bounds.y += grid_bounds.h + c->style.window.spacing.y;
    const auto grid = wormedit::ui::chrome::ChipGrid{grids, 15, selected, 5}.render(c);
    quantize_grid_tooltip(c, v, grid_bounds, values, grids, 15, 5);
    if (grid.chosen >= 0) {
        quantize_grid_invalid = quantize_grid_editing = false;
        set_quantize_action(i, WORM_UI_COMPOSE_ACTION_SET_QUANTIZE_SETTINGS, values[grid.chosen],
                            v.quantize_space, v.quantize_tie, v.quantize_holds);
    }
    if (wormedit::ui::chrome::ChipGrid{nullptr, 0, -1, 1, "custom…"}.render(c).add) {
        quantize_grid_projected_value = 0;
        sync_quantize_grid_text(v);
        open_quantize_popup(i, QuantizePopup::custom);
    }
}

void quantize_setting_spacing(nk_context* context) {
    nk_layout_row_dynamic(context, 9.0f, 1);
    nk_spacing(context, 1);
}

void render_quantize_setting_fields(const ComposeInspectorContext& i) {
    const auto& v = *i.view;
    auto* c = i.context;
    render_quantize_grid(i);
    constexpr const char* spaces[]{"Standard", "Authored"};
    constexpr const char* st[]{"Snap on the standard-measure grid across the chart.",
                               "Divide each authored measure into this many equal parts."};
    constexpr const char* ties[]{"Earlier", "Later", "Block"};
    constexpr const char* tt[]{
        "Round halfway objects to the earlier grid line.",
        "Round halfway objects to the later grid line.",
        "Don't quantize if any selected object is exactly halfway; those objects are listed."};
    constexpr const char* holds[]{"Leave in place", "Snap too"};
    constexpr const char* ht[]{
        "Snap the head and leave the tail in place, changing the hold length.",
        "Snap both the hold head and its tail to the grid."};
    quantize_setting_spacing(c);
    nk_layout_row_dynamic(c, 18, 1);
    nk_label(c, "Space", NK_TEXT_LEFT);
    const auto space = quantize_segments(c, spaces, 2, v.quantize_space, st);
    quantize_setting_spacing(c);
    nk_layout_row_dynamic(c, 18, 1);
    nk_labelf(c, NK_TEXT_LEFT, "Halfway objects · %zu in selection",
              v.quantize_preview_halfway_notes);
    const auto tie =
        quantize_segments(c, ties, 3, v.quantize_tie, tt, v.quantize_preview_halfway_notes == 0);
    quantize_setting_spacing(c);
    nk_layout_row_dynamic(c, 18, 1);
    nk_labelf(c, NK_TEXT_LEFT, "Hold tails · %zu %s in selection", v.quantize_preview_holds,
              v.quantize_preview_holds == 1 ? "hold" : "holds");
    const auto hold = quantize_segments(c, holds, 2, v.quantize_holds, ht);
    if (space != v.quantize_space || tie != v.quantize_tie || hold != v.quantize_holds)
        set_quantize_action(i, WORM_UI_COMPOSE_ACTION_SET_QUANTIZE_SETTINGS, v.quantize_divisions,
                            space, tie, hold);
}
auto quantize_preset_name_is_unique(const WormUiComposeView& v, const char* name) -> bool {
    if (!name || !name[0])
        return false;
    for (std::size_t x = 0; x < v.quantize_preset_count; ++x) {
        if (quantize_rename && x == quantize_menu_preset)
            continue;
        if (v.quantize_presets && v.quantize_presets[x].name &&
            std::strcmp(v.quantize_presets[x].name, name) == 0)
            return false;
    }
    return quantize_rename || v.quantize_preset_count < WORM_UI_COMPOSE_QUANTIZE_PRESET_CAPACITY;
}
void render_quantize_preset_picker(const ComposeInspectorContext& i) {
    const auto& v = *i.view;
    auto* c = i.context;
    const auto count = std::min(v.quantize_preset_count,
                                static_cast<std::size_t>(WORM_UI_COMPOSE_QUANTIZE_PRESET_CAPACITY));
    std::array<const char*, WORM_UI_COMPOSE_QUANTIZE_PRESET_CAPACITY> labels{};
    for (std::size_t x = 0; x < count; ++x)
        labels[x] = v.quantize_presets && v.quantize_presets[x].name ? v.quantize_presets[x].name
                                                                     : "(unnamed)";
    quantize_setting_spacing(c);
    nk_layout_row_dynamic(c, 18, 1);
    const auto heading = nk_widget_bounds(c);
    nk_label(c, "Presets", NK_TEXT_LEFT);
    const auto spacing = c->style.window.spacing;
    const auto first =
        nk_rect(heading.x, heading.y + heading.h + spacing.y, (heading.w - 3 * spacing.x) / 4,
                wormedit::ui::metrics::control_compact);
    const auto chosen =
        v.quantize_selected_preset_valid ? static_cast<int>(v.quantize_selected_preset_index) : -1;
    const auto result =
        wormedit::ui::chrome::ChipGrid{labels.data(), static_cast<int>(count), chosen, 4, "+"}
            .render(c);
    if (result.chosen >= 0)
        set_quantize_action(i, WORM_UI_COMPOSE_ACTION_SELECT_QUANTIZE_PRESET, v.quantize_divisions,
                            v.quantize_space, v.quantize_tie, v.quantize_holds, true,
                            result.chosen);
    if (result.add) {
        quantize_rename = false;
        quantize_preset_name.fill('\0');
        open_quantize_popup(i, QuantizePopup::name);
    }
    if (compose_quantize_owns_input())
        return;
    for (std::size_t x = 0; x < count; ++x) {
        const auto box = nk_rect(first.x + (first.w + spacing.x) * (x % 4),
                                 first.y + (first.h + spacing.y) * (x / 4), first.w, first.h);
        if (nk_input_is_mouse_pressed(&c->input, NK_BUTTON_RIGHT) &&
            nk_input_is_mouse_hovering_rect(&c->input, nk_window_get_content_region(c)) &&
            nk_input_is_mouse_hovering_rect(&c->input, box)) {
            quantize_menu_preset = x;
            open_quantize_popup(i, QuantizePopup::preset);
        }
    }
}
void render_quantize_custom_popup(const ComposeInspectorContext& i) {
    const auto& v = *i.view;
    auto* c = i.context;
    nk_layout_row_dynamic(c, 24, 1);
    nk_label(c, "Grid divisions", NK_TEXT_LEFT);
    nk_layout_row_dynamic(c, 25, 1);
    const auto edit = wormedit::ui::chrome::worm_ui_nuklear_text_field(
        c, static_cast<nk_flags>(NK_EDIT_FIELD) | NK_EDIT_SIG_ENTER, quantize_grid_text.data(), quantize_grid_text.size(),
        nk_filter_decimal);
    quantize_grid_editing = (edit & NK_EDIT_ACTIVE) != 0;
    nk_layout_row_dynamic(c, 25, 1);
    const auto apply = nk_button_label(c, "Apply custom grid");
    if (apply || (edit & NK_EDIT_COMMITED) != 0) {
        const auto divisions = parse_quantize_grid(quantize_grid_text.data());
        quantize_grid_invalid = !divisions;
        if (divisions) {
            quantize_grid_editing = false;
            set_quantize_action(i, WORM_UI_COMPOSE_ACTION_SET_QUANTIZE_SETTINGS, *divisions,
                                v.quantize_space, v.quantize_tie, v.quantize_holds);
            wormedit::ui::chrome::context_menu_close(quantize_menu, c);
        }
    }
    if (quantize_grid_invalid) {
        nk_layout_row_dynamic(c, 36, 1);
        nk_label_wrap(c, "Grid must be a positive whole number");
    }
}
void render_quantize_name_popup(const ComposeInspectorContext& i) {
    const auto& v = *i.view;
    auto* c = i.context;
    nk_layout_row_dynamic(c, 24, 1);
    nk_label(c, quantize_rename ? "Rename preset" : "Preset name", NK_TEXT_LEFT);
    nk_layout_row_dynamic(c, 25, 1);
    wormedit::ui::chrome::worm_ui_nuklear_text_field(c, NK_EDIT_FIELD, quantize_preset_name.data(),
                                                     quantize_preset_name.size(),
                                                     nk_filter_default);
    const auto valid = v.scene_available && !quantize_grid_editing && !quantize_grid_invalid &&
                       quantize_preset_name_is_unique(v, quantize_preset_name.data());
    if (wormedit::ui::chrome::context_menu_item(
            quantize_menu, c, quantize_rename ? "Rename" : "Save preset",
            valid ? nullptr
                  : "Enter a unique preset name; the preset capacity must not be full.")) {
        set_quantize_action(i,
                            quantize_rename ? WORM_UI_COMPOSE_ACTION_RENAME_QUANTIZE_PRESET
                                            : WORM_UI_COMPOSE_ACTION_SAVE_QUANTIZE_PRESET,
                            v.quantize_divisions, v.quantize_space, v.quantize_tie,
                            v.quantize_holds, quantize_rename, quantize_menu_preset);
        std::memcpy(i.action->compose_quantize_preset_name, quantize_preset_name.data(),
                    quantize_preset_name.size());
    }
}
void render_quantize_preset_actions(const ComposeInspectorContext& i) {
    auto* c = i.context;
    const auto& v = *i.view;
    if (!wormedit::ui::chrome::context_menu_begin(quantize_menu, c, wormedit::ui::chrome::PopupContextStamp::from(v, quantize_menu_preset),
                                                  nk_vec2(300, 160)))
        return;
    if (quantize_popup == QuantizePopup::custom)
        render_quantize_custom_popup(i);
    else if (quantize_popup == QuantizePopup::name)
        render_quantize_name_popup(i);
    else {
        nk_layout_row_dynamic(c, 25, 1);
        if (wormedit::ui::chrome::context_menu_button(c, "Rename")) {
            quantize_rename = true;
            quantize_popup = QuantizePopup::name;
            quantize_menu.shared_keyboard_navigation = false;
            std::snprintf(quantize_preset_name.data(), quantize_preset_name.size(), "%s",
                          v.quantize_presets[quantize_menu_preset].name);
        }
        if (wormedit::ui::chrome::context_menu_item(quantize_menu, c, "Delete"))
            set_quantize_action(i, WORM_UI_COMPOSE_ACTION_DELETE_QUANTIZE_PRESET,
                                v.quantize_divisions, v.quantize_space, v.quantize_tie,
                                v.quantize_holds, true, quantize_menu_preset);
    }
    wormedit::ui::chrome::context_menu_end(quantize_menu, c);
}

void quantize_report_label(nk_context* context, const char* label) {
    auto height = 40.0f;
    const auto* font = context->style.font;
    if (font && font->width) {
        const auto width = std::max(64.0f, nk_window_get_content_region(context).w - 32.0f);
        const auto text_width =
            font->width(font->userdata, font->height, label, static_cast<int>(std::strlen(label)));
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
        std::snprintf(label.data(), label.size(), "%s %llu · lane %llu · %s, collision with %llu",
                      row.bgm ? "BGM" : "Note",
                      static_cast<unsigned long long>(row.object_id),
                      static_cast<unsigned long long>(row.lane_id),
                      row.moved ? "moved" : "unchanged",
                      static_cast<unsigned long long>(row.collision_peer_id));
    } else {
        std::snprintf(label.data(), label.size(), "%s %llu · lane %llu · %s%s",
                      row.bgm ? "BGM" : "Note",
                      static_cast<unsigned long long>(row.object_id),
                      static_cast<unsigned long long>(row.lane_id),
                      row.moved ? "moved" : "unchanged", row.collision ? " · collision" : "");
    }
    quantize_report_label(context, label.data());
    std::snprintf(label.data(), label.size(), "Start m%llu:r%llu -> m%llu:r%llu",
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
                                 const WormUiComposeQuantizeReportRowView* rows, std::size_t count,
                                 std::size_t total, bool truncated,
                                 nk_collapse_states initial = NK_MINIMIZED) {
    if (!nk_tree_push_id(context, NK_TREE_TAB, title, initial, tree_id))
        return;
    if (truncated) {
        nk_layout_row_dynamic(context, 18, 1);
        nk_labelf(context, NK_TEXT_LEFT, "%zu rows shown of %zu", count, total);
    }
    const auto visible_count =
        std::min(count, static_cast<std::size_t>(WORM_UI_COMPOSE_QUANTIZE_REPORT_CAPACITY));
    for (std::size_t index = 0; rows && index < visible_count; ++index)
        render_quantize_report_row(context, rows[index]);
    nk_tree_pop(context);
}

} // namespace

void reset_compose_quantize_popup() {
    wormedit::ui::chrome::context_menu_reset(quantize_menu);
    quantize_grid_editing = quantize_grid_invalid = false;
    quantize_grid_project_id = quantize_grid_chart_id = quantize_grid_projected_value = 0;
    quantize_grid_text.fill('\0');
    quantize_menu_presets.clear();
}

void abandon_compose_quantize_popup() {
    wormedit::ui::chrome::context_menu_abandon(quantize_menu);
    quantize_grid_editing = quantize_grid_invalid = false;
}

auto compose_quantize_owns_input() -> bool {
    return wormedit::ui::chrome::context_menu_owns_input(quantize_menu);
}

void reset_compose_quantize_section_visibility() {
    quantize_section_visible_this_frame = false;
}

auto compose_quantize_section_visible() -> bool {
    return quantize_section_visible_this_frame;
}

static auto quantize_presets_match(const WormUiComposeView& view) -> bool {
    if (view.quantize_preset_count != quantize_menu_presets.size())
        return false;
    for (std::size_t index = 0; index < view.quantize_preset_count; ++index) {
        if (!view.quantize_presets)
            return false;
        const auto& preset = view.quantize_presets[index];
        const auto& expected = quantize_menu_presets[index];
        if (expected.name != (preset.name ? preset.name : "") ||
            expected.divisions != preset.divisions || expected.space != preset.space ||
            expected.tie != preset.tie || expected.holds != preset.holds)
            return false;
    }
    return true;
}

void prepare_compose_quantize_input(nk_context* context, const WormUiComposeView& view,
                                    bool inspector_visible, bool window_focused) {
    quantize_menu.opened_this_frame = false;
    (void)wormedit::ui::chrome::popup_update_release_pending(context,
                                                             quantize_menu.release_pending);
    if (!inspector_visible || !window_focused || view.input_occluded) {
        abandon_compose_quantize_popup();
        return;
    }
    if (quantize_menu.open &&
        (!wormedit::ui::chrome::popup_context_matches(quantize_menu.stamp, wormedit::ui::chrome::PopupContextStamp::from(view, quantize_menu_preset)) ||
         !quantize_presets_match(view)))
        wormedit::ui::chrome::context_menu_close(quantize_menu);
}

void render_quantize_preview_toggle(const ComposeInspectorContext& inspector) {
    auto* context = inspector.context;
    nk_bool preview = inspector.view->quantize_preview_enabled;
    if (compose_quantize_owns_input()) nk_widget_disable_begin(context);
    const auto changed = nk_checkbox_label(context, "Preview", &preview);
    if (compose_quantize_owns_input()) nk_widget_disable_end(context);
    if (changed && !compose_quantize_owns_input() &&
        inspector.action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
        set_quantize_action(inspector, WORM_UI_COMPOSE_ACTION_TOGGLE_QUANTIZE_PREVIEW,
            inspector.view->quantize_divisions, inspector.view->quantize_space,
            inspector.view->quantize_tie, inspector.view->quantize_holds);
}

void render_quantize_action_row(const ComposeInspectorContext& inspector) {
    auto* context = inspector.context;
    const auto* view = inspector.view;
    auto* action = inspector.action;
    const auto popup_owns_input = compose_quantize_owns_input();
    wormedit::ui::chrome::section_separator(context);
    nk_layout_row_dynamic(context, 26, 2);
    render_quantize_preview_toggle(inspector);
    const auto enabled = view->scene_available && (view->selected_note_count + view->selected_bgm_event_count) != 0 &&
                         view->quantize_preview_blocked == 0 && view->quantize_preview_moved != 0 &&
                         !quantize_grid_editing && !quantize_grid_invalid && !popup_owns_input;
    if (!enabled)
        nk_widget_disable_begin(context);
    const auto bounds = nk_widget_bounds(context);
    const auto clicked = nk_button_label(context, view->quantize_q_bound ? "Quantize (Q)"
                                                                         : "Quantize");
    if (!enabled)
        nk_widget_disable_end(context);
    if (!enabled && !popup_owns_input && wormedit::ui::chrome::button_hovered(context, bounds))
        wormedit::ui::nuklear::tooltip::text(
            context, view->quantize_preview_blocked
                         ? view->quantize_preview_message
                         : "Select notes or BGM that would move and finish editing a valid grid.");
    if (clicked && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
        set_quantize_action(inspector, WORM_UI_COMPOSE_ACTION_AREA_QUANTIZE,
                            view->quantize_divisions, view->quantize_space, view->quantize_tie,
                            view->quantize_holds, view->quantize_selected_preset_valid,
                            view->quantize_selected_preset_index);
}

void render_compose_quantize_section(const ComposeInspectorContext& inspector) {
    const auto* view = inspector.view;
    auto* context = inspector.context;
    sync_quantize_grid_text(*view);
    const auto section_clip = nk_window_get_canvas(context)->clip;
    if (!nk_tree_push(context, NK_TREE_TAB, "Quantize / Snap", NK_MAXIMIZED)) return;
    wormedit::ui::chrome::context_menu_retire(quantize_menu, context);
    if (!quantize_menu.open) {
        if (quantize_grid_editing || quantize_grid_invalid)
            quantize_grid_projected_value = 0;
        quantize_grid_editing = false;
        quantize_grid_invalid = false;
        sync_quantize_grid_text(*view);
    }
    nk_layout_row_dynamic(context, 18, 1);
    const auto header_bounds = nk_widget_bounds(context);
    nk_labelf(context, NK_TEXT_LEFT, "Quantize (%zu)", (view->selected_note_count + view->selected_bgm_event_count));
    if ((view->selected_note_count + view->selected_bgm_event_count) == 0) {
        nk_layout_row_dynamic(context, 18, 1);
        nk_label(context, "Select notes or BGM first", NK_TEXT_LEFT);
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
    const auto popup_owns_input = compose_quantize_owns_input();
    if (popup_owns_input)
        nk_widget_disable_begin(context);
    render_quantize_setting_fields(inspector);
    render_quantize_preset_picker(inspector);
    if (popup_owns_input)
        nk_widget_disable_end(context);
    render_quantize_action_row(inspector);
    render_quantize_preset_actions(inspector);
    if (view->quantize_preview_enabled)
        render_quantize_report_rows(context, "Preview rows", 0, view->quantize_preview_rows,
                                view->quantize_preview_row_count, view->quantize_preview_total_rows,
                                view->quantize_preview_rows_truncated);
    // Only after an apply that moved rows; an empty heading says nothing, and it opens expanded so
    // the moved rows are visible without another click.
    if (view->quantize_applied_row_count != 0)
        render_quantize_report_rows(context, "Last applied report", 1, view->quantize_applied_rows,
                                    view->quantize_applied_row_count,
                                    view->quantize_applied_total_rows,
                                    view->quantize_applied_rows_truncated, NK_MAXIMIZED);
    const auto section_bounds = nk_widget_bounds(context);
    quantize_section_visible_this_frame |= quantize_bounds_intersect(
        header_bounds, section_bounds, section_clip);
    nk_tree_pop(context);
}
