#include "ui/nuklear/catalog_chrome.h"
#include "ui/compose_draw_source_labels.h"
#include "ui/nuklear/content_layout.h"
#include "core/text_util.h"
#include "ui/nuklear/compose_sounds_panel.h"
#include "ui/nuklear/catalog_confirmation.h"
#include "ui/nuklear/chrome.h"
#include "ui/nuklear/compose_action_stamp.h"
#include "ui/nuklear/compose_bmson_tab.h"
#include "ui/nuklear/compose_panel_internal.h"
#include "ui/nuklear/compose_resources.h"
#include "ui/nuklear/lucide_icons.h"
#include "ui/nuklear/popup_lifecycle.h"
#include "ui/nuklear/sound_import_state.h"
#include "ui/nuklear/sound_import_runtime.h"
#include "ui/nuklear/theme.h"
#include "ui/nuklear/tooltip.h"
#include "ui/nuklear/vector_icons.h"

#include "wormedit/nuklear_config.h"

#include <SDL3/SDL_keyboard.h>

#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace {

using wormedit::ui::chrome::color;
using wormedit::ui::chrome::DrawnIconButton;
using wormedit::ui::chrome::IconButton;
using wormedit::ui::lucide::Glyph;
using wormedit::ui::theme::UiColor;
namespace icons = wormedit::ui::vector_icons;

constexpr float row_height = 22.0f;
constexpr float layer_row_height = 22.0f;
constexpr float chip_size = 8.0f;

struct SoundsPanel {
    nk_context* context;
    const WormUiComposeView* view;
    WormUiWorkspaceAction* action;
    worm_ui_nuklear_compose_state_t* state;
};

void prepare_pending_group_review(const SoundsPanel& panel);
void render_sound_group_context_menu(const SoundsPanel& panel);

auto bms_slot_label(std::uint16_t number) -> std::string {
    constexpr std::string_view digits = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    if (number == 0 || number >= 1296) return "??";
    std::string result;
    result.push_back(digits[number / 36]);
    result.push_back(digits[number % 36]);
    return result;
}

// ---- Actions ----

auto raise(const SoundsPanel& panel, WormUiComposeActionKind kind) -> bool {
    return wormedit::ui::raise_compose_action(*panel.action, *panel.view, kind);
}

void stage_selection(const SoundsPanel& panel) {
    auto& state = *panel.state;
    state.keysound_catalog.keysound_staged_ids = state.keysound_catalog.keysound_selection.selected;
    panel.action->compose_sound_resource_ids = state.keysound_catalog.keysound_staged_ids.data();
    panel.action->compose_sound_resource_count = state.keysound_catalog.keysound_staged_ids.size();
}

void raise_on_selection(const SoundsPanel& panel, WormUiComposeActionKind kind,
                        std::uint64_t layer_id = 0) {
    if (!raise(panel, kind)) return;
    stage_selection(panel);
    panel.action->compose_semantic_layer_id = layer_id;
}

void stage_path_payload(const SoundsPanel& panel, std::string_view path) {
    auto& state = *panel.state;
    state.keysound_catalog.keysound_staged_paths = {std::string{path}};
    state.keysound_catalog.keysound_staged_path_pointers.clear();
    state.keysound_catalog.keysound_staged_path_pointers.push_back(state.keysound_catalog.keysound_staged_paths.front().c_str());
}

void raise_sound_slot_assignment(const SoundsPanel& panel, std::uint16_t slot,
                                 std::string_view path, bool confirmed) {
    if (!raise(panel, WORM_UI_COMPOSE_ACTION_ASSIGN_SOUND_SLOT)) return;
    stage_path_payload(panel, path);
    panel.action->compose_sound_slot = slot;
    panel.action->compose_sound_slot_confirmed = confirmed;
    panel.action->compose_import_paths = panel.state->keysound_catalog.keysound_staged_path_pointers.data();
    panel.action->compose_import_path_count = panel.state->keysound_catalog.keysound_staged_path_pointers.size();
}

void process_slot_file(const SoundsPanel& panel, std::uint64_t project, std::uint64_t chart,
                       std::uint64_t revision, std::uint16_t slot,
                       std::uint64_t occupied_resource, std::string_view path) {
    auto& state = *panel.state;
    if (!state.keysound_filters.keysound_slots_mode || !panel.view->sound_slots_mode ||
        panel.view->project_id != project || panel.view->chart_id != chart ||
        panel.view->document_revision != revision || slot == 0 || slot > 1295) {
        state.keysound_slots.keysound_slot_feedback = "The WAV slot target changed; choose the file again.";
        return;
    }
    state.keysound_slots.keysound_slot_feedback.clear();
    if (occupied_resource != 0) {
        state.keysound_slots.keysound_slot_confirmation_open = true;
        state.keysound_slots.keysound_slot_confirmation_project = project;
        state.keysound_slots.keysound_slot_confirmation_chart = chart;
        state.keysound_slots.keysound_slot_confirmation_revision = revision;
        state.keysound_slots.keysound_slot_confirmation_number = slot;
        state.keysound_slots.keysound_slot_confirmation_resource = occupied_resource;
        state.keysound_slots.keysound_slot_confirmation_path.assign(path);
        return;
    }
    raise_sound_slot_assignment(panel, slot, path, false);
}

void process_pending_slot_file(const SoundsPanel& panel) {
    auto& state = *panel.state;
    if (!state.keysound_slots.keysound_pending_file || panel.action->compose_action != WORM_UI_COMPOSE_ACTION_NONE)
        return;
    process_slot_file(panel, state.keysound_slots.keysound_pending_project, state.keysound_slots.keysound_pending_chart,
                      state.keysound_slots.keysound_pending_revision, state.keysound_slots.keysound_pending_slot,
                      state.keysound_slots.keysound_pending_occupied_resource, state.keysound_slots.keysound_pending_path);
    state.keysound_slots.keysound_pending_file = false;
    state.keysound_slots.keysound_pending_path.clear();
}

// ---- Selection state kept in step with the document ----

auto find_row(const WormUiComposeView& view, std::uint64_t id)
    -> const WormUiComposeKeysoundRowView* {
    for (std::size_t index = 0; index < view.keysound_count; ++index)
        if (view.keysounds[index].resource_id == id) return &view.keysounds[index];
    for (std::size_t index = 0; index < view.keysound_selected_row_count; ++index)
        if (view.keysound_selected_rows[index].resource_id == id)
            return &view.keysound_selected_rows[index];
    return nullptr;
}

auto find_selection_row(const WormUiComposeView& view, std::uint64_t id)
    -> const WormUiComposeKeysoundSelectionView* {
    for (std::size_t index = 0; index < view.keysound_selection_row_count; ++index)
        if (view.keysound_selection_rows[index].resource_id == id)
            return &view.keysound_selection_rows[index];
    return nullptr;
}

auto selected_group_id(const WormUiComposeView& view, std::uint64_t id) -> std::uint64_t {
    if (const auto* selected = find_selection_row(view, id)) return selected->group_id;
    const auto* row = find_row(view, id);
    return row ? row->sound_group_id : 0;
}

auto selected_conflict_mask(const WormUiComposeView& view, std::uint64_t id) -> std::uint64_t {
    if (const auto* selected = find_selection_row(view, id))
        return selected->note_layer_conflict_mask;
    const auto* row = find_row(view, id);
    return row ? row->note_layer_conflict_mask : 0;
}

void prune_filter_ids(std::vector<std::uint64_t>& ids,
                      const WormUiComposeSoundGroupView* groups, std::size_t count) {
    std::erase_if(ids, [&](auto id) {
        return !groups || std::ranges::none_of(std::span{groups, count},
            [&](const auto& group) { return group.group_id == id; });
    });
}

void sync_filter_state(const SoundsPanel& panel) {
    auto& state = *panel.state;
    const auto& view = *panel.view;
    if (state.keysound_filters.keysound_filter_project_id != view.project_id) {
        if (state.keysound_filters.keysound_filter_project_id != 0) {
            state.keysound_filters.keysound_filters.search = state.keysound_catalog.keysound_search;
            state.keysound_filters.keysound_filter_states_by_project[state.keysound_filters.keysound_filter_project_id] =
                state.keysound_filters.keysound_filters;
        }
        const auto saved = state.keysound_filters.keysound_filter_states_by_project.find(view.project_id);
        state.keysound_filters.keysound_filters = saved == state.keysound_filters.keysound_filter_states_by_project.end()
            ? worm_ui_keysound_filter_state_t{} : saved->second;
        state.keysound_catalog.keysound_search = state.keysound_filters.keysound_filters.search;
        state.keysound_filters.keysound_filter_layer_first = 0;
        state.keysound_filters.keysound_filter_project_id = view.project_id;
        state.keysound_filters.keysound_filter_chart_id = 0;
    }
    state.keysound_filters.keysound_filters.search = state.keysound_catalog.keysound_search;
    if (state.keysound_filters.keysound_filter_chart_id != view.chart_id) {
        if (state.keysound_filters.keysound_filters.layer_chart_id != view.chart_id) {
            state.keysound_filters.keysound_filters.layer_filter_kind = WORM_UI_KEYSOUND_LAYER_FILTER_ANY;
            state.keysound_filters.keysound_filters.layer_filter_id = 0;
            state.keysound_filters.keysound_filters.layer_chart_id = view.chart_id;
            state.keysound_filters.keysound_filter_layer_first = 0;
        }
        state.keysound_filters.keysound_filter_chart_id = view.chart_id;
    }
    prune_filter_ids(state.keysound_filters.keysound_filters.group_ids, view.compose_sound_groups,
                     view.compose_sound_group_count);
    prune_filter_ids(state.keysound_filters.keysound_filters.collapsed_group_ids, view.compose_sound_groups,
                     view.compose_sound_group_count);
    if (view.keysound_section_projection_available &&
        state.keysound_filters.keysound_filters.layer_filter_kind == WORM_UI_KEYSOUND_LAYER_FILTER_SPECIFIC &&
        !view.keysound_filter_selected_layer_valid) {
            state.keysound_filters.keysound_filters.layer_filter_kind = WORM_UI_KEYSOUND_LAYER_FILTER_ANY;
            state.keysound_filters.keysound_filters.layer_filter_id = 0;
    }
    state.keysound_filters.keysound_filter_states_by_project[view.project_id] = state.keysound_filters.keysound_filters;
    prepare_pending_group_review(panel);
}

void sync_with_document(const SoundsPanel& panel) {
    auto& state = *panel.state;
    const auto& view = *panel.view;
    if (state.keysound_catalog.keysound_project_id != view.project_id || state.keysound_catalog.keysound_chart_id != view.chart_id) {
        worm_ui_keysound_clear(state.keysound_catalog.keysound_selection);
        state.keysound_catalog.keysound_project_id = view.project_id;
        state.keysound_catalog.keysound_chart_id = view.chart_id;
        state.keysound_catalog.keysound_revision = view.document_revision;
        return;
    }
    if (state.keysound_catalog.keysound_revision == view.document_revision) return;
    state.keysound_catalog.keysound_revision = view.document_revision;
    state.keysound_catalog.keysound_all_ids.clear();
    if (view.keysound_section_projection_available) {
        state.keysound_catalog.keysound_all_ids.clear();
        if (view.keysound_all_resource_ids)
            state.keysound_catalog.keysound_all_ids.assign(view.keysound_all_resource_ids,
                view.keysound_all_resource_ids + view.keysound_all_resource_count);
    } else {
        for (std::size_t index = 0; index < view.keysound_count; ++index)
            state.keysound_catalog.keysound_all_ids.push_back(view.keysounds[index].resource_id);
    }
    worm_ui_keysound_prune(state.keysound_catalog.keysound_selection, state.keysound_catalog.keysound_all_ids);
    if (state.keysound_catalog.armed_from_sounds_dock &&
        std::ranges::find(state.keysound_catalog.keysound_all_ids, state.resource_picker.armed_resource_id) == state.keysound_catalog.keysound_all_ids.end())
        worm_ui_nuklear_pick_draw_source(state, *panel.action, 0, 0);
}

void reset_sound_slots(worm_ui_nuklear_compose_state_t& state, std::uint64_t project,
                       std::uint64_t chart, std::uint64_t revision) {
    if (state.keysound_slots.keysound_pending_file || state.keysound_slots.keysound_slot_confirmation_open) {
        state.keysound_slots.keysound_slot_feedback = "The WAV slot target changed; choose the file again.";
        state.keysound_slots.keysound_slot_release_pending = true;
    }
    state.keysound_filters.keysound_slots_mode = false;
    state.keysound_catalog.keysound_slots_search.fill(0);
    state.keysound_catalog.keysound_slots_first_row = 0;
    state.keysound_catalog.keysound_slots_row_limit = 64;
    state.keysound_catalog.keysound_selected_slot = 1;
    state.keysound_catalog.keysound_selected_slot_explicit = false;
    state.keysound_catalog.keysound_selected_slot_resource = 0;
    state.keysound_catalog.keysound_selected_slot_resource_known = false;
    state.keysound_slots.keysound_pending_file = false;
    state.keysound_slots.keysound_pending_path.clear();
    state.keysound_catalog.keysound_slots_project = project;
    state.keysound_catalog.keysound_slots_chart = chart;
    state.keysound_catalog.keysound_slots_revision = revision;
}

void sync_sound_slots(const SoundsPanel& panel) {
    auto& state = *panel.state;
    const auto& view = *panel.view;
    if (state.keysound_catalog.keysound_slots_project != view.project_id ||
        state.keysound_catalog.keysound_slots_chart != view.chart_id) {
        reset_sound_slots(state, view.project_id, view.chart_id, view.document_revision);
    }
    if (state.keysound_catalog.keysound_slots_revision != view.document_revision) {
        if (state.keysound_slots.keysound_pending_file || state.keysound_slots.keysound_slot_confirmation_open) {
            state.keysound_slots.keysound_slot_feedback = "The WAV slot target changed; choose the file again.";
            state.keysound_slots.keysound_slot_release_pending = true;
        }
        state.keysound_catalog.keysound_slots_revision = view.document_revision;
        state.keysound_catalog.keysound_selected_slot_resource = 0;
        state.keysound_catalog.keysound_selected_slot_resource_known = false;
        state.keysound_slots.keysound_pending_file = false;
        state.keysound_slots.keysound_pending_path.clear();
    }
    if (!view.keysound_ids_available && state.keysound_filters.keysound_slots_mode)
        reset_sound_slots(state, view.project_id, view.chart_id, view.document_revision);
}

void collect_shown_and_masks(const SoundsPanel& panel) {
    auto& state = *panel.state;
    const auto& view = *panel.view;
    state.keysound_catalog.keysound_shown.clear();
    state.keysound_catalog.keysound_advance_rows.clear();
    state.keysound_catalog.keysound_masks.clear();
    for (std::size_t index = 0; index < view.keysound_count; ++index)
        state.keysound_catalog.keysound_masks.push_back({view.keysounds[index].resource_id,
                                        view.keysounds[index].layer_mask});
    for (std::size_t index = 0; index < view.keysound_selection_row_count; ++index)
        state.keysound_catalog.keysound_masks.push_back({view.keysound_selection_rows[index].resource_id,
                                        view.keysound_selection_rows[index].layer_mask});
    if (state.keysound_filters.keysound_slots_mode) {
        const std::string_view query{state.keysound_catalog.keysound_slots_search.data()};
        for (std::size_t index = 0; index < view.keysound_count; ++index) {
            const auto& row = view.keysounds[index];
            if (row.slot && row.slot[0] != '\0' &&
                worm_ui_keysound_matches(row.slot, row.name, query)) {
                state.keysound_catalog.keysound_shown.push_back(row.resource_id);
                state.keysound_catalog.keysound_advance_rows.push_back({row.resource_id, row.missing});
            }
        }
        return;
    }
    if (view.keysound_section_projection_available) {
        state.keysound_catalog.keysound_shown.assign(view.keysound_listed_resource_ids,
            view.keysound_listed_resource_ids + view.keysound_listed_resource_count);
        for (std::size_t index = 0; index < view.keysound_advance_row_count; ++index)
            state.keysound_catalog.keysound_advance_rows.push_back({view.keysound_advance_rows[index].resource_id,
                                                   view.keysound_advance_rows[index].missing});
    } else {
        const std::string_view query{state.keysound_catalog.keysound_search.data()};
        for (std::size_t index = 0; index < view.keysound_count; ++index) {
            const auto& row = view.keysounds[index];
            if (worm_ui_keysound_matches(row.slot, row.name, query, row.source_path ? row.source_path : "") &&
                worm_ui_resource_use_matches(row.use_count, row.missing, state.keysound_filters.keysound_filters.use_filter)) {
                state.keysound_catalog.keysound_shown.push_back(row.resource_id);
                state.keysound_catalog.keysound_advance_rows.push_back({row.resource_id, row.missing});
            }
        }
    }
}

auto parse_slot_number(std::string_view identifier) -> std::uint16_t {
    if (identifier.size() != 2) return 0;
    constexpr std::string_view digits = "0123456789abcdefghijklmnopqrstuvwxyz";
    const auto high = digits.find(wormedit::text_util::ascii_lowercase(identifier[0]));
    const auto low = digits.find(wormedit::text_util::ascii_lowercase(identifier[1]));
    if (high == digits.npos || low == digits.npos) return 0;
    const auto number = high * 36 + low;
    return number > 1295 ? 0 : static_cast<std::uint16_t>(number);
}

void refresh_selected_slot_resource(const SoundsPanel& panel) {
    auto& state = *panel.state;
    const auto& view = *panel.view;
    for (std::size_t index = 0; index < view.sound_slot_count; ++index) {
        const auto& slot = view.sound_slots[index];
        if (slot.number != state.keysound_catalog.keysound_selected_slot) continue;
        state.keysound_catalog.keysound_selected_slot_resource = slot.resource_id;
        state.keysound_catalog.keysound_selected_slot_resource_known = true;
        return;
    }
}

void sync_selected_slot_to_armed_resource(const SoundsPanel& panel) {
    auto& state = *panel.state;
    if (!state.keysound_filters.keysound_slots_mode || !state.keysound_catalog.keysound_reveal_primary) return;
    const auto* row = find_row(*panel.view, state.keysound_catalog.keysound_selection.primary);
    const auto number = row && row->slot ? parse_slot_number(row->slot) : 0;
    if (number != 0) {
        if (number != state.keysound_catalog.keysound_selected_slot)
            state.keysound_catalog.keysound_selected_slot_explicit = false;
        state.keysound_catalog.keysound_selected_slot = number;
        state.keysound_catalog.keysound_selected_slot_resource = row->resource_id;
        state.keysound_catalog.keysound_selected_slot_resource_known = true;

    }
}

// ---- Toolbar ----

template <typename Button>
auto toolbar_button(nk_context* context, bool enabled, const Button& button) -> bool;

auto sound_mode_button(nk_context* context, const char* label, bool active) -> bool {
    const auto styled = active && nk_style_push_style_item(context, &context->style.button.normal,
        nk_style_item_color(color(UiColor::selected)));
    const auto clicked = nk_button_label(context, label) != 0;
    if (styled) nk_style_pop_style_item(context);
    return clicked;
}

void render_mode_row(const SoundsPanel& panel) {
    if (!panel.view->keysound_ids_available) return;
    auto& state = *panel.state;
    auto* context = panel.context;
    nk_layout_row_dynamic(context, 24, 2);
    if (sound_mode_button(context, "Automatic IDs", !state.keysound_filters.keysound_slots_mode)) {
        state.keysound_filters.keysound_slots_mode = false;
        state.keysound_catalog.keysound_selected_slot_explicit = false;
        if (state.keysound_slots.keysound_slot_confirmation_open) {
            const auto bounds = nk_rect(0, 60,
                std::min(360.0f, nk_window_get_content_region(context).w), 180);
            wormedit::ui::chrome::dismiss_popup(context, "Replace BMS WAV slot", bounds);
        }
        state.keysound_slots.keysound_slot_confirmation_open = false;
        state.keysound_slots.keysound_pending_file = false;
        state.keysound_slots.keysound_pending_path.clear();
        state.keysound_slots.keysound_slot_confirmation_path.clear();
        state.keysound_slots.keysound_slot_release_pending = true;
    }
    if (sound_mode_button(context, "BMS slots", state.keysound_filters.keysound_slots_mode)) {
        state.keysound_filters.keysound_slots_mode = true;
        state.keysound_catalog.keysound_slots_first_row = 0;
        state.keysound_catalog.keysound_slots_search.fill(0);
        state.keysound_catalog.keysound_selected_slot = 1;
        state.keysound_catalog.keysound_selected_slot_explicit = false;
        state.keysound_catalog.keysound_selected_slot_resource = 0;
    }
}

void render_add_sounds_button(const SoundsPanel& panel) {
    auto& state = *panel.state;
    const auto label = state.keysound_filters.keysound_slots_mode && state.keysound_catalog.keysound_selected_slot_explicit
        ? "Choose files for consecutive BMS WAV slots"
        : "Add sounds from files (you can also drop audio files on this list)";
    if (!toolbar_button(panel.context, !worm_ui_nuklear_sound_import_active(), DrawnIconButton{icons::plus, label})) return;
    if (state.keysound_filters.keysound_slots_mode && state.keysound_catalog.keysound_selected_slot_explicit &&
        !state.keysound_catalog.keysound_selected_slot_resource_known) {
        state.keysound_slots.keysound_slot_feedback = "Wait for the selected WAV slot to load, or select a slot.";
        return;
    }
    panel.action->sound_import_pick_requested = true;
}

void render_section_add_buttons(const SoundsPanel& panel) {
    if (!panel.view->bmson_tools_available || panel.state->keysound_filters.keysound_slots_mode) return;
    nk_layout_row_template_begin(panel.context, 24);
    nk_layout_row_template_push_dynamic(panel.context);
    nk_layout_row_template_push_static(panel.context, 116);
    nk_layout_row_template_end(panel.context);
    nk_label(panel.context, "Stems", NK_TEXT_LEFT);
    if (nk_button_label(panel.context, "Add stem…")) {
        panel.action->sound_import_pick_requested = true;
        panel.action->sound_import_kind = WORM_UI_SOUND_IMPORT_STEM;
    }
    nk_layout_row_template_begin(panel.context, 24);
    nk_layout_row_template_push_dynamic(panel.context);
    nk_layout_row_template_push_static(panel.context, 116);
    nk_layout_row_template_end(panel.context);
    nk_label(panel.context, "Keysounds", NK_TEXT_LEFT);
    if (nk_button_label(panel.context, "Add keysound…")) {
        panel.action->sound_import_pick_requested = true;
        panel.action->sound_import_kind = WORM_UI_SOUND_IMPORT_KEYSOUND;
    }
}

void render_id_sort_controls(const SoundsPanel& panel) {
    auto& state = *panel.state;
    auto* context = panel.context;
    if (!panel.view->keysound_ids_available || state.keysound_filters.keysound_slots_mode) return;
    if (IconButton{Glyph::chevrons_left, state.keysound_filters.keysound_sort_by_id
                       ? "Sort by name" : "Sort by BMS ID", state.keysound_filters.keysound_sort_by_id}.render(context))
        state.keysound_filters.keysound_sort_by_id = !state.keysound_filters.keysound_sort_by_id;
    if (toolbar_button(context, true,
                       IconButton{Glyph::rotate_ccw, "Renumber BMS IDs by name"})) {
        wormedit::ui::nuklear::open_catalog_confirmation(
            wormedit::ui::nuklear::catalog_confirmation(state, false), *panel.view, state.keysound_catalog.keysound_selection.selected);
    }
}

template <typename Button>
auto toolbar_button(nk_context* context, bool enabled, const Button& button) -> bool {
    if (!enabled) nk_widget_disable_begin(context);
    const auto clicked = button.render(context);
    if (!enabled) nk_widget_disable_end(context);
    return clicked && enabled;
}

void render_toolbar(const SoundsPanel& panel) {
    auto* context = panel.context;
    auto& state = *panel.state;
    const auto has_primary = state.keysound_catalog.keysound_selection.primary != 0;
    const auto has_selection = !state.keysound_catalog.keysound_selection.selected.empty();
    const auto spacing = context->style.window.spacing;
    const auto pushed = nk_style_push_vec2(context, &context->style.window.spacing,
                                           nk_vec2(2.0f, spacing.y)) != 0;
    nk_layout_row_template_begin(context, 27);
    IconButton::layout_columns(context, panel.view->keysound_ids_available ? 11 : 9, 27.0f);
    nk_layout_row_template_end(context);

    if (IconButton{Glyph::minus, wormedit::ui::no_draw_sound_resource_tooltip, state.pointer_capture.draw_source_kind == 0}.render(context))
        worm_ui_nuklear_pick_draw_source(state, *panel.action, 0, 0);
    if (IconButton{Glyph::chevron_right, "Advance after place: arm the next available sound shown",
                   state.resource_picker.auto_increment_on_place}.render(context)) {
        state.resource_picker.auto_increment_on_place = !state.resource_picker.auto_increment_on_place;
    }
    if (toolbar_button(context, has_primary,
            IconButton{Glyph::play, "Play the selected sound (Space on the list)"}) &&
        raise(panel, WORM_UI_COMPOSE_ACTION_AUDITION_SOUND))
        panel.action->compose_resource_id = state.keysound_catalog.keysound_selection.primary;
    if (IconButton{Glyph::audio_lines, state.dock_layout.sounds_preview_on_click
                                            ? "Preview on click: on (click to turn off)"
                                            : "Preview on click: off (click to turn on)",
                   state.dock_layout.sounds_preview_on_click}.render(context))
        state.dock_layout.sounds_preview_on_click = !state.dock_layout.sounds_preview_on_click;
    render_add_sounds_button(panel);
    if (toolbar_button(context, has_selection,
            DrawnIconButton{icons::trash,
                            "Remove the selected sounds from the list (only sounds no note uses)"}))
        raise_on_selection(panel, WORM_UI_COMPOSE_ACTION_DELETE_SOUNDS);
    if (toolbar_button(context, !state.keysound_catalog.keysound_shown.empty(),
            DrawnIconButton{icons::select_all, "Select every sound shown"}))
        worm_ui_keysound_select_all(state.keysound_catalog.keysound_selection, state.keysound_catalog.keysound_shown);
    if (toolbar_button(context, has_selection,
            IconButton{Glyph::x, "Clear the selection"}))
        worm_ui_keysound_clear(state.keysound_catalog.keysound_selection);
    render_id_sort_controls(panel);
    if (IconButton{Glyph::layers, "Layers: give the selected sounds a semantic layer, so notes "
                                  "painted from them join it", state.dock_layout.sounds_layers_open}
            .render(context))
        state.dock_layout.sounds_layers_open = !state.dock_layout.sounds_layers_open;
    if (pushed) (void)nk_style_pop_vec2(context);
}

auto group_candidates(const SoundsPanel& panel, bool ungrouped_only)
    -> std::vector<worm_ui_keysound_group_candidate_t> {
    std::vector<worm_ui_keysound_group_candidate_t> candidates;
    for (const auto id : panel.state->keysound_catalog.keysound_selection.selected) {
        const auto* row = find_row(*panel.view, id);
        if (!row || (ungrouped_only && row->sound_group_id != 0)) continue;
        candidates.push_back({.id = id, .name = row->name ? row->name : ""});
    }
    return candidates;
}

void open_manual_group_review(const SoundsPanel& panel) {
    auto& review = panel.state->keysound_catalog.keysound_group_review;
    wormedit::ui::chrome::popup_arm_release(panel.state->keysound_catalog.keysound_group_menu.release_pending);
    review = {};
    review.manual = true;
    review.open_pending = true;
    review.keep_ids_together = true;
    review.project_id = panel.view->project_id;
    review.chart_id = panel.view->chart_id;
    review.document_revision = panel.view->document_revision;
    std::snprintf(review.manual_group_name.data(), review.manual_group_name.size(), "%s",
                  "New group");
}

void open_suggested_group_review(const SoundsPanel& panel) {
    auto& review = panel.state->keysound_catalog.keysound_group_review;
    wormedit::ui::chrome::popup_arm_release(panel.state->keysound_catalog.keysound_group_menu.release_pending);
    review = {};
    review.project_id = panel.view->project_id;
    review.chart_id = panel.view->chart_id;
    review.document_revision = panel.view->document_revision;
    review.open_pending = true;
    review.keep_ids_together = true;
}

void prepare_pending_group_review(const SoundsPanel& panel) {
    auto& review = panel.state->keysound_catalog.keysound_group_review;
    if (!review.open_pending || review.preparation_complete) return;
    const auto candidates = group_candidates(panel, !review.manual);
    if (review.manual) {
        review.candidates = candidates;
    } else {
        std::vector<wormedit::sound_group_t> existing;
        existing.reserve(panel.view->compose_sound_group_count);
        for (std::size_t index = 0; index < panel.view->compose_sound_group_count; ++index) {
            const auto& group = panel.view->compose_sound_groups[index];
            existing.push_back({.id = wormedit::sound_group_id_t{group.group_id},
                                .name = group.name ? group.name : ""});
        }
        worm_ui_keysound_group_review_suggest(review, candidates, existing,
            panel.view->project_id, panel.view->chart_id, panel.view->document_revision);
        review.open = false;
        review.open_pending = true;
        review.keep_ids_together = true;
    }
    review.preparation_complete = true;
}

void render_group_actions(const SoundsPanel& panel) {
    auto* context = panel.context;
    auto& state = *panel.state;
    const auto has_selection = !state.keysound_catalog.keysound_selection.selected.empty();
    const auto any_grouped = std::ranges::any_of(state.keysound_catalog.keysound_selection.selected, [&](auto id) {
        return selected_group_id(*panel.view, id) != 0;
    });
    nk_layout_row_template_begin(context, 24);
    nk_layout_row_template_push_static(context, 80);
    nk_layout_row_template_push_static(context, 76);
    nk_layout_row_template_push_dynamic(context);
    nk_layout_row_template_end(context);
    const auto plain_button = [&](bool enabled, const char* label) {
        if (!enabled) nk_widget_disable_begin(context);
        const auto clicked = nk_button_label(context, label);
        if (!enabled) nk_widget_disable_end(context);
        return clicked && enabled;
    };
    if (plain_button(has_selection, "Group...")) open_manual_group_review(panel);
    if (plain_button(any_grouped, "Ungroup"))
        raise_on_selection(panel, WORM_UI_COMPOSE_ACTION_UNGROUP_SOUNDS);
    const auto has_ungrouped = std::ranges::any_of(state.keysound_catalog.keysound_selection.selected, [&](auto id) {
        return selected_group_id(*panel.view, id) == 0 && find_row(*panel.view, id);
    });
    if (plain_button(has_ungrouped, "Suggest groups...")) open_suggested_group_review(panel);
}

void raise_sound_renumber(const SoundsPanel& panel, bool all) {
    if (!raise(panel, WORM_UI_COMPOSE_ACTION_RENUMBER_BMS_IDS)) return;
    auto& state = *panel.state;
    state.keysound_catalog.keysound_staged_ids = all ? std::vector<std::uint64_t>{}
                                    : state.keysound_slots.keysound_renumber_resources;
    panel.action->compose_sound_resource_ids = state.keysound_catalog.keysound_staged_ids.data();
    panel.action->compose_sound_resource_count = state.keysound_catalog.keysound_staged_ids.size();
    panel.action->compose_renumber_all = all;
    panel.action->compose_renumber_bga = false;
    wormedit::ui::nuklear::close_catalog_confirmation(
        wormedit::ui::nuklear::catalog_confirmation(state, false));
}

void render_sound_renumber_confirmation(const SoundsPanel& panel) {
    const auto choice = wormedit::ui::nuklear::render_catalog_confirmation(panel.context, *panel.view,
        wormedit::ui::nuklear::catalog_confirmation(*panel.state, false), false);
    if (choice) raise_sound_renumber(panel, *choice);
}

void close_sound_slot_confirmation(const SoundsPanel& panel) {
    panel.state->keysound_slots.keysound_slot_confirmation_open = false;
    panel.state->keysound_slots.keysound_slot_confirmation_path.clear();
    panel.state->keysound_slots.keysound_slot_release_pending = true;
}

void render_sound_slot_confirmation(const SoundsPanel& panel) {
    auto& state = *panel.state;
    if (!state.keysound_slots.keysound_slot_confirmation_open) return;
    auto* context = panel.context;
    const auto bounds = nk_rect(0, 60,
        std::min(360.0f, nk_window_get_content_region(context).w), 180);
    if (!panel.view->sound_slots_mode ||
        state.keysound_slots.keysound_slot_confirmation_project != panel.view->project_id ||
        state.keysound_slots.keysound_slot_confirmation_chart != panel.view->chart_id ||
        state.keysound_slots.keysound_slot_confirmation_revision != panel.view->document_revision) {
        state.keysound_slots.keysound_slot_feedback = "The WAV slot changed; choose the file again.";
        close_sound_slot_confirmation(panel);
        wormedit::ui::chrome::dismiss_popup(context, "Replace BMS WAV slot", bounds);
        return;
    }
    if (nk_input_is_key_pressed(&context->input, NK_KEY_TEXT_RESET_MODE)) {
        wormedit::ui::chrome::dismiss_popup(context, "Replace BMS WAV slot", bounds);
        close_sound_slot_confirmation(panel);
        return;
    }
    if (!nk_popup_begin(context, NK_POPUP_STATIC, "Replace BMS WAV slot",
                        NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR, bounds)) {
        close_sound_slot_confirmation(panel);
        return;
    }
    const auto slot_label = bms_slot_label(state.keysound_slots.keysound_slot_confirmation_number);
    char message[128];
    std::snprintf(message, sizeof(message), "Replace occupied WAV slot %s with this file?",
                  slot_label.c_str());
    nk_layout_row_dynamic(context, 66, 1);
    nk_label_wrap(context, message);
    nk_layout_row_dynamic(context, 28, 2);
    if (nk_button_label(context, "Replace")) {
        raise_sound_slot_assignment(panel, state.keysound_slots.keysound_slot_confirmation_number,
                                    state.keysound_slots.keysound_slot_confirmation_path, true);
        close_sound_slot_confirmation(panel);
    }
    if (nk_button_label(context, "Cancel")) close_sound_slot_confirmation(panel);
    if (!state.keysound_slots.keysound_slot_confirmation_open) nk_popup_close(context);
    nk_popup_end(context);
}

void render_sound_slot_row(const SoundsPanel& panel, std::size_t index) {
    auto* context = panel.context;
    const auto& view = *panel.view;
    auto& state = *panel.state;
    nk_layout_row_dynamic(context, 27, 1);
    const auto hovered = nk_widget_is_hovered(context);
    const auto clicked = nk_widget_is_mouse_clicked(context, NK_BUTTON_LEFT) &&
                         !view.input_occluded && !state.keysound_slots.keysound_slot_confirmation_open &&
                         !state.keysound_slots.keysound_slot_release_pending;
    struct nk_rect bounds{};
    if (nk_widget(&bounds, context) == NK_WIDGET_INVALID ||
        index < view.sound_slot_first_row || index - view.sound_slot_first_row >= view.sound_slot_count)
        return;
    const auto& slot = view.sound_slots[index - view.sound_slot_first_row];
    const auto selected = slot.number == state.keysound_catalog.keysound_selected_slot;
    auto* canvas = nk_window_get_canvas(context);
    if (selected || hovered)
        nk_fill_rect(canvas, bounds, 2.0f, color(selected ? UiColor::selected : UiColor::surface_hover));
    char label[512];
    std::snprintf(label, sizeof(label), "%s  %s%s", slot.slot,
                  slot.resource_id ? slot.name : "(empty)",
                  slot.missing ? " · missing" : "");
    nk_draw_text(canvas, nk_rect(bounds.x + 6, bounds.y + 4,
                                 bounds.w - (slot.resource_id ? 50.0f : 12.0f), bounds.h - 8),
                 label, static_cast<int>(std::strlen(label)), context->style.font,
                 nk_rgba(0, 0, 0, 0), color(UiColor::text));
    if (slot.resource_id) {
        char uses[32];
        std::snprintf(uses, sizeof(uses), "%llu", static_cast<unsigned long long>(slot.use_count));
        nk_draw_text(canvas, nk_rect(bounds.x + bounds.w - 34, bounds.y + 4, 30, bounds.h - 8),
                     uses, static_cast<int>(std::strlen(uses)), context->style.font,
                     nk_rgba(0, 0, 0, 0), color(UiColor::text_muted));
    }
    if (!clicked) return;
    state.keysound_catalog.keysound_selected_slot = slot.number;
    state.keysound_catalog.keysound_selected_slot_explicit = true;
    state.keysound_catalog.keysound_selected_slot_resource = slot.resource_id;
    state.keysound_catalog.keysound_selected_slot_resource_known = true;
    state.keysound_slots.keysound_slot_feedback.clear();
    worm_ui_keysound_clear(state.keysound_catalog.keysound_selection);
    if (slot.resource_id && slot.placeable && !slot.missing) {
        const std::array resource{slot.resource_id};
        worm_ui_keysound_select_all(state.keysound_catalog.keysound_selection, resource);
        set_armed_resource(state, slot.resource_id, compose_resource_origin_t::sounds);
        worm_ui_nuklear_pick_draw_source(state, *panel.action, 1, slot.resource_id);
        if (state.dock_layout.sounds_preview_on_click && raise(panel, WORM_UI_COMPOSE_ACTION_AUDITION_SOUND))
            panel.action->compose_resource_id = slot.resource_id;
    } else {
        worm_ui_nuklear_pick_draw_source(state, *panel.action, 0, 0);
    }
}

void render_sound_slot_list(const SoundsPanel& panel, float height) {
    auto* context = panel.context;
    auto& state = *panel.state;
    const auto& view = *panel.view;
    if (state.keysound_catalog.keysound_reveal_primary &&
        view.sound_slot_selected_number == state.keysound_catalog.keysound_selected_slot) {
        if (view.sound_slot_selected_row != SIZE_MAX)
            nk_group_set_scroll(context, "Compose sound slots", 0,
                static_cast<nk_uint>(view.sound_slot_selected_row *
                    (27 + context->style.window.spacing.y)));
        state.keysound_catalog.keysound_reveal_primary = false;
    }
    nk_layout_row_dynamic(context, height, 1);
    struct nk_list_view list{};
    if (!nk_list_view_begin(context, &list, "Compose sound slots", NK_WINDOW_BORDER, 27,
                            static_cast<int>(std::max<std::size_t>(view.sound_slot_total_matching, 1))))
        return;
    state.keysound_catalog.keysound_slots_first_row = static_cast<std::size_t>(list.begin);
    state.keysound_catalog.keysound_slots_row_limit = static_cast<std::size_t>(std::clamp(list.end - list.begin, 1, 128));
    if (view.sound_slot_count == 0) {
        nk_layout_row_dynamic(context, 36, 1);
        nk_label(context, "No BMS WAV slots match the search.", NK_TEXT_LEFT);
    } else {
        const auto first = static_cast<std::size_t>(list.begin);
        const auto end = std::min<std::size_t>(static_cast<std::size_t>(list.end),
                                                view.sound_slot_first_row + view.sound_slot_count);
        for (auto index = first; index < end; ++index) render_sound_slot_row(panel, index);
    }
    nk_list_view_end(&list);
}

void render_sound_slot_search(const SoundsPanel& panel) {
    auto& state = *panel.state;
    const auto before = state.keysound_catalog.keysound_slots_search;
    nk_layout_row_dynamic(panel.context, 25, 1);
    (void)wormedit::ui::chrome::worm_ui_nuklear_text_field(
        panel.context, NK_EDIT_FIELD, state.keysound_catalog.keysound_slots_search.data(),
        static_cast<int>(state.keysound_catalog.keysound_slots_search.size()), nk_filter_default);
    if (before != state.keysound_catalog.keysound_slots_search) {
        state.keysound_catalog.keysound_slots_first_row = 0;
        nk_group_set_scroll(panel.context, "Compose sound slots", 0, 0);
    }
}

void render_search(const SoundsPanel& panel) {
    auto* context = panel.context;
    nk_layout_row_dynamic(context, 25, 1);
    (void)wormedit::ui::chrome::worm_ui_nuklear_text_field(
        context, NK_EDIT_FIELD, panel.state->keysound_catalog.keysound_search.data(),
        static_cast<int>(panel.state->keysound_catalog.keysound_search.size()), nk_filter_default);
    if (panel.state->keysound_catalog.keysound_search != panel.state->keysound_filters.keysound_filters.search) {
        panel.state->keysound_filters.keysound_auto_first_row = 0;
        panel.state->keysound_filters.keysound_filters.search = panel.state->keysound_catalog.keysound_search;
        nk_group_set_scroll(context, "Compose sound list", 0, 0);
    }
}

void reset_automatic_window(const SoundsPanel& panel) {
    panel.state->keysound_filters.keysound_auto_first_row = 0;
    nk_group_set_scroll(panel.context, "Compose sound list", 0, 0);
}

auto filter_popup_dismiss_requested(nk_context* context, bool was_open) -> bool {
    return wormedit::ui::chrome::popup_dismiss_requested(
        context, nk_window_get_bounds(context), was_open);
}

void start_group_filter_from_all(const SoundsPanel& panel) {
    auto& filters = panel.state->keysound_filters.keysound_filters;
    filters.group_ids.clear();
    for (std::size_t index = 0; index < panel.view->compose_sound_group_count; ++index)
        filters.group_ids.push_back(panel.view->compose_sound_groups[index].group_id);
    filters.include_ungrouped = true;
    filters.group_filter_active = true;
}

auto close_filter_popup_if_requested(const SoundsPanel& panel, bool group_popup,
                                     bool was_open) -> bool {
    auto& state = *panel.state;
    auto* context = panel.context;
    if (!filter_popup_dismiss_requested(context, was_open)) return false;
    nk_combo_close(context);
    wormedit::ui::chrome::safe_combo_end(context);
    if (group_popup) state.keysound_filters.keysound_filter_group_popup_open = false;
    else state.keysound_filters.keysound_layer_filter_popup_open = false;
    wormedit::ui::chrome::popup_arm_release(state.keysound_filters.keysound_filter_release_pending);
    return true;
}

void render_group_filter_items(const SoundsPanel& panel) {
    auto& filters = panel.state->keysound_filters.keysound_filters;
    auto* context = panel.context;
    nk_layout_row_dynamic(context, 22, 1);
    for (std::size_t index = 0; index < panel.view->compose_sound_group_count; ++index) {
        const auto& group = panel.view->compose_sound_groups[index];
        const auto selected = !filters.group_filter_active ||
            std::ranges::find(filters.group_ids, group.group_id) != filters.group_ids.end();
        const auto checked = nk_check_label(context, group.name, selected);
        if (checked == selected) continue;
        if (!filters.group_filter_active) start_group_filter_from_all(panel);
        if (checked) filters.group_ids.push_back(group.group_id);
        else std::erase(filters.group_ids, group.group_id);
        wormedit::ui::chrome::popup_arm_release(panel.state->keysound_filters.keysound_filter_release_pending);
        reset_automatic_window(panel);
    }
    const auto ungrouped = !filters.group_filter_active || filters.include_ungrouped;
    const auto checked_ungrouped = nk_check_label(context, "Ungrouped", ungrouped);
    if (checked_ungrouped != ungrouped) {
        if (!filters.group_filter_active) start_group_filter_from_all(panel);
        filters.include_ungrouped = checked_ungrouped;
        wormedit::ui::chrome::popup_arm_release(panel.state->keysound_filters.keysound_filter_release_pending);
        reset_automatic_window(panel);
    }
}

auto render_layer_filter_items(const SoundsPanel& panel) -> bool {
    auto& filters = panel.state->keysound_filters.keysound_filters;
    auto* context = panel.context;
    nk_layout_row_dynamic(context, 22, 1);
    if (panel.state->keysound_filters.keysound_filter_layer_first > 0 &&
        nk_combo_item_label(context, "Previous layers...", NK_TEXT_LEFT)) {
        panel.state->keysound_filters.keysound_filter_layer_first =
            panel.state->keysound_filters.keysound_filter_layer_first > 64
                ? panel.state->keysound_filters.keysound_filter_layer_first - 64 : 0;
        return true;
    }
    if (nk_combo_item_label(context, "Any layer", NK_TEXT_LEFT)) {
        filters.layer_filter_kind = WORM_UI_KEYSOUND_LAYER_FILTER_ANY;
        filters.layer_filter_id = 0;
        reset_automatic_window(panel);
        return true;
    }
    if (nk_combo_item_label(context, "No layer", NK_TEXT_LEFT)) {
        filters.layer_filter_kind = WORM_UI_KEYSOUND_LAYER_FILTER_NONE;
        filters.layer_filter_id = 0;
        reset_automatic_window(panel);
        return true;
    }
    for (std::size_t index = 0; index < panel.view->keysound_filter_layer_count; ++index) {
        const auto& layer = panel.view->keysound_filter_layers[index];
        if (!nk_combo_item_label(context, layer.name, NK_TEXT_LEFT)) continue;
        filters.layer_filter_kind = WORM_UI_KEYSOUND_LAYER_FILTER_SPECIFIC;
        filters.layer_filter_id = layer.layer_id;
        reset_automatic_window(panel);
        return true;
    }
    if (panel.view->keysound_filter_layer_first + panel.view->keysound_filter_layer_count <
            panel.view->keysound_filter_layer_total &&
        nk_combo_item_label(context, "More layers...", NK_TEXT_LEFT)) {
        panel.state->keysound_filters.keysound_filter_layer_first = panel.view->keysound_filter_layer_first +
            panel.view->keysound_filter_layer_count;
        return true;
    }
    return false;
}

void render_group_filter_choices(const SoundsPanel& panel) {
    auto& filters = panel.state->keysound_filters.keysound_filters;
    auto& state = *panel.state;
    auto* context = panel.context;
    const auto was_open = state.keysound_filters.keysound_filter_group_popup_open;
    const char* label = filters.group_filter_active ? "Groups: custom" : "Groups: all";
    const auto began = (!state.keysound_filters.keysound_filter_release_pending || was_open) &&
        wormedit::ui::chrome::safe_combo_begin_label(
            context, label, nk_vec2(nk_widget_width(context), 240));
    state.keysound_filters.keysound_filter_group_popup_open = began;
    if (!began && was_open &&
        (nk_input_is_mouse_pressed(&context->input, NK_BUTTON_LEFT) ||
         nk_input_is_mouse_pressed(&context->input, NK_BUTTON_RIGHT)))
        wormedit::ui::chrome::popup_arm_release(state.keysound_filters.keysound_filter_release_pending);
    if (began) {
        if (close_filter_popup_if_requested(panel, true, was_open)) return;
        render_group_filter_items(panel);
        wormedit::ui::chrome::safe_combo_end(context);
    }
}

void render_layer_filter_choice(const SoundsPanel& panel) {
    auto& filters = panel.state->keysound_filters.keysound_filters;
    auto& state = *panel.state;
    auto* context = panel.context;
    const auto was_open = state.keysound_filters.keysound_layer_filter_popup_open;
    const char* layer_label = "Layer: any";
    if (filters.layer_filter_kind == WORM_UI_KEYSOUND_LAYER_FILTER_NONE)
        layer_label = "Layer: none";
    else if (filters.layer_filter_kind == WORM_UI_KEYSOUND_LAYER_FILTER_SPECIFIC &&
             panel.view->keysound_filter_selected_layer_name)
        layer_label = panel.view->keysound_filter_selected_layer_name;
    const auto began = !state.keysound_filters.keysound_filter_release_pending &&
        !state.keysound_filters.keysound_filter_group_popup_open &&
        wormedit::ui::chrome::safe_combo_begin_label(
            context, layer_label, nk_vec2(nk_widget_width(context), 240));
    state.keysound_filters.keysound_layer_filter_popup_open = began;
    if (!began && was_open &&
        (nk_input_is_mouse_pressed(&context->input, NK_BUTTON_LEFT) ||
         nk_input_is_mouse_pressed(&context->input, NK_BUTTON_RIGHT)))
        wormedit::ui::chrome::popup_arm_release(state.keysound_filters.keysound_filter_release_pending);
    if (began) {
        if (close_filter_popup_if_requested(panel, false, was_open)) return;
        if (render_layer_filter_items(panel)) {
            nk_combo_close(context);
            wormedit::ui::chrome::safe_combo_end(context);
            state.keysound_filters.keysound_layer_filter_popup_open = false;
            wormedit::ui::chrome::popup_arm_release(state.keysound_filters.keysound_filter_release_pending);
            return;
        }
        wormedit::ui::chrome::safe_combo_end(context);
    }
}

void clear_sound_filters(const SoundsPanel& panel) {
    auto& filters = panel.state->keysound_filters.keysound_filters;
    auto* context = panel.context;
    nk_layout_row_dynamic(context, 23, 1);
    const auto clear_enabled = filters.group_filter_active ||
        filters.layer_filter_kind != WORM_UI_KEYSOUND_LAYER_FILTER_ANY ||
        panel.state->keysound_catalog.keysound_search[0] != '\0' || filters.use_filter != 0;
    if (!clear_enabled) nk_widget_disable_begin(context);
    if (nk_button_label(context, "Clear filters") && clear_enabled) {
        panel.state->keysound_catalog.keysound_search.fill(0);
        filters.search.fill(0);
        filters.use_filter = 0;
        filters.group_filter_active = false;
        filters.group_ids.clear();
        filters.include_ungrouped = true;
        filters.layer_filter_kind = WORM_UI_KEYSOUND_LAYER_FILTER_ANY;
        filters.layer_filter_id = 0;
        reset_automatic_window(panel);
    }
    if (!clear_enabled) nk_widget_disable_end(context);
}

void render_use_filter(const SoundsPanel& panel) {
    auto* context = panel.context;
    auto& state = *panel.state;
    const bool blocked = state.keysound_filters.is_popup_open() || state.keysound_filters.keysound_filter_release_pending ||
        state.keysound_slots.keysound_renumber_confirmation_open || state.keysound_slots.keysound_renumber_release_pending ||
        state.keysound_slots.keysound_slot_confirmation_open || state.keysound_slots.keysound_slot_release_pending || panel.view->input_occluded;
    const auto chosen = wormedit::ui::nuklear::render_catalog_use_filter(context,
        state.keysound_filters.keysound_filters.use_filter, blocked);
    if (chosen) {
        state.keysound_filters.keysound_filters.use_filter = *chosen;
        reset_automatic_window(panel);
    }
}

void render_group_filter(const SoundsPanel& panel) {
    nk_layout_row_dynamic(panel.context, 24, 2);
    render_group_filter_choices(panel);
    render_layer_filter_choice(panel);
    clear_sound_filters(panel);
}

// ---- Layer strip ----

void draw_tri_state(struct nk_command_buffer* canvas, struct nk_rect box, worm_ui_keysound_layer_state_t state,
                    nk_color tint) {
    nk_stroke_rect(canvas, box, 2.0f, 1.0f, tint);
    if (state == worm_ui_keysound_layer_state_t::all)
        nk_fill_rect(canvas, nk_rect(box.x + 3, box.y + 3, box.w - 6, box.h - 6), 1.0f, tint);
    else if (state == worm_ui_keysound_layer_state_t::some)
        nk_fill_rect(canvas, nk_rect(box.x + 3, box.y + box.h * 0.5f - 1.0f, box.w - 6, 2.0f),
                     0.0f, tint);
}

void assign_sound_layer(const SoundsPanel& panel, std::size_t index) {
    const auto& layer = panel.view->keysound_layers[index];
    const auto mask = std::uint64_t{1} << index;
    const auto conflicts = std::ranges::any_of(panel.state->keysound_catalog.keysound_selection.selected,
        [&](auto id) {
            return (selected_conflict_mask(*panel.view, id) & mask) != 0;
        });
    if (!conflicts) {
        raise_on_selection(panel, WORM_UI_COMPOSE_ACTION_ADD_SOUND_LAYER, layer.layer_id);
        return;
    }
    auto& state = *panel.state;
    state.keysound_slots.sound_layer_confirmation_open = true;
    state.keysound_slots.sound_layer_confirmation_project = panel.view->project_id;
    state.keysound_slots.sound_layer_confirmation_chart = panel.view->chart_id;
    state.keysound_slots.sound_layer_confirmation_revision = panel.view->document_revision;
    state.keysound_slots.sound_layer_confirmation_layer = layer.layer_id;
    state.keysound_slots.sound_layer_confirmation_resources = state.keysound_catalog.keysound_selection.selected;
}

void confirm_sound_layer(const SoundsPanel& panel, bool overwrite) {
    if (!raise(panel, WORM_UI_COMPOSE_ACTION_ADD_SOUND_LAYER)) return;
    auto& state = *panel.state;
    state.keysound_catalog.keysound_staged_ids = state.keysound_slots.sound_layer_confirmation_resources;
    panel.action->compose_sound_resource_ids = state.keysound_catalog.keysound_staged_ids.data();
    panel.action->compose_sound_resource_count = state.keysound_catalog.keysound_staged_ids.size();
    panel.action->compose_semantic_layer_id = state.keysound_slots.sound_layer_confirmation_layer;
    panel.action->compose_sound_overwrite_note_layers = overwrite;
    state.keysound_slots.sound_layer_confirmation_open = false;
}

void render_sound_layer_confirmation(const SoundsPanel& panel) {
    auto& state = *panel.state;
    if (!state.keysound_slots.sound_layer_confirmation_open) return;
    const auto& view = *panel.view;
    auto* context = panel.context;
    const auto region = nk_window_get_content_region(context);
    const auto bounds = nk_rect(0, 60, std::min(360.0f, region.w), 204);
    if (state.keysound_slots.sound_layer_confirmation_project != view.project_id ||
        state.keysound_slots.sound_layer_confirmation_chart != view.chart_id ||
        state.keysound_slots.sound_layer_confirmation_revision != view.document_revision) {
        state.keysound_slots.sound_layer_confirmation_open = false;
        wormedit::ui::chrome::dismiss_popup(context, "Sound note layers", bounds);
        return;
    }
    if (!nk_popup_begin(context, NK_POPUP_STATIC, "Sound note layers",
                        NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR, bounds)) {
        state.keysound_slots.sound_layer_confirmation_open = false;
        return;
    }
    nk_layout_row_dynamic(context, 66, 1);
    nk_label_wrap(context, "Some notes using these sounds have different layers. Keep their "
                           "layers, or replace them with the sounds' layers?");
    nk_layout_row_dynamic(context, 28, 1);
    if (nk_button_label(context, "Keep existing layers")) confirm_sound_layer(panel, false);
    if (nk_button_label(context, "Overwrite")) confirm_sound_layer(panel, true);
    if (nk_button_label(context, "Cancel")) state.keysound_slots.sound_layer_confirmation_open = false;
    if (!state.keysound_slots.sound_layer_confirmation_open) nk_popup_close(context);
    nk_popup_end(context);
}

void render_layer_row(const SoundsPanel& panel, std::size_t index) {
    auto* context = panel.context;
    const auto& layer = panel.view->keysound_layers[index];
    const auto layer_state = worm_ui_keysound_layer_state(
        panel.state->keysound_catalog.keysound_selection, panel.state->keysound_catalog.keysound_masks,
        static_cast<unsigned>(index));
    const auto has_selection = !panel.state->keysound_catalog.keysound_selection.selected.empty();
    const auto adding = layer_state != worm_ui_keysound_layer_state_t::all;
    const auto enabled = has_selection && !(adding && layer.locked);
    nk_layout_row_dynamic(context, layer_row_height, 1);
    const auto hovered = nk_widget_is_hovered(context);
    const auto clicked = nk_widget_is_mouse_clicked(context, NK_BUTTON_LEFT);
    struct nk_rect bounds{};
    if (nk_widget(&bounds, context) == NK_WIDGET_INVALID) return;
    auto* canvas = nk_window_get_canvas(context);
    if (hovered && enabled) nk_fill_rect(canvas, bounds, 3.0f, color(UiColor::surface_hover));
    const auto tint = enabled ? color(UiColor::text) : color(UiColor::text_muted);
    draw_tri_state(canvas, nk_rect(bounds.x + 4, bounds.y + 4, 14, 14), layer_state, tint);
    nk_fill_circle(canvas, nk_rect(bounds.x + 24, bounds.y + 7, 8, 8),
                   nk_rgba(layer.red, layer.green, layer.blue, 255));
    std::string label = layer.name;
    if (layer.locked) label += " (locked)";
    nk_draw_text(canvas, nk_rect(bounds.x + 38, bounds.y + 2, bounds.w - 42, bounds.h - 4),
                 label.c_str(), static_cast<int>(label.size()), context->style.font,
                 nk_rgba(0, 0, 0, 0), tint);
    if (hovered)
        wormedit::ui::nuklear::tooltip::text(context, !has_selection ? "Select sounds first"
                            : layer.locked && adding ? "Locked layers cannot gain sounds"
                            : adding ? "Add this layer to the selected sounds"
                                     : "Remove this layer from the selected sounds");
    if (clicked && enabled) {
        if (adding) assign_sound_layer(panel, index);
        else raise_on_selection(panel, WORM_UI_COMPOSE_ACTION_REMOVE_SOUND_LAYER, layer.layer_id);
    }
}

void render_layer_section(const SoundsPanel& panel) {
    auto* context = panel.context;
    const auto& view = *panel.view;
    if (view.keysound_layer_count == 0) {
        nk_layout_row_dynamic(context, 34, 1);
        nk_label_wrap(context, "No layers yet. Create them in the Semantic layers section of "
                               "the inspector.");
        return;
    }
    const auto visible = std::min<std::size_t>(view.keysound_layer_count, 5);
    const auto height = static_cast<float>(visible) * (layer_row_height +
                        context->style.window.spacing.y) + 6.0f;
    nk_layout_row_dynamic(context, height, 1);
    if (nk_group_begin(context, "Sound layers", NK_WINDOW_BORDER)) {
        for (std::size_t index = 0; index < view.keysound_layer_count; ++index)
            render_layer_row(panel, index);
        nk_group_end(context);
    }
    nk_layout_row_dynamic(context, 24, 1);
    const auto can_clear = !panel.state->keysound_catalog.keysound_selection.selected.empty();
    if (!can_clear) nk_widget_disable_begin(context);
    const auto clear = nk_button_label(context, "Clear layers of selected sounds");
    if (!can_clear) nk_widget_disable_end(context);
    if (clear && can_clear)
        raise_on_selection(panel, WORM_UI_COMPOSE_ACTION_CLEAR_SOUND_LAYERS);
}

// ---- List ----

auto modifier_state() -> std::pair<bool, bool> {
    const auto modifiers = SDL_GetModState();
    return {(modifiers & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0, (modifiers & SDL_KMOD_SHIFT) != 0};
}

void draw_row(const SoundsPanel& panel, struct nk_rect bounds, const WormUiComposeKeysoundRowView& row,
              bool selected, bool hovered) {
    auto* context = panel.context;
    auto* canvas = nk_window_get_canvas(context);
    const auto armed = panel.state->keysound_catalog.armed_from_sounds_dock && !row.missing &&
                       panel.state->resource_picker.armed_resource_id == row.resource_id;
    if (selected) nk_fill_rect(canvas, bounds, 3.0f, color(UiColor::selected));
    else if (hovered) nk_fill_rect(canvas, bounds, 3.0f, color(UiColor::surface_hover));
    if (armed) {
        nk_stroke_rect(canvas, bounds, 3.0f, 2.0f, color(UiColor::accent));
        nk_fill_rect(canvas, nk_rect(bounds.x, bounds.y + 3, 3, bounds.h - 6), 1,
                     color(UiColor::accent));
    }
    const auto text_color = row.missing ? color(UiColor::text_muted)
                            : selected  ? color(UiColor::selected_text) : color(UiColor::text);
    const auto clear = nk_rgba(0, 0, 0, 0);
    const auto slot = panel.view->keysound_ids_available ? row.slot : "";
    const auto slot_width = panel.view->keysound_ids_available ? 30.0f : 0.0f;
    const auto slot_len = static_cast<int>(std::strlen(slot));
    nk_draw_text(canvas, nk_rect(bounds.x + 6, bounds.y + 3, slot_width, bounds.h - 6), slot,
                 slot_len, context->style.font, clear, color(UiColor::text_muted));
    const auto chips = static_cast<float>(
        std::min<int>(std::popcount(row.layer_mask), 3)) * (chip_size + 2.0f);
    const bool typed_armed = armed && panel.view->bmson_tools_available;
    const float status_width = typed_armed ? 90.0f : 48.0f;
    const float status_extra = status_width - 48.0f;
    const auto name_width = bounds.w - slot_width - 110.0f - chips - status_extra;
    nk_draw_text(canvas, nk_rect(bounds.x + 12 + slot_width, bounds.y + 3, std::max(name_width, 8.0f),
                                 bounds.h - 6),
                 row.name, static_cast<int>(std::strlen(row.name)), context->style.font, clear,
                 text_color);
    char uses[24];
    std::snprintf(uses, sizeof(uses), "%llu", static_cast<unsigned long long>(row.use_count));
    nk_draw_text(canvas, nk_rect(bounds.x + bounds.w - 86 - status_extra, bounds.y + 3, 32, bounds.h - 6),
                 uses, static_cast<int>(std::strlen(uses)), context->style.font, clear, text_color);
    const auto* status = row.missing ? "missing"
        : typed_armed ? (row.stem_source_lane_id ? "armed stem" : "armed note")
        : armed ? "armed" : row.use_count ? "ok" : "unused";
    nk_draw_text(canvas, nk_rect(bounds.x + bounds.w - status_width, bounds.y + 3, status_width, bounds.h - 6),
                 status, static_cast<int>(std::strlen(status)), context->style.font, clear, text_color);
    float chip_x = bounds.x + bounds.w - 94.0f - chips - status_extra;
    int drawn = 0;
    for (std::size_t bit = 0; bit < panel.view->keysound_layer_count && drawn < 3; ++bit) {
        if (((row.layer_mask >> bit) & 1U) == 0) continue;
        const auto& layer = panel.view->keysound_layers[bit];
        nk_fill_circle(canvas, nk_rect(chip_x, bounds.y + (bounds.h - chip_size) * 0.5f,
                                       chip_size, chip_size),
                       nk_rgba(layer.red, layer.green, layer.blue, 255));
        chip_x += chip_size + 2.0f;
        ++drawn;
    }
}

auto find_fallback(const WormUiComposeView& view, std::uint64_t resource)
    -> const WormUiKeysoundFallbackView* {
    for (std::size_t index = 0; index < view.keysound_fallback_count; ++index)
        if (view.keysound_fallbacks[index].resource_id == resource) return &view.keysound_fallbacks[index];
    return nullptr;
}

// "File missing. Found nearby: x.ogg" or the ambiguous list; the context menu relinks.
auto fallback_text(const WormUiComposeView& view, const WormUiComposeKeysoundRowView& row)
    -> std::string {
    const auto* fallback = find_fallback(view, row.resource_id);
    if (!fallback) return {};
    std::string text = fallback->outcome == 1 ? "Found nearby: " : "Several files match, right-click to choose: ";
    for (std::size_t index = 0; index < fallback->candidate_count; ++index) {
        if (index) text += ", ";
        text += fallback->candidates[index];
    }
    return text + ". ";
}

auto layer_tooltip(const SoundsPanel& panel, const WormUiComposeKeysoundRowView& row)
    -> std::string {
    std::string text = row.source_path ? row.source_path : "";
    text += "\n" + std::to_string(row.use_count) + " stored references. ";
    text += row.missing ? "File missing; cannot arm or audition. "
        : row.stem_source_lane_id ? "Click to arm this stem. " : "Double-click to arm. ";
    if (panel.view->bmson_tools_available)
        text += row.stem_source_lane_id ? "Stem: draws linked continuation/restart points. "
                                       : "Packed keysound: draws notes using this sound. ";
    text += fallback_text(*panel.view, row);
    std::string layers;
    for (std::size_t bit = 0; bit < panel.view->keysound_layer_count; ++bit) {
        if (((row.layer_mask >> bit) & 1U) == 0) continue;
        if (!layers.empty()) layers += ", ";
        layers += panel.view->keysound_layers[bit].name;
    }
    if (!layers.empty()) text += "Layers: " + layers;
    return text;
}

void click_row(const SoundsPanel& panel, std::size_t shown_index, std::uint64_t id) {
    auto& state = *panel.state;
    state.input_queue.keyboard_focus = false;
    wormedit::ui::chrome::context_menu_focus(wormedit::ui::chrome::KeyboardContextTarget::sounds, id);
    const auto [ctrl, shift] = modifier_state();
    worm_ui_keysound_click(state.keysound_catalog.keysound_selection, state.keysound_catalog.keysound_shown, shown_index, ctrl,
                           shift);
    const auto* primary = find_row(*panel.view, state.keysound_catalog.keysound_selection.primary);
    if (state.dock_layout.sounds_preview_on_click && !ctrl && !shift && primary && !primary->missing &&
        raise(panel, WORM_UI_COMPOSE_ACTION_AUDITION_SOUND))
        panel.action->compose_resource_id = id;
    const bool double_click = nk_input_is_mouse_released(&panel.context->input, NK_BUTTON_DOUBLE);
    if (!primary || primary->missing || ctrl || shift) return;
    if (!double_click && !primary->stem_source_lane_id) return;
    set_armed_resource(state, primary->resource_id, compose_resource_origin_t::sounds);
    worm_ui_nuklear_pick_draw_source(state, *panel.action, 1, state.keysound_catalog.keysound_selection.primary);
}

bool keysound_row_input_available(const SoundsPanel& panel) {
    return !panel.view->input_occluded &&
        !wormedit::ui::chrome::context_menu_owns_input(panel.state->keysound_catalog.keysound_group_menu) &&
        !panel.state->keysound_filters.is_popup_open() && !panel.state->keysound_filters.keysound_filter_release_pending &&
        !panel.state->keysound_slots.keysound_renumber_confirmation_open && !panel.state->keysound_slots.keysound_renumber_release_pending &&
        !panel.state->keysound_slots.keysound_slot_confirmation_open && !panel.state->keysound_slots.keysound_slot_release_pending &&
        !panel.state->keysound_catalog.keysound_group_review.open && !panel.state->keysound_slots.sound_layer_confirmation_open;
}

void render_row_actions(const SoundsPanel& panel, const WormUiComposeKeysoundRowView& row) {
    auto* context = panel.context;
    const bool available = keysound_row_input_available(panel);
    nk_layout_row_push(context, 22);
    if (toolbar_button(context, available && !row.missing,
            IconButton{Glyph::play, available ? "Audition this sound" : nullptr}) &&
        raise(panel, WORM_UI_COMPOSE_ACTION_AUDITION_SOUND))
        panel.action->compose_resource_id = row.resource_id;
    nk_layout_row_push(context, 22);
    const auto tip = row.use_count == 0 ? std::string{"Delete unused resource; keep its file"}
        : "Cannot delete: " + std::to_string(row.use_count) + " stored references";
    if (toolbar_button(context, available && row.use_count == 0,
            DrawnIconButton{icons::trash, available ? tip.c_str() : nullptr}) &&
        raise(panel, WORM_UI_COMPOSE_ACTION_DELETE_SOUNDS)) {
        panel.state->keysound_catalog.keysound_staged_ids = {row.resource_id};
        panel.action->compose_sound_resource_ids = panel.state->keysound_catalog.keysound_staged_ids.data();
        panel.action->compose_sound_resource_count = 1;
    }
}

void render_row(const SoundsPanel& panel, std::size_t shown_index) {
    auto* context = panel.context;
    const auto id = panel.state->keysound_catalog.keysound_shown[shown_index];
    const auto* row = find_row(*panel.view, id);
    if (!row) { nk_layout_row_dynamic(context, row_height, 1); nk_spacing(context, 1); return; }
    const auto width = nk_window_get_content_region(context).w;
    nk_layout_row_begin(context, NK_STATIC, row_height, 3);
    nk_layout_row_push(context, std::max(32.0f, width - 44.0f - 2 * context->style.window.spacing.x));
    const auto hovered = nk_widget_is_hovered(context);
    const auto clicked = nk_widget_is_mouse_clicked(context, NK_BUTTON_LEFT) &&
                         keysound_row_input_available(panel);
    struct nk_rect bounds{};
    if (nk_widget(&bounds, context) == NK_WIDGET_INVALID) return;
    draw_row(panel, bounds, *row, worm_ui_keysound_is_selected(panel.state->keysound_catalog.keysound_selection, id),
             hovered);
    if (hovered && keysound_row_input_available(panel)) {
        const auto tip = layer_tooltip(panel, *row);
        if (!tip.empty()) wormedit::ui::nuklear::tooltip::text(context, tip);
    }
    if (clicked) click_row(panel, shown_index, id);
    if (row->stem_source_lane_id && panel.view->note_paint_mode_active &&
        !panel.view->timing_focus && keysound_row_input_available(panel) &&
        nk_input_is_mouse_pressed(&context->input, NK_BUTTON_LEFT) &&
        nk_input_has_mouse_click_in_rect(&context->input, NK_BUTTON_LEFT, bounds)) {
        const auto& view = *panel.view;
        panel.state->bmson_input.bmson_point_capture = worm_ui_nuklear_compose_state_t::bmson_point_capture_t{
            view.project_id, view.chart_id, view.document_revision, row->stem_source_lane_id,
            row->resource_id, 0, 0, true, false, true};
    }
    const bool keyboard_open = id == panel.state->keysound_catalog.keysound_selection.primary &&
        keysound_row_input_available(panel) &&
        wormedit::ui::chrome::context_menu_take_open_request(wormedit::ui::chrome::KeyboardContextTarget::sounds);
    const bool pointer_open = nk_input_is_mouse_pressed(&context->input, NK_BUTTON_RIGHT) &&
        nk_input_has_mouse_click_in_rect(&context->input, NK_BUTTON_RIGHT, bounds);
    if (!wormedit::ui::chrome::context_menu_owns_input(panel.state->keysound_catalog.keysound_group_menu) &&
        !panel.view->input_occluded && (keyboard_open || pointer_open)) {
        auto& state = *panel.state;
        if (!worm_ui_keysound_is_selected(state.keysound_catalog.keysound_selection, id)) {
            worm_ui_keysound_clear(state.keysound_catalog.keysound_selection);
            state.keysound_catalog.keysound_selection.selected.push_back(id);
            state.keysound_catalog.keysound_selection.anchor = id;
            state.keysound_catalog.keysound_selection.primary = id;
        }
        wormedit::ui::chrome::context_menu_open(
            state.keysound_catalog.keysound_group_menu, keyboard_open ? nk_vec2(bounds.x, bounds.y + bounds.h) : context->input.mouse.pos,
            {panel.view->project_id, panel.view->chart_id, panel.view->document_revision, id}, keyboard_open);
        wormedit::ui::chrome::popup_arm_release(state.keysound_catalog.keysound_group_menu.release_pending);
    }
    render_row_actions(panel, *row);
    nk_layout_row_end(context);
}

void render_list_hint(const SoundsPanel& panel) {
    auto* context = panel.context;
    const auto& view = *panel.view;
    nk_layout_row_dynamic(context, 52, 1);
    if (view.keysound_total_count == 0)
        nk_label_wrap(context, "No sounds yet. Drop audio files here, or use the add button.");
    else
        nk_label_wrap(context, "No sound matches the search.");
}

void toggle_collapsed_group(const SoundsPanel& panel, std::uint64_t group_id) {
    auto& state = *panel.state;
    if (group_id == 0) state.keysound_filters.keysound_filters.ungrouped_collapsed =
        !state.keysound_filters.keysound_filters.ungrouped_collapsed;
    else {
        auto& collapsed = state.keysound_filters.keysound_filters.collapsed_group_ids;
        if (std::ranges::find(collapsed, group_id) == collapsed.end()) collapsed.push_back(group_id);
        else std::erase(collapsed, group_id);
    }
    state.keysound_filters.keysound_filter_states_by_project[panel.view->project_id] = state.keysound_filters.keysound_filters;
}

void render_group_header(const SoundsPanel& panel,
                         const WormUiComposeKeysoundDisplayRowView& row) {
    auto* context = panel.context;
    nk_layout_row_dynamic(context, row_height, 1);
    char label[192];
    std::snprintf(label, sizeof(label), "%s %s (%zu)", row.expanded ? "v" : ">",
                  row.group_name ? row.group_name : "Ungrouped", row.group_sound_count);
    if (nk_button_label(context, label)) toggle_collapsed_group(panel, row.group_id);
}

void render_display_row(const SoundsPanel& panel,
                        const WormUiComposeKeysoundDisplayRowView& display) {
    if (display.kind == WORM_UI_COMPOSE_KEYSOUND_GROUP_HEADER) {
        render_group_header(panel, display);
        return;
    }
    const auto shown = std::ranges::find(panel.state->keysound_catalog.keysound_shown, display.resource_id);
    if (shown == panel.state->keysound_catalog.keysound_shown.end()) {
        nk_layout_row_dynamic(panel.context, row_height, 1);
        nk_spacing(panel.context, 1);
        return;
    }
    render_row(panel, static_cast<std::size_t>(shown - panel.state->keysound_catalog.keysound_shown.begin()));
}

void render_sound_group_context_menu(const SoundsPanel& panel) {
    namespace chrome = wormedit::ui::chrome;
    auto* context = panel.context;
    auto& state = *panel.state;
    auto& menu = state.keysound_catalog.keysound_group_menu;
    // A review opens only once the gesture that requested it has been released.
    if (menu.release_pending && !menu.open) return;
    if (state.keysound_catalog.keysound_group_review.open_pending) {
        state.keysound_catalog.keysound_group_review.open_pending = false;
        state.keysound_catalog.keysound_group_review.open = true;
    }
    if (!menu.open) return;
    if (!find_row(*panel.view, menu.stamp.object_id)) {
        chrome::context_menu_close(menu, context);
        return;
    }
    const auto current = wormedit::ui::chrome::PopupContextStamp::from(*panel.view, menu.stamp.object_id);
    const auto* menu_row = find_row(*panel.view, menu.stamp.object_id);
    const auto* fallback = menu_row ? find_fallback(*panel.view, menu_row->resource_id) : nullptr;
    const std::size_t candidates = fallback ? fallback->candidate_count : 0;
    if (!chrome::context_menu_begin(menu, context, current,
                                    nk_vec2(chrome::context_menu_width,
                                            chrome::context_menu_height(4 + candidates))))
        return;
    for (std::size_t index = 0; fallback && index < fallback->candidate_count; ++index) {
        const auto label = std::string{"Use "} + fallback->candidates[index];
        if (!chrome::context_menu_item(menu, context, label.c_str())) continue;
        auto& action = *panel.action;
        if (action.compose_action != WORM_UI_COMPOSE_ACTION_NONE) break;
        action.compose_action = WORM_UI_COMPOSE_ACTION_RELINK_SOUND_FALLBACK;
        action.compose_input_project_id = panel.view->project_id;
        action.compose_resource_id = menu.stamp.object_id;
        action.compose_fallback_candidate = static_cast<std::uint32_t>(index);
        action.compose_input_chart_id = panel.view->chart_id;
        action.compose_input_document_revision = panel.view->document_revision;
    }
    if (chrome::context_menu_item(menu, context, "Select same sound (whole chart)")) {
        append_compose_input(panel.action, WORM_UI_COMPOSE_INPUT_SELECT_SAME_SOUND_CHART,
            0, 0, 0, false, WORM_UI_COMPOSE_SELECTION_ADD);
        if (panel.action->compose_input_count)
            panel.action->compose_inputs[panel.action->compose_input_count - 1].resource_id = menu.stamp.object_id;
    } else if (chrome::context_menu_item(menu, context, "Group..."))
        open_manual_group_review(panel);
    else if (chrome::context_menu_item(menu, context, "Ungroup"))
        raise_on_selection(panel, WORM_UI_COMPOSE_ACTION_UNGROUP_SOUNDS);
    else if (chrome::context_menu_item(menu, context, "Suggest groups..."))
        open_suggested_group_review(panel);
    chrome::context_menu_end(menu, context);
}

auto chip_sound_reveal_ready(const SoundsPanel& panel) -> bool {
    const auto& state = *panel.state;
    const auto& view = *panel.view;
    return state.pointer_capture.source_chip_reveal_kind != 1 ||
        (view.keysound_projected_primary_resource_id == state.pointer_capture.source_chip_reveal_id &&
         view.keysound_projected_filters_unrestricted && view.keysound_primary_display_index_valid);
}

void restore_chip_sound_selection(const SoundsPanel& panel) {
    auto& state = *panel.state;
    const auto& view = *panel.view;
    if (state.pointer_capture.source_chip_reveal_kind != 1) return;
    if (state.pointer_capture.chip_reveal_project != view.project_id || state.pointer_capture.chip_reveal_chart != view.chart_id ||
        view.draw_source_kind != 1 || view.draw_source_id != state.pointer_capture.source_chip_reveal_id || view.draw_source_missing) {
        state.pointer_capture.source_chip_reveal_kind = 0;
        state.pointer_capture.source_chip_reveal_id = 0;
        state.keysound_catalog.keysound_reveal_primary = false;
        return;
    }
    state.keysound_catalog.keysound_selection.primary = state.pointer_capture.source_chip_reveal_id;
    state.keysound_catalog.keysound_selection.selected = {state.pointer_capture.source_chip_reveal_id};
    state.keysound_catalog.keysound_search.fill(0);
    state.keysound_filters.keysound_filters = {};
    state.keysound_filters.keysound_filters.include_ungrouped = true;
    state.keysound_filters.keysound_filters.layer_chart_id = view.chart_id;
    state.keysound_filters.keysound_filter_states_by_project[view.project_id] = state.keysound_filters.keysound_filters;
    state.keysound_catalog.keysound_reveal_primary = true;
}

auto chip_sound_page_ready(const SoundsPanel& panel) -> bool {
    const auto& state = *panel.state;
    const auto& view = *panel.view;
    if (state.pointer_capture.source_chip_reveal_kind != 1) return true;
    const auto primary = view.keysound_primary_display_index;
    if (primary < view.keysound_display_first_row ||
        primary - view.keysound_display_first_row >= view.keysound_display_row_count) return false;
    const auto& row = view.keysound_display_rows[primary - view.keysound_display_first_row];
    return row.kind == WORM_UI_COMPOSE_KEYSOUND_SOUND_ROW &&
        row.resource_id == state.pointer_capture.source_chip_reveal_id;
}

void reveal_sound_primary(const SoundsPanel& panel) {
    auto* context = panel.context;
    if (!panel.state->keysound_catalog.keysound_reveal_primary || !chip_sound_reveal_ready(panel)) return;
    if (!panel.state->keysound_filters.keysound_slots_mode &&
        panel.view->keysound_section_projection_available &&
        panel.view->keysound_primary_display_index_valid) {
        nk_group_set_scroll(context, "Compose sound list", 0,
            static_cast<nk_uint>(panel.view->keysound_primary_display_index *
                (row_height + context->style.window.spacing.y)));
    } else {
        const auto primary = std::ranges::find(panel.state->keysound_catalog.keysound_shown,
                                               panel.state->keysound_catalog.keysound_selection.primary);
        if (primary != panel.state->keysound_catalog.keysound_shown.end())
        nk_group_set_scroll(context, "Compose sound list", 0,
            static_cast<nk_uint>(std::distance(panel.state->keysound_catalog.keysound_shown.begin(), primary) *
                (row_height + context->style.window.spacing.y)));
    }
    if (!chip_sound_page_ready(panel)) return;
    panel.state->keysound_catalog.keysound_reveal_primary = false;
    if (panel.state->pointer_capture.source_chip_reveal_kind == 1) {
        panel.state->pointer_capture.source_chip_reveal_kind = 0;
        panel.state->pointer_capture.source_chip_reveal_id = 0;
    }
}

void render_list(const SoundsPanel& panel, float height) {
    auto* context = panel.context;
    wormedit::ui::nuklear::render_catalog_header(context,
        wormedit::ui::nuklear::catalog_layout_t::sounds);
    height = std::max(40.0f, height - wormedit::ui::nuklear::catalog_header_height - context->style.window.spacing.y);
    // Before rows: they open the group menu, and the opening press must survive to its first begin.
    (void)wormedit::ui::chrome::context_menu_update(panel.state->keysound_catalog.keysound_group_menu, context);
    const auto automatic_sections = !panel.state->keysound_filters.keysound_slots_mode &&
        panel.view->keysound_section_projection_available;
    const auto count = static_cast<int>(automatic_sections
        ? panel.view->keysound_display_total_count : panel.state->keysound_catalog.keysound_shown.size());
    nk_layout_row_dynamic(context, height, 1);
    reveal_sound_primary(panel);
    struct nk_list_view list{};
    if (!nk_list_view_begin(context, &list, "Compose sound list", NK_WINDOW_BORDER,
                            static_cast<int>(row_height), std::max(count, 1))) return;
    if (count == 0) {
        render_list_hint(panel);
    } else if (automatic_sections) {
        if (panel.state->keysound_filters.keysound_auto_first_row != static_cast<std::size_t>(list.begin))
            panel.state->keysound_filters.keysound_auto_first_row = static_cast<std::size_t>(list.begin);
        const auto available = static_cast<std::size_t>(std::max(list.end - list.begin, 1));
        panel.state->keysound_filters.keysound_auto_row_limit = std::clamp(available + 16, std::size_t{32},
                                                          std::size_t{128});
        const auto first = panel.view->keysound_display_first_row;
        for (int index = list.begin; index < list.end; ++index) {
            const auto absolute = static_cast<std::size_t>(index);
            if (absolute < first || absolute - first >= panel.view->keysound_display_row_count) {
                nk_layout_row_dynamic(context, row_height, 1);
                nk_spacing(context, 1);
                continue;
            }
            render_display_row(panel, panel.view->keysound_display_rows[absolute - first]);
        }
    } else {
        for (int index = list.begin; index < std::min(list.end, count); ++index)
            render_row(panel, static_cast<std::size_t>(index));
    }
    nk_list_view_end(&list);
    render_sound_group_context_menu(panel);
}

void render_title(const SoundsPanel& panel) {
    auto* context = panel.context;
    const auto& view = *panel.view;
    const auto& state = *panel.state;
    nk_layout_row_dynamic(context, 20, 1);
    char text[96];
    std::snprintf(text, sizeof(text), "Sounds  %zu%s  ·  %zu selected", view.keysound_total_count,
                  view.keysounds_truncated ? "+" : "", state.keysound_catalog.keysound_selection.selected.size());
    nk_label(context, text, NK_TEXT_LEFT);
}

void publish_drop_bounds(const SoundsPanel& panel) {
    const auto bounds = nk_window_get_content_region(panel.context);
    auto& state = *panel.state;
    state.sounds_dock.sound_list_valid = true;
    state.sounds_dock.sound_list_x = bounds.x;
    state.sounds_dock.sound_list_y = bounds.y;
    state.sounds_dock.sound_list_width = bounds.w;
    state.sounds_dock.sound_list_height = bounds.h;
}

void clear_filter_release_guard_if_quiet(const SoundsPanel& panel) {
    auto& state = *panel.state;
    (void)wormedit::ui::chrome::popup_update_release_pending(
        panel.context, state.keysound_filters.keysound_filter_release_pending);
}

void render_sound_panel_controls(const SoundsPanel& panel) {
    auto& state = *panel.state;
    render_title(panel);
    render_mode_row(panel);
    render_toolbar(panel);
    render_group_actions(panel);
    if (state.keysound_filters.keysound_slots_mode && panel.view->keysound_ids_available)
        render_sound_slot_search(panel);
    else {
        render_search(panel);
        render_use_filter(panel);
        render_group_filter(panel);
    }
}

} // namespace

// Sounds and BMSON tabs; the strip exists only on BMSON charts.
static void render_sounds_dock_tabs(const SoundsPanel& panel) {
    nk_layout_row_dynamic(panel.context, 24, 2);
    const auto sounds = wormedit::ui::chrome::mode_button(
        panel.context, "Sounds", "Sample list and sound tools", !panel.state->dock_layout.sounds_dock_bmson_tab);
    const auto bmson = wormedit::ui::chrome::mode_button(
        panel.context, "BMSON", "BMSON BGM markers, razor and staged regions",
        panel.state->dock_layout.sounds_dock_bmson_tab);
    if (sounds) panel.state->dock_layout.sounds_dock_bmson_tab = false;
    else if (bmson) panel.state->dock_layout.sounds_dock_bmson_tab = true;
}

void worm_ui_nuklear_render_compose_sounds(struct nk_context* context,
                                           const WormUiComposeView* view,
                                           WormUiWorkspaceAction* action,
                                           worm_ui_nuklear_compose_state_t* state) {
    if (!context || !view || !action || !state) return;
    const SoundsPanel panel{context, view, action, state};
    if (!view->bmson_tools_available) state->dock_layout.sounds_dock_bmson_tab = false;
    else render_sounds_dock_tabs(panel);
    if (state->dock_layout.sounds_dock_bmson_tab) {
        // The dock window has no scrollbar, so the tools get a scrollable group of their own: in a
        // short window the lower staged-region actions stay reachable with the wheel.
        const auto height = wormedit::ui::layout::remaining_content_height(context,
            {.minimum = 60.0f, .include_spacing = true,
             .clamp_order = wormedit::ui::layout::content_height_clamp_order_t::available_first});
        nk_layout_row_dynamic(context, height, 1);
        if (nk_group_begin(context, "bmson-tools", 0)) {
            worm_ui_nuklear_render_compose_bmson_tab(context, view, action, state);
            nk_group_end(context);
        }
        return;
    }
    clear_filter_release_guard_if_quiet(panel);
    publish_drop_bounds(panel);
    sync_filter_state(panel);
    sync_with_document(panel);
    restore_chip_sound_selection(panel);
    sync_sound_slots(panel);
    collect_shown_and_masks(panel);
    refresh_selected_slot_resource(panel);
    sync_selected_slot_to_armed_resource(panel);
    process_pending_slot_file(panel);
    render_sound_panel_controls(panel);
    render_section_add_buttons(panel);
    if (state->dock_layout.sounds_layers_open) render_layer_section(panel);
    const auto footer_height = (view->keysound_ids_available ? 44.0f : 0.0f) +
                               (state->keysound_slots.keysound_slot_feedback.empty() ? 0.0f : 24.0f);
    const auto height = wormedit::ui::layout::remaining_content_height(context,
        {.minimum = 60.0f, .reserved = footer_height, .include_spacing = true,
         .clamp_order = wormedit::ui::layout::content_height_clamp_order_t::available_first});
    const auto block_list_pointer = state->keysound_filters.is_popup_open() ||
                                    state->keysound_filters.keysound_filter_release_pending ||
                                    state->keysound_slots.keysound_renumber_confirmation_open ||
                                    state->keysound_slots.keysound_renumber_release_pending ||
                                    state->keysound_slots.keysound_slot_confirmation_open ||
                                    state->keysound_slots.keysound_slot_release_pending;
    if (block_list_pointer) nk_widget_disable_begin(context);
    if (state->keysound_filters.keysound_slots_mode && view->sound_slots_mode)
        render_sound_slot_list(panel, height);
    else
        render_list(panel, height);
    if (block_list_pointer) nk_widget_disable_end(context);
    if (view->keysound_ids_available)
        wormedit::ui::nuklear::render_catalog_capacity(context,
            {.used = view->keysound_ids_used, .total = view->keysound_ids_total,
             .largest_free_run = view->keysound_largest_free_run});
    render_sound_layer_confirmation(panel);
    render_sound_renumber_confirmation(panel);
    render_sound_slot_confirmation(panel);
    if (!state->keysound_slots.keysound_slot_feedback.empty()) {
        nk_layout_row_dynamic(context, 20, 1);
        nk_label_colored(context, state->keysound_slots.keysound_slot_feedback.c_str(), NK_TEXT_LEFT,
                         nk_rgb(226, 166, 64));
    }
}
