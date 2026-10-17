#include "ui/nuklear/metadata_state.h"

#include <wormedit/core/numeric_text.h>
#include "core/checked_math.h"
#include "core/text_util.h"

#include <algorithm>
#include <cstdio>
#include <mutex>
#include <string_view>
#include <utility>

namespace {

auto parse_metadata_rational_unsigned(std::string_view text, std::uint64_t limit)
    -> std::optional<std::uint64_t> {
    std::uint64_t value{};
    if (!wormedit::parse_integer(text, value) || value > limit) return std::nullopt;
    return value;
}

auto parse_metadata_rational_numerator(std::string_view text)
    -> std::optional<std::int64_t> {
    bool negative = false;
    if (!text.empty() && (text.front() == '-' || text.front() == '+')) {
        negative = text.front() == '-';
        text.remove_prefix(1);
    }
    const auto limit = negative
                           ? static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + 1
                           : static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    const auto magnitude = parse_metadata_rational_unsigned(text, limit);
    if (!magnitude)
        return std::nullopt;
    if (!negative)
        return static_cast<std::int64_t>(*magnitude);
    if (*magnitude == limit)
        return std::numeric_limits<std::int64_t>::min();
    return -static_cast<std::int64_t>(*magnitude);
}

auto parse_metadata_rational_number(std::string_view text)
    -> std::optional<worm_ui_nuklear_metadata_rational_t> {
    const auto slash = text.find('/');
    if (slash != std::string_view::npos) {
        if (text.find('/', slash + 1) != std::string_view::npos)
            return std::nullopt;
        const auto numerator = parse_metadata_rational_numerator(text.substr(0, slash));
        const auto denominator = parse_metadata_rational_unsigned(
            text.substr(slash + 1), std::numeric_limits<std::uint64_t>::max());
        if (!numerator || !denominator || *denominator == 0)
            return std::nullopt;
        return worm_ui_nuklear_metadata_rational_t{*numerator, *denominator};
    }

    const auto point = text.find('.');
    if (point != std::string_view::npos && text.find('.', point + 1) != std::string_view::npos)
        return std::nullopt;
    auto integer_part = point == std::string_view::npos ? text : text.substr(0, point);
    const auto fractional_part = point == std::string_view::npos
                                     ? std::string_view{}
                                     : text.substr(point + 1);
    bool negative = false;
    if (!integer_part.empty() && (integer_part.front() == '-' || integer_part.front() == '+')) {
        negative = integer_part.front() == '-';
        integer_part.remove_prefix(1);
    }
    if (integer_part.empty() && fractional_part.empty())
        return std::nullopt;
    std::uint64_t scale = 1;
    for (std::size_t i = 0; i < fractional_part.size(); ++i) {
        const auto next_scale = wormedit::checked_math::checked_multiply(scale, 10);
        if (!next_scale)
            return std::nullopt;
        scale = *next_scale;
    }
    const auto int_limit = negative
                               ? static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + 1
                               : static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    const auto integer = integer_part.empty() ? std::optional<std::uint64_t>{0}
                                              : parse_metadata_rational_unsigned(integer_part, int_limit);
    const auto fraction = fractional_part.empty() ? std::optional<std::uint64_t>{0}
                                                  : parse_metadata_rational_unsigned(fractional_part, scale - 1);
    if (!integer || !fraction || *integer > (int_limit - *fraction) / scale)
        return std::nullopt;
    const auto magnitude = *integer * scale + *fraction;
    if (!negative)
        return worm_ui_nuklear_metadata_rational_t{static_cast<std::int64_t>(magnitude), scale};
    const auto numerator = magnitude == int_limit
                               ? std::numeric_limits<std::int64_t>::min()
                               : -static_cast<std::int64_t>(magnitude);
    return worm_ui_nuklear_metadata_rational_t{numerator, scale};
}

auto append_metadata_rational_digits(std::string& output, std::uint64_t value) -> void {
    char digits[20];
    std::size_t length{};
    do {
        digits[length++] = static_cast<char>('0' + value % 10);
        value /= 10;
    } while (value != 0);
    while (length > 0)
        output.push_back(digits[--length]);
}

auto metadata_rational_decimal_places(std::uint64_t denominator) -> std::optional<std::size_t> {
    std::size_t places{};
    while (denominator > 1 && denominator % 10 == 0) {
        denominator /= 10;
        ++places;
    }
    if (denominator != 1)
        return std::nullopt;
    return places;
}

auto format_metadata_rational_decimal(worm_ui_nuklear_metadata_rational_t value,
                                      std::size_t places) -> std::string {
    std::string result;
    const auto magnitude = wormedit::checked_math::magnitude(value.numerator);
    const auto scale = value.denominator;
    const auto integer = magnitude / scale;
    auto fraction = magnitude % scale;
    if (value.numerator < 0)
        result.push_back('-');
    append_metadata_rational_digits(result, integer);
    if (places == 0)
        return result;
    result.push_back('.');
    char digits[20];
    for (std::size_t i = places; i > 0; --i) {
        digits[i - 1] = static_cast<char>('0' + fraction % 10);
        fraction /= 10;
    }
    result.append(digits, places);
    return result;
}

auto format_metadata_rational_fraction(worm_ui_nuklear_metadata_rational_t value) -> std::string {
    std::string result;
    if (value.numerator < 0)
        result.push_back('-');
    append_metadata_rational_digits(result, wormedit::checked_math::magnitude(value.numerator));
    result.push_back('/');
    append_metadata_rational_digits(result, value.denominator);
    return result;
}

auto metadata_file_dialog_state() -> wormedit::ui::file_dialog_slot_t& {
    static wormedit::ui::file_dialog_slot_t state;
    return state;
}

} // namespace

auto worm_ui_nuklear_metadata_state_t::rational_action_text(std::int64_t numerator,
                                                          std::uint64_t denominator)
    -> rational_action_text_t {
    std::snprintf(action_numerator_text.data(), action_numerator_text.size(), "%lld",
                  static_cast<long long>(numerator));
    std::snprintf(action_denominator_text.data(), action_denominator_text.size(), "%llu",
                  static_cast<unsigned long long>(denominator));
    return {.numerator = action_numerator_text.data(),
            .denominator = action_denominator_text.data()};
}

auto worm_ui_nuklear_parse_metadata_rational(std::string_view text, bool allow_milliseconds)
    -> std::optional<worm_ui_nuklear_metadata_rational_t> {
    text = wormedit::text_util::trim(text, wormedit::text_util::ascii_whitespace);
    bool milliseconds = false;
    if (allow_milliseconds && text.ends_with("ms")) {
        milliseconds = true;
        text.remove_suffix(2);
    } else if (allow_milliseconds && text.ends_with('s')) {
        text.remove_suffix(1);
    }
    text = wormedit::text_util::trim(text, wormedit::text_util::ascii_whitespace);
    auto value = parse_metadata_rational_number(text);
    if (!value || !milliseconds)
        return value;
    const auto denominator = wormedit::checked_math::checked_multiply(value->denominator, 1000);
    if (!denominator)
        return std::nullopt;
    value->denominator = *denominator;
    return value;
}

auto worm_ui_nuklear_format_metadata_rational(worm_ui_nuklear_metadata_rational_t value)
    -> std::string {
    if (value.denominator == 0)
        return format_metadata_rational_fraction(value);
    const auto places = metadata_rational_decimal_places(value.denominator);
    if (places)
        return format_metadata_rational_decimal(value, *places);
    return format_metadata_rational_fraction(value);
}

auto worm_ui_nuklear_metadata_file_dialog_begin() -> std::optional<std::uint64_t> {
    return metadata_file_dialog_state().begin();
}

void worm_ui_nuklear_metadata_file_dialog_complete(std::uint64_t generation, std::string path,
                                                   std::string error, bool canceled) {
    metadata_file_dialog_state().complete(generation, std::move(path), std::move(error), canceled);
}

auto worm_ui_nuklear_metadata_file_dialog_take()
    -> std::optional<worm_ui_nuklear_metadata_file_dialog_result_t> {
    return metadata_file_dialog_state().take();
}

auto worm_ui_nuklear_metadata_file_dialog_open() -> bool {
    return metadata_file_dialog_state().open();
}

auto worm_ui_nuklear_metadata_state() noexcept -> worm_ui_nuklear_metadata_state_t& {
    static worm_ui_nuklear_metadata_state_t state;
    return state;
}

void worm_ui_nuklear_clear_metadata_drop_targets() {
    auto& state = worm_ui_nuklear_metadata_state();
    state.project_drop_rectangle = {};
    state.export_drop_rectangle = {};
    state.export_drop_group_index = 0;
    state.output_path_drop_rectangle = {};
    state.output_path_drop_group_index = 0;
}

void worm_ui_nuklear_set_metadata_drop_target(
    WormUiMetadataDropTarget target, worm_ui_nuklear_metadata_state_t::drop_rectangle_t rect) {
    auto& state = worm_ui_nuklear_metadata_state();
    if (target.destination == WORM_UI_METADATA_DROP_PROJECT) {
        state.project_drop_rectangle = rect;
    } else if (target.destination == WORM_UI_METADATA_DROP_EXPORT) {
        state.export_drop_rectangle = rect;
        state.export_drop_group_index = target.output_group_index;
    } else if (target.destination == WORM_UI_METADATA_DROP_OUTPUT_PATH) {
        state.output_path_drop_rectangle = rect;
        state.output_path_drop_group_index = target.output_group_index;
    }
}

auto worm_ui_nuklear_metadata_drop_target(float x, float y) -> WormUiMetadataDropTarget {
    const auto& state = worm_ui_nuklear_metadata_state();
    if (state.export_drop_rectangle.is_point_inside(x, y))
        return {WORM_UI_METADATA_DROP_EXPORT, state.export_drop_group_index};
    if (state.output_path_drop_rectangle.is_point_inside(x, y))
        return {WORM_UI_METADATA_DROP_OUTPUT_PATH, state.output_path_drop_group_index};
    if (state.project_drop_rectangle.is_point_inside(x, y))
        return {WORM_UI_METADATA_DROP_PROJECT, 0};
    return {};
}

void worm_ui_nuklear_reset_metadata_state() {
    metadata_file_dialog_state().reset();
    worm_ui_nuklear_metadata_state() = {};
}

void worm_ui_nuklear_cancel_metadata_tag_drag() {
    worm_ui_nuklear_metadata_state().tag_drag = {};
}

auto worm_ui_nuklear_begin_metadata_tag_drag(std::size_t index, float x, float y) -> bool {
    auto& value = worm_ui_nuklear_metadata_state();
    if (!value.tag_buffers_valid || index >= value.search_tags.size()) return false;
    value.tag_drag = {.source = index, .insertion = std::nullopt, .start_x = x, .start_y = y};
    return true;
}

void worm_ui_nuklear_target_metadata_tag_drag(std::size_t index, bool after) {
    auto& value = worm_ui_nuklear_metadata_state();
    if (!value.tag_drag.source || index >= value.search_tags.size()) return;
    value.tag_drag.insertion = index + (after ? 1U : 0U);
}

auto worm_ui_nuklear_finish_metadata_tag_drag() -> bool {
    auto& value = worm_ui_nuklear_metadata_state();
    const auto drag = value.tag_drag;
    value.tag_drag = {};
    if (!drag.source || !drag.insertion || !drag.moved ||
        *drag.source >= value.search_tags.size() || *drag.insertion > value.search_tags.size())
        return false;
    const auto source = *drag.source;
    const auto target = *drag.insertion > source ? *drag.insertion - 1 : *drag.insertion;
    if (source == target) return false;
    auto begin = value.search_tags.begin();
    if (target < source) std::rotate(begin + target, begin + source, begin + source + 1);
    else std::rotate(begin + source, begin + source + 1, begin + target + 1);
    value.search_tag_pointers.clear();
    return true;
}
