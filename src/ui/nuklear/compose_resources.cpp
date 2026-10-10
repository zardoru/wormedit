#include "ui/nuklear/compose_resources.h"
#include "ui/nuklear/chrome.h"
#include "ui/nuklear/popup_lifecycle.h"

#include "wormedit/nuklear_config.h"

#include <array>
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
            wormedit::ui::chrome::safe_combo_end(context);
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
    wormedit::ui::chrome::safe_combo_end(context);
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
            wormedit::ui::chrome::safe_combo_end(context);
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

} // namespace

// The inspector no longer lists resources: sounds, BGA and BMSON tools live in their docks, music is
// linked in Metadata, and copies are made there. What stays is the arming that follows the selected
// object, and the output-group export, which has no other home yet.
void worm_ui_nuklear_render_compose_resources(struct nk_context* context,
                                              const WormUiComposeView* view,
                                              WormUiWorkspaceAction* action,
                                              worm_ui_nuklear_compose_state_t* state) {
    if (!context || !view || !action || !state) {
        return;
    }
    ResourcePanel panel{context, view, action, state};
    update_selection_state(*view, *state);
    if (!nk_tree_push(context, NK_TREE_TAB, "Export", NK_MAXIMIZED)) {
        return;
    }
    (void)render_export_group(panel);
    nk_tree_pop(context);
}
