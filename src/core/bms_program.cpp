#include <wormedit/core/bms_program.h>
#include <algorithm>
#include <type_traits>
#include <unordered_set>

namespace wormedit {
auto bms_content_item_t::resource_identifier() const -> std::optional<bms_native_resource_reference_t> {
    return std::visit([](const auto& value) -> std::optional<bms_native_resource_reference_t> {
        using value_type = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::same_as<value_type, bms_resource_declaration_t>)
            return bms_native_resource_reference_t{.name_space = value.name_space, .identifier = value.identifier};
        else if constexpr (std::same_as<value_type, bms_native_content_t>) return value.resource_reference();
        else if constexpr (std::same_as<value_type, bms_bga_content_t>)
            return bms_native_resource_reference_t{.name_space = bms_resource_namespace_t::bmp, .identifier = value.bmp};
        else if constexpr (std::same_as<value_type, bms_hold_endpoint_t>)
            return bms_native_resource_reference_t{.identifier = value.wav};
        else if constexpr (requires { value.wav; }) {
            if (value.wav) return bms_native_resource_reference_t{.identifier = *value.wav};
            return std::nullopt;
        } else return std::nullopt;
    }, value);
}

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
    auto account_text(const bms_text_t& text, std::size_t& bytes) const -> std::optional<failure_t> {
        if (text.bytes.size() > limits.maximum_text_bytes - bytes)
            return failure_t{failure_code_t::capacity_exceeded, "BMS program exceeds its Text byte limit."};
        bytes += text.bytes.size();
        return std::nullopt;
    }
    auto validate_content_ids(const bms_chart_content_t& chart,
        std::unordered_set<std::uint64_t>& identities, std::size_t& count, std::size_t& text_bytes) const -> std::optional<failure_t> {
        for (const auto& item : chart.items) {
            if (!item.id || !identities.insert(item.id.value).second)
                return failure_t{failure_code_t::invalid_data, "BMS content IDs must be nonzero and unique across all Charts."};
            if (const auto* text = std::get_if<bms_text_t>(&item.value))
                if (auto failure = account_text(*text, text_bytes)) return failure;
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
                if (auto failure = account_text(*text, text_bytes)) return failure;
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
            if (auto failure = validate_content_ids(*chart, identities, item_count, text_bytes)) return failure;
        }
        return std::nullopt;
    }
};
} // namespace

auto bms_program_node_t::is_scope() const -> bool {
    return std::holds_alternative<bms_root_scope_t>(value) || std::holds_alternative<bms_bare_scope_t>(value) ||
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
auto bms_program_t::has_control_flow() const -> bool {
    return std::ranges::any_of(nodes_, [](const auto& node) {
        return !std::holds_alternative<bms_root_scope_t>(node.value) &&
            !std::holds_alternative<bms_bare_scope_t>(node.value) &&
            (node.is_scope() || std::holds_alternative<bms_set_random_t>(node.value) ||
                std::holds_alternative<bms_set_switch_t>(node.value));
    });
}

auto bms_program_t::conditional_selector(branch_id_t id) const -> std::optional<branch_id_t> {
    const auto* node = find(id);
    if (!node) return std::nullopt;
    if (std::holds_alternative<bms_case_scope_t>(node->value)) return parent(id);
    if (!node->is_if_arm()) return std::nullopt;
    for (auto ancestor = parent(id); ancestor; ancestor = parent(*ancestor))
        if (std::holds_alternative<bms_random_scope_t>(find(*ancestor)->value)) return ancestor;
    return std::nullopt;
}
auto bms_program_t::hold_endpoints(std::span<const bms_program_owner_t> contributors) const
    -> std::vector<bms_program_owner_t> {
    std::vector<bms_program_owner_t> result;
    for (const auto contributor : contributors) {
        const auto* node = find(contributor.chart);
        if (!node) continue;
        const auto* content = std::get_if<bms_chart_content_t>(&node->value);
        if (!content) continue;
        const auto item = std::ranges::find(content->items, contributor.item, &bms_content_item_t::id);
        if (item != content->items.end() && std::holds_alternative<bms_hold_endpoint_t>(item->value)) result.push_back(contributor);
    }
    return result;
}
auto bms_program_t::with_parameter(branch_id_t id, std::optional<std::int64_t> value, bms_program_limits_t limits) const
    -> result_t<bms_program_t, error_t<failure_code_t>> {
    const auto* node = find(id);
    if (!node) return std::unexpected(error_t{failure_code_t::not_found, "Parameter node no longer exists."});
    auto nodes = nodes_;
    auto& parameter = nodes[indices_.at(id.value)].value;
    const auto changed = std::visit([&](auto& setting) -> bool {
        using value_t = std::decay_t<decltype(setting)>;
        if constexpr (std::is_same_v<value_t, bms_case_scope_t>) { setting.value = value; return true; }
        else if constexpr (std::is_same_v<value_t, bms_if_scope_t> || std::is_same_v<value_t, bms_else_if_scope_t> ||
            std::is_same_v<value_t, bms_switch_scope_t> || std::is_same_v<value_t, bms_set_random_t> ||
            std::is_same_v<value_t, bms_set_switch_t>) {
            if (!value) return false;
            setting.value = *value;
            return true;
        } else return false;
    }, parameter);
    if (!changed) return std::unexpected(error_t{failure_code_t::invalid_request,
        "This node has no editable scalar parameter; RANDOM uses its range command."});
    return from(std::move(nodes), limits);
}
auto bms_program_t::with_random_range(branch_id_t id, std::int64_t range, bms_program_limits_t limits) const
    -> result_t<bms_program_t, error_t<failure_code_t>> {
    const auto* node = find(id);
    if (!node || !std::holds_alternative<bms_random_scope_t>(node->value))
        return std::unexpected(error_t{failure_code_t::not_found, "Range edits require an existing RANDOM node."});
    auto nodes = nodes_;
    std::get<bms_random_scope_t>(nodes[indices_.at(id.value)].value).range = range;
    return from(std::move(nodes), limits);
}
auto bms_program_t::is_unreachable_random_arm(branch_id_t id) const -> bool {
    const auto* arm = find(id);
    if (!arm) return false;
    std::optional<std::int64_t> value;
    if (const auto* conditional = std::get_if<bms_if_scope_t>(&arm->value)) value = conditional->value;
    if (const auto* conditional = std::get_if<bms_else_if_scope_t>(&arm->value)) value = conditional->value;
    if (!value) return false;
    const auto selector = conditional_selector(id);
    if (!selector) return false;
    const auto* random = std::get_if<bms_random_scope_t>(&find(*selector)->value);
    return random && (*value < 1 || *value > random->range);
}
auto bms_program_t::unreachable_random_arms(branch_id_t selector) const -> std::vector<branch_id_t> {
    const auto* node = find(selector);
    const auto* random = node ? std::get_if<bms_random_scope_t>(&node->value) : nullptr;
    if (!random) return {};
    std::vector<branch_id_t> result;
    for (const auto& arm : nodes_)
        if (is_unreachable_random_arm(arm.id) && conditional_selector(arm.id) == selector) result.push_back(arm.id);
    return result;
}
auto bms_program_evaluation_t::without_invalid_choices(const bms_program_t& program) const -> bms_program_evaluation_t {
    auto result = *this;
    std::erase_if(result.selections, [&](const auto& choice) {
        const auto* node = program.find(choice.branch);
        if (!node) return true;
        const auto* random = std::get_if<bms_random_scope_t>(&node->value);
        return random && (choice.selected_value < 1 || choice.selected_value > random->range);
    });
    return result;
}
namespace {
struct control_position_t {
    branch_id_t node;
    std::optional<branch_id_t> parent;
    auto operator==(const control_position_t&) const -> bool = default;
};
auto build_control_positions(const bms_program_t& program) -> std::vector<control_position_t> {
    std::vector<control_position_t> result;
    std::vector<branch_id_t> pending{program.root()};
    while (!pending.empty()) {
        const auto* node = program.find(pending.back());
        pending.pop_back();
        if (std::holds_alternative<bms_chart_content_t>(node->value) || std::holds_alternative<bms_text_t>(node->value)) continue;
        result.push_back({.node = node->id, .parent = program.parent(node->id)});
        pending.insert(pending.end(), node->children.rbegin(), node->children.rend());
    }
    return result;
}
auto validate_move_bindings(const bms_program_t& before, const bms_program_t& after)
    -> std::optional<error_t<failure_code_t>> {
    bool has_fixed{};
    for (const auto& node : before.nodes()) {
        if (node.is_if_arm() && before.conditional_selector(node.id) != after.conditional_selector(node.id))
            return error_t{failure_code_t::invalid_request, "Move would rebind an IF arm to a different RANDOM selector."};
        if (std::holds_alternative<bms_case_scope_t>(node.value) &&
            before.conditional_selector(node.id) != after.conditional_selector(node.id))
            return error_t{failure_code_t::invalid_request, "Move would rebind a CASE/DEF to a different SWITCH selector."};
        has_fixed |= std::holds_alternative<bms_set_random_t>(node.value) || std::holds_alternative<bms_set_switch_t>(node.value);
    }
    if (has_fixed && build_control_positions(before) != build_control_positions(after))
        return error_t{failure_code_t::unsupported,
            "Move changes control order or containment in a fixed-selector program; unchanged selector bindings cannot be proven."};
    return std::nullopt;
}
} // namespace

auto bms_program_t::with_move(const bms_program_move_t& move, bms_program_limits_t limits) const
    -> result_t<bms_program_t, error_t<failure_code_t>> {
    const auto old_parent = parent(move.node);
    const auto* destination = find(move.parent);
    if (!old_parent || !destination || !destination->is_scope())
        return std::unexpected(error_t{failure_code_t::not_found, "Move requires a non-Root source and an existing body scope destination."});
    for (auto ancestor = std::optional{move.parent}; ancestor; ancestor = parent(*ancestor))
        if (*ancestor == move.node)
            return std::unexpected(error_t{failure_code_t::invalid_request, "Move cannot place a subtree inside itself."});
    if (move.after == move.node || (move.after && std::ranges::find(destination->children, *move.after) == destination->children.end()))
        return std::unexpected(error_t{failure_code_t::invalid_request, "Move anchor must be another direct child of the destination."});
    auto nodes = nodes_;
    std::erase(nodes[indices_.at(old_parent->value)].children, move.node);
    auto& children = nodes[indices_.at(move.parent.value)].children;
    const auto position = move.after ? std::ranges::find(children, *move.after) + 1 : children.begin();
    children.insert(position, move.node);
    auto proposed = from(std::move(nodes), limits);
    if (!proposed) return proposed;
    if (const auto failure = validate_move_bindings(*this, *proposed)) return std::unexpected(*failure);
    return proposed;
}
auto bms_program_subtree_t::from(const bms_program_t& program, branch_id_t root)
    -> result_t<bms_program_subtree_t, error_t<failure_code_t>> {
    if (!program.find(root))
        return std::unexpected(error_t{failure_code_t::not_found, "Subtree requires an existing BMS node."});
    bms_program_subtree_t result{.root = root, .parent = program.parent(root)};
    std::vector<branch_id_t> pending{root};
    while (!pending.empty()) {
        const auto* node = program.find(pending.back());
        pending.pop_back();
        result.nodes.push_back(node->id);
        pending.insert(pending.end(), node->children.rbegin(), node->children.rend());
        if (const auto* arm = std::get_if<bms_case_scope_t>(&node->value))
            result.content_items += arm->skips.size();
        if (node->is_scope()) ++result.scopes;
        else if (const auto* chart = std::get_if<bms_chart_content_t>(&node->value)) {
            ++result.charts;
            result.content_items += chart->items.size();
            for (const auto& item : chart->items) {
                if (const auto* native = std::get_if<bms_native_content_t>(&item.value))
                    result.content_items += native->declared_ids().size();
                if (const auto* text = std::get_if<bms_text_t>(&item.value)) {
                    ++result.texts;
                    result.text_bytes += text->bytes.size();
                }
            }
        } else if (const auto* text = std::get_if<bms_text_t>(&node->value)) {
            ++result.texts;
            result.text_bytes += text->bytes.size();
        } else ++result.settings;
    }
    return result;
}
auto bms_program_t::unwrap_conversions(branch_id_t id) const -> std::vector<branch_id_t> {
    std::vector<branch_id_t> result;
    const auto* wrapper = find(id);
    if (!wrapper) return result;
    for (const auto& node : nodes_)
        if (conditional_selector(node.id) == id) result.push_back(node.id);
    // Removing an arm also removes the predicate prefix of its following arms.
    if (!wrapper->is_if_arm()) return result;
    const auto containing = parent(id);
    if (!containing) return result;
    const auto& siblings = find(*containing)->children;
    for (auto next = std::ranges::find(siblings, id) + 1; next != siblings.end(); ++next) {
        if (!find(*next)->is_if_continuation()) break;
        result.push_back(*next);
    }
    return result;
}
namespace {
void promote_unwrapped_children(std::vector<bms_program_node_t>& nodes, const bms_program_node_t& wrapper,
    branch_id_t parent) {
    auto& siblings = std::ranges::find(nodes, parent, &bms_program_node_t::id)->children;
    const auto position = std::ranges::find(siblings, wrapper.id);
    const auto previous = position == siblings.begin() ? std::nullopt : std::optional{*(position - 1)};
    const auto offset = siblings.erase(position) - siblings.begin();
    siblings.insert(siblings.begin() + offset, wrapper.children.begin(), wrapper.children.end());
    // Preserve external SKIP execution positions when their lexical scope/anchor moves.
    for (auto& node : nodes) {
        auto* arm = std::get_if<bms_case_scope_t>(&node.value);
        if (!arm) continue;
        for (auto& skip : arm->skips) {
            if (skip.scope == wrapper.id) {
                skip.scope = parent;
                if (!skip.after_child) skip.after_child = previous;
            } else if (skip.scope == parent && skip.after_child == wrapper.id) {
                skip.after_child = wrapper.children.back();
            }
        }
    }
}
auto validate_unwrap_bindings(const bms_program_t& before, const bms_program_t& after)
    -> std::optional<error_t<failure_code_t>> {
    for (const auto& node : after.nodes()) {
        if (std::holds_alternative<bms_bare_scope_t>(node.value)) continue;
        if (before.conditional_selector(node.id) != after.conditional_selector(node.id))
            return error_t{failure_code_t::invalid_request, "Unwrap would rebind a surviving condition."};
    }
    return std::nullopt;
}
} // namespace

auto bms_program_t::with_unwrap(branch_id_t id, bms_program_limits_t limits) const
    -> result_t<bms_program_t, error_t<failure_code_t>> {
    const auto* wrapper = find(id);
    const auto containing = parent(id);
    if (!wrapper || !containing || !wrapper->is_scope())
        return std::unexpected(error_t{failure_code_t::invalid_request, "Unwrap requires a non-Root body scope."});
    if (std::holds_alternative<bms_case_scope_t>(wrapper->value))
        return std::unexpected(error_t{failure_code_t::unsupported,
            "Unwrap the containing SWITCH to make CASE/DEF bodies unconditional; direct promotion would retain switch entry/fallthrough gating."});
    const auto has_fixed = std::ranges::any_of(nodes_, [](const auto& node) {
        return std::holds_alternative<bms_set_random_t>(node.value) || std::holds_alternative<bms_set_switch_t>(node.value);
    });
    if (has_fixed)
        return std::unexpected(error_t{failure_code_t::unsupported,
            "Unwrap cannot prove unchanged fixed-selector bindings in this program."});
    auto nodes = nodes_;
    for (const auto branch : unwrap_conversions(id))
        nodes[indices_.at(branch.value)].value = bms_bare_scope_t{};
    promote_unwrapped_children(nodes, *wrapper, *containing);
    std::erase_if(nodes, [&](const auto& node) { return node.id == id; });
    auto proposed = from(std::move(nodes), limits);
    if (!proposed) return proposed;
    if (const auto failure = validate_unwrap_bindings(*this, *proposed)) return std::unexpected(*failure);
    return proposed;
}

auto bms_program_t::without_subtree(branch_id_t id, bms_program_limits_t limits) const
    -> result_t<bms_program_t, error_t<failure_code_t>> {
    const auto subtree = bms_program_subtree_t::from(*this, id);
    if (!subtree) return std::unexpected(subtree.error());
    if (!subtree->parent)
        return std::unexpected(error_t{failure_code_t::invalid_request, "Root cannot be removed as a subtree."});
    std::unordered_set<std::uint64_t> removed;
    for (const auto node : subtree->nodes) removed.insert(node.value);
    auto nodes = nodes_;
    std::erase(nodes[indices_.at(subtree->parent->value)].children, id);
    std::erase_if(nodes, [&](const auto& node) { return removed.contains(node.id.value); });
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
