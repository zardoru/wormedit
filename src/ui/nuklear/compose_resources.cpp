#include "ui/nuklear/compose_resources.h"
#include "ui/nuklear/chrome.h"

#include "wormedit/nuklear_config.h"

#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_video.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <new>
#include <string>
#include <string_view>

namespace {

struct ResourcePanel {
    nk_context* context;
    const WormUiComposeView* view;
    WormUiWorkspaceAction* action;
    worm_ui_nuklear_compose_state_t* state;
};

auto filter_role(worm_ui_nuklear_compose_resource_filter_t filter) -> std::uint8_t;
auto filter_name(worm_ui_nuklear_compose_resource_filter_t filter) -> const char*;
auto resource_matches(const WormUiComposeResourceView& resource, const char* query) -> bool;
void SDLCALL compose_file_dialog_callback(void* userdata, const char* const* filelist,
                                          int selected_filter);

void apply_file_dialog_result(worm_ui_nuklear_compose_state_t& state) {
    auto picked = worm_ui_nuklear_compose_file_dialog_take();
    if (!picked) return;
    state.resource_import_error.fill('\0');
    if (!picked->error.empty()) {
        std::snprintf(state.resource_import_error.data(), state.resource_import_error.size(), "%s",
                      picked->error.c_str());
        return;
    }
    if (picked->canceled) return;
    if (picked->path.size() >= state.resource_import_path.size() ||
        !std::filesystem::path{picked->path}.is_absolute()) {
        std::snprintf(state.resource_import_error.data(), state.resource_import_error.size(),
                      "Choose a bounded local filesystem path.");
        return;
    }
    std::snprintf(state.resource_import_path.data(), state.resource_import_path.size(), "%s",
                  picked->path.c_str());
}

auto find_resource(const WormUiComposeView& view, std::uint64_t id)
    -> const WormUiComposeResourceView* {
    for (std::size_t index = 0; index < view.resource_count; ++index) {
        if (view.resources[index].resource_id == id) return &view.resources[index];
    }
    return nullptr;
}

void update_selection_state(const WormUiComposeView& view,
                            worm_ui_nuklear_compose_state_t& state) {
    const auto selected_object_id = view.selected_bgm_event_id != 0
        ? view.selected_bgm_event_id : view.selected_note_label_note_id;
    const auto changed = state.resource_selection_chart_id != view.chart_id ||
        state.resource_selection_object_id != selected_object_id ||
        state.resource_selection_assigned_id != view.selected_assigned_resource_id;
    if (!changed) return;
    state.resource_selection_chart_id = view.chart_id;
    state.resource_selection_object_id = selected_object_id;
    state.resource_selection_assigned_id = view.selected_assigned_resource_id;
    if (selected_object_id == 0 || view.selected_assigned_resource_id == 0) return;

    state.armed_resource_id = view.selected_assigned_resource_id;
    const auto* assigned = find_resource(view, view.selected_assigned_resource_id);
    if (assigned) {
        const auto preferred = view.selected_bgm_event_id != 0
            ? WORM_UI_COMPOSE_RESOURCE_ROLE_BGM : WORM_UI_COMPOSE_RESOURCE_ROLE_SAMPLE;
        if ((assigned->roles & preferred) != 0) {
            state.resource_filter = view.selected_bgm_event_id != 0
                ? worm_ui_nuklear_compose_resource_filter_t::bgm
                : worm_ui_nuklear_compose_resource_filter_t::sample;
        } else if ((assigned->roles & WORM_UI_COMPOSE_RESOURCE_ROLE_SAMPLE) != 0) {
            state.resource_filter = worm_ui_nuklear_compose_resource_filter_t::sample;
        } else if ((assigned->roles & WORM_UI_COMPOSE_RESOURCE_ROLE_BGM) != 0) {
            state.resource_filter = worm_ui_nuklear_compose_resource_filter_t::bgm;
        }
    }
    state.resource_search.fill('\0');
    state.resource_pointer_mode = worm_ui_nuklear_compose_resource_pointer_mode_t::off;
}

auto find_export_group(const WormUiComposeView& view,
                       worm_ui_nuklear_compose_state_t& state)
    -> const WormUiComposeOutputGroupView* {
    for (std::size_t index = 0; index < view.output_group_count; ++index) {
        if (view.output_groups[index].document_index == state.export_group_document_index)
            return &view.output_groups[index];
    }
    if (view.output_group_count == 0) return nullptr;
    state.export_group_document_index = view.output_groups[0].document_index;
    return &view.output_groups[0];
}

auto render_export_group(const ResourcePanel& panel)
    -> const WormUiComposeOutputGroupView* {
    auto* context = panel.context;
    const auto& view = *panel.view;
    auto* state = panel.state;
    auto* action = panel.action;
    auto* export_group = find_export_group(view, *state);
    nk_layout_row_dynamic(context, 22, 1);
    if (!export_group) {
        nk_label(context, "No output group for this chart", NK_TEXT_LEFT);
    } else {
        std::array<char, 180> label{};
        std::snprintf(label.data(), label.size(), "Export: %s",
                      export_group->name[0] ? export_group->name
                                            : export_group->relative_output_path);
        if (wormedit::ui::chrome::safe_combo_begin_label(context, label.data(),
                                 nk_vec2(nk_widget_width(context), 220.0f))) {
            nk_layout_row_dynamic(context, 22, 1);
            for (std::size_t index = 0; index < view.output_group_count; ++index) {
                const auto& candidate = view.output_groups[index];
                if (nk_combo_item_label(context,
                        candidate.name[0] ? candidate.name : candidate.relative_output_path,
                        NK_TEXT_LEFT)) {
                    state->export_group_document_index = candidate.document_index;
                    export_group = &candidate;
                }
            }
            int overwrite = state->export_overwrite ? 1 : 0;
            nk_layout_row_dynamic(context, 20, 1);
            nk_checkbox_label(context, "Replace existing chart files", &overwrite);
            state->export_overwrite = overwrite != 0;
            nk_layout_row_dynamic(context, 24, 1);
            if (nk_button_label(context, "Export selected group") &&
                action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
                action->compose_output_group_index = export_group->document_index;
                action->compose_export_overwrite = state->export_overwrite;
                action->compose_action = WORM_UI_COMPOSE_ACTION_EXPORT_OUTPUT_GROUP;
            }
            nk_combo_end(context);
        }
        nk_layout_row_dynamic(context, 17, 1);
        nk_label(context, export_group->relative_output_path, NK_TEXT_LEFT);
    }
    if (view.output_groups_truncated) {
        nk_layout_row_dynamic(context, 17, 1);
        nk_label(context, "Additional output groups are hidden by the view cap.", NK_TEXT_LEFT);
    }
    return export_group;
}

auto can_move_selected_bmson_bgm(const WormUiComposeView& view) -> bool {
    return view.scene_available && view.selected_bmson_bgm_event_id != 0;
}

void render_bmson_object_actions(const ResourcePanel& panel, bool can_move) {
    auto* context = panel.context;
    const auto& view = *panel.view;
    auto* action = panel.action;
    nk_layout_row_dynamic(context, 24, 2);
    if (!can_move) nk_widget_disable_begin(context);
    const auto move_clicked = nk_button_label(context, "Move BMSON BGM to cursor");
    const auto delete_clicked = nk_button_label(context, "Delete BMSON BGM");
    if (!can_move) nk_widget_disable_end(context);
    if (action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
        if (move_clicked && can_move) {
            action->compose_bmson_bgm_event_id = view.selected_bmson_bgm_event_id;
            action->compose_action = WORM_UI_COMPOSE_ACTION_MOVE_BMSON_BGM_TO_CURSOR;
        } else if (delete_clicked && can_move) {
            action->compose_bmson_bgm_event_id = view.selected_bmson_bgm_event_id;
            action->compose_action = WORM_UI_COMPOSE_ACTION_DELETE_BMSON_BGM_MARKER;
        }
    }

    nk_layout_row_dynamic(context, 24, 2);
    if (!can_move) nk_widget_disable_begin(context);
    const auto assign_key_clicked = nk_button_label(context, "BGM to active key lane");
    if (!can_move) nk_widget_disable_end(context);
    const auto can_assign_to_bgm = view.selected_bmson_note_event_id != 0;
    if (!can_assign_to_bgm) nk_widget_disable_begin(context);
    const auto assign_bgm_clicked = nk_button_label(context, "Selected tap to BGM");
    if (!can_assign_to_bgm) nk_widget_disable_end(context);
    if (action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
        if (assign_key_clicked && can_move) {
            action->compose_bmson_bgm_event_id = view.selected_bmson_bgm_event_id;
            action->compose_action = WORM_UI_COMPOSE_ACTION_ASSIGN_BMSON_BGM_TO_ACTIVE_LANE;
        } else if (assign_bgm_clicked && can_assign_to_bgm) {
            action->compose_bmson_bgm_event_id = view.selected_bmson_note_event_id;
            action->compose_action = WORM_UI_COMPOSE_ACTION_ASSIGN_BMSON_NOTE_TO_BGM;
        }
    }
}

void render_bmson_marker_actions(const ResourcePanel& panel, bool can_move) {
    auto* context = panel.context;
    const auto& view = *panel.view;
    auto* action = panel.action;
    nk_layout_row_dynamic(context, 12, 1);
    nk_rule_horizontal(context, wormedit::ui::theme::ui(
                                    wormedit::ui::theme::UiColor::border), false);
    nk_layout_row_dynamic(context, 24, 2);
    if (!can_move) nk_widget_disable_begin(context);
    const auto insert_continue = nk_button_label(context, "Insert continue at cursor");
    const auto insert_restart = nk_button_label(context, "Insert restart at cursor");
    if (!can_move) nk_widget_disable_end(context);
    if ((insert_continue || insert_restart) && can_move &&
        action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
        action->compose_bmson_bgm_event_id = view.selected_bmson_bgm_event_id;
        action->compose_action = insert_restart
            ? WORM_UI_COMPOSE_ACTION_INSERT_BMSON_BGM_RESTART_AT_CURSOR
            : WORM_UI_COMPOSE_ACTION_INSERT_BMSON_BGM_CONTINUE_AT_CURSOR;
    }
    nk_layout_row_dynamic(context, 20, 1);
    nk_labelf(context, NK_TEXT_LEFT, "Staged BMSON markers: %zu",
              view.staged_bmson_marker_count);
    nk_layout_row_dynamic(context, 24, 2);
    if (!can_move) nk_widget_disable_begin(context);
    const auto stage_continue = nk_button_label(context, "Stage continue");
    const auto stage_restart = nk_button_label(context, "Stage restart");
    if (!can_move) nk_widget_disable_end(context);
    if ((stage_continue || stage_restart) && can_move &&
        action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
        action->compose_bmson_bgm_event_id = view.selected_bmson_bgm_event_id;
        action->compose_action = stage_restart
            ? WORM_UI_COMPOSE_ACTION_STAGE_BMSON_BGM_RESTART_AT_CURSOR
            : WORM_UI_COMPOSE_ACTION_STAGE_BMSON_BGM_CONTINUE_AT_CURSOR;
    }
}

void render_bmson_razor_mode(const ResourcePanel& panel, bool can_move) {
    auto* context = panel.context;
    const auto& view = *panel.view;
    auto* state = panel.state;
    nk_layout_row_dynamic(context, 12, 1);
    nk_rule_horizontal(context, wormedit::ui::theme::ui(
                                    wormedit::ui::theme::UiColor::border), false);
    const auto can_razor = can_move && view.selected_bmson_source_lane_id != 0 &&
                           !view.note_paint_mode_active &&
                           !view.semantic_layer_paint_mode_active &&
                           !view.timing_stage_active;
    if (!can_razor) {
        state->bmson_razor_mode = worm_ui_nuklear_bmson_razor_mode_t::off;
        nk_widget_disable_begin(context);
    }
    nk_layout_row_dynamic(context, 24, 2);
    const auto continue_clicked = wormedit::ui::chrome::mode_button(
        context, "Razor continue", "Slice the selected BGM lane without restarting",
        state->bmson_razor_mode == worm_ui_nuklear_bmson_razor_mode_t::continuation);
    const auto restart_clicked = wormedit::ui::chrome::mode_button(
        context, "Razor restart", "Slice the selected BGM lane and restart playback",
        state->bmson_razor_mode == worm_ui_nuklear_bmson_razor_mode_t::restart);
    if (!can_razor) nk_widget_disable_end(context);
    if (can_razor && (continue_clicked || restart_clicked)) {
        const auto choice = restart_clicked ? worm_ui_nuklear_bmson_razor_mode_t::restart
                                             : worm_ui_nuklear_bmson_razor_mode_t::continuation;
        state->bmson_razor_mode = state->bmson_razor_mode == choice
            ? worm_ui_nuklear_bmson_razor_mode_t::off : choice;
        if (state->bmson_razor_mode != worm_ui_nuklear_bmson_razor_mode_t::off)
            state->resource_pointer_mode = worm_ui_nuklear_compose_resource_pointer_mode_t::off;
    }
}

void render_staged_bmson_actions(const ResourcePanel& panel) {
    const auto& view = *panel.view;
    auto* context = panel.context;
    auto* action = panel.action;
    const auto has_staged = view.staged_bmson_marker_count != 0;
    nk_layout_row_dynamic(context, 24, 2);
    if (!has_staged) nk_widget_disable_begin(context);
    const auto commit_clicked = nk_button_label(context, "Commit staged");
    const auto cancel_clicked = nk_button_label(context, "Cancel staged");
    if (!has_staged) nk_widget_disable_end(context);
    if (has_staged && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
        if (commit_clicked) action->compose_action = WORM_UI_COMPOSE_ACTION_COMMIT_STAGED_BMSON_BGM;
        else if (cancel_clicked) action->compose_action = WORM_UI_COMPOSE_ACTION_CANCEL_STAGED_BMSON_BGM;
    }
}

void render_bmson_controls(const ResourcePanel& panel) {
    const auto can_move = can_move_selected_bmson_bgm(*panel.view);
    render_bmson_object_actions(panel, can_move);
    render_bmson_marker_actions(panel, can_move);
    render_bmson_razor_mode(panel, can_move);
    render_staged_bmson_actions(panel);
}

void render_filter_and_search(const ResourcePanel& panel) {
    auto* context = panel.context;
    auto* state = panel.state;
    nk_layout_row_dynamic(context, 24, 3);
    constexpr std::array filters{
        worm_ui_nuklear_compose_resource_filter_t::sample,
        worm_ui_nuklear_compose_resource_filter_t::bgm,
        worm_ui_nuklear_compose_resource_filter_t::bga,
    };
    for (const auto candidate : filters) {
        const auto selected = state->resource_filter == candidate;
        std::array<char, 24> label{};
        std::snprintf(label.data(), label.size(), "%s%s", filter_name(candidate),
                      selected ? " *" : "");
        if (!nk_button_label(context, label.data())) continue;
        if (state->resource_filter != candidate)
            state->resource_pointer_mode = worm_ui_nuklear_compose_resource_pointer_mode_t::off;
        state->resource_filter = candidate;
    }
    nk_layout_row_dynamic(context, 19, 1);
    nk_label(context, "Search", NK_TEXT_LEFT);
    nk_layout_row_begin(context, NK_DYNAMIC, 25, 2);
    nk_layout_row_push(context, 0.76f);
    (void)wormedit::ui::chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD, state->resource_search.data(),
                                         state->resource_search.size(), nk_filter_default);
    nk_layout_row_push(context, 0.24f);
    if (nk_button_label(context, "Clear")) state->resource_search.fill('\0');
    nk_layout_row_end(context);
}

auto armed_resource(const ResourcePanel& panel) -> const WormUiComposeResourceView* {
    return find_resource(*panel.view, panel.state->armed_resource_id);
}

void render_armed_resource_status(const ResourcePanel& panel,
                                  const WormUiComposeResourceView* armed) {
    auto* context = panel.context;
    const auto& view = *panel.view;
    const auto& state = *panel.state;
    nk_layout_row_dynamic(context, 19, 1);
    if (state.armed_resource_id == 0)
        nk_label(context, "Armed: none", NK_TEXT_LEFT);
    else if (armed)
        nk_labelf(context, NK_TEXT_LEFT, "Armed: %s · %s · %zu selected",
                  armed->target_index, armed->name, view.selected_object_count);
    else
        nk_labelf(context, NK_TEXT_LEFT, "Armed resource #%llu is unavailable",
                  static_cast<unsigned long long>(state.armed_resource_id));
}

void render_assign_disarm(const ResourcePanel& panel,
                          const WormUiComposeResourceView* armed) {
    const auto& view = *panel.view;
    auto* context = panel.context;
    auto* action = panel.action;
    auto* state = panel.state;
    nk_layout_row_dynamic(context, 24, 2);
    const auto can_assign = view.scene_available && view.selected_object_count != 0 &&
                            armed && !armed->missing;
    if (!can_assign) nk_widget_disable_begin(context);
    const auto assign_clicked = nk_button_label(context, "Assign selected");
    if (!can_assign) nk_widget_disable_end(context);
    if (assign_clicked && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
        action->compose_resource_id = state->armed_resource_id;
        action->compose_action = WORM_UI_COMPOSE_ACTION_ASSIGN_RESOURCE_TO_SELECTION;
    }
    const auto armed_id = state->armed_resource_id;
    if (armed_id == 0) nk_widget_disable_begin(context);
    const auto disarm_clicked = nk_button_label(context, "Disarm");
    if (armed_id == 0) nk_widget_disable_end(context);
    if (disarm_clicked) {
        state->armed_resource_id = 0;
        state->auto_increment_on_place = false;
        state->resource_pointer_mode = worm_ui_nuklear_compose_resource_pointer_mode_t::off;
    }
}

auto resource_can_place(const ResourcePanel& panel,
                        const WormUiComposeResourceView* armed) -> bool {
    const auto role = filter_role(panel.state->resource_filter);
    if (!panel.view->scene_available) return false;
    if (armed) return !armed->missing && (armed->roles & role) != 0;
    const auto id = panel.state->armed_resource_id;
    if (role == WORM_UI_COMPOSE_RESOURCE_ROLE_BGA)
        return id && id == panel.state->bga_selection.primary &&
            std::ranges::find(std::span(panel.view->bga_matching_ids, panel.view->bga_total_matching), id) !=
                std::span(panel.view->bga_matching_ids, panel.view->bga_total_matching).end();
    if (role == WORM_UI_COMPOSE_RESOURCE_ROLE_SAMPLE)
        return std::ranges::any_of(std::span(panel.view->keysounds, panel.view->keysound_count),
            [&](const auto& row) { return row.resource_id == id && !row.missing; });
    return false;
}

void render_place_at_cursor(const ResourcePanel& panel,
                            const WormUiComposeResourceView* armed, bool can_place) {
    auto* context = panel.context;
    auto* action = panel.action;
    const auto& state = *panel.state;
    nk_layout_row_dynamic(context, 24, 1);
    if (!can_place) nk_widget_disable_begin(context);
    const auto filter = state.resource_filter;
    const auto* label = filter == worm_ui_nuklear_compose_resource_filter_t::sample
        ? "Place Sample at cursor"
        : filter == worm_ui_nuklear_compose_resource_filter_t::bgm
            ? "Place BGM at cursor" : "Place BGA background at cursor";
    const auto place_clicked = nk_button_label(context, label);
    if (!can_place) nk_widget_disable_end(context);
    if (place_clicked && can_place &&
        action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
        action->compose_resource_id = armed->resource_id;
        action->compose_resource_role = filter_role(filter);
        action->compose_action = WORM_UI_COMPOSE_ACTION_PLACE_RESOURCE;
    }
}

auto pointer_place_allowed(const ResourcePanel& panel, bool can_place) -> bool {
    const auto& view = *panel.view;
    const auto filter = panel.state->resource_filter;
    const auto activating_sound = panel.action->compose_activate_sound_paint;
    return can_place && (activating_sound || !view.note_paint_mode_active) &&
           (activating_sound || !view.semantic_layer_paint_mode_active) && !view.timing_stage_active &&
           (filter != worm_ui_nuklear_compose_resource_filter_t::bgm ||
            view.bgm_lane_count != 0);
}

void render_pointer_modes(const ResourcePanel& panel, bool allowed) {
    auto* context = panel.context;
    auto* state = panel.state;
    if (!allowed) {
        state->resource_pointer_mode = worm_ui_nuklear_compose_resource_pointer_mode_t::off;
        nk_widget_disable_begin(context);
    }
    nk_layout_row_dynamic(context, 24, 2);
    const auto place_clicked = wormedit::ui::chrome::mode_button(
        context, "Place", "Place resources by clicking the field",
        state->resource_pointer_mode == worm_ui_nuklear_compose_resource_pointer_mode_t::place);
    const auto replace_clicked = wormedit::ui::chrome::mode_button(
        context, "Replace", "Replace a resource by clicking an existing object",
        state->resource_pointer_mode == worm_ui_nuklear_compose_resource_pointer_mode_t::replace);
    if (!allowed) nk_widget_disable_end(context);
    if (place_clicked && allowed) {
        const auto is_active = state->resource_pointer_mode ==
            worm_ui_nuklear_compose_resource_pointer_mode_t::place;
        state->resource_pointer_mode = is_active
            ? worm_ui_nuklear_compose_resource_pointer_mode_t::off
            : worm_ui_nuklear_compose_resource_pointer_mode_t::place;
    } else if (replace_clicked && allowed) {
        const auto is_active = state->resource_pointer_mode ==
            worm_ui_nuklear_compose_resource_pointer_mode_t::replace;
        state->resource_pointer_mode = is_active
            ? worm_ui_nuklear_compose_resource_pointer_mode_t::off
            : worm_ui_nuklear_compose_resource_pointer_mode_t::replace;
    } else {
        return;
    }
    state->bmson_razor_mode = worm_ui_nuklear_bmson_razor_mode_t::off;
}

void render_auto_increment(const ResourcePanel& panel, bool enabled) {
    auto* context = panel.context;
    auto* state = panel.state;
    nk_layout_row_dynamic(context, 23, 1);
    if (!enabled) nk_widget_disable_begin(context);
    const auto clicked = nk_button_label(
        context, state->auto_increment_on_place ? "Auto next after place: On"
                                                : "Auto next after place: Off");
    if (!enabled) nk_widget_disable_end(context);
    if (clicked && enabled) state->auto_increment_on_place = !state->auto_increment_on_place;
}

void render_armed_placement_controls(const ResourcePanel& panel,
                                    const WormUiComposeResourceView* armed) {
    render_armed_resource_status(panel, armed);
    render_assign_disarm(panel, armed);
    const auto can_place = resource_can_place(panel, armed);
    render_place_at_cursor(panel, armed, can_place && armed);
    render_pointer_modes(panel, pointer_place_allowed(panel, can_place));
    render_auto_increment(panel, can_place);
}

void render_create_resource(const ResourcePanel& panel) {
    const auto& view = *panel.view;
    auto* context = panel.context;
    const auto& filter = panel.state->resource_filter;
    auto* action = panel.action;
    const auto visual = filter == worm_ui_nuklear_compose_resource_filter_t::bga;
    const auto asset_id = visual ? view.selected_visual_asset_id : view.selected_audio_asset_id;
    const auto can_create = asset_id != 0;
    nk_layout_row_dynamic(context, 24, 1);
    if (!can_create) nk_widget_disable_begin(context);
    const auto* label = visual ? "Create BGA from selected image"
        : filter == worm_ui_nuklear_compose_resource_filter_t::sample
            ? "Create Sample from waveform" : "Create BGM from waveform";
    const auto clicked = nk_button_label(context, label);
    if (!can_create) nk_widget_disable_end(context);
    if (!clicked || !can_create || action->compose_action != WORM_UI_COMPOSE_ACTION_NONE) return;
    action->compose_resource_asset_id = asset_id;
    action->compose_resource_role = visual ? WORM_UI_COMPOSE_RESOURCE_ROLE_BGA
        : filter == worm_ui_nuklear_compose_resource_filter_t::sample
            ? WORM_UI_COMPOSE_RESOURCE_ROLE_SAMPLE : WORM_UI_COMPOSE_RESOURCE_ROLE_BGM;
    action->compose_action = WORM_UI_COMPOSE_ACTION_CREATE_RESOURCE;
}

void show_file_picker(const ResourcePanel& panel) {
    auto* state = panel.state;
    const auto generation = worm_ui_nuklear_compose_file_dialog_begin();
    if (!generation) return;
    auto* token = new (std::nothrow) std::uint64_t{*generation};
    if (!token) {
        worm_ui_nuklear_compose_file_dialog_complete(
            *generation, {}, "File picker could not allocate callback state.", false);
        return;
    }
    state->resource_import_error.fill('\0');
    SDL_ShowOpenFileDialog(compose_file_dialog_callback, token,
                           SDL_GetKeyboardFocus(), nullptr, 0, nullptr, false);
}

void render_import_path(const ResourcePanel& panel) {
    auto* context = panel.context;
    auto* state = panel.state;
    nk_layout_row_dynamic(context, 18, 1);
    nk_label(context, "External file (absolute path)", NK_TEXT_LEFT);
    nk_layout_row_begin(context, NK_DYNAMIC, 24, 2);
    nk_layout_row_push(context, 0.75f);
    (void)wormedit::ui::chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD, state->resource_import_path.data(),
                                         state->resource_import_path.size(), nk_filter_default);
    nk_layout_row_push(context, 0.25f);
    const auto dialog_open = worm_ui_nuklear_compose_file_dialog_open();
    if (dialog_open) nk_widget_disable_begin(context);
    const auto browse_clicked = nk_button_label(context, dialog_open ? "Choosing" : "Browse");
    if (dialog_open) nk_widget_disable_end(context);
    nk_layout_row_end(context);
    if (browse_clicked && !dialog_open) show_file_picker(panel);
    if (state->resource_import_error[0] != '\0') {
        nk_layout_row_dynamic(context, 18, 1);
        nk_label(context, state->resource_import_error.data(), NK_TEXT_LEFT);
    }
}

void render_import_action(const ResourcePanel& panel) {
    auto* context = panel.context;
    auto* action = panel.action;
    auto* state = panel.state;
    const auto can_import = state->resource_import_path[0] != '\0';
    if (!can_import) nk_widget_disable_begin(context);
    const auto filter = state->resource_filter;
    const auto* label = filter == worm_ui_nuklear_compose_resource_filter_t::bga
        ? "Link image as BGA"
        : filter == worm_ui_nuklear_compose_resource_filter_t::bgm
            ? "Link audio as BGM" : "Link audio as Sample";
    nk_layout_row_dynamic(context, 24, 1);
    const auto clicked = nk_button_label(context, label);
    if (!can_import) nk_widget_disable_end(context);
    if (!clicked || !can_import || action->compose_action != WORM_UI_COMPOSE_ACTION_NONE) return;
    std::snprintf(action->compose_import_path, sizeof(action->compose_import_path), "%s",
                  state->resource_import_path.data());
    action->compose_resource_role = filter_role(filter);
    action->compose_action = WORM_UI_COMPOSE_ACTION_IMPORT_RESOURCE_ASSET;
}

void render_import_controls(const ResourcePanel& panel) {
    render_import_path(panel);
    render_import_action(panel);
}

auto count_visible_resources(const ResourcePanel& panel)
    -> std::pair<std::size_t, std::size_t> {
    const auto& view = *panel.view;
    const auto& state = *panel.state;
    const auto role = filter_role(state.resource_filter);
    std::size_t role_count{};
    std::size_t matched_count{};
    for (std::size_t index = 0; index < view.resource_count; ++index) {
        const auto& resource = view.resources[index];
        if ((resource.roles & role) == 0) continue;
        ++role_count;
        if (resource_matches(resource, state.resource_search.data())) ++matched_count;
    }
    return {role_count, matched_count};
}

void render_resource_summary(const ResourcePanel& panel, std::size_t role_count,
                             std::size_t matched_count) {
    auto* context = panel.context;
    const auto& view = *panel.view;
    const auto filter = panel.state->resource_filter;
    nk_layout_row_dynamic(context, 19, 1);
    nk_labelf(context, NK_TEXT_LEFT, "%s · %zu/%zu match · %zu indexed",
              filter_name(filter), matched_count, role_count, view.resource_total_count);
    if (role_count == 0) {
        nk_layout_row_dynamic(context, 22, 1);
        nk_labelf(context, NK_TEXT_LEFT, "No %s resources are registered.", filter_name(filter));
    } else if (matched_count == 0) {
        nk_layout_row_dynamic(context, 22, 1);
        nk_label(context, "No resources match the search.", NK_TEXT_LEFT);
    }
}

void render_resource_identity(const ResourcePanel& panel,
                              const WormUiComposeResourceView& resource) {
    const auto& view = *panel.view;
    auto* context = panel.context;
    auto* action = panel.action;
    std::array<char, 320> label{};
    std::snprintf(label.data(), label.size(), "%s%s%s%s · %s",
                  view.selected_assigned_resource_id == resource.resource_id ? "[Selected] " : "",
                  resource.selected_audio_asset || resource.selected_visual_asset ? "> " : "",
                  resource.target_index,
                  panel.state->armed_resource_id == resource.resource_id ? " *" : "",
                  resource.name);
    nk_layout_row_dynamic(context, 25, 1);
    if (!resource.audio_asset && !resource.image_asset) {
        nk_label(context, label.data(), NK_TEXT_LEFT);
    } else {
        if (resource.missing) nk_widget_disable_begin(context);
        const auto clicked = nk_button_label(context, label.data());
        if (resource.missing) nk_widget_disable_end(context);
        if (clicked && !resource.missing && !resource.selected_audio_asset &&
            !resource.selected_visual_asset && action->kind == WORM_UI_WORKSPACE_ACTION_NONE) {
            action->kind = resource.audio_asset ? WORM_UI_WORKSPACE_ACTION_SELECT_AUDIO_ASSET
                                                : WORM_UI_WORKSPACE_ACTION_SELECT_VISUAL_ASSET;
            if (resource.audio_asset) action->audio_asset_id = resource.asset_id;
            else action->visual_asset_id = resource.asset_id;
        }
    }
    if (resource.source_name[0] == '\0') return;
    nk_layout_row_dynamic(context, 18, 1);
    nk_label(context, resource.source_name, NK_TEXT_LEFT);
}

void render_resource_usage_and_arm(const ResourcePanel& panel,
                                   const WormUiComposeResourceView& resource) {
    auto* context = panel.context;
    auto* action = panel.action;
    auto* state = panel.state;
    const auto available = panel.view->scene_available;
    nk_layout_row_begin(context, NK_DYNAMIC, 24, 2);
    nk_layout_row_push(context, 0.50f);
    if (!available) nk_widget_disable_begin(context);
    const auto uses_clicked = nk_button_label(context, "Uses");
    if (!available) nk_widget_disable_end(context);
    if (uses_clicked && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
        action->compose_resource_id = resource.resource_id;
        action->compose_action = WORM_UI_COMPOSE_ACTION_SELECT_RESOURCE_USES;
    }
    nk_layout_row_push(context, 0.50f);
    if (resource.missing) nk_widget_disable_begin(context);
    const auto arm_clicked = nk_button_label(
        context, state->armed_resource_id == resource.resource_id ? "Armed" : "Arm");
    if (resource.missing) nk_widget_disable_end(context);
    if (arm_clicked) {
        state->armed_resource_id = resource.resource_id;
        state->armed_from_sounds_dock = false;
        state->resource_pointer_mode = worm_ui_nuklear_compose_resource_pointer_mode_t::off;
    }
    nk_layout_row_end(context);
}

void render_export_copy(const ResourcePanel& panel,
                        const WormUiComposeResourceView& resource,
                        const WormUiComposeOutputGroupView* export_group) {
    auto* context = panel.context;
    auto* action = panel.action;
    if (resource.missing || resource.asset_id == 0) return;
    nk_layout_row_dynamic(context, 22, 1);
    if (nk_button_label(context, "Copy source into project") &&
        action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
        action->compose_resource_asset_id = resource.asset_id;
        action->compose_action = WORM_UI_COMPOSE_ACTION_COPY_RESOURCE_TO_PROJECT;
    }
    if (!export_group || export_group->relative_asset_directory[0] == '\0') return;
    const auto destination = std::filesystem::path(export_group->relative_asset_directory) /
        (std::to_string(resource.asset_id) + "-" +
         std::filesystem::path(resource.source_name).filename().string());
    const auto destination_text = destination.generic_string();
    std::array<char, 200> destination_label{};
    std::snprintf(destination_label.data(), destination_label.size(), "Export copy: %s",
                  destination_text.c_str());
    nk_layout_row_dynamic(context, 17, 1);
    nk_label(context, destination_label.data(), NK_TEXT_LEFT);
    nk_layout_row_dynamic(context, 22, 1);
    if (nk_button_label(context, "Copy source into export folder") &&
        action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
        action->compose_resource_asset_id = resource.asset_id;
        action->compose_output_group_index = export_group->document_index;
        action->compose_action = WORM_UI_COMPOSE_ACTION_COPY_RESOURCE_TO_EXPORT;
    }
}

void render_resource_row(const ResourcePanel& panel,
                         const WormUiComposeResourceView& resource,
                         const WormUiComposeOutputGroupView* export_group) {
    render_resource_identity(panel, resource);
    render_resource_usage_and_arm(panel, resource);
    render_export_copy(panel, resource, export_group);
    if (resource.missing) {
        nk_layout_row_dynamic(panel.context, 17, 1);
        nk_label(panel.context, "Missing source", NK_TEXT_LEFT);
    }
}

void render_resource_help(const ResourcePanel& panel, std::size_t matched_count) {
    const auto& view = *panel.view;
    auto* context = panel.context;
    const auto filter = panel.state->resource_filter;
    if (view.resources_truncated) {
        nk_layout_row_dynamic(context, 18, 1);
        nk_label(context, "Additional indexed resources are hidden by the view cap.", NK_TEXT_LEFT);
    }
    if (matched_count != 0) {
        nk_layout_row_dynamic(context, 18, 1);
        nk_label(context, filter == worm_ui_nuklear_compose_resource_filter_t::bga
            ? "Click an image to select it for BGA creation."
            : "Click audio to select its waveform.", NK_TEXT_LEFT);
        nk_layout_row_dynamic(context, 18, 1);
        nk_label(context, "Arm a resource, then Assign selected.", NK_TEXT_LEFT);
    }
}

void render_resource_list(const ResourcePanel& panel,
                          const WormUiComposeOutputGroupView* export_group) {
    const auto& view = *panel.view;
    const auto [role_count, matched_count] = count_visible_resources(panel);
    render_resource_summary(panel, role_count, matched_count);
    const auto role = filter_role(panel.state->resource_filter);
    for (std::size_t index = 0; index < view.resource_count; ++index) {
        const auto& resource = view.resources[index];
        if ((resource.roles & role) == 0 ||
            !resource_matches(resource, panel.state->resource_search.data())) continue;
        render_resource_row(panel, resource, export_group);
    }
    render_resource_help(panel, matched_count);
}

auto filter_role(worm_ui_nuklear_compose_resource_filter_t filter) -> std::uint8_t {
    switch (filter) {
    case worm_ui_nuklear_compose_resource_filter_t::sample:
        return WORM_UI_COMPOSE_RESOURCE_ROLE_SAMPLE;
    case worm_ui_nuklear_compose_resource_filter_t::bgm:
        return WORM_UI_COMPOSE_RESOURCE_ROLE_BGM;
    case worm_ui_nuklear_compose_resource_filter_t::bga:
        return WORM_UI_COMPOSE_RESOURCE_ROLE_BGA;
    }
    return WORM_UI_COMPOSE_RESOURCE_ROLE_SAMPLE;
}

auto filter_name(worm_ui_nuklear_compose_resource_filter_t filter) -> const char* {
    switch (filter) {
    case worm_ui_nuklear_compose_resource_filter_t::sample:
        return "Samples";
    case worm_ui_nuklear_compose_resource_filter_t::bgm:
        return "BGM";
    case worm_ui_nuklear_compose_resource_filter_t::bga:
        return "BGA";
    }
    return "Samples";
}

auto contains_ascii_case_insensitive(std::string_view value, std::string_view query) -> bool {
    if (query.empty()) {
        return true;
    }
    const auto equal = [](char left, char right) {
        return std::tolower(static_cast<unsigned char>(left)) ==
               std::tolower(static_cast<unsigned char>(right));
    };
    return std::search(value.begin(), value.end(), query.begin(), query.end(), equal) !=
           value.end();
}

auto resource_matches(const WormUiComposeResourceView& resource, const char* query) -> bool {
    const std::string_view needle{query ? query : ""};
    return contains_ascii_case_insensitive(resource.target_index, needle) ||
           contains_ascii_case_insensitive(resource.name, needle) ||
           contains_ascii_case_insensitive(resource.source_name, needle);
}

void SDLCALL compose_file_dialog_callback(void* userdata,
                                          const char* const* filelist,
                                          int /*selected_filter*/) {
    std::unique_ptr<std::uint64_t> generation{
        static_cast<std::uint64_t*>(userdata)};
    if (!generation) {
        return;
    }
    if (!filelist) {
        const auto* error = SDL_GetError();
        worm_ui_nuklear_compose_file_dialog_complete(
            *generation, {}, error && *error ? error : "File picker failed.", false);
    } else if (!filelist[0]) {
        worm_ui_nuklear_compose_file_dialog_complete(*generation, {}, {}, true);
    } else {
        std::size_t length{};
        while (length <= WORM_UI_COMPOSE_IMPORT_PATH_BYTES &&
               filelist[0][length] != '\0') {
            ++length;
        }
        if (length > WORM_UI_COMPOSE_IMPORT_PATH_BYTES) {
            worm_ui_nuklear_compose_file_dialog_complete(
                *generation, {}, "Selected file path exceeds the import limit.", false);
        } else {
            worm_ui_nuklear_compose_file_dialog_complete(
                *generation, std::string{filelist[0], length}, {}, false);
        }
    }
}

} // namespace

void worm_ui_nuklear_render_compose_resources(struct nk_context* context,
                                              const WormUiComposeView* view,
                                              WormUiWorkspaceAction* action,
                                              worm_ui_nuklear_compose_state_t* state) {
    if (!context || !view || !action || !state) {
        return;
    }
    ResourcePanel panel{context, view, action, state};
    apply_file_dialog_result(*state);
    update_selection_state(*view, *state);
    if (!nk_tree_push(context, NK_TREE_TAB, "Samples / BGM / BGA", NK_MAXIMIZED)) {
        return;
    }

    const auto* export_group = render_export_group(panel);

    render_filter_and_search(panel);
    const auto* armed = armed_resource(panel);
    render_armed_placement_controls(panel, armed);
    render_bmson_controls(panel);
    render_create_resource(panel);
    render_import_controls(panel);
    render_resource_list(panel, export_group);
    nk_tree_pop(context);
}
