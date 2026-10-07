#include <wormedit/core/document.h>
#include <wormedit/core/sv_recipe.h>

#include <algorithm>
#include <cctype>
#include <functional>
#include <limits>
#include <map>
#include <numeric>
#include <ranges>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace wormedit {
namespace {

constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();

auto add_checked(std::uint64_t left, std::uint64_t right, std::uint64_t& result) -> bool {
    if (right > maximum - left) {
        return false;
    }
    result = left + right;
    return true;
}

auto add_diagnostic(diagnostic_list_t& diagnostics, diagnostic_code_t code, std::string message,
                    std::optional<object_id_t> object = std::nullopt) -> void {
    diagnostics.push_back({
        .severity = diagnostic_severity_t::error,
        .code = code,
        .message = std::move(message),
        .object = object,
    });
}

template <typename id_t>
auto require_id(diagnostic_list_t& diagnostics, id_t id, std::string_view description) -> bool {
    if (id) {
        return true;
    }
    add_diagnostic(diagnostics, diagnostic_code_t::invalid_id,
                   std::string(description) + " has the reserved zero ID");
    return false;
}

template <typename range_t, typename projection_t>
auto check_unique_ids(diagnostic_list_t& diagnostics, const range_t& values,
                      projection_t projection, std::string_view description) -> void {
    std::unordered_set<std::uint64_t> ids;
    for (const auto& value : values) {
        const auto id = projection(value);
        if (!require_id(diagnostics, id, description)) {
            continue;
        }
        if (!ids.insert(id.value).second) {
            add_diagnostic(diagnostics, diagnostic_code_t::duplicate_id,
                           std::string(description) + " ID is duplicated");
        }
    }
}

auto measure_error_message(measure_error_t error) -> std::string {
    switch (error) {
    case measure_error_t::none:
        return "measure is valid";
    case measure_error_t::empty:
        return "measure length is empty";
    case measure_error_t::invalid_syntax:
        return "measure length syntax is invalid";
    case measure_error_t::non_positive:
        return "measure length is not positive";
    case measure_error_t::zero_denominator:
        return "measure length has a zero denominator";
    case measure_error_t::overflow:
        return "measure geometry overflows the supported range";
    case measure_error_t::zero_resolution:
        return "chart resolution is zero";
    case measure_error_t::non_integral_capacity:
        return "measure length cannot be represented at the chart resolution";
    case measure_error_t::meter_mismatch:
        return "authored meter does not match measure length";
    }
    return "measure is invalid";
}

auto contains_resource(const std::unordered_set<std::uint64_t>& resources, resource_id_t id)
    -> bool {
    return id && resources.contains(id.value);
}

auto valid_format_family(format_family_t family) -> bool {
    switch (family) {
    case format_family_t::native_project:
    case format_family_t::bms:
    case format_family_t::stepmania:
    case format_family_t::osu_mania:
    case format_family_t::bmson:
    case format_family_t::midi:
    case format_family_t::quaver:
        return true;
    }
    return false;
}

auto canonical_target_identifier(const target_resource_identity_t& identity) -> std::string {
    auto result = identity.identifier;
    if (identity.family == format_family_t::bms) {
        std::ranges::transform(result, result.begin(), [](unsigned char character) {
            return character >= 'a' && character <= 'z' ? static_cast<char>(character - ('a' - 'A'))
                                                        : static_cast<char>(character);
        });
    }
    return result;
}

auto canonical_target_namespace(const target_resource_identity_t& identity) -> std::string {
    auto result = identity.namespace_name;
    if (identity.family == format_family_t::bms) {
        std::ranges::transform(result, result.begin(), [](unsigned char character) {
            return character >= 'a' && character <= 'z' ? static_cast<char>(character - ('a' - 'A'))
                                                        : static_cast<char>(character);
        });
    }
    return result;
}

auto line_ending_size(bms_line_ending_t ending) -> std::optional<std::size_t> {
    switch (ending) {
    case bms_line_ending_t::none:
        return 0;
    case bms_line_ending_t::lf:
    case bms_line_ending_t::cr:
        return 1;
    case bms_line_ending_t::crlf:
        return 2;
    }
    return std::nullopt;
}

auto is_synthetic_control_node(const bms_control_node_t& node) -> bool {
    return node.kind == bms_control_kind_t::if_group;
}

auto first_source_byte(const bms_control_node_t& node) -> std::optional<std::size_t> {
    if (node.kind != bms_control_kind_t::root && !is_synthetic_control_node(node)) {
        return node.source_span.byte_begin;
    }
    for (const auto& child : node.children) {
        if (const auto first = first_source_byte(child); first.has_value()) {
            return first;
        }
    }
    if (node.close_source_span.has_value()) {
        return node.close_source_span->byte_begin;
    }
    return std::nullopt;
}

auto control_kind_has_value(bms_control_kind_t kind) -> bool {
    switch (kind) {
    case bms_control_kind_t::random:
    case bms_control_kind_t::set_random:
    case bms_control_kind_t::if_branch:
    case bms_control_kind_t::else_if_branch:
    case bms_control_kind_t::switch_block:
    case bms_control_kind_t::set_switch:
    case bms_control_kind_t::case_branch:
        return true;
    case bms_control_kind_t::root:
    case bms_control_kind_t::raw_statement:
    case bms_control_kind_t::if_group:
    case bms_control_kind_t::else_branch:
    case bms_control_kind_t::default_branch:
    case bms_control_kind_t::skip:
        return false;
    }
    return false;
}

auto control_kind_requires_positive_value(bms_control_kind_t kind) -> bool {
    switch (kind) {
    case bms_control_kind_t::random:
    case bms_control_kind_t::set_random:
    case bms_control_kind_t::if_branch:
    case bms_control_kind_t::else_if_branch:
    case bms_control_kind_t::switch_block:
    case bms_control_kind_t::set_switch:
    case bms_control_kind_t::case_branch:
        return true;
    case bms_control_kind_t::root:
    case bms_control_kind_t::raw_statement:
    case bms_control_kind_t::if_group:
    case bms_control_kind_t::else_branch:
    case bms_control_kind_t::default_branch:
    case bms_control_kind_t::skip:
        return false;
    }
    return false;
}

auto valid_control_kind(bms_control_kind_t kind) -> bool {
    switch (kind) {
    case bms_control_kind_t::root:
    case bms_control_kind_t::raw_statement:
    case bms_control_kind_t::random:
    case bms_control_kind_t::set_random:
    case bms_control_kind_t::if_group:
    case bms_control_kind_t::if_branch:
    case bms_control_kind_t::else_if_branch:
    case bms_control_kind_t::else_branch:
    case bms_control_kind_t::switch_block:
    case bms_control_kind_t::set_switch:
    case bms_control_kind_t::case_branch:
    case bms_control_kind_t::default_branch:
    case bms_control_kind_t::skip:
        return true;
    }
    return false;
}

auto is_conditional_branch(bms_control_kind_t kind) -> bool {
    return kind == bms_control_kind_t::if_branch || kind == bms_control_kind_t::else_if_branch ||
           kind == bms_control_kind_t::else_branch;
}

auto control_kind_may_have_close(bms_control_kind_t kind) -> bool {
    return kind == bms_control_kind_t::random || kind == bms_control_kind_t::if_group ||
           kind == bms_control_kind_t::switch_block;
}

auto is_switch_branch(bms_control_kind_t kind) -> bool {
    return kind == bms_control_kind_t::case_branch || kind == bms_control_kind_t::default_branch;
}

struct bms_source_record_t {
    bms_source_span_t span;
    std::size_t source_order{};
};

auto original_bms_source_size(const chart_document_t& chart) -> std::optional<std::size_t> {
    for (const auto& group : chart.metadata.format_groups) {
        if (group.family == format_family_t::bms && group.encoding.original_bytes) {
            return group.encoding.original_bytes->size();
        }
    }
    return std::nullopt;
}

auto validate_bms_root_metadata(const bms_control_node_t& root,
                                std::optional<std::size_t> original_source_size,
                                diagnostic_list_t& diagnostics) -> void {
    if (root.kind != bms_control_kind_t::root) {
        add_diagnostic(diagnostics, diagnostic_code_t::invalid_id,
                       "BMS control tree root has a non-root kind");
    }
    if (root.source_span.byte_begin != 0) {
        add_diagnostic(diagnostics, diagnostic_code_t::invalid_id,
                       "BMS control tree root must begin at source byte zero");
    }
    if (original_source_size.has_value() && root.source_span.byte_end != *original_source_size) {
        add_diagnostic(diagnostics, diagnostic_code_t::invalid_id,
                       "BMS control tree root span does not cover its retained source bytes");
    }
    if (root.close_source_span.has_value() || root.close_text.has_value() ||
        root.close_source_order.has_value() || root.close_line_ending.has_value()) {
        add_diagnostic(diagnostics, diagnostic_code_t::invalid_id,
                       "BMS control tree root cannot carry a closing directive");
    }
}

auto validate_bms_source_records(std::vector<bms_source_record_t>& records,
                                 diagnostic_list_t& diagnostics) -> void {
    std::ranges::sort(records, {}, &bms_source_record_t::source_order);
    for (std::size_t index = 1; index < records.size(); ++index) {
        const auto& previous = records[index - 1];
        const auto& current = records[index];
        if (current.source_order <= previous.source_order) {
            add_diagnostic(diagnostics, diagnostic_code_t::invalid_id,
                           "BMS source line order is duplicated or reversed");
        }
        if (current.span.byte_begin < previous.span.byte_end) {
            add_diagnostic(diagnostics, diagnostic_code_t::invalid_id,
                           "BMS source spans overlap or are out of source order");
        }
    }
}

auto validate_active_bms_branch_selection(
    const chart_document_t& chart,
    const std::unordered_map<std::uint64_t, const bms_control_node_t*>& nodes_by_id,
    diagnostic_list_t& diagnostics) -> void {
    std::unordered_set<std::uint64_t> selected_nodes;
    for (const auto& selection : chart.active_branch_selection) {
        if (!require_id(diagnostics, selection.branch, "active branch selection")) {
            continue;
        }
        if (!selected_nodes.insert(selection.branch.value).second) {
            add_diagnostic(diagnostics, diagnostic_code_t::duplicate_id,
                           "active branch selection refers to the same control block twice");
        }
        const auto found = nodes_by_id.find(selection.branch.value);
        if (found == nodes_by_id.end()) {
            add_diagnostic(diagnostics, diagnostic_code_t::invalid_id,
                           "active branch selection refers to an unknown control node");
            continue;
        }
        const auto& selected = *found->second;
        if (selected.kind == bms_control_kind_t::random) {
            if (!selected.value.has_value() || selection.selected_value < 1 ||
                selection.selected_value > *selected.value) {
                add_diagnostic(diagnostics, diagnostic_code_t::invalid_id,
                               "active RANDOM selection is outside its declared range");
            }
        } else if (selected.kind == bms_control_kind_t::switch_block) {
            const auto has_matching_case =
                std::ranges::any_of(selected.children, [&](const auto& child) {
                    return child.kind == bms_control_kind_t::case_branch &&
                           child.value.has_value() && *child.value == selection.selected_value;
                });
            const auto has_default = std::ranges::any_of(selected.children, [](const auto& child) {
                return child.kind == bms_control_kind_t::default_branch;
            });
            if (!has_matching_case && !has_default) {
                add_diagnostic(diagnostics, diagnostic_code_t::invalid_id,
                               "active SWITCH selection matches no CASE and has no DEFAULT");
            }
        } else {
            add_diagnostic(diagnostics, diagnostic_code_t::invalid_id,
                           "active branch selection must refer to a RANDOM or SWITCH block");
        }
    }
}

struct bms_control_visit_context_t {
    diagnostic_list_t& diagnostics;
    std::optional<std::size_t> original_source_size;
    std::unordered_map<std::uint64_t, const bms_control_node_t*>& nodes_by_id;
    std::unordered_set<std::uint64_t>& node_ids;
    std::vector<bms_source_record_t>& source_records;
};

auto bms_span_is_valid(bms_control_visit_context_t& context, bms_source_span_t span,
                       std::string_view description) -> bool {
    auto valid = true;
    if (span.byte_end <= span.byte_begin) {
        add_diagnostic(context.diagnostics, diagnostic_code_t::invalid_id,
                       std::string(description) + " has an empty or reversed source span");
        valid = false;
    }
    if (context.original_source_size.has_value() && span.byte_end > *context.original_source_size) {
        add_diagnostic(context.diagnostics, diagnostic_code_t::invalid_id,
                       std::string(description) + " extends past the retained source bytes");
        valid = false;
    }
    return valid;
}

auto validate_bms_line_metadata(bms_control_visit_context_t& context, bms_source_span_t span,
                                bms_line_ending_t ending, std::string_view description) -> void {
    const auto ending_size = line_ending_size(ending);
    if (!ending_size.has_value()) {
        add_diagnostic(context.diagnostics, diagnostic_code_t::invalid_id,
                       std::string(description) + " has an unknown line ending");
    } else if (span.byte_end >= span.byte_begin && span.byte_end - span.byte_begin < *ending_size) {
        add_diagnostic(context.diagnostics, diagnostic_code_t::invalid_id,
                       std::string(description) + " span is shorter than its line ending");
    }
}

auto validate_bms_node_identity(bms_control_visit_context_t& context,
                                const bms_control_node_t& node, bool is_root) -> void {
    if (!require_id(context.diagnostics, node.id, "BMS control node")) {
        // Continue validating shape and provenance even for a zero ID.
    } else if (!context.node_ids.insert(node.id.value).second) {
        add_diagnostic(context.diagnostics, diagnostic_code_t::duplicate_id,
                       "BMS control node ID is duplicated");
    } else {
        context.nodes_by_id.emplace(node.id.value, &node);
    }
    if (!is_root && node.kind == bms_control_kind_t::root) {
        add_diagnostic(context.diagnostics, diagnostic_code_t::invalid_id,
                       "nested BMS control node has root kind");
    }
    if (!valid_control_kind(node.kind)) {
        add_diagnostic(context.diagnostics, diagnostic_code_t::invalid_id,
                       "BMS control node has an unknown kind");
    }
    if (control_kind_has_value(node.kind) && !node.value.has_value()) {
        add_diagnostic(context.diagnostics, diagnostic_code_t::invalid_id,
                       "BMS control node kind requires a numeric value");
    }
    if (control_kind_requires_positive_value(node.kind) && node.value.has_value() &&
        *node.value <= 0) {
        add_diagnostic(context.diagnostics, diagnostic_code_t::invalid_id,
                       "BMS control node requires a positive numeric value");
    }
}

auto validate_bms_close_fields(bms_control_visit_context_t& context, const bms_control_node_t& node)
    -> bool {
    const auto has_close_span = node.close_source_span.has_value();
    if (has_close_span != node.close_text.has_value() ||
        has_close_span != node.close_source_order.has_value() ||
        has_close_span != node.close_line_ending.has_value()) {
        add_diagnostic(context.diagnostics, diagnostic_code_t::invalid_id,
                       "BMS control node closing source fields must be present together");
    }
    if (has_close_span && !control_kind_may_have_close(node.kind)) {
        add_diagnostic(context.diagnostics, diagnostic_code_t::invalid_id,
                       "BMS control node kind cannot carry a closing directive");
    }
    if (control_kind_may_have_close(node.kind) && !has_close_span) {
        add_diagnostic(context.diagnostics, diagnostic_code_t::invalid_id,
                       "BMS control block is missing its closing directive");
    }
    return has_close_span;
}

struct bms_node_bounds_t {
    std::size_t child_begin{};
    std::size_t child_end{};
    std::size_t subtree_end{};
};

auto validate_bms_node_source(bms_control_visit_context_t& context, const bms_control_node_t& node,
                              bool is_root, std::size_t parent_begin, std::size_t parent_end)
    -> bms_node_bounds_t {
    auto bounds = bms_node_bounds_t{parent_begin, parent_end, parent_end};
    if (is_root) {
        if (node.source_span.byte_end < node.source_span.byte_begin) {
            add_diagnostic(context.diagnostics, diagnostic_code_t::invalid_id,
                           "BMS control tree root has a reversed source span");
        } else {
            if (context.original_source_size.has_value() &&
                node.source_span.byte_end > *context.original_source_size) {
                add_diagnostic(context.diagnostics, diagnostic_code_t::invalid_id,
                               "BMS control tree root extends past its retained source bytes");
            }
            bounds.child_begin = node.source_span.byte_begin;
            bounds.child_end = node.source_span.byte_end;
        }
        if (!node.original_text.empty() || node.source_order != 0 ||
            node.line_ending != bms_line_ending_t::none) {
            add_diagnostic(context.diagnostics, diagnostic_code_t::invalid_id,
                           "BMS control tree root cannot represent a physical source line");
        }
        return bounds;
    }
    if (is_synthetic_control_node(node)) {
        if (node.source_span.byte_begin != 0 || node.source_span.byte_end != 0 ||
            !node.original_text.empty() || node.source_order != 0 ||
            node.line_ending != bms_line_ending_t::none) {
            add_diagnostic(context.diagnostics, diagnostic_code_t::invalid_id,
                           "synthetic IF group cannot carry a physical source line");
        }
        return bounds;
    }
    const auto span_valid = bms_span_is_valid(context, node.source_span, "BMS control node");
    validate_bms_line_metadata(context, node.source_span, node.line_ending, "BMS control node");
    if (node.source_span.byte_begin < parent_begin || node.source_span.byte_end > parent_end) {
        add_diagnostic(context.diagnostics, diagnostic_code_t::invalid_id,
                       "BMS control node source span escapes its containing control block");
    }
    if (span_valid) {
        context.source_records.push_back({node.source_span, node.source_order});
    }
    bounds.child_begin = node.source_span.byte_end;
    return bounds;
}

auto validate_bms_close_source(bms_control_visit_context_t& context, const bms_control_node_t& node,
                               bms_node_bounds_t& bounds) -> void {
    if (!node.close_source_span.has_value()) {
        return;
    }
    const auto close = *node.close_source_span;
    const auto close_valid = bms_span_is_valid(context, close, "BMS closing directive");
    if (node.close_line_ending.has_value()) {
        validate_bms_line_metadata(context, close, *node.close_line_ending,
                                   "BMS closing directive");
    }
    if (close.byte_begin < bounds.child_begin || close.byte_end > bounds.child_end) {
        add_diagnostic(context.diagnostics, diagnostic_code_t::invalid_id,
                       "BMS closing directive lies outside its control block source span");
    }
    if (close_valid && node.close_source_order.has_value()) {
        context.source_records.push_back({close, *node.close_source_order});
    }
    if (close.byte_begin >= bounds.child_begin && close.byte_begin <= bounds.child_end) {
        bounds.subtree_end = close.byte_begin;
    }
}

auto validate_bms_if_branch_order(bms_control_visit_context_t& context,
                                  const bms_control_node_t& node) -> void {
    auto saw_else = false;
    if (node.children.empty() || node.children.front().kind != bms_control_kind_t::if_branch) {
        add_diagnostic(context.diagnostics, diagnostic_code_t::invalid_id,
                       "BMS IF group must begin with an IF branch");
    }
    for (std::size_t index = 0; index < node.children.size(); ++index) {
        const auto kind = node.children[index].kind;
        const auto is_else = kind == bms_control_kind_t::else_branch;
        if (!is_conditional_branch(kind) || saw_else ||
            (is_else && index + 1 != node.children.size())) {
            add_diagnostic(context.diagnostics, diagnostic_code_t::invalid_id,
                           "BMS IF group has an invalid branch kind or ordering");
            break;
        }
        saw_else = saw_else || is_else;
    }
}

auto validate_bms_switch_branch_order(bms_control_visit_context_t& context,
                                      const bms_control_node_t& node) -> void {
    auto saw_default = false;
    for (std::size_t index = 0; index < node.children.size(); ++index) {
        const auto kind = node.children[index].kind;
        if (!is_switch_branch(kind)) {
            continue;
        }
        if (kind == bms_control_kind_t::default_branch) {
            if (saw_default || index + 1 != node.children.size()) {
                add_diagnostic(context.diagnostics, diagnostic_code_t::invalid_id,
                               "BMS switch default branch must be unique and last");
                break;
            }
            saw_default = true;
        } else if (kind == bms_control_kind_t::case_branch && saw_default) {
            add_diagnostic(context.diagnostics, diagnostic_code_t::invalid_id,
                           "BMS switch case cannot follow its default branch");
            break;
        }
    }
}

auto validate_bms_branch_order(bms_control_visit_context_t& context, const bms_control_node_t& node)
    -> void {
    if (node.kind == bms_control_kind_t::if_group) {
        validate_bms_if_branch_order(context, node);
    } else if (node.kind == bms_control_kind_t::switch_block) {
        validate_bms_switch_branch_order(context, node);
    }
}

auto visit_bms_control_node(bms_control_visit_context_t& context, const bms_control_node_t& node,
                            bool is_root, std::size_t parent_begin, std::size_t parent_end)
    -> void {
    validate_bms_node_identity(context, node, is_root);
    validate_bms_close_fields(context, node);
    auto bounds = validate_bms_node_source(context, node, is_root, parent_begin, parent_end);
    validate_bms_close_source(context, node, bounds);
    validate_bms_branch_order(context, node);
    for (std::size_t index = 0; index < node.children.size(); ++index) {
        const auto child_kind = node.children[index].kind;
        if ((is_conditional_branch(child_kind) && node.kind != bms_control_kind_t::if_group) ||
            (is_switch_branch(child_kind) && node.kind != bms_control_kind_t::switch_block)) {
            add_diagnostic(context.diagnostics, diagnostic_code_t::invalid_id,
                           "BMS branch node is attached to the wrong control block kind");
        }
        auto sibling_end = bounds.subtree_end;
        for (auto next = index + 1; next < node.children.size(); ++next) {
            if (const auto first = first_source_byte(node.children[next]); first.has_value()) {
                sibling_end = std::min(sibling_end, *first);
                break;
            }
        }
        visit_bms_control_node(context, node.children[index], false, bounds.child_begin,
                               sibling_end);
    }
}

auto validate_bms_control_tree(const chart_document_t& chart, diagnostic_list_t& diagnostics)
    -> void {
    std::unordered_map<std::uint64_t, const bms_control_node_t*> nodes_by_id;
    std::unordered_set<std::uint64_t> node_ids;
    std::vector<bms_source_record_t> source_records;
    const auto original_source_size = original_bms_source_size(chart);

    if (!chart.bms_control_tree.has_value()) {
        if (!chart.active_branch_selection.empty()) {
            add_diagnostic(diagnostics, diagnostic_code_t::invalid_id,
                           "active branch selection exists without a BMS control tree");
        }
        return;
    }

    const auto& root = *chart.bms_control_tree;
    validate_bms_root_metadata(root, original_source_size, diagnostics);

    auto context = bms_control_visit_context_t{diagnostics, original_source_size, nodes_by_id,
                                               node_ids, source_records};
    visit_bms_control_node(context, root, true, root.source_span.byte_begin,
                           root.source_span.byte_end);

    validate_bms_source_records(source_records, diagnostics);
    validate_active_bms_branch_selection(chart, nodes_by_id, diagnostics);
}

auto position_row_or_diagnostic(const measure_index_t& index, local_position_t position,
                                diagnostic_list_t& diagnostics,
                                std::optional<object_id_t> object = std::nullopt)
    -> std::optional<std::uint64_t> {
    const auto entry = std::ranges::find_if(
        index.entries, [&](const auto& candidate) { return candidate.id == position.measure; });
    if (entry == index.entries.end()) {
        add_diagnostic(diagnostics, diagnostic_code_t::unknown_measure,
                       "object position refers to an unknown measure", object);
        return std::nullopt;
    }
    if (position.row >= entry->row_capacity) {
        add_diagnostic(diagnostics, diagnostic_code_t::row_out_of_range,
                       "object row lies outside its measure", object);
        return std::nullopt;
    }
    std::uint64_t row{};
    if (!add_checked(entry->absolute_begin_row, position.row, row)) {
        add_diagnostic(diagnostics, diagnostic_code_t::invalid_measure,
                       "absolute object row overflows the supported range", object);
        return std::nullopt;
    }
    return row;
}

auto effective_layout(const chart_document_t& chart, const measure_index_t& index,
                      local_position_t position) -> const lane_layout_t* {
    const auto* selected = static_cast<const lane_layout_t*>(nullptr);
    auto selected_row = std::uint64_t{};
    const auto event_row = absolute_row(index, position);
    if (!event_row.has_value()) {
        return nullptr;
    }
    for (const auto& segment : chart.lane_layouts) {
        const auto segment_row = absolute_row(index, segment.position);
        if (segment_row.has_value() && *segment_row <= *event_row &&
            (selected == nullptr || *segment_row >= selected_row)) {
            selected = &segment.layout;
            selected_row = *segment_row;
        }
    }
    return selected;
}

auto lane_is_active(const chart_document_t& chart, const measure_index_t& index,
                    local_position_t position, lane_id_t lane) -> bool {
    const auto* layout = effective_layout(chart, index, position);
    return layout != nullptr && std::ranges::any_of(layout->lanes, [lane](const auto& candidate) {
               return candidate.id == lane;
           });
}

auto combined_layout(const chart_document_t& chart) -> lane_layout_t {
    lane_layout_t result;
    std::unordered_set<std::uint64_t> inserted;
    for (const auto& segment : chart.lane_layouts) {
        result.split_stage = result.split_stage || segment.layout.split_stage;
        for (const auto& lane : segment.layout.lanes) {
            if (lane.id && inserted.insert(lane.id.value).second) {
                result.lanes.push_back(lane);
            }
        }
    }
    return result;
}

auto validate_object_id(diagnostic_list_t& diagnostics,
                        std::unordered_set<std::uint64_t>& object_ids, object_id_t id,
                        std::string_view description) -> void {
    if (!require_id(diagnostics, id, description)) {
        return;
    }
    if (!object_ids.insert(id.value).second) {
        add_diagnostic(diagnostics, diagnostic_code_t::duplicate_id,
                       std::string(description) + " reuses an object ID", id);
    }
}

void validate_format_groups(const std::vector<format_metadata_t>& groups,
                            diagnostic_list_t& diagnostics) {
    for (const auto& group : groups) {
        if (group.ordered_field_source_orders.size() > group.ordered_fields.size()) {
            add_diagnostic(diagnostics, diagnostic_code_t::invalid_id,
                           "format metadata has more source-order identities than fields");
            continue;
        }
        std::unordered_set<std::size_t> source_orders;
        for (const auto source_order : group.ordered_field_source_orders) {
            if (!source_orders.insert(source_order).second) {
                add_diagnostic(diagnostics, diagnostic_code_t::duplicate_id,
                               "format metadata source-order identity is duplicated");
            }
        }
    }
}

auto validate_bms_measure_projections(const chart_document_t& chart, diagnostic_list_t& diagnostics)
    -> void {
    std::unordered_set<std::size_t> projected_measure_indices;
    for (const auto& projection : chart.bms_measure_length_projections) {
        if (projection.measure_index >= chart.measures.size()) {
            add_diagnostic(diagnostics, diagnostic_code_t::invalid_measure,
                           "BMS measure-length projection refers to an unknown measure index");
        } else if (!projected_measure_indices.insert(projection.measure_index).second) {
            add_diagnostic(diagnostics, diagnostic_code_t::duplicate_id,
                           "BMS measure-length projection is duplicated for one measure");
        }
        if (projection.imported_length.numerator == 0 ||
            projection.imported_length.denominator == 0) {
            add_diagnostic(diagnostics, diagnostic_code_t::invalid_rational,
                           "BMS imported measure-length baseline must be positive");
        }
    }
}

auto validate_stem_lane_links(const chart_document_t& chart, diagnostic_list_t& diagnostics) -> void {
    std::unordered_map<std::uint64_t, std::size_t> channels;
    for (const auto& channel : chart.sound_channels) ++channels[channel.id.value];
    std::unordered_set<std::uint64_t> linked;
    for (const auto& lane : chart.bgm_source_lanes) {
        if (!lane.stem_channel) continue;
        const auto channel = lane.stem_channel->value;
        if (chart.source_family != format_family_t::bmson || !channel || channels[channel] != 1 ||
            !linked.insert(channel).second)
            add_diagnostic(diagnostics, diagnostic_code_t::invalid_id,
                "chart " + std::to_string(chart.id.value) + " (" + chart.metadata.name +
                ") stem lane " + std::to_string(lane.id.value) + " (" + lane.name +
                ") refers to a missing or ambiguous sound channel", lane.id);
    }
}

auto validate_chart_recipes(const chart_document_t& chart, diagnostic_list_t& diagnostics) -> void {
    check_unique_ids(
        diagnostics, chart.semantic_layers, [](const auto& layer) { return layer.id; },
        "semantic layer");
    check_unique_ids(
        diagnostics, chart.sv_recipes, [](const auto& recipe) { return recipe.id; }, "SV recipe");
    if (chart.sv_recipes.size() > maximum_sv_recipes_per_chart) {
        add_diagnostic(diagnostics, diagnostic_code_t::invalid_resolution,
                       "chart exceeds the maximum number of SV recipes");
    }
    for (const auto& recipe : chart.sv_recipes) {
        const auto recipe_diagnostics = validate_sv_recipe(chart, recipe);
        diagnostics.insert(diagnostics.end(), recipe_diagnostics.begin(), recipe_diagnostics.end());
    }
    check_unique_ids(
        diagnostics, chart.bgm_source_lanes, [](const auto& lane) { return lane.id; },
        "BGM source lane");
}

struct retained_sound_event_t {
    const sound_channel_t* channel{};
    const sound_channel_event_t* event{};
};

struct semantic_sound_links_t {
    const std::unordered_map<std::uint64_t, asset_id_t>& resource_assets;
    diagnostic_list_t& diagnostics;
    std::unordered_map<std::uint64_t, retained_sound_event_t> events;
    std::unordered_set<std::uint64_t> ambiguous_ids;
    std::unordered_set<std::uint64_t> linked_ids;
};

auto collect_retained_sound_events(const chart_document_t& chart, semantic_sound_links_t& links)
    -> void {
    for (const auto& channel : chart.sound_channels) {
        for (const auto& event : channel.events) {
            if (!event.id ||
                !links.events.emplace(event.id.value, retained_sound_event_t{&channel, &event})
                     .second) {
                if (event.id) {
                    links.ambiguous_ids.insert(event.id.value);
                }
            }
        }
    }
}

auto validate_semantic_sound_link(semantic_sound_links_t& links, object_id_t semantic_id,
                                  std::optional<object_id_t> linked_id, bool playable,
                                  std::optional<resource_id_t> sound) -> void {
    if (!linked_id.has_value()) {
        return;
    }
    const auto linked = links.events.find(linked_id->value);
    if (!*linked_id || linked == links.events.end() ||
        links.ambiguous_ids.contains(linked_id->value)) {
        add_diagnostic(links.diagnostics, diagnostic_code_t::invalid_id,
                       "semantic sound link refers to an unknown or ambiguous retained event",
                       semantic_id);
        return;
    }
    if (!links.linked_ids.insert(linked_id->value).second) {
        add_diagnostic(links.diagnostics, diagnostic_code_t::duplicate_id,
                       "retained sound event is linked by more than one semantic object",
                       semantic_id);
    }
    const auto& event = *linked->second.event;
    if (event.playable != playable) {
        add_diagnostic(links.diagnostics, diagnostic_code_t::invalid_id,
                       "semantic sound link does not match the retained event role", semantic_id);
    }
    const auto resource =
        sound.has_value() ? links.resource_assets.find(sound->value) : links.resource_assets.end();
    if (!sound.has_value() || !linked->second.channel->source_clip.source ||
        resource == links.resource_assets.end() ||
        resource->second != linked->second.channel->source_clip.source) {
        add_diagnostic(links.diagnostics, diagnostic_code_t::dangling_resource,
                       "semantic sound link requires a sound resource bound to its source clip",
                       semantic_id);
    }
}

struct chart_object_validation_t {
    const chart_document_t& chart;
    const measure_index_t& index;
    const std::unordered_set<std::uint64_t>& resources;
    diagnostic_list_t& diagnostics;
    std::unordered_set<std::uint64_t>& object_ids;
    const std::unordered_set<std::uint64_t>& layer_ids;
    const std::unordered_set<std::uint64_t>& bgm_lane_ids;
    semantic_sound_links_t& sound_links;
};

auto validate_chart_note(chart_object_validation_t& context, measure_id_t measure_id,
                         const note_t& note) -> void {
    validate_object_id(context.diagnostics, context.object_ids, note.id, "note");
    const auto start =
        position_row_or_diagnostic(context.index, note.position, context.diagnostics, note.id);
    if (note.position.measure != measure_id) {
        add_diagnostic(context.diagnostics, diagnostic_code_t::invalid_measure,
                       "note is stored outside the measure named by its position", note.id);
    }
    if (!lane_is_active(context.chart, context.index, note.position, note.lane)) {
        add_diagnostic(context.diagnostics, diagnostic_code_t::unknown_lane,
                       "note refers to a lane absent from its active layout", note.id);
    }
    if (note.sound.has_value() && !contains_resource(context.resources, *note.sound)) {
        add_diagnostic(context.diagnostics, diagnostic_code_t::dangling_resource,
                       "note refers to an unknown sound resource", note.id);
    }
    validate_semantic_sound_link(context.sound_links, note.id, note.sound_channel_event, true,
                                 note.sound);
    for (const auto layer : note.semantic_layers) {
        if (!layer || !context.layer_ids.contains(layer.value)) {
            add_diagnostic(context.diagnostics, diagnostic_code_t::invalid_id,
                           "note refers to an unknown semantic layer", note.id);
        }
    }
    const auto requires_end = note.kind == note_kind_t::hold || note.kind == note_kind_t::roll;
    const auto may_have_end = requires_end || note.kind == note_kind_t::fake;
    if (requires_end ? !note.end.has_value() : (note.end.has_value() && !may_have_end)) {
        add_diagnostic(context.diagnostics, diagnostic_code_t::invalid_hold,
                       "hold/roll notes require an end and only fake notes may otherwise carry one",
                       note.id);
    } else if (note.end.has_value()) {
        const auto end =
            position_row_or_diagnostic(context.index, *note.end, context.diagnostics, note.id);
        if (start.has_value() && end.has_value() && *end <= *start) {
            add_diagnostic(context.diagnostics, diagnostic_code_t::invalid_hold,
                           "hold/roll end must be after its start", note.id);
        }
        if (!lane_is_active(context.chart, context.index, *note.end, note.lane)) {
            add_diagnostic(context.diagnostics, diagnostic_code_t::unknown_lane,
                           "hold/roll lane is absent from the layout at its end", note.id);
        }
    }
}

auto validate_chart_bgm(chart_object_validation_t& context, measure_id_t measure_id,
                        const bgm_event_t& bgm) -> void {
    validate_object_id(context.diagnostics, context.object_ids, bgm.id, "BGM event");
    position_row_or_diagnostic(context.index, bgm.position, context.diagnostics, bgm.id);
    if (bgm.position.measure != measure_id) {
        add_diagnostic(context.diagnostics, diagnostic_code_t::invalid_measure,
                       "BGM event is stored outside the measure named by its position", bgm.id);
    }
    if (!context.bgm_lane_ids.contains(bgm.source_lane.value)) {
        add_diagnostic(context.diagnostics, diagnostic_code_t::invalid_id,
                       "BGM event refers to an unknown source lane", bgm.id);
    }
    if (bgm.sound.has_value() && !contains_resource(context.resources, *bgm.sound)) {
        add_diagnostic(context.diagnostics, diagnostic_code_t::dangling_resource,
                       "BGM event refers to an unknown sound resource", bgm.id);
    }
    validate_semantic_sound_link(context.sound_links, bgm.id, bgm.sound_channel_event, false,
                                 bgm.sound);
    if (bgm.semantic_layers.size() > 1) {
        add_diagnostic(context.diagnostics, diagnostic_code_t::invalid_id,
                       "BGM event may belong to at most one semantic layer", bgm.id);
    }
    for (const auto layer : bgm.semantic_layers) {
        if (!layer || !context.layer_ids.contains(layer.value)) {
            add_diagnostic(context.diagnostics, diagnostic_code_t::invalid_id,
                           "BGM event refers to an unknown semantic layer", bgm.id);
        }
    }
}

auto validate_chart_notes_and_bgm(chart_object_validation_t& context) -> void {
    for (const auto& measure : context.chart.measures) {
        for (const auto& note : measure.notes) {
            validate_chart_note(context, measure.id, note);
        }
        for (const auto& bgm : measure.bgm_events) {
            validate_chart_bgm(context, measure.id, bgm);
        }
    }
}

auto validate_positioned_chart_events(chart_object_validation_t& context) -> void {
    const auto validate_positioned = [&](const auto& values, std::string_view description) {
        for (const auto& value : values) {
            validate_object_id(context.diagnostics, context.object_ids, value.id, description);
            position_row_or_diagnostic(context.index, value.position, context.diagnostics,
                                       value.id);
        }
    };
    validate_positioned(context.chart.bpms, "BPM event");
    validate_positioned(context.chart.pauses, "pause event");
    validate_positioned(context.chart.scrolls, "scroll event");
    validate_positioned(context.chart.speeds, "speed event");
    validate_positioned(context.chart.kiai, "kiai event");
    validate_positioned(context.chart.timing_anchors, "timing anchor");
    validate_positioned(context.chart.bga_events, "BGA event");
}

auto validate_bpm_pause_rationals(chart_object_validation_t& context) -> void {
    for (const auto& bpm : context.chart.bpms) {
        if (bpm.beats_per_minute.denominator == 0 || bpm.beats_per_minute.numerator <= 0) {
            add_diagnostic(context.diagnostics, diagnostic_code_t::invalid_rational,
                           "BPM must be a positive rational", bpm.id);
        }
    }
    for (const auto& pause : context.chart.pauses) {
        if (pause.duration.denominator == 0 || pause.duration.numerator < 0) {
            add_diagnostic(context.diagnostics, diagnostic_code_t::invalid_rational,
                           "pause duration must be a non-negative rational", pause.id);
        }
    }
}

auto validate_bms_timing_projections(chart_object_validation_t& context) -> void {
    std::unordered_set<std::uint64_t> projected_bpm_ids;
    for (const auto& projection : context.chart.bms_bpm_projections) {
        const auto event =
            std::ranges::find(context.chart.bpms, projection.event, &bpm_event_t::id);
        if (!projection.event || event == context.chart.bpms.end()) {
            add_diagnostic(context.diagnostics, diagnostic_code_t::invalid_id,
                           "BMS BPM projection refers to an unknown event", projection.event);
        } else if (!projected_bpm_ids.insert(projection.event.value).second) {
            add_diagnostic(context.diagnostics, diagnostic_code_t::duplicate_id,
                           "BMS BPM projection is duplicated for one event", projection.event);
        } else if (event->provenance.source_order != projection.source_order) {
            add_diagnostic(context.diagnostics, diagnostic_code_t::invalid_id,
                           "BMS BPM projection source identity does not match its event",
                           projection.event);
        }
        if (projection.imported_beats_per_minute.denominator == 0 ||
            projection.imported_beats_per_minute.numerator <= 0) {
            add_diagnostic(context.diagnostics, diagnostic_code_t::invalid_rational,
                           "BMS imported BPM baseline must be positive", projection.event);
        }
    }
    std::unordered_set<std::uint64_t> projected_pause_ids;
    for (const auto& projection : context.chart.bms_pause_projections) {
        const auto event =
            std::ranges::find(context.chart.pauses, projection.event, &pause_event_t::id);
        if (!projection.event || event == context.chart.pauses.end()) {
            add_diagnostic(context.diagnostics, diagnostic_code_t::invalid_id,
                           "BMS pause projection refers to an unknown event", projection.event);
        } else if (!projected_pause_ids.insert(projection.event.value).second) {
            add_diagnostic(context.diagnostics, diagnostic_code_t::duplicate_id,
                           "BMS pause projection is duplicated for one event", projection.event);
        } else if (event->provenance.source_order != projection.source_order) {
            add_diagnostic(context.diagnostics, diagnostic_code_t::invalid_id,
                           "BMS pause projection source identity does not match its event",
                           projection.event);
        }
        if (projection.imported_duration.denominator == 0 ||
            projection.imported_duration.numerator < 0) {
            add_diagnostic(context.diagnostics, diagnostic_code_t::invalid_rational,
                           "BMS imported pause baseline must be non-negative", projection.event);
        }
    }
}

auto validate_scroll_speed_warp_bga(chart_object_validation_t& context) -> void {
    for (const auto& scroll : context.chart.scrolls) {
        if (scroll.multiplier.denominator == 0) {
            add_diagnostic(context.diagnostics, diagnostic_code_t::invalid_rational,
                           "scroll multiplier has a zero denominator", scroll.id);
        }
    }
    for (const auto& speed : context.chart.speeds) {
        if (speed.multiplier.denominator == 0 || speed.transition_beats.denominator == 0) {
            add_diagnostic(context.diagnostics, diagnostic_code_t::invalid_rational,
                           "speed event contains an invalid rational", speed.id);
        }
    }
    for (const auto& warp : context.chart.warps) {
        validate_object_id(context.diagnostics, context.object_ids, warp.id, "warp event");
        const auto begin = position_row_or_diagnostic(context.index, warp.span.begin,
                                                      context.diagnostics, warp.id);
        const auto end =
            position_row_or_diagnostic(context.index, warp.span.end, context.diagnostics, warp.id);
        if (begin.has_value() && end.has_value() && *end <= *begin) {
            add_diagnostic(context.diagnostics, diagnostic_code_t::invalid_hold,
                           "warp end must be after its start", warp.id);
        }
    }
    for (const auto& bga : context.chart.bga_events) {
        if (!contains_resource(context.resources, bga.resource)) {
            add_diagnostic(context.diagnostics, diagnostic_code_t::dangling_resource,
                           "BGA event refers to an unknown resource", bga.id);
        }
    }
}

auto validate_chart_bookmarks(chart_object_validation_t& context) -> void {
    for (const auto& bookmark : context.chart.bookmarks) {
        validate_object_id(context.diagnostics, context.object_ids, bookmark.id, "bookmark");
        const auto valid_beat = bookmark.anchor == marker_anchor_t::beat &&
                                bookmark.position.has_value() &&
                                !bookmark.audio_milliseconds.has_value();
        const auto valid_audio = bookmark.anchor == marker_anchor_t::audio_time &&
                                 !bookmark.position.has_value() &&
                                 bookmark.audio_milliseconds.has_value();
        if (!valid_beat && !valid_audio) {
            add_diagnostic(context.diagnostics, diagnostic_code_t::invalid_id,
                           "bookmark must carry exactly the value selected by its anchor kind",
                           bookmark.id);
        }
        if (bookmark.position.has_value()) {
            position_row_or_diagnostic(context.index, *bookmark.position, context.diagnostics,
                                       bookmark.id);
        }
    }
}

auto validate_chart_sections(chart_object_validation_t& context) -> void {
    for (const auto& section : context.chart.sections) {
        validate_object_id(context.diagnostics, context.object_ids, section.id, "section");
        const auto valid_beat = section.anchor == marker_anchor_t::beat &&
                                section.beat_span.has_value() &&
                                !section.audio_millisecond_span.has_value();
        const auto valid_audio = section.anchor == marker_anchor_t::audio_time &&
                                 !section.beat_span.has_value() &&
                                 section.audio_millisecond_span.has_value();
        if (!valid_beat && !valid_audio) {
            add_diagnostic(context.diagnostics, diagnostic_code_t::invalid_id,
                           "section must carry exactly the span selected by its anchor kind",
                           section.id);
        }
        if (section.beat_span.has_value()) {
            const auto begin = position_row_or_diagnostic(context.index, section.beat_span->begin,
                                                          context.diagnostics, section.id);
            const auto end = position_row_or_diagnostic(context.index, section.beat_span->end,
                                                        context.diagnostics, section.id);
            if (begin.has_value() && end.has_value() && *end <= *begin) {
                add_diagnostic(context.diagnostics, diagnostic_code_t::invalid_hold,
                               "beat-anchored section end must be after its start", section.id);
            }
        }
        if (section.audio_millisecond_span.has_value() &&
            section.audio_millisecond_span->second <= section.audio_millisecond_span->first) {
            add_diagnostic(context.diagnostics, diagnostic_code_t::invalid_hold,
                           "audio-anchored section end must be after its start", section.id);
        }
    }
}

auto validate_sound_channel_event(chart_object_validation_t& context,
                                  const sound_channel_event_t& event) -> void {
    validate_object_id(context.diagnostics, context.object_ids, event.id, "sound channel event");
    const auto begin =
        position_row_or_diagnostic(context.index, event.position, context.diagnostics, event.id);
    if (event.playable && !event.lane) {
        add_diagnostic(context.diagnostics, diagnostic_code_t::unknown_lane,
                       "playable sound channel event must have a non-zero lane", event.id);
    } else if (!event.playable && event.lane) {
        add_diagnostic(context.diagnostics, diagnostic_code_t::unknown_lane,
                       "non-playable BGM sound channel event must use lane zero", event.id);
    } else if (event.playable &&
               !lane_is_active(context.chart, context.index, event.position, event.lane)) {
        add_diagnostic(context.diagnostics, diagnostic_code_t::unknown_lane,
                       "sound channel event refers to a lane absent from its active layout",
                       event.id);
    }
    if (event.end.has_value()) {
        const auto end =
            position_row_or_diagnostic(context.index, *event.end, context.diagnostics, event.id);
        if (begin.has_value() && end.has_value() && *end <= *begin) {
            add_diagnostic(context.diagnostics, diagnostic_code_t::invalid_hold,
                           "sound channel event end must be after its start", event.id);
        }
    }
}

auto validate_chart_sound_channels(chart_object_validation_t& context,
                                   const std::unordered_set<std::uint64_t>& audio_assets) -> void {
    for (const auto& channel : context.chart.sound_channels) {
        validate_object_id(context.diagnostics, context.object_ids, channel.id, "sound channel");
        if (!channel.source_clip.source ||
            !audio_assets.contains(channel.source_clip.source.value)) {
            add_diagnostic(context.diagnostics, diagnostic_code_t::dangling_asset,
                           "sound channel clip refers to an unknown audio asset", channel.id);
        }
        if (channel.source_clip.end_frame.has_value() &&
            *channel.source_clip.end_frame <= channel.source_clip.begin_frame) {
            add_diagnostic(context.diagnostics, diagnostic_code_t::invalid_hold,
                           "sound channel clip end must be after its start", channel.id);
        }
        for (const auto& event : channel.events) {
            validate_sound_channel_event(context, event);
        }
    }
}

auto validate_sound_layer_defaults(const chart_document_t& chart,
                                   const std::unordered_set<std::uint64_t>& resources,
                                   const std::unordered_set<std::uint64_t>& layer_ids,
                                   diagnostic_list_t& diagnostics) -> void {
    std::unordered_set<std::uint64_t> seen;
    for (const auto& entry : chart.sound_layer_defaults) {
        if (!entry.resource || !resources.contains(entry.resource.value)) {
            add_diagnostic(diagnostics, diagnostic_code_t::dangling_resource,
                           "sound layer default refers to an unknown sound resource");
        } else if (!seen.insert(entry.resource.value).second) {
            add_diagnostic(diagnostics, diagnostic_code_t::duplicate_id,
                           "sound layer default is duplicated for one resource");
        }
        if (entry.layers.empty()) {
            add_diagnostic(diagnostics, diagnostic_code_t::invalid_id,
                           "sound layer default must name at least one layer");
        }
        std::unordered_set<std::uint64_t> listed;
        for (const auto layer : entry.layers) {
            if (!layer || !layer_ids.contains(layer.value)) {
                add_diagnostic(diagnostics, diagnostic_code_t::invalid_id,
                               "sound layer default refers to an unknown semantic layer");
            } else if (!listed.insert(layer.value).second) {
                add_diagnostic(diagnostics, diagnostic_code_t::duplicate_id,
                               "sound layer default lists a layer twice");
            }
        }
    }
}

auto validate_chart(const chart_document_t& chart,
                    const std::unordered_set<std::uint64_t>& resources,
                    const std::unordered_map<std::uint64_t, asset_id_t>& resource_assets,
                    const std::unordered_set<std::uint64_t>& audio_assets,
                    diagnostic_list_t& diagnostics) -> void {
    validate_bms_control_tree(chart, diagnostics);
    validate_format_groups(chart.metadata.format_groups, diagnostics);
    if (chart.divisions_per_standard_measure == 0) {
        add_diagnostic(diagnostics, diagnostic_code_t::invalid_resolution,
                       "chart divisions per standard measure must be positive");
    }
    if (chart.beat_zero_audio_offset_seconds.denominator == 0) {
        add_diagnostic(diagnostics, diagnostic_code_t::invalid_rational,
                       "beat-zero audio offset has a zero denominator");
    }

    const auto built_index = build_measure_index(chart);
    if (std::holds_alternative<diagnostic_list_t>(built_index)) {
        const auto& index_diagnostics = std::get<diagnostic_list_t>(built_index);
        diagnostics.insert(diagnostics.end(), index_diagnostics.begin(), index_diagnostics.end());
        return;
    }
    const auto& index = std::get<measure_index_t>(built_index);
    validate_bms_measure_projections(chart, diagnostics);
    std::unordered_set<std::uint64_t> object_ids;

    validate_chart_recipes(chart, diagnostics);
    validate_stem_lane_links(chart, diagnostics);

    std::unordered_set<std::uint64_t> layer_ids;
    for (const auto& layer : chart.semantic_layers) {
        if (layer.id) {
            layer_ids.insert(layer.id.value);
        }
    }
    validate_sound_layer_defaults(chart, resources, layer_ids, diagnostics);
    std::unordered_set<std::uint64_t> bgm_lane_ids;
    for (const auto& lane : chart.bgm_source_lanes) {
        if (lane.id) {
            bgm_lane_ids.insert(lane.id.value);
        }
        validate_object_id(diagnostics, object_ids, lane.id, "BGM source lane");
    }

    std::unordered_set<std::uint64_t> layout_rows;
    for (const auto& segment : chart.lane_layouts) {
        validate_object_id(diagnostics, object_ids, segment.id, "lane layout segment");
        const auto row =
            position_row_or_diagnostic(index, segment.position, diagnostics, segment.id);
        if (row.has_value() && !layout_rows.insert(*row).second) {
            add_diagnostic(diagnostics, diagnostic_code_t::invalid_measure,
                           "multiple lane layouts start at the same position", segment.id);
        }
        check_unique_ids(
            diagnostics, segment.layout.lanes, [](const auto& lane) { return lane.id; },
            "layout lane");
    }
    const auto profile_layout = combined_layout(chart);
    const auto profile_diagnostics = validate_input_profile(profile_layout, chart.input_profile);
    diagnostics.insert(diagnostics.end(), profile_diagnostics.begin(), profile_diagnostics.end());

    auto sound_links = semantic_sound_links_t{resource_assets, diagnostics};
    collect_retained_sound_events(chart, sound_links);

    auto objects = chart_object_validation_t{chart,      index,     resources,    diagnostics,
                                             object_ids, layer_ids, bgm_lane_ids, sound_links};
    validate_chart_notes_and_bgm(objects);

    validate_positioned_chart_events(objects);
    validate_bpm_pause_rationals(objects);
    validate_bms_timing_projections(objects);
    validate_scroll_speed_warp_bga(objects);
    validate_chart_bookmarks(objects);
    validate_chart_sections(objects);
    validate_chart_sound_channels(objects, audio_assets);
}

} // namespace

auto conversion_report_t::has_errors() const -> bool {
    return std::ranges::any_of(diagnostics, [](const auto& diagnostic) {
        return diagnostic.severity == diagnostic_severity_t::error;
    });
}

namespace {

// Flags each measure whose nonzero ID already appeared earlier in chart order. IDs that
// ascend strictly (the usual case) cannot repeat, so they need no storage at all; the
// result is then empty. This runs every frame on large charts, so it avoids a node
// allocation per measure.
auto later_duplicate_measure_flags(const chart_document_t& chart) -> std::vector<char> {
    const auto& measures = chart.measures;
    const auto ascending = std::ranges::adjacent_find(measures, [](const auto& left, const auto& right) {
                               return left.id.value >= right.id.value;
                           }) == measures.end();
    if (ascending) return {};
    std::vector<std::pair<std::uint64_t, std::size_t>> seen;
    seen.reserve(measures.size());
    for (std::size_t position = 0; position < measures.size(); ++position)
        if (measures[position].id.value != 0) seen.emplace_back(measures[position].id.value, position);
    std::ranges::sort(seen);
    std::vector<char> flags(measures.size(), 0);
    for (std::size_t index = 1; index < seen.size(); ++index)
        if (seen[index].first == seen[index - 1].first) flags[seen[index].second] = 1;
    return flags;
}

} // namespace

auto build_measure_index(const chart_document_t& chart)
    -> std::variant<measure_index_t, diagnostic_list_t> {
    diagnostic_list_t diagnostics;
    measure_index_t index;
    index.entries.reserve(chart.measures.size());
    const auto duplicates = later_duplicate_measure_flags(chart);

    if (chart.divisions_per_standard_measure == 0) {
        add_diagnostic(diagnostics, diagnostic_code_t::invalid_resolution,
                       "chart divisions per standard measure must be positive");
    }
    std::size_t measure_number = 0;
    for (const auto& measure : chart.measures) {
        ++measure_number;
        if (!require_id(diagnostics, measure.id, "measure")) {
            diagnostics.back().message += " (measure " + std::to_string(measure_number) + ")";
            continue;
        }
        if (!duplicates.empty() && duplicates[measure_number - 1] != 0) {
            add_diagnostic(diagnostics, diagnostic_code_t::duplicate_id,
                           "measure " + std::to_string(measure_number) + " ID is duplicated");
            continue;
        }
        const auto validation =
            validate_measure(chart.divisions_per_standard_measure, measure.definition);
        if (!validation) {
            add_diagnostic(diagnostics, diagnostic_code_t::invalid_measure,
                           "measure " + std::to_string(measure_number) + ": " +
                               measure_error_message(validation.error));
            continue;
        }
        index.entries.push_back({
            .id = measure.id,
            .absolute_begin_row = index.total_rows,
            .row_capacity = *validation.row_capacity,
        });
        std::uint64_t total{};
        if (!add_checked(index.total_rows, *validation.row_capacity, total)) {
            add_diagnostic(diagnostics, diagnostic_code_t::invalid_measure,
                           "measure " + std::to_string(measure_number) +
                               " prefix rows overflow the supported range");
        } else {
            index.total_rows = total;
        }
    }

    if (!diagnostics.empty()) {
        return diagnostics;
    }
    return index;
}

auto absolute_row(const measure_index_t& index, local_position_t position)
    -> std::optional<std::uint64_t> {
    const auto entry = std::ranges::find_if(
        index.entries, [&](const auto& candidate) { return candidate.id == position.measure; });
    if (entry == index.entries.end() || position.row >= entry->row_capacity) {
        return std::nullopt;
    }
    std::uint64_t result{};
    if (!add_checked(entry->absolute_begin_row, position.row, result)) {
        return std::nullopt;
    }
    return result;
}

namespace {

struct input_profile_validation_t {
    const input_profile_t& profile;
    const std::unordered_map<std::uint64_t, stage_group_t>& lane_stages;
    diagnostic_list_t& diagnostics;
    std::unordered_set<std::uint64_t> bound_lanes;
    std::unordered_set<std::string> active_keys;
};

auto validate_input_binding(input_profile_validation_t& context, const lane_binding_t& binding)
    -> void {
    if (binding.key == physical_key_t::unknown || !binding.lane) {
        add_diagnostic(context.diagnostics, diagnostic_code_t::invalid_binding,
                       "input binding must contain a physical key and lane ID");
        return;
    }
    const auto lane = context.lane_stages.find(binding.lane.value);
    if (lane == context.lane_stages.end()) {
        add_diagnostic(context.diagnostics, diagnostic_code_t::unknown_lane,
                       "input binding refers to a lane absent from the layout");
    }
    if (!context.bound_lanes.insert(binding.lane.value).second) {
        add_diagnostic(context.diagnostics, diagnostic_code_t::duplicate_binding,
                       "a lane has more than one default physical binding");
    }
    if (context.profile.requires_focused_stage != binding.focused_stage.has_value()) {
        add_diagnostic(context.diagnostics, diagnostic_code_t::invalid_stage,
                       "focused-stage binding does not match the profile focus policy");
        return;
    }
    if (binding.focused_stage.has_value() &&
        *binding.focused_stage >= context.profile.stage_count) {
        add_diagnostic(context.diagnostics, diagnostic_code_t::invalid_stage,
                       "input binding focused stage is out of range");
        return;
    }
    if (context.profile.requires_focused_stage && lane != context.lane_stages.end()) {
        const auto expected_stage = lane->second == stage_group_t::right ? 1U : 0U;
        if (*binding.focused_stage != expected_stage) {
            add_diagnostic(context.diagnostics, diagnostic_code_t::invalid_stage,
                           "input binding lane does not belong to its focused stage");
        }
    }
    const auto stage = binding.focused_stage.value_or(0);
    const auto key = std::to_string(static_cast<int>(binding.key)) + ":" + std::to_string(stage);
    if (!context.active_keys.insert(key).second) {
        add_diagnostic(context.diagnostics, diagnostic_code_t::duplicate_binding,
                       "physical key collides within an active input stage");
    }
}

} // namespace

auto validate_input_profile(const lane_layout_t& layout, const input_profile_t& profile)
    -> diagnostic_list_t {
    diagnostic_list_t diagnostics;
    if (profile.stage_count == 0) {
        add_diagnostic(diagnostics, diagnostic_code_t::invalid_stage,
                       "input profile stage count must be positive");
    }

    std::unordered_map<std::uint64_t, stage_group_t> lane_stages;
    for (const auto& lane : layout.lanes) {
        if (lane.id) {
            lane_stages.emplace(lane.id.value, lane.stage);
        }
    }
    auto context = input_profile_validation_t{profile, lane_stages, diagnostics};
    for (const auto& binding : profile.bindings) {
        validate_input_binding(context, binding);
    }
    return diagnostics;
}

namespace {

struct document_validation_work_t {
    const song_document_t& song;
    diagnostic_list_t& diagnostics;
    std::unordered_set<std::uint64_t> asset_ids;
    std::unordered_set<std::uint64_t> audio_asset_ids;
    std::unordered_set<std::uint64_t> resource_ids;
    std::unordered_map<std::uint64_t, asset_id_t> resource_assets;
    std::map<std::tuple<int, std::string, std::string>, resource_id_t> target_identity_owners;
};

auto validate_song_unique_ids(document_validation_work_t& work) -> void {
    require_id(work.diagnostics, work.song.id, "song");
    validate_format_groups(work.song.metadata.format_groups, work.diagnostics);
    check_unique_ids(
        work.diagnostics, work.song.audio_assets, [](const auto& asset) { return asset.id; },
        "audio asset");
    check_unique_ids(
        work.diagnostics, work.song.visual_assets, [](const auto& asset) { return asset.id; },
        "visual asset");
    check_unique_ids(
        work.diagnostics, work.song.resources, [](const auto& resource) { return resource.id; },
        "resource");
    check_unique_ids(
        work.diagnostics, work.song.charts, [](const auto& chart) { return chart.id; }, "chart");
    check_unique_ids(
        work.diagnostics, work.song.midi_assets, [](const auto& asset) { return asset.id; },
        "MIDI asset");
    check_unique_ids(
        work.diagnostics, work.song.generator_recipes, [](const auto& recipe) { return recipe.id; },
        "generator recipe");
    check_unique_ids(
        work.diagnostics, work.song.storyboards,
        [](const auto& storyboard) { return storyboard.id; }, "storyboard");
    for (const auto& storyboard : work.song.storyboards) {
        if (storyboard.model.has_value() && !emit_osb(*storyboard.model)) {
            add_diagnostic(work.diagnostics, diagnostic_code_t::unsupported_feature,
                           "storyboard model is invalid or exceeds the bounded OSB limits");
        }
    }
}

auto collect_song_assets(document_validation_work_t& work) -> void {
    for (const auto& asset : work.song.audio_assets) {
        if (asset.id && !work.asset_ids.insert(asset.id.value).second) {
            add_diagnostic(work.diagnostics, diagnostic_code_t::duplicate_id,
                           "audio and visual assets share a duplicate ID");
        }
        if (asset.id) {
            work.audio_asset_ids.insert(asset.id.value);
        }
    }
    for (const auto& asset : work.song.visual_assets) {
        if (asset.id && !work.asset_ids.insert(asset.id.value).second) {
            add_diagnostic(work.diagnostics, diagnostic_code_t::duplicate_id,
                           "audio and visual assets share a duplicate ID");
        }
    }
    for (const auto& asset : work.song.midi_assets) {
        if (asset.id && !work.asset_ids.insert(asset.id.value).second) {
            add_diagnostic(work.diagnostics, diagnostic_code_t::duplicate_id,
                           "audio, visual, and MIDI assets share a duplicate ID");
        }
        if (asset.ticks_per_quarter == 0) {
            add_diagnostic(work.diagnostics, diagnostic_code_t::invalid_resolution,
                           "MIDI asset ticks per quarter must be positive");
        }
    }
}

auto validate_target_identity(document_validation_work_t& work, const resource_entry_t& resource,
                              const target_resource_identity_t& identity) -> void {
    if (!valid_format_family(identity.family)) {
        add_diagnostic(work.diagnostics, diagnostic_code_t::invalid_id,
                       "resource target identity has an unknown format family");
        return;
    }
    const auto identifier_is_blank = std::ranges::all_of(
        identity.identifier, [](unsigned char character) { return std::isspace(character) != 0; });
    const auto namespace_is_blank =
        std::ranges::all_of(identity.namespace_name,
                            [](unsigned char character) { return std::isspace(character) != 0; });
    if (identity.identifier.empty() || identifier_is_blank ||
        identity.identifier.find('\0') != std::string::npos ||
        (identity.family == format_family_t::bms && identity.namespace_name.empty()) ||
        (!identity.namespace_name.empty() && namespace_is_blank) ||
        identity.namespace_name.find('\0') != std::string::npos) {
        add_diagnostic(work.diagnostics, diagnostic_code_t::invalid_id,
                       "resource target identity has an empty or invalid namespace/identifier");
        return;
    }
    const auto key =
        std::tuple{static_cast<int>(identity.family), canonical_target_namespace(identity),
                   canonical_target_identifier(identity)};
    if (!work.target_identity_owners.emplace(key, resource.id).second) {
        add_diagnostic(work.diagnostics, diagnostic_code_t::duplicate_id,
                       "target resource identity is assigned more than once");
    }
}

auto validate_song_resources(document_validation_work_t& work) -> void {
    for (const auto& resource : work.song.resources) {
        if (resource.id) {
            work.resource_ids.insert(resource.id.value);
            work.resource_assets.emplace(resource.id.value, resource.asset);
        }
        if (!resource.asset || !work.asset_ids.contains(resource.asset.value)) {
            add_diagnostic(work.diagnostics, diagnostic_code_t::dangling_asset,
                           "resource refers to an unknown asset");
        }
        for (const auto& identity : resource.target_identities) {
            validate_target_identity(work, resource, identity);
        }
    }
}

auto ascii_fold(std::string_view text) -> std::string {
    std::string folded(text);
    for (auto& character : folded) {
        const auto value = static_cast<unsigned char>(character);
        if (value >= 'A' && value <= 'Z') character = static_cast<char>(value + ('a' - 'A'));
    }
    return folded;
}

auto validate_song_sound_groups(const song_document_t& song, diagnostic_list_t& diagnostics)
    -> void {
    check_unique_ids(diagnostics, song.sound_groups, [](const auto& group) { return group.id; },
                     "sound group");
    std::unordered_set<std::string> names;
    std::unordered_set<std::uint64_t> group_ids;
    std::unordered_set<std::uint64_t> populated;
    for (const auto& group : song.sound_groups) {
        if (group.id) group_ids.insert(group.id.value);
        const auto folded = ascii_fold(group.name);
        const auto first = group.name.find_first_not_of(" \t\n\r\f\v");
        const auto last = group.name.find_last_not_of(" \t\n\r\f\v");
        if (group.name.empty() || group.name.size() > 64 || group.name.find('\0') != std::string::npos ||
            first != 0 || last + 1 != group.name.size() ||
            !names.insert(folded).second) {
            add_diagnostic(diagnostics, diagnostic_code_t::invalid_id,
                           "sound group name is empty, over 64 bytes, or duplicated ignoring case");
        }
    }
    for (const auto& resource : song.resources) {
        if (!resource.sound_group) continue;
        const auto is_sound = std::ranges::any_of(resource.roles, [](const auto role) {
            return role == resource_role_t::sample || role == resource_role_t::bgm ||
                   role == resource_role_t::music || role == resource_role_t::preview;
        });
        if (!is_sound) {
            add_diagnostic(diagnostics, diagnostic_code_t::invalid_id,
                           "sound group membership refers to a non-sound resource");
            continue;
        }
        if (!group_ids.contains(resource.sound_group->value)) {
            add_diagnostic(diagnostics, diagnostic_code_t::invalid_id,
                           "sound resource refers to an unknown sound group");
        } else {
            populated.insert(resource.sound_group->value);
        }
    }
    for (const auto& group : song.sound_groups) {
        if (group.id && !populated.contains(group.id.value)) {
            add_diagnostic(diagnostics, diagnostic_code_t::invalid_id,
                           "sound group has no sound resources");
        }
    }
}

auto validate_song_recipes(document_validation_work_t& work) -> void {
    std::unordered_set<std::uint64_t> recipe_ids;
    for (const auto& recipe : work.song.generator_recipes) {
        if (recipe.id && !recipe_ids.insert(recipe.id.value).second) {
            add_diagnostic(work.diagnostics, diagnostic_code_t::duplicate_id,
                           "generator and SV recipe IDs must be unique across the song");
        }
    }
    for (const auto& chart : work.song.charts) {
        for (const auto& recipe : chart.sv_recipes) {
            if (recipe.id && !recipe_ids.insert(recipe.id.value).second) {
                add_diagnostic(work.diagnostics, diagnostic_code_t::duplicate_id,
                               "generator and SV recipe IDs must be unique across the song");
            }
        }
    }
    for (const auto& recipe : work.song.generator_recipes) {
        for (const auto asset : recipe.input_assets) {
            if (!asset || !work.asset_ids.contains(asset.value)) {
                add_diagnostic(work.diagnostics, diagnostic_code_t::dangling_asset,
                               "generator recipe refers to an unknown input asset");
            }
        }
    }
}

auto validate_song_output_groups(document_validation_work_t& work) -> void {
    std::unordered_set<std::uint64_t> chart_ids;
    for (const auto& chart : work.song.charts) {
        if (chart.id) {
            chart_ids.insert(chart.id.value);
        }
    }
    for (const auto& group : work.song.output_groups) {
        for (const auto chart : group.charts) {
            if (!chart || !chart_ids.contains(chart.value)) {
                add_diagnostic(work.diagnostics, diagnostic_code_t::invalid_id,
                               "output group refers to an unknown chart");
            }
        }
    }
}

struct diagnostic_object_location_t {
    local_position_t position;
    lane_id_t lane;
};
using diagnostic_locations_t = std::unordered_map<std::uint64_t, diagnostic_object_location_t>;

void collect_marker_diagnostic_locations(diagnostic_locations_t& locations,
                                         const chart_document_t& chart) {
    for (const auto& bookmark : chart.bookmarks) {
        if (bookmark.position)
            locations.try_emplace(bookmark.id.value,
                                  diagnostic_object_location_t{*bookmark.position, {}});
    }
    for (const auto& section : chart.sections) {
        if (section.beat_span)
            locations.try_emplace(section.id.value,
                                  diagnostic_object_location_t{section.beat_span->begin, {}});
    }
}

template <typename Range>
void collect_diagnostic_locations(diagnostic_locations_t& locations, const Range& objects) {
    for (const auto& object : objects) {
        lane_id_t lane{};
        if constexpr (requires { object.lane; }) lane = object.lane;
        if constexpr (requires { object.position; })
            locations.try_emplace(object.id.value, diagnostic_object_location_t{object.position, lane});
        else
            locations.try_emplace(object.id.value, diagnostic_object_location_t{object.span.begin, lane});
    }
}

auto diagnostic_locations(const chart_document_t& chart) -> diagnostic_locations_t {
    diagnostic_locations_t locations;
    collect_marker_diagnostic_locations(locations, chart);
    for (const auto& measure : chart.measures) {
        collect_diagnostic_locations(locations, measure.notes);
        collect_diagnostic_locations(locations, measure.bgm_events);
    }
    collect_diagnostic_locations(locations, chart.lane_layouts);
    collect_diagnostic_locations(locations, chart.bpms);
    collect_diagnostic_locations(locations, chart.pauses);
    collect_diagnostic_locations(locations, chart.warps);
    collect_diagnostic_locations(locations, chart.scrolls);
    collect_diagnostic_locations(locations, chart.speeds);
    collect_diagnostic_locations(locations, chart.kiai);
    collect_diagnostic_locations(locations, chart.timing_anchors);
    collect_diagnostic_locations(locations, chart.bga_events);
    for (const auto& channel : chart.sound_channels)
        collect_diagnostic_locations(locations, channel.events);
    return locations;
}

auto diagnostic_chart_name(const chart_document_t& chart) -> std::string {
    if (!chart.metadata.name.empty()) return chart.metadata.name;
    for (const auto& group : chart.metadata.format_groups) {
        const auto difficulty = std::ranges::find(group.ordered_fields, std::string{"DIFFICULTY"},
                                                  &std::pair<std::string, std::string>::first);
        if (difficulty != group.ordered_fields.end()) return difficulty->second;
    }
    return "unnamed";
}

auto diagnostic_location_text(const chart_document_t& chart,
                              const diagnostic_object_location_t& location) -> std::string {
    const auto measure = std::ranges::find(chart.measures, location.position.measure,
                                           &measure_chunk_t::id);
    auto text = measure == chart.measures.end()
        ? ", unknown measure id " + std::to_string(location.position.measure.value)
        : ", measure " + std::to_string(std::distance(chart.measures.begin(), measure) + 1) +
          " (id " + std::to_string(location.position.measure.value) + ")";
    text += ", row " + std::to_string(location.position.row);
    if (location.lane) text += ", lane " + std::to_string(location.lane.value);
    return text;
}

void contextualize_chart_diagnostics(const chart_document_t& chart,
                                     diagnostic_list_t& diagnostics, std::size_t begin) {
    if (begin == diagnostics.size()) return;
    const auto locations = diagnostic_locations(chart);
    const auto chart_text = "Chart \"" + diagnostic_chart_name(chart) + "\" (id " +
                            std::to_string(chart.id.value) + ")";
    for (std::size_t index = begin; index < diagnostics.size(); ++index) {
        auto text = chart_text;
        const auto& object = diagnostics[index].object;
        if (object) {
            const auto location = locations.find(object->value);
            if (location != locations.end()) text += diagnostic_location_text(chart, location->second);
            text += ", object " + std::to_string(object->value);
        }
        diagnostics[index].message = text + ": " + diagnostics[index].message;
    }
}

} // namespace

auto validate_document(const song_document_t& song) -> diagnostic_list_t {
    diagnostic_list_t diagnostics;
    auto work = document_validation_work_t{song, diagnostics};
    validate_song_unique_ids(work);
    collect_song_assets(work);
    validate_song_resources(work);
    validate_song_sound_groups(song, diagnostics);
    for (const auto& chart : song.charts) {
        const auto first_diagnostic = diagnostics.size();
        validate_chart(chart, work.resource_ids, work.resource_assets, work.audio_asset_ids,
                       diagnostics);
        contextualize_chart_diagnostics(chart, diagnostics, first_diagnostic);
    }
    validate_song_recipes(work);
    validate_song_output_groups(work);
    return diagnostics;
}

namespace {

struct redivision_context_t {
    const measure_index_t& source_index;
    std::uint64_t numerator;
    std::uint64_t denominator;
    diagnostic_list_t& diagnostics;
    std::vector<redivision_move_t>& moves;
    bool failed{};
};

auto redivide_position(redivision_context_t& context, local_position_t& position,
                       object_id_t object, position_role_t role) -> void {
    const auto before = position;
    const auto diagnostic_object = object ? std::optional<object_id_t>{object} : std::nullopt;
    if (!absolute_row(context.source_index, position).has_value()) {
        add_diagnostic(context.diagnostics, diagnostic_code_t::row_out_of_range,
                       "object position is outside the source measure geometry", diagnostic_object);
        context.failed = true;
        return;
    }
    if (position.row % context.denominator != 0) {
        add_diagnostic(context.diagnostics, diagnostic_code_t::row_out_of_range,
                       "object row is not exactly representable at the target resolution",
                       diagnostic_object);
        context.failed = true;
        return;
    }
    const auto reduced_row = position.row / context.denominator;
    if (context.numerator != 0 && reduced_row > maximum / context.numerator) {
        add_diagnostic(context.diagnostics, diagnostic_code_t::invalid_resolution,
                       "object row overflows at the target resolution", diagnostic_object);
        context.failed = true;
        return;
    }
    position.row = reduced_row * context.numerator;
    if (position != before) {
        context.moves.push_back(
            {.object = object, .role = role, .before = before, .after = position});
    }
}

auto redivide_chart_positions(chart_document_t& chart, redivision_context_t& context) -> void {
    for (auto& segment : chart.lane_layouts) {
        redivide_position(context, segment.position, segment.id, position_role_t::point);
    }
    for (auto& measure : chart.measures) {
        for (auto& note : measure.notes) {
            redivide_position(context, note.position, note.id, position_role_t::point);
            if (note.end.has_value()) {
                redivide_position(context, *note.end, note.id, position_role_t::note_end);
            }
        }
        for (auto& bgm : measure.bgm_events) {
            redivide_position(context, bgm.position, bgm.id, position_role_t::point);
        }
    }
    const auto transform_positioned = [&](auto& values) {
        for (auto& value : values) {
            redivide_position(context, value.position, value.id, position_role_t::point);
        }
    };
    transform_positioned(chart.bpms);
    transform_positioned(chart.pauses);
    transform_positioned(chart.scrolls);
    transform_positioned(chart.speeds);
    transform_positioned(chart.kiai);
    transform_positioned(chart.timing_anchors);
    transform_positioned(chart.bga_events);
    for (auto& warp : chart.warps) {
        redivide_position(context, warp.span.begin, warp.id, position_role_t::span_begin);
        redivide_position(context, warp.span.end, warp.id, position_role_t::span_end);
    }
    for (auto& bookmark : chart.bookmarks) {
        if (bookmark.position.has_value()) {
            redivide_position(context, *bookmark.position, bookmark.id, position_role_t::point);
        }
    }
    for (auto& section : chart.sections) {
        if (section.beat_span.has_value()) {
            redivide_position(context, section.beat_span->begin, section.id,
                              position_role_t::span_begin);
            redivide_position(context, section.beat_span->end, section.id,
                              position_role_t::span_end);
        }
    }
    for (auto& channel : chart.sound_channels) {
        for (auto& event : channel.events) {
            redivide_position(context, event.position, event.id, position_role_t::point);
            if (event.end.has_value()) {
                redivide_position(context, *event.end, event.id, position_role_t::note_end);
            }
        }
    }
}

} // namespace

auto redivide_chart_exact(const chart_document_t& chart,
                          std::uint64_t target_divisions_per_standard_measure)
    -> redivision_result_t {
    redivision_result_t result;
    if (chart.divisions_per_standard_measure == 0 || target_divisions_per_standard_measure == 0) {
        add_diagnostic(result.diagnostics, diagnostic_code_t::invalid_resolution,
                       "source and target chart resolutions must be positive");
        return result;
    }

    const auto source_index_result = build_measure_index(chart);
    if (std::holds_alternative<diagnostic_list_t>(source_index_result)) {
        result.diagnostics = std::get<diagnostic_list_t>(source_index_result);
        return result;
    }
    const auto& source_index = std::get<measure_index_t>(source_index_result);

    for (const auto& measure : chart.measures) {
        const auto validation =
            validate_measure(target_divisions_per_standard_measure, measure.definition);
        if (!validation) {
            add_diagnostic(result.diagnostics, diagnostic_code_t::invalid_resolution,
                           "target resolution cannot represent measure " +
                               std::to_string(measure.id.value) + ": " +
                               measure_error_message(validation.error));
        }
    }
    if (!result.diagnostics.empty()) {
        return result;
    }

    auto transformed = chart;
    const auto common =
        std::gcd(chart.divisions_per_standard_measure, target_divisions_per_standard_measure);
    const auto denominator = chart.divisions_per_standard_measure / common;
    const auto numerator = target_divisions_per_standard_measure / common;
    auto context = redivision_context_t{source_index, numerator, denominator, result.diagnostics,
                                        result.moves};
    redivide_chart_positions(transformed, context);
    if (context.failed) {
        result.moves.clear();
        return result;
    }

    transformed.divisions_per_standard_measure = target_divisions_per_standard_measure;
    result.chart = std::move(transformed);
    return result;
}

} // namespace wormedit
