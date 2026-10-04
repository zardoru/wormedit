#include "ui/nuklear/compose_sounds_panel.h"
#include "ui/nuklear/chrome.h"
#include "ui/nuklear/lucide_icons.h"
#include "ui/nuklear/theme.h"
#include "ui/nuklear/vector_icons.h"

#include "wormedit/nuklear_config.h"

#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_keyboard.h>

#include <algorithm>
#include <bit>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <mutex>
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

// ---- Multi-file picker. SDL may call back from another thread. ----

struct picker_target_t {
    bool slots_mode{};
    std::uint64_t project{}, chart{}, revision{}, occupied_resource{};
    std::uint16_t slot{};
};

struct PickerState {
    std::mutex mutex;
    bool open{};
    bool callback_ready{};
    picker_target_t target;
    std::vector<std::string> paths;
};

auto picker() -> PickerState& {
    static PickerState state;
    return state;
}

void SDLCALL picker_callback(void* /*userdata*/, const char* const* filelist, int /*filter*/) {
    auto& state = picker();
    std::lock_guard lock{state.mutex};
    state.open = false;
    state.callback_ready = true;
    for (std::size_t index = 0; filelist && filelist[index] &&
                                state.paths.size() < WORM_UI_SOUND_DROP_CAPACITY; ++index)
        state.paths.emplace_back(filelist[index]);
}

auto picker_open() -> bool {
    auto& state = picker();
    std::lock_guard lock{state.mutex};
    return state.open;
}

void show_picker(const SoundsPanel& panel) {
    {
        auto& state = picker();
        std::lock_guard lock{state.mutex};
        if (state.open) return;
        state.open = true;
        state.callback_ready = false;
        state.paths.clear();
        state.target = {};
        state.target.project = panel.view->project_id;
        state.target.chart = panel.view->chart_id;
        state.target.revision = panel.view->document_revision;
        if (panel.state->keysound_slots_mode) {
            state.target.slots_mode = true;
            state.target.slot = panel.state->keysound_selected_slot;
            state.target.occupied_resource = panel.state->keysound_selected_slot_resource;
        }
    }
    static const SDL_DialogFileFilter filters[]{{"Audio files", "wav;ogg;mp3;flac"}};
    SDL_ShowOpenFileDialog(picker_callback, nullptr, SDL_GetKeyboardFocus(), filters, 1, nullptr,
                           !panel.state->keysound_slots_mode);
}

auto take_picker_result(std::vector<std::string>& destination, picker_target_t& target) -> bool {
    auto& state = picker();
    std::lock_guard lock{state.mutex};
    if (!state.callback_ready) return false;
    destination = std::move(state.paths);
    state.paths.clear();
    target = state.target;
    state.target = {};
    state.callback_ready = false;
    return true;
}

auto grouped_count(std::size_t value) -> std::string {
    auto text = std::to_string(value);
    for (auto position = text.size(); position > 3; position -= 3)
        text.insert(position - 3, ",");
    return text;
}

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
    if (panel.action->compose_action != WORM_UI_COMPOSE_ACTION_NONE) return false;
    panel.action->compose_action = kind;
    panel.action->compose_input_chart_id = panel.view->chart_id;
    panel.action->compose_input_document_revision = panel.view->document_revision;
    return true;
}

void stage_selection(const SoundsPanel& panel) {
    auto& state = *panel.state;
    state.keysound_staged_ids = state.keysound_selection.selected;
    panel.action->compose_sound_resource_ids = state.keysound_staged_ids.data();
    panel.action->compose_sound_resource_count = state.keysound_staged_ids.size();
}

void raise_on_selection(const SoundsPanel& panel, WormUiComposeActionKind kind,
                        std::uint64_t layer_id = 0) {
    if (!raise(panel, kind)) return;
    stage_selection(panel);
    panel.action->compose_semantic_layer_id = layer_id;
}

void stage_path_payload(const SoundsPanel& panel, std::string_view path) {
    auto& state = *panel.state;
    state.keysound_staged_paths = {std::string{path}};
    state.keysound_staged_path_pointers.clear();
    state.keysound_staged_path_pointers.push_back(state.keysound_staged_paths.front().c_str());
}

void raise_sound_slot_assignment(const SoundsPanel& panel, std::uint16_t slot,
                                 std::string_view path, bool confirmed) {
    if (!raise(panel, WORM_UI_COMPOSE_ACTION_ASSIGN_SOUND_SLOT)) return;
    stage_path_payload(panel, path);
    panel.action->compose_sound_slot = slot;
    panel.action->compose_sound_slot_confirmed = confirmed;
    panel.action->compose_import_paths = panel.state->keysound_staged_path_pointers.data();
    panel.action->compose_import_path_count = panel.state->keysound_staged_path_pointers.size();
}

void process_slot_file(const SoundsPanel& panel, std::uint64_t project, std::uint64_t chart,
                       std::uint64_t revision, std::uint16_t slot,
                       std::uint64_t occupied_resource, std::string_view path) {
    auto& state = *panel.state;
    if (!state.keysound_slots_mode || !panel.view->sound_slots_mode ||
        panel.view->project_id != project || panel.view->chart_id != chart ||
        panel.view->document_revision != revision || slot == 0 || slot > 1295) {
        state.keysound_slot_feedback = "The WAV slot target changed; choose the file again.";
        return;
    }
    state.keysound_slot_feedback.clear();
    if (occupied_resource != 0) {
        state.keysound_slot_confirmation_open = true;
        state.keysound_slot_confirmation_project = project;
        state.keysound_slot_confirmation_chart = chart;
        state.keysound_slot_confirmation_revision = revision;
        state.keysound_slot_confirmation_number = slot;
        state.keysound_slot_confirmation_resource = occupied_resource;
        state.keysound_slot_confirmation_path.assign(path);
        return;
    }
    raise_sound_slot_assignment(panel, slot, path, false);
}

void process_pending_slot_file(const SoundsPanel& panel) {
    auto& state = *panel.state;
    if (!state.keysound_pending_file || panel.action->compose_action != WORM_UI_COMPOSE_ACTION_NONE)
        return;
    process_slot_file(panel, state.keysound_pending_project, state.keysound_pending_chart,
                      state.keysound_pending_revision, state.keysound_pending_slot,
                      state.keysound_pending_occupied_resource, state.keysound_pending_path);
    state.keysound_pending_file = false;
    state.keysound_pending_path.clear();
}

void import_picked_files(const SoundsPanel& panel) {
    auto& state = *panel.state;
    if (panel.action->compose_action != WORM_UI_COMPOSE_ACTION_NONE) return;
    picker_target_t target{};
    if (take_picker_result(state.keysound_staged_paths, target)) {
        if (target.slots_mode) {
            if (state.keysound_staged_paths.empty()) return;
            if (state.keysound_staged_paths.size() != 1) {
                state.keysound_slot_feedback = "Choose exactly one file for a BMS WAV slot.";
                return;
            }
            state.keysound_pending_project = target.project;
            state.keysound_pending_chart = target.chart;
            state.keysound_pending_revision = target.revision;
            state.keysound_pending_slot = target.slot;
            state.keysound_pending_occupied_resource = target.occupied_resource;
            state.keysound_pending_path = state.keysound_staged_paths.front();
            state.keysound_pending_file = true;
        } else if (target.project != panel.view->project_id ||
                   target.chart != panel.view->chart_id ||
                   target.revision != panel.view->document_revision ||
                   state.keysound_slots_mode) {
            state.keysound_slot_feedback = "The project, chart, revision, or mode changed while the picker was open.";
            state.keysound_staged_paths.clear();
            return;
        } else if (!state.keysound_staged_paths.empty()) {
            state.keysound_staged_path_pointers.clear();
            for (const auto& path : state.keysound_staged_paths)
                state.keysound_staged_path_pointers.push_back(path.c_str());
            (void)raise(panel, WORM_UI_COMPOSE_ACTION_IMPORT_SOUND_FILES);
            panel.action->compose_import_paths = state.keysound_staged_path_pointers.data();
            panel.action->compose_import_path_count = state.keysound_staged_path_pointers.size();
            return;
        }
        state.keysound_staged_paths.clear();
    }
    process_pending_slot_file(panel);
}

// ---- Selection state kept in step with the document ----

auto find_row(const WormUiComposeView& view, std::uint64_t id)
    -> const WormUiComposeKeysoundRowView* {
    for (std::size_t index = 0; index < view.keysound_count; ++index)
        if (view.keysounds[index].resource_id == id) return &view.keysounds[index];
    return nullptr;
}

void sync_with_document(const SoundsPanel& panel) {
    auto& state = *panel.state;
    const auto& view = *panel.view;
    if (state.keysound_project_id != view.project_id || state.keysound_chart_id != view.chart_id) {
        worm_ui_keysound_clear(state.keysound_selection);
        state.keysound_project_id = view.project_id;
        state.keysound_chart_id = view.chart_id;
        state.keysound_revision = view.document_revision;
        return;
    }
    if (state.keysound_revision == view.document_revision) return;
    state.keysound_revision = view.document_revision;
    state.keysound_all_ids.clear();
    for (std::size_t index = 0; index < view.keysound_count; ++index)
        state.keysound_all_ids.push_back(view.keysounds[index].resource_id);
    worm_ui_keysound_prune(state.keysound_selection, state.keysound_all_ids);
}

void reset_sound_slots(worm_ui_nuklear_compose_state_t& state, std::uint64_t project,
                       std::uint64_t chart, std::uint64_t revision) {
    if (state.keysound_pending_file || state.keysound_slot_confirmation_open) {
        state.keysound_slot_feedback = "The WAV slot target changed; choose the file again.";
        state.keysound_slot_release_pending = true;
    }
    state.keysound_slots_mode = false;
    state.keysound_slots_search.fill(0);
    state.keysound_slots_first_row = 0;
    state.keysound_slots_row_limit = 64;
    state.keysound_selected_slot = 1;
    state.keysound_selected_slot_resource = 0;
    state.keysound_selected_slot_resource_known = false;
    state.keysound_pending_file = false;
    state.keysound_pending_path.clear();
    state.keysound_slots_project = project;
    state.keysound_slots_chart = chart;
    state.keysound_slots_revision = revision;
}

void sync_sound_slots(const SoundsPanel& panel) {
    auto& state = *panel.state;
    const auto& view = *panel.view;
    if (state.keysound_slots_project != view.project_id ||
        state.keysound_slots_chart != view.chart_id) {
        reset_sound_slots(state, view.project_id, view.chart_id, view.document_revision);
    }
    if (state.keysound_slots_revision != view.document_revision) {
        if (state.keysound_pending_file || state.keysound_slot_confirmation_open) {
            state.keysound_slot_feedback = "The WAV slot target changed; choose the file again.";
            state.keysound_slot_release_pending = true;
        }
        state.keysound_slots_revision = view.document_revision;
        state.keysound_selected_slot_resource = 0;
        state.keysound_selected_slot_resource_known = false;
        state.keysound_pending_file = false;
        state.keysound_pending_path.clear();
    }
    if (!view.keysound_ids_available && state.keysound_slots_mode)
        reset_sound_slots(state, view.project_id, view.chart_id, view.document_revision);
}

void collect_shown_and_masks(const SoundsPanel& panel) {
    auto& state = *panel.state;
    const auto& view = *panel.view;
    const std::string_view query{state.keysound_slots_mode ? state.keysound_slots_search.data()
                                                            : state.keysound_search.data()};
    state.keysound_shown.clear();
    state.keysound_advance_rows.clear();
    state.keysound_masks.clear();
    for (std::size_t index = 0; index < view.keysound_count; ++index) {
        const auto& row = view.keysounds[index];
        state.keysound_masks.push_back({row.resource_id, row.layer_mask});
        if (state.keysound_slots_mode && (!row.slot || row.slot[0] == '\0')) continue;
        if (worm_ui_keysound_matches(row.slot, row.name, query)) {
            state.keysound_shown.push_back(row.resource_id);
            state.keysound_advance_rows.push_back({row.resource_id, row.missing});
        }
    }
}

auto parse_slot_number(std::string_view identifier) -> std::uint16_t {
    if (identifier.size() != 2) return 0;
    constexpr std::string_view digits = "0123456789abcdefghijklmnopqrstuvwxyz";
    const auto high = digits.find(static_cast<char>(std::tolower(static_cast<unsigned char>(identifier[0]))));
    const auto low = digits.find(static_cast<char>(std::tolower(static_cast<unsigned char>(identifier[1]))));
    if (high == digits.npos || low == digits.npos) return 0;
    const auto number = high * 36 + low;
    return number > 1295 ? 0 : static_cast<std::uint16_t>(number);
}

void refresh_selected_slot_resource(const SoundsPanel& panel) {
    auto& state = *panel.state;
    const auto& view = *panel.view;
    for (std::size_t index = 0; index < view.sound_slot_count; ++index) {
        const auto& slot = view.sound_slots[index];
        if (slot.number != state.keysound_selected_slot) continue;
        state.keysound_selected_slot_resource = slot.resource_id;
        state.keysound_selected_slot_resource_known = true;
        return;
    }
}

void sync_selected_slot_to_armed_resource(const SoundsPanel& panel) {
    auto& state = *panel.state;
    if (!state.keysound_slots_mode || !state.keysound_reveal_primary) return;
    const auto* row = find_row(*panel.view, state.keysound_selection.primary);
    const auto number = row && row->slot ? parse_slot_number(row->slot) : 0;
    if (number != 0) {
        state.keysound_selected_slot = number;
        state.keysound_selected_slot_resource = row->resource_id;
        state.keysound_selected_slot_resource_known = true;

    }
}

// ---- Toolbar ----

// Paint is the note-paint tool: the palette picks the kind, this dock the sound.
auto paint_allowed(const SoundsPanel& panel) -> bool {
    const auto& view = *panel.view;
    return view.scene_available && !view.timing_stage_active && !view.timing_focus;
}

void toggle_paint(const SoundsPanel& panel) {
    auto& state = *panel.state;
    state.resource_pointer_mode = worm_ui_nuklear_compose_resource_pointer_mode_t::off;
    state.bmson_razor_mode = worm_ui_nuklear_bmson_razor_mode_t::off;
    (void)raise(panel, WORM_UI_COMPOSE_ACTION_TOGGLE_NOTE_PAINT_MODE);
}

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
    if (sound_mode_button(context, "Automatic IDs", !state.keysound_slots_mode)) {
        state.keysound_slots_mode = false;
        if (state.keysound_slot_confirmation_open) {
            const auto bounds = nk_rect(0, 60,
                std::min(360.0f, nk_window_get_content_region(context).w), 180);
            wormedit::ui::chrome::dismiss_popup(context, "Replace BMS WAV slot", bounds);
        }
        state.keysound_slot_confirmation_open = false;
        state.keysound_pending_file = false;
        state.keysound_pending_path.clear();
        state.keysound_slot_confirmation_path.clear();
        state.keysound_slot_release_pending = true;
    }
    if (sound_mode_button(context, "BMS slots", state.keysound_slots_mode)) {
        state.keysound_slots_mode = true;
        state.keysound_slots_first_row = 0;
        state.keysound_slots_search.fill(0);
        state.keysound_selected_slot = 1;
        state.keysound_selected_slot_resource = 0;
    }
}

void render_add_sounds_button(const SoundsPanel& panel) {
    auto& state = *panel.state;
    const auto label = state.keysound_slots_mode
        ? "Choose one file for the selected BMS WAV slot"
        : "Add sounds from files (you can also drop audio files on this list)";
    if (!toolbar_button(panel.context, !picker_open(), DrawnIconButton{icons::plus, label})) return;
    if (state.keysound_slots_mode && !state.keysound_selected_slot_resource_known) {
        state.keysound_slot_feedback = "Wait for the selected WAV slot to load, or select a slot.";
        return;
    }
    show_picker(panel);
}

void render_id_sort_controls(const SoundsPanel& panel) {
    auto& state = *panel.state;
    auto* context = panel.context;
    if (!panel.view->keysound_ids_available || state.keysound_slots_mode) return;
    if (IconButton{Glyph::chevrons_left, state.keysound_sort_by_id
                       ? "Sort by name" : "Sort by BMS ID", state.keysound_sort_by_id}.render(context))
        state.keysound_sort_by_id = !state.keysound_sort_by_id;
    if (toolbar_button(context, true,
                       IconButton{Glyph::rotate_ccw, "Renumber BMS IDs by name"})) {
        state.keysound_renumber_confirmation_open = true;
        state.keysound_renumber_project = panel.view->project_id;
        state.keysound_renumber_chart = panel.view->chart_id;
        state.keysound_renumber_revision = panel.view->document_revision;
        state.keysound_renumber_resources = state.keysound_selection.selected;
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
    const auto has_primary = find_row(*panel.view, state.keysound_selection.primary) != nullptr;
    const auto has_selection = !state.keysound_selection.selected.empty();
    const auto painting = panel.view->note_paint_mode_active;
    const auto spacing = context->style.window.spacing;
    const auto pushed = nk_style_push_vec2(context, &context->style.window.spacing,
                                           nk_vec2(2.0f, spacing.y)) != 0;
    nk_layout_row_template_begin(context, 27);
    IconButton::layout_columns(context, panel.view->keysound_ids_available ? 11 : 9, 27.0f);
    nk_layout_row_template_end(context);

    const auto can_paint = paint_allowed(panel);
    if (toolbar_button(context, can_paint || painting,
            IconButton{Glyph::paintbrush,
                       can_paint || painting
                           ? "Paint: click the chart to place notes of the palette's kind "
                             "with the selected sound"
                           : "Paint is unavailable here",
                       painting}))
        toggle_paint(panel);
    if (IconButton{Glyph::chevron_right, "Advance after place: arm the next available sound shown",
                   state.auto_increment_on_place}.render(context)) {
        state.auto_increment_on_place = !state.auto_increment_on_place;
    }
    if (toolbar_button(context, has_primary,
            IconButton{Glyph::play, "Play the selected sound (Space on the list)"}) &&
        raise(panel, WORM_UI_COMPOSE_ACTION_AUDITION_SOUND))
        panel.action->compose_resource_id = state.keysound_selection.primary;
    if (IconButton{Glyph::audio_lines, state.sounds_preview_on_click
                                            ? "Preview on click: on (click to turn off)"
                                            : "Preview on click: off (click to turn on)",
                   state.sounds_preview_on_click}.render(context))
        state.sounds_preview_on_click = !state.sounds_preview_on_click;
    render_add_sounds_button(panel);
    if (toolbar_button(context, has_selection,
            DrawnIconButton{icons::trash,
                            "Remove the selected sounds from the list (only sounds no note uses)"}))
        raise_on_selection(panel, WORM_UI_COMPOSE_ACTION_DELETE_SOUNDS);
    if (toolbar_button(context, !state.keysound_shown.empty(),
            DrawnIconButton{icons::select_all, "Select every sound shown"}))
        worm_ui_keysound_select_all(state.keysound_selection, state.keysound_shown);
    if (toolbar_button(context, has_selection,
            IconButton{Glyph::x, "Clear the selection"}))
        worm_ui_keysound_clear(state.keysound_selection);
    render_id_sort_controls(panel);
    if (IconButton{Glyph::layers, "Layers: give the selected sounds a semantic layer, so notes "
                                  "painted from them join it", state.sounds_layers_open}
            .render(context))
        state.sounds_layers_open = !state.sounds_layers_open;
    if (pushed) (void)nk_style_pop_vec2(context);
}

void raise_sound_renumber(const SoundsPanel& panel, bool all) {
    if (!raise(panel, WORM_UI_COMPOSE_ACTION_RENUMBER_BMS_IDS)) return;
    auto& state = *panel.state;
    state.keysound_staged_ids = all ? std::vector<std::uint64_t>{}
                                    : state.keysound_renumber_resources;
    panel.action->compose_sound_resource_ids = state.keysound_staged_ids.data();
    panel.action->compose_sound_resource_count = state.keysound_staged_ids.size();
    panel.action->compose_renumber_all = all;
    panel.action->compose_renumber_bga = false;
    state.keysound_renumber_confirmation_open = false;
    state.keysound_renumber_release_pending = true;
    state.keysound_renumber_resources.clear();
}

void render_sound_renumber_confirmation(const SoundsPanel& panel) {
    auto& state = *panel.state;
    if (!state.keysound_renumber_confirmation_open) return;
    auto* context = panel.context;
    const auto region = nk_window_get_content_region(context);
    const auto bounds = nk_rect(0, 60, std::min(360.0f, region.w), 204);
    if (!panel.view->keysound_ids_available ||
        state.keysound_renumber_project != panel.view->project_id ||
        state.keysound_renumber_chart != panel.view->chart_id ||
        state.keysound_renumber_revision != panel.view->document_revision) {
        state.keysound_renumber_confirmation_open = false;
        state.keysound_renumber_release_pending = true;
        state.keysound_renumber_resources.clear();
        wormedit::ui::chrome::dismiss_popup(context, "Renumber WAV IDs", bounds);
        return;
    }
    if (!nk_popup_begin(context, NK_POPUP_STATIC, "Renumber WAV IDs",
                        NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR, bounds)) {
        state.keysound_renumber_confirmation_open = false;
        state.keysound_renumber_release_pending = true;
        state.keysound_renumber_resources.clear();
        return;
    }
    nk_layout_row_dynamic(context, 66, 1);
    nk_label_wrap(context, "Renumber WAV IDs in natural name order? Choose the selected sounds or the whole list.");
    nk_layout_row_dynamic(context, 28, 2);
    if (!state.keysound_renumber_resources.empty() && nk_button_label(context, "Selected"))
        raise_sound_renumber(panel, false);
    if (nk_button_label(context, "All sounds")) raise_sound_renumber(panel, true);
    if (nk_button_label(context, "Cancel")) {
        state.keysound_renumber_confirmation_open = false;
        state.keysound_renumber_release_pending = true;
        state.keysound_renumber_resources.clear();
    }
    if (!state.keysound_renumber_confirmation_open) nk_popup_close(context);
    nk_popup_end(context);
}

void close_sound_slot_confirmation(const SoundsPanel& panel) {
    panel.state->keysound_slot_confirmation_open = false;
    panel.state->keysound_slot_confirmation_path.clear();
    panel.state->keysound_slot_release_pending = true;
}

void render_sound_slot_confirmation(const SoundsPanel& panel) {
    auto& state = *panel.state;
    if (!state.keysound_slot_confirmation_open) return;
    auto* context = panel.context;
    const auto bounds = nk_rect(0, 60,
        std::min(360.0f, nk_window_get_content_region(context).w), 180);
    if (!panel.view->sound_slots_mode ||
        state.keysound_slot_confirmation_project != panel.view->project_id ||
        state.keysound_slot_confirmation_chart != panel.view->chart_id ||
        state.keysound_slot_confirmation_revision != panel.view->document_revision) {
        state.keysound_slot_feedback = "The WAV slot changed; choose the file again.";
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
    const auto slot_label = bms_slot_label(state.keysound_slot_confirmation_number);
    char message[128];
    std::snprintf(message, sizeof(message), "Replace occupied WAV slot %s with this file?",
                  slot_label.c_str());
    nk_layout_row_dynamic(context, 66, 1);
    nk_label_wrap(context, message);
    nk_layout_row_dynamic(context, 28, 2);
    if (nk_button_label(context, "Replace")) {
        raise_sound_slot_assignment(panel, state.keysound_slot_confirmation_number,
                                    state.keysound_slot_confirmation_path, true);
        close_sound_slot_confirmation(panel);
    }
    if (nk_button_label(context, "Cancel")) close_sound_slot_confirmation(panel);
    if (!state.keysound_slot_confirmation_open) nk_popup_close(context);
    nk_popup_end(context);
}

void render_sound_slot_row(const SoundsPanel& panel, std::size_t index) {
    auto* context = panel.context;
    const auto& view = *panel.view;
    auto& state = *panel.state;
    nk_layout_row_dynamic(context, 27, 1);
    const auto hovered = nk_widget_is_hovered(context);
    const auto clicked = nk_widget_is_mouse_clicked(context, NK_BUTTON_LEFT) &&
                         !view.input_occluded && !state.keysound_slot_confirmation_open &&
                         !state.keysound_slot_release_pending;
    struct nk_rect bounds{};
    if (nk_widget(&bounds, context) == NK_WIDGET_INVALID ||
        index < view.sound_slot_first_row || index - view.sound_slot_first_row >= view.sound_slot_count)
        return;
    const auto& slot = view.sound_slots[index - view.sound_slot_first_row];
    const auto selected = slot.number == state.keysound_selected_slot;
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
        std::snprintf(uses, sizeof(uses), "%u", slot.use_count);
        nk_draw_text(canvas, nk_rect(bounds.x + bounds.w - 34, bounds.y + 4, 30, bounds.h - 8),
                     uses, static_cast<int>(std::strlen(uses)), context->style.font,
                     nk_rgba(0, 0, 0, 0), color(UiColor::text_muted));
    }
    if (!clicked) return;
    state.keysound_selected_slot = slot.number;
    state.keysound_selected_slot_resource = slot.resource_id;
    state.keysound_selected_slot_resource_known = true;
    state.keysound_slot_feedback.clear();
    worm_ui_keysound_clear(state.keysound_selection);
    if (slot.resource_id && slot.placeable && !slot.missing) {
        const std::array resource{slot.resource_id};
        worm_ui_keysound_select_all(state.keysound_selection, resource);
        state.armed_resource_id = slot.resource_id;
        state.armed_from_sounds_dock = true;
        state.resource_filter = worm_ui_nuklear_compose_resource_filter_t::sample;
        if (state.sounds_preview_on_click && raise(panel, WORM_UI_COMPOSE_ACTION_AUDITION_SOUND))
            panel.action->compose_resource_id = slot.resource_id;
    } else {
        state.armed_resource_id = 0;
        state.armed_from_sounds_dock = false;
        state.resource_pointer_mode = worm_ui_nuklear_compose_resource_pointer_mode_t::off;
    }
}

void render_sound_slot_list(const SoundsPanel& panel, float height) {
    auto* context = panel.context;
    auto& state = *panel.state;
    const auto& view = *panel.view;
    if (state.keysound_reveal_primary &&
        view.sound_slot_selected_number == state.keysound_selected_slot) {
        if (view.sound_slot_selected_row != SIZE_MAX)
            nk_group_set_scroll(context, "Compose sound slots", 0,
                static_cast<nk_uint>(view.sound_slot_selected_row *
                    (27 + context->style.window.spacing.y)));
        state.keysound_reveal_primary = false;
    }
    nk_layout_row_dynamic(context, height, 1);
    struct nk_list_view list{};
    if (!nk_list_view_begin(context, &list, "Compose sound slots", NK_WINDOW_BORDER, 27,
                            static_cast<int>(std::max<std::size_t>(view.sound_slot_total_matching, 1))))
        return;
    state.keysound_slots_first_row = static_cast<std::size_t>(list.begin);
    state.keysound_slots_row_limit = static_cast<std::size_t>(std::clamp(list.end - list.begin, 1, 128));
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
    const auto before = state.keysound_slots_search;
    nk_layout_row_dynamic(panel.context, 25, 1);
    (void)wormedit::ui::chrome::worm_ui_nuklear_text_field(
        panel.context, NK_EDIT_FIELD, state.keysound_slots_search.data(),
        static_cast<int>(state.keysound_slots_search.size()), nk_filter_default);
    if (before != state.keysound_slots_search) {
        state.keysound_slots_first_row = 0;
        nk_group_set_scroll(panel.context, "Compose sound slots", 0, 0);
    }
}

void render_search(const SoundsPanel& panel) {
    auto* context = panel.context;
    nk_layout_row_dynamic(context, 25, 1);
    (void)wormedit::ui::chrome::worm_ui_nuklear_text_field(
        context, NK_EDIT_FIELD, panel.state->keysound_search.data(),
        static_cast<int>(panel.state->keysound_search.size()), nk_filter_default);
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
    const auto conflicts = std::ranges::any_of(panel.state->keysound_selection.selected,
        [&](auto id) {
            const auto* row = find_row(*panel.view, id);
            return row && (row->note_layer_conflict_mask & mask) != 0;
        });
    if (!conflicts) {
        raise_on_selection(panel, WORM_UI_COMPOSE_ACTION_ADD_SOUND_LAYER, layer.layer_id);
        return;
    }
    auto& state = *panel.state;
    state.sound_layer_confirmation_open = true;
    state.sound_layer_confirmation_project = panel.view->project_id;
    state.sound_layer_confirmation_chart = panel.view->chart_id;
    state.sound_layer_confirmation_revision = panel.view->document_revision;
    state.sound_layer_confirmation_layer = layer.layer_id;
    state.sound_layer_confirmation_resources = state.keysound_selection.selected;
}

void confirm_sound_layer(const SoundsPanel& panel, bool overwrite) {
    if (!raise(panel, WORM_UI_COMPOSE_ACTION_ADD_SOUND_LAYER)) return;
    auto& state = *panel.state;
    state.keysound_staged_ids = state.sound_layer_confirmation_resources;
    panel.action->compose_sound_resource_ids = state.keysound_staged_ids.data();
    panel.action->compose_sound_resource_count = state.keysound_staged_ids.size();
    panel.action->compose_semantic_layer_id = state.sound_layer_confirmation_layer;
    panel.action->compose_sound_overwrite_note_layers = overwrite;
    state.sound_layer_confirmation_open = false;
}

void render_sound_layer_confirmation(const SoundsPanel& panel) {
    auto& state = *panel.state;
    if (!state.sound_layer_confirmation_open) return;
    const auto& view = *panel.view;
    auto* context = panel.context;
    const auto region = nk_window_get_content_region(context);
    const auto bounds = nk_rect(0, 60, std::min(360.0f, region.w), 204);
    if (state.sound_layer_confirmation_project != view.project_id ||
        state.sound_layer_confirmation_chart != view.chart_id ||
        state.sound_layer_confirmation_revision != view.document_revision) {
        state.sound_layer_confirmation_open = false;
        wormedit::ui::chrome::dismiss_popup(context, "Sound note layers", bounds);
        return;
    }
    if (!nk_popup_begin(context, NK_POPUP_STATIC, "Sound note layers",
                        NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR, bounds)) {
        state.sound_layer_confirmation_open = false;
        return;
    }
    nk_layout_row_dynamic(context, 66, 1);
    nk_label_wrap(context, "Some notes using these sounds have different layers. Keep their "
                           "layers, or replace them with the sounds' layers?");
    nk_layout_row_dynamic(context, 28, 1);
    if (nk_button_label(context, "Keep existing layers")) confirm_sound_layer(panel, false);
    if (nk_button_label(context, "Overwrite")) confirm_sound_layer(panel, true);
    if (nk_button_label(context, "Cancel")) state.sound_layer_confirmation_open = false;
    if (!state.sound_layer_confirmation_open) nk_popup_close(context);
    nk_popup_end(context);
}

void render_layer_row(const SoundsPanel& panel, std::size_t index) {
    auto* context = panel.context;
    const auto& layer = panel.view->keysound_layers[index];
    const auto layer_state = worm_ui_keysound_layer_state(
        panel.state->keysound_selection, panel.state->keysound_masks,
        static_cast<unsigned>(index));
    const auto has_selection = !panel.state->keysound_selection.selected.empty();
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
        nk_tooltip(context, !has_selection ? "Select sounds first"
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
    const auto can_clear = !panel.state->keysound_selection.selected.empty();
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
    const auto armed = panel.state->armed_resource_id == row.resource_id;
    if (selected) nk_fill_rect(canvas, bounds, 3.0f, color(UiColor::selected));
    else if (hovered) nk_fill_rect(canvas, bounds, 3.0f, color(UiColor::surface_hover));
    if (armed) nk_stroke_rect(canvas, bounds, 3.0f, 1.0f, color(UiColor::accent));
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
    const auto name_width = bounds.w - slot_width - 12.0f - chips;
    nk_draw_text(canvas, nk_rect(bounds.x + 6 + slot_width, bounds.y + 3, std::max(name_width, 8.0f),
                                 bounds.h - 6),
                 row.name, static_cast<int>(std::strlen(row.name)), context->style.font, clear,
                 text_color);
    float chip_x = bounds.x + bounds.w - chips - 2.0f;
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

auto layer_tooltip(const SoundsPanel& panel, const WormUiComposeKeysoundRowView& row)
    -> std::string {
    std::string text = row.missing ? "File missing. " : "";
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
    const auto [ctrl, shift] = modifier_state();
    worm_ui_keysound_click(state.keysound_selection, state.keysound_shown, shown_index, ctrl,
                           shift);
    const auto* primary = find_row(*panel.view, state.keysound_selection.primary);
    state.armed_resource_id = primary && !primary->missing ? primary->resource_id : 0;
    state.resource_filter = worm_ui_nuklear_compose_resource_filter_t::sample;
    state.armed_from_sounds_dock = true;
    if (state.sounds_preview_on_click && !ctrl && !shift && id == state.armed_resource_id &&
        raise(panel, WORM_UI_COMPOSE_ACTION_AUDITION_SOUND))
        panel.action->compose_resource_id = id;
}

void render_row(const SoundsPanel& panel, std::size_t shown_index) {
    auto* context = panel.context;
    const auto id = panel.state->keysound_shown[shown_index];
    const auto* row = find_row(*panel.view, id);
    nk_layout_row_dynamic(context, row_height, 1);
    if (!row) return;
    const auto hovered = nk_widget_is_hovered(context);
    const auto clicked = nk_widget_is_mouse_clicked(context, NK_BUTTON_LEFT) &&
                         !panel.view->input_occluded &&
                         !panel.state->keysound_renumber_confirmation_open &&
                         !panel.state->keysound_renumber_release_pending &&
                         !panel.state->keysound_slot_confirmation_open &&
                         !panel.state->keysound_slot_release_pending;
    struct nk_rect bounds{};
    if (nk_widget(&bounds, context) == NK_WIDGET_INVALID) return;
    draw_row(panel, bounds, *row, worm_ui_keysound_is_selected(panel.state->keysound_selection, id),
             hovered);
    if (hovered) {
        const auto tip = layer_tooltip(panel, *row);
        if (!tip.empty()) nk_tooltip(context, tip.c_str());
    }
    if (clicked) click_row(panel, shown_index, id);
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

void render_list(const SoundsPanel& panel, float height) {
    auto* context = panel.context;
    const auto count = static_cast<int>(panel.state->keysound_shown.size());
    nk_layout_row_dynamic(context, height, 1);
    if (panel.state->keysound_reveal_primary) {
        const auto primary = std::ranges::find(panel.state->keysound_shown,
                                               panel.state->keysound_selection.primary);
        if (primary != panel.state->keysound_shown.end())
            nk_group_set_scroll(context, "Compose sound list", 0,
                static_cast<nk_uint>(std::distance(panel.state->keysound_shown.begin(), primary) *
                    (row_height + context->style.window.spacing.y)));
        panel.state->keysound_reveal_primary = false;
    }
    struct nk_list_view list{};
    if (!nk_list_view_begin(context, &list, "Compose sound list", NK_WINDOW_BORDER,
                            static_cast<int>(row_height), std::max(count, 1))) return;
    if (count == 0) {
        render_list_hint(panel);
    } else {
        for (int index = list.begin; index < std::min(list.end, count); ++index)
            render_row(panel, static_cast<std::size_t>(index));
    }
    nk_list_view_end(&list);
}

void render_title(const SoundsPanel& panel) {
    auto* context = panel.context;
    const auto& view = *panel.view;
    const auto& state = *panel.state;
    nk_layout_row_dynamic(context, 20, 1);
    char text[96];
    std::snprintf(text, sizeof(text), "Sounds  %zu%s  ·  %zu selected", view.keysound_total_count,
                  view.keysounds_truncated ? "+" : "", state.keysound_selection.selected.size());
    nk_label(context, text, NK_TEXT_LEFT);
}

void publish_drop_bounds(const SoundsPanel& panel) {
    const auto bounds = nk_window_get_content_region(panel.context);
    auto& state = *panel.state;
    state.sound_list_valid = true;
    state.sound_list_x = bounds.x;
    state.sound_list_y = bounds.y;
    state.sound_list_width = bounds.w;
    state.sound_list_height = bounds.h;
}

} // namespace

void worm_ui_nuklear_render_compose_sounds(struct nk_context* context,
                                           const WormUiComposeView* view,
                                           WormUiWorkspaceAction* action,
                                           worm_ui_nuklear_compose_state_t* state) {
    if (!context || !view || !action || !state) return;
    const SoundsPanel panel{context, view, action, state};
    publish_drop_bounds(panel);
    sync_with_document(panel);
    sync_sound_slots(panel);
    collect_shown_and_masks(panel);
    refresh_selected_slot_resource(panel);
    sync_selected_slot_to_armed_resource(panel);
    import_picked_files(panel);
    render_title(panel);
    render_mode_row(panel);
    render_toolbar(panel);
    if (state->keysound_slots_mode && view->keysound_ids_available)
        render_sound_slot_search(panel);
    else
        render_search(panel);
    if (state->sounds_layers_open) render_layer_section(panel);
    const auto region = nk_window_get_content_region(context);
    const auto footer_height = (view->keysound_ids_available ? 44.0f : 0.0f) +
                               (state->keysound_slot_feedback.empty() ? 0.0f : 24.0f);
    const auto remaining = region.y + region.h - nk_widget_position(context).y - footer_height -
                           context->style.window.spacing.y;
    const auto block_list_pointer = state->keysound_renumber_confirmation_open ||
                                    state->keysound_renumber_release_pending ||
                                    state->keysound_slot_confirmation_open ||
                                    state->keysound_slot_release_pending;
    if (block_list_pointer) nk_widget_disable_begin(context);
    if (state->keysound_slots_mode && view->sound_slots_mode)
        render_sound_slot_list(panel, std::max(remaining, 60.0f));
    else
        render_list(panel, std::max(remaining, 60.0f));
    if (block_list_pointer) nk_widget_disable_end(context);
    if (view->keysound_ids_available) {
        nk_layout_row_dynamic(context, 20, 1);
        char capacity[64];
        const auto used = grouped_count(view->keysound_ids_used);
        const auto total = grouped_count(view->keysound_ids_total);
        std::snprintf(capacity, sizeof(capacity), "%s / %s IDs", used.c_str(), total.c_str());
        const auto amber = view->keysound_ids_total != 0 &&
            view->keysound_ids_used * 10 > view->keysound_ids_total * 9;
        nk_label_colored(context, capacity, NK_TEXT_LEFT,
                         amber ? nk_rgb(226, 166, 64) : context->style.text.color);
        nk_layout_row_dynamic(context, 20, 1);
        char free_run[64];
        std::snprintf(free_run, sizeof(free_run), "Largest free run: %zu",
                      view->keysound_largest_free_run);
        nk_label(context, free_run, NK_TEXT_LEFT);
    }
    render_sound_layer_confirmation(panel);
    render_sound_renumber_confirmation(panel);
    render_sound_slot_confirmation(panel);
    if (!state->keysound_slot_feedback.empty()) {
        nk_layout_row_dynamic(context, 20, 1);
        nk_label_colored(context, state->keysound_slot_feedback.c_str(), NK_TEXT_LEFT,
                         nk_rgb(226, 166, 64));
    }
    if (state->keysound_renumber_release_pending &&
        !context->input.mouse.buttons[NK_BUTTON_LEFT].down)
        state->keysound_renumber_release_pending = false;
    if (state->keysound_slot_release_pending &&
        !context->input.mouse.buttons[NK_BUTTON_LEFT].down)
        state->keysound_slot_release_pending = false;
}
