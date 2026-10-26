#pragma once
#include <wormedit/core/proposal.h>
#include <wormedit/core/generator_types.h>

namespace wormedit {
[[nodiscard]] auto generator_identity_floor(const document_history_t&) -> std::uint64_t;
[[nodiscard]] auto validate_generator_recipe(const generator_recipe_t&) -> diagnostic_list_t;
struct generator_note_t {
    std::string key;
    note_t value;
    std::vector<std::string> layers;
    auto operator==(const generator_note_t&) const -> bool = default;
};
struct generator_layer_t { std::string key; semantic_layer_t value; auto operator==(const generator_layer_t&) const -> bool = default; };
struct generator_output_t {
    std::vector<generator_note_t> notes;
    std::vector<generator_layer_t> layers;
    std::vector<generator_chart_binding_t> charts;
    std::vector<std::string> rationalizations;
    auto operator==(const generator_output_t&) const -> bool = default;
};
struct generator_preview_t;
using generator_proposal_t = proposal_t<generator_preview_t>;
using generator_outcome_t = proposal_outcome_t<generator_preview_t>;
struct generator_preview_t {
    static constexpr proposal_capture_policy_t apply_policy{.stale_label = "Lua generator"};
    [[nodiscard]] auto apply(proposal_edit_scope_t<chart_stamp_t>&) const
        -> diagnosed_t<std::optional<std::string>>;
    generator_recipe_t recipe;
    generator_diff_t notes, layers, charts;
    std::vector<std::string> rationalizations;
    bool keep_recipe{true};
    [[nodiscard]] auto bind_charts(const document_history_t&, std::vector<generator_chart_binding_t>,
        const generator_recipe_t& old, std::uint64_t& next) -> diagnostic_list_t;
    [[nodiscard]] auto stage_charts(document_command_transaction_t&, const generator_recipe_t& old) const
        -> attempt_t<document_edit_effect_t>;
    [[nodiscard]] static auto propose(const document_history_t&, generator_recipe_t,
        generator_output_t, bool keep_recipe = true, std::uint64_t identity_floor = 0) -> generator_outcome_t;
};
[[nodiscard]] auto apply_generator(document_history_t&, const generator_proposal_t&) -> document_command_result_t;
// Removes only recipe ownership; ordinary chart notes/layers remain editable.
[[nodiscard]] auto detach_generator(document_history_t&, recipe_id_t) -> document_command_result_t;
} // namespace wormedit
