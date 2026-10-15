#include <wormedit/core/bms_branch_view.h>

#include <algorithm>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace wormedit {
namespace {

struct branch_tree_validation_context_t {
    bms_branch_projection_limits_t limits;
    std::unordered_map<std::uint64_t, const bms_control_node_t*> nodes;
    std::unordered_set<std::size_t> raw_source_orders;
    std::size_t node_count{};
    std::optional<bms_branch_projection_diagnostic_t> failure;
};

auto validate_branch_tree_node(branch_tree_validation_context_t& context,
                               const bms_control_node_t& node, std::size_t depth) -> void {
    if (context.failure)
        return;
    if (depth > context.limits.maximum_depth ||
        context.node_count >= context.limits.maximum_nodes) {
        context.failure = bms_branch_projection_diagnostic_t{
            bms_branch_projection_status_t::capacity_exceeded,
            "BMS branch tree exceeds the configured projection bounds", node.id};
        return;
    }
    ++context.node_count;
    if (!node.id || !context.nodes.emplace(node.id.value, &node).second) {
        context.failure = bms_branch_projection_diagnostic_t{
            bms_branch_projection_status_t::invalid_tree,
            "BMS branch tree contains a zero or duplicate node ID", node.id};
        return;
    }
    if (node.kind == bms_control_kind_t::raw_statement &&
        !context.raw_source_orders.insert(node.source_order).second) {
        context.failure = bms_branch_projection_diagnostic_t{
            bms_branch_projection_status_t::invalid_tree,
            "BMS raw statements contain duplicate source orders", node.id};
        return;
    }
    for (const auto& child : node.children)
        validate_branch_tree_node(context, child, depth + 1);
}

struct active_statement_t {
    branch_id_t id;
    std::size_t source_order{};
};

struct branch_projection_context_t {
    const std::map<std::uint64_t, std::int64_t>& selected_values;
    bms_branch_projection_limits_t limits;
    std::vector<active_statement_t> active_statements;
    std::set<std::uint64_t> visited_selections;
    std::optional<bms_branch_projection_diagnostic_t> failure;
};

void visit_projection_node(branch_projection_context_t& context, const bms_control_node_t& node,
                           std::optional<std::int64_t> random_value, bool inside_active_branch,
                           bool* switch_stopped);

auto record_active_statement(branch_projection_context_t& context, const bms_control_node_t& node)
    -> void {
    if (context.active_statements.size() >= context.limits.maximum_active_statements) {
        context.failure = bms_branch_projection_diagnostic_t{
            bms_branch_projection_status_t::capacity_exceeded,
            "active BMS branch statements exceed the configured projection bound", node.id};
        return;
    }
    context.active_statements.push_back({node.id, node.source_order});
}

auto visit_random_control(branch_projection_context_t& context, const bms_control_node_t& node,
                          bool* switch_stopped) -> void {
    const auto selected = context.selected_values.find(node.id.value);
    if (selected == context.selected_values.end())
        return;
    if (!node.value || selected->second < 1 || selected->second > *node.value) {
        context.failure = bms_branch_projection_diagnostic_t{
            bms_branch_projection_status_t::invalid_selection,
            "BMS RANDOM selection is outside the control's declared range", node.id};
        return;
    }
    context.visited_selections.insert(node.id.value);
    for (const auto& child : node.children)
        visit_projection_node(context, child, selected->second, true, switch_stopped);
}

auto selected_if_branch(const bms_control_node_t& node, std::int64_t random_value)
    -> const bms_control_node_t* {
    const bms_control_node_t* selected{};
    const bms_control_node_t* fallback{};
    for (const auto& child : node.children) {
        if ((child.kind == bms_control_kind_t::if_branch ||
             child.kind == bms_control_kind_t::else_if_branch) &&
            child.value == random_value) {
            selected = &child;
            break;
        }
        if (child.kind == bms_control_kind_t::else_branch)
            fallback = &child;
    }
    return selected ? selected : fallback;
}

auto visit_if_group(branch_projection_context_t& context, const bms_control_node_t& node,
                    std::optional<std::int64_t> random_value, bool* switch_stopped) -> void {
    if (!random_value)
        return;
    const auto* selected = selected_if_branch(node, *random_value);
    if (selected)
        visit_projection_node(context, *selected, random_value, true, switch_stopped);
}

auto selected_switch_branch(const bms_control_node_t& node, std::int64_t choice)
    -> const bms_control_node_t* {
    const bms_control_node_t* selected{};
    const bms_control_node_t* fallback{};
    for (const auto& child : node.children) {
        if (child.kind == bms_control_kind_t::case_branch && child.value == choice) {
            selected = &child;
            break;
        }
        if (child.kind == bms_control_kind_t::default_branch)
            fallback = &child;
    }
    return selected ? selected : fallback;
}

auto visit_switch_group(branch_projection_context_t& context, const bms_control_node_t& node,
                        std::optional<std::int64_t> random_value, bool* switch_stopped) -> void {
    const auto choice = context.selected_values.find(node.id.value);
    if (choice == context.selected_values.end())
        return;
    const auto* selected = selected_switch_branch(node, choice->second);
    if (!selected) {
        context.failure = bms_branch_projection_diagnostic_t{
            bms_branch_projection_status_t::invalid_selection,
            "BMS SWITCH selection matches no CASE and has no DEFAULT", node.id};
        return;
    }
    context.visited_selections.insert(node.id.value);
    bool stopped{};
    bool entered{};
    for (const auto& child : node.children) {
        if (is_switch_branch(child.kind))
            entered = entered || &child == selected;
        if (entered && !stopped)
            visit_projection_node(context, child, random_value, true, &stopped);
    }
    (void)switch_stopped;
}

void visit_projection_node(branch_projection_context_t& context, const bms_control_node_t& node,
                           std::optional<std::int64_t> random_value, bool inside_active_branch,
                           bool* switch_stopped) {
    if (context.failure)
        return;
    if (node.kind == bms_control_kind_t::raw_statement) {
        if (inside_active_branch)
            record_active_statement(context, node);
        return;
    }
    if (node.kind == bms_control_kind_t::skip) {
        if (switch_stopped)
            *switch_stopped = true;
        return;
    }
    if (node.kind == bms_control_kind_t::random) {
        visit_random_control(context, node, switch_stopped);
        return;
    }
    if (node.kind == bms_control_kind_t::if_group) {
        visit_if_group(context, node, random_value, switch_stopped);
        return;
    }
    if (node.kind == bms_control_kind_t::switch_block) {
        visit_switch_group(context, node, random_value, switch_stopped);
        return;
    }
    const auto branch_scope = inside_active_branch || node.kind == bms_control_kind_t::if_branch ||
                              node.kind == bms_control_kind_t::else_if_branch ||
                              node.kind == bms_control_kind_t::else_branch ||
                              node.kind == bms_control_kind_t::case_branch ||
                              node.kind == bms_control_kind_t::default_branch;
    for (const auto& child : node.children)
        visit_projection_node(context, child, random_value, branch_scope, switch_stopped);
}

auto validate_selections(const std::unordered_map<std::uint64_t, const bms_control_node_t*>& nodes,
                         const std::vector<branch_selection_t>& selections,
                         std::map<std::uint64_t, std::int64_t>& selected_values)
    -> std::optional<bms_branch_projection_diagnostic_t> {
    for (const auto& selection : selections) {
        if (!selection.branch ||
            !selected_values.emplace(selection.branch.value, selection.selected_value).second)
            return bms_branch_projection_diagnostic_t{
                bms_branch_projection_status_t::duplicate_selection,
                "BMS branch projection contains a zero or duplicate selection", selection.branch};
        const auto node = nodes.find(selection.branch.value);
        if (node == nodes.end() || !is_choice_control(node->second->kind))
            return bms_branch_projection_diagnostic_t{
                bms_branch_projection_status_t::invalid_selection,
                "BMS branch selection does not identify a RANDOM or SWITCH control",
                selection.branch};
    }
    return std::nullopt;
}

auto make_projection_result(branch_projection_context_t& context) -> bms_branch_view_t {
    std::ranges::sort(context.active_statements, [](const auto& left, const auto& right) {
        return std::tie(left.source_order, left.id.value) <
               std::tie(right.source_order, right.id.value);
    });
    bms_branch_view_t result;
    result.active_statement_ids.reserve(context.active_statements.size());
    result.active_statement_source_orders.reserve(context.active_statements.size());
    for (const auto& statement : context.active_statements) {
        result.active_statement_ids.push_back(statement.id);
        result.active_statement_source_orders.push_back(statement.source_order);
    }
    result.visited_selection_controls.reserve(context.visited_selections.size());
    for (const auto id : context.visited_selections)
        result.visited_selection_controls.push_back(branch_id_t{id});
    return result;
}

} // namespace

auto bms_branch_view_t::from(const bms_control_node_t& root,
                             const std::vector<branch_selection_t>& selections,
                             bms_branch_projection_limits_t limits) -> bms_branch_view_outcome_t {
    bms_branch_view_outcome_t result;
    if (root.kind != bms_control_kind_t::root) {
        result.diagnostics.push_back(bms_branch_projection_diagnostic_t{
            bms_branch_projection_status_t::invalid_tree,
            "BMS branch projection requires a root control node", root.id});
        return result;
    }
    if (limits.maximum_nodes == 0 || limits.maximum_active_statements == 0) {
        result.diagnostics.push_back(
            bms_branch_projection_diagnostic_t{bms_branch_projection_status_t::capacity_exceeded,
                                               "BMS branch projection limits must be nonzero",
                                               {}});
        return result;
    }

    branch_tree_validation_context_t validation{
        .limits = limits, .nodes = {}, .raw_source_orders = {}, .node_count = 0, .failure = {}};
    validate_branch_tree_node(validation, root, 0);
    if (validation.failure) {
        result.diagnostics.push_back(std::move(*validation.failure));
        return result;
    }
    std::map<std::uint64_t, std::int64_t> selected_values;
    if (auto failure = validate_selections(validation.nodes, selections, selected_values)) {
        result.diagnostics.push_back(std::move(*failure));
        return result;
    }

    branch_projection_context_t projection{.selected_values = selected_values,
                                           .limits = limits,
                                           .active_statements = {},
                                           .visited_selections = {},
                                           .failure = {}};
    visit_projection_node(projection, root, std::nullopt, false, nullptr);
    if (projection.failure) {
        result.diagnostics.push_back(std::move(*projection.failure));
        return result;
    }
    if (projection.visited_selections.size() != selected_values.size()) {
        const auto unreachable = std::ranges::find_if(selected_values, [&](const auto& selection) {
            return !projection.visited_selections.contains(selection.first);
        });
        result.diagnostics.push_back(bms_branch_projection_diagnostic_t{
            bms_branch_projection_status_t::unreachable_selection,
            "BMS branch selection targets a control outside the active projection",
            branch_id_t{unreachable->first}});
        return result;
    }
    result.value = make_projection_result(projection);
    return result;
}

auto bms_branch_view_result_t::from(bms_branch_view_outcome_t&& outcome)
    -> bms_branch_view_result_t {
    const auto status = outcome.diagnostics.empty() ? bms_branch_projection_status_t::projected
                                                    : outcome.diagnostics.front().code;
    return {.status = status,
            .active_statement_ids = std::move(outcome.value.active_statement_ids),
            .active_statement_source_orders =
                std::move(outcome.value.active_statement_source_orders),
            .visited_selection_controls = std::move(outcome.value.visited_selection_controls),
            .diagnostics = std::move(outcome.diagnostics)};
}

auto bms_branch_view_result_t::from(const bms_control_node_t& root,
                                    const std::vector<branch_selection_t>& selections,
                                    bms_branch_projection_limits_t limits)
    -> bms_branch_view_result_t {
    return bms_branch_view_result_t::from(bms_branch_view_t::from(root, selections, limits));
}

auto bms_branch_preview_view_t::from(const bms_control_node_t& root,
                                     const std::vector<branch_selection_t>& current,
                                     const std::vector<branch_selection_t>& proposed,
                                     bms_branch_projection_limits_t limits)
    -> bms_branch_preview_view_t {
    bms_branch_preview_view_t result;
    result.current = bms_branch_view_result_t::from(root, current, limits);
    result.proposed = bms_branch_view_result_t::from(root, proposed, limits);
    if (!result) {
        return result;
    }

    std::unordered_set<std::uint64_t> current_ids;
    current_ids.reserve(result.current.active_statement_ids.size());
    for (const auto id : result.current.active_statement_ids) {
        current_ids.insert(id.value);
    }
    std::unordered_set<std::uint64_t> proposed_ids;
    proposed_ids.reserve(result.proposed.active_statement_ids.size());
    for (const auto id : result.proposed.active_statement_ids) {
        proposed_ids.insert(id.value);
        if (!current_ids.contains(id.value)) {
            result.entering_statement_ids.push_back(id);
        }
    }
    for (const auto id : result.current.active_statement_ids) {
        if (!proposed_ids.contains(id.value)) {
            result.leaving_statement_ids.push_back(id);
        }
    }
    return result;
}

} // namespace wormedit
