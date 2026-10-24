#pragma once

#include <wormedit/core/bms_program.h>
#include <wormedit/core/command.h>

namespace wormedit {

// Temporary isolated document adapter; public activation is owned by T-744.
// Program content is authoritative. Projections are detached read-only values.
class bms_program_document_t {
  public:
    [[nodiscard]] static auto from(chart_id_t difficulty, bms_prepared_program_t prepared,
        bms_program_evaluation_t evaluation = {})
        -> attempt_t<std::shared_ptr<const bms_program_document_t>, bms_program_projection_diagnostics_t>;
    [[nodiscard]] auto read_resource(resource_id_t id) const -> const resource_entry_t*;
    [[nodiscard]] auto read_chart() const -> const chart_document_t*;
    [[nodiscard]] auto read_authoring_chart(branch_id_t chart) const -> const chart_document_t*;
    [[nodiscard]] auto difficulty() const -> chart_id_t { return difficulty_; }
    [[nodiscard]] auto prepared() const -> const bms_prepared_program_t& { return prepared_; }
    [[nodiscard]] auto evaluation() const -> const bms_program_evaluation_t& { return evaluation_; }
    [[nodiscard]] auto build_projection(std::optional<branch_id_t> authoring_chart = {}) const
        -> attempt_t<bms_program_projection_t, bms_program_projection_diagnostics_t>;
  private:
    friend class document_command_transaction_t;
    bms_program_document_t(chart_id_t difficulty, bms_prepared_program_t prepared,
        bms_program_evaluation_t evaluation)
        : difficulty_(difficulty), prepared_(std::move(prepared)), evaluation_(std::move(evaluation)) {}
    chart_id_t difficulty_;
    bms_prepared_program_t prepared_;
    bms_program_evaluation_t evaluation_;
    mutable std::optional<song_document_t> view_;
    mutable std::optional<std::pair<branch_id_t, song_document_t>> authoring_view_;
};

struct bms_program_edit_target_t {
    document_stamp_t stamp;
    chart_id_t difficulty;
    branch_id_t scope;
    branch_id_t chart;
    bool authoring{};
    // No selection means Root; a scope selects its first direct Chart. An
    // explicit Chart remains that Chart. Invalid/hidden selections never default.
    [[nodiscard]] static auto from(const song_document_t& document,
        chart_id_t difficulty, std::optional<branch_id_t> selected = std::nullopt)
        -> result_t<bms_program_edit_target_t, error_t<failure_code_t>>;
    [[nodiscard]] static auto from(const song_document_t& document,
        chart_id_t difficulty, object_id_t existing_item)
        -> result_t<bms_program_edit_target_t, error_t<failure_code_t>>;
    [[nodiscard]] static auto for_authoring(const song_document_t& document,
        chart_id_t difficulty, branch_id_t selected, std::optional<object_id_t> existing = {})
        -> result_t<bms_program_edit_target_t, error_t<failure_code_t>>;
    [[nodiscard]] auto validate(const song_document_t& document) const
        -> result_t<const bms_program_document_t*, error_t<failure_code_t>>;
    auto operator==(const bms_program_edit_target_t&) const -> bool = default;
};

struct bms_program_content_insert_t {
    bms_content_item_t item;
    auto operator==(const bms_program_content_insert_t&) const -> bool = default;
};
struct bms_program_content_update_t {
    bms_program_owner_t owner;
    bms_content_value_t value;
    auto operator==(const bms_program_content_update_t&) const -> bool = default;
};
struct bms_program_content_delete_t {
    bms_program_owner_t owner;
    auto operator==(const bms_program_content_delete_t&) const -> bool = default;
};
using bms_program_content_edit_t = std::variant<bms_program_content_insert_t,
    bms_program_content_update_t, bms_program_content_delete_t>;

// One semantic batch, including both endpoints of any newly authored hold.
// Existing-object batches target their recorded owner, independently of selection.
struct bms_program_content_edit_batch_t {
    bms_program_edit_target_t target;
    std::vector<bms_program_content_edit_t> edits;
    std::string label;
};

[[nodiscard]] auto validate_prepared_bms_programs(const song_document_t& document)
    -> diagnostic_list_t;
[[nodiscard]] auto validate_bms_program_publication(const song_document_t& before,
    const song_document_t& proposed, std::size_t maximum_evaluations = 256) -> diagnostic_list_t;

} // namespace wormedit
