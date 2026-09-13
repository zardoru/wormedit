#pragma once

#include <wormedit/core/document.h>

#include <optional>
#include <span>
#include <string>
#include <vector>

namespace wormedit {

enum class bms_parse_diagnostic_code_t {
    invalid_encoding,
    unsupported_encoding,
    malformed_line,
    invalid_number,
    invalid_measure,
    malformed_control,
    duplicate_resource,
    unsupported_channel,
    unresolved_reference,
    invalid_branch_selection,
    unpaired_long_note,
};

struct bms_parse_diagnostic_t {
    diagnostic_severity_t severity{diagnostic_severity_t::error};
    bms_parse_diagnostic_code_t code{bms_parse_diagnostic_code_t::malformed_line};
    std::string message;
    std::size_t line{};   // One-based physical line number.
    std::size_t column{}; // One-based UTF-8 byte column in decoded text.
    std::optional<bms_source_span_t> source_span;
};

struct bms_parse_options_t {
    // Non-ASCII input without a UTF-8 BOM is intentionally ambiguous. Select its codec here.
    std::optional<text_encoding_t> encoding_override;
    std::size_t max_source_bytes{16U * 1024U * 1024U};
    std::size_t max_line_bytes{1024U * 1024U};
    std::size_t max_nesting_depth{256};
    std::size_t max_lines{262144};
    // Exact BMS subdivisions may promote the chart's row resolution. Payloads
    // requiring a larger resolution remain raw and receive a diagnostic.
    std::uint64_t max_chart_resolution{1'000'000'000ULL};
    // Optional built-in profile selected by the project or importing caller.
    std::optional<std::string> lane_profile_id;
    // Optional explicit choice for a RANDOM or SWITCH block. The source order
    // is the zero-based physical line order of the block's opening directive.
    // Unselected blocks retain every branch in the source tree and project no
    // branch-local channel payloads.
    struct branch_choice_t {
        std::size_t control_source_order{};
        std::int64_t selected_value{};
    };
    std::vector<branch_choice_t> branch_choices;
};

struct bms_parse_result_t {
    std::optional<song_document_t> song;
    encoding_provenance_t encoding;
    std::vector<bms_parse_diagnostic_t> diagnostics;

    [[nodiscard]] auto has_errors() const -> bool;
};

enum class bms_branch_statement_edit_diagnostic_code_t {
    missing_chart,
    missing_control_tree,
    invalid_branch_selection,
    target_not_found,
    ambiguous_target,
    target_not_branch_statement,
    active_statement,
    provenance_mismatch,
    invalid_replacement,
    resource_limit,
    unsupported_song_shape,
};

struct bms_branch_statement_edit_diagnostic_t {
    diagnostic_severity_t severity{diagnostic_severity_t::error};
    bms_branch_statement_edit_diagnostic_code_t code{
        bms_branch_statement_edit_diagnostic_code_t::provenance_mismatch};
    std::string message;
    std::string path;
};

struct bms_branch_statement_edit_t {
    // BMS-family documents contain one chart, but callers editing a multi-chart
    // project must identify the intended chart explicitly.
    std::optional<chart_id_t> chart;
    branch_id_t statement;
    // All retained identity fields are required so a stale inspector row cannot
    // silently edit a different statement after the tree changes.
    std::size_t expected_source_order{};
    bms_source_span_t expected_source_span;
    std::string expected_original_text;
    std::string replacement_text;
    std::size_t max_replacement_bytes{1024U * 1024U};
};

struct bms_branch_statement_edit_result_t {
    std::optional<song_document_t> song;
    std::vector<bms_branch_statement_edit_diagnostic_t> diagnostics;

    [[nodiscard]] auto has_errors() const -> bool;
};

// Atomically replaces one source-backed raw statement in an inactive BMS branch.
// The command token and control-tree shape are immutable in this bounded editing
// slice. Active statements must instead be edited through their materialized
// semantic objects so the chart projection cannot become stale.
[[nodiscard]] auto edit_bms_inactive_branch_statement(const song_document_t& song,
                                                      bms_branch_statement_edit_t edit)
    -> bms_branch_statement_edit_result_t;

struct bms_branch_statement_insert_t {
    std::optional<chart_id_t> chart;
    // Insert into this exact leaf branch, immediately after the raw statement
    // identified below. Both identities are checked against retained source.
    branch_id_t leaf_branch;
    std::size_t expected_leaf_source_order{};
    bms_source_span_t expected_leaf_source_span;
    std::string expected_leaf_original_text;
    branch_id_t after_statement;
    std::size_t expected_statement_source_order{};
    bms_source_span_t expected_statement_source_span;
    std::string expected_statement_original_text;
    std::string statement_text;
    std::size_t max_statement_bytes{1024U * 1024U};
};

// Atomically inserts one source-backed, single-line BMS statement after a raw
// statement in an inactive IF/ELSEIF/ELSE/CASE/DEFAULT leaf. Until provenance
// carries a source-file identity, insertion is limited to a one-chart song so
// shifting source orders cannot affect an unrelated imported BMS chart. The
// control tree remains intact and preserve-tree emission retains every branch.
[[nodiscard]] auto insert_bms_inactive_branch_statement(const song_document_t& song,
                                                        bms_branch_statement_insert_t insertion)
    -> bms_branch_statement_edit_result_t;

struct bms_branch_statement_delete_t {
    std::optional<chart_id_t> chart;
    // Delete this raw statement only when it is a direct child of this exact leaf.
    branch_id_t leaf_branch;
    std::size_t expected_leaf_source_order{};
    bms_source_span_t expected_leaf_source_span;
    std::string expected_leaf_original_text;
    branch_id_t statement;
    std::size_t expected_source_order{};
    bms_source_span_t expected_source_span;
    std::string expected_original_text;
    std::size_t max_statement_bytes{1024U * 1024U};
};

// Atomically deletes one exact-provenance raw statement directly inside an
// inactive IF/ELSEIF/ELSE/CASE/DEFAULT leaf. The complete retained control
// structure remains in place, and source spans/orders plus semantic provenance
// are shifted with the removed source line. Until provenance carries a source-
// file identity, deletion is limited to one-chart BMS source documents.
[[nodiscard]] auto delete_bms_inactive_branch_statement(const song_document_t& song,
                                                        bms_branch_statement_delete_t deletion)
    -> bms_branch_statement_edit_result_t;

// ASCII and UTF-8 are always available. Legacy encodings depend on the conversion
// backend provided by the build and host; unsupported requests fail explicitly.
[[nodiscard]] auto is_bms_text_encoding_supported(text_encoding_t encoding) -> bool;

// Parses BMS-family bytes into a loss-aware source tree and materializes supported
// root-level semantics. Branch-local metadata, definitions, measure geometry, and
// object payloads are projected when a RANDOM/SWITCH choice is explicitly supplied.
[[nodiscard]] auto parse_bms_bytes(std::span<const std::byte> bytes,
                                   bms_parse_options_t options = {}) -> bms_parse_result_t;

} // namespace wormedit
