#pragma once
#include <wormedit/core/id.h>
#include <cstdint>
#include <array>
#include <string_view>
#include <optional>
#include <string>
#include <vector>

namespace wormedit::ui {
enum class bms_program_node_kind_t {
    root, random, if_arm, else_if, else_arm, switch_scope, case_arm, default_arm,
    set_random, set_switch, chart, text, bare
};
struct bms_program_kind_t {
    bms_program_node_kind_t kind;
    std::string_view label;
    bool can_add{};
    auto operator==(const bms_program_kind_t&) const -> bool = default;
};
inline constexpr std::array bms_program_kinds{
    bms_program_kind_t{.kind = bms_program_node_kind_t::root, .label = "Root"},
    bms_program_kind_t{.kind = bms_program_node_kind_t::random, .label = "RANDOM", .can_add = true},
    bms_program_kind_t{.kind = bms_program_node_kind_t::if_arm, .label = "IF", .can_add = true},
    bms_program_kind_t{.kind = bms_program_node_kind_t::else_if, .label = "ELSEIF", .can_add = true},
    bms_program_kind_t{.kind = bms_program_node_kind_t::else_arm, .label = "ELSE", .can_add = true},
    bms_program_kind_t{.kind = bms_program_node_kind_t::switch_scope, .label = "SWITCH", .can_add = true},
    bms_program_kind_t{.kind = bms_program_node_kind_t::case_arm, .label = "CASE", .can_add = true},
    bms_program_kind_t{.kind = bms_program_node_kind_t::default_arm, .label = "DEFAULT", .can_add = true},
    bms_program_kind_t{.kind = bms_program_node_kind_t::set_random, .label = "SETRANDOM", .can_add = true},
    bms_program_kind_t{.kind = bms_program_node_kind_t::set_switch, .label = "SETSWITCH", .can_add = true},
    bms_program_kind_t{.kind = bms_program_node_kind_t::chart, .label = "Chart", .can_add = true},
    bms_program_kind_t{.kind = bms_program_node_kind_t::text, .label = "Text", .can_add = true},
    bms_program_kind_t{.kind = bms_program_node_kind_t::bare, .label = "Always"}};
enum class bms_program_operation_t {
    select, reload, draft_parameter, draft_text, draft_preview, save, add, move,
    prepare_delete, prepare_unwrap, confirm_delete, confirm_promotion, confirm_unconditional, cancel, set_preview, clear_preview
};
struct bms_program_action_t {
    song_id_t project;
    chart_id_t difficulty;
    std::uint64_t revision{};
    branch_id_t selected;
    bms_program_operation_t operation{};
    branch_id_t node;
    branch_id_t parent;
    std::optional<branch_id_t> after;
    bms_program_node_kind_t kind{};
    std::string text;
    auto operator==(const bms_program_action_t&) const -> bool = default;
};
struct bms_program_row_t {
    branch_id_t node;
    std::string label, path;
    std::size_t depth{};
    bms_program_node_kind_t kind{};
    bool selected{}, playback_active{}, unreachable{};
    auto operator==(const bms_program_row_t&) const -> bool = default;
};
struct bms_program_message_t {
    std::string text;
    bool warning{}, error{};
    auto operator==(const bms_program_message_t&) const -> bool = default;
};
struct bms_program_view_t {
    bool available{}, stale_draft{}, has_parameter{}, has_text{}, can_preview{}, can_delete{}, can_unwrap{};
    song_id_t project;
    chart_id_t difficulty;
    std::uint64_t revision{};
    branch_id_t selected;
    std::optional<branch_id_t> content_chart;
    std::string target_path, parameter, text, preview;
    std::optional<std::int64_t> playback_choice;
    std::vector<bms_program_row_t> rows;
    std::vector<bms_program_message_t> messages;
    std::string confirmation;
    std::optional<bms_program_operation_t> confirmation_operation;
    bool unconditional_conversion{};
    auto operator==(const bms_program_view_t&) const -> bool = default;
};
}
