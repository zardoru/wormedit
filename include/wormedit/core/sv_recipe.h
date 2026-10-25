#pragma once

#include <wormedit/core/proposal.h>
#include <wormedit/core/snap.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <variant>
#include <vector>

namespace wormedit {

inline constexpr std::size_t maximum_sv_recipes_per_chart = 256;
inline constexpr std::uint32_t maximum_sv_recipe_sample_points = 4096;

struct sv_recipe_point_t {
    auto operator==(const sv_recipe_point_t&) const -> bool = default;
    std::uint64_t absolute_row{};
    local_position_t position;
    double value{};
    rational_t transition_duration{};
    bool transition_is_seconds{false};
};

struct sv_recipe_samples_t {
    // Raw built-in inspection. Use the prepared owner or sample_sv_recipe for
    // final postprocessed values used by projection and compilation.
    [[nodiscard]] static auto from(const chart_document_t& chart, const sv_recipe_t& recipe)
        -> diagnosed_t<sv_recipe_samples_t>;
    std::vector<sv_recipe_point_t> points;
    bool tolerance_met{true};
    auto operator==(const sv_recipe_samples_t&) const -> bool = default;
};
using sv_recipe_sample_outcome_t = diagnosed_t<sv_recipe_samples_t>;

// A transient semantic emission, before target rounding or double conversion.
struct sv_recipe_emission_t {
    std::uint64_t absolute_row{};
    long double value{};
    auto operator==(const sv_recipe_emission_t&) const -> bool = default;
};

// Derived finite right-owned approximation of a structural normalized-p impulse.
struct sv_recipe_impulse_box_t {
    std::uint64_t entry_row{}, exit_row{};
    long double raw_area{}, area{}, realized_area{}, area_error{};
    long double width_milliseconds{}, height{};
    auto operator==(const sv_recipe_impulse_box_t&) const -> bool = default;
};

// Script backends are supplied only inside an explicit worker/build scope. Core
// owns sampling and immutable preparation; implementations own bounded sessions.
struct sv_recipe_script_query_t {
    long double p{};
    long double baseline{1};
    std::uint64_t absolute_row{};
    std::uint32_t cycle_index{}, emission_index{}, cycle_emission_index{};
    std::uint32_t cycle_count{}, emission_count{};
};

class sv_recipe_script_session_t {
  public:
    virtual ~sv_recipe_script_session_t() = default;
    [[nodiscard]] virtual auto evaluate(const sv_recipe_script_query_t&)
        -> diagnosed_t<long double> = 0;
    [[nodiscard]] virtual auto finish() -> diagnostic_list_t = 0;
};

class sv_recipe_script_backend_t {
  public:
    virtual ~sv_recipe_script_backend_t() = default;
    [[nodiscard]] virtual auto start(const chart_document_t&, const sv_recipe_t&) const
        -> diagnosed_t<std::unique_ptr<sv_recipe_script_session_t>> = 0;
};

class sv_recipe_evaluation_scope_t {
  public:
    explicit sv_recipe_evaluation_scope_t(const sv_recipe_script_backend_t&);
    ~sv_recipe_evaluation_scope_t();
    sv_recipe_evaluation_scope_t(const sv_recipe_evaluation_scope_t&) = delete;
    auto operator=(const sv_recipe_evaluation_scope_t&) -> sv_recipe_evaluation_scope_t& = delete;
    [[nodiscard]] static auto current() -> const sv_recipe_script_backend_t*;
  private:
    const sv_recipe_script_backend_t* previous_{};
};

// Owns the evaluated recipe contract shared by composition and inspection.
// Built-ins retain their exact random-access curve. Materialized output (from
// an ordered evaluator or a batch transform) is a half-open step function:
// between emissions the previous value holds, without invoking an evaluator.
class sv_recipe_prepared_t {
  public:
    [[nodiscard]] static auto from(const chart_document_t& chart, const sv_recipe_t& recipe)
        -> diagnosed_t<sv_recipe_prepared_t>;
    [[nodiscard]] static auto from_emissions(const chart_document_t& chart,
        const sv_recipe_t& recipe, std::span<const sv_recipe_emission_t> emissions)
        -> diagnosed_t<sv_recipe_prepared_t>;

    [[nodiscard]] auto recipe() const -> const sv_recipe_t& { return recipe_; }
    [[nodiscard]] auto samples() const -> const sv_recipe_samples_t& { return samples_; }
    [[nodiscard]] auto raw_samples() const -> const sv_recipe_samples_t& {
        return recipe_.postprocess.empty() ? samples_ : raw_samples_;
    }
    [[nodiscard]] auto range() const -> const std::pair<std::uint64_t, std::uint64_t>& {
        return range_;
    }
    [[nodiscard]] auto value_at(std::uint64_t row) const -> std::optional<long double>;
    [[nodiscard]] auto continuous_value_at(std::uint64_t row) const -> std::optional<long double>;
    [[nodiscard]] auto has_impulse_at(std::uint64_t entry_row) const -> bool;
    [[nodiscard]] auto impulse_boxes() const -> std::span<const sv_recipe_impulse_box_t> { return impulse_boxes_; }
    [[nodiscard]] auto has_matching_geometry(const chart_document_t& chart,
        const measure_index_t& index) const -> bool;
    auto operator==(const sv_recipe_prepared_t&) const -> bool = default;

  private:
    sv_recipe_prepared_t() = default;
    [[nodiscard]] static auto from_builtin_samples(const chart_document_t&, const sv_recipe_t&,
        sv_recipe_sample_outcome_t) -> diagnosed_t<sv_recipe_prepared_t>;
    sv_recipe_t recipe_;
    chart_id_t chart_;
    std::uint64_t divisions_{};
    std::pair<std::uint64_t, std::uint64_t> range_{};
    sv_recipe_samples_t samples_;
    sv_recipe_samples_t raw_samples_;
    std::vector<sv_recipe_emission_t> emissions_;
    std::vector<sv_recipe_impulse_box_t> impulse_boxes_;
    bool uses_analytic_curve_{};
};

// Borrows immutable worker-prepared snapshots during synchronous frame actions.
// A Lua request that does not exactly match this scope is rejected.
class sv_recipe_prepared_scope_t {
  public:
    explicit sv_recipe_prepared_scope_t(std::span<const sv_recipe_prepared_t>);
    ~sv_recipe_prepared_scope_t();
    sv_recipe_prepared_scope_t(const sv_recipe_prepared_scope_t&) = delete;
    auto operator=(const sv_recipe_prepared_scope_t&) -> sv_recipe_prepared_scope_t& = delete;
    [[nodiscard]] static auto current() -> const sv_recipe_prepared_scope_t*;
    [[nodiscard]] auto find(const chart_document_t&, const sv_recipe_t&) const
        -> const sv_recipe_prepared_t*;
  private:
    std::span<const sv_recipe_prepared_t> prepared_;
    const sv_recipe_prepared_scope_t* previous_{};
};

struct sv_recipe_sample_result_t {
    [[nodiscard]] static auto from(sv_recipe_sample_outcome_t&& outcome) -> sv_recipe_sample_result_t;
    std::vector<sv_recipe_point_t> points;
    diagnostic_list_t diagnostics;
    // True means adaptive sampling reached max_points before meeting tolerance.
    bool tolerance_met{true};

    [[nodiscard]] explicit operator bool() const noexcept {
        return diagnostics.empty();
    }
};

struct sv_recipe_compiled_output_t {
    [[nodiscard]] static auto from(const chart_document_t& chart, sv_recipe_output_t output)
        -> diagnosed_t<sv_recipe_compiled_output_t>;
    // Takes enabled recipes of this output family, from matching chart geometry,
    // in the caller's declared composition order. Snapshot stamps remain the caller's responsibility.
    [[nodiscard]] static auto from(const chart_document_t& chart, sv_recipe_output_t output,
        std::span<const sv_recipe_prepared_t> recipes)
        -> diagnosed_t<sv_recipe_compiled_output_t>;
    sv_recipe_output_t output{sv_recipe_output_t::sv};
    std::vector<sv_recipe_point_t> points;
    auto operator==(const sv_recipe_compiled_output_t&) const -> bool = default;
};
using sv_recipe_compilation_outcome_t = diagnosed_t<sv_recipe_compiled_output_t>;
struct sv_recipe_compilation_t {
    [[nodiscard]] static auto from(sv_recipe_compilation_outcome_t&& outcome) -> sv_recipe_compilation_t;
    [[nodiscard]] auto into_outcome() && -> sv_recipe_compilation_outcome_t;
    sv_recipe_output_t output{sv_recipe_output_t::sv};
    std::vector<sv_recipe_point_t> points;
    diagnostic_list_t diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return diagnostics.empty();
    }
};

using sv_recipe_export_bake_outcome_t = diagnosed_t<std::optional<chart_document_t>>;

// Transient parameter editing is revision-bound. New drafts keep a zero ID
// until Apply; replacement drafts retain the selected recipe's stable ID.
struct sv_recipe_draft_t {
    static constexpr std::string_view begin_locked_message = "SV recipe editing is unavailable while timing is locked";
    static constexpr proposal_capture_policy_t apply_policy{
        .stale_label = "SV recipe draft", .identity = document_identity_policy_t::revision_only,
        .stale_code = diagnostic_code_t::invalid_binding,
        .locked_message = "SV recipe draft was cancelled by timing lock"};
    static constexpr proposal_lifecycle_policy_t lifecycle_policy{
        .inactive_inspect_message = "SV recipe draft is not active",
        .can_echo_inactive_diagnostics = true};
    [[nodiscard]] auto validate_target(const song_document_t&, chart_id_t, proposal_operation_t) const
        -> result_t<const chart_document_t*, diagnostic_t>;
    [[nodiscard]] auto apply(proposal_edit_scope_t<chart_stamp_t>&,
        const proposal_application_input_t& = {}) -> diagnosed_t<std::optional<std::string>>;
    [[nodiscard]] static auto propose(const document_history_t& history, chart_id_t chart,
        sv_recipe_t candidate, bool timing_locked = false) -> proposal_outcome_t<sv_recipe_draft_t>;
    [[nodiscard]] static auto propose(const document_history_t& history, chart_id_t chart,
        recipe_id_t recipe, bool timing_locked = false) -> proposal_outcome_t<sv_recipe_draft_t>;
    // Successful envelope revisions transfer this payload into the returned proposal. Failed
    // range updates keep its last valid candidate. Reuse the prior diagnostic buffer.
    [[nodiscard]] auto revise(proposal_revision_scope_t<chart_stamp_t>&, sv_recipe_t candidate) -> bool;
    [[nodiscard]] auto revise(proposal_revision_scope_t<chart_stamp_t>&,
        position_span_t pointer_hits, snap_grid_t snap) -> bool;
    [[nodiscard]] auto preview_candidate() const -> sv_recipe_t;

    proposal_lifecycle_t lifecycle;
    std::optional<recipe_id_t> existing_recipe;
    sv_recipe_t candidate;
    auto operator==(const sv_recipe_draft_t&) const -> bool = default;
};

static_assert(staged_proposal<sv_recipe_draft_t,
    std::tuple<const document_history_t&, chart_id_t, sv_recipe_t, bool>,
    std::tuple<proposal_revision_scope_t<chart_stamp_t>&, sv_recipe_t>>);
static_assert(staged_proposal<sv_recipe_draft_t,
    std::tuple<const document_history_t&, chart_id_t, recipe_id_t, bool>,
    std::tuple<proposal_revision_scope_t<chart_stamp_t>&, sv_recipe_t>>);
static_assert(staged_proposal<sv_recipe_draft_t,
    std::tuple<const document_history_t&, chart_id_t, sv_recipe_t, bool>,
    std::tuple<proposal_revision_scope_t<chart_stamp_t>&, position_span_t, snap_grid_t>>);

// Raw candidate samples and the effective composition preview are both
// transient. Composition includes the candidate at its stored recipe order,
// or at the end for a new draft.
struct sv_recipe_draft_preview_t {
    sv_recipe_sample_result_t candidate_points;
    sv_recipe_compilation_t effective_output;
    diagnostic_list_t diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return diagnostics.empty();
    }
};

// The same validator is called from whole-document validation and the edit
// command boundary so invalid durable recipe data cannot enter through either.
[[nodiscard]] auto validate_sv_recipe(const chart_document_t& chart, const sv_recipe_t& recipe)
    -> diagnostic_list_t;

// Produces a deterministic transient point batch for one recipe. It never
// mutates the chart and does not store points in the document.
[[nodiscard]] auto sample_sv_recipe(const chart_document_t& chart, const sv_recipe_t& recipe)
    -> sv_recipe_sample_result_t;

// Compiles enabled recipes of one output family into a transient composed
// batch. Authored scroll/speed values form the baseline and overlapping recipe
// values multiply that baseline in chart vector order.
[[nodiscard]] auto compile_sv_recipes(const chart_document_t& chart, sv_recipe_output_t output)
    -> sv_recipe_compilation_t;

// Recent diagnostics are borrowed from the build/revision/command outcome, never
// cached in the payload. Inactive previews/commands retain that feedback when supplied.
[[nodiscard]] auto preview_sv_recipe_draft(const document_history_t& history,
    const proposal_t<sv_recipe_draft_t>& draft,
    std::span<const diagnostic_t> recent_diagnostics = {}) -> sv_recipe_draft_preview_t;


// Builds an export-only chart copy. SV recipes become ordinary instantaneous
// scroll events; SPEEDS recipes become ordinary speed events. The source chart
// remains parametric and unchanged.
[[nodiscard]] auto bake_sv_recipes_for_export(const chart_document_t& chart)
    -> sv_recipe_export_bake_outcome_t;

template <> struct chart_edit_value_traits_t<sv_recipe_t> {
    using id_type = recipe_id_t;
    using delete_options_type = std::monostate;
};

struct duplicate_sv_recipe_action_t {
    chart_id_t chart;
    recipe_id_t id;
};

struct reorder_sv_recipe_action_t {
    chart_id_t chart;
    recipe_id_t id;
    std::size_t new_index{};
};

struct set_sv_recipe_enabled_action_t {
    chart_id_t chart;
    recipe_id_t id;
    bool enabled{true};
};

struct move_sv_recipe_range_action_t {
    chart_id_t chart;
    recipe_id_t id;
    position_span_t range;
};

enum class sv_recipe_range_handle_t {
    begin,
    end,
    whole_span,
};

// Renderer-neutral state for clicking and dragging a recipe's start, end, or
// whole interval. Only preview_range changes during the gesture; the document
// is published once by apply_proposal.
struct sv_recipe_range_drag_t {
    static constexpr proposal_capture_policy_t apply_policy{
        .stale_label = "SV recipe range drag", .identity = document_identity_policy_t::revision_only,
        .stale_code = diagnostic_code_t::invalid_binding,
        .locked_message = "SV recipe range drag was cancelled by timing lock"};
    static constexpr std::string_view invalid_target_message = "SV recipe range drag target no longer exists";
    static constexpr proposal_lifecycle_policy_t lifecycle_policy{
        .inactive_apply_message = "SV recipe range drag is not active",
        .locked_status = proposal_status_t::rejected,
        .expired_status = proposal_status_t::rejected,
        .binding_status = proposal_status_t::rejected,
        .preparation_failure_status = proposal_status_t::rejected,
        .unchanged_status = proposal_status_t::cancelled,
        .completion = proposal_completion_policy_t::applied};
    [[nodiscard]] auto validate_target(const song_document_t&, chart_id_t, proposal_operation_t) const
        -> result_t<const chart_document_t*, diagnostic_t>;
    [[nodiscard]] auto is_unchanged() const noexcept -> bool;
    [[nodiscard]] auto apply(proposal_edit_scope_t<chart_stamp_t>&,
        const proposal_application_input_t& = {}) -> diagnosed_t<std::optional<std::string>>;
    // The pointer is an unsnapped chart hit; the snap denominator must divide the chart resolution.
    [[nodiscard]] static auto propose(const document_history_t& history, chart_id_t chart,
        recipe_id_t recipe, sv_recipe_range_handle_t handle, local_position_t pointer_position,
        snap_grid_t snap, bool timing_locked)
        -> proposal_outcome_t<sv_recipe_range_drag_t>;
    // Failed pointer updates leave the previous valid range available. Fatal errors
    // mark this state rejected; neither failure returns an applicable proposal.
    [[nodiscard]] auto revise(proposal_revision_scope_t<chart_stamp_t>&, local_position_t pointer_position) -> bool;

    proposal_lifecycle_t lifecycle;
    recipe_id_t recipe;
    sv_recipe_range_handle_t handle{sv_recipe_range_handle_t::begin};
    position_span_t original_range;
    position_span_t preview_range;
    local_position_t snapped_pointer_anchor;
    snap_grid_t snap;
    auto operator==(const sv_recipe_range_drag_t&) const -> bool = default;
};

static_assert(staged_proposal<sv_recipe_range_drag_t,
    std::tuple<const document_history_t&, chart_id_t, recipe_id_t, sv_recipe_range_handle_t,
               local_position_t, snap_grid_t, bool>,
    std::tuple<proposal_revision_scope_t<chart_stamp_t>&, local_position_t>>);

// Turns one recipe into ordinary scroll or speed events and removes the recipe. The events are the
// recipe's values multiplied into the authored baseline, exactly what export baking would emit for
// this recipe alone; other recipes keep composing over the new baseline. Disabled recipes
// materialize too. A recipe that cannot be evaluated (a Lua expression curve, for now) rejects.
struct materialize_sv_recipe_action_t {
    chart_id_t chart;
    recipe_id_t id;
};

using sv_recipe_edit_action_t = std::variant<
    wormedit::insert_edit_t<wormedit::sv_recipe_t>, wormedit::update_edit_t<wormedit::sv_recipe_t>,
    wormedit::delete_edit_t<wormedit::sv_recipe_t>, duplicate_sv_recipe_action_t, reorder_sv_recipe_action_t,
    set_sv_recipe_enabled_action_t, move_sv_recipe_range_action_t, materialize_sv_recipe_action_t>;

// Every recipe edit is revision checked and published through shared document
// history. Range changes are exact chart-grid positions, so dragging previews
// can submit a snapped span as one atomic command.
// Temporary: removed by T-637; native_chart_edit_action.h dispatches into existing transactions.
[[nodiscard]] auto apply_sv_recipe_edit(document_command_transaction_t& transaction,
                                        const sv_recipe_edit_action_t& action)
    -> document_command_result_t;

[[nodiscard]] auto apply_sv_recipe_edit(document_history_t& history,
                                        const sv_recipe_edit_action_t& action)
    -> document_command_result_t;

} // namespace wormedit
