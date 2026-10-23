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
                return failure_t{failure_code_t::invalid_data, "Chart and Text nodes cannot own children."};
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
            const auto* random = std::get_if<bms_random_scope_t>(&node.value);
            if (random && random->range <= 0)
                return failure_t{failure_code_t::invalid_data, "RANDOM requires a positive outcome range."};
            if (std::holds_alternative<bms_if_scope_t>(node.value) && !current.has_random)
                return failure_t{failure_code_t::invalid_data, "IF requires a containing RANDOM scope."};
            for (const auto child : node.children)
                pending.push_back({indices.at(child.value), current.depth + 1, current.has_random || random != nullptr});
        }
        if (visited.size() != nodes.size())
            return failure_t{failure_code_t::invalid_data, "BMS program has a disconnected cycle."};
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
            const auto* chart = std::get_if<bms_chart_content_t>(&node.value);
            if (!chart) continue;
            if (chart->items.size() > limits.maximum_content_items - item_count)
                return failure_t{failure_code_t::capacity_exceeded, "BMS program exceeds its content item limit."};
            item_count += chart->items.size();
            for (const auto& item : chart->items)
                if (!item.id || !identities.insert(item.id.value).second)
                    return failure_t{failure_code_t::invalid_data, "BMS content IDs must be nonzero and unique across all Charts."};
        }
        return std::nullopt;
    }
};
} // namespace

auto bms_program_node_t::is_scope() const -> bool {
    return std::holds_alternative<bms_root_scope_t>(value) ||
        std::holds_alternative<bms_random_scope_t>(value) || std::holds_alternative<bms_if_scope_t>(value);
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
auto bms_program_t::with_scope(branch_id_t parent_id, bms_program_node_t scope,
    branch_id_t chart, bms_program_limits_t limits) const -> result_t<bms_program_t, error_t<failure_code_t>> {
    const auto* containing = find(parent_id);
    if (!containing || !containing->is_scope())
        return std::unexpected(error_t{failure_code_t::invalid_data, "A new BMS scope requires an existing scope parent."});
    if (limits.maximum_nodes < 2 || nodes_.size() > limits.maximum_nodes - 2)
        return std::unexpected(error_t{failure_code_t::capacity_exceeded, "BMS scope and its Chart exceed the node limit."});
    auto nodes = nodes_;
    nodes[indices_.at(parent_id.value)].children.push_back(scope.id);
    scope.children = {chart};
    nodes.push_back(std::move(scope));
    nodes.push_back({.id = chart, .value = bms_chart_content_t{}});
    return from(std::move(nodes), limits);
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
