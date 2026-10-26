#include "ui/nuklear/compose/compose_resources.h"

#include "core/text_util.h"
#include "ui/nuklear/chrome/chrome.h"
#include "ui/nuklear/chrome/popup_lifecycle.h"

#include "wormedit/nuklear_config.h"

#include <array>
#include <algorithm>
#include <string>
#include <string_view>
#include <cstdio>

namespace {

struct ResourcePanel {
    nk_context* context;
    const wormedit::ui::compose_view_t* view;
    wormedit::ui::workspace_action_t* action;
    worm_ui_nuklear_compose_state_t* state;
};

auto find_resource(const wormedit::ui::compose_view_t& view, std::uint64_t id)
    -> const wormedit::ui::compose_resource_view_t* {
    for (std::size_t index = 0; index < view.resources.size(); ++index) {
        if (view.resources[index].resource_id.value == id) return &view.resources[index];
    }
    return nullptr;
}

void update_selection_state(const wormedit::ui::compose_view_t& view,
                            worm_ui_nuklear_compose_state_t& state) {
    const auto selected_object_id = view.selected_bgm_event_id.value != 0
        ? view.selected_bgm_event_id.value : view.selected_note_label_note_id.value;
    const auto changed = state.resource_picker.resource_selection_chart_id != view.chart_id.value ||
        state.resource_picker.resource_selection_object_id != selected_object_id ||
        state.resource_picker.resource_selection_assigned_id != view.selected_assigned_resource_id.value;
    if (!changed) return;
    state.resource_picker.resource_selection_chart_id = view.chart_id.value;
    state.resource_picker.resource_selection_object_id = selected_object_id;
    state.resource_picker.resource_selection_assigned_id = view.selected_assigned_resource_id.value;
    if (selected_object_id == 0 || view.selected_assigned_resource_id.value == 0) return;
    // The Sounds dock owns the armed sound; painted notes must not re-arm their own sound over
    // the advanced one.
    if (state.keysound_catalog.armed_from_sounds_dock || state.bga_catalog.armed_from_bga_dock) return;

    set_armed_resource(state, view.selected_assigned_resource_id.value, compose_resource_origin_t::selection);
    const auto* assigned = find_resource(view, view.selected_assigned_resource_id.value);
    if (assigned) {
        const auto preferred = view.selected_bgm_event_id.value != 0
            ? WORM_UI_COMPOSE_RESOURCE_ROLE_BGM : WORM_UI_COMPOSE_RESOURCE_ROLE_SAMPLE;
        if ((assigned->roles & preferred) != 0) {
            state.resource_picker.resource_filter = view.selected_bgm_event_id.value != 0
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

auto find_export_group(const wormedit::ui::compose_view_t& view,
                       worm_ui_nuklear_compose_state_t& state)
    -> const wormedit::ui::compose_output_group_view_t* {
    for (std::size_t index = 0; index < view.output_groups.size(); ++index) {
        if (view.output_groups[index].document_index == state.dialogs.export_group_document_index)
            return &view.output_groups[index];
    }
    if (view.output_groups.size() == 0) return nullptr;
    state.dialogs.export_group_document_index = view.output_groups[0].document_index;
    return &view.output_groups[0];
}

void render_export_group_choices(const ResourcePanel& panel) {
    nk_layout_row_dynamic(panel.context, 26, 1);
    for (std::size_t index = 0; index < panel.view->output_groups.size(); ++index) {
        const auto& candidate = panel.view->output_groups[index];
        int selected = panel.state->dialogs.export_group_document_index == candidate.document_index;
        if (nk_selectable_label(panel.context,
                candidate.name[0] ? candidate.name.c_str() : candidate.relative_output_path.c_str(),
                NK_TEXT_LEFT, &selected))
            panel.state->dialogs.export_group_document_index = candidate.document_index;
    }
}

void stamp_action(const wormedit::ui::compose_view_t& view, wormedit::ui::workspace_action_t& action) {
    action.compose_input_project_id.value = view.project_id.value;
    action.compose_input_chart_id.value = view.chart_id.value;
    action.compose_input_document_revision = view.document_revision;
}

auto action_available(const wormedit::ui::workspace_action_t& action) -> bool {
    return action.kind == WORM_UI_WORKSPACE_ACTION_NONE && action.compose_inputs.size() == 0 &&
        (action.compose_action == WORM_UI_COMPOSE_ACTION_NONE ||
         action.compose_action == WORM_UI_COMPOSE_ACTION_SET_SEMANTIC_LAYER_WINDOW);
}

void close_export(worm_ui_nuklear_compose_state_t& state);

auto existing_files_label(WormUiExistingFilesChoice policy) -> const char* {
    switch (policy) {
    case WORM_UI_EXISTING_FILES_ASK:
        return "Ask before replacing";
    case WORM_UI_EXISTING_FILES_REPLACE:
        return "Replace existing";
    case WORM_UI_EXISTING_FILES_KEEP:
        return "Keep existing";
    case WORM_UI_EXISTING_FILES_SAVED:
        break;
    }
    return "Use saved policy";
}

void publish_export_action(const ResourcePanel& panel, const wormedit::ui::compose_output_group_view_t& selected,
                           WormUiExistingFilesChoice policy) {
    stamp_action(*panel.view, *panel.action);
    panel.action->compose_action = WORM_UI_COMPOSE_ACTION_EXPORT_OUTPUT_GROUP;
    panel.action->compose_output_group_index = selected.document_index;
    panel.action->compose_existing_files = policy;
    close_export(*panel.state);
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
            wormedit::ui::chrome::PopupContextStamp::from(view))) {
        close_export(state);
        return;
    }
    if (chrome::dialog_begin(dialog, panel.context, "Export output group", "Export", {.width = 560})) {
        nk_layout_row_dynamic(panel.context, 20, 1);
        nk_label(panel.context, "Output group", NK_TEXT_LEFT);
        // A scrollable list keeps long catalogs from pushing the footer offscreen.
        if (view.output_groups.size() != 0) {
            const auto list_height = std::min({156.0f,
                34.0f * static_cast<float>(view.output_groups.size()) + 8.0f,
                std::max(26.0f, wormedit::ui::nuklear::tooltip::viewport().h - 230.0f)});
            nk_layout_row_dynamic(panel.context, list_height, 1);
            if (nk_group_begin(panel.context, "Export groups", 0)) {
                render_export_group_choices(panel);
                nk_group_end(panel.context);
            }
        }
        const auto* selected = find_export_group(view, state);
        chrome::dialog_text(dialog, panel.context,
            selected ? selected->relative_output_path.c_str() : "No output group for this chart",
            chrome::color(chrome::UiColor::text_muted));
        nk_layout_row_dynamic(panel.context, 24, 1);
        nk_labelf(panel.context, NK_TEXT_LEFT, "Saved policy: %s",
                  selected ? existing_files_label(selected->existing_files) : "Unavailable");
        chrome::dialog_footer_begin(dialog, panel.context, 4);
        if (chrome::dialog_button(dialog, panel.context, "Cancel", chrome::DialogButtonRole::secondary))
            close_export(state);
        if (chrome::dialog_button(dialog, panel.context, "Export",
                                  chrome::DialogButtonRole::primary,
                                  selected && action_available(*panel.action)))
            publish_export_action(panel, *selected, WORM_UI_EXISTING_FILES_SAVED);
        if (chrome::dialog_button(dialog, panel.context, "Replace",
                                  chrome::DialogButtonRole::secondary,
                                  selected && action_available(*panel.action)))
            publish_export_action(panel, *selected, WORM_UI_EXISTING_FILES_REPLACE);
        if (chrome::dialog_button(dialog, panel.context, "Keep",
                                  chrome::DialogButtonRole::secondary,
                                  selected && action_available(*panel.action)))
            publish_export_action(panel, *selected, WORM_UI_EXISTING_FILES_KEEP);
    }
    if (chrome::dialog_end(dialog, panel.context) == chrome::DialogResult::cancel) close_export(state);
}

void render_export_confirmation(const ResourcePanel& panel) {
    namespace chrome = wormedit::ui::chrome;
    auto& dialog = panel.state->dialogs.export_confirmation_dialog;
    if (!panel.view->export_confirmation_required) {
        dialog = {};
        return;
    }
    if (chrome::dialog_begin(dialog, panel.context, "Confirm export replacement",
                             panel.view->export_confirmation_message.data(),
                             {.width = 620.0f, .enter_confirms = false})) {
        chrome::dialog_text(dialog, panel.context,
                            "These files already exist. Replace them for this run?",
                            chrome::color(chrome::UiColor::text));
        chrome::dialog_text(dialog, panel.context, panel.view->export_confirmation_detail.data(),
                            chrome::color(chrome::UiColor::text_muted));
        chrome::dialog_footer_begin(dialog, panel.context, 2, true);
        if (chrome::dialog_button(dialog, panel.context, "Replace",
                                  chrome::DialogButtonRole::destructive)) {
            stamp_action(*panel.view, *panel.action);
            panel.action->compose_action = WORM_UI_COMPOSE_ACTION_CONFIRM_OUTPUT_EXPORT;
        }
        if (chrome::dialog_button(dialog, panel.context, "Cancel",
                                  chrome::DialogButtonRole::secondary)) {
            stamp_action(*panel.view, *panel.action);
            panel.action->compose_action = WORM_UI_COMPOSE_ACTION_CANCEL_OUTPUT_EXPORT;
        }
    }
    if (chrome::dialog_end(dialog, panel.context) == chrome::DialogResult::cancel) {
        stamp_action(*panel.view, *panel.action);
        panel.action->compose_action = WORM_UI_COMPOSE_ACTION_CANCEL_OUTPUT_EXPORT;
    }
}

void render_layer_creation(const ResourcePanel& panel) {
    namespace chrome = wormedit::ui::chrome;
    auto& state = *panel.state;
    if (!state.layer_editor.layer_creation_context) return;
    const auto& captured = *state.layer_editor.layer_creation_context;
    const auto& view = *panel.view;
    if (captured.project != view.project_id.value || captured.chart != view.chart_id.value ||
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
        const auto valid = !wormedit::text_util::trim(
            state.layer_editor.layer_creation_name.data(), " \t\r\n").empty();
        chrome::dialog_footer_begin(dialog, panel.context, 2);
        if (chrome::dialog_button(dialog, panel.context, "Cancel", chrome::DialogButtonRole::secondary))
            state.layer_editor.layer_creation_context.reset();
        if (chrome::dialog_button(dialog, panel.context, "Create", chrome::DialogButtonRole::primary,
                                 valid && action_available(*panel.action))) {
            stamp_action(view, *panel.action);
            panel.action->compose_action = WORM_UI_COMPOSE_ACTION_ADD_SEMANTIC_LAYER;
            panel.action->compose_semantic_layer_name = state.layer_editor.layer_creation_name.data();
            state.layer_editor.layer_creation_context.reset();
        }
    }
    if (chrome::dialog_end(dialog, panel.context) == chrome::DialogResult::cancel)
        state.layer_editor.layer_creation_context.reset();
    if (!state.layer_editor.layer_creation_context)
        chrome::popup_arm_release(state.dialogs.compose_dialog_release_pending);
}

} // namespace

void worm_ui_nuklear_open_export_dialog(const wormedit::ui::compose_view_t& view,
    worm_ui_nuklear_compose_state_t& state) {
    state.dialogs.export_dialog_open = true;
    state.dialogs.export_dialog_project_id = view.project_id.value;
    state.dialogs.export_dialog_chart_id = view.chart_id.value;
    state.dialogs.export_dialog_revision = view.document_revision;
    state.dialogs.export_dialog = {};
    (void)find_export_group(view, state);
}

void worm_ui_nuklear_open_layer_creation(const wormedit::ui::compose_view_t& view,
    worm_ui_nuklear_compose_state_t& state) {
    state.layer_editor.layer_creation_context = {view.project_id.value, view.chart_id.value, view.document_revision};
    state.layer_editor.layer_creation_dialog = {};
    state.layer_editor.layer_creation_focus_pending = true;
    std::string name = "Layer";
    for (std::size_t suffix = 2; std::ranges::any_of(
            std::span{view.semantic_layers},
            [&](const auto& layer) { return name == layer.name; }); ++suffix)
        name = "Layer " + std::to_string(suffix);
    std::snprintf(state.layer_editor.layer_creation_name.data(), state.layer_editor.layer_creation_name.size(), "%s", name.c_str());
}

void worm_ui_nuklear_render_compose_dialogs(nk_context* context, const wormedit::ui::compose_view_t& view,
    wormedit::ui::workspace_action_t& action, worm_ui_nuklear_compose_state_t& state) {
    ResourcePanel panel{context, &view, &action, &state};
    state.preview_audio.render(context, view, action);
    render_export_dialog(panel);
    render_export_confirmation(panel);
    render_layer_creation(panel);
}

void worm_ui_nuklear_render_compose_resources(nk_context* context, const wormedit::ui::compose_view_t* view,
    wormedit::ui::workspace_action_t* action, worm_ui_nuklear_compose_state_t* state) {
    if (!context || !view || !action || !state) return;
    update_selection_state(*view, *state);
}
