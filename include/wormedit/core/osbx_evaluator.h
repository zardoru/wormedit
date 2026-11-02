#pragma once

#include <wormedit/core/arena.h>
#include <wormedit/core/visual_layout.h>
#include <wormedit/core/osbx_document.h>
#include <wormedit/core/storyboard.h>

#include <array>
#include <span>
#include <memory>
#include <string_view>

namespace wormedit {


template <class Value> struct osbx_track_view_t {
    const Value* default_value{};
    std::span<const osbx_key_t<Value>> keys;
    std::span<const osbx_key_program_t<Value>> program;
    const osbx_clip_clock_t* clock{}; // Compiled storage owns the exact scene map.

    [[nodiscard]] static auto from(const osbx_track_t<Value>& track) -> osbx_track_view_t {
        return {.default_value = &track.default_value, .keys = track.keys, .program = track.program};
    }
};

struct osbx_evaluation_op_t {
    std::string_view id;
    osbx_transform_kind_t kind{};
    osbx_track_view_t<std::vector<double>> track;
    std::vector<osbx_evaluation_op_t> children;
    const storyboard_time_t* begin{};
    bool authored{true};
};

struct osbx_evaluation_clip_t {
    const osbx_compiled_clip_t* source{};
    osbx_track_view_t<std::vector<double>> opacity, color;
    osbx_track_view_t<bool> visibility;
    std::optional<osbx_track_view_t<osbx_blend_t>> blend;
};

struct osbx_sample_index_t;

struct osbx_evaluation_node_t {
    const osbx_node_t* source{};
    std::optional<std::size_t> parent; // Earlier entry in topological order.
    std::vector<osbx_evaluation_op_t> ops;
    osbx_track_view_t<std::vector<double>> opacity;
    osbx_track_view_t<std::vector<double>> color;
    osbx_track_view_t<bool> visibility;
    osbx_track_view_t<osbx_blend_t> blend;
    std::vector<osbx_evaluation_clip_t> clips;
    std::string_view sample_refusal; // Static reason, inherited conservatively at index construction.
};

// Immutable after construction. Authored nodes/tracks borrow document storage:
// rebuild on change; that document must outlive the index. Compiled clip views
// share immutable owned storage; prepared_owner retains prepared Lua documents.
struct osbx_evaluation_index_t {
    [[nodiscard]] static auto from(const osbx_document_t& document,
                                   osbx_validation_options_t options = {})
        -> attempt_t<osbx_evaluation_index_t, std::vector<osbx_diagnostic_t>>;
    std::shared_ptr<const osbx_document_t> prepared_owner;
    std::shared_ptr<const std::vector<osbx_compiled_clip_t>> compiled_owner;
    std::vector<osbx_evaluation_node_t> nodes;
    std::vector<std::size_t> draw_order;
    std::shared_ptr<const osbx_sample_index_t> sample_owner;
    [[nodiscard]] auto sample_schedule() const -> const storyboard_schedule_t&;
    [[nodiscard]] auto sample_for(const storyboard_event_t&,
        storyboard_conditional_layer_t = storyboard_conditional_layer_t::pass) const -> const storyboard_sample_t*;
    // Additional bounded property sampling and exact-clock work, excluding
    // operations already charged by the existing export operation counter.
    [[nodiscard]] auto clip_frame_work() const -> std::size_t;
    [[nodiscard]] auto program_frame_work() const -> std::size_t;
};

struct osbx_draw_instance_t {
    std::string_view id;
    visual_affine_t world{1, 0, 0, 1, 0, 0};
    double opacity{1};
    std::array<double, 4> color{1, 1, 1, 1};
    osbx_blend_t blend{osbx_blend_t::normal};
    std::string_view image;
    std::string_view origin;
    std::optional<std::uint32_t> frame_index; // Owned identity; paths belong only to the preview arena.
    auto operator==(const osbx_draw_instance_t&) const -> bool = default;
};

// Borrowed text makes error frames allocation-free too. id identifies the node;
// message is static text. Unsupported placeholders are errors (OSBX-0 #10).
struct osbx_frame_diagnostic_t {
    failure_code_t code{};
    std::string_view id;
    std::string_view message;
    auto operator==(const osbx_frame_diagnostic_t&) const -> bool = default;
};

// Same-pass state for every topologically indexed node, including groups and
// hidden nodes. This is the evaluator's existing arena workspace, exposed read
// only; it owns no second matrix cache or document references.
struct osbx_node_frame_t {
    visual_affine_t matrix{1, 0, 0, 1, 0, 0};
    double opacity{1};
    std::array<double, 4> color{1, 1, 1, 1};
    bool visible{true};
    [[nodiscard]] auto is_finite() const -> bool;
    auto operator==(const osbx_node_frame_t&) const -> bool = default;
};

struct osbx_op_space_t {
    std::string_view node, op; // Borrow the same immutable index source.
    osbx_transform_kind_t kind{};
    visual_affine_t world_side;
    std::array<double, 6> values{};
    std::size_t component_count{};
    [[nodiscard]] auto components() const -> std::span<const double> {
        return std::span{values}.first(component_count);
    }
    auto operator==(const osbx_op_space_t&) const -> bool = default;
};

struct osbx_frame_effect_t {
    std::span<const osbx_draw_instance_t> instances;
    std::span<const osbx_frame_diagnostic_t> diagnostics;
    std::optional<failure_code_t> arena_error;
    std::span<const osbx_node_frame_t> node_states;
    const osbx_evaluation_node_t* source_nodes{}; // Stable through index moves.
    storyboard_time_t time;
    const storyboard_trigger_scenario_t* scenario{}; // Same borrowed input as this frame.
    // The index must be the one used for this frame and outlive the lookup.
    // State pointers expire on the arena's next reset. Unsupported/nonfinite
    // nodes, failed frames and missing stable IDs have no available state.
    [[nodiscard]] auto node_state(const osbx_evaluation_index_t&, std::string_view id) const
        -> const osbx_node_frame_t*;
    // Like node_state, this query must run before the arena's next reset and
    // while the exact index/source is alive. Returned numeric data is owned.
    // Only the selected op's world-side suffix is sampled, using the evaluator's
    // existing op sampler; there is no second node/scene evaluation or arena.
    [[nodiscard]] auto op_space(const osbx_evaluation_index_t&, std::string_view node,
        std::string_view op, storyboard_time_t) const -> result_t<osbx_op_space_t, failure_code_t>;
    [[nodiscard]] auto is_success() const -> bool {
        return diagnostics.empty() && !arena_error;
    }
};

struct osbx_frame_options_t {
    storyboard_conditional_layer_t conditional_layer{storyboard_conditional_layer_t::pass};
    // Serialization/baking preserves the authored scene, including both branches.
    bool include_all_conditional_layers{};
    // Sealed off-frame; caller retains it through frame/op-space queries.
    const storyboard_trigger_scenario_t* scenario{};
};

// Standalone samplers, like indexed frames, require a structurally validated
// track (ordered keys, supported program headers and value arity).
[[nodiscard]] auto sample_osbx_numeric_track(const osbx_track_t<>&, std::int64_t time_ms, const storyboard_trigger_scenario_t* scenario = nullptr)
    -> std::vector<double>;
// Uses the same interpolation as the scene evaluator. Invalid clocks return no
// components; callers must not substitute a truncated integer sample.
[[nodiscard]] auto sample_osbx_numeric_track(const osbx_track_t<>&, storyboard_time_t time, const storyboard_trigger_scenario_t* scenario = nullptr)
    -> std::vector<double>;
[[nodiscard]] auto sample_osbx_blend_track(const osbx_track_t<osbx_blend_t>&, storyboard_time_t, const storyboard_trigger_scenario_t* scenario = nullptr)
    -> result_t<osbx_blend_t, failure_code_t>;
[[nodiscard]] auto sample_osbx_visibility_track(const osbx_track_t<bool>&, storyboard_time_t, const storyboard_trigger_scenario_t* scenario = nullptr)
    -> result_t<bool, failure_code_t>;

// Resets arena. Results live until its next reset; text borrows the document.
// Parent visibility gates descendants; blend is sampled on each sprite itself.
[[nodiscard]] auto evaluate_osbx_frame(const osbx_evaluation_index_t& index, std::int64_t time_ms,
    frame_arena_t& arena, osbx_frame_options_t options = {}) -> osbx_frame_effect_t;

[[nodiscard]] auto evaluate_osbx_frame(const osbx_evaluation_index_t& index, storyboard_time_t time,
    frame_arena_t& arena, osbx_frame_options_t options = {}) -> osbx_frame_effect_t;

} // namespace wormedit
