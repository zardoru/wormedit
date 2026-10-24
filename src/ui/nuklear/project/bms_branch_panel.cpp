#include "ui/nuklear/project/bms_branch_panel.h"

#include "wormedit/nuklear_config.h"
#include "ui/nuklear/chrome/chrome.h"
#include "ui/nuklear/chrome/text_block.h"

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
    std::string node_title;
    std::string diagnostic_label;
    auto build_node_title(const wormedit::ui::compose_branch_row_view_t& row) -> const std::string& {
        node_title.assign(row.active ? "" : "(inactive) ");
        if (row.has_source_order) {
            char order[32]{};
            std::snprintf(order, sizeof(order), "#%zu ", row.source_order);
            node_title.append(order);
        }
        node_title.append(row.display_text);
        if (row.display_text_truncated) node_title.append(" …");
        return node_title;
    }

};

void select_branch_statement(branch_statement_editor_state_t& state,
                             const wormedit::ui::compose_branch_row_view_t& row);
auto selected_branch_statement(const wormedit::ui::compose_view_t& view, std::uint64_t selected_id)
    -> const wormedit::ui::compose_branch_row_view_t*;

auto branch_statement_editor_state(const wormedit::ui::compose_view_t& view)
    -> branch_statement_editor_state_t& {
    static branch_statement_editor_state_t state;
    if (state.project_id != view.project_id.value || state.chart_id != view.chart_id.value) {
        state = {};
        state.project_id = view.project_id.value;
        state.chart_id = view.chart_id.value;
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

void set_choice_action(wormedit::ui::workspace_action_t* action, std::uint64_t branch, std::int64_t value) {
    if (!action || action->compose_action != WORM_UI_COMPOSE_ACTION_NONE) {
        return;
    }
    action->compose_action = WORM_UI_COMPOSE_ACTION_SET_BMS_BRANCH_CHOICE;
    action->compose_branch_id.value = branch;
    action->compose_branch_value = value;
}

void set_clear_action(wormedit::ui::workspace_action_t* action, std::uint64_t branch) {
    if (!action || action->compose_action != WORM_UI_COMPOSE_ACTION_NONE) {
        return;
    }
    action->compose_action = WORM_UI_COMPOSE_ACTION_CLEAR_BMS_BRANCH_CHOICE;
    action->compose_branch_id.value = branch;
    action->compose_branch_value = 0;
}

void choice_button(struct nk_context* context, const char* label, bool enabled,
                   wormedit::ui::workspace_action_t* action, std::uint64_t branch, std::int64_t value) {
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

void render_branch_choice(struct nk_context* context, const wormedit::ui::compose_branch_choice_view_t& choice,
                          wormedit::ui::workspace_action_t* action) {
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
                  choice.editable && choice.has_previous_value, action, choice.branch_id.value,
                  choice.previous_value);
    nk_layout_row_push(context, 0.34f);
    choice_button(context, choice.has_next_value ? next : "—",
                  choice.editable && choice.has_next_value, action, choice.branch_id.value,
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
        set_clear_action(action, choice.branch_id.value);
    }
    nk_layout_row_end(context);
}

auto render_branch_row(struct nk_context* context, const wormedit::ui::compose_branch_row_view_t& row,
                       std::uint64_t selected_statement_id) -> bool {
    std::string label;
    label.reserve(std::min<size_t>(row.depth, 32) * 2 + 96 + strlen(row.display_text.c_str()));
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
    label += row.display_text.c_str();
    if (row.display_text_truncated) {
        label += " …";
    }
    const auto color = row.selected ? nk_rgb(252, 221, 128)
                       : row.active ? nk_rgb(145, 222, 172)
                                    : nk_rgb(147, 154, 170);
    nk_style_push_color(context, &context->style.text.color, color);
    nk_layout_row_dynamic(context, 18.0f, 1);
    nk_bool selected = row.selected ||
                               (row.statement_id.value != 0 && row.statement_id.value == selected_statement_id)
                           ? nk_true
                           : nk_false;
    nk_bool clicked = nk_false;
    if (row.statement_id.value != 0)
        clicked = nk_selectable_label(context, label.c_str(), NK_TEXT_LEFT, &selected);
    else
        nk_label(context, label.c_str(), NK_TEXT_LEFT);
    nk_style_pop_color(context);
    return clicked != 0;
}

void select_branch_statement(branch_statement_editor_state_t& state,
                             const wormedit::ui::compose_branch_row_view_t& row) {
    state.selected_statement_id = row.statement_id.value;
    state.text.fill('\0');
    state.source_text_too_large = row.statement_text.size() >= state.text.size();
    if (!state.source_text_too_large && !row.statement_text.empty()) {
        std::memcpy(state.text.data(), row.statement_text.data(), row.statement_text.size());
        state.text[row.statement_text.size()] = '\0';
    }
}

auto selected_branch_statement(const wormedit::ui::compose_view_t& view,
                               std::uint64_t selected_id)
    -> const wormedit::ui::compose_branch_row_view_t* {
    for (std::size_t index = 0; index < view.branch_rows.size(); ++index) {
        const auto& row = view.branch_rows[index];
        if (row.statement_id.value == selected_id && selected_id != 0) return &row;
    }
    return nullptr;
}

void set_statement_action(wormedit::ui::workspace_action_t* action, const wormedit::ui::compose_view_t& view,
                          const branch_statement_editor_state_t& state,
                          WormUiComposeActionKind kind, bool include_text) {
    if (!action || action->compose_action != WORM_UI_COMPOSE_ACTION_NONE) return;
    action->compose_action = kind;
    action->compose_input_chart_id.value = view.chart_id.value;
    action->compose_input_document_revision = view.document_revision;
    action->compose_branch_statement_id.value = state.selected_statement_id;
    if (include_text) {
        action->compose_branch_statement_text = state.text.data();
    }
}

void statement_button(struct nk_context* context, const char* label, bool enabled,
                      wormedit::ui::workspace_action_t* action, const wormedit::ui::compose_view_t& view,
                      const branch_statement_editor_state_t& state,
                      WormUiComposeActionKind kind, bool include_text) {
    if (!enabled) nk_widget_disable_begin(context);
    const auto clicked = nk_button_label(context, label) != 0;
    if (!enabled) nk_widget_disable_end(context);
    if (clicked && enabled)
        set_statement_action(action, view, state, kind, include_text);
}

void render_branch_statement_editor(struct nk_context* context, const wormedit::ui::compose_view_t& view,
                                    wormedit::ui::workspace_action_t* action,
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
    const auto current_text_matches = has_text && std::strlen(state.text.data()) == selected->statement_text.size() &&
        std::memcmp(state.text.data(), selected->statement_text.data(), selected->statement_text.size()) == 0;
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
    const wormedit::ui::compose_view_t& view;
    wormedit::ui::workspace_action_t* action;
    branch_statement_editor_state_t& state;
};

auto branch_choice_for(const wormedit::ui::compose_view_t& view, std::uint64_t node_id)
    -> const wormedit::ui::compose_branch_choice_view_t* {
    const auto choices = std::span(view.branch_choices);
    const auto found = std::ranges::find(choices, wormedit::branch_id_t{node_id}, &wormedit::ui::compose_branch_choice_view_t::branch_id);
    return found == choices.end() ? nullptr : &*found;
}

// A control node's children are the control rows that follow it one level deeper. Unparsed
// statements are not tree nodes; they are listed for the selected node below the tree.
auto branch_row_has_children(const wormedit::ui::compose_view_t& view, std::size_t index) -> bool {
    const auto& row = view.branch_rows[index];
    if (row.choice_block) return true;
    for (auto next = index + 1; next < view.branch_rows.size() && view.branch_rows[next].depth > row.depth; ++next)
        if (!view.branch_rows[next].raw_statement) return true;
    return false;
}

// Pushes one control node; true when it is open and its children should be drawn.
auto push_branch_node(const branch_tree_context_t& tree, const wormedit::ui::compose_branch_row_view_t& row,
                      bool container) -> bool {
    const auto& title = tree.state.build_node_title(row);
    nk_bool selected = row.node_id.value == tree.state.selected_node_id ? nk_true : nk_false;
    nk_layout_row_dynamic(tree.context, 20.0f, 1);
    const auto color = row.active ? nk_rgb(145, 222, 172) : nk_rgb(147, 154, 170);
    nk_style_push_color(tree.context, &tree.context->style.text.color, color);
    auto open = false;
    if (container) {
        open = nk_tree_element_push_hashed(tree.context, NK_TREE_NODE, title.c_str(), NK_MAXIMIZED,
                                           &selected, reinterpret_cast<const char*>(&row.node_id.value),
                                           static_cast<int>(sizeof(row.node_id.value)), 0) != 0;
    } else {
        (void)nk_selectable_label(tree.context, title.c_str(), NK_TEXT_LEFT, &selected);
    }
    nk_style_pop_color(tree.context);
    if (selected) tree.state.selected_node_id = row.node_id.value;
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
    for (std::size_t index = 0; index < view.branch_rows.size(); ++index) {
        const auto& row = view.branch_rows[index];
        if (row.raw_statement) continue;
        close_to(row.depth);
        const auto container = branch_row_has_children(view, index);
        const auto open = push_branch_node(tree, row, container);
        if (!container) continue;
        if (!open) {
            while (index + 1 < view.branch_rows.size() && view.branch_rows[index + 1].depth > row.depth) ++index;
            continue;
        }
        open_depths.push_back(row.depth);
        if (const auto* choice = row.choice_block ? branch_choice_for(view, row.node_id.value) : nullptr)
            render_branch_choice(tree.context, *choice, tree.action);
    }
    close_to(0);
}

auto selected_node_index(const wormedit::ui::compose_view_t& view, std::uint64_t node_id) -> std::optional<std::size_t> {
    for (std::size_t index = 0; index < view.branch_rows.size(); ++index)
        if (!view.branch_rows[index].raw_statement && view.branch_rows[index].node_id.value == node_id) return index;
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
    for (auto index = *selected + 1; index < view.branch_rows.size() && view.branch_rows[index].depth > node.depth; ++index) {
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

auto outline_parent_index(const wormedit::ui::compose_view_t& view, std::size_t index) -> std::optional<std::size_t> {
    const auto depth = view.branch_rows[index].depth;
    for (auto candidate = index; candidate-- > 0;)
        if (!view.branch_rows[candidate].raw_statement && view.branch_rows[candidate].depth < depth)
            return candidate;
    return std::nullopt;
}

auto selected_outline_node(const wormedit::ui::compose_view_t& view, std::uint64_t node_id) -> selected_outline_node_t {
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
    for (auto after = *index + 1; after < view.branch_rows.size() && view.branch_rows[after].depth > view.branch_rows[*group].depth; ++after)
        if (!view.branch_rows[after].raw_statement && view.branch_rows[after].depth == row.depth)
            result.has_next_leaf = true;
    return result;
}

void set_block_action(wormedit::ui::workspace_action_t* action, const wormedit::ui::compose_view_t& view,
                      WormUiComposeActionKind kind, std::uint64_t node, std::uint64_t statement,
                      std::int64_t value) {
    if (!action || action->compose_action != WORM_UI_COMPOSE_ACTION_NONE) return;
    action->compose_action = kind;
    action->compose_input_chart_id.value = view.chart_id.value;
    action->compose_input_document_revision = view.document_revision;
    action->compose_branch_id.value = node;
    action->compose_branch_statement_id.value = statement;
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

namespace {
using scope_operation_t = wormedit::ui::bms_program_operation_t;
using scope_kind_t = wormedit::ui::bms_program_node_kind_t;
using scope_view_t = wormedit::ui::bms_program_view_t;
constexpr nk_color scope_warning_color{252, 221, 128, 255};

void emit_scope_action(wormedit::ui::workspace_action_t& action, const scope_view_t& view,
    scope_operation_t operation, wormedit::branch_id_t node = {}, std::string text = {}) {
    if (action.compose_action != WORM_UI_COMPOSE_ACTION_NONE) return;
    action.compose_action = WORM_UI_COMPOSE_ACTION_BMS_PROGRAM;
    action.bms_program = {.project = view.project, .difficulty = view.difficulty, .revision = view.revision,
        .selected = view.selected, .operation = operation, .node = node, .text = std::move(text)};
}
void scope_warning_icon(nk_context* context, nk_color color) {
    struct nk_rect bounds{};
    if (!nk_widget(&bounds, context)) return;
    const auto x = bounds.x + bounds.w * 0.5f;
    const auto y = bounds.y + 3.0f;
    auto* canvas = nk_window_get_canvas(context);
    nk_stroke_triangle(canvas, x, y, x - 7, y + 14, x + 7, y + 14, 1.5f, color);
    nk_stroke_line(canvas, x, y + 4, x, y + 8, 1.5f, color);
    nk_fill_circle(canvas, nk_rect(x - 1, y + 10, 2, 2), color);
}
void scope_message(nk_context* context, const char* text, bool warning, bool error = false) {
    const auto color = error ? nk_rgb(255, 130, 130) : warning ? scope_warning_color : context->style.text.color;
    nk_style_push_color(context, &context->style.text.color, color);
    if (warning) {
        nk_layout_row_begin(context, NK_DYNAMIC, 20.0f, 2);
        nk_layout_row_push(context, 0.09f); scope_warning_icon(context, color);
        nk_layout_row_push(context, 0.91f); nk_label(context, "Warning", NK_TEXT_LEFT);
        nk_layout_row_end(context);
    }
    wormedit::ui::chrome::text_block_t{.text = text}.render(context);
    nk_style_pop_color(context);
}
void scope_button(nk_context* context, const char* label, const scope_view_t& view,
    wormedit::ui::workspace_action_t& action, scope_operation_t operation, bool enabled = true) {
    if (!enabled) nk_widget_disable_begin(context);
    const auto clicked = nk_button_label(context, label) != 0;
    if (!enabled) nk_widget_disable_end(context);
    if (clicked && enabled) { nk_edit_unfocus(context); emit_scope_action(action, view, operation); }
}
void scope_text_input(nk_context* context, const scope_view_t& view, wormedit::ui::workspace_action_t& action,
    const std::string& value, scope_operation_t operation, bool multiline = false) {
    constexpr std::size_t maximum = 1024U * 1024U;
    if (value.size() >= maximum) {
        scope_message(context, "Text exceeds the panel editor capacity; the original remains intact.", true);
        return;
    }
    std::vector<char> buffer(std::min(maximum, std::max(std::size_t{8192}, value.size() + 4096)), '\0');
    std::copy(value.begin(), value.end(), buffer.begin());
    nk_layout_row_dynamic(context, multiline ? 96.0f : 25.0f, 1);
    nk_edit_string_zero_terminated(context, multiline ? NK_EDIT_BOX : NK_EDIT_FIELD, buffer.data(),
        static_cast<int>(buffer.size()), nk_filter_default);
    if (value != buffer.data()) emit_scope_action(action, view, operation, {}, std::string{buffer.data()});
}
void render_scope_row(nk_context* context, const scope_view_t& view,
    const wormedit::ui::bms_program_row_t& row, wormedit::ui::workspace_action_t& action) {
    std::string text(std::min(row.depth, std::size_t{24}) * 2, ' ');
    if (row.kind != scope_kind_t::text && row.kind != scope_kind_t::set_random && row.kind != scope_kind_t::set_switch)
        text += row.unreachable ? "Warning: " : row.playback_active ? "Playing: " : "Inactive: ";
    text += row.label;
    const auto color = row.unreachable ? scope_warning_color : context->style.text.color;
    const std::array text_colors{&context->style.selectable.text_normal, &context->style.selectable.text_hover,
        &context->style.selectable.text_pressed, &context->style.selectable.text_normal_active,
        &context->style.selectable.text_hover_active, &context->style.selectable.text_pressed_active};
    for (auto* text_color : text_colors) nk_style_push_color(context, text_color, color);
    nk_layout_row_begin(context, NK_DYNAMIC, 23.0f, row.unreachable ? 2 : 1);
    if (row.unreachable) { nk_layout_row_push(context, 0.09f); scope_warning_icon(context, color); }
    nk_layout_row_push(context, row.unreachable ? 0.91f : 1.0f);
    nk_bool selected = row.selected;
    if (nk_selectable_label(context, text.c_str(), NK_TEXT_LEFT, &selected)) {
        nk_edit_unfocus(context);
        emit_scope_action(action, view, scope_operation_t::select, row.node);
    }
    nk_layout_row_end(context);
    for (std::size_t index = 0; index < text_colors.size(); ++index) nk_style_pop_color(context);
}
void render_scope_tree(nk_context* context, const scope_view_t& view, wormedit::ui::workspace_action_t& action) {
    nk_layout_row_dynamic(context, 230.0f, 1);
    if (!nk_group_begin(context, "Scope tree", NK_WINDOW_BORDER)) return;
    for (const auto& row : view.rows) render_scope_row(context, view, row, action);
    nk_group_end(context);
}
void render_scope_draft(nk_context* context, const scope_view_t& view, wormedit::ui::workspace_action_t& action) {
    if (view.stale_draft) scope_message(context, "The document changed. Reload before saving this draft.", true);
    if (view.has_parameter) {
        const auto selected = std::ranges::find(view.rows, view.selected, &wormedit::ui::bms_program_row_t::node);
        nk_layout_row_dynamic(context, 20.0f, 1);
        nk_label(context, selected != view.rows.end() && selected->kind == scope_kind_t::random ?
            "RANDOM range (outcome count)" : "Node value (DEF for default case)", NK_TEXT_LEFT);
        scope_text_input(context, view, action, view.parameter, scope_operation_t::draft_parameter);
    }
    if (view.has_text) scope_text_input(context, view, action, view.text, scope_operation_t::draft_text, true);
    if (view.has_parameter || view.has_text) {
        nk_layout_row_dynamic(context, 25.0f, 2);
        scope_button(context, "Save node", view, action, scope_operation_t::save, !view.stale_draft);
        scope_button(context, "Reload / discard", view, action, scope_operation_t::reload);
    }
    if (view.can_preview) {
        nk_layout_row_dynamic(context, 20.0f, 1);
        if (view.playback_choice) nk_labelf(context, NK_TEXT_LEFT, "Playback preview choice: %lld", static_cast<long long>(*view.playback_choice));
        else nk_label(context, "Playback preview choice: unresolved", NK_TEXT_LEFT);
        scope_text_input(context, view, action, view.preview, scope_operation_t::draft_preview);
        nk_layout_row_dynamic(context, 25.0f, 2);
        if (nk_button_label(context, "Set preview")) emit_scope_action(action, view, scope_operation_t::set_preview, {}, view.preview);
        scope_button(context, "Clear preview", view, action, scope_operation_t::clear_preview);
    }
}
void render_scope_add(nk_context* context, const scope_view_t& view, wormedit::ui::workspace_action_t& action) {
    nk_layout_row_dynamic(context, 25.0f, 1);
    if (!wormedit::ui::chrome::safe_combo_begin_label(context, "Add node…", nk_vec2(240, 260))) return;
    for (const auto& kind : wormedit::ui::bms_program_kinds) {
        if (!kind.can_add) continue;
        nk_layout_row_dynamic(context, 23, 1);
        if (!nk_button_label(context, kind.label.data())) continue;
        emit_scope_action(action, view, scope_operation_t::add);
        action.bms_program.kind = kind.kind;
        action.bms_program.parent = view.selected;
        // Runtime resolves a scope's last child, or the selected leaf's parent/anchor.
        nk_combo_close(context);
    }
    wormedit::ui::chrome::safe_combo_end(context);
}
void render_scope_move(nk_context* context, const scope_view_t& view, wormedit::ui::workspace_action_t& action) {
    nk_layout_row_dynamic(context, 25.0f, 1);
    if (!wormedit::ui::chrome::safe_combo_begin_label(context, "Move node…", nk_vec2(260, 250))) return;
    for (const auto& row : view.rows) {
        if (row.node == view.selected) continue;
        const auto inside = row.kind != scope_kind_t::chart && row.kind != scope_kind_t::text &&
            row.kind != scope_kind_t::set_random && row.kind != scope_kind_t::set_switch;
        const auto title = std::string{inside ? "Inside " : "After "} + row.path;
        nk_layout_row_dynamic(context, 23, 1);
        if (!nk_button_label(context, title.c_str())) continue;
        emit_scope_action(action, view, scope_operation_t::move, row.node);
        action.bms_program.parent = inside ? row.node : wormedit::branch_id_t{};
        action.bms_program.after = inside ? std::nullopt : std::optional{row.node};
        nk_combo_close(context);
    }
    wormedit::ui::chrome::safe_combo_end(context);
}
void render_scope_panel(nk_context* context, const scope_view_t& view, wormedit::ui::workspace_action_t& action) {
    render_scope_tree(context, view, action);
    scope_message(context, ("Selected node: " + view.target_path).c_str(), false);
    if (view.content_chart) {
        const auto row = std::ranges::find(view.rows, *view.content_chart, &wormedit::ui::bms_program_row_t::node);
        if (row != view.rows.end()) scope_message(context, ("Content target: " + row->path).c_str(), false);
    }
    scope_message(context, view.content_chart ? "Common and ancestor content is read-only (gray). Select its owning node to edit it." :
        "This node has no Chart content. Select a scope or Chart for content editing.", false);
    render_scope_draft(context, view, action);
    render_scope_add(context, view, action);
    render_scope_move(context, view, action);
    nk_layout_row_dynamic(context, 25.0f, 2);
    scope_button(context, "Delete…", view, action, scope_operation_t::prepare_delete, view.can_delete);
    scope_button(context, "Unwrap…", view, action, scope_operation_t::prepare_unwrap, view.can_unwrap);
    if (!view.confirmation.empty()) {
        scope_message(context, view.confirmation.c_str(), true);
        nk_layout_row_dynamic(context, view.unconditional_conversion ? 42.0f : 25.0f, 1);
        scope_button(context, view.unconditional_conversion ? "Make branches always apply" : "Confirm", view, action, view.confirmation_operation.value_or(scope_operation_t::cancel));
        nk_layout_row_dynamic(context, 25.0f, 1);
        scope_button(context, "Cancel", view, action, scope_operation_t::cancel);
    }
    for (const auto& message : view.messages) scope_message(context, message.text.c_str(), message.warning, message.error);
}
} // namespace

bool worm_ui_nuklear_bms_branch_panel_visible(const wormedit::ui::compose_view_t* view) {
    return view && (view->bms_program.available || view->branch_outline_available || view->branch_choices.size() != 0 ||
                    view->branch_rows.size() != 0 || view->branch_diagnostic_count() != 0 ||
                    view->branch_rows_truncated || view->branch_choices_truncated ||
                    view->branch_display_text_truncated || view->branch_diagnostics_truncated);
}

void worm_ui_nuklear_render_bms_branch_panel(struct nk_context* context,
                                             const wormedit::ui::compose_view_t* view,
                                             wormedit::ui::workspace_action_t* action) {
    if (!context || !view || !action || !worm_ui_nuklear_bms_branch_panel_visible(view)) {
        return;
    }

    if (view->bms_program.available) {
        render_scope_panel(context, view->bms_program, *action);
        return;
    }
    auto& editor_state = branch_statement_editor_state(*view);
    if (!selected_node_index(*view, editor_state.selected_node_id) && view->branch_rows.size() != 0) {
        const auto first = std::ranges::find_if(std::span(view->branch_rows),
                                                [](const auto& row) { return !row.raw_statement; });
        editor_state.selected_node_id = first == std::span(view->branch_rows).end()
                                            ? 0 : first->node_id.value;
    }
    const branch_tree_context_t tree{context, *view, action, editor_state};
    // Choices whose block row was cut by a capacity still get their controls.
    for (size_t index = 0; index < view->branch_choices.size(); ++index) {
        const auto& choice = view->branch_choices[index];
        const auto shown = std::ranges::any_of(std::span(view->branch_rows),
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
    for (size_t index = 0; index < view->branch_diagnostic_count(); ++index) {
        if (!view->branch_diagnostic_at(index).empty()) {
            auto& diagnostic = editor_state.diagnostic_label;
            diagnostic.assign("Branch diagnostic: ");
            diagnostic.append(view->branch_diagnostic_at(index));
            nk_layout_row_dynamic(context, 96.0f, 1);
            nk_label_wrap(context, diagnostic.c_str());
        }
    }
}
