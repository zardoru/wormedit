#include "checked_math.h"
#include "text_util.h"
#include "wormedit/core/numeric_text.h"
#include <wormedit/core/storyboard.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <numbers>
#include <string>
#include <tuple>
#include <utility>

namespace wormedit {
namespace {

using checked_math::checked_add;

auto checked_add_unsigned_i64(std::int64_t base, std::uint64_t offset,
                              std::int64_t& output) noexcept -> bool {
    if (base >= 0) {
        const auto positive_base = static_cast<std::uint64_t>(base);
        const auto maximum = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
        if (offset > maximum - positive_base)
            return false;
        output = static_cast<std::int64_t>(positive_base + offset);
        return true;
    }

    const auto base_magnitude = static_cast<std::uint64_t>(-(base + 1)) + 1;
    if (offset < base_magnitude) {
        const auto negative_magnitude = base_magnitude - offset;
        if (negative_magnitude == (std::uint64_t{1} << 63)) {
            output = std::numeric_limits<std::int64_t>::min();
        } else {
            output = -static_cast<std::int64_t>(negative_magnitude);
        }
        return true;
    }

    const auto positive = offset - base_magnitude;
    if (positive > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        return false;
    output = static_cast<std::int64_t>(positive);
    return true;
}

auto checked_mul_nonnegative(std::int64_t value, std::uint64_t count, std::int64_t& output) noexcept
    -> bool {
    if (value < 0) {
        return false;
    }
    if (value == 0 || count == 0) {
        output = 0;
        return true;
    }
    const auto maximum = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    const auto unsigned_value = static_cast<std::uint64_t>(value);
    if (count > maximum / unsigned_value) {
        return false;
    }
    output = static_cast<std::int64_t>(unsigned_value * count);
    return true;
}

auto distance_i64(std::int64_t begin, std::int64_t end) noexcept -> std::uint64_t {
    return static_cast<std::uint64_t>(end) - static_cast<std::uint64_t>(begin);
}

auto expected_value_count(storyboard_opcode_t opcode) noexcept -> std::uint8_t {
    switch (opcode) {
    case storyboard_opcode_t::fade:
    case storyboard_opcode_t::move_x:
    case storyboard_opcode_t::move_y:
    case storyboard_opcode_t::scale:
    case storyboard_opcode_t::rotate:
        return 1;
    case storyboard_opcode_t::move:
    case storyboard_opcode_t::vector_scale:
        return 2;
    case storyboard_opcode_t::color:
        return 3;
    }
    return 0;
}

auto opcode_name(storyboard_opcode_t opcode) noexcept -> std::string_view {
    switch (opcode) {
    case storyboard_opcode_t::fade:
        return "F";
    case storyboard_opcode_t::move:
        return "M";
    case storyboard_opcode_t::move_x:
        return "MX";
    case storyboard_opcode_t::move_y:
        return "MY";
    case storyboard_opcode_t::scale:
        return "S";
    case storyboard_opcode_t::vector_scale:
        return "V";
    case storyboard_opcode_t::rotate:
        return "R";
    case storyboard_opcode_t::color:
        return "C";
    }
    return {};
}

auto is_valid_opcode(storyboard_opcode_t opcode) noexcept -> bool {
    return !opcode_name(opcode).empty();
}

auto is_valid_easing(std::int32_t easing) noexcept -> bool {
    return easing >= 0 && easing <= 34;
}

auto parse_opcode(std::string_view name) noexcept -> std::optional<storyboard_opcode_t> {
    if (name == "F")
        return storyboard_opcode_t::fade;
    if (name == "M")
        return storyboard_opcode_t::move;
    if (name == "MX")
        return storyboard_opcode_t::move_x;
    if (name == "MY")
        return storyboard_opcode_t::move_y;
    if (name == "S")
        return storyboard_opcode_t::scale;
    if (name == "V")
        return storyboard_opcode_t::vector_scale;
    if (name == "R")
        return storyboard_opcode_t::rotate;
    if (name == "C")
        return storyboard_opcode_t::color;
    return std::nullopt;
}

auto trim(std::string_view text) noexcept -> std::string_view {
    return text_util::trim(text, " \t");
}

auto split_csv(std::string_view line) -> std::vector<std::string> {
    std::vector<std::string> fields;
    std::string field;
    bool quoted = false;
    for (std::size_t index = 0; index < line.size(); ++index) {
        const auto value = line[index];
        if (value == '"') {
            if (quoted && index + 1 < line.size() && line[index + 1] == '"') {
                field.push_back('"');
                ++index;
            } else {
                quoted = !quoted;
            }
        } else if (value == ',' && !quoted) {
            const auto cleaned = trim(field);
            fields.emplace_back(cleaned);
            field.clear();
        } else {
            field.push_back(value);
        }
    }
    const auto cleaned = trim(field);
    fields.emplace_back(cleaned);
    return fields;
}

template <class Integer>
auto parse_integer(std::string_view text, Integer& value) noexcept -> bool {
    return wormedit::parse_integer(trim(text), value);
}

auto parse_double(std::string_view text, double& value) noexcept -> bool {
    text = trim(text);
    if (text.empty())
        return false;
    const auto [end, error] =
        std::from_chars(text.data(), text.data() + text.size(), value, std::chars_format::general);
    return error == std::errc{} && end == text.data() + text.size() && std::isfinite(value);
}

auto append_integer(std::string& output, std::int64_t value) -> bool {
    char buffer[32];
    const auto [end, error] = std::to_chars(buffer, buffer + sizeof(buffer), value);
    if (error != std::errc{})
        return false;
    output.append(buffer, end);
    return true;
}

auto append_unsigned(std::string& output, std::uint64_t value) -> bool {
    char buffer[32];
    const auto [end, error] = std::to_chars(buffer, buffer + sizeof(buffer), value);
    if (error != std::errc{})
        return false;
    output.append(buffer, end);
    return true;
}

auto append_double(std::string& output, double value) -> bool {
    char buffer[64];
    const auto [end, error] =
        std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::general,
                      std::numeric_limits<double>::max_digits10);
    if (error != std::errc{})
        return false;
    output.append(buffer, end);
    return true;
}

auto append_bounded(std::string& output, std::string_view text, std::size_t maximum) -> bool {
    if (text.size() > maximum || output.size() > maximum - text.size())
        return false;
    output.append(text);
    return true;
}

auto line_prefix(std::size_t indentation) -> std::string {
    return std::string(indentation, ' ');
}

auto is_valid_single_line(std::string_view value) noexcept -> bool {
    return value.find('\n') == std::string_view::npos && value.find('\r') == std::string_view::npos;
}

auto is_valid_header_token(std::string_view value) noexcept -> bool {
    return is_valid_single_line(value) && value.find(',') == std::string_view::npos &&
           value.find('"') == std::string_view::npos;
}

auto is_valid_emit_node(const storyboard_node_t& node, std::size_t depth,
                     std::size_t node_count) -> bool {
    if (depth > 128 || node_count >= 250'000 ||
        (node.kind != storyboard_node_kind_t::command &&
         node.kind != storyboard_node_kind_t::loop &&
         node.kind != storyboard_node_kind_t::retained_line) ||
        !is_valid_opcode(node.command.opcode) ||
        !std::ranges::all_of(node.command.from,
                             [](double value) { return std::isfinite(value); }) ||
        !std::ranges::all_of(node.command.to, [](double value) { return std::isfinite(value); })) {
        return false;
    }
    return true;
}

auto emit_node(std::string& output, const storyboard_node_t& node, std::size_t indentation,
               std::size_t maximum_bytes, std::size_t depth, std::size_t& node_count) -> bool;

auto emit_loop_node(std::string& output, const storyboard_node_t& node,
                    std::size_t indentation, std::size_t maximum_bytes, std::size_t depth,
                    std::size_t& node_count) -> bool {
    const auto prefix = line_prefix(indentation);
    if (!append_bounded(output, prefix, maximum_bytes) ||
        !append_bounded(output, "L,", maximum_bytes) ||
        !append_integer(output, node.loop_start_time_ms) ||
        !append_bounded(output, ",", maximum_bytes) ||
        !append_unsigned(output, node.loop_iteration_count) ||
        !append_bounded(output, "\n", maximum_bytes)) {
        return false;
    }
    if (indentation == std::numeric_limits<std::size_t>::max()) {
        return false;
    }
    for (const auto& child : node.children) {
        if (!emit_node(output, child, indentation + 1, maximum_bytes, depth + 1, node_count)) {
            return false;
        }
    }
    return true;
}

auto emit_command_node(std::string& output, const storyboard_node_t& node,
                       std::size_t indentation, std::size_t maximum_bytes) -> bool {
    const auto& command = node.command;
    if (command.value_count != expected_value_count(command.opcode) ||
        !is_valid_easing(command.easing) || command.start_time_ms > command.end_time_ms ||
        !append_bounded(output, line_prefix(indentation), maximum_bytes) ||
        !append_bounded(output, opcode_name(command.opcode), maximum_bytes) ||
        !append_bounded(output, ",", maximum_bytes) || !append_integer(output, command.easing) ||
        !append_bounded(output, ",", maximum_bytes) ||
        !append_integer(output, command.start_time_ms) ||
        !append_bounded(output, ",", maximum_bytes) ||
        !append_integer(output, command.end_time_ms)) {
        return false;
    }
    for (std::size_t index = 0; index < command.value_count; ++index) {
        if (!append_bounded(output, ",", maximum_bytes) ||
            !append_double(output, command.from[index])) {
            return false;
        }
    }
    for (std::size_t index = 0; index < command.value_count; ++index) {
        if (!append_bounded(output, ",", maximum_bytes) ||
            !append_double(output, command.to[index])) {
            return false;
        }
    }
    return append_bounded(output, "\n", maximum_bytes);
}

auto emit_node(std::string& output, const storyboard_node_t& node, std::size_t indentation,
               std::size_t maximum_bytes, std::size_t depth, std::size_t& node_count) -> bool {
    if (!is_valid_emit_node(node, depth, node_count)) {
        return false;
    }
    ++node_count;
    if (node.kind == storyboard_node_kind_t::retained_line) {
        return is_valid_single_line(node.retained_line) &&
               append_bounded(output, node.retained_line, maximum_bytes) &&
               append_bounded(output, "\n", maximum_bytes);
    }
    if (node.kind == storyboard_node_kind_t::loop) {
        return emit_loop_node(output, node, indentation, maximum_bytes, depth, node_count);
    }
    return emit_command_node(output, node, indentation, maximum_bytes);
}

auto extent_of_nodes(const std::vector<storyboard_node_t>& nodes, std::size_t depth,
                     std::size_t maximum_depth, std::size_t maximum_nodes, std::size_t& node_count,
                     std::int64_t& extent) -> std::optional<storyboard_diagnostic_code_t>;

auto extent_of_node(const storyboard_node_t& node, std::size_t depth, std::size_t maximum_depth,
                    std::size_t maximum_nodes, std::size_t& node_count, std::int64_t& extent)
    -> std::optional<storyboard_diagnostic_code_t> {
    if (depth > maximum_depth)
        return storyboard_diagnostic_code_t::nesting_limit;
    if (node_count >= maximum_nodes)
        return storyboard_diagnostic_code_t::node_limit;
    ++node_count;
    extent = 0;
    if (node.kind == storyboard_node_kind_t::command) {
        if (!is_valid_opcode(node.command.opcode) ||
            node.command.value_count != expected_value_count(node.command.opcode) ||
            !is_valid_easing(node.command.easing) ||
            node.command.start_time_ms > node.command.end_time_ms) {
            return storyboard_diagnostic_code_t::invalid_model;
        }
        extent = std::max<std::int64_t>(0, node.command.end_time_ms);
        for (std::size_t index = 0; index < node.command.value_count; ++index) {
            if (!std::isfinite(node.command.from[index]) ||
                !std::isfinite(node.command.to[index])) {
                return storyboard_diagnostic_code_t::invalid_model;
            }
        }
        return std::nullopt;
    }
    if (node.kind == storyboard_node_kind_t::retained_line)
        return std::nullopt;

    std::int64_t period{};
    const auto error =
        extent_of_nodes(node.children, depth + 1, maximum_depth, maximum_nodes, node_count, period);
    if (error.has_value())
        return error;
    std::int64_t repeated{};
    std::int64_t child_extent{};
    if (!checked_mul_nonnegative(period, node.loop_iteration_count, repeated) ||
        !checked_add(node.loop_start_time_ms, repeated, child_extent)) {
        return storyboard_diagnostic_code_t::time_overflow;
    }
    extent = std::max<std::int64_t>(0, child_extent);
    return std::nullopt;
}

auto extent_of_nodes(const std::vector<storyboard_node_t>& nodes, std::size_t depth,
                     std::size_t maximum_depth, std::size_t maximum_nodes, std::size_t& node_count,
                     std::int64_t& extent) -> std::optional<storyboard_diagnostic_code_t> {
    if (depth > maximum_depth)
        return storyboard_diagnostic_code_t::nesting_limit;
    extent = 0;
    for (const auto& node : nodes) {
        std::int64_t node_extent{};
        const auto error =
            extent_of_node(node, depth, maximum_depth, maximum_nodes, node_count, node_extent);
        if (error.has_value())
            return error;
        extent = std::max(extent, node_extent);
    }
    return std::nullopt;
}

auto bounce_out(double value) noexcept -> double {
    constexpr double n = 7.5625;
    constexpr double d = 2.75;
    if (value < 1.0 / d) {
        return n * value * value;
    }
    if (value < 2.0 / d) {
        const auto shifted = value - 1.5 / d;
        return n * shifted * shifted + 0.75;
    }
    if (value < 2.5 / d) {
        const auto shifted = value - 2.25 / d;
        return n * shifted * shifted + 0.9375;
    }
    const auto shifted = value - 2.625 / d;
    return n * shifted * shifted + 0.984375;
}

auto ease(std::int32_t easing, double progress) noexcept -> double {
    const auto t = std::clamp(progress, 0.0, 1.0);
    if (t == 0.0 || t == 1.0)
        return t;
    constexpr double elastic_period = 0.3;
    constexpr double elastic_phase = elastic_period / 4.0;
    constexpr double elastic_frequency = 2.0 * std::numbers::pi / elastic_period;
    constexpr double expo_offset = 1.0 / 1024.0;
    constexpr double elastic_offset_full = 1.0 / 2048.0;
    const auto elastic_offset = [=](double period_scale) noexcept {
        return std::pow(2.0, -10.0) * std::sin((period_scale - elastic_phase) * elastic_frequency);
    };
    const auto elastic_in_out_offset =
        std::pow(2.0, -10.0) * std::sin((1.0 - elastic_phase * 1.5) * elastic_frequency / 1.5);
    const auto elastic_out = [=](double value) noexcept {
        return std::pow(2.0, -10.0 * value) *
                   std::sin((value - elastic_phase) * elastic_frequency) +
               1.0 - elastic_offset_full * value;
    };
    switch (easing) {
    case 0:
        return t; // linear
    case 1:
        return t * (2.0 - t); // quadratic out
    case 2:
        return t * t; // quadratic in
    case 3:
        return t * t; // quadratic in
    case 4:
        return 1.0 - (1.0 - t) * (1.0 - t); // quadratic out
    case 5:
        return t < 0.5 ? 2.0 * t * t : 1.0 - std::pow(-2.0 * t + 2.0, 2.0) / 2.0;
    case 6:
        return t * t * t;
    case 7:
        return 1.0 - std::pow(1.0 - t, 3.0);
    case 8:
        return t < 0.5 ? 4.0 * t * t * t : 1.0 - std::pow(-2.0 * t + 2.0, 3.0) / 2.0;
    case 9:
        return std::pow(t, 4.0);
    case 10:
        return 1.0 - std::pow(1.0 - t, 4.0);
    case 11:
        return t < 0.5 ? 8.0 * std::pow(t, 4.0) : 1.0 - std::pow(-2.0 * t + 2.0, 4.0) / 2.0;
    case 12:
        return std::pow(t, 5.0);
    case 13:
        return 1.0 - std::pow(1.0 - t, 5.0);
    case 14:
        return t < 0.5 ? 16.0 * std::pow(t, 5.0) : 1.0 - std::pow(-2.0 * t + 2.0, 5.0) / 2.0;
    case 15:
        return 1.0 - std::cos(t * std::numbers::pi / 2.0);
    case 16:
        return std::sin(t * std::numbers::pi / 2.0);
    case 17:
        return -(std::cos(std::numbers::pi * t) - 1.0) / 2.0;
    case 18:
        return std::pow(2.0, 10.0 * (t - 1.0)) + expo_offset * (t - 1.0);
    case 19:
        return 1.0 - std::pow(2.0, -10.0 * t) + expo_offset * t;
    case 20:
        return t < 0.5
                   ? 0.5 * (std::pow(2.0, 20.0 * t - 10.0) + expo_offset * (2.0 * t - 1.0))
                   : 1.0 - 0.5 * (std::pow(2.0, -20.0 * t + 10.0) + expo_offset * (-2.0 * t + 1.0));
    case 21:
        return 1.0 - std::sqrt(1.0 - t * t);
    case 22:
        return std::sqrt(1.0 - (t - 1.0) * (t - 1.0));
    case 23:
        return t < 0.5 ? (1.0 - std::sqrt(1.0 - std::pow(2.0 * t, 2.0))) / 2.0
                       : (std::sqrt(1.0 - std::pow(-2.0 * t + 2.0, 2.0)) + 1.0) / 2.0;
    case 24:
        return -std::pow(2.0, -10.0 + 10.0 * t) *
                   std::sin((1.0 - elastic_phase - t) * elastic_frequency) +
               elastic_offset_full * (1.0 - t);
    case 25:
        return elastic_out(t);
    case 26:
        return std::pow(2.0, -10.0 * t) * std::sin((0.5 * t - elastic_phase) * elastic_frequency) +
               1.0 - elastic_offset(0.5) * t;
    case 27:
        return std::pow(2.0, -10.0 * t) * std::sin((0.25 * t - elastic_phase) * elastic_frequency) +
               1.0 - elastic_offset(0.25) * t;
    case 28: {
        if (t < 0.5) {
            const auto doubled = 2.0 * t;
            return -0.5 *
                   (std::pow(2.0, -10.0 + 10.0 * doubled) *
                        std::sin((1.0 - elastic_phase * 1.5 - doubled) * elastic_frequency / 1.5) -
                    elastic_in_out_offset * (1.0 - doubled));
        }
        const auto doubled = 2.0 * t - 1.0;
        return 0.5 * (std::pow(2.0, -10.0 * doubled) *
                          std::sin((doubled - elastic_phase * 1.5) * elastic_frequency / 1.5) -
                      elastic_in_out_offset * doubled) +
               1.0;
    }
    case 29:
        return 2.70158 * t * t * t - 1.70158 * t * t;
    case 30:
        return 1.0 + 2.70158 * std::pow(t - 1.0, 3.0) + 1.70158 * std::pow(t - 1.0, 2.0);
    case 31:
        return t < 0.5 ? std::pow(2.0 * t, 2.0) *
                             ((1.70158 * 1.525 + 1.0) * 2.0 * t - 1.70158 * 1.525) / 2.0
                       : (std::pow(2.0 * t - 2.0, 2.0) *
                              ((1.70158 * 1.525 + 1.0) * (t * 2.0 - 2.0) + 1.70158 * 1.525) +
                          2.0) /
                             2.0;
    case 32:
        return 1.0 - bounce_out(1.0 - t);
    case 33:
        return bounce_out(t);
    case 34:
        return t < 0.5 ? (1.0 - bounce_out(1.0 - 2.0 * t)) / 2.0
                       : (1.0 + bounce_out(2.0 * t - 1.0)) / 2.0;
    default:
        return std::numeric_limits<double>::quiet_NaN();
    }
}

struct evaluated_candidate_t {
    storyboard_value_t value;
    std::int64_t start_time_ms{};
    std::uint64_t source_order{};
    std::uint64_t visit_order{};
    bool present{};
};

struct evaluation_context_t {
    storyboard_opcode_t opcode{};
    std::int64_t query_time_ms{};
    std::size_t maximum_nesting{};
    std::size_t maximum_nodes{250'000};
    std::uint64_t visit_order{};
    evaluated_candidate_t candidate;
    storyboard_diagnostic_t diagnostic;
    bool failed{};
};

void evaluate_nodes(const std::vector<storyboard_node_t>& nodes, std::int64_t parent_start_ms,
                    std::int64_t query_time_ms, std::size_t depth, evaluation_context_t& context);

void set_evaluation_error(evaluation_context_t& context, storyboard_diagnostic_code_t code,
                          std::string message) {
    context.failed = true;
    context.diagnostic = {code, 0, std::move(message)};
}

void evaluate_command_node(const storyboard_node_t& node, std::int64_t parent_start_ms,
                           std::int64_t query_time_ms, std::uint64_t visit_order,
                           evaluation_context_t& context) {
    const auto& command = node.command;
    if (command.opcode != context.opcode) {
        return;
    }
    std::int64_t start{};
    std::int64_t end{};
    if (!checked_add(parent_start_ms, command.start_time_ms, start) ||
        !checked_add(parent_start_ms, command.end_time_ms, end)) {
        set_evaluation_error(context, storyboard_diagnostic_code_t::time_overflow,
                             "storyboard command time overflowed");
        return;
    }
    if (start > query_time_ms) {
        return;
    }
    if (!is_valid_opcode(command.opcode) ||
        command.value_count != expected_value_count(command.opcode) ||
        !is_valid_easing(command.easing)) {
        set_evaluation_error(context, storyboard_diagnostic_code_t::invalid_model,
                             "storyboard command has invalid fields or easing");
        return;
    }
    auto progress = 1.0;
    if (end > start && query_time_ms < end) {
        progress = static_cast<double>(distance_i64(start, query_time_ms)) /
                   static_cast<double>(distance_i64(start, end));
    }
    progress = ease(command.easing, progress);
    storyboard_value_t value;
    value.present = true;
    value.value_count = command.value_count;
    for (std::size_t index = 0; index < command.value_count; ++index) {
        value.values[index] =
            command.from[index] + (command.to[index] - command.from[index]) * progress;
    }
    const auto source_order = command.source_order != 0 ? command.source_order : node.source_order;
    const auto candidate_key = std::tie(start, source_order, visit_order);
    const auto current_key =
        std::tie(context.candidate.start_time_ms, context.candidate.source_order,
                 context.candidate.visit_order);
    if (!context.candidate.present || candidate_key >= current_key) {
        context.candidate = {value, start, source_order, visit_order, true};
    }
}

struct loop_evaluation_window_t {
    std::int64_t iteration_start{};
    std::int64_t previous_start{};
    std::int64_t previous_end{};
    std::int64_t child_query{};
    std::uint64_t iteration{};
    std::int64_t period{};
};

auto loop_window(const storyboard_node_t& node, std::int64_t parent_start_ms,
                 std::int64_t query_time_ms, std::size_t depth, evaluation_context_t& context)
    -> std::optional<loop_evaluation_window_t> {
    std::int64_t loop_start{};
    if (!checked_add(parent_start_ms, node.loop_start_time_ms, loop_start)) {
        set_evaluation_error(context, storyboard_diagnostic_code_t::time_overflow,
                             "storyboard loop start overflowed");
        return std::nullopt;
    }
    if (node.loop_iteration_count == 0 || query_time_ms < loop_start) {
        return std::nullopt;
    }
    std::size_t count{};
    std::int64_t period{};
    const auto extent_error = extent_of_nodes(node.children, depth + 1, context.maximum_nesting,
                                              context.maximum_nodes, count, period);
    if (extent_error.has_value()) {
        set_evaluation_error(context, *extent_error, "could not determine storyboard loop duration");
        return std::nullopt;
    }
    auto loop_end = loop_start;
    std::uint64_t iteration = node.loop_iteration_count - 1;
    if (period > 0) {
        std::int64_t repeated{};
        if (!checked_mul_nonnegative(period, node.loop_iteration_count, repeated) ||
            !checked_add(loop_start, repeated, loop_end)) {
            set_evaluation_error(context, storyboard_diagnostic_code_t::time_overflow,
                                 "storyboard loop end overflowed");
            return std::nullopt;
        }
        if (query_time_ms < loop_end) {
            iteration = distance_i64(loop_start, query_time_ms) /
                        static_cast<std::uint64_t>(period);
        }
    }
    std::int64_t offset{};
    std::int64_t iteration_start{};
    if (!checked_mul_nonnegative(period, iteration, offset) ||
        !checked_add(loop_start, offset, iteration_start)) {
        set_evaluation_error(context, storyboard_diagnostic_code_t::time_overflow,
                             "storyboard loop iteration time overflowed");
        return std::nullopt;
    }
    loop_evaluation_window_t window{.iteration_start = iteration_start,
                                    .child_query = std::min(query_time_ms, loop_end),
                                    .iteration = iteration,
                                    .period = period};
    if (period > 0 && iteration > 0) {
        std::int64_t previous_offset{};
        if (!checked_mul_nonnegative(period, iteration - 1, previous_offset) ||
            !checked_add(loop_start, previous_offset, window.previous_start) ||
            !checked_add(window.previous_start, period, window.previous_end)) {
            set_evaluation_error(context, storyboard_diagnostic_code_t::time_overflow,
                                 "storyboard prior loop iteration time overflowed");
            return std::nullopt;
        }
    }
    return window;
}

void evaluate_loop_node(const storyboard_node_t& node, std::int64_t parent_start_ms,
                        std::int64_t query_time_ms, std::size_t depth,
                        evaluation_context_t& context) {
    const auto window = loop_window(node, parent_start_ms, query_time_ms, depth, context);
    if (!window.has_value()) {
        return;
    }
    if (window->period > 0 && window->iteration > 0) {
        evaluate_nodes(node.children, window->previous_start, window->previous_end, depth + 1,
                       context);
    }
    evaluate_nodes(node.children, window->iteration_start, window->child_query, depth + 1, context);
}

void evaluate_nodes(const std::vector<storyboard_node_t>& nodes, std::int64_t parent_start_ms,
                    std::int64_t query_time_ms, std::size_t depth, evaluation_context_t& context) {
    if (context.failed) {
        return;
    }
    if (depth > context.maximum_nesting) {
        set_evaluation_error(context, storyboard_diagnostic_code_t::nesting_limit,
                             "storyboard evaluation exceeded its nesting limit");
        return;
    }
    for (const auto& node : nodes) {
        if (context.visit_order >= context.maximum_nodes) {
            set_evaluation_error(context, storyboard_diagnostic_code_t::node_limit,
                                 "storyboard evaluation exceeded its node limit");
            return;
        }
        const auto visit_order = context.visit_order++;
        if (node.kind == storyboard_node_kind_t::retained_line) {
            continue;
        }
        if (node.kind == storyboard_node_kind_t::command) {
            evaluate_command_node(node, parent_start_ms, query_time_ms, visit_order, context);
        } else {
            evaluate_loop_node(node, parent_start_ms, query_time_ms, depth, context);
        }
        if (context.failed) {
            return;
        }
    }
}

auto semantic_nodes_equal(const std::vector<storyboard_node_t>& left,
                          const std::vector<storyboard_node_t>& right) -> bool;

auto semantic_node_equal(const storyboard_node_t& left, const storyboard_node_t& right) -> bool {
    if (left.kind != right.kind)
        return false;
    if (left.kind == storyboard_node_kind_t::retained_line)
        return left.retained_line == right.retained_line;
    if (left.kind == storyboard_node_kind_t::command) {
        const auto& a = left.command;
        const auto& b = right.command;
        return a.opcode == b.opcode && a.easing == b.easing && a.start_time_ms == b.start_time_ms &&
               a.end_time_ms == b.end_time_ms && a.from == b.from && a.to == b.to &&
               a.value_count == b.value_count;
    }
    return left.loop_start_time_ms == right.loop_start_time_ms &&
           left.loop_iteration_count == right.loop_iteration_count &&
           semantic_nodes_equal(left.children, right.children);
}

auto semantic_nodes_equal(const std::vector<storyboard_node_t>& left,
                          const std::vector<storyboard_node_t>& right) -> bool {
    auto left_index = std::size_t{};
    auto right_index = std::size_t{};
    for (;;) {
        while (left_index < left.size() &&
               left[left_index].kind == storyboard_node_kind_t::retained_line &&
               trim(left[left_index].retained_line).empty()) {
            ++left_index;
        }
        while (right_index < right.size() &&
               right[right_index].kind == storyboard_node_kind_t::retained_line &&
               trim(right[right_index].retained_line).empty()) {
            ++right_index;
        }
        if (left_index == left.size() || right_index == right.size())
            return left_index == left.size() && right_index == right.size();
        if (!semantic_node_equal(left[left_index], right[right_index]))
            return false;
        ++left_index;
        ++right_index;
    }
}

auto semantic_top_lines_equal(const std::vector<storyboard_top_level_line_t>& left,
                              const std::vector<storyboard_top_level_line_t>& right) -> bool {
    auto left_index = std::size_t{};
    auto right_index = std::size_t{};
    for (;;) {
        while (left_index < left.size() && trim(left[left_index].text).empty())
            ++left_index;
        while (right_index < right.size() && trim(right[right_index].text).empty())
            ++right_index;
        if (left_index == left.size() || right_index == right.size())
            return left_index == left.size() && right_index == right.size();
        if (left[left_index].text != right[right_index].text)
            return false;
        ++left_index;
        ++right_index;
    }
}

class osb_parser_t {
  public:
    explicit osb_parser_t(storyboard_parse_options_t options) : options_(options) {}

    auto parse(std::string_view text) -> storyboard_parse_result_t {
        if (text.size() > options_.maximum_input_bytes) {
            fail(storyboard_diagnostic_code_t::input_too_large,
                 "OSB input exceeds the configured byte limit");
            return std::move(result_);
        }
        try {
            std::size_t cursor{};
            while (cursor < text.size()) {
                const auto newline = text.find('\n', cursor);
                const auto end = newline == std::string_view::npos ? text.size() : newline;
                auto line = text.substr(cursor, end - cursor);
                if (!line.empty() && line.back() == '\r') {
                    line.remove_suffix(1);
                }
                cursor = newline == std::string_view::npos ? text.size() : newline + 1;
                if (!process_line(line)) {
                    return std::move(result_);
                }
            }
            result_.document = std::move(document_);
        } catch (const std::bad_alloc&) {
            result_.document.reset();
            try {
                fail(storyboard_diagnostic_code_t::capacity_exceeded,
                     "OSB parser allocation failed");
            } catch (...) {
            }
        }
        return std::move(result_);
    }

  private:
    void fail(storyboard_diagnostic_code_t code, std::string message) {
        result_.diagnostics.push_back({code, line_number_, std::move(message)});
    }

    auto process_line(std::string_view line) -> bool {
        ++line_number_;
        if (line_number_ > options_.maximum_lines) {
            fail(storyboard_diagnostic_code_t::line_limit,
                 "OSB input exceeds the configured line limit");
            return false;
        }
        if (source_order_ == std::numeric_limits<std::uint64_t>::max()) {
            fail(storyboard_diagnostic_code_t::invalid_model, "OSB source order overflowed");
            return false;
        }
        ++source_order_;
        std::size_t indentation{};
        while (indentation < line.size() && (line[indentation] == ' ' || line[indentation] == '\t')) {
            ++indentation;
        }
        const auto content = trim(line);
        if (content.empty()) {
            return retain_blank_line(line);
        }
        const auto fields = split_csv(content);
        if (indentation == 0 && fields.front() == "Sprite") {
            return parse_sprite(fields);
        }
        if (indentation == 0) {
            active_sprite_.reset();
            loop_path_.clear();
            loop_indentation_.clear();
            document_.retained_top_level_lines.push_back({source_order_, std::string(line)});
            return true;
        }
        if (!active_sprite_) {
            document_.retained_top_level_lines.push_back({source_order_, std::string(line)});
            return true;
        }
        return parse_sprite_content(line, indentation, fields);
    }

    auto retain_blank_line(std::string_view line) -> bool {
        if (!active_sprite_) {
            document_.retained_top_level_lines.push_back({source_order_, std::string(line)});
            return true;
        }
        auto* destination = current_destination();
        storyboard_node_t node;
        node.kind = storyboard_node_kind_t::retained_line;
        node.source_order = source_order_;
        node.retained_line = std::string(line);
        std::vector<storyboard_node_t> nodes;
        nodes.push_back(std::move(node));
        return insert_nodes(*destination, std::move(nodes), 0);
    }

    auto parse_sprite(const std::vector<std::string>& fields) -> bool {
        if (fields.size() != 6 || fields[1].empty() || fields[2].empty() ||
            fields[3].size() > options_.maximum_path_bytes || fields[3].empty()) {
            fail(storyboard_diagnostic_code_t::malformed_line,
                 "OSB Sprite header must contain layer, origin, path, and position");
            return false;
        }
        storyboard_sprite_t sprite;
        sprite.stable_id = source_order_;
        sprite.source_order = source_order_;
        sprite.layer = fields[1];
        sprite.origin = fields[2];
        sprite.path = fields[3];
        if (!parse_double(fields[4], sprite.x) || !parse_double(fields[5], sprite.y)) {
            fail(storyboard_diagnostic_code_t::invalid_number,
                 "OSB Sprite position is not a finite number");
            return false;
        }
        document_.sprites.push_back(std::move(sprite));
        active_sprite_ = document_.sprites.size() - 1;
        loop_path_.clear();
        loop_indentation_.clear();
        return true;
    }

    auto current_destination() -> std::vector<storyboard_node_t>* {
        auto* nodes = &document_.sprites[*active_sprite_].nodes;
        for (const auto index : loop_path_) {
            nodes = &(*nodes)[index].children;
        }
        return nodes;
    }

    auto prepare_destination(std::size_t indentation)
        -> std::optional<std::vector<storyboard_node_t>*> {
        if (indentation > 0 && indentation - 1 > options_.maximum_nesting) {
            fail(storyboard_diagnostic_code_t::nesting_limit,
                 "OSB indentation exceeds the configured nesting limit");
            return std::nullopt;
        }
        while (!loop_indentation_.empty() && loop_indentation_.back() >= indentation) {
            loop_indentation_.pop_back();
            loop_path_.pop_back();
        }
        if (loop_path_.size() > options_.maximum_nesting) {
            fail(storyboard_diagnostic_code_t::nesting_limit,
                 "OSB loop nesting exceeds the configured nesting limit");
            return std::nullopt;
        }
        return current_destination();
    }

    auto parse_sprite_content(std::string_view line, std::size_t indentation,
                              const std::vector<std::string>& fields) -> bool {
        const auto destination = prepare_destination(indentation);
        if (!destination.has_value()) {
            return false;
        }
        auto nodes = parse_nodes(fields, line);
        if (!nodes.has_value()) {
            return false;
        }
        return insert_nodes(**destination, std::move(*nodes), indentation);
    }

    auto parse_nodes(const std::vector<std::string>& fields, std::string_view line)
        -> std::optional<std::vector<storyboard_node_t>> {
        storyboard_node_t node;
        node.source_order = source_order_;
        if (fields.front() == "L") {
            if (fields.size() == 3 && parse_integer(fields[1], node.loop_start_time_ms) &&
                parse_integer(fields[2], node.loop_iteration_count)) {
                node.kind = storyboard_node_kind_t::loop;
                return std::vector<storyboard_node_t>{std::move(node)};
            }
            return malformed_command();
        }
        const auto opcode = parse_opcode(fields.front());
        if (!opcode.has_value()) {
            node.kind = storyboard_node_kind_t::retained_line;
            node.retained_line = std::string(line);
            return std::vector<storyboard_node_t>{std::move(node)};
        }
        node.kind = storyboard_node_kind_t::command;
        node.command.opcode = *opcode;
        node.command.value_count = expected_value_count(*opcode);
        node.command.source_order = source_order_;
        std::vector<storyboard_node_t> expanded;
        const auto diagnostics_before = result_.diagnostics.size();
        if (!parse_command_fields(fields, node, expanded)) {
            if (result_.diagnostics.size() == diagnostics_before) {
                return malformed_command();
            }
            return std::nullopt;
        }
        if (expanded.size() > 1) {
            return expanded;
        }
        if (expanded.size() == 1) {
            return expanded;
        }
        return std::vector<storyboard_node_t>{std::move(node)};
    }

    auto malformed_command() -> std::optional<std::vector<storyboard_node_t>> {
        fail(storyboard_diagnostic_code_t::malformed_line,
             "recognized OSB command has invalid fields");
        return std::nullopt;
    }

    auto parse_command_fields(const std::vector<std::string>& fields, storyboard_node_t& node,
                              std::vector<storyboard_node_t>& expanded) -> bool {
        auto& command = node.command;
        const auto values_per_group = static_cast<std::size_t>(command.value_count);
        if (fields.size() < 4 + values_per_group || !parse_integer(fields[1], command.easing) ||
            !parse_integer(fields[2], command.start_time_ms)) {
            return false;
        }
        if (!is_valid_easing(command.easing)) {
            fail(storyboard_diagnostic_code_t::invalid_command,
                 "OSB easing value must be between 0 and 34");
            return false;
        }
        if (fields[3].empty()) {
            command.end_time_ms = command.start_time_ms;
        } else if (!parse_integer(fields[3], command.end_time_ms)) {
            return false;
        }
        if (command.start_time_ms > command.end_time_ms) {
            return false;
        }
        const auto parameter_count = fields.size() - 4;
        if (parameter_count < values_per_group || parameter_count % values_per_group != 0) {
            return false;
        }
        std::vector<double> values(parameter_count);
        for (std::size_t index = 0; index < parameter_count; ++index) {
            if (!parse_double(fields[4 + index], values[index])) {
                return false;
            }
        }
        return populate_command_groups(command, values, expanded);
    }

    auto populate_command_groups(storyboard_command_t& command, const std::vector<double>& values,
                                 std::vector<storyboard_node_t>& expanded) -> bool {
        const auto per_group = static_cast<std::size_t>(command.value_count);
        const auto group_count = values.size() / per_group;
        if (group_count <= 2) {
            command.from.fill(0.0);
            command.to.fill(0.0);
            for (std::size_t index = 0; index < per_group; ++index) {
                command.from[index] = values[index];
                command.to[index] = group_count == 1 ? values[index] : values[per_group + index];
            }
            return true;
        }
        return expand_sequential_groups(command, values, group_count, expanded);
    }

    auto expand_sequential_groups(const storyboard_command_t& command,
                                  const std::vector<double>& values, std::size_t group_count,
                                  std::vector<storyboard_node_t>& expanded) -> bool {
        const auto duration = distance_i64(command.start_time_ms, command.end_time_ms);
        const auto per_group = static_cast<std::size_t>(command.value_count);
        expanded.reserve(group_count - 1);
        for (std::size_t group = 0; group + 1 < group_count; ++group) {
            if (group != 0 && duration > std::numeric_limits<std::uint64_t>::max() / group) {
                fail(storyboard_diagnostic_code_t::time_overflow,
                     "OSB sequential command time overflowed");
                return false;
            }
            const auto offset = duration * static_cast<std::uint64_t>(group);
            storyboard_node_t item;
            item.kind = storyboard_node_kind_t::command;
            item.source_order = source_order_;
            item.command = command;
            if (!checked_add_unsigned_i64(command.start_time_ms, offset,
                                          item.command.start_time_ms) ||
                !checked_add_unsigned_i64(command.end_time_ms, offset,
                                          item.command.end_time_ms)) {
                fail(storyboard_diagnostic_code_t::time_overflow,
                     "OSB sequential command time overflowed");
                return false;
            }
            for (std::size_t index = 0; index < per_group; ++index) {
                item.command.from[index] = values[group * per_group + index];
                item.command.to[index] = values[(group + 1) * per_group + index];
            }
            expanded.push_back(std::move(item));
        }
        return true;
    }

    auto insert_nodes(std::vector<storyboard_node_t>& destination,
                      std::vector<storyboard_node_t> nodes, std::size_t indentation) -> bool {
        if (nodes.size() > 1) {
            const auto remaining = options_.maximum_nodes -
                                   std::min(options_.maximum_nodes, node_count_);
            if (nodes.size() > remaining) {
                fail(storyboard_diagnostic_code_t::node_limit,
                     "OSB input exceeds the configured node limit");
                return false;
            }
            node_count_ += nodes.size();
            for (auto& node : nodes) {
                destination.push_back(std::move(node));
            }
            return true;
        }
        auto& node = nodes.front();
        if (node.kind == storyboard_node_kind_t::loop &&
            loop_path_.size() >= options_.maximum_nesting) {
            fail(storyboard_diagnostic_code_t::nesting_limit,
                 "OSB loop nesting exceeds the configured nesting limit");
            return false;
        }
        if (++node_count_ > options_.maximum_nodes) {
            fail(storyboard_diagnostic_code_t::node_limit,
                 "OSB input exceeds the configured node limit");
            return false;
        }
        const auto inserted_index = destination.size();
        destination.push_back(std::move(node));
        if (destination.back().kind == storyboard_node_kind_t::loop) {
            loop_path_.push_back(inserted_index);
            loop_indentation_.push_back(indentation);
        }
        return true;
    }

    storyboard_parse_options_t options_;
    storyboard_parse_result_t result_;
    storyboard_document_model_t document_;
    std::vector<std::size_t> loop_path_;
    std::vector<std::size_t> loop_indentation_;
    std::optional<std::size_t> active_sprite_;
    std::size_t line_number_{};
    std::size_t node_count_{};
    std::uint64_t source_order_{};
};

} // namespace

auto parse_osb(std::string_view text, storyboard_parse_options_t options)
    -> storyboard_parse_result_t {
    return osb_parser_t{options}.parse(text);
}

namespace {

struct osb_top_item_t {
    std::uint64_t order{};
    bool sprite{};
    std::size_t index{};
};

auto make_osb_top_items(const storyboard_document_model_t& document,
                        storyboard_emit_result_t& result) -> std::optional<std::vector<osb_top_item_t>> {
    if (document.sprites.size() > std::numeric_limits<std::size_t>::max() -
                                      document.retained_top_level_lines.size()) {
        result.diagnostics.push_back({storyboard_diagnostic_code_t::capacity_exceeded, 0,
                                      "OSB top-level item count overflowed"});
        return std::nullopt;
    }
    const auto count = document.sprites.size() + document.retained_top_level_lines.size();
    if (count > 250'000) {
        result.diagnostics.push_back({storyboard_diagnostic_code_t::node_limit, 0,
                                      "OSB document exceeds the emitter node limit"});
        return std::nullopt;
    }
    std::vector<osb_top_item_t> items;
    try {
        items.reserve(count);
        for (std::size_t index = 0; index < document.sprites.size(); ++index) {
            items.push_back({document.sprites[index].source_order, true, index});
        }
        for (std::size_t index = 0; index < document.retained_top_level_lines.size(); ++index) {
            items.push_back({document.retained_top_level_lines[index].source_order, false, index});
        }
    } catch (const std::bad_alloc&) {
        result.diagnostics.push_back({storyboard_diagnostic_code_t::capacity_exceeded, 0,
                                      "OSB ordering allocation failed"});
        return std::nullopt;
    }
    std::ranges::sort(items, [](const auto& left, const auto& right) {
        return std::tie(left.order, left.sprite, left.index) <
               std::tie(right.order, right.sprite, right.index);
    });
    return items;
}

auto append_osb_sprite_header(std::string& output, const storyboard_sprite_t& sprite,
                              std::size_t maximum_bytes,
                              storyboard_emit_result_t& result) -> bool {
    if (sprite.layer.empty() || sprite.origin.empty() || sprite.path.empty() ||
        sprite.path.size() > storyboard_default_maximum_path_bytes ||
        !is_valid_header_token(sprite.layer) || !is_valid_header_token(sprite.origin) ||
        !is_valid_single_line(sprite.path) || !std::isfinite(sprite.x) || !std::isfinite(sprite.y)) {
        result.diagnostics.push_back({storyboard_diagnostic_code_t::invalid_model, 0,
                                      "OSB sprite has an invalid header"});
        return false;
    }
    if (!append_bounded(output, "Sprite,", maximum_bytes) ||
        !append_bounded(output, sprite.layer, maximum_bytes) ||
        !append_bounded(output, ",", maximum_bytes) ||
        !append_bounded(output, sprite.origin, maximum_bytes) ||
        !append_bounded(output, ",\"", maximum_bytes)) {
        return false;
    }
    for (const auto character : sprite.path) {
        const auto piece = character == '"' ? std::string_view{"\"\""}
                                             : std::string_view{&character, 1};
        if (!append_bounded(output, piece, maximum_bytes)) {
            return false;
        }
    }
    return append_bounded(output, "\",", maximum_bytes) && append_double(output, sprite.x) &&
           append_bounded(output, ",", maximum_bytes) && append_double(output, sprite.y) &&
           append_bounded(output, "\n", maximum_bytes);
}

auto emit_osb_top_item(std::string& output, const storyboard_document_model_t& document,
                       const osb_top_item_t& item, std::size_t maximum_bytes,
                       std::size_t& node_count, storyboard_emit_result_t& result) -> bool {
    if (!item.sprite) {
        const auto& line = document.retained_top_level_lines[item.index].text;
        if (is_valid_single_line(line) && append_bounded(output, line, maximum_bytes) &&
            append_bounded(output, "\n", maximum_bytes)) {
            return true;
        }
        result.diagnostics.push_back({storyboard_diagnostic_code_t::capacity_exceeded, 0,
                                      "OSB output exceeds its configured byte limit"});
        return false;
    }
    const auto& sprite = document.sprites[item.index];
    if (!append_osb_sprite_header(output, sprite, maximum_bytes, result)) {
        if (result.diagnostics.empty() ||
            result.diagnostics.back().code != storyboard_diagnostic_code_t::invalid_model) {
            result.diagnostics.push_back({storyboard_diagnostic_code_t::capacity_exceeded, 0,
                                          "OSB output exceeds its configured byte limit"});
        }
        return false;
    }
    for (const auto& node : sprite.nodes) {
        if (!emit_node(output, node, 1, maximum_bytes, 1, node_count)) {
            result.diagnostics.push_back({storyboard_diagnostic_code_t::invalid_model, 0,
                                         "OSB node is invalid or output exceeds its limit"});
            return false;
        }
    }
    return true;
}

} // namespace

auto emit_osb(const storyboard_document_model_t& document, std::size_t maximum_output_bytes)
    -> storyboard_emit_result_t {
    storyboard_emit_result_t result;
    const auto items = make_osb_top_items(document, result);
    if (!items.has_value()) {
        return result;
    }
    std::string output;
    std::size_t node_count{};
    try {
        output.reserve(std::min(maximum_output_bytes, std::size_t{64 * 1024}));
        for (const auto& item : *items) {
            if (!emit_osb_top_item(output, document, item, maximum_output_bytes, node_count,
                                   result)) {
                return result;
            }
        }
    } catch (const std::bad_alloc&) {
        result.diagnostics.push_back({storyboard_diagnostic_code_t::capacity_exceeded, 0,
                                      "OSB output allocation failed"});
        return result;
    }
    result.text = std::move(output);
    return result;
}

namespace {

auto reserve_schedule_events(const storyboard_document_model_t& document,
                             storyboard_schedule_options_t options,
                             storyboard_schedule_result_t& result)
    -> std::optional<std::size_t> {
    std::size_t count{};
    for (const auto& sprite : document.sprites) {
        for (const auto& node : sprite.nodes) {
            if (node.kind == storyboard_node_kind_t::retained_line) {
                continue;
            }
            if (count > options.maximum_events || options.maximum_events - count < 2) {
                result.diagnostics.push_back({storyboard_diagnostic_code_t::capacity_exceeded, 0,
                                              "storyboard schedule exceeds its event limit"});
                return std::nullopt;
            }
            count += 2;
        }
    }
    return count;
}

auto schedule_node_times(const storyboard_node_t& node, storyboard_schedule_options_t options,
                         storyboard_schedule_result_t& result)
    -> std::optional<std::pair<std::int64_t, std::int64_t>> {
    if (node.kind == storyboard_node_kind_t::command) {
        if (!is_valid_opcode(node.command.opcode) ||
            node.command.value_count != expected_value_count(node.command.opcode) ||
            node.command.start_time_ms > node.command.end_time_ms) {
            result.diagnostics.push_back({storyboard_diagnostic_code_t::invalid_model, 0,
                                          "storyboard schedule contains an invalid command"});
            return std::nullopt;
        }
        return std::pair{node.command.start_time_ms, node.command.end_time_ms};
    }
    std::int64_t repeated{};
    std::size_t nested_count{};
    std::int64_t period{};
    const auto error = extent_of_nodes(node.children, 1, options.maximum_nesting,
                                       options.maximum_nodes, nested_count, period);
    std::int64_t end_time{};
    if (error.has_value() ||
        !checked_mul_nonnegative(period, node.loop_iteration_count, repeated) ||
        !checked_add(node.loop_start_time_ms, repeated, end_time)) {
        result.diagnostics.push_back(
            {error.value_or(storyboard_diagnostic_code_t::time_overflow), 0,
             "storyboard loop duration overflowed"});
        return std::nullopt;
    }
    return std::pair{node.loop_start_time_ms, end_time};
}

auto append_schedule_node(const storyboard_sprite_t& sprite, const storyboard_node_t& node,
                          storyboard_schedule_options_t options, storyboard_schedule_t& schedule,
                          std::size_t& node_count, std::uint64_t& stable_order,
                          storyboard_schedule_result_t& result) -> bool {
    if (node_count >= options.maximum_nodes) {
        result.diagnostics.push_back({storyboard_diagnostic_code_t::node_limit, 0,
                                      "storyboard schedule exceeds its node limit"});
        return false;
    }
    std::size_t nested_count{};
    std::int64_t ignored_extent{};
    const auto error = extent_of_node(node, 1, options.maximum_nesting, options.maximum_nodes,
                                      nested_count, ignored_extent);
    if (error.has_value()) {
        result.diagnostics.push_back({*error, 0,
                                      "storyboard schedule contains an invalid or too-deep node"});
        return false;
    }
    if (nested_count > options.maximum_nodes - node_count) {
        result.diagnostics.push_back({storyboard_diagnostic_code_t::node_limit, 0,
                                      "storyboard schedule exceeds its node limit"});
        return false;
    }
    node_count += nested_count;
    if (stable_order > std::numeric_limits<std::uint64_t>::max() - 2) {
        result.diagnostics.push_back({storyboard_diagnostic_code_t::invalid_model, 0,
                                      "storyboard schedule order overflowed"});
        return false;
    }
    const auto times = schedule_node_times(node, options, result);
    if (!times.has_value()) {
        return false;
    }
    const auto node_order = node.source_order;
    schedule.events.push_back({times->first, node_order, sprite.stable_id,
                               node_order != 0 ? node_order : stable_order,
                               storyboard_event_t::edge_t::begin, stable_order++});
    schedule.events.push_back({times->second, node_order, sprite.stable_id,
                               node_order != 0 ? node_order : stable_order,
                               storyboard_event_t::edge_t::end, stable_order++});
    return true;
}

auto schedule_event_less(const storyboard_event_t& left, const storyboard_event_t& right) -> bool {
    return std::tie(left.time_ms, left.source_order, left.stable_object_id, left.stable_node_id,
                    left.edge, left.schedule_order) <
           std::tie(right.time_ms, right.source_order, right.stable_object_id,
                    right.stable_node_id, right.edge, right.schedule_order);
}

} // namespace

auto compile_storyboard_schedule(const storyboard_document_model_t& document,
                                 storyboard_schedule_options_t options)
    -> storyboard_schedule_result_t {
    storyboard_schedule_result_t result;
    storyboard_schedule_t candidate;
    const auto expected_events = reserve_schedule_events(document, options, result);
    if (!expected_events.has_value()) {
        return result;
    }
    try {
        candidate.events.reserve(*expected_events);
    } catch (const std::bad_alloc&) {
        result.diagnostics.push_back({storyboard_diagnostic_code_t::capacity_exceeded, 0,
                                      "storyboard schedule allocation failed"});
        return result;
    }
    std::size_t node_count{};
    std::uint64_t stable_order{};
    for (const auto& sprite : document.sprites) {
        for (const auto& node : sprite.nodes) {
            if (node.kind != storyboard_node_kind_t::retained_line &&
                !append_schedule_node(sprite, node, options, candidate, node_count, stable_order,
                                      result)) {
                return result;
            }
        }
    }
    std::ranges::sort(candidate.events, schedule_event_less);
    result.schedule = std::move(candidate);
    return result;
}

auto storyboard_event_cursor_t::start(const storyboard_schedule_t& schedule, std::int64_t time_ms)
    -> storyboard_cursor_status_t {
    if (!std::ranges::is_sorted(schedule.events, [](const auto& left, const auto& right) {
            return std::tie(left.time_ms, left.source_order, left.stable_object_id,
                            left.stable_node_id, left.edge, left.schedule_order) <
                   std::tie(right.time_ms, right.source_order, right.stable_object_id,
                            right.stable_node_id, right.edge, right.schedule_order);
        })) {
        schedule_ = nullptr;
        active_ = false;
        return storyboard_cursor_status_t::invalid_schedule;
    }
    schedule_ = &schedule;
    active_ = true;
    return seek(time_ms);
}

auto storyboard_event_cursor_t::seek(std::int64_t time_ms) -> storyboard_cursor_status_t {
    if (!active_ || schedule_ == nullptr)
        return storyboard_cursor_status_t::not_started;
    const auto first =
        std::ranges::lower_bound(schedule_->events, time_ms, {}, &storyboard_event_t::time_ms);
    head_ = static_cast<std::size_t>(std::distance(schedule_->events.begin(), first));
    last_end_time_ms_ = time_ms;
    return storyboard_cursor_status_t::ready;
}

auto storyboard_event_cursor_t::advance(std::int64_t exclusive_end_time_ms) noexcept
    -> storyboard_cursor_result_t {
    if (!active_ || schedule_ == nullptr)
        return {};
    if (exclusive_end_time_ms < last_end_time_ms_) {
        return {.status = storyboard_cursor_status_t::non_monotonic_window};
    }
    const auto begin = head_;
    while (head_ < schedule_->events.size() &&
           schedule_->events[head_].time_ms < exclusive_end_time_ms) {
        ++head_;
    }
    last_end_time_ms_ = exclusive_end_time_ms;
    return {
        .status = storyboard_cursor_status_t::ready,
        .events =
            std::span<const storyboard_event_t>{schedule_->events}.subspan(begin, head_ - begin),
    };
}

void storyboard_event_cursor_t::stop() noexcept {
    schedule_ = nullptr;
    head_ = 0;
    last_end_time_ms_ = 0;
    active_ = false;
}

auto evaluate_storyboard_property(const storyboard_sprite_t& sprite, storyboard_opcode_t opcode,
                                  std::int64_t time_ms, std::size_t maximum_nesting)
    -> storyboard_evaluation_result_t {
    evaluation_context_t context;
    context.opcode = opcode;
    context.query_time_ms = time_ms;
    context.maximum_nesting = maximum_nesting;
    evaluate_nodes(sprite.nodes, 0, time_ms, 0, context);
    return {
        .value = context.candidate.value,
        .diagnostic = std::move(context.diagnostic),
        .start_time_ms = context.candidate.start_time_ms,
        .source_order = context.candidate.source_order,
        .visit_order = context.candidate.visit_order,
        .failed = context.failed,
    };
}

auto storyboard_semantically_equal(const storyboard_document_model_t& left,
                                   const storyboard_document_model_t& right) -> bool {
    if (left.sprites.size() != right.sprites.size() ||
        !semantic_top_lines_equal(left.retained_top_level_lines, right.retained_top_level_lines)) {
        return false;
    }
    for (std::size_t index = 0; index < left.sprites.size(); ++index) {
        const auto& a = left.sprites[index];
        const auto& b = right.sprites[index];
        if (a.layer != b.layer || a.origin != b.origin || a.path != b.path || a.x != b.x ||
            a.y != b.y || !semantic_nodes_equal(a.nodes, b.nodes)) {
            return false;
        }
    }
    return true;
}

} // namespace wormedit
