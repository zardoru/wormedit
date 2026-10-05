#include "ui/nuklear/compose_resources.h"
#include "ui/nuklear/chrome.h"
#include "ui/nuklear/popup_lifecycle.h"

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
    // The Sounds dock owns the armed sound; painted notes must not re-arm their own sound over
    // the advanced one.
    if (state.armed_from_sounds_dock || state.armed_from_bga_dock) return;

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

auto begin_export_group_popup(const ResourcePanel& panel, const char* label) -> bool {
    auto* context = panel.context;
    auto& state = *panel.state;
    const auto was_open = state.resource_export_group_popup_open;
    (void)wormedit::ui::chrome::popup_update_release_pending(
        context, state.resource_export_group_release_pending);
    const auto current = wormedit::ui::chrome::PopupContextStamp{
        panel.view->project_id, panel.view->chart_id, panel.view->document_revision, 0};
    const auto captured = wormedit::ui::chrome::PopupContextStamp{
        state.resource_export_group_project_id, state.resource_export_group_chart_id,
        state.resource_export_group_revision, 0};
    const auto began = (!state.resource_export_group_release_pending || was_open) &&
        wormedit::ui::chrome::safe_combo_begin_label(
            context, label, nk_vec2(nk_widget_width(context), 220.0f));
    state.resource_export_group_popup_open = began;
    if (was_open && !wormedit::ui::chrome::popup_context_matches(captured, current)) {
        if (began) {
            nk_combo_close(context);
            nk_combo_end(context);
        }
        state.resource_export_group_popup_open = false;
        wormedit::ui::chrome::popup_arm_release(state.resource_export_group_release_pending);
        return false;
    }
    if (began && !was_open) {
        state.resource_export_group_project_id = current.project_id;
        state.resource_export_group_chart_id = current.chart_id;
        state.resource_export_group_revision = current.revision;
    }
    if (!began && was_open &&
        (nk_input_is_mouse_pressed(&context->input, NK_BUTTON_LEFT) ||
         nk_input_is_mouse_pressed(&context->input, NK_BUTTON_RIGHT) ||
         nk_input_is_mouse_released(&context->input, NK_BUTTON_LEFT) ||
         nk_input_is_mouse_released(&context->input, NK_BUTTON_RIGHT)))
        wormedit::ui::chrome::popup_arm_release(state.resource_export_group_release_pending);
    return began;
}

auto dismiss_export_group_popup(const ResourcePanel& panel, bool was_open) -> bool {
    auto* context = panel.context;
    auto& state = *panel.state;
    if (!wormedit::ui::chrome::popup_dismiss_requested(
            context, nk_window_get_bounds(context), was_open)) return false;
    nk_combo_close(context);
    nk_combo_end(context);
    state.resource_export_group_popup_open = false;
    wormedit::ui::chrome::popup_arm_release(state.resource_export_group_release_pending);
    return true;
}

void render_export_group_choices(const ResourcePanel& panel,
                                 const WormUiComposeOutputGroupView*& selected) {
    auto* context = panel.context;
    auto& state = *panel.state;
    nk_layout_row_dynamic(context, 22, 1);
    for (std::size_t index = 0; index < panel.view->output_group_count; ++index) {
        const auto& candidate = panel.view->output_groups[index];
        if (!nk_combo_item_label(context,
                candidate.name[0] ? candidate.name : candidate.relative_output_path,
                NK_TEXT_LEFT)) continue;
        state.export_group_document_index = candidate.document_index;
        selected = &candidate;
        wormedit::ui::chrome::popup_arm_release(state.resource_export_group_release_pending);
    }
}

void render_export_group_popup_controls(const ResourcePanel& panel,
                                        const WormUiComposeOutputGroupView& selected) {
    auto* context = panel.context;
    auto& state = *panel.state;
    auto* action = panel.action;
    int overwrite = state.export_overwrite ? 1 : 0;
    nk_layout_row_dynamic(context, 20, 1);
    if (nk_checkbox_label(context, "Replace existing chart files", &overwrite))
        wormedit::ui::chrome::popup_arm_release(state.resource_export_group_release_pending);
    state.export_overwrite = overwrite != 0;
    nk_layout_row_dynamic(context, 24, 1);
    if (!nk_button_label(context, "Export selected group") ||
        action->compose_action != WORM_UI_COMPOSE_ACTION_NONE) return;
    action->compose_output_group_index = selected.document_index;
    action->compose_export_overwrite = state.export_overwrite;
    action->compose_action = WORM_UI_COMPOSE_ACTION_EXPORT_OUTPUT_GROUP;
    state.resource_export_group_popup_open = false;
    wormedit::ui::chrome::popup_arm_release(state.resource_export_group_release_pending);
    nk_combo_close(context);
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
        const auto was_open = state->resource_export_group_popup_open;
        const auto began = begin_export_group_popup(panel, label.data());
        if (began) {
            if (dismiss_export_group_popup(panel, was_open)) return export_group;
            render_export_group_choices(panel, export_group);
            render_export_group_popup_controls(panel, *export_group);
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

void render_filter_and_search(const ResourcePanel& panel) {
    auto* context = panel.context;
    auto* state = panel.state;
    nk_layout_row_dynamic(context, 24, 2);
    constexpr std::array filters{
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
    if (state->resource_filter == worm_ui_nuklear_compose_resource_filter_t::sample) {
        nk_layout_row_dynamic(context, 18, 1);
        nk_label(context, "Select samples in the Sounds dock.", NK_TEXT_LEFT);
        return;
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
        state->armed_from_sounds_dock = false;
        state->armed_from_bga_dock = false;
        state->bga_auto_increment_on_place = false;
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
    return can_place && !view.note_paint_mode_active &&
           !view.semantic_layer_paint_mode_active && !view.timing_stage_active &&
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
    if (panel.state->resource_filter == worm_ui_nuklear_compose_resource_filter_t::sample) return;
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
                               : "Create BGM from waveform";
    const auto clicked = nk_button_label(context, label);
    if (!can_create) nk_widget_disable_end(context);
    if (!clicked || !can_create || action->compose_action != WORM_UI_COMPOSE_ACTION_NONE) return;
    action->compose_resource_asset_id = asset_id;
    action->compose_resource_role = visual ? WORM_UI_COMPOSE_RESOURCE_ROLE_BGA
                                           : WORM_UI_COMPOSE_RESOURCE_ROLE_BGM;
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
        ? "Link image as BGA" : "Link audio as BGM";
    nk_layout_row_dynamic(context, 24, 1);
    const auto clicked = nk_button_label(context, label);
    if (!can_import) nk_widget_disable_end(context);
    if (!clicked || !can_import || action->compose_action != WORM_UI_COMPOSE_ACTION_NONE) return;
    std::snprintf(action->compose_import_path, sizeof(action->compose_import_path), "%s",
                  state->resource_import_path.data());
    action->compose_resource_role = filter == worm_ui_nuklear_compose_resource_filter_t::bga
        ? WORM_UI_COMPOSE_RESOURCE_ROLE_BGA : WORM_UI_COMPOSE_RESOURCE_ROLE_BGM;
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
        state->armed_from_bga_dock = false;
        state->bga_auto_increment_on_place = false;
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
    if (state->resource_filter != worm_ui_nuklear_compose_resource_filter_t::sample) {
        render_create_resource(panel);
        render_import_controls(panel);
        render_resource_list(panel, export_group);
    }
    nk_tree_pop(context);
}
