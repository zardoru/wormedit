#include <wormedit/core/bms_branch_projection.h>

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

auto fail(bms_branch_projection_status_t status, std::string message, branch_id_t branch = {})
    -> bms_branch_projection_result_t {
    bms_branch_projection_result_t result;
    result.status = status;
    result.diagnostics.push_back({status, std::move(message), branch});
    return result;
}

auto is_choice_control(bms_control_kind_t kind) -> bool {
    return kind == bms_control_kind_t::random || kind == bms_control_kind_t::switch_block;
}

} // namespace

auto project_bms_branch_tree(const bms_control_node_t& root,
                             const std::vector<branch_selection_t>& selections,
                             bms_branch_projection_limits_t limits)
    -> bms_branch_projection_result_t {
    if (root.kind != bms_control_kind_t::root) {
        return fail(bms_branch_projection_status_t::invalid_tree,
                    "BMS branch projection requires a root control node", root.id);
    }
    if (limits.maximum_nodes == 0 || limits.maximum_active_statements == 0) {
        return fail(bms_branch_projection_status_t::capacity_exceeded,
                    "BMS branch projection limits must be nonzero");
    }

    std::unordered_map<std::uint64_t, const bms_control_node_t*> nodes;
    std::unordered_set<std::size_t> raw_source_orders;
    std::size_t node_count{};
    std::optional<bms_branch_projection_result_t> tree_failure;
    const auto inspect = [&](const auto& self, const bms_control_node_t& node,
                             std::size_t depth) -> void {
        if (tree_failure.has_value()) {
            return;
        }
        if (depth > limits.maximum_depth || node_count >= limits.maximum_nodes) {
            tree_failure =
                fail(bms_branch_projection_status_t::capacity_exceeded,
                     "BMS branch tree exceeds the configured projection bounds", node.id);
            return;
        }
        ++node_count;
        if (!node.id || !nodes.emplace(node.id.value, &node).second) {
            tree_failure = fail(bms_branch_projection_status_t::invalid_tree,
                                "BMS branch tree contains a zero or duplicate node ID", node.id);
            return;
        }
        if (node.kind == bms_control_kind_t::raw_statement &&
            !raw_source_orders.insert(node.source_order).second) {
            tree_failure = fail(bms_branch_projection_status_t::invalid_tree,
                                "BMS raw statements contain duplicate source orders", node.id);
            return;
        }
        for (const auto& child : node.children) {
            self(self, child, depth + 1);
        }
    };
    inspect(inspect, root, 0);
    if (tree_failure.has_value()) {
        return std::move(*tree_failure);
    }

    std::map<std::uint64_t, std::int64_t> selected_values;
    for (const auto& selection : selections) {
        if (!selection.branch ||
            !selected_values.emplace(selection.branch.value, selection.selected_value).second) {
            return fail(bms_branch_projection_status_t::duplicate_selection,
                        "BMS branch projection contains a zero or duplicate selection",
                        selection.branch);
        }
        const auto node = nodes.find(selection.branch.value);
        if (node == nodes.end() || !is_choice_control(node->second->kind)) {
            return fail(bms_branch_projection_status_t::invalid_selection,
                        "BMS branch selection does not identify a RANDOM or SWITCH control",
                        selection.branch);
        }
    }

    struct active_statement_t {
        branch_id_t id;
        std::size_t source_order{};
    };
    std::vector<active_statement_t> active_statements;
    std::set<std::uint64_t> visited_selections;
    std::optional<bms_branch_projection_result_t> projection_failure;
    std::function<void(const bms_control_node_t&, std::optional<std::int64_t>, bool, bool*)> visit;
    visit = [&](const bms_control_node_t& node, std::optional<std::int64_t> random_value,
                bool inside_active_branch, bool* switch_stopped) {
        if (projection_failure.has_value()) {
            return;
        }
        if (node.kind == bms_control_kind_t::raw_statement) {
            if (inside_active_branch) {
                if (active_statements.size() >= limits.maximum_active_statements) {
                    projection_failure =
                        fail(bms_branch_projection_status_t::capacity_exceeded,
                             "active BMS branch statements exceed the configured projection bound",
                             node.id);
                    return;
                }
                active_statements.push_back({node.id, node.source_order});
            }
            return;
        }
        if (node.kind == bms_control_kind_t::skip) {
            if (switch_stopped != nullptr) {
                *switch_stopped = true;
            }
            return;
        }
        if (node.kind == bms_control_kind_t::random) {
            const auto selected = selected_values.find(node.id.value);
            if (selected == selected_values.end()) {
                return;
            }
            if (!node.value.has_value() || selected->second < 1 || selected->second > *node.value) {
                projection_failure =
                    fail(bms_branch_projection_status_t::invalid_selection,
                         "BMS RANDOM selection is outside the control's declared range", node.id);
                return;
            }
            visited_selections.insert(node.id.value);
            for (const auto& child : node.children) {
                visit(child, selected->second, true, switch_stopped);
            }
            return;
        }
        if (node.kind == bms_control_kind_t::if_group) {
            if (!random_value.has_value()) {
                return;
            }
            const bms_control_node_t* selected{};
            const bms_control_node_t* fallback{};
            for (const auto& child : node.children) {
                if ((child.kind == bms_control_kind_t::if_branch ||
                     child.kind == bms_control_kind_t::else_if_branch) &&
                    child.value == random_value) {
                    selected = &child;
                    break;
                }
                if (child.kind == bms_control_kind_t::else_branch) {
                    fallback = &child;
                }
            }
            if (selected == nullptr) {
                selected = fallback;
            }
            if (selected != nullptr) {
                visit(*selected, random_value, true, switch_stopped);
            }
            return;
        }
        if (node.kind == bms_control_kind_t::switch_block) {
            const auto choice = selected_values.find(node.id.value);
            if (choice == selected_values.end()) {
                return;
            }
            const bms_control_node_t* selected{};
            const bms_control_node_t* fallback{};
            for (const auto& child : node.children) {
                if (child.kind == bms_control_kind_t::case_branch &&
                    child.value == choice->second) {
                    selected = &child;
                    break;
                }
                if (child.kind == bms_control_kind_t::default_branch) {
                    fallback = &child;
                }
            }
            if (selected == nullptr) {
                selected = fallback;
            }
            if (selected == nullptr) {
                projection_failure =
                    fail(bms_branch_projection_status_t::invalid_selection,
                         "BMS SWITCH selection matches no CASE and has no DEFAULT", node.id);
                return;
            }
            visited_selections.insert(node.id.value);
            bool stopped{};
            bool entered{};
            for (const auto& child : node.children) {
                if (child.kind == bms_control_kind_t::case_branch ||
                    child.kind == bms_control_kind_t::default_branch) {
                    entered = entered || &child == selected;
                    if (entered && !stopped) {
                        visit(child, random_value, true, &stopped);
                    }
                } else if (entered && !stopped) {
                    visit(child, random_value, true, &stopped);
                }
            }
            return;
        }

        const auto branch_scope = inside_active_branch ||
                                  node.kind == bms_control_kind_t::if_branch ||
                                  node.kind == bms_control_kind_t::else_if_branch ||
                                  node.kind == bms_control_kind_t::else_branch ||
                                  node.kind == bms_control_kind_t::case_branch ||
                                  node.kind == bms_control_kind_t::default_branch;
        for (const auto& child : node.children) {
            visit(child, random_value, branch_scope, switch_stopped);
        }
    };
    visit(root, std::nullopt, false, nullptr);
    if (projection_failure.has_value()) {
        return std::move(*projection_failure);
    }
    if (visited_selections.size() != selected_values.size()) {
        const auto unreachable = std::ranges::find_if(selected_values, [&](const auto& selection) {
            return !visited_selections.contains(selection.first);
        });
        return fail(bms_branch_projection_status_t::unreachable_selection,
                    "BMS branch selection targets a control outside the active projection",
                    branch_id_t{unreachable->first});
    }

    std::ranges::sort(active_statements, [](const auto& left, const auto& right) {
        return std::tie(left.source_order, left.id.value) <
               std::tie(right.source_order, right.id.value);
    });
    bms_branch_projection_result_t result;
    result.status = bms_branch_projection_status_t::projected;
    result.active_statement_ids.reserve(active_statements.size());
    result.active_statement_source_orders.reserve(active_statements.size());
    for (const auto& statement : active_statements) {
        result.active_statement_ids.push_back(statement.id);
        result.active_statement_source_orders.push_back(statement.source_order);
    }
    result.visited_selection_controls.reserve(visited_selections.size());
    for (const auto id : visited_selections) {
        result.visited_selection_controls.push_back(branch_id_t{id});
    }
    return result;
}

auto preview_bms_branch_projection(const bms_control_node_t& root,
                                   const std::vector<branch_selection_t>& current,
                                   const std::vector<branch_selection_t>& proposed,
                                   bms_branch_projection_limits_t limits)
    -> bms_branch_projection_preview_t {
    bms_branch_projection_preview_t result;
    result.current = project_bms_branch_tree(root, current, limits);
    result.proposed = project_bms_branch_tree(root, proposed, limits);
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
