#pragma once

#include <wormedit/core/document.h>
#include <wormedit/core/result.h>

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
    [[nodiscard]] constexpr auto is_error() const noexcept -> bool {
        return true;
    }
    auto operator==(const bms_branch_projection_diagnostic_t&) const -> bool = default;
};

struct bms_branch_view_t {
    [[nodiscard]] static auto from(const bms_control_node_t& root,
                                   const std::vector<branch_selection_t>& selections,
                                   bms_branch_projection_limits_t limits = {})
        -> diagnosed_t<bms_branch_view_t, std::vector<bms_branch_projection_diagnostic_t>>;
    std::vector<branch_id_t> active_statement_ids;
    std::vector<std::size_t> active_statement_source_orders;
    std::vector<branch_id_t> visited_selection_controls;
    auto operator==(const bms_branch_view_t&) const -> bool = default;
};
using bms_branch_view_outcome_t =
    diagnosed_t<bms_branch_view_t, std::vector<bms_branch_projection_diagnostic_t>>;

struct bms_branch_view_result_t {
    // Temporary: removed by T-642 after legacy branch result consumers migrate.
    // Evaluates only retained control-flow structure. Root statements are common
    // to every projection and are intentionally absent from active_statement_*.
    // A control with no explicit choice contributes no branch-local statements.
    [[nodiscard]] static auto from(const bms_control_node_t& root,
                                   const std::vector<branch_selection_t>& selections,
                                   bms_branch_projection_limits_t limits = {})
        -> bms_branch_view_result_t;

    // Temporary: removed by T-642 after legacy branch result consumers migrate.
    [[nodiscard]] static auto from(bms_branch_view_outcome_t&& outcome) -> bms_branch_view_result_t;

    bms_branch_projection_status_t status{bms_branch_projection_status_t::invalid_tree};
    std::vector<branch_id_t> active_statement_ids;
    std::vector<std::size_t> active_statement_source_orders;
    std::vector<branch_id_t> visited_selection_controls;
    std::vector<bms_branch_projection_diagnostic_t> diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return status == bms_branch_projection_status_t::projected;
    }
};

struct bms_branch_preview_view_t {
    // Computes an immutable proposal delta. It never changes the chart projection;
    // callers must rematerialize semantic objects before publishing new choices.
    [[nodiscard]] static auto from(const bms_control_node_t& root,
                                   const std::vector<branch_selection_t>& current,
                                   const std::vector<branch_selection_t>& proposed,
                                   bms_branch_projection_limits_t limits = {})
        -> bms_branch_preview_view_t;

    bms_branch_view_result_t current;
    bms_branch_view_result_t proposed;
    std::vector<branch_id_t> entering_statement_ids;
    std::vector<branch_id_t> leaving_statement_ids;

    [[nodiscard]] explicit operator bool() const noexcept {
        return static_cast<bool>(current) && static_cast<bool>(proposed);
    }
};

} // namespace wormedit
