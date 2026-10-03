#include "ui/nuklear/bms_branch_panel.h"

#include "wormedit/nuklear_config.h"
#include "ui/nuklear/chrome.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

constexpr std::size_t branch_statement_text_capacity = 4097;

struct branch_statement_editor_state_t {
    std::uint64_t project_id{};
    std::uint64_t chart_id{};
    std::uint64_t revision{};
    std::uint64_t selected_statement_id{};
    std::array<char, branch_statement_text_capacity> text{};
    bool source_text_too_large{};
};

void select_branch_statement(branch_statement_editor_state_t& state,
                             const WormUiComposeBranchRowView& row);
auto selected_branch_statement(const WormUiComposeView& view, std::uint64_t selected_id)
    -> const WormUiComposeBranchRowView*;

auto branch_statement_editor_state(const WormUiComposeView& view)
    -> branch_statement_editor_state_t& {
    static branch_statement_editor_state_t state;
    if (state.project_id != view.project_id || state.chart_id != view.chart_id) {
        state = {};
        state.project_id = view.project_id;
        state.chart_id = view.chart_id;
    }
    if (state.revision != view.document_revision) {
        state.revision = view.document_revision;
        if (const auto* row = selected_branch_statement(view, state.selected_statement_id)) {
            select_branch_statement(state, *row);
        } else {
            state.selected_statement_id = 0;
            state.text.fill('\0');
            state.source_text_too_large = false;
        }
    }
    return state;
}

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

void render_branch_choice(struct nk_context* context, const WormUiComposeBranchChoiceView& choice,
                          WormUiWorkspaceAction* action) {
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

auto render_branch_row(struct nk_context* context, const WormUiComposeBranchRowView& row,
                       std::uint64_t selected_statement_id) -> bool {
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
    nk_bool selected = row.selected ||
                               (row.statement_id != 0 && row.statement_id == selected_statement_id)
                           ? nk_true
                           : nk_false;
    nk_bool clicked = nk_false;
    if (row.statement_id != 0)
        clicked = nk_selectable_label(context, label.c_str(), NK_TEXT_LEFT, &selected);
    else
        nk_label(context, label.c_str(), NK_TEXT_LEFT);
    nk_style_pop_color(context);
    return clicked != 0;
}

void select_branch_statement(branch_statement_editor_state_t& state,
                             const WormUiComposeBranchRowView& row) {
    state.selected_statement_id = row.statement_id;
    state.text.fill('\0');
    state.source_text_too_large = row.statement_text_bytes >= state.text.size();
    if (!state.source_text_too_large && row.statement_text && row.statement_text_bytes != 0) {
        std::memcpy(state.text.data(), row.statement_text, row.statement_text_bytes);
        state.text[row.statement_text_bytes] = '\0';
    }
}

auto selected_branch_statement(const WormUiComposeView& view,
                               std::uint64_t selected_id)
    -> const WormUiComposeBranchRowView* {
    for (std::size_t index = 0; index < view.branch_row_count; ++index) {
        const auto& row = view.branch_rows[index];
        if (row.statement_id == selected_id && selected_id != 0) return &row;
    }
    return nullptr;
}

void set_statement_action(WormUiWorkspaceAction* action, const WormUiComposeView& view,
                          const branch_statement_editor_state_t& state,
                          WormUiComposeActionKind kind, bool include_text) {
    if (!action || action->compose_action != WORM_UI_COMPOSE_ACTION_NONE) return;
    action->compose_action = kind;
    action->compose_input_chart_id = view.chart_id;
    action->compose_input_document_revision = view.document_revision;
    action->compose_branch_statement_id = state.selected_statement_id;
    if (include_text) {
        action->compose_branch_statement_text = state.text.data();
        action->compose_branch_statement_text_bytes = std::strlen(state.text.data());
    }
}

void statement_button(struct nk_context* context, const char* label, bool enabled,
                      WormUiWorkspaceAction* action, const WormUiComposeView& view,
                      const branch_statement_editor_state_t& state,
                      WormUiComposeActionKind kind, bool include_text) {
    if (!enabled) nk_widget_disable_begin(context);
    const auto clicked = nk_button_label(context, label) != 0;
    if (!enabled) nk_widget_disable_end(context);
    if (clicked && enabled)
        set_statement_action(action, view, state, kind, include_text);
}

void render_branch_statement_editor(struct nk_context* context, const WormUiComposeView& view,
                                    WormUiWorkspaceAction* action,
                                    branch_statement_editor_state_t& state) {
    const auto* selected = selected_branch_statement(view, state.selected_statement_id);
    if (!selected) {
        state.selected_statement_id = 0;
        return;
    }

    nk_layout_row_dynamic(context, 4.0f, 1);
    nk_rule_horizontal(context, nk_rgb(68, 75, 92), nk_false);
    nk_layout_row_dynamic(context, 18.0f, 1);
    nk_label(context, "Selected raw statement", NK_TEXT_LEFT);
    const bool active = selected->active;
    const bool text_editable = !active && !state.source_text_too_large;
    if (!text_editable) nk_widget_disable_begin(context);
    nk_layout_row_dynamic(context, 26.0f, 1);
    (void)wormedit::ui::chrome::worm_ui_nuklear_text_field(
        context, NK_EDIT_FIELD, state.text.data(), static_cast<int>(state.text.size()),
        nk_filter_default);
    if (!text_editable) nk_widget_disable_end(context);

    if (active) {
        nk_layout_row_dynamic(context, 18.0f, 1);
        nk_label(context, "switch the branch choice first", NK_TEXT_LEFT);
    } else if (state.source_text_too_large) {
        nk_layout_row_dynamic(context, 18.0f, 1);
        nk_label(context, "Source line exceeds the bounded text editor; text changes are disabled.",
                 NK_TEXT_LEFT);
    }

    const bool has_text = state.text[0] != '\0';
    const auto current_text_matches = has_text && std::strlen(state.text.data()) == selected->statement_text_bytes &&
        std::memcmp(state.text.data(), selected->statement_text, selected->statement_text_bytes) == 0;
    const bool edit_enabled = !active && !state.source_text_too_large && has_text &&
                              !current_text_matches;
    const bool insert_enabled = !active && !state.source_text_too_large && has_text;
    nk_layout_row_begin(context, NK_DYNAMIC, 24.0f, 3);
    nk_layout_row_push(context, 0.30f);
    statement_button(context, "Edit text", edit_enabled, action, view, state,
                      WORM_UI_COMPOSE_ACTION_EDIT_BMS_BRANCH_STATEMENT, true);
    nk_layout_row_push(context, 0.40f);
    statement_button(context, "Insert after", insert_enabled, action, view, state,
                      WORM_UI_COMPOSE_ACTION_INSERT_BMS_BRANCH_STATEMENT, true);
    nk_layout_row_push(context, 0.30f);
    statement_button(context, "Delete", !active, action, view, state,
                      WORM_UI_COMPOSE_ACTION_DELETE_BMS_BRANCH_STATEMENT, false);
    nk_layout_row_end(context);
}

} // namespace

bool worm_ui_nuklear_bms_branch_panel_visible(const WormUiComposeView* view) {
    return view && (view->branch_outline_available || view->branch_choice_count != 0 ||
                    view->branch_row_count != 0 || view->branch_diagnostic_count != 0 ||
                    view->branch_rows_truncated || view->branch_choices_truncated ||
                    view->branch_display_text_truncated || view->branch_diagnostics_truncated);
}

float worm_ui_nuklear_bms_branch_panel_height(const WormUiComposeView* view) {
    return worm_ui_nuklear_bms_branch_panel_visible(view) ? 268.0f : 0.0f;
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
        render_branch_choice(context, view->branch_choices[index], action);
    }

    if (view->branch_choice_count != 0 && view->branch_row_count != 0) {
        nk_layout_row_dynamic(context, 4.0f, 1);
        nk_rule_horizontal(context, nk_rgb(68, 75, 92), nk_false);
    }
    auto& editor_state = branch_statement_editor_state(*view);
    for (size_t index = 0; index < view->branch_row_count; ++index) {
        const auto& row = view->branch_rows[index];
        if (render_branch_row(context, row, editor_state.selected_statement_id))
            select_branch_statement(editor_state, row);
    }
    render_branch_statement_editor(context, *view, action, editor_state);
    if (view->branch_rows_truncated || view->branch_choices_truncated ||
        view->branch_display_text_truncated || view->branch_diagnostics_truncated) {
        nk_layout_row_dynamic(context, 18.0f, 1);
        nk_label(context, "Additional branch data was truncated by a configured capacity.",
                 NK_TEXT_LEFT);
    }
    for (size_t index = 0; index < view->branch_diagnostic_count; ++index) {
        if (view->branch_diagnostics[index]) {
            std::string diagnostic{"Branch diagnostic: "};
            diagnostic.append(view->branch_diagnostics[index]);
            nk_layout_row_dynamic(context, 96.0f, 1);
            nk_label_wrap(context, diagnostic.c_str());
        }
    }
    nk_group_end(context);
}
