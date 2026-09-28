#include "ui/nuklear/compose_resources.h"

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
    if (auto picked = worm_ui_nuklear_compose_file_dialog_take()) {
        state->resource_import_error.fill('\0');
        if (!picked->error.empty()) {
            std::snprintf(state->resource_import_error.data(),
                          state->resource_import_error.size(), "%s",
                          picked->error.c_str());
        } else if (!picked->canceled) {
            if (picked->path.size() >= state->resource_import_path.size() ||
                !std::filesystem::path{picked->path}.is_absolute()) {
                std::snprintf(state->resource_import_error.data(),
                              state->resource_import_error.size(),
                              "Choose a bounded local filesystem path.");
            } else {
                std::snprintf(state->resource_import_path.data(),
                              state->resource_import_path.size(), "%s",
                              picked->path.c_str());
            }
        }
    }
    const auto selected_object_id = view->selected_bgm_event_id != 0
                                        ? view->selected_bgm_event_id
                                        : view->selected_note_label_note_id;
    if (state->resource_selection_chart_id != view->chart_id ||
        state->resource_selection_object_id != selected_object_id ||
        state->resource_selection_assigned_id != view->selected_assigned_resource_id) {
        state->resource_selection_chart_id = view->chart_id;
        state->resource_selection_object_id = selected_object_id;
        state->resource_selection_assigned_id = view->selected_assigned_resource_id;
        if (selected_object_id != 0 && view->selected_assigned_resource_id != 0) {
            state->armed_resource_id = view->selected_assigned_resource_id;
            const WormUiComposeResourceView* assigned = nullptr;
            for (size_t index = 0; index < view->resource_count; ++index) {
                if (view->resources[index].resource_id == view->selected_assigned_resource_id) {
                    assigned = &view->resources[index];
                    break;
                }
            }
            if (assigned) {
                const auto preferred = view->selected_bgm_event_id != 0
                                           ? WORM_UI_COMPOSE_RESOURCE_ROLE_BGM
                                           : WORM_UI_COMPOSE_RESOURCE_ROLE_SAMPLE;
                if ((assigned->roles & preferred) != 0) {
                    state->resource_filter = view->selected_bgm_event_id != 0
                                                 ? worm_ui_nuklear_compose_resource_filter_t::bgm
                                                 : worm_ui_nuklear_compose_resource_filter_t::sample;
                } else if ((assigned->roles & WORM_UI_COMPOSE_RESOURCE_ROLE_SAMPLE) != 0) {
                    state->resource_filter = worm_ui_nuklear_compose_resource_filter_t::sample;
                } else if ((assigned->roles & WORM_UI_COMPOSE_RESOURCE_ROLE_BGM) != 0) {
                    state->resource_filter = worm_ui_nuklear_compose_resource_filter_t::bgm;
                }
            }
            state->resource_search.fill('\0');
            state->resource_pointer_mode =
                worm_ui_nuklear_compose_resource_pointer_mode_t::off;
        }
    }
    if (!nk_tree_push(context, NK_TREE_TAB, "Samples / BGM / BGA", NK_MAXIMIZED)) {
        return;
    }

    const WormUiComposeOutputGroupView* export_group = nullptr;
    for (size_t index = 0; index < view->output_group_count; ++index) {
        if (view->output_groups[index].document_index == state->export_group_document_index) {
            export_group = &view->output_groups[index];
            break;
        }
    }
    if (!export_group && view->output_group_count != 0) {
        export_group = &view->output_groups[0];
        state->export_group_document_index = export_group->document_index;
    }
    nk_layout_row_dynamic(context, 22, 1);
    if (export_group) {
        std::array<char, 180> selected_label{};
        std::snprintf(selected_label.data(), selected_label.size(), "Export: %s",
                      export_group->name[0] ? export_group->name
                                            : export_group->relative_output_path);
        if (nk_combo_begin_label(context, selected_label.data(),
                                 nk_vec2(nk_widget_width(context), 220.0f))) {
            nk_layout_row_dynamic(context, 22, 1);
            for (size_t index = 0; index < view->output_group_count; ++index) {
                const auto& candidate = view->output_groups[index];
                if (nk_combo_item_label(context,
                                        candidate.name[0] ? candidate.name
                                                          : candidate.relative_output_path,
                                        NK_TEXT_LEFT)) {
                    state->export_group_document_index = candidate.document_index;
                    export_group = &candidate;
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
            }
            nk_combo_end(context);
        }
        nk_layout_row_dynamic(context, 17, 1);
        nk_label(context, export_group->relative_output_path, NK_TEXT_LEFT);
    } else {
        nk_label(context, "No output group for this chart", NK_TEXT_LEFT);
    }
    if (view->output_groups_truncated) {
        nk_layout_row_dynamic(context, 17, 1);
        nk_label(context, "Additional output groups are hidden by the view cap.", NK_TEXT_LEFT);
    }

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
        if (nk_button_label(context, label.data())) {
            if (state->resource_filter != candidate) {
                state->resource_pointer_mode =
                    worm_ui_nuklear_compose_resource_pointer_mode_t::off;
            }
            state->resource_filter = candidate;
        }
    }

    nk_layout_row_dynamic(context, 19, 1);
    nk_label(context, "Search", NK_TEXT_LEFT);
    nk_layout_row_begin(context, NK_DYNAMIC, 25, 2);
    nk_layout_row_push(context, 0.76f);
    (void)nk_edit_string_zero_terminated(context, NK_EDIT_FIELD, state->resource_search.data(),
                                         state->resource_search.size(), nk_filter_default);
    nk_layout_row_push(context, 0.24f);
    if (nk_button_label(context, "Clear")) {
        state->resource_search.fill('\0');
    }
    nk_layout_row_end(context);

    const WormUiComposeResourceView* armed = nullptr;
    for (std::size_t index = 0; index < view->resource_count; ++index) {
        if (view->resources[index].resource_id == state->armed_resource_id) {
            armed = &view->resources[index];
            break;
        }
    }
    nk_layout_row_dynamic(context, 19, 1);
    if (state->armed_resource_id == 0) {
        nk_label(context, "Armed: none", NK_TEXT_LEFT);
    } else if (armed) {
        nk_labelf(context, NK_TEXT_LEFT, "Armed: %s · %s · %zu selected",
                  armed->target_index, armed->name, view->selected_object_count);
    } else {
        nk_labelf(context, NK_TEXT_LEFT, "Armed resource #%llu is unavailable",
                  static_cast<unsigned long long>(state->armed_resource_id));
    }
    nk_layout_row_dynamic(context, 24, 2);
    const auto can_assign = view->scene_available && view->selected_object_count != 0 &&
                            armed && !armed->missing;
    if (!can_assign) {
        nk_widget_disable_begin(context);
    }
    const auto assign_clicked = nk_button_label(context, "Assign selected");
    if (!can_assign) {
        nk_widget_disable_end(context);
    }
    if (assign_clicked && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
        action->compose_resource_id = state->armed_resource_id;
        action->compose_action = WORM_UI_COMPOSE_ACTION_ASSIGN_RESOURCE_TO_SELECTION;
    }
    if (state->armed_resource_id == 0) {
        nk_widget_disable_begin(context);
    }
    const auto disarm_clicked = nk_button_label(context, "Disarm");
    if (state->armed_resource_id == 0) {
        nk_widget_disable_end(context);
    }
    if (disarm_clicked) {
        state->armed_resource_id = 0;
        state->auto_increment_on_place = false;
        state->resource_pointer_mode =
            worm_ui_nuklear_compose_resource_pointer_mode_t::off;
    }

    nk_layout_row_dynamic(context, 24, 1);
    const auto placement_role = filter_role(state->resource_filter);
    const auto can_place = view->scene_available && armed && !armed->missing &&
                           (armed->roles & placement_role) != 0;
    if (!can_place) {
        nk_widget_disable_begin(context);
    }
    const auto place_clicked =
        nk_button_label(context, state->resource_filter ==
                                     worm_ui_nuklear_compose_resource_filter_t::sample
                                 ? "Place Sample at cursor"
                                 : state->resource_filter ==
                                           worm_ui_nuklear_compose_resource_filter_t::bgm
                                       ? "Place BGM at cursor"
                                       : "Place BGA background at cursor");
    if (!can_place) {
        nk_widget_disable_end(context);
    }
    if (place_clicked && can_place &&
        action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
        action->compose_resource_id = armed->resource_id;
        action->compose_resource_role = placement_role;
        action->compose_action = WORM_UI_COMPOSE_ACTION_PLACE_RESOURCE;
    }
    const auto can_pointer_place = can_place && !view->note_paint_mode_active &&
                                   !view->semantic_layer_paint_mode_active &&
                                   !view->timing_stage_active &&
                                   (state->resource_filter !=
                                        worm_ui_nuklear_compose_resource_filter_t::bgm ||
                                    view->bgm_lane_count != 0);
    if (!can_pointer_place) {
        state->resource_pointer_mode =
            worm_ui_nuklear_compose_resource_pointer_mode_t::off;
        nk_widget_disable_begin(context);
    }
    nk_layout_row_dynamic(context, 24, 2);
    const auto place_mode_clicked = nk_button_label(
        context, state->resource_pointer_mode ==
                         worm_ui_nuklear_compose_resource_pointer_mode_t::place
                     ? "Place: On"
                     : "Place: Off");
    const auto replace_mode_clicked = nk_button_label(
        context, state->resource_pointer_mode ==
                         worm_ui_nuklear_compose_resource_pointer_mode_t::replace
                     ? "Replace: On"
                     : "Replace: Off");
    if (!can_pointer_place) {
        nk_widget_disable_end(context);
    }
    if (place_mode_clicked && can_pointer_place) {
        state->resource_pointer_mode =
            state->resource_pointer_mode == worm_ui_nuklear_compose_resource_pointer_mode_t::place
                ? worm_ui_nuklear_compose_resource_pointer_mode_t::off
                : worm_ui_nuklear_compose_resource_pointer_mode_t::place;
        state->bmson_razor_mode = worm_ui_nuklear_bmson_razor_mode_t::off;
    } else if (replace_mode_clicked && can_pointer_place) {
        state->resource_pointer_mode =
            state->resource_pointer_mode ==
                    worm_ui_nuklear_compose_resource_pointer_mode_t::replace
                ? worm_ui_nuklear_compose_resource_pointer_mode_t::off
                : worm_ui_nuklear_compose_resource_pointer_mode_t::replace;
        state->bmson_razor_mode = worm_ui_nuklear_bmson_razor_mode_t::off;
    }
    nk_layout_row_dynamic(context, 23, 1);
    if (!can_place) {
        nk_widget_disable_begin(context);
    }
    const auto auto_clicked = nk_button_label(
        context, state->auto_increment_on_place ? "Auto next after place: On"
                                                : "Auto next after place: Off");
    if (!can_place) {
        nk_widget_disable_end(context);
    }
    if (auto_clicked && can_place) {
        state->auto_increment_on_place = !state->auto_increment_on_place;
    }
    nk_layout_row_dynamic(context, 24, 2);
    const auto can_move_bmson = view->scene_available &&
                                 view->selected_bmson_bgm_event_id != 0;
    if (!can_move_bmson) {
        nk_widget_disable_begin(context);
    }
    const auto move_bmson_clicked = nk_button_label(
        context, "Move BMSON BGM to cursor");
    const auto delete_bmson_clicked = nk_button_label(context, "Delete BMSON BGM");
    if (!can_move_bmson) {
        nk_widget_disable_end(context);
    }
    if (move_bmson_clicked && can_move_bmson &&
        action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
        action->compose_bmson_bgm_event_id = view->selected_bmson_bgm_event_id;
        action->compose_action = WORM_UI_COMPOSE_ACTION_MOVE_BMSON_BGM_TO_CURSOR;
    } else if (delete_bmson_clicked && can_move_bmson &&
               action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
        action->compose_bmson_bgm_event_id = view->selected_bmson_bgm_event_id;
        action->compose_action = WORM_UI_COMPOSE_ACTION_DELETE_BMSON_BGM_MARKER;
    }
    nk_layout_row_dynamic(context, 24, 2);
    if (!can_move_bmson)
        nk_widget_disable_begin(context);
    const auto assign_to_key_clicked = nk_button_label(context, "BGM to active key lane");
    if (!can_move_bmson)
        nk_widget_disable_end(context);
    const auto can_assign_to_bgm = view->selected_bmson_note_event_id != 0;
    if (!can_assign_to_bgm)
        nk_widget_disable_begin(context);
    const auto assign_to_bgm_clicked = nk_button_label(context, "Selected tap to BGM");
    if (!can_assign_to_bgm)
        nk_widget_disable_end(context);
    if (action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
        if (assign_to_key_clicked && can_move_bmson) {
            action->compose_bmson_bgm_event_id = view->selected_bmson_bgm_event_id;
            action->compose_action = WORM_UI_COMPOSE_ACTION_ASSIGN_BMSON_BGM_TO_ACTIVE_LANE;
        } else if (assign_to_bgm_clicked && can_assign_to_bgm) {
            action->compose_bmson_bgm_event_id = view->selected_bmson_note_event_id;
            action->compose_action = WORM_UI_COMPOSE_ACTION_ASSIGN_BMSON_NOTE_TO_BGM;
        }
    }
    nk_layout_row_dynamic(context, 24, 2);
    if (!can_move_bmson) {
        nk_widget_disable_begin(context);
    }
    const auto insert_continue_clicked = nk_button_label(context, "Insert continue at cursor");
    const auto insert_restart_clicked = nk_button_label(context, "Insert restart at cursor");
    if (!can_move_bmson) {
        nk_widget_disable_end(context);
    }
    if ((insert_continue_clicked || insert_restart_clicked) && can_move_bmson &&
        action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
        action->compose_bmson_bgm_event_id = view->selected_bmson_bgm_event_id;
        action->compose_action = insert_restart_clicked
            ? WORM_UI_COMPOSE_ACTION_INSERT_BMSON_BGM_RESTART_AT_CURSOR
            : WORM_UI_COMPOSE_ACTION_INSERT_BMSON_BGM_CONTINUE_AT_CURSOR;
    }
    nk_layout_row_dynamic(context, 20, 1);
    nk_labelf(context, NK_TEXT_LEFT, "Staged BMSON markers: %zu",
              view->staged_bmson_marker_count);
    nk_layout_row_dynamic(context, 24, 2);
    if (!can_move_bmson) {
        nk_widget_disable_begin(context);
    }
    const auto stage_continue_clicked = nk_button_label(context, "Stage continue");
    const auto stage_restart_clicked = nk_button_label(context, "Stage restart");
    if (!can_move_bmson) {
        nk_widget_disable_end(context);
    }
    if ((stage_continue_clicked || stage_restart_clicked) && can_move_bmson &&
        action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
        action->compose_bmson_bgm_event_id = view->selected_bmson_bgm_event_id;
        action->compose_action = stage_restart_clicked
            ? WORM_UI_COMPOSE_ACTION_STAGE_BMSON_BGM_RESTART_AT_CURSOR
            : WORM_UI_COMPOSE_ACTION_STAGE_BMSON_BGM_CONTINUE_AT_CURSOR;
    }
    const auto can_razor = can_move_bmson && view->selected_bmson_source_lane_id != 0 &&
                           !view->note_paint_mode_active &&
                           !view->semantic_layer_paint_mode_active &&
                           !view->timing_stage_active;
    if (!can_razor) {
        state->bmson_razor_mode = worm_ui_nuklear_bmson_razor_mode_t::off;
        nk_widget_disable_begin(context);
    }
    nk_layout_row_dynamic(context, 24, 2);
    const auto razor_continue_clicked = nk_button_label(
        context, state->bmson_razor_mode == worm_ui_nuklear_bmson_razor_mode_t::continuation
                     ? "Razor continue: On" : "Razor continue: Off");
    const auto razor_restart_clicked = nk_button_label(
        context, state->bmson_razor_mode == worm_ui_nuklear_bmson_razor_mode_t::restart
                     ? "Razor restart: On" : "Razor restart: Off");
    if (!can_razor) {
        nk_widget_disable_end(context);
    }
    if (can_razor && (razor_continue_clicked || razor_restart_clicked)) {
        const auto choice = razor_restart_clicked
            ? worm_ui_nuklear_bmson_razor_mode_t::restart
            : worm_ui_nuklear_bmson_razor_mode_t::continuation;
        state->bmson_razor_mode = state->bmson_razor_mode == choice
            ? worm_ui_nuklear_bmson_razor_mode_t::off : choice;
        if (state->bmson_razor_mode != worm_ui_nuklear_bmson_razor_mode_t::off) {
            state->resource_pointer_mode =
                worm_ui_nuklear_compose_resource_pointer_mode_t::off;
        }
    }
    nk_layout_row_dynamic(context, 24, 2);
    if (view->staged_bmson_marker_count == 0) {
        nk_widget_disable_begin(context);
    }
    const auto commit_staged_clicked = nk_button_label(context, "Commit staged");
    const auto cancel_staged_clicked = nk_button_label(context, "Cancel staged");
    if (view->staged_bmson_marker_count == 0) {
        nk_widget_disable_end(context);
    }
    if (view->staged_bmson_marker_count != 0 &&
        action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
        if (commit_staged_clicked) {
            action->compose_action = WORM_UI_COMPOSE_ACTION_COMMIT_STAGED_BMSON_BGM;
        } else if (cancel_staged_clicked) {
            action->compose_action = WORM_UI_COMPOSE_ACTION_CANCEL_STAGED_BMSON_BGM;
        }
    }

    {
        nk_layout_row_dynamic(context, 24, 1);
        const auto visual_role =
            state->resource_filter == worm_ui_nuklear_compose_resource_filter_t::bga;
        const auto selected_asset = visual_role ? view->selected_visual_asset_id
                                                : view->selected_audio_asset_id;
        const auto can_create = selected_asset != 0;
        if (!can_create) {
            nk_widget_disable_begin(context);
        }
        const auto create_clicked = nk_button_label(
            context, visual_role ? "Create BGA from selected image"
                                 : state->resource_filter ==
                                           worm_ui_nuklear_compose_resource_filter_t::sample
                                       ? "Create Sample from waveform"
                                       : "Create BGM from waveform");
        if (!can_create) {
            nk_widget_disable_end(context);
        }
        if (create_clicked && can_create &&
            action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
            action->compose_resource_asset_id = selected_asset;
            action->compose_resource_role =
                visual_role ? WORM_UI_COMPOSE_RESOURCE_ROLE_BGA
                : state->resource_filter == worm_ui_nuklear_compose_resource_filter_t::sample
                    ? WORM_UI_COMPOSE_RESOURCE_ROLE_SAMPLE
                    : WORM_UI_COMPOSE_RESOURCE_ROLE_BGM;
            action->compose_action = WORM_UI_COMPOSE_ACTION_CREATE_RESOURCE;
        }
    }
    nk_layout_row_dynamic(context, 18, 1);
    nk_label(context, "External file (absolute path)", NK_TEXT_LEFT);
    nk_layout_row_begin(context, NK_DYNAMIC, 24, 2);
    nk_layout_row_push(context, 0.75f);
    (void)nk_edit_string_zero_terminated(
        context, NK_EDIT_FIELD, state->resource_import_path.data(),
        state->resource_import_path.size(), nk_filter_default);
    nk_layout_row_push(context, 0.25f);
    const auto dialog_open = worm_ui_nuklear_compose_file_dialog_open();
    if (dialog_open) {
        nk_widget_disable_begin(context);
    }
    const auto browse_clicked = nk_button_label(context, dialog_open ? "Choosing" : "Browse");
    if (dialog_open) {
        nk_widget_disable_end(context);
    }
    nk_layout_row_end(context);
    if (browse_clicked && !dialog_open) {
        if (const auto generation = worm_ui_nuklear_compose_file_dialog_begin()) {
            auto* token = new (std::nothrow) std::uint64_t{*generation};
            if (!token) {
                worm_ui_nuklear_compose_file_dialog_complete(
                    *generation, {}, "File picker could not allocate callback state.", false);
            } else {
                state->resource_import_error.fill('\0');
                SDL_ShowOpenFileDialog(compose_file_dialog_callback, token,
                                       SDL_GetKeyboardFocus(), nullptr, 0, nullptr, false);
            }
        }
    }
    if (state->resource_import_error[0] != '\0') {
        nk_layout_row_dynamic(context, 18, 1);
        nk_label(context, state->resource_import_error.data(), NK_TEXT_LEFT);
    }
    nk_layout_row_dynamic(context, 24, 1);
    const auto can_import = state->resource_import_path[0] != '\0';
    if (!can_import) {
        nk_widget_disable_begin(context);
    }
    const auto import_clicked = nk_button_label(
        context, state->resource_filter == worm_ui_nuklear_compose_resource_filter_t::bga
                     ? "Link image as BGA"
                     : state->resource_filter ==
                               worm_ui_nuklear_compose_resource_filter_t::bgm
                           ? "Link audio as BGM"
                           : "Link audio as Sample");
    if (!can_import) {
        nk_widget_disable_end(context);
    }
    if (import_clicked && can_import &&
        action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
        std::snprintf(action->compose_import_path, sizeof(action->compose_import_path),
                      "%s", state->resource_import_path.data());
        action->compose_resource_role = filter_role(state->resource_filter);
        action->compose_action = WORM_UI_COMPOSE_ACTION_IMPORT_RESOURCE_ASSET;
    }

    const auto role = filter_role(state->resource_filter);
    std::size_t role_count{};
    std::size_t matched_count{};
    for (std::size_t index = 0; index < view->resource_count; ++index) {
        if ((view->resources[index].roles & role) == 0) {
            continue;
        }
        ++role_count;
        matched_count +=
            resource_matches(view->resources[index], state->resource_search.data()) ? 1 : 0;
    }
    nk_layout_row_dynamic(context, 19, 1);
    nk_labelf(context, NK_TEXT_LEFT, "%s · %zu/%zu match · %zu indexed",
              filter_name(state->resource_filter), matched_count, role_count,
              view->resource_total_count);

    if (role_count == 0) {
        nk_layout_row_dynamic(context, 22, 1);
        nk_labelf(context, NK_TEXT_LEFT, "No %s resources are registered.",
                  filter_name(state->resource_filter));
    } else if (matched_count == 0) {
        nk_layout_row_dynamic(context, 22, 1);
        nk_label(context, "No resources match the search.", NK_TEXT_LEFT);
    }
    for (std::size_t index = 0; index < view->resource_count; ++index) {
        const auto& resource = view->resources[index];
        if ((resource.roles & role) == 0 ||
            !resource_matches(resource, state->resource_search.data())) {
            continue;
        }
        std::array<char, 320> label{};
        std::snprintf(label.data(), label.size(), "%s%s%s%s · %s",
                      view->selected_assigned_resource_id == resource.resource_id
                          ? "[Selected] "
                          : "",
                      resource.selected_audio_asset || resource.selected_visual_asset ? "> " : "",
                      resource.target_index,
                      state->armed_resource_id == resource.resource_id ? " *" : "",
                      resource.name);
        nk_layout_row_dynamic(context, 25, 1);
        if (resource.audio_asset || resource.image_asset) {
            if (resource.missing) {
                nk_widget_disable_begin(context);
            }
            const auto clicked = nk_button_label(context, label.data());
            if (resource.missing) {
                nk_widget_disable_end(context);
            }
            if (clicked && !resource.missing && !resource.selected_audio_asset &&
                !resource.selected_visual_asset &&
                action->kind == WORM_UI_WORKSPACE_ACTION_NONE) {
                action->kind = resource.audio_asset
                                   ? WORM_UI_WORKSPACE_ACTION_SELECT_AUDIO_ASSET
                                   : WORM_UI_WORKSPACE_ACTION_SELECT_VISUAL_ASSET;
                if (resource.audio_asset) {
                    action->audio_asset_id = resource.asset_id;
                } else {
                    action->visual_asset_id = resource.asset_id;
                }
            }
        } else {
            nk_label(context, label.data(), NK_TEXT_LEFT);
        }
        if (resource.source_name[0] != '\0') {
            nk_layout_row_dynamic(context, 18, 1);
            nk_label(context, resource.source_name, NK_TEXT_LEFT);
        }
        nk_layout_row_begin(context, NK_DYNAMIC, 24, 2);
        nk_layout_row_push(context, 0.50f);
        if (!view->scene_available) {
            nk_widget_disable_begin(context);
        }
        const auto uses_clicked = nk_button_label(context, "Uses");
        if (!view->scene_available) {
            nk_widget_disable_end(context);
        }
        if (uses_clicked && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
            action->compose_resource_id = resource.resource_id;
            action->compose_action = WORM_UI_COMPOSE_ACTION_SELECT_RESOURCE_USES;
        }
        nk_layout_row_push(context, 0.50f);
        if (resource.missing) {
            nk_widget_disable_begin(context);
        }
        const auto arm_clicked = nk_button_label(
            context, state->armed_resource_id == resource.resource_id ? "Armed" : "Arm");
        if (resource.missing) {
            nk_widget_disable_end(context);
        }
        if (arm_clicked) {
            state->armed_resource_id = resource.resource_id;
            state->resource_pointer_mode =
                worm_ui_nuklear_compose_resource_pointer_mode_t::off;
        }
        nk_layout_row_end(context);
        if (!resource.missing && resource.asset_id != 0) {
            nk_layout_row_dynamic(context, 22, 1);
            if (nk_button_label(context, "Copy source into project") &&
                action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
                action->compose_resource_asset_id = resource.asset_id;
                action->compose_action = WORM_UI_COMPOSE_ACTION_COPY_RESOURCE_TO_PROJECT;
            }
            if (export_group && export_group->relative_asset_directory[0] != '\0') {
                const auto destination =
                    std::filesystem::path(export_group->relative_asset_directory) /
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
        }
        if (resource.missing) {
            nk_layout_row_dynamic(context, 17, 1);
            nk_label(context, "Missing source", NK_TEXT_LEFT);
        }
    }
    if (view->resources_truncated) {
        nk_layout_row_dynamic(context, 18, 1);
        nk_label(context, "Additional indexed resources are hidden by the view cap.", NK_TEXT_LEFT);
    }
    if (matched_count != 0 &&
        state->resource_filter != worm_ui_nuklear_compose_resource_filter_t::bga) {
        nk_layout_row_dynamic(context, 18, 1);
        nk_label(context, "Click audio to select its waveform.", NK_TEXT_LEFT);
    }
    if (matched_count != 0 &&
        state->resource_filter == worm_ui_nuklear_compose_resource_filter_t::bga) {
        nk_layout_row_dynamic(context, 18, 1);
        nk_label(context, "Click an image to select it for BGA creation.", NK_TEXT_LEFT);
    }
    if (matched_count != 0) {
        nk_layout_row_dynamic(context, 18, 1);
        nk_label(context, "Arm a resource, then Assign selected.", NK_TEXT_LEFT);
    }
    nk_tree_pop(context);
}
