#include "ui/nuklear/bms_branch_panel.h"

#include "wormedit/nuklear_config.h"
#include "ui/nuklear/chrome.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace {

constexpr std::size_t branch_statement_text_capacity = 4097;

struct branch_statement_editor_state_t {
    std::uint64_t project_id{};
    std::uint64_t chart_id{};
    std::uint64_t revision{};
    std::uint64_t selected_statement_id{};
    // The control node whose unparsed statements are listed; zero until the first frame picks the root.
    std::uint64_t selected_node_id{};
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


struct branch_tree_context_t {
    struct nk_context* context;
    const WormUiComposeView& view;
    WormUiWorkspaceAction* action;
    branch_statement_editor_state_t& state;
};

auto branch_choice_for(const WormUiComposeView& view, std::uint64_t node_id)
    -> const WormUiComposeBranchChoiceView* {
    const auto choices = std::span(view.branch_choices, view.branch_choice_count);
    const auto found = std::ranges::find(choices, node_id, &WormUiComposeBranchChoiceView::branch_id);
    return found == choices.end() ? nullptr : &*found;
}

// A control node's children are the control rows that follow it one level deeper. Unparsed
// statements are not tree nodes; they are listed for the selected node below the tree.
auto branch_row_has_children(const WormUiComposeView& view, std::size_t index) -> bool {
    const auto& row = view.branch_rows[index];
    if (row.choice_block) return true;
    for (auto next = index + 1; next < view.branch_row_count && view.branch_rows[next].depth > row.depth; ++next)
        if (!view.branch_rows[next].raw_statement) return true;
    return false;
}

auto branch_node_title(const WormUiComposeBranchRowView& row) -> std::string {
    std::string title = row.active ? "" : "(inactive) ";
    if (row.has_source_order) title += "#" + std::to_string(row.source_order) + " ";
    title += row.display_text;
    if (row.display_text_truncated) title += " …";
    return title;
}

// Pushes one control node; true when it is open and its children should be drawn.
auto push_branch_node(const branch_tree_context_t& tree, const WormUiComposeBranchRowView& row,
                      bool container) -> bool {
    const auto title = branch_node_title(row);
    nk_bool selected = row.node_id == tree.state.selected_node_id ? nk_true : nk_false;
    nk_layout_row_dynamic(tree.context, 20.0f, 1);
    const auto color = row.active ? nk_rgb(145, 222, 172) : nk_rgb(147, 154, 170);
    nk_style_push_color(tree.context, &tree.context->style.text.color, color);
    auto open = false;
    if (container) {
        open = nk_tree_element_push_hashed(tree.context, NK_TREE_NODE, title.c_str(), NK_MAXIMIZED,
                                           &selected, reinterpret_cast<const char*>(&row.node_id),
                                           static_cast<int>(sizeof(row.node_id)), 0) != 0;
    } else {
        (void)nk_selectable_label(tree.context, title.c_str(), NK_TEXT_LEFT, &selected);
    }
    nk_style_pop_color(tree.context);
    if (selected) tree.state.selected_node_id = row.node_id;
    return open;
}

void render_branch_tree(const branch_tree_context_t& tree) {
    const auto& view = tree.view;
    std::vector<std::size_t> open_depths;
    const auto close_to = [&](std::size_t depth) {
        while (!open_depths.empty() && open_depths.back() >= depth) {
            nk_tree_element_pop(tree.context);
            open_depths.pop_back();
        }
    };
    for (std::size_t index = 0; index < view.branch_row_count; ++index) {
        const auto& row = view.branch_rows[index];
        if (row.raw_statement) continue;
        close_to(row.depth);
        const auto container = branch_row_has_children(view, index);
        const auto open = push_branch_node(tree, row, container);
        if (!container) continue;
        if (!open) {
            while (index + 1 < view.branch_row_count && view.branch_rows[index + 1].depth > row.depth) ++index;
            continue;
        }
        open_depths.push_back(row.depth);
        if (const auto* choice = row.choice_block ? branch_choice_for(view, row.node_id) : nullptr)
            render_branch_choice(tree.context, *choice, tree.action);
    }
    close_to(0);
}

auto selected_node_index(const WormUiComposeView& view, std::uint64_t node_id) -> std::optional<std::size_t> {
    for (std::size_t index = 0; index < view.branch_row_count; ++index)
        if (!view.branch_rows[index].raw_statement && view.branch_rows[index].node_id == node_id) return index;
    return std::nullopt;
}

// The selected node's own unparsed statements: raw rows one level deeper, before the next control row.
void render_node_statements(const branch_tree_context_t& tree) {
    const auto& view = tree.view;
    const auto selected = selected_node_index(view, tree.state.selected_node_id);
    if (!selected) return;
    const auto& node = view.branch_rows[*selected];
    nk_layout_row_dynamic(tree.context, 18.0f, 1);
    nk_label(tree.context, "Unparsed statements in the selected node", NK_TEXT_LEFT);
    bool any = false;
    for (auto index = *selected + 1; index < view.branch_row_count && view.branch_rows[index].depth > node.depth; ++index) {
        const auto& row = view.branch_rows[index];
        if (!row.raw_statement || row.depth != node.depth + 1) continue;
        any = true;
        if (render_branch_row(tree.context, row, tree.state.selected_statement_id))
            select_branch_statement(tree.state, row);
    }
    if (!any) {
        nk_layout_row_dynamic(tree.context, 18.0f, 1);
        nk_label(tree.context, "None: every statement here was understood.", NK_TEXT_LEFT);
    }
}


// Structure of the selected control node, derived from the outline rows: a leaf sits two levels
// below a RANDOM block (block -> if-group -> leaf), and its siblings are the leaves of that group.
struct selected_outline_node_t {
    bool block{};
    bool leaf{};
    bool has_previous_leaf{};
    bool has_next_leaf{};
};

auto outline_parent_index(const WormUiComposeView& view, std::size_t index) -> std::optional<std::size_t> {
    const auto depth = view.branch_rows[index].depth;
    for (auto candidate = index; candidate-- > 0;)
        if (!view.branch_rows[candidate].raw_statement && view.branch_rows[candidate].depth < depth)
            return candidate;
    return std::nullopt;
}

auto selected_outline_node(const WormUiComposeView& view, std::uint64_t node_id) -> selected_outline_node_t {
    selected_outline_node_t result;
    const auto index = selected_node_index(view, node_id);
    if (!index) return result;
    const auto& row = view.branch_rows[*index];
    result.block = row.choice_block;
    const auto group = outline_parent_index(view, *index);
    const auto block = group ? outline_parent_index(view, *group) : std::nullopt;
    if (!block || !view.branch_rows[*block].choice_block || view.branch_rows[*group].choice_block) return result;
    result.leaf = true;
    for (auto before = *index; before-- > *group + 1;)
        if (!view.branch_rows[before].raw_statement && view.branch_rows[before].depth == row.depth)
            result.has_previous_leaf = true;
    for (auto after = *index + 1; after < view.branch_row_count && view.branch_rows[after].depth > view.branch_rows[*group].depth; ++after)
        if (!view.branch_rows[after].raw_statement && view.branch_rows[after].depth == row.depth)
            result.has_next_leaf = true;
    return result;
}

void set_block_action(WormUiWorkspaceAction* action, const WormUiComposeView& view,
                      WormUiComposeActionKind kind, std::uint64_t node, std::uint64_t statement,
                      std::int64_t value) {
    if (!action || action->compose_action != WORM_UI_COMPOSE_ACTION_NONE) return;
    action->compose_action = kind;
    action->compose_input_chart_id = view.chart_id;
    action->compose_input_document_revision = view.document_revision;
    action->compose_branch_id = node;
    action->compose_branch_statement_id = statement;
    action->compose_branch_value = value;
}

void block_button(struct nk_context* context, const char* label, bool enabled,
                  const std::function<void()>& on_click) {
    if (!enabled) nk_widget_disable_begin(context);
    const auto clicked = nk_button_label(context, label) != 0;
    if (!enabled) nk_widget_disable_end(context);
    if (clicked && enabled) on_click();
}

// Block-level edits: wrap the selected raw statement, remove the selected empty block, and
// move the selected leaf. The application revalidates every identity before editing.
void render_branch_block_buttons(const branch_tree_context_t& tree) {
    const auto& view = tree.view;
    const auto node = selected_outline_node(view, tree.state.selected_node_id);
    const auto statement = selected_branch_statement(view, tree.state.selected_statement_id) != nullptr;
    const auto node_id = tree.state.selected_node_id;
    nk_layout_row_dynamic(tree.context, 4.0f, 1);
    nk_rule_horizontal(tree.context, nk_rgb(68, 75, 92), nk_false);
    nk_layout_row_dynamic(tree.context, 24.0f, 2);
    block_button(tree.context, "New block", statement, [&] {
        set_block_action(tree.action, view, WORM_UI_COMPOSE_ACTION_CREATE_BMS_BRANCH_BLOCK, 0,
                         tree.state.selected_statement_id, 0);
    });
    block_button(tree.context, "Remove block", node.block, [&] {
        set_block_action(tree.action, view, WORM_UI_COMPOSE_ACTION_REMOVE_BMS_BRANCH_BLOCK, node_id, 0, 0);
    });
    nk_layout_row_dynamic(tree.context, 24.0f, 2);
    block_button(tree.context, "Leaf up", node.leaf && node.has_previous_leaf, [&] {
        set_block_action(tree.action, view, WORM_UI_COMPOSE_ACTION_MOVE_BMS_BRANCH_LEAF, node_id, 0, -1);
    });
    block_button(tree.context, "Leaf down", node.leaf && node.has_next_leaf, [&] {
        set_block_action(tree.action, view, WORM_UI_COMPOSE_ACTION_MOVE_BMS_BRANCH_LEAF, node_id, 0, 1);
    });
}

} // namespace

bool worm_ui_nuklear_bms_branch_panel_visible(const WormUiComposeView* view) {
    return view && (view->branch_outline_available || view->branch_choice_count != 0 ||
                    view->branch_row_count != 0 || view->branch_diagnostic_count != 0 ||
                    view->branch_rows_truncated || view->branch_choices_truncated ||
                    view->branch_display_text_truncated || view->branch_diagnostics_truncated);
}

void worm_ui_nuklear_render_bms_branch_panel(struct nk_context* context,
                                             const WormUiComposeView* view,
                                             WormUiWorkspaceAction* action) {
    if (!context || !view || !action || !worm_ui_nuklear_bms_branch_panel_visible(view)) {
        return;
    }

    auto& editor_state = branch_statement_editor_state(*view);
    if (!selected_node_index(*view, editor_state.selected_node_id) && view->branch_row_count != 0) {
        const auto first = std::ranges::find_if(std::span(view->branch_rows, view->branch_row_count),
                                                [](const auto& row) { return !row.raw_statement; });
        editor_state.selected_node_id = first == std::span(view->branch_rows, view->branch_row_count).end()
                                            ? 0 : first->node_id;
    }
    const branch_tree_context_t tree{context, *view, action, editor_state};
    // Choices whose block row was cut by a capacity still get their controls.
    for (size_t index = 0; index < view->branch_choice_count; ++index) {
        const auto& choice = view->branch_choices[index];
        const auto shown = std::ranges::any_of(std::span(view->branch_rows, view->branch_row_count),
            [&](const auto& row) { return row.choice_block && row.node_id == choice.branch_id; });
        if (!shown) render_branch_choice(context, choice, action);
    }
    render_branch_tree(tree);
    render_branch_block_buttons(tree);
    render_node_statements(tree);
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
}
