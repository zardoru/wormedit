#include "ui/nuklear/bms_branch_panel.h"

#include "wormedit/nuklear_config.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

void set_choice_action(WormUiWorkspaceAction* action, std::uint64_t branch, std::int64_t value) {
    if (!action || action->compose_action != WORM_UI_COMPOSE_ACTION_NONE) {
        return;
    }
    action->compose_action = WORM_UI_COMPOSE_ACTION_SET_BMS_BRANCH_CHOICE;
    action->compose_branch_id = branch;
    action->compose_branch_value = value;
}

void set_clear_action(WormUiWorkspaceAction* action, std::uint64_t branch) {
    if (!action || action->compose_action != WORM_UI_COMPOSE_ACTION_NONE) {
        return;
    }
    action->compose_action = WORM_UI_COMPOSE_ACTION_CLEAR_BMS_BRANCH_CHOICE;
    action->compose_branch_id = branch;
    action->compose_branch_value = 0;
}

void choice_button(struct nk_context* context, const char* label, bool enabled,
                   WormUiWorkspaceAction* action, std::uint64_t branch, std::int64_t value) {
    if (!enabled) {
        nk_widget_disable_begin(context);
    }
    const auto clicked = nk_button_label(context, label) != 0;
    if (!enabled) {
        nk_widget_disable_end(context);
    }
    if (clicked && enabled) {
        set_choice_action(action, branch, value);
    }
}

} // namespace

bool worm_ui_nuklear_bms_branch_panel_visible(const WormUiComposeView* view) {
    return view && (view->branch_outline_available || view->branch_choice_count != 0 ||
                    view->branch_row_count != 0 || view->branch_diagnostic_count != 0 ||
                    view->branch_rows_truncated || view->branch_choices_truncated ||
                    view->branch_display_text_truncated || view->branch_diagnostics_truncated);
}

float worm_ui_nuklear_bms_branch_panel_height(const WormUiComposeView* view) {
    return worm_ui_nuklear_bms_branch_panel_visible(view) ? 168.0f : 0.0f;
}

void worm_ui_nuklear_render_bms_branch_panel(struct nk_context* context,
                                             const WormUiComposeView* view,
                                             WormUiWorkspaceAction* action) {
    if (!context || !view || !action || !worm_ui_nuklear_bms_branch_panel_visible(view)) {
        return;
    }

    nk_layout_row_dynamic(context, worm_ui_nuklear_bms_branch_panel_height(view), 1);
    if (!nk_group_begin(context, "BMS branch controls", NK_WINDOW_BORDER | NK_WINDOW_TITLE)) {
        return;
    }

    for (size_t index = 0; index < view->branch_choice_count; ++index) {
        const auto& choice = view->branch_choices[index];
        char identity[96]{};
        if (choice.has_source_order) {
            std::snprintf(identity, sizeof(identity), "%s #%zu",
                          choice.kind == WORM_UI_COMPOSE_BRANCH_SWITCH ? "SWITCH" : "RANDOM",
                          choice.source_order);
        } else {
            std::snprintf(identity, sizeof(identity), "%s",
                          choice.kind == WORM_UI_COMPOSE_BRANCH_SWITCH ? "SWITCH" : "RANDOM");
        }
        char current[64]{};
        if (choice.has_selected_value) {
            std::snprintf(current, sizeof(current), "value %lld",
                          static_cast<long long>(choice.selected_value));
        } else {
            std::snprintf(current, sizeof(current), "%s",
                          choice.active ? "unresolved" : "inactive");
        }
        char previous[64]{};
        char next[64]{};
        std::snprintf(previous, sizeof(previous), "< %lld",
                      static_cast<long long>(choice.previous_value));
        std::snprintf(next, sizeof(next), "%s%lld", choice.has_selected_value ? "> " : "Select ",
                      static_cast<long long>(choice.next_value));

        nk_layout_row_begin(context, NK_DYNAMIC, 21.0f, 2);
        nk_layout_row_push(context, 0.50f);
        nk_label(context, identity, NK_TEXT_LEFT);
        nk_layout_row_push(context, 0.50f);
        nk_label(context, current, NK_TEXT_RIGHT);
        nk_layout_row_end(context);

        nk_layout_row_begin(context, NK_DYNAMIC, 24.0f, 3);
        nk_layout_row_push(context, 0.34f);
        choice_button(context, choice.has_previous_value ? previous : "—",
                      choice.editable && choice.has_previous_value, action, choice.branch_id,
                      choice.previous_value);
        nk_layout_row_push(context, 0.34f);
        choice_button(context, choice.has_next_value ? next : "—",
                      choice.editable && choice.has_next_value, action, choice.branch_id,
                      choice.next_value);
        nk_layout_row_push(context, 0.32f);
        if (!choice.can_clear) {
            nk_widget_disable_begin(context);
        }
        const auto clear = nk_button_label(context, "Clear") != 0;
        if (!choice.can_clear) {
            nk_widget_disable_end(context);
        }
        if (clear && choice.can_clear) {
            set_clear_action(action, choice.branch_id);
        }
        nk_layout_row_end(context);
    }

    if (view->branch_choice_count != 0 && view->branch_row_count != 0) {
        nk_layout_row_dynamic(context, 4.0f, 1);
        nk_rule_horizontal(context, nk_rgb(68, 75, 92), nk_false);
    }
    for (size_t index = 0; index < view->branch_row_count; ++index) {
        const auto& row = view->branch_rows[index];
        std::string label;
        label.reserve(std::min<size_t>(row.depth, 32) * 2 + 96 + strlen(row.display_text));
        label.append(std::min<size_t>(row.depth, 32), ' ');
        label += row.active ? "▶ " : "· ";
        if (row.selected) {
            label += "selected · ";
        } else if (!row.active) {
            label += "inactive · ";
        }
        if (row.has_source_order) {
            label += "#" + std::to_string(row.source_order) + " ";
        }
        label += row.display_text;
        if (row.display_text_truncated) {
            label += " …";
        }
        const auto color = row.selected ? nk_rgb(252, 221, 128)
                           : row.active ? nk_rgb(145, 222, 172)
                                        : nk_rgb(147, 154, 170);
        nk_style_push_color(context, &context->style.text.color, color);
        nk_layout_row_dynamic(context, 18.0f, 1);
        nk_label(context, label.c_str(), NK_TEXT_LEFT);
        nk_style_pop_color(context);
    }
    if (view->branch_rows_truncated || view->branch_choices_truncated ||
        view->branch_display_text_truncated || view->branch_diagnostics_truncated) {
        nk_layout_row_dynamic(context, 18.0f, 1);
        nk_label(context, "Additional branch data was truncated by a configured capacity.",
                 NK_TEXT_LEFT);
    }
    for (size_t index = 0; index < view->branch_diagnostic_count; ++index) {
        if (view->branch_diagnostics[index]) {
            nk_layout_row_dynamic(context, 18.0f, 1);
            nk_labelf(context, NK_TEXT_LEFT, "Branch diagnostic: %s",
                      view->branch_diagnostics[index]);
        }
    }
    nk_group_end(context);
}
