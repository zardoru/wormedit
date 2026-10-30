#pragma once

#include "ui/frontend/actions.h"
#include "ui/frontend/compose.h"
#include <wormedit/core/interaction_actions.h>
#include <wormedit/core/result.h>
#include <wormedit/core/span_manipulation.h>
#include <wormedit/core/sv_recipe.h>

#include <cstdint>
#include <optional>
#include <string_view>
#include <variant>

namespace wormedit::sys::compose_input_detail {

static_assert(static_cast<std::uint8_t>(wormedit::note_paint_intent_t::toggle) == 0);
static_assert(static_cast<std::uint8_t>(wormedit::note_paint_intent_t::draw) == 1);
static_assert(static_cast<std::uint8_t>(wormedit::note_paint_intent_t::erase) == 2);
static_assert(static_cast<std::uint8_t>(wormedit::span_handle_t::begin) == WORM_UI_SPAN_HANDLE_BEGIN);
static_assert(static_cast<std::uint8_t>(wormedit::span_handle_t::end) == WORM_UI_SPAN_HANDLE_END);
static_assert(static_cast<std::uint8_t>(wormedit::span_handle_t::body) == WORM_UI_SPAN_HANDLE_BODY);
static_assert(static_cast<std::uint8_t>(wormedit::sv_recipe_range_handle_t::begin) == WORM_UI_SPAN_HANDLE_BEGIN);
static_assert(static_cast<std::uint8_t>(wormedit::sv_recipe_range_handle_t::end) == WORM_UI_SPAN_HANDLE_END);
static_assert(static_cast<std::uint8_t>(wormedit::sv_recipe_range_handle_t::whole_span) == WORM_UI_SPAN_HANDLE_BODY);
static_assert(static_cast<std::uint8_t>(wormedit::ui::compose_recipe_command_t::toggle_enabled) == 0);
static_assert(static_cast<std::uint8_t>(wormedit::ui::compose_recipe_command_t::later) == 5);

enum class kiai_handle_value_t : std::uint8_t { begin, end, body, create };
static_assert(static_cast<std::uint8_t>(kiai_handle_value_t::begin) == WORM_UI_SPAN_HANDLE_BEGIN);
static_assert(static_cast<std::uint8_t>(kiai_handle_value_t::end) == WORM_UI_SPAN_HANDLE_END);
static_assert(static_cast<std::uint8_t>(kiai_handle_value_t::body) == WORM_UI_SPAN_HANDLE_BODY);
static_assert(static_cast<std::uint8_t>(kiai_handle_value_t::create) == WORM_UI_SPAN_HANDLE_CREATE);

struct admitted_note_paint_t {
    wormedit::note_paint_intent_t intent{};
    auto operator==(const admitted_note_paint_t&) const -> bool = default;
    [[nodiscard]] static auto from(std::uint8_t raw) -> std::optional<admitted_note_paint_t> {
        if (raw > 2) return std::nullopt;
        return admitted_note_paint_t{static_cast<wormedit::note_paint_intent_t>(raw)};
    }
};
struct admitted_semantic_layer_paint_t {
    wormedit::semantic_layer_paint_mode_t mode{};
    auto operator==(const admitted_semantic_layer_paint_t&) const -> bool = default;
    [[nodiscard]] static auto from(std::uint8_t raw)
        -> std::optional<admitted_semantic_layer_paint_t> {
        using mode_t = wormedit::semantic_layer_paint_mode_t;
        if (raw == 1) return admitted_semantic_layer_paint_t{mode_t::add};
        if (raw == 2) return admitted_semantic_layer_paint_t{mode_t::remove};
        return std::nullopt;
    }
};
struct admitted_bga_draw_t {
    bool erase{};
    auto operator==(const admitted_bga_draw_t&) const -> bool = default;
    [[nodiscard]] static auto from(std::uint8_t raw) -> std::optional<admitted_bga_draw_t> {
        if (raw != 1 && raw != 2) return std::nullopt;
        return admitted_bga_draw_t{raw == 2};
    }
};
struct admitted_draw_source_t {
    std::optional<wormedit::resource_role_t> role;
    wormedit::resource_id_t resource{};
    auto operator==(const admitted_draw_source_t&) const -> bool = default;
    [[nodiscard]] static auto from(const wormedit::ui::compose_input_action_t& input)
        -> std::optional<admitted_draw_source_t> {
        using role_t = wormedit::resource_role_t;
        if (input.resource_role && *input.resource_role != role_t::sample &&
            *input.resource_role != role_t::bga)
            return std::nullopt;
        if (!input.resource_role && input.resource_id.value != 0) return std::nullopt;
        return admitted_draw_source_t{input.resource_role, input.resource_id};
    }
};
struct admitted_span_handle_t {
    wormedit::span_handle_t handle{};
    auto operator==(const admitted_span_handle_t&) const -> bool = default;
    [[nodiscard]] static auto from(std::uint8_t raw) -> std::optional<admitted_span_handle_t> {
        if (raw > 2) return std::nullopt;
        return admitted_span_handle_t{static_cast<wormedit::span_handle_t>(raw)};
    }
};
struct admitted_kiai_handle_t {
    kiai_handle_value_t handle{};
    auto operator==(const admitted_kiai_handle_t&) const -> bool = default;
    [[nodiscard]] static auto from(std::uint8_t raw) -> std::optional<admitted_kiai_handle_t> {
        if (raw > 3) return std::nullopt;
        return admitted_kiai_handle_t{static_cast<kiai_handle_value_t>(raw)};
    }
};
struct admitted_recipe_range_handle_t {
    wormedit::sv_recipe_range_handle_t handle{};
    auto operator==(const admitted_recipe_range_handle_t&) const -> bool = default;
    [[nodiscard]] static auto from(std::uint8_t raw)
        -> std::optional<admitted_recipe_range_handle_t> {
        if (raw > 2) return std::nullopt;
        return admitted_recipe_range_handle_t{static_cast<wormedit::sv_recipe_range_handle_t>(raw)};
    }
};
struct admitted_recipe_command_t {
    wormedit::ui::compose_recipe_command_t command{};
    auto operator==(const admitted_recipe_command_t&) const -> bool = default;
    [[nodiscard]] static auto from(std::uint8_t raw) -> std::optional<admitted_recipe_command_t> {
        using command_t = wormedit::ui::compose_recipe_command_t;
        if (raw > static_cast<std::uint8_t>(command_t::later)) return std::nullopt;
        return admitted_recipe_command_t{static_cast<command_t>(raw)};
    }
};

using admitted_compose_selector_t = std::variant<admitted_note_paint_t,
    admitted_semantic_layer_paint_t, admitted_bga_draw_t, admitted_draw_source_t, admitted_kiai_handle_t,
    admitted_span_handle_t, admitted_recipe_range_handle_t, admitted_recipe_command_t>;

using compose_selector_admission_t = wormedit::result_t<
    std::optional<admitted_compose_selector_t>, std::string_view>;

inline constexpr std::string_view unsupported_compose_selector_message =
    "Unsupported Compose input.";
inline constexpr std::string_view stale_export_cut_handle_message = "The export cut handle is stale.";

template <typename Admitted>
auto as_compose_selector(std::optional<Admitted> admitted,
    std::string_view message = unsupported_compose_selector_message) -> compose_selector_admission_t {
    if (!admitted) return std::unexpected{message};
    return std::optional<admitted_compose_selector_t>{admitted_compose_selector_t{*admitted}};
}

inline auto admit_compose_input_selector(const wormedit::ui::compose_input_action_t& input)
    -> compose_selector_admission_t {
    using namespace wormedit;
    using namespace wormedit::ui;
    const auto raw_paint = input.paint_intent;
    const auto bga_or_bgm = input.resource_role == resource_role_t::bga ||
                            input.resource_role == resource_role_t::bgm;
    switch (input.kind) {
    case WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN:
        if (bga_or_bgm) {
            return as_compose_selector(admitted_bga_draw_t::from(raw_paint));
        }
        if (input.resource_role && *input.resource_role != resource_role_t::sample)
            return std::unexpected{unsupported_compose_selector_message};
        return as_compose_selector(admitted_note_paint_t::from(raw_paint));
    case WORM_UI_COMPOSE_INPUT_NOTE_PAINT_UPDATE:
    case WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE:
        if (!bga_or_bgm) return std::optional<admitted_compose_selector_t>{};
        return as_compose_selector(admitted_bga_draw_t::from(raw_paint));
    case WORM_UI_COMPOSE_INPUT_SEMANTIC_LAYER_PAINT_BEGIN:
        return as_compose_selector(admitted_semantic_layer_paint_t::from(raw_paint));
    case WORM_UI_COMPOSE_INPUT_KIAI_SPAN_EDIT:
        return as_compose_selector(admitted_kiai_handle_t::from(input.span_edge));
    case WORM_UI_COMPOSE_INPUT_SPEED_SPAN_EDIT:
    case WORM_UI_COMPOSE_INPUT_PREVIEW_SPAN_EDIT:
    case WORM_UI_COMPOSE_INPUT_EXPORT_CUT_SPAN_EDIT:
        return as_compose_selector(admitted_span_handle_t::from(input.span_edge),
            input.kind == WORM_UI_COMPOSE_INPUT_EXPORT_CUT_SPAN_EDIT
                ? stale_export_cut_handle_message : unsupported_compose_selector_message);
    case WORM_UI_COMPOSE_INPUT_RECIPE_RANGE_EDIT:
        return as_compose_selector(admitted_recipe_range_handle_t::from(input.span_edge));
    case WORM_UI_COMPOSE_INPUT_RECIPE_COMMAND:
        return as_compose_selector(admitted_recipe_command_t::from(input.span_edge),
                                    "Unknown SV recipe command.");
    case WORM_UI_COMPOSE_INPUT_DRAW_SOURCE:
        return as_compose_selector(admitted_draw_source_t::from(input), "Unsupported Draw source.");
    default:
        return std::optional<admitted_compose_selector_t>{};
    }
}

} // namespace wormedit::sys::compose_input_detail
