#pragma once

#include <wormedit/core/command.h>

#include <cstddef>
#include <cstdint>
#include <variant>
#include <vector>

namespace wormedit {

inline constexpr std::size_t maximum_sv_recipes_per_chart = 256;
inline constexpr std::uint32_t maximum_sv_recipe_sample_points = 4096;

struct sv_recipe_point_t {
    std::uint64_t absolute_row{};
    local_position_t position;
    double value{};
    rational_t transition_duration{};
    bool transition_is_seconds{false};
};

struct sv_recipe_sample_result_t {
    std::vector<sv_recipe_point_t> points;
    diagnostic_list_t diagnostics;
    // True means adaptive sampling reached max_points before meeting tolerance.
    bool tolerance_met{true};

    [[nodiscard]] explicit operator bool() const noexcept {
        return diagnostics.empty();
    }
};

struct sv_recipe_compilation_t {
    sv_recipe_output_t output{sv_recipe_output_t::sv};
    std::vector<sv_recipe_point_t> points;
    diagnostic_list_t diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return diagnostics.empty();
    }
};

struct sv_recipe_export_bake_result_t {
    std::optional<chart_document_t> chart;
    diagnostic_list_t diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return chart.has_value() && diagnostics.empty();
    }
};

enum class sv_recipe_draft_status_t {
    inactive,
    editing,
    applied,
    cancelled,
    rejected,
};

// Transient parameter editing is revision-bound. New drafts keep a zero ID
// until Apply; replacement drafts retain the selected recipe's stable ID.
struct sv_recipe_draft_t {
    sv_recipe_draft_status_t status{sv_recipe_draft_status_t::inactive};
    chart_stamp_t stamp;
    std::optional<recipe_id_t> existing_recipe;
    sv_recipe_t candidate;
    diagnostic_list_t diagnostics;
};

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

[[nodiscard]] auto begin_sv_recipe_draft(const document_history_t& history, chart_id_t chart,
                                         sv_recipe_t candidate, bool timing_locked = false)
    -> sv_recipe_draft_t;
[[nodiscard]] auto begin_existing_sv_recipe_draft(const document_history_t& history,
                                                  chart_id_t chart, recipe_id_t recipe,
                                                  bool timing_locked = false) -> sv_recipe_draft_t;
// Replaces only transient candidate data; invalid parameter combinations stay
// editable and are reported by preview/apply validation.
[[nodiscard]] auto revise_sv_recipe_draft(const document_history_t& history,
                                          sv_recipe_draft_t& draft, sv_recipe_t candidate,
                                          bool timing_locked = false) -> bool;
// Snaps both unsnapped interval endpoints to an exact grid, normalizes drag
// direction, and updates only the transient candidate range.
[[nodiscard]] auto revise_sv_recipe_draft_range(const document_history_t& history,
                                             sv_recipe_draft_t& draft, local_position_t begin_hit,
                                             local_position_t end_hit,
                                             std::uint64_t active_snap_denominator,
                                             bool timing_locked = false) -> bool;
[[nodiscard]] auto preview_sv_recipe_draft(const document_history_t& history,
                                           const sv_recipe_draft_t& draft)
    -> sv_recipe_draft_preview_t;
[[nodiscard]] auto apply_sv_recipe_draft(document_history_t& history, sv_recipe_draft_t& draft,
                                         bool timing_locked = false) -> document_command_result_t;
void cancel_sv_recipe_draft(sv_recipe_draft_t& draft) noexcept;

// Builds an export-only chart copy. Generic SV and SCROLLS recipes become
// ordinary instantaneous scroll events; SPEEDS recipes become ordinary speed
// events. The source chart remains parametric and unchanged.
[[nodiscard]] auto bake_sv_recipes_for_export(const chart_document_t& chart)
    -> sv_recipe_export_bake_result_t;

struct insert_sv_recipe_action_t {
    chart_id_t chart;
    sv_recipe_t recipe;
};

struct update_sv_recipe_action_t {
    chart_id_t chart;
    recipe_id_t id;
    sv_recipe_t replacement;
};

struct delete_sv_recipe_action_t {
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

enum class sv_recipe_range_drag_status_t {
    inactive,
    previewing,
    committed,
    cancelled,
    rejected,
};

// Renderer-neutral state for clicking and dragging a recipe's start, end, or
// whole interval. Only preview_range changes during the gesture; the document
// is published once by apply_sv_recipe_range_drag.
struct sv_recipe_range_drag_t {
    sv_recipe_range_drag_status_t status{sv_recipe_range_drag_status_t::inactive};
    chart_stamp_t stamp;
    recipe_id_t recipe;
    sv_recipe_range_handle_t handle{sv_recipe_range_handle_t::begin};
    position_span_t original_range;
    position_span_t preview_range;
    local_position_t snapped_pointer_anchor;
    std::uint64_t active_snap_denominator{};
    diagnostic_list_t diagnostics;
};

// The pointer position is an unsnapped chart hit. The active denominator must
// divide the chart's standard-measure resolution exactly.
[[nodiscard]] auto begin_sv_recipe_range_drag(const document_history_t& history, chart_id_t chart,
                                              recipe_id_t recipe, sv_recipe_range_handle_t handle,
                                              local_position_t pointer_position,
                                              std::uint64_t active_snap_denominator,
                                              bool timing_locked) -> sv_recipe_range_drag_t;

// Updates only preview_range. Crossing an endpoint normalizes the interval;
// invalid or zero-width positions are diagnosed without replacing the last
// valid preview.
[[nodiscard]] auto revise_sv_recipe_range_drag(const document_history_t& history,
                                               sv_recipe_range_drag_t& drag,
                                               local_position_t pointer_position,
                                               bool timing_locked) -> bool;

// Publishes the previewed interval as one revision-checked history command.
[[nodiscard]] auto apply_sv_recipe_range_drag(document_history_t& history,
                                               sv_recipe_range_drag_t& drag, bool timing_locked)
    -> document_command_result_t;

void cancel_sv_recipe_range_drag(sv_recipe_range_drag_t& drag) noexcept;

// Turns one recipe into ordinary scroll or speed events and removes the recipe. The events are the
// recipe's values multiplied into the authored baseline, exactly what export baking would emit for
// this recipe alone; other recipes keep composing over the new baseline. Disabled recipes
// materialize too. A recipe that cannot be evaluated (a Lua expression curve, for now) rejects.
struct materialize_sv_recipe_action_t {
    chart_id_t chart;
    recipe_id_t id;
};

using sv_recipe_edit_action_t =
    std::variant<insert_sv_recipe_action_t, update_sv_recipe_action_t, delete_sv_recipe_action_t,
                 reorder_sv_recipe_action_t, set_sv_recipe_enabled_action_t,
                 move_sv_recipe_range_action_t, materialize_sv_recipe_action_t>;

// Every recipe edit is revision checked and published through shared document
// history. Range changes are exact chart-grid positions, so dragging previews
// can submit a snapped span as one atomic command.
[[nodiscard]] auto apply_sv_recipe_edit(document_command_transaction_t& transaction,
                                        const sv_recipe_edit_action_t& action)
    -> document_command_result_t;

[[nodiscard]] auto apply_sv_recipe_edit(document_history_t& history,
                                        const sv_recipe_edit_action_t& action)
    -> document_command_result_t;

} // namespace wormedit
