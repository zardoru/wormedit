#pragma once

#include <wormedit/core/document.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace wormedit {

enum class bms_branch_projection_status_t {
    projected,
    invalid_tree,
    duplicate_selection,
    invalid_selection,
    unreachable_selection,
    capacity_exceeded,
};

struct bms_branch_projection_limits_t {
    std::size_t maximum_nodes{262'144};
    std::size_t maximum_depth{256};
    std::size_t maximum_active_statements{262'144};
};

struct bms_branch_projection_diagnostic_t {
    bms_branch_projection_status_t code{bms_branch_projection_status_t::invalid_tree};
    std::string message;
    branch_id_t branch;
};

struct bms_branch_projection_result_t {
    bms_branch_projection_status_t status{bms_branch_projection_status_t::invalid_tree};
    std::vector<branch_id_t> active_statement_ids;
    std::vector<std::size_t> active_statement_source_orders;
    std::vector<branch_id_t> visited_selection_controls;
    std::vector<bms_branch_projection_diagnostic_t> diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return status == bms_branch_projection_status_t::projected;
    }
};

// Evaluates only retained control-flow structure. Root statements are common
// to every projection and are intentionally absent from active_statement_*.
// A control with no explicit choice contributes no branch-local statements.
[[nodiscard]] auto build_bms_branch_tree_view(const bms_control_node_t& root,
                                           const std::vector<branch_selection_t>& selections,
                                           bms_branch_projection_limits_t limits = {})
    -> bms_branch_projection_result_t;

struct bms_branch_projection_preview_t {
    bms_branch_projection_result_t current;
    bms_branch_projection_result_t proposed;
    std::vector<branch_id_t> entering_statement_ids;
    std::vector<branch_id_t> leaving_statement_ids;

    [[nodiscard]] explicit operator bool() const noexcept {
        return static_cast<bool>(current) && static_cast<bool>(proposed);
    }
};

// Computes an immutable proposal delta. It never changes the chart projection;
// callers must rematerialize semantic objects before publishing new choices.
[[nodiscard]] auto preview_bms_branch_projection(const bms_control_node_t& root,
                                                 const std::vector<branch_selection_t>& current,
                                                 const std::vector<branch_selection_t>& proposed,
                                                 bms_branch_projection_limits_t limits = {})
    -> bms_branch_projection_preview_t;

} // namespace wormedit
