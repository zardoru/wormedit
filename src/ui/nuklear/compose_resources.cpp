#include "ui/nuklear/compose_resources.h"
#include "ui/nuklear/chrome.h"
#include "ui/nuklear/popup_lifecycle.h"

#include "wormedit/nuklear_config.h"

#include <array>
#include <algorithm>
#include <string>
#include <string_view>
#include <cstdio>

namespace {

struct ResourcePanel {
    nk_context* context;
    const WormUiComposeView* view;
    WormUiWorkspaceAction* action;
    worm_ui_nuklear_compose_state_t* state;
};

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
    const auto changed = state.resource_picker.resource_selection_chart_id != view.chart_id ||
        state.resource_picker.resource_selection_object_id != selected_object_id ||
        state.resource_picker.resource_selection_assigned_id != view.selected_assigned_resource_id;
    if (!changed) return;
    state.resource_picker.resource_selection_chart_id = view.chart_id;
    state.resource_picker.resource_selection_object_id = selected_object_id;
    state.resource_picker.resource_selection_assigned_id = view.selected_assigned_resource_id;
    if (selected_object_id == 0 || view.selected_assigned_resource_id == 0) return;
    // The Sounds dock owns the armed sound; painted notes must not re-arm their own sound over
    // the advanced one.
    if (state.keysound_catalog.armed_from_sounds_dock || state.bga_catalog.armed_from_bga_dock) return;

    set_armed_resource(state, view.selected_assigned_resource_id, compose_resource_origin_t::selection);
    const auto* assigned = find_resource(view, view.selected_assigned_resource_id);
    if (assigned) {
        const auto preferred = view.selected_bgm_event_id != 0
            ? WORM_UI_COMPOSE_RESOURCE_ROLE_BGM : WORM_UI_COMPOSE_RESOURCE_ROLE_SAMPLE;
        if ((assigned->roles & preferred) != 0) {
            state.resource_picker.resource_filter = view.selected_bgm_event_id != 0
                ? worm_ui_nuklear_compose_resource_filter_t::bgm
                : worm_ui_nuklear_compose_resource_filter_t::sample;
        } else if ((assigned->roles & WORM_UI_COMPOSE_RESOURCE_ROLE_SAMPLE) != 0) {
            state.resource_picker.resource_filter = worm_ui_nuklear_compose_resource_filter_t::sample;
        } else if ((assigned->roles & WORM_UI_COMPOSE_RESOURCE_ROLE_BGM) != 0) {
            state.resource_picker.resource_filter = worm_ui_nuklear_compose_resource_filter_t::bgm;
        }
    }
    state.resource_picker.resource_search.fill('\0');
    state.resource_picker.resource_pointer_mode = worm_ui_nuklear_compose_resource_pointer_mode_t::off;
}

auto find_export_group(const WormUiComposeView& view,
                       worm_ui_nuklear_compose_state_t& state)
    -> const WormUiComposeOutputGroupView* {
    for (std::size_t index = 0; index < view.output_group_count; ++index) {
        if (view.output_groups[index].document_index == state.dialogs.export_group_document_index)
            return &view.output_groups[index];
    }
    if (view.output_group_count == 0) return nullptr;
    state.dialogs.export_group_document_index = view.output_groups[0].document_index;
    return &view.output_groups[0];
}

void render_export_group_choices(const ResourcePanel& panel) {
    nk_layout_row_dynamic(panel.context, 26, 1);
    for (std::size_t index = 0; index < panel.view->output_group_count; ++index) {
        const auto& candidate = panel.view->output_groups[index];
        int selected = panel.state->dialogs.export_group_document_index == candidate.document_index;
        if (nk_selectable_label(panel.context,
                candidate.name[0] ? candidate.name : candidate.relative_output_path,
                NK_TEXT_LEFT, &selected))
            panel.state->dialogs.export_group_document_index = candidate.document_index;
    }
}

void stamp_action(const WormUiComposeView& view, WormUiWorkspaceAction& action) {
    action.compose_input_project_id = view.project_id;
    action.compose_input_chart_id = view.chart_id;
    action.compose_input_document_revision = view.document_revision;
}

auto action_available(const WormUiWorkspaceAction& action) -> bool {
    return action.kind == WORM_UI_WORKSPACE_ACTION_NONE && action.compose_input_count == 0 &&
        (action.compose_action == WORM_UI_COMPOSE_ACTION_NONE ||
         action.compose_action == WORM_UI_COMPOSE_ACTION_SET_SEMANTIC_LAYER_WINDOW);
}

void close_export(worm_ui_nuklear_compose_state_t& state) {
    state.dialogs.export_dialog_open = false;
    wormedit::ui::chrome::popup_arm_release(state.dialogs.compose_dialog_release_pending);
}

void render_export_dialog(const ResourcePanel& panel) {
    namespace chrome = wormedit::ui::chrome;
    auto& state = *panel.state;
    auto& dialog = state.dialogs.export_dialog;
    const auto& view = *panel.view;
    if (!state.dialogs.export_dialog_open) return;
    const auto captured = chrome::PopupContextStamp{state.dialogs.export_dialog_project_id,
        state.dialogs.export_dialog_chart_id, state.dialogs.export_dialog_revision, 0};
    if (!chrome::popup_context_matches(captured,
            {view.project_id, view.chart_id, view.document_revision, 0})) {
        close_export(state);
        return;
    }
    if (chrome::dialog_begin(dialog, panel.context, "Export output group", "Export", {.width = 560})) {
        nk_layout_row_dynamic(panel.context, 20, 1);
        nk_label(panel.context, "Output group", NK_TEXT_LEFT);
        // A scrollable list keeps long catalogs from pushing the footer offscreen.
        if (view.output_group_count != 0) {
            const auto list_height = std::min({156.0f,
                34.0f * static_cast<float>(view.output_group_count) + 8.0f,
                std::max(26.0f, wormedit::ui::nuklear::tooltip::viewport().h - 230.0f)});
            nk_layout_row_dynamic(panel.context, list_height, 1);
            if (nk_group_begin(panel.context, "Export groups", 0)) {
                render_export_group_choices(panel);
                nk_group_end(panel.context);
            }
        }
        const auto* selected = find_export_group(view, state);
        chrome::dialog_text(dialog, panel.context,
            selected ? selected->relative_output_path : "No output group for this chart",
            chrome::color(chrome::UiColor::text_muted));
        int overwrite = state.dialogs.export_overwrite;
        nk_layout_row_dynamic(panel.context, 24, 1);
        nk_checkbox_label(panel.context, "Replace existing chart files", &overwrite);
        state.dialogs.export_overwrite = overwrite != 0;
        chrome::dialog_footer_begin(dialog, panel.context, 2, false, 140);
        if (chrome::dialog_button(dialog, panel.context, "Cancel", chrome::DialogButtonRole::secondary))
            close_export(state);
        if (chrome::dialog_button(dialog, panel.context, "Export group", chrome::DialogButtonRole::primary,
                                 selected && action_available(*panel.action))) {
            stamp_action(view, *panel.action);
            panel.action->compose_action = WORM_UI_COMPOSE_ACTION_EXPORT_OUTPUT_GROUP;
            panel.action->compose_output_group_index = selected->document_index;
            panel.action->compose_export_overwrite = state.dialogs.export_overwrite;
            close_export(state);
        }
    }
    if (chrome::dialog_end(dialog, panel.context) == chrome::DialogResult::cancel) close_export(state);
}

void render_layer_creation(const ResourcePanel& panel) {
    namespace chrome = wormedit::ui::chrome;
    auto& state = *panel.state;
    if (!state.layer_editor.layer_creation_context) return;
    const auto& captured = *state.layer_editor.layer_creation_context;
    const auto& view = *panel.view;
    if (captured.project != view.project_id || captured.chart != view.chart_id ||
        captured.revision != view.document_revision) {
        state.layer_editor.layer_creation_context.reset();
        return;
    }
    auto& dialog = state.layer_editor.layer_creation_dialog;
    if (chrome::dialog_begin(dialog, panel.context, "Create layer", "Create layer", {.width = 420})) {
        nk_layout_row_dynamic(panel.context, 20, 1);
        nk_label(panel.context, "Name", NK_TEXT_LEFT);
        nk_layout_row_dynamic(panel.context, 28, 1);
        chrome::autofocus_text_field(panel.context, static_cast<nk_flags>(NK_EDIT_FIELD) | NK_EDIT_AUTO_SELECT,
            state.layer_editor.layer_creation_name.data(), static_cast<int>(state.layer_editor.layer_creation_name.size()),
            nk_filter_default, state.layer_editor.layer_creation_focus_pending);
        const auto valid = std::string_view(state.layer_editor.layer_creation_name.data()).find_first_not_of(" \t\r\n") !=
            std::string_view::npos;
        chrome::dialog_footer_begin(dialog, panel.context, 2);
        if (chrome::dialog_button(dialog, panel.context, "Cancel", chrome::DialogButtonRole::secondary))
            state.layer_editor.layer_creation_context.reset();
        if (chrome::dialog_button(dialog, panel.context, "Create", chrome::DialogButtonRole::primary,
                                 valid && action_available(*panel.action))) {
            stamp_action(view, *panel.action);
            panel.action->compose_action = WORM_UI_COMPOSE_ACTION_ADD_SEMANTIC_LAYER;
            std::snprintf(panel.action->compose_semantic_layer_name,
                sizeof(panel.action->compose_semantic_layer_name), "%s", state.layer_editor.layer_creation_name.data());
            state.layer_editor.layer_creation_context.reset();
        }
    }
    if (chrome::dialog_end(dialog, panel.context) == chrome::DialogResult::cancel)
        state.layer_editor.layer_creation_context.reset();
    if (!state.layer_editor.layer_creation_context)
        chrome::popup_arm_release(state.dialogs.compose_dialog_release_pending);
}

} // namespace

void worm_ui_nuklear_open_export_dialog(const WormUiComposeView& view,
    worm_ui_nuklear_compose_state_t& state) {
    state.dialogs.export_dialog_open = true;
    state.dialogs.export_dialog_project_id = view.project_id;
    state.dialogs.export_dialog_chart_id = view.chart_id;
    state.dialogs.export_dialog_revision = view.document_revision;
    state.dialogs.export_dialog = {};
    (void)find_export_group(view, state);
}

void worm_ui_nuklear_open_layer_creation(const WormUiComposeView& view,
    worm_ui_nuklear_compose_state_t& state) {
    state.layer_editor.layer_creation_context = {view.project_id, view.chart_id, view.document_revision};
    state.layer_editor.layer_creation_dialog = {};
    state.layer_editor.layer_creation_focus_pending = true;
    std::string name = "Layer";
    for (std::size_t suffix = 2; std::ranges::any_of(
            std::span{view.semantic_layers, view.semantic_layer_count},
            [&](const auto& layer) { return name == layer.name; }); ++suffix)
        name = "Layer " + std::to_string(suffix);
    std::snprintf(state.layer_editor.layer_creation_name.data(), state.layer_editor.layer_creation_name.size(), "%s", name.c_str());
}

void worm_ui_nuklear_render_compose_dialogs(nk_context* context, const WormUiComposeView& view,
    WormUiWorkspaceAction& action, worm_ui_nuklear_compose_state_t& state) {
    ResourcePanel panel{context, &view, &action, &state};
    render_export_dialog(panel);
    render_layer_creation(panel);
}

void worm_ui_nuklear_render_compose_resources(nk_context* context, const WormUiComposeView* view,
    WormUiWorkspaceAction* action, worm_ui_nuklear_compose_state_t* state) {
    if (!context || !view || !action || !state) return;
    update_selection_state(*view, *state);
}
