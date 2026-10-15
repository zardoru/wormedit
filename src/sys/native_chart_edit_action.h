#pragma once

#include "sys/compose_action_outcome.h"
#include <cassert>
#include <type_traits>
#include <utility>
#include <variant>
#include <wormedit/core/bpm_editor.h>
#include <wormedit/core/lane_layout_editor.h>
#include <wormedit/core/marker_editor.h>
#include <wormedit/core/pause_warp_editor.h>
#include <wormedit/core/proposal.h>
#include <wormedit/core/scroll_speed_editor.h>
#include <wormedit/core/semantic_layer_editor.h>
#include <wormedit/core/sv_recipe.h>
#include <wormedit/core/timing_anchor_editor.h>

namespace wormedit::sys {
// The original native variant is borrowed so visiting its typed edit never copies
// strings, layout profiles or recipe vectors back into another variant.
template <class NativeAction, class Owner> struct native_chart_edit_context_t {
    using outcome_type = compose_action_outcome_t;
    Owner& owner;
    const NativeAction& original;
    const compose_action_description_t& description;

    template <class Edit>
    auto validate(const Edit& edit) const -> std::optional<compose_action_outcome_t> {
        assert(std::get_if<Edit>(&original) == &edit);
        if constexpr (std::same_as<Owner, document_command_transaction_t>) {
            // Preserve the native owner's terminal failure policy without staging
            // or publishing a document (commit({}) and finish_without_commit differ).
            if (!owner.is_ready())
                return compose_action_outcome_t::from(apply(), description);
        }
        return std::nullopt;
    }
    template <class Edit> auto perform(const Edit&) const -> compose_action_outcome_t {
        return compose_action_outcome_t::from(apply(), description);
    }

  private:
    auto apply() const -> document_command_result_t {
        if constexpr (std::same_as<NativeAction, bpm_edit_action_t>)
            return apply_bpm_edit(owner, original);
        else if constexpr (std::same_as<NativeAction, marker_edit_action_t>)
            return apply_marker_edit(owner, original);
        else if constexpr (std::same_as<NativeAction, pause_warp_edit_action_t>)
            return apply_pause_warp_edit(owner, original);
        else if constexpr (std::same_as<NativeAction, scroll_speed_edit_action_t>)
            return apply_scroll_speed_edit(owner, original);
        else if constexpr (std::same_as<NativeAction, timing_anchor_edit_action_t>)
            return apply_timing_anchor_edit(owner, original);
        else if constexpr (std::same_as<NativeAction, sv_recipe_edit_action_t>)
            return apply_sv_recipe_edit(owner, original);
        else if constexpr (std::same_as<NativeAction, semantic_layer_edit_action_t>)
            return apply_semantic_layer_edit(owner, original);
        else if constexpr (std::same_as<NativeAction, lane_layout_action_t>)
            return apply_lane_layout_edit(owner, original);
        else
            static_assert(sizeof(NativeAction) == 0, "Unsupported native chart edit family");
    }
};

namespace native_chart_edit_detail {
template <class Edit> inline constexpr bool is_chart_edit = false;
template <class Value> inline constexpr bool is_chart_edit<insert_edit_t<Value>> = true;
template <class Value> inline constexpr bool is_chart_edit<update_edit_t<Value>> = true;
template <class Value> inline constexpr bool is_chart_edit<delete_edit_t<Value>> = true;

template <class Context> struct visitor_t {
    const Context& context;
    template <class Edit> auto operator()(const Edit& edit) const -> compose_action_outcome_t {
        if constexpr (is_chart_edit<Edit>) {
            static_assert(compose_action<Edit, const Context>);
            return run_compose_action(edit, context);
        } else
            return context.perform(edit); // Native non-trio operations keep their existing API.
    }
};

template <class Edit, class Context> consteval auto has_edit_contract() -> bool {
    return !is_chart_edit<Edit> || compose_action<Edit, Context>;
}

template <class NativeAction, std::size_t... Index>
consteval auto has_edit_contracts(std::index_sequence<Index...>) -> bool {
    using context_type = const native_chart_edit_context_t<NativeAction, document_history_t>;
    return (has_edit_contract<std::variant_alternative_t<Index, NativeAction>, context_type>() &&
            ...);
}
} // namespace native_chart_edit_detail

// Explicit NativeAction keeps typed-DTO entry conversions at the native boundary,
// exactly once; an already constructed variant remains borrowed throughout.
template <class NativeAction, class Owner>
[[nodiscard]] auto run_native_chart_edit(Owner& owner, const NativeAction& action,
                                         const compose_action_description_t& description)
    -> compose_action_outcome_t {
    const native_chart_edit_context_t<NativeAction, Owner> context{owner, action, description};
    return std::visit(native_chart_edit_detail::visitor_t{context}, action);
}

// Assert the contract for the original variants rather than duplicating their value catalog.
static_assert(native_chart_edit_detail::has_edit_contracts<bpm_edit_action_t>(
    std::make_index_sequence<std::variant_size_v<bpm_edit_action_t>>{}));
static_assert(native_chart_edit_detail::has_edit_contracts<marker_edit_action_t>(
    std::make_index_sequence<std::variant_size_v<marker_edit_action_t>>{}));
static_assert(native_chart_edit_detail::has_edit_contracts<pause_warp_edit_action_t>(
    std::make_index_sequence<std::variant_size_v<pause_warp_edit_action_t>>{}));
static_assert(native_chart_edit_detail::has_edit_contracts<scroll_speed_edit_action_t>(
    std::make_index_sequence<std::variant_size_v<scroll_speed_edit_action_t>>{}));
static_assert(native_chart_edit_detail::has_edit_contracts<timing_anchor_edit_action_t>(
    std::make_index_sequence<std::variant_size_v<timing_anchor_edit_action_t>>{}));
static_assert(native_chart_edit_detail::has_edit_contracts<sv_recipe_edit_action_t>(
    std::make_index_sequence<std::variant_size_v<sv_recipe_edit_action_t>>{}));
static_assert(native_chart_edit_detail::has_edit_contracts<semantic_layer_edit_action_t>(
    std::make_index_sequence<std::variant_size_v<semantic_layer_edit_action_t>>{}));
static_assert(native_chart_edit_detail::has_edit_contracts<lane_layout_action_t>(
    std::make_index_sequence<std::variant_size_v<lane_layout_action_t>>{}));
} // namespace wormedit::sys
