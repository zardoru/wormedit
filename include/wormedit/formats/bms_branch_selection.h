#pragma once

#include <wormedit/core/bms_branch_projection.h>
#include <wormedit/formats/bms.h>
#include <wormedit/formats/bms_writer.h>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace wormedit {

enum class bms_branch_selection_diagnostic_code_t {
    missing_chart,
    unsupported_song_shape,
    missing_control_tree,
    invalid_selection,
    unsupported_editor_state,
    resource_limit,
    write_failed,
    parse_failed,
    control_tree_changed,
    selection_mismatch,
};

using bms_branch_selection_diagnostic_t =
    path_diagnostic_t<bms_branch_selection_diagnostic_code_t,
                      bms_branch_selection_diagnostic_code_t::invalid_selection>;

struct bms_branch_selection_request_t {
    std::optional<chart_id_t> chart;
    std::vector<branch_selection_t> proposed;
    bms_branch_projection_limits_t projection_limits;
    std::size_t max_source_bytes{16U * 1024U * 1024U};
    std::size_t max_line_bytes{1024U * 1024U};
    std::size_t max_nesting_depth{256};
    std::size_t max_lines{262'144};
    std::uint64_t max_chart_resolution{1'000'000'000ULL};
    std::size_t max_slots_per_line{500'000};
};

struct bms_branch_selection_result_t {
    std::optional<song_document_t> song;
    bms_branch_projection_preview_t preview;
    std::vector<bms_branch_selection_diagnostic_t> diagnostics;
    std::vector<bms_write_diagnostic_t> write_diagnostics;
    std::vector<bms_parse_diagnostic_t> parse_diagnostics;

    [[nodiscard]] auto has_errors() const -> bool;
    [[nodiscard]] explicit operator bool() const noexcept {
        return song.has_value();
    }
};

// Serializes the current BMS projection into its retained full branch tree and
// reparses that tree with the proposed choices. No partial document is returned:
// a caller can publish the rematerialized semantic graph as one history command.
// Editor-only state that the BMS round trip cannot currently preserve is rejected.
[[nodiscard]] auto rematerialize_bms_branch_selection(const song_document_t& song,
                                                      const bms_branch_selection_request_t& request)
    -> bms_branch_selection_result_t;

} // namespace wormedit
