#include <wormedit/core/bms_program.h>
#include <algorithm>
#include <unordered_set>

namespace wormedit {
namespace {
using failure_t = error_t<failure_code_t>;
struct program_validation_t {
    const std::vector<bms_program_node_t>& nodes;
    const bms_program_limits_t& limits;
    std::unordered_map<std::uint64_t, std::size_t> indices;
    std::unordered_map<std::uint64_t, branch_id_t> parents;
    std::optional<std::size_t> root;

    auto index_nodes() -> std::optional<failure_t> {
        if (nodes.empty()) return failure_t{failure_code_t::invalid_data, "BMS program requires one Root."};
        if (nodes.size() > limits.maximum_nodes)
            return failure_t{failure_code_t::capacity_exceeded, "BMS program exceeds its node limit."};
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            const auto& node = nodes[i];
            if (!node.id || !indices.emplace(node.id.value, i).second)
                return failure_t{failure_code_t::invalid_data, "BMS program node IDs must be nonzero and unique."};
            if (!std::holds_alternative<bms_root_scope_t>(node.value)) continue;
            if (root) return failure_t{failure_code_t::invalid_data, "BMS program has more than one Root."};
            root = i;
        }
        if (!root) return failure_t{failure_code_t::invalid_data, "BMS program has no Root."};
        return std::nullopt;
    }
    auto index_children() -> std::optional<failure_t> {
        for (const auto& node : nodes) {
            if (!node.is_scope() && !node.children.empty())
                return failure_t{failure_code_t::invalid_data, "Only body scopes can own children."};
            if (node.children.size() >= nodes.size())
                return failure_t{failure_code_t::invalid_data, "BMS scope has too many child references."};
            bool has_chart{};
            for (const auto child : node.children) {
                const auto found = indices.find(child.value);
                if (found == indices.end() || child == nodes[*root].id ||
                    !parents.emplace(child.value, node.id).second)
                    return failure_t{failure_code_t::invalid_data, "BMS child has a missing, duplicate or invalid parent."};
                has_chart |= std::holds_alternative<bms_chart_content_t>(nodes[found->second].value);
            }
            if (node.is_scope() && !has_chart)
                return failure_t{failure_code_t::invalid_data, "Every BMS scope requires a direct Chart child."};
        }
        if (parents.size() != nodes.size() - 1)
            return failure_t{failure_code_t::invalid_data, "BMS program contains an orphan node."};
        return std::nullopt;
    }
    auto validate_arm(const bms_program_node_t& node, const bms_program_node_t& parent,
        std::optional<branch_id_t> previous) const -> std::optional<failure_t> {
        std::optional<branch_id_t> required;
        if (const auto* arm = std::get_if<bms_else_if_scope_t>(&node.value)) required = arm->previous_arm;
        if (const auto* arm = std::get_if<bms_else_scope_t>(&node.value)) required = arm->previous_arm;
        if (required) {
            const auto found = indices.find(required->value);
            if (required != previous || found == indices.end())
                return failure_t{failure_code_t::invalid_data, "Conditional arms must follow their previous sibling arm."};
            const auto& value = nodes[found->second].value;
            if (!std::holds_alternative<bms_if_scope_t>(value) && !std::holds_alternative<bms_else_if_scope_t>(value))
                return failure_t{failure_code_t::invalid_data, "ELSE/ELSEIF requires an IF/ELSEIF predecessor."};
        }
        if (std::holds_alternative<bms_case_scope_t>(node.value) &&
            !std::holds_alternative<bms_switch_scope_t>(parent.value))
            return failure_t{failure_code_t::invalid_data, "CASE/DEF requires a SWITCH parent."};
        return std::nullopt;
    }
    auto validate_children(const bms_program_node_t& node) const -> std::optional<failure_t> {
        std::optional<branch_id_t> previous;
        bool saw_default{};
        for (const auto id : node.children) {
            const auto& child = nodes[indices.at(id.value)];
            if (auto failure = validate_arm(child, node, previous)) return failure;
            if (const auto* arm = std::get_if<bms_case_scope_t>(&child.value)) {
                if (saw_default) return failure_t{failure_code_t::invalid_data, "CASE/DEF cannot follow DEF."};
                saw_default = !arm->value;
            }
            previous = id;
        }
        return std::nullopt;
    }
    auto validate_skips(const bms_program_node_t& node) const -> std::optional<failure_t> {
        const auto* arm = std::get_if<bms_case_scope_t>(&node.value);
        if (!arm) return std::nullopt;
        for (const auto& skip : arm->skips) {
            const auto found = indices.find(skip.scope.value);
            if (found == indices.end() || !nodes[found->second].is_scope())
                return failure_t{failure_code_t::invalid_data, "SKIP requires a valid containing scope."};
            const auto& scope = nodes[found->second];
            if (skip.after_child && std::ranges::find(scope.children, *skip.after_child) == scope.children.end())
                return failure_t{failure_code_t::invalid_data, "SKIP anchor must be a child of its containing scope."};
            auto containing = skip.scope;
            while (containing != node.id) {
                if (std::holds_alternative<bms_case_scope_t>(nodes[indices.at(containing.value)].value)) break;
                const auto parent = parents.find(containing.value);
                if (parent == parents.end()) break;
                containing = parent->second;
            }
            if (containing != node.id)
                return failure_t{failure_code_t::invalid_data, "SKIP must belong to its nearest containing case."};
        }
        return std::nullopt;
    }
    auto validate_scope(const bms_program_node_t& node, bool has_random) const -> std::optional<failure_t> {
        if (auto failure = validate_children(node)) return failure;
        if (auto failure = validate_skips(node)) return failure;
        const auto* random = std::get_if<bms_random_scope_t>(&node.value);
        if (random && random->range <= 0)
            return failure_t{failure_code_t::invalid_data, "RANDOM requires a positive outcome range."};
        if ((std::holds_alternative<bms_if_scope_t>(node.value) ||
             std::holds_alternative<bms_else_if_scope_t>(node.value) ||
             std::holds_alternative<bms_else_scope_t>(node.value)) && !has_random)
            return failure_t{failure_code_t::invalid_data, "IF requires a containing RANDOM scope."};
        return std::nullopt;
    }
    auto validate_tree() const -> std::optional<failure_t> {
        struct pending_t { std::size_t index, depth; bool has_random; };
        std::vector<pending_t> pending{{*root, 1, false}};
        std::unordered_set<std::size_t> visited;
        while (!pending.empty()) {
            const auto current = pending.back();
            pending.pop_back();
            if (current.depth > limits.maximum_depth)
                return failure_t{failure_code_t::capacity_exceeded, "BMS program exceeds its depth limit."};
            if (!visited.insert(current.index).second)
                return failure_t{failure_code_t::invalid_data, "BMS program contains a cycle."};
            const auto& node = nodes[current.index];
            if (auto failure = validate_scope(node, current.has_random)) return failure;
            const auto has_random = current.has_random || std::holds_alternative<bms_random_scope_t>(node.value);
            for (const auto child : node.children)
                pending.push_back({indices.at(child.value), current.depth + 1, has_random});
        }
        if (visited.size() != nodes.size())
            return failure_t{failure_code_t::invalid_data, "BMS program has a disconnected cycle."};
        return std::nullopt;
    }
    auto validate_content_ids(const bms_chart_content_t& chart,
        std::unordered_set<std::uint64_t>& identities, std::size_t& count) const -> std::optional<failure_t> {
        for (const auto& item : chart.items) {
            if (!item.id || !identities.insert(item.id.value).second)
                return failure_t{failure_code_t::invalid_data, "BMS content IDs must be nonzero and unique across all Charts."};
            const auto* native = std::get_if<bms_native_content_t>(&item.value);
            if (!native) continue;
            const auto children = native->declared_ids();
            if (children.size() > limits.maximum_content_items - count)
                return failure_t{failure_code_t::capacity_exceeded, "Native scoped content exceeds its child limit."};
            count += children.size();
            for (const auto id : children)
                if (!id || !identities.insert(id.value).second)
                    return failure_t{failure_code_t::invalid_data, "Native child IDs must be nonzero and unique."};
        }
        return std::nullopt;
    }
    auto validate_content() const -> std::optional<failure_t> {
        std::unordered_set<std::uint64_t> identities;
        std::size_t text_bytes{}, item_count{};
        for (const auto& node : nodes) {
            if (const auto* text = std::get_if<bms_text_t>(&node.value)) {
                if (text->bytes.size() > limits.maximum_text_bytes - text_bytes)
                    return failure_t{failure_code_t::capacity_exceeded, "BMS program exceeds its Text byte limit."};
                text_bytes += text->bytes.size();
            }
            if (const auto* arm = std::get_if<bms_case_scope_t>(&node.value)) {
                if (arm->skips.size() > limits.maximum_content_items - item_count)
                    return failure_t{failure_code_t::capacity_exceeded, "BMS program exceeds its case behavior limit."};
                item_count += arm->skips.size();
            }
            const auto* chart = std::get_if<bms_chart_content_t>(&node.value);
            if (!chart) continue;
            if (chart->items.size() > limits.maximum_content_items - item_count)
                return failure_t{failure_code_t::capacity_exceeded, "BMS program exceeds its content item limit."};
            item_count += chart->items.size();
            if (auto failure = validate_content_ids(*chart, identities, item_count)) return failure;
        }
        return std::nullopt;
    }
};
} // namespace

auto bms_program_node_t::is_scope() const -> bool {
    return std::holds_alternative<bms_root_scope_t>(value) ||
        std::holds_alternative<bms_random_scope_t>(value) || std::holds_alternative<bms_if_scope_t>(value) ||
        std::holds_alternative<bms_else_if_scope_t>(value) || std::holds_alternative<bms_else_scope_t>(value) ||
        std::holds_alternative<bms_switch_scope_t>(value) || std::holds_alternative<bms_case_scope_t>(value);
}
auto bms_program_t::from(std::vector<bms_program_node_t> nodes, bms_program_limits_t limits)
    -> result_t<bms_program_t, error_t<failure_code_t>> {
    program_validation_t validation{.nodes = nodes, .limits = limits};
    if (auto failure = validation.index_nodes()) return std::unexpected(std::move(*failure));
    if (auto failure = validation.index_children()) return std::unexpected(std::move(*failure));
    if (auto failure = validation.validate_tree()) return std::unexpected(std::move(*failure));
    if (auto failure = validation.validate_content()) return std::unexpected(std::move(*failure));
    bms_program_t result;
    result.root_index_ = *validation.root;
    result.nodes_ = std::move(nodes);
    result.indices_ = std::move(validation.indices);
    result.parents_ = std::move(validation.parents);
    return result;
}
auto bms_program_t::from_root(branch_id_t root, branch_id_t chart, bms_program_limits_t limits)
    -> result_t<bms_program_t, error_t<failure_code_t>> {
    return from({{.id = root, .value = bms_root_scope_t{}, .children = {chart}},
                 {.id = chart, .value = bms_chart_content_t{}}}, limits);
}
auto bms_program_t::find(branch_id_t id) const -> const bms_program_node_t* {
    const auto found = indices_.find(id.value);
    return found == indices_.end() ? nullptr : &nodes_[found->second];
}
auto bms_program_t::parent(branch_id_t id) const -> std::optional<branch_id_t> {
    const auto found = parents_.find(id.value);
    return found == parents_.end() ? std::nullopt : std::optional{found->second};
}
auto bms_program_t::target_chart(branch_id_t id) const -> std::optional<branch_id_t> {
    const auto* node = find(id);
    if (!node) return std::nullopt;
    if (std::holds_alternative<bms_chart_content_t>(node->value)) return node->id;
    for (const auto child : node->children)
        if (std::holds_alternative<bms_chart_content_t>(find(child)->value)) return child;
    return std::nullopt;
}
auto bms_program_t::with_node(branch_id_t parent_id, std::optional<branch_id_t> after,
    bms_program_node_t node, std::optional<branch_id_t> first_chart, bms_program_limits_t limits) const
    -> result_t<bms_program_t, error_t<failure_code_t>> {
    const auto* containing = find(parent_id);
    if (!containing || !containing->is_scope())
        return std::unexpected(error_t{failure_code_t::invalid_data, "Insertion requires an existing body scope parent."});
    if (!node.children.empty() || std::holds_alternative<bms_root_scope_t>(node.value) ||
        node.is_scope() != first_chart.has_value())
        return std::unexpected(error_t{failure_code_t::invalid_data, "New scopes require exactly one fresh Chart; Root cannot be inserted."});
    auto offset = std::size_t{};
    if (after) {
        const auto anchor = std::ranges::find(containing->children, *after);
        if (anchor == containing->children.end())
            return std::unexpected(error_t{failure_code_t::not_found, "Insertion anchor is not a child of the selected parent."});
        offset = static_cast<std::size_t>(anchor - containing->children.begin()) + 1;
    }
    const auto added = first_chart ? 2U : 1U;
    if (added > limits.maximum_nodes || nodes_.size() > limits.maximum_nodes - added)
        return std::unexpected(error_t{failure_code_t::capacity_exceeded, "Inserted nodes exceed the program node limit."});
    auto nodes = nodes_;
    auto& children = nodes[indices_.at(parent_id.value)].children;
    children.insert(children.begin() + static_cast<std::ptrdiff_t>(offset), node.id);
    if (first_chart) node.children.push_back(*first_chart);
    nodes.push_back(std::move(node));
    if (first_chart) nodes.push_back({.id = *first_chart, .value = bms_chart_content_t{}});
    return from(std::move(nodes), limits);
}
auto bms_program_t::with_text(branch_id_t id, bms_text_t text, bms_program_limits_t limits) const
    -> result_t<bms_program_t, error_t<failure_code_t>> {
    const auto* node = find(id);
    if (!node || !std::holds_alternative<bms_text_t>(node->value))
        return std::unexpected(error_t{failure_code_t::not_found, "Text edit requires an existing Text node."});
    auto nodes = nodes_;
    nodes[indices_.at(id.value)].value = std::move(text);
    return from(std::move(nodes), limits);
}
auto bms_program_t::with_scope(branch_id_t parent_id, bms_program_node_t scope,
    branch_id_t chart, bms_program_limits_t limits) const -> result_t<bms_program_t, error_t<failure_code_t>> {
    const auto* containing = find(parent_id);
    if (!containing || !containing->is_scope())
        return std::unexpected(error_t{failure_code_t::invalid_data, "A new BMS scope requires an existing scope parent."});
    const auto after = containing->children.empty() ? std::nullopt : std::optional{containing->children.back()};
    return with_node(parent_id, after, std::move(scope), chart, limits);
}
auto bms_program_t::with_random(branch_id_t parent_id, branch_id_t scope, branch_id_t chart,
    std::int64_t range, bms_program_limits_t limits) const -> result_t<bms_program_t, error_t<failure_code_t>> {
    return with_scope(parent_id, {.id = scope, .value = bms_random_scope_t{range}}, chart, limits);
}
auto bms_program_t::with_if(branch_id_t parent_id, branch_id_t scope, branch_id_t chart,
    std::int64_t value, bms_program_limits_t limits) const -> result_t<bms_program_t, error_t<failure_code_t>> {
    return with_scope(parent_id, {.id = scope, .value = bms_if_scope_t{value}}, chart, limits);
}
} // namespace wormedit

namespace wormedit {
auto bms_metadata_definition_t::from(std::string_view command) -> const bms_metadata_definition_t* {
    const auto found = std::ranges::find(bms_metadata_definitions, command, &bms_metadata_definition_t::command);
    return found == bms_metadata_definitions.end() ? nullptr : &*found;
}
} // namespace wormedit
