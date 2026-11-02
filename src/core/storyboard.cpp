#include "checked_math.h"
#include "osu_line_fields.h"
#include "text_util.h"
#include "wormedit/core/numeric_text.h"
#include <wormedit/core/storyboard.h>

#include <algorithm>
#include <charconv>
#include <cstring>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <numbers>
#include <numeric>
#include <string>
#include <tuple>
#include <utility>
#include <unordered_set>

namespace wormedit {

namespace {
auto consume_trigger_token(std::string_view& text, std::span<const std::string_view> tokens) noexcept -> bool {
    for (const auto token : tokens) {
        if (!text.starts_with(token)) continue;
        text.remove_prefix(token.size());
        return true;
    }
    return false;
}
auto is_hit_sound_suffix(std::string_view suffix) noexcept -> bool {
    constexpr std::array sets{std::string_view{"All"}, std::string_view{"Normal"},
        std::string_view{"Soft"}, std::string_view{"Drum"}};
    constexpr std::array additions{std::string_view{"Whistle"}, std::string_view{"Finish"}, std::string_view{"Clap"}};
    (void)consume_trigger_token(suffix, sets);
    (void)consume_trigger_token(suffix, sets);
    (void)consume_trigger_token(suffix, additions);
    if (suffix.empty()) return true;
    if (suffix.size() > 10 || suffix.front() < '0' || suffix.front() > '9') return false;
    std::int32_t custom{};
    const auto [end, error] = std::from_chars(suffix.data(), suffix.data() + suffix.size(), custom);
    return error == std::errc{} && end == suffix.data() + suffix.size();
}
} // namespace

auto storyboard_trigger_name_t::kind() const noexcept -> std::optional<storyboard_trigger_kind_t> {
    if (value == "Passing") return storyboard_trigger_kind_t::passing;
    if (value == "Failing") return storyboard_trigger_kind_t::failing;
    constexpr std::string_view prefix = "HitSound";
    if (std::string_view(value).starts_with(prefix) && is_hit_sound_suffix(std::string_view(value).substr(prefix.size())))
        return storyboard_trigger_kind_t::hit_sound;
    return std::nullopt;
}
auto storyboard_trigger_name_t::from(std::string_view text) -> std::optional<storyboard_trigger_name_t> {
    if (text.size() > 37) return std::nullopt;
    storyboard_trigger_name_t result{.value = std::string(text)};
    if (!result.is_valid()) return std::nullopt;
    return result;
}
auto storyboard_trigger_t::is_valid() const noexcept -> bool {
    return name.is_valid() && (!end_time_ms || start_time_ms) && (!group_number || end_time_ms) &&
        (!start_time_ms || !end_time_ms || *start_time_ms <= *end_time_ms);
}

auto storyboard_trigger_scenario_t::from(std::span<const storyboard_trigger_firing_t> firings,
    std::size_t maximum_firings) -> result_t<storyboard_trigger_scenario_t, failure_code_t> {
    if (firings.size() > maximum_firings || firings.size() > (64U * 1024U * 1024U) / (2 * (sizeof(storyboard_trigger_firing_t) + 37)))
        return std::unexpected(failure_code_t::capacity_exceeded);
    for (const auto& firing : firings)
        if (!firing.name.is_valid() || !firing.time.is_valid()) return std::unexpected(failure_code_t::invalid_request);
    try {
        storyboard_trigger_scenario_t result;
        result.firings_.assign(firings.begin(), firings.end());
        std::stable_sort(result.firings_.begin(), result.firings_.end(), [](const auto& a, const auto& b) {
            return a.name.value != b.name.value ? a.name.value < b.name.value : storyboard_time_t::is_before(a.time, b.time);
        });
        return result;
    } catch (const std::bad_alloc&) { return std::unexpected(failure_code_t::capacity_exceeded); }
}
auto storyboard_trigger_scenario_t::latest(const storyboard_trigger_name_t& name,
    std::optional<storyboard_time_t> begin, std::optional<storyboard_time_t> end,
    storyboard_time_t query) const noexcept -> std::optional<storyboard_time_t> {
    if (!query.is_valid()) return {};
    if (end && storyboard_time_t::is_before(*end, query)) query = *end;
    const auto first = std::lower_bound(firings_.begin(), firings_.end(), name.value,
        [](const auto& event, const auto& key) { return event.name.value < key; });
    const auto last = std::upper_bound(first, firings_.end(), name.value,
        [](const auto& key, const auto& event) { return key < event.name.value; });
    const auto upper = std::upper_bound(first, last, query,
        [](auto time, const auto& event) { return storyboard_time_t::is_before(time, event.time); });
    if (upper == first) return {};
    const auto time = std::prev(upper)->time;
    if (begin && storyboard_time_t::is_before(time, *begin)) return {};
    return time;
}

auto storyboard_sample_t::is_valid_payload(std::string_view path, std::optional<std::uint8_t> volume,
    std::size_t maximum_path_bytes) noexcept -> bool {
    return !path.empty() && path.size() <= maximum_path_bytes && osu_line::is_writable_filename(path) &&
        (!volume || (*volume >= 1 && *volume <= 100));
}
auto storyboard_sample_t::is_valid(std::size_t maximum_path_bytes) const noexcept -> bool {
    return stable_id != 0 && static_cast<unsigned>(layer) <= static_cast<unsigned>(osbx_layer_t::foreground) &&
        is_valid_payload(path, volume, maximum_path_bytes);
}
auto storyboard_document_model_t::has_valid_samples(std::size_t maximum_samples) const -> bool {
    if (samples.empty()) return true;
    if (samples.size() > maximum_samples || sprites.size() > 250'000) return false;
    try {
        std::unordered_set<std::uint64_t> identifiers;
        for (const auto& sprite : sprites)
            if (sprite.stable_id == 0 || !identifiers.insert(sprite.stable_id).second) return false;
        for (const auto& sample : samples)
            if (!sample.is_valid() || !identifiers.insert(sample.stable_id).second) return false;
    } catch (const std::bad_alloc&) { return false; }
    return true;
}

auto storyboard_time_t::from_frame(std::uint64_t frame_index, std::uint32_t numerator,
    std::uint32_t denominator) -> result_t<storyboard_time_t, failure_code_t> {
    if (numerator == 0 || denominator == 0)
        return std::unexpected(failure_code_t::invalid_request);
    const auto scaled = checked_math::checked_multiply(wide_math::multiply(frame_index, denominator), 1000);
    if (!scaled) return std::unexpected(failure_code_t::arithmetic_overflow);
    const auto divided = wide_math::divide(*scaled, {.low = numerator});
    if (!wide_math::can_fit_u64(divided.quotient) ||
        divided.quotient.low > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        return std::unexpected(failure_code_t::arithmetic_overflow);
    const auto remainder = static_cast<std::uint32_t>(divided.remainder.low);
    const auto divisor = std::gcd(remainder, numerator);
    return storyboard_time_t{.milliseconds = static_cast<std::int64_t>(divided.quotient.low),
        .remainder = remainder / divisor, .denominator = numerator / divisor};
}


auto storyboard_animation_t::is_valid() const noexcept -> bool {
    return frame_count > 0 && frame_count <= static_cast<std::uint32_t>(INT32_MAX) &&
        std::isfinite(frame_delay_ms) && frame_delay_ms > 0 &&
        (loop_type == storyboard_animation_loop_t::loop_forever || loop_type == storyboard_animation_loop_t::loop_once);
}

auto is_valid_easing(std::int32_t easing) noexcept -> bool {
    return easing >= 0 && easing <= 34;
}

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

// A delay is its exact binary64 value S*2^e. Elapsed A/D fits 96 bits;
// B=D*S fits 85, and B*frame_count fits 116. No floating time subtraction.
struct animation_ratio_t {
    static_assert(std::numeric_limits<double>::radix == 2 && std::numeric_limits<double>::digits == 53 &&
        std::numeric_limits<double>::max_exponent == 1024);
    wide_math::u128_t numerator;
    wide_math::u128_t divisor;
    int exponent{};
    static auto from(storyboard_time_t time, std::int64_t epoch, double delay) noexcept -> animation_ratio_t {
        int exponent{};
        const auto fraction = std::frexp(delay, &exponent);
        auto significand = static_cast<std::uint64_t>(std::ldexp(fraction, 53));
        exponent -= 53;
        while ((significand & 1U) == 0) { significand >>= 1; ++exponent; }
        return {wide_math::add(wide_math::multiply(distance_i64(epoch, time.milliseconds), time.denominator), time.remainder),
            wide_math::multiply(time.denominator, significand), exponent};
    }
};

auto shift_animation_quotient(wide_math::u128_t value, int exponent) noexcept -> wide_math::u128_t {
    if (exponent >= 128) return {};
    if (exponent >= 64) return {.low = value.high >> (exponent - 64)};
    if (exponent == 0) return value;
    return {value.high >> exponent, (value.low >> exponent) | (value.high << (64 - exponent))};
}

auto animation_wrapped_index(animation_ratio_t ratio, std::uint32_t count) noexcept -> std::uint32_t {
    // floor(A*2^k/B) mod N = floor(((A*2^k) mod (B*N))/B).
    const auto modulus = *checked_math::checked_multiply(ratio.divisor, count);
    auto residue = wide_math::divide(ratio.numerator, modulus).remainder;
    for (int bit = 0; bit < -ratio.exponent; ++bit) {
        residue = wide_math::shift_left(residue, 1);
        if (!wide_math::u128_less(residue, modulus)) residue = wide_math::subtract(residue, modulus);
    }
    return static_cast<std::uint32_t>(wide_math::divide(residue, ratio.divisor).quotient.low);
}

auto animation_clamped_index(animation_ratio_t ratio, std::uint32_t count) noexcept -> std::uint32_t {
    const auto limit = *checked_math::checked_multiply(ratio.divisor, count - 1);
    for (int bit = 0; bit < -ratio.exponent; ++bit) {
        if (!wide_math::u128_less(ratio.numerator, limit)) return count - 1;
        ratio.numerator = wide_math::shift_left(ratio.numerator, 1);
    }
    if (!wide_math::u128_less(ratio.numerator, limit)) return count - 1;
    return static_cast<std::uint32_t>(wide_math::divide(ratio.numerator, ratio.divisor).quotient.low);
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

auto storyboard_indentation(std::string_view line) noexcept -> std::size_t {
    const auto end = line.find_first_not_of(" \t_");
    return end == std::string_view::npos ? line.size() : end;
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

auto is_valid_visual_header(const storyboard_sprite_t& sprite,
    std::size_t maximum_path_bytes = storyboard_default_maximum_path_bytes) noexcept -> bool {
    return !sprite.layer.empty() && !sprite.origin.empty() && !sprite.path.empty() &&
        sprite.path.size() <= maximum_path_bytes && is_valid_header_token(sprite.layer) &&
        is_valid_header_token(sprite.origin) && osu_line::is_writable_filename(sprite.path) &&
        std::isfinite(sprite.x) && std::isfinite(sprite.y) &&
        (!sprite.animation || sprite.animation->is_valid());
}

auto is_valid_command_payload(const storyboard_command_t& command) -> bool {
    return is_valid_opcode(command.opcode) && command.value_count == expected_value_count(command.opcode) &&
        is_valid_easing(command.easing) && command.start_time_ms <= command.end_time_ms;
}
auto is_valid_node_payload(const storyboard_node_t& node) -> bool {
    const bool container = node.kind == storyboard_node_kind_t::loop || node.kind == storyboard_node_kind_t::trigger;
    if (static_cast<unsigned>(node.kind) > static_cast<unsigned>(storyboard_node_kind_t::trigger) ||
        (!container && !node.children.empty()) ||
        (node.kind == storyboard_node_kind_t::trigger) != node.trigger.has_value() ||
        (node.trigger && !node.trigger->is_valid())) return false;
    return (node.kind != storyboard_node_kind_t::retained_line || is_valid_single_line(node.retained_line)) &&
        is_valid_opcode(node.command.opcode) &&
        (node.kind != storyboard_node_kind_t::command || is_valid_command_payload(node.command)) &&
        std::ranges::all_of(node.command.from, [](double value) { return std::isfinite(value); }) &&
        std::ranges::all_of(node.command.to, [](double value) { return std::isfinite(value); });
}

auto is_valid_emit_node(const storyboard_node_t& node, std::size_t depth, std::size_t node_count) -> bool {
    return depth <= 128 && node_count < 250'000 && is_valid_node_payload(node);
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

auto emit_trigger_node(std::string& output, const storyboard_node_t& node,
    std::size_t indentation, std::size_t maximum_bytes, std::size_t depth, std::size_t& node_count) -> bool {
    const auto& trigger = *node.trigger;
    if (!append_bounded(output, line_prefix(indentation) + "T," + trigger.name.value, maximum_bytes)) return false;
    for (const auto value : {trigger.start_time_ms, trigger.end_time_ms}) {
        if (value && (!append_bounded(output, ",", maximum_bytes) || !append_integer(output, *value))) return false;
    }
    if (trigger.group_number && (!append_bounded(output, ",", maximum_bytes) ||
        !append_integer(output, *trigger.group_number))) return false;
    if (!append_bounded(output, "\n", maximum_bytes)) return false;
    for (const auto& child : node.children)
        if (!emit_node(output, child, indentation + 1, maximum_bytes, depth + 1, node_count)) return false;
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
    if (node.kind == storyboard_node_kind_t::trigger)
        return emit_trigger_node(output, node, indentation, maximum_bytes, depth, node_count);
    return emit_command_node(output, node, indentation, maximum_bytes);
}

struct storyboard_node_extent_t {
    std::int64_t extent_ms{}, end_time_ms{};
    bool has_unconditional{}, has_trigger{};
};

auto extent_of_nodes(const std::vector<storyboard_node_t>& nodes, std::size_t depth,
    std::size_t maximum_depth, std::size_t maximum_nodes, std::size_t& node_count,
    storyboard_node_extent_t& result) -> std::optional<storyboard_diagnostic_code_t>;

auto extent_of_node(const storyboard_node_t& node, std::size_t depth,
    std::size_t maximum_depth, std::size_t maximum_nodes, std::size_t& node_count,
    storyboard_node_extent_t& result) -> std::optional<storyboard_diagnostic_code_t> {
    if (depth > maximum_depth) return storyboard_diagnostic_code_t::nesting_limit;
    if (node_count >= maximum_nodes) return storyboard_diagnostic_code_t::node_limit;
    ++node_count;
    if (!is_valid_node_payload(node)) return storyboard_diagnostic_code_t::invalid_model;
    result = {};
    if (node.kind == storyboard_node_kind_t::command) {
        result = {.extent_ms = std::max<std::int64_t>(0, node.command.end_time_ms),
            .end_time_ms = node.command.end_time_ms, .has_unconditional = true};
        return std::nullopt;
    }
    if (node.kind == storyboard_node_kind_t::retained_line) {
        const auto content = trim(std::string_view(node.retained_line).substr(storyboard_indentation(node.retained_line)));
        result.has_trigger = content == "T" || content.starts_with("T,");
        return std::nullopt;
    }
    storyboard_node_extent_t children;
    if (const auto error = extent_of_nodes(node.children, depth + 1, maximum_depth, maximum_nodes, node_count, children)) return error;
    if (node.kind == storyboard_node_kind_t::trigger) {
        result.has_trigger = true;
        return std::nullopt;
    }
    result.has_trigger = children.has_trigger;
    result.has_unconditional = children.has_unconditional || !children.has_trigger;
    if (!result.has_unconditional) return std::nullopt; // Trigger-only loops have no lifetime.
    std::int64_t repeated{};
    if (!checked_mul_nonnegative(children.extent_ms, node.loop_iteration_count, repeated) ||
        !checked_add(node.loop_start_time_ms, repeated, result.end_time_ms)) return storyboard_diagnostic_code_t::time_overflow;
    result.extent_ms = std::max<std::int64_t>(0, result.end_time_ms);
    return std::nullopt;
}

auto extent_of_nodes(const std::vector<storyboard_node_t>& nodes, std::size_t depth,
    std::size_t maximum_depth, std::size_t maximum_nodes, std::size_t& node_count,
    storyboard_node_extent_t& result) -> std::optional<storyboard_diagnostic_code_t> {
    if (depth > maximum_depth) return storyboard_diagnostic_code_t::nesting_limit;
    result = {};
    for (const auto& node : nodes) {
        storyboard_node_extent_t child;
        if (const auto error = extent_of_node(node, depth, maximum_depth, maximum_nodes, node_count, child)) return error;
        result.extent_ms = std::max(result.extent_ms, child.extent_ms);
        result.has_unconditional = result.has_unconditional || child.has_unconditional;
        result.has_trigger = result.has_trigger || child.has_trigger;
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
    double fraction{};
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
        const auto fraction = query_time_ms == context.query_time_ms ? context.fraction : 0.0;
        progress = (static_cast<double>(distance_i64(start, query_time_ms)) + fraction) /
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
    storyboard_node_extent_t extent;
    const auto extent_error = extent_of_nodes(node.children, depth + 1, context.maximum_nesting,
                                              context.maximum_nodes, count, extent);
    if (extent_error.has_value()) {
        set_evaluation_error(context, *extent_error, "could not determine storyboard loop duration");
        return std::nullopt;
    }
    const auto period = extent.extent_ms;
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
        if (node.kind == storyboard_node_kind_t::retained_line || node.kind == storyboard_node_kind_t::trigger) {
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
    if (left.kind == storyboard_node_kind_t::trigger)
        return left.trigger == right.trigger && semantic_nodes_equal(left.children, right.children);
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


struct trigger_parse_stage_t {
    std::size_t indentation{}, destination_size{}, node_count_before{};
    std::vector<std::size_t> parent_path, parent_indentation;
    std::vector<storyboard_top_level_line_t> raw_lines;
    bool failed{};
};

class osb_parser_t {
  public:
    explicit osb_parser_t(storyboard_parse_options_t options) : options_(options) {}

    auto parse(std::string_view text) -> storyboard_parse_outcome_t {
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
            if (!finish_trigger_stage()) return std::move(result_);
            result_.value = std::move(document_);
        } catch (const std::bad_alloc&) {
            result_.value.reset();
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

    auto account_line() -> bool {
        ++line_number_;
        if (line_number_ > options_.maximum_lines) {
            fail(storyboard_diagnostic_code_t::line_limit, "OSB input exceeds the configured line limit");
            return false;
        }
        if (source_order_ == std::numeric_limits<std::uint64_t>::max()) {
            fail(storyboard_diagnostic_code_t::invalid_model, "OSB source order overflowed");
            return false;
        }
        ++source_order_;
        return true;
    }

    auto capture_trigger_line(std::string_view line, std::size_t indentation) -> bool {
        if (indentation > 0 && indentation - 1 > options_.maximum_nesting) {
            fail(storyboard_diagnostic_code_t::nesting_limit, "OSB retained trigger indentation exceeds the nesting limit");
            return false;
        }
        const auto remaining = options_.maximum_nodes - std::min(options_.maximum_nodes, trigger_stage_->node_count_before);
        if (trigger_stage_->raw_lines.size() >= remaining) {
            fail(storyboard_diagnostic_code_t::node_limit, "OSB retained trigger exceeds the node limit");
            return false;
        }
        trigger_stage_->raw_lines.push_back({source_order_, std::string(line)});
        return true;
    }

    auto finish_trigger_stage() -> bool {
        if (!trigger_stage_) return true;
        if (trigger_stage_->failed) {
            loop_path_ = trigger_stage_->parent_path;
            loop_indentation_ = trigger_stage_->parent_indentation;
            auto& destination = *current_destination();
            destination.resize(trigger_stage_->destination_size);
            node_count_ = trigger_stage_->node_count_before;
            for (auto& line : trigger_stage_->raw_lines) {
                destination.push_back({.kind = storyboard_node_kind_t::retained_line,
                    .source_order = line.source_order, .retained_line = std::move(line.text)});
                ++node_count_;
            }
        }
        trigger_stage_.reset();
        return true;
    }

    auto process_line(std::string_view line) -> bool {
        if (!account_line()) return false;
        const auto indentation = storyboard_indentation(line);
        const auto content = trim(line.substr(indentation));
        if (trigger_stage_ && !content.empty() && indentation <= trigger_stage_->indentation)
            if (!finish_trigger_stage()) return false;
        if (trigger_stage_) {
            if (!capture_trigger_line(line, indentation)) return false;
            if (trigger_stage_->failed) return true;
        }
        if (process_model_line(line, indentation, content)) return true;
        if (!trigger_stage_ || result_.diagnostics.empty()) return false;
        const auto code = result_.diagnostics.back().code;
        if (code != storyboard_diagnostic_code_t::malformed_line &&
            code != storyboard_diagnostic_code_t::invalid_number &&
            code != storyboard_diagnostic_code_t::invalid_command &&
            code != storyboard_diagnostic_code_t::time_overflow) return false;
        trigger_stage_->failed = true;
        return true;
    }

    auto process_model_line(std::string_view line, std::size_t indentation, std::string_view content) -> bool {
        if (content.empty()) {
            return retain_blank_line(line);
        }
        const auto fields = osu_line::split_fields(content);
        if (indentation == 0 && fields.front() == "Sprite") {
            return parse_sprite(fields);
        }
        if (indentation == 0 && fields.front() == "Animation") return parse_animation(line, fields);
        if (indentation == 0 && fields.front() == "Sample") return parse_sample(line, fields);
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

    auto parse_sample(std::string_view line, const std::vector<std::string>& fields) -> bool {
        active_sprite_.reset();
        loop_path_.clear();
        loop_indentation_.clear();
        storyboard_sample_t sample{.stable_id = source_order_, .source_order = source_order_};
        std::uint32_t layer{}, volume{};
        const auto valid_fields = fields.size() == 4 || fields.size() == 5;
        const auto valid_numbers = valid_fields && parse_integer(fields[1], sample.time_ms) &&
            parse_integer(fields[2], layer) && layer <= 3 &&
            (fields.size() == 4 || (parse_integer(fields[4], volume) && volume >= 1 && volume <= 100));
        if (valid_numbers) {
            sample.layer = static_cast<osbx_layer_t>(layer);
            sample.path = osu_line::clean_filename(fields[3]);
            if (fields.size() == 5) sample.volume = static_cast<std::uint8_t>(volume);
        }
        if (!valid_numbers || !sample.is_valid(options_.maximum_path_bytes)) {
            fail(storyboard_diagnostic_code_t::malformed_line, "Sample requires an integer time, layer0..3, path and optional volume1..100");
            document_.retained_top_level_lines.push_back({source_order_, std::string(line)});
            return true;
        }
        if (++node_count_ > options_.maximum_nodes) {
            fail(storyboard_diagnostic_code_t::node_limit, "OSB Sample count exceeds the node limit");
            return false;
        }
        document_.samples.push_back(std::move(sample));
        return true;
    }

    auto read_visual_header(const std::vector<std::string>& fields, storyboard_sprite_t& sprite) -> bool {
        if (fields.size() < 6 || fields[1].empty() || fields[2].empty() ||
            fields[3].size() > options_.maximum_path_bytes || fields[3].empty()) {
            fail(storyboard_diagnostic_code_t::malformed_line,
                 "OSB visual header must contain layer, origin, path, and position");
            return false;
        }
        sprite.stable_id = source_order_;
        sprite.source_order = source_order_;
        sprite.layer = fields[1];
        sprite.origin = fields[2];
        sprite.path = osu_line::clean_filename(fields[3]);
        if (!parse_double(fields[4], sprite.x) || !parse_double(fields[5], sprite.y)) {
            fail(storyboard_diagnostic_code_t::invalid_number, "OSB visual position is not a finite number");
            return false;
        }
        return true;
    }

    void append_visual(storyboard_sprite_t sprite) {
        document_.sprites.push_back(std::move(sprite));
        active_sprite_ = document_.sprites.size() - 1;
        loop_path_.clear();
        loop_indentation_.clear();
    }

    auto parse_sprite(const std::vector<std::string>& fields) -> bool {
        if (fields.size() != 6) {
            fail(storyboard_diagnostic_code_t::malformed_line,
                 "OSB Sprite header must contain layer, origin, path, and position");
            return false;
        }
        storyboard_sprite_t sprite;
        if (!read_visual_header(fields, sprite)) return false;
        append_visual(std::move(sprite));
        return true;
    }

    auto read_animation(const std::vector<std::string>& fields, storyboard_animation_t& animation) -> bool {
        if (!parse_integer(fields[6], animation.frame_count) || !parse_double(fields[7], animation.frame_delay_ms)) {
            fail(storyboard_diagnostic_code_t::invalid_number, "OSB Animation count or frame delay is not valid");
            return false;
        }
        if (fields.size() == 9) {
            if (fields[8] == "LoopOnce") animation.loop_type = storyboard_animation_loop_t::loop_once;
            else if (fields[8] != "LoopForever") {
                fail(storyboard_diagnostic_code_t::malformed_line, "Unknown OSB Animation loop type");
                return false;
            }
        }
        if (!animation.is_valid()) {
            fail(storyboard_diagnostic_code_t::invalid_number, "OSB Animation count and frame delay must be positive and bounded");
            return false;
        }
        return true;
    }

    auto parse_animation(std::string_view line, const std::vector<std::string>& fields) -> bool {
        // A malformed new object cannot keep the preceding Sprite active. Its
        // indented block is retained at top level by the ordinary orphan path.
        active_sprite_.reset();
        loop_path_.clear();
        loop_indentation_.clear();
        storyboard_sprite_t sprite;
        storyboard_animation_t animation;
        if (fields.size() != 8 && fields.size() != 9) {
            fail(storyboard_diagnostic_code_t::malformed_line, "OSB Animation header needs count, delay and optional loop type");
        } else if (read_visual_header(fields, sprite) && read_animation(fields, animation)) {
            sprite.animation = animation;
            if (is_valid_visual_header(sprite, options_.maximum_path_bytes)) {
                append_visual(std::move(sprite));
                return true;
            }
            fail(storyboard_diagnostic_code_t::malformed_line, "OSB Animation has an invalid visual header");
        }
        document_.retained_top_level_lines.push_back({source_order_, std::string(line)});
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
        if (fields.front() == "T" && !trigger_stage_) {
            trigger_stage_ = trigger_parse_stage_t{.indentation = indentation,
                .destination_size = (**destination).size(), .node_count_before = node_count_,
                .parent_path = loop_path_, .parent_indentation = loop_indentation_};
            if (!capture_trigger_line(line, indentation)) return false;
        }
        auto nodes = parse_nodes(fields, line);
        if (!nodes.has_value()) {
            return false;
        }
        return insert_nodes(**destination, std::move(*nodes), indentation);
    }

    auto parse_trigger(const std::vector<std::string>& fields, storyboard_node_t node)
        -> std::optional<std::vector<storyboard_node_t>> {
        if (fields.size() < 2 || fields.size() > 5) return malformed_command();
        const auto name = storyboard_trigger_name_t::from(fields[1]);
        if (!name) return malformed_command();
        storyboard_trigger_t trigger{.name = *name};
        std::int64_t time{};
        if (fields.size() > 2) {
            if (!parse_integer(fields[2], time)) return malformed_command();
            trigger.start_time_ms = time;
        }
        if (fields.size() > 3) {
            if (!parse_integer(fields[3], time)) return malformed_command();
            trigger.end_time_ms = time;
        }
        if (fields.size() > 4) {
            std::int32_t group{};
            if (!parse_integer(fields[4], group)) return malformed_command();
            trigger.group_number = group;
        }
        if (!trigger.is_valid()) return malformed_command();
        node.kind = storyboard_node_kind_t::trigger;
        node.trigger = std::move(trigger);
        return std::vector<storyboard_node_t>{std::move(node)};
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
        if (fields.front() == "T") return parse_trigger(fields, std::move(node));
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
        if ((node.kind == storyboard_node_kind_t::loop || node.kind == storyboard_node_kind_t::trigger) &&
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
        if (destination.back().kind == storyboard_node_kind_t::loop || destination.back().kind == storyboard_node_kind_t::trigger) {
            loop_path_.push_back(inserted_index);
            loop_indentation_.push_back(indentation);
        }
        return true;
    }

    std::optional<trigger_parse_stage_t> trigger_stage_;
    storyboard_parse_options_t options_;
    storyboard_parse_outcome_t result_;
    storyboard_document_model_t document_;
    std::vector<std::size_t> loop_path_;
    std::vector<std::size_t> loop_indentation_;
    std::optional<std::size_t> active_sprite_;
    std::size_t line_number_{};
    std::size_t node_count_{};
    std::uint64_t source_order_{};
};

} // namespace

auto evaluate_storyboard_easing(std::int32_t easing, double progress) noexcept -> double {
    return ease(easing, progress);
}

auto parse_osb(std::string_view text, storyboard_parse_options_t options)
    -> storyboard_parse_outcome_t {
    return osb_parser_t{options}.parse(text);
}

namespace {

struct osb_top_item_t {
    std::uint64_t order{};
    enum class kind_t { retained, sprite, sample } kind{};
    std::size_t index{};
};

auto make_osb_top_items(const storyboard_document_model_t& document,
                        storyboard_emit_outcome_t& result) -> std::optional<std::vector<osb_top_item_t>> {
    if (document.sprites.size() > std::numeric_limits<std::size_t>::max() -
                                      document.retained_top_level_lines.size()) {
        result.diagnostics.push_back({storyboard_diagnostic_code_t::capacity_exceeded, 0,
                                      "OSB top-level item count overflowed"});
        return std::nullopt;
    }
    const auto base_count = document.sprites.size() + document.retained_top_level_lines.size();
    if (document.samples.size() > 250'000 || base_count > 250'000 - document.samples.size()) {
        result.diagnostics.push_back({storyboard_diagnostic_code_t::node_limit, 0,
                                      "OSB document exceeds the emitter node limit"});
        return std::nullopt;
    }
    const auto count = base_count + document.samples.size();
    std::vector<osb_top_item_t> items;
    try {
        items.reserve(count);
        for (std::size_t index = 0; index < document.sprites.size(); ++index) {
            items.push_back({document.sprites[index].source_order, osb_top_item_t::kind_t::sprite, index});
        }
        for (std::size_t index = 0; index < document.retained_top_level_lines.size(); ++index) {
            items.push_back({document.retained_top_level_lines[index].source_order, osb_top_item_t::kind_t::retained, index});
        }
        for (std::size_t index = 0; index < document.samples.size(); ++index)
            items.push_back({document.samples[index].source_order, osb_top_item_t::kind_t::sample, index});
    } catch (const std::bad_alloc&) {
        result.diagnostics.push_back({storyboard_diagnostic_code_t::capacity_exceeded, 0,
                                      "OSB ordering allocation failed"});
        return std::nullopt;
    }
    std::ranges::sort(items, [](const auto& left, const auto& right) {
        return std::tie(left.order, left.kind, left.index) <
               std::tie(right.order, right.kind, right.index);
    });
    return items;
}

auto append_animation_header_tail(std::string& output, const storyboard_animation_t& animation,
    std::size_t maximum_bytes) -> bool {
    return append_bounded(output, ",", maximum_bytes) && append_unsigned(output, animation.frame_count) &&
        append_bounded(output, ",", maximum_bytes) && append_double(output, animation.frame_delay_ms) &&
        append_bounded(output, animation.loop_type == storyboard_animation_loop_t::loop_once
            ? ",LoopOnce" : ",LoopForever", maximum_bytes);
}

auto append_osb_sprite_header(std::string& output, const storyboard_sprite_t& sprite,
                              std::size_t maximum_bytes,
                              storyboard_emit_outcome_t& result) -> bool {
    if (!is_valid_visual_header(sprite)) {
        result.diagnostics.push_back({storyboard_diagnostic_code_t::invalid_model, 0,
                                      "OSB sprite has an invalid header"});
        return false;
    }
    if (!append_bounded(output, sprite.animation ? "Animation," : "Sprite,", maximum_bytes) ||
        !append_bounded(output, sprite.layer, maximum_bytes) ||
        !append_bounded(output, ",", maximum_bytes) ||
        !append_bounded(output, sprite.origin, maximum_bytes) ||
        !append_bounded(output, ",", maximum_bytes) ||
        !append_bounded(output, osu_line::quote_filename(sprite.path), maximum_bytes)) {
        return false;
    }
    return append_bounded(output, ",", maximum_bytes) && append_double(output, sprite.x) &&
           append_bounded(output, ",", maximum_bytes) && append_double(output, sprite.y) &&
           (!sprite.animation || append_animation_header_tail(output, *sprite.animation, maximum_bytes)) &&
           append_bounded(output, "\n", maximum_bytes);
}

auto emit_osb_sample(std::string& output, const storyboard_sample_t& sample, std::size_t maximum_bytes,
    std::size_t& node_count, storyboard_emit_outcome_t& result) -> bool {
    if (!sample.is_valid() || node_count >= 250'000) {
        result.diagnostics.push_back({storyboard_diagnostic_code_t::invalid_model, 0, "Invalid or excessive OSB Sample"});
        return false;
    }
    ++node_count;
    const auto success = append_bounded(output, "Sample,", maximum_bytes) && append_integer(output, sample.time_ms) &&
        append_bounded(output, ",", maximum_bytes) && append_unsigned(output, static_cast<std::uint64_t>(sample.layer)) &&
        append_bounded(output, ",", maximum_bytes) && append_bounded(output, osu_line::quote_filename(sample.path), maximum_bytes) &&
        (!sample.volume || (append_bounded(output, ",", maximum_bytes) && append_unsigned(output, *sample.volume))) &&
        append_bounded(output, "\n", maximum_bytes);
    if (!success) result.diagnostics.push_back({storyboard_diagnostic_code_t::capacity_exceeded, 0, "OSB Sample output exceeds its byte limit"});
    return success;
}

auto emit_osb_top_item(std::string& output, const storyboard_document_model_t& document,
                       const osb_top_item_t& item, std::size_t maximum_bytes,
                       std::size_t& node_count, storyboard_emit_outcome_t& result) -> bool {
    if (item.kind == osb_top_item_t::kind_t::sample)
        return emit_osb_sample(output, document.samples[item.index], maximum_bytes, node_count, result);
    if (item.kind == osb_top_item_t::kind_t::retained) {
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
    -> storyboard_emit_outcome_t {
    storyboard_emit_outcome_t result{};
    if (!document.has_valid_samples()) {
        result.diagnostics.push_back({storyboard_diagnostic_code_t::invalid_model, 0, "Invalid OSB Sample identities/data"});
        return result;
    }
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
    result.value = std::move(output);
    return result;
}


namespace {


auto reserve_schedule_events(const storyboard_document_model_t& document,
    storyboard_schedule_options_t options, storyboard_schedule_outcome_t& result) -> std::optional<std::size_t> {
    if (document.samples.size() > options.maximum_events || document.samples.size() > options.maximum_nodes) {
        result.diagnostics.push_back({storyboard_diagnostic_code_t::capacity_exceeded, 0, "Sample schedule exceeds its event/node limit"});
        return std::nullopt;
    }
    // Bounded upper capacity only: ignored roots must not fail the actual event budget.
    auto count = document.samples.size();
    for (const auto& sprite : document.sprites)
        for ([[maybe_unused]] const auto& node : sprite.nodes)
            count += std::min<std::size_t>(2, options.maximum_events - count);
    return count;
}

auto append_schedule_node(const storyboard_sprite_t& sprite, const storyboard_node_t& node,
                          storyboard_schedule_options_t options, storyboard_schedule_t& schedule,
                          std::size_t& node_count, std::uint64_t& stable_order,
                          storyboard_schedule_outcome_t& result) -> bool {
    if (node_count >= options.maximum_nodes) {
        result.diagnostics.push_back({storyboard_diagnostic_code_t::node_limit, 0,
                                      "storyboard schedule exceeds its node limit"});
        return false;
    }
    std::size_t nested_count{};
    storyboard_node_extent_t extent;
    const auto error = extent_of_node(node, 1, options.maximum_nesting, options.maximum_nodes,
                                      nested_count, extent);
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
    if (!extent.has_unconditional) return true;
    if (schedule.events.size() > options.maximum_events || options.maximum_events - schedule.events.size() < 2) {
        result.diagnostics.push_back({storyboard_diagnostic_code_t::capacity_exceeded, 0, "storyboard schedule exceeds its event limit"});
        return false;
    }
    if (stable_order > std::numeric_limits<std::uint64_t>::max() - 2) {
        result.diagnostics.push_back({storyboard_diagnostic_code_t::invalid_model, 0,
                                      "storyboard schedule order overflowed"});
        return false;
    }
    const auto begin = node.kind == storyboard_node_kind_t::command ? node.command.start_time_ms : node.loop_start_time_ms;
    const auto node_order = node.source_order;
    schedule.events.push_back({begin, node_order, sprite.stable_id,
                               node_order != 0 ? node_order : stable_order,
                               storyboard_event_t::edge_t::begin, stable_order++});
    schedule.events.push_back({extent.end_time_ms, node_order, sprite.stable_id,
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

namespace {
auto append_sample_schedule(const storyboard_document_model_t& document, storyboard_schedule_options_t options,
    storyboard_schedule_t& schedule, std::size_t node_count, std::uint64_t& stable_order,
    storyboard_schedule_outcome_t& result) -> bool {
    if (document.samples.empty()) return true;
    if (node_count > options.maximum_nodes || document.samples.size() > options.maximum_nodes - node_count ||
        document.sprites.size() > options.maximum_nodes) {
        result.diagnostics.push_back({storyboard_diagnostic_code_t::node_limit, 0, "Sample schedule exceeds its aggregate node limit"});
        return false;
    }
    if (!document.has_valid_samples(options.maximum_nodes)) {
        result.diagnostics.push_back({storyboard_diagnostic_code_t::invalid_model, 0, "Invalid Sample identities/data"});
        return false;
    }
    for (const auto& sample : document.samples) {
        schedule.events.push_back({.time_ms = sample.time_ms,
            .source_order = sample.source_order != 0 ? sample.source_order : stable_order + 1,
            .stable_object_id = sample.stable_id, .edge = storyboard_event_t::edge_t::sample,
            .schedule_order = stable_order++});
    }
    return true;
}
} // namespace

auto compile_storyboard_schedule(const storyboard_document_model_t& document,
                                 storyboard_schedule_options_t options)
    -> storyboard_schedule_outcome_t {
    storyboard_schedule_outcome_t result{};
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
    auto visual_options = options;
    visual_options.maximum_events -= document.samples.size();
    for (const auto& sprite : document.sprites) {
        for (const auto& node : sprite.nodes) {
            if (!append_schedule_node(sprite, node, visual_options, candidate, node_count, stable_order,
                                      result)) {
                return result;
            }
        }
    }
    try {
        if (!append_sample_schedule(document, options, candidate, node_count, stable_order, result)) return result;
    } catch (const std::bad_alloc&) {
        result.diagnostics.push_back({storyboard_diagnostic_code_t::capacity_exceeded, 0, "Sample schedule allocation failed"});
        return result;
    }
    std::ranges::sort(candidate.events, schedule_event_less);
    result.value = std::move(candidate);
    return result;
}


auto storyboard_event_cursor_t::start(const storyboard_schedule_t& schedule, std::int64_t time_ms)
    -> result_t<void, failure_code_t> {
    if (!std::ranges::is_sorted(schedule.events, [](const auto& left, const auto& right) {
            return std::tie(left.time_ms, left.source_order, left.stable_object_id,
                            left.stable_node_id, left.edge, left.schedule_order) <
                   std::tie(right.time_ms, right.source_order, right.stable_object_id,
                            right.stable_node_id, right.edge, right.schedule_order);
        })) {
        schedule_ = nullptr;
        active_ = false;
        return std::unexpected(failure_code_t::invalid_data);
    }
    schedule_ = &schedule;
    active_ = true;
    return seek(time_ms);
}

auto storyboard_event_cursor_t::seek(std::int64_t time_ms) -> result_t<void, failure_code_t> {
    if (!active_ || schedule_ == nullptr)
        return std::unexpected(failure_code_t::not_started);
    const auto first =
        std::ranges::lower_bound(schedule_->events, time_ms, {}, &storyboard_event_t::time_ms);
    head_ = static_cast<std::size_t>(std::distance(schedule_->events.begin(), first));
    last_end_time_ms_ = time_ms;
    return {};
}

auto storyboard_event_cursor_t::advance_events(std::int64_t exclusive_end_time_ms) noexcept
    -> result_t<std::span<const storyboard_event_t>, failure_code_t> {
    if (!active_ || schedule_ == nullptr)
        return std::unexpected(failure_code_t::not_started);
    if (exclusive_end_time_ms < last_end_time_ms_) {
        return std::unexpected(failure_code_t::invalid_state);
    }
    const auto begin = head_;
    while (head_ < schedule_->events.size() &&
           schedule_->events[head_].time_ms < exclusive_end_time_ms) {
        ++head_;
    }
    last_end_time_ms_ = exclusive_end_time_ms;
    return std::span<const storyboard_event_t>{schedule_->events}.subspan(begin, head_ - begin);
}


void storyboard_event_cursor_t::stop() noexcept {
    schedule_ = nullptr;
    head_ = 0;
    last_end_time_ms_ = 0;
    active_ = false;
}

auto evaluate_storyboard_property(const storyboard_sprite_t& sprite, storyboard_opcode_t opcode,
                                  std::int64_t time_ms, std::size_t maximum_nesting)
    -> storyboard_evaluation_effect_t {
    return evaluate_storyboard_property(sprite, opcode,
        storyboard_time_t{.milliseconds = time_ms}, maximum_nesting);
}

auto evaluate_storyboard_property(const storyboard_sprite_t& sprite, storyboard_opcode_t opcode,
    storyboard_time_t time, std::size_t maximum_nesting) -> storyboard_evaluation_effect_t {
    if (!time.is_valid()) return {.succeeded = false,
        .diagnostic = {storyboard_diagnostic_code_t::invalid_model, 0, "Invalid storyboard time fraction"}};
    evaluation_context_t context;
    context.opcode = opcode;
    context.query_time_ms = time.milliseconds;
    context.fraction = time.fraction();
    context.maximum_nesting = maximum_nesting;
    std::size_t validated_nodes{};
    storyboard_node_extent_t ignored_extent;
    if (const auto error = extent_of_nodes(sprite.nodes, 0, maximum_nesting, context.maximum_nodes,
            validated_nodes, ignored_extent))
        return {.succeeded = false, .diagnostic = {*error, 0, "Invalid storyboard property tree"}};
    evaluate_nodes(sprite.nodes, 0, time.milliseconds, 0, context);
    storyboard_evaluation_effect_t effect{
        .succeeded = !context.failed,
        .value = context.candidate.value,
        .diagnostic = std::move(context.diagnostic),
        .start_time_ms = context.candidate.start_time_ms,
        .source_order = context.candidate.source_order,
        .visit_order = context.candidate.visit_order,
    };
    return effect;
}


auto storyboard_animation_t::frame_path(std::string_view base, std::uint32_t index, storyboard_frame_arena_t& arena)
    -> result_t<std::string_view, failure_code_t> {
    std::array<char, 10> digits{};
    const auto converted = std::to_chars(digits.data(), digits.data() + digits.size(), index);
    if (converted.ec != std::errc{}) return std::unexpected(failure_code_t::arithmetic_overflow);
    const auto count = static_cast<std::size_t>(converted.ptr - digits.data());
    if (base.empty() || base.size() > storyboard_default_maximum_path_bytes - count)
        return std::unexpected(failure_code_t::capacity_exceeded);
    auto split = base.find_last_of('.');
    const auto slash = base.find_last_of("/\\");
    if (split == std::string_view::npos || (slash != std::string_view::npos && split < slash)) split = base.size();
    const auto storage = arena.allocate(base.size() + count, alignof(char));
    if (!storage) return std::unexpected(storage.error());
    auto* text = static_cast<char*>(*storage);
    std::memcpy(text, base.data(), split);
    std::memcpy(text + split, digits.data(), count);
    std::memcpy(text + split + count, base.data() + split, base.size() - split);
    return std::string_view{text, base.size() + count};
}

auto storyboard_animation_t::frame_index_at(storyboard_time_t time, std::int64_t epoch_ms) const noexcept
    -> std::optional<std::uint32_t> {
    if (!is_valid() || !time.is_valid()) return std::nullopt;
    if (time.milliseconds < epoch_ms || frame_count == 1) return 0;
    const auto ratio = animation_ratio_t::from(time, epoch_ms, frame_delay_ms);
    if (ratio.exponent < 0) return loop_type == storyboard_animation_loop_t::loop_once ?
        animation_clamped_index(ratio, frame_count) : animation_wrapped_index(ratio, frame_count);
    const auto quotient = shift_animation_quotient(wide_math::divide(ratio.numerator, ratio.divisor).quotient, ratio.exponent);
    if (loop_type == storyboard_animation_loop_t::loop_once) {
        if (!wide_math::can_fit_u64(quotient) || quotient.low >= frame_count - 1) return frame_count - 1;
        return static_cast<std::uint32_t>(quotient.low);
    }
    return static_cast<std::uint32_t>(wide_math::divide(quotient, {.low = frame_count}).remainder.low);
}

auto storyboard_origin_t::catalog() -> std::span<const storyboard_origin_entry_t> {
    static constexpr std::array<storyboard_origin_entry_t, 9> origins{{
        {"TopLeft", {0, 0}}, {"TopCentre", {0.5, 0}}, {"TopRight", {1, 0}},
        {"CentreLeft", {0, 0.5}}, {"Centre", {0.5, 0.5}}, {"CentreRight", {1, 0.5}},
        {"BottomLeft", {0, 1}}, {"BottomCentre", {0.5, 1}}, {"BottomRight", {1, 1}},
    }};
    return origins;
}

auto storyboard_origin_t::from(std::string_view name) -> std::optional<storyboard_origin_t> {
    const auto origins = catalog();
    const auto found = std::ranges::find_if(origins, [name](const auto& entry) {
        return entry.name == name;
    });
    if (found == origins.end()) return std::nullopt;
    return found->value;
}

auto storyboard_semantically_equal(const storyboard_document_model_t& left,
                                   const storyboard_document_model_t& right) -> bool {
    if (left.samples.size() != right.samples.size() || left.sprites.size() != right.sprites.size() ||
        !semantic_top_lines_equal(left.retained_top_level_lines, right.retained_top_level_lines)) {
        return false;
    }
    for (std::size_t index = 0; index < left.samples.size(); ++index) {
        const auto& a = left.samples[index];
        const auto& b = right.samples[index];
        if (std::tie(a.time_ms, a.layer, a.path, a.volume) != std::tie(b.time_ms, b.layer, b.path, b.volume)) return false;
    }
    for (std::size_t index = 0; index < left.sprites.size(); ++index) {
        const auto& a = left.sprites[index];
        const auto& b = right.sprites[index];
        if (a.layer != b.layer || a.origin != b.origin || a.path != b.path || a.x != b.x ||
            a.y != b.y || a.animation != b.animation || !semantic_nodes_equal(a.nodes, b.nodes)) {
            return false;
        }
    }
    return true;
}

} // namespace wormedit
