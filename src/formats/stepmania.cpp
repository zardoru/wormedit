#include <wormedit/formats/stepmania.h>

#include <wormedit/core/measure.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <numeric>
#include <ranges>
#include <set>
#include <string_view>
#include <tuple>
#include <utility>

namespace wormedit {
namespace {

struct field_t {
    std::string key;
    std::string value;
    std::size_t order{};
    std::size_t offset{};
};

struct chart_source_t {
    std::vector<field_t> fields;
};

struct failure_t {
    stepmania_parse_diagnostic_code_t code;
    std::string message;
    std::size_t order{};
    std::optional<std::size_t> offset;
};

struct id_source_t {
    std::uint64_t next{1};
    template <typename id_t> auto take() -> id_t {
        return id_t{next++};
    }
};

[[noreturn]] auto fail(stepmania_parse_diagnostic_code_t code, std::string message,
                       std::size_t order, std::optional<std::size_t> offset = {}) -> void {
    throw failure_t{code, std::move(message), order, offset};
}

auto trim(std::string_view text) -> std::string_view {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r' ||
                             text.front() == '\n')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' ||
                             text.back() == '\n')) {
        text.remove_suffix(1);
    }
    return text;
}

auto upper_ascii(std::string_view text) -> std::string {
    std::string result(text);
    for (auto& character : result) {
        if (character >= 'a' && character <= 'z')
            character = static_cast<char>(character - 'a' + 'A');
    }
    return result;
}

auto valid_utf8(std::string_view source) -> std::optional<std::size_t> {
    std::size_t at{};
    while (at < source.size()) {
        const auto first = static_cast<unsigned char>(source[at]);
        if (first <= 0x7fU) {
            ++at;
            continue;
        }
        std::size_t count{};
        std::uint32_t point{};
        if (first >= 0xc2U && first <= 0xdfU) {
            count = 2;
            point = first & 0x1fU;
        } else if (first >= 0xe0U && first <= 0xefU) {
            count = 3;
            point = first & 0x0fU;
        } else if (first >= 0xf0U && first <= 0xf4U) {
            count = 4;
            point = first & 0x07U;
        } else
            return at;
        if (count > source.size() - at)
            return at;
        for (std::size_t i = 1; i < count; ++i) {
            const auto next = static_cast<unsigned char>(source[at + i]);
            if ((next & 0xc0U) != 0x80U)
                return at + i;
            point = (point << 6U) | (next & 0x3fU);
        }
        if ((count == 3 && point < 0x800U) || (count == 4 && point < 0x10000U) ||
            (point >= 0xd800U && point <= 0xdfffU) || point > 0x10ffffU)
            return at;
        at += count;
    }
    return std::nullopt;
}

auto parse_unsigned(std::string_view text, std::uint64_t& value) -> bool {
    text = trim(text);
    if (text.empty())
        return false;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}

auto decimal_rational(std::string_view text) -> std::optional<signed_rational_t> {
    text = trim(text);
    if (text.empty())
        return std::nullopt;
    bool negative{};
    if (text.front() == '+' || text.front() == '-') {
        negative = text.front() == '-';
        text.remove_prefix(1);
    }
    if (text.empty())
        return std::nullopt;
    const auto point = text.find('.');
    if (point != std::string_view::npos && text.find('.', point + 1) != std::string_view::npos)
        return std::nullopt;
    const auto fractional = point == std::string_view::npos ? 0U : text.size() - point - 1;
    if (fractional > 18)
        return std::nullopt;
    std::string digits;
    digits.reserve(text.size());
    bool any_digit{};
    for (const auto ch : text) {
        if (ch == '.')
            continue;
        if (ch < '0' || ch > '9')
            return std::nullopt;
        digits.push_back(ch);
        any_digit = true;
    }
    if (!any_digit)
        return std::nullopt;
    std::uint64_t magnitude{};
    if (!parse_unsigned(digits, magnitude))
        return std::nullopt;
    std::uint64_t denominator{1};
    for (std::size_t i = 0; i < fractional; ++i) {
        if (denominator > std::numeric_limits<std::uint64_t>::max() / 10)
            return std::nullopt;
        denominator *= 10;
    }
    const auto divisor = std::gcd(magnitude, denominator);
    magnitude /= divisor;
    denominator /= divisor;
    const auto max_numerator = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    if (magnitude > max_numerator)
        return std::nullopt;
    const auto numerator = static_cast<std::int64_t>(magnitude);
    return signed_rational_t{negative ? -numerator : numerator, denominator};
}

auto checked_lcm(std::uint64_t left, std::uint64_t right) -> std::optional<std::uint64_t> {
    if (left == 0 || right == 0)
        return std::uint64_t{0};
    const auto factor = left / std::gcd(left, right);
    if (factor > std::numeric_limits<std::uint64_t>::max() / right)
        return std::nullopt;
    return factor * right;
}

auto multiply_divide_exact(std::uint64_t value, std::uint64_t multiplier, std::uint64_t divisor)
    -> std::optional<std::uint64_t> {
    if (divisor == 0)
        return std::nullopt;
    auto left = value;
    auto right = multiplier;
    const auto common = std::gcd(left, divisor);
    left /= common;
    divisor /= common;
    const auto second_common = std::gcd(right, divisor);
    right /= second_common;
    divisor /= second_common;
    if (divisor != 1 || (right != 0 && left > std::numeric_limits<std::uint64_t>::max() / right))
        return std::nullopt;
    return left * right;
}

auto position_row(signed_rational_t beat, std::uint64_t divisions, std::size_t order,
                  std::optional<std::size_t> offset) -> std::uint64_t {
    if (beat.numerator < 0)
        fail(stepmania_parse_diagnostic_code_t::invalid_timing,
             "Timing positions cannot be negative", order, offset);
    if (beat.denominator > std::numeric_limits<std::uint64_t>::max() / 4U)
        fail(stepmania_parse_diagnostic_code_t::position_not_representable,
             "Beat denominator exceeds the exact position range", order, offset);
    const auto rows = multiply_divide_exact(static_cast<std::uint64_t>(beat.numerator), divisions,
                                            beat.denominator * 4U);
    if (!rows)
        fail(stepmania_parse_diagnostic_code_t::position_not_representable,
             "Beat position does not land on the selected exact editor grid", order, offset);
    return *rows;
}

auto fields_for_key(const std::vector<field_t>& fields, std::string_view key)
    -> std::vector<const field_t*> {
    std::vector<const field_t*> found;
    for (const auto& field : fields)
        if (upper_ascii(field.key) == key)
            found.push_back(&field);
    return found;
}

auto last_field(const std::vector<field_t>& fields, std::string_view key) -> const field_t* {
    const auto found = fields_for_key(fields, key);
    return found.empty() ? nullptr : found.back();
}

auto split(std::string_view value, char delimiter) -> std::vector<std::string_view> {
    std::vector<std::string_view> result;
    while (true) {
        const auto at = value.find(delimiter);
        if (at == std::string_view::npos) {
            result.push_back(value);
            return result;
        }
        result.push_back(value.substr(0, at));
        value.remove_prefix(at + 1);
    }
}

auto parse_source(std::string_view source, std::size_t max_fields) -> std::vector<field_t> {
    std::vector<field_t> fields;
    std::size_t at{};
    while (at < source.size()) {
        while (at < source.size()) {
            if (std::isspace(static_cast<unsigned char>(source[at]))) {
                ++at;
                continue;
            }
            if (source.substr(at, 2) == "//") {
                const auto end = source.find('\n', at + 2);
                at = end == std::string_view::npos ? source.size() : end + 1;
                continue;
            }
            break;
        }
        if (at == source.size())
            break;
        if (source[at] != '#')
            fail(stepmania_parse_diagnostic_code_t::malformed_tag, "Expected a #TAG declaration",
                 fields.size(), at);
        const auto tag_offset = at++;
        const auto key_begin = at;
        while (at < source.size() && source[at] != ':' && source[at] != ';' &&
               !std::isspace(static_cast<unsigned char>(source[at])))
            ++at;
        if (at == key_begin || at >= source.size() || source[at] != ':')
            fail(stepmania_parse_diagnostic_code_t::malformed_tag,
                 "Tag name must be followed by ':'", fields.size(), tag_offset);
        std::string key(source.substr(key_begin, at - key_begin));
        ++at;
        const auto value_begin = at;
        const auto terminator = source.find(';', at);
        if (terminator == std::string_view::npos)
            fail(stepmania_parse_diagnostic_code_t::malformed_tag,
                 "Tag value is missing its terminating semicolon", fields.size(), tag_offset);
        fields.push_back({std::move(key),
                          std::string(source.substr(value_begin, terminator - value_begin)),
                          fields.size(), tag_offset});
        if (fields.size() > max_fields)
            fail(stepmania_parse_diagnostic_code_t::source_limit,
                 "StepMania source exceeds the configured tag limit", fields.size(), tag_offset);
        at = terminator + 1;
    }
    return fields;
}

auto note_column_count(std::string_view step_type) -> std::optional<std::uint32_t> {
    const auto type = upper_ascii(trim(step_type));
    if (type == "DANCE-SINGLE" || type == "TECHNO-SINGLE")
        return 4;
    if (type == "DANCE-THREEPANEL")
        return 3;
    if (type == "DANCE-SOLO")
        return 6;
    if (type == "DANCE-DOUBLE" || type == "DANCE-COUPLE" || type == "DANCE-ROUTINE" ||
        type == "TECHNO-DOUBLE")
        return 8;
    if (type == "PUMP-SINGLE" || type == "EZ2-SINGLE")
        return 5;
    if (type == "PUMP-DOUBLE" || type == "PUMP-COUPLE" || type == "PUMP-ROUTINE" ||
        type == "EZ2-DOUBLE")
        return 10;
    if (type == "PUMP-HALFDOUBLE")
        return 6;
    if (type == "EZ2-REAL")
        return 7;
    if (type == "KB7")
        return 7;
    if (type == "KB4")
        return 4;
    if (type == "KB5")
        return 5;
    if (type == "KB6")
        return 6;
    if (type == "PARA-SINGLE")
        return 5;
    return std::nullopt;
}

auto fallback_step_type_for_count(std::size_t count) -> std::optional<std::string> {
    switch (count) {
    case 3:
        return "dance-threepanel";
    case 4:
        return "dance-single";
    case 5:
        return "pump-single";
    case 6:
        return "dance-solo";
    case 7:
        return "kb7";
    case 8:
        return "dance-double";
    case 10:
        return "pump-double";
    default:
        return std::nullopt;
    }
}

auto parse_note_source(const chart_source_t& source, stepmania_profile_t profile,
                       std::size_t chart_order)
    -> std::tuple<std::string, std::string, std::string, std::string, std::string> {
    const auto* notes = last_field(source.fields, "NOTES");
    if (!notes)
        fail(stepmania_parse_diagnostic_code_t::invalid_notes, "Chart has no #NOTES map",
             chart_order);
    if (profile == stepmania_profile_t::ssc) {
        auto step_type = last_field(source.fields, "STEPSTYPE");
        if (!step_type)
            fail(stepmania_parse_diagnostic_code_t::invalid_notes,
                 "SSC chart is missing #STEPSTYPE", notes->order, notes->offset);
        const auto description = last_field(source.fields, "DESCRIPTION");
        const auto difficulty = last_field(source.fields, "DIFFICULTY");
        const auto meter = last_field(source.fields, "METER");
        const auto radar = last_field(source.fields, "RADARVALUES");
        return {std::string(trim(step_type->value)),
                description ? std::string(trim(description->value)) : "",
                difficulty ? std::string(trim(difficulty->value)) : "Edit",
                meter ? std::string(trim(meter->value)) : "1",
                radar ? std::string(trim(radar->value)) : "0,0,0,0,0"};
    }
    const auto parts = split(notes->value, ':');
    if (parts.size() < 6)
        fail(stepmania_parse_diagnostic_code_t::invalid_notes,
             "SM #NOTES must contain six colon-separated fields", notes->order, notes->offset);
    return {std::string(trim(parts[0])), std::string(trim(parts[1])), std::string(trim(parts[2])),
            std::string(trim(parts[3])), std::string(trim(parts[4]))};
}

auto parse_row_map(std::string_view source, std::uint32_t lanes, std::uint64_t divisions,
                   std::size_t max_measures, std::size_t max_events, std::size_t source_order)
    -> std::vector<std::vector<std::pair<std::uint64_t, std::string>>> {
    auto measures = split(source, ',');
    if (measures.size() > max_measures)
        fail(stepmania_parse_diagnostic_code_t::measure_limit,
             "StepMania note map exceeds the configured measure limit", source_order);
    std::vector<std::vector<std::pair<std::uint64_t, std::string>>> output;
    output.reserve(measures.size());
    std::size_t event_count{};
    for (const auto& raw_measure : measures) {
        std::vector<std::string_view> rows;
        std::size_t begin{};
        while (begin <= raw_measure.size()) {
            const auto end = raw_measure.find_first_of("\r\n", begin);
            const auto row = trim(raw_measure.substr(
                begin, end == std::string_view::npos ? raw_measure.size() - begin : end - begin));
            if (!row.empty())
                rows.push_back(row);
            if (end == std::string_view::npos)
                break;
            begin = end + 1;
        }
        if (rows.empty())
            rows.push_back(std::string_view{"0000000000"}.substr(0, lanes));
        std::vector<std::pair<std::uint64_t, std::string>> measure_rows;
        measure_rows.reserve(rows.size());
        for (std::size_t row_index = 0; row_index < rows.size(); ++row_index) {
            if (rows[row_index].size() != lanes)
                fail(stepmania_parse_diagnostic_code_t::invalid_notes,
                     "Note row column count does not match the declared step type", source_order);
            std::string row;
            row.reserve(lanes);
            for (const auto character : rows[row_index]) {
                const auto symbol =
                    static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
                if (symbol != '0' && symbol != '1' && symbol != '2' && symbol != '3' &&
                    symbol != '4' && symbol != 'M' && symbol != 'L' && symbol != 'F')
                    fail(stepmania_parse_diagnostic_code_t::invalid_notes,
                         "Note map contains an unsupported StepMania note symbol", source_order);
                row.push_back(symbol);
            }
            const auto local =
                multiply_divide_exact(static_cast<std::uint64_t>(row_index), divisions,
                                      static_cast<std::uint64_t>(rows.size()));
            if (!local || *local >= divisions)
                fail(stepmania_parse_diagnostic_code_t::position_not_representable,
                     "Note row spacing cannot be represented on the selected editor grid",
                     source_order);
            measure_rows.emplace_back(*local, std::move(row));
            event_count += static_cast<std::size_t>(std::ranges::count_if(
                measure_rows.back().second, [](char symbol) { return symbol != '0'; }));
            if (event_count > max_events)
                fail(stepmania_parse_diagnostic_code_t::event_limit,
                     "StepMania note map exceeds the configured event limit", source_order);
        }
        output.push_back(std::move(measure_rows));
    }
    return output;
}

auto make_layout(std::string step_type, std::uint32_t count, id_source_t& ids) -> lane_layout_t {
    lane_layout_t layout;
    layout.profile_id = "stepmania:" + step_type;
    layout.display_name = step_type;
    static constexpr std::array<std::string_view, 10> dance_names{
        "Left", "Down", "Up", "Right", "Left 2", "Down 2", "Up 2", "Right 2", "Center", "Diagonal"};
    for (std::uint32_t i = 0; i < count; ++i) {
        layout.lanes.push_back(
            {.id = ids.take<lane_id_t>(),
             .name = std::string(i < dance_names.size() ? dance_names[i] : "Column") +
                     (i < dance_names.size() ? "" : " " + std::to_string(i + 1)),
             .role = lane_role_t::playable,
             .stage = count > 4 && i >= count / 2 ? stage_group_t::right : stage_group_t::left,
             .display_order = i,
             .glyph = lane_glyph_t::bar});
    }
    layout.split_stage = count == 8 || count == 10;
    if (layout.split_stage)
        layout.split_after_column = count / 2;
    return layout;
}

auto as_metadata_group(const std::vector<field_t>& fields, stepmania_profile_t profile,
                       const std::shared_ptr<const std::vector<std::byte>>& bytes)
    -> format_metadata_t {
    format_metadata_t group;
    group.family = format_family_t::stepmania;
    group.encoding.detected = text_encoding_t::utf8;
    group.encoding.original_bytes = bytes;
    group.encoding.unchanged_since_import = true;
    group.ordered_fields.reserve(fields.size());
    group.ordered_field_source_orders.reserve(fields.size());
    for (const auto& field : fields) {
        group.ordered_fields.emplace_back(field.key, field.value);
        group.ordered_field_source_orders.push_back(field.order);
    }
    (void)profile;
    return group;
}

auto has_error(const std::vector<stepmania_parse_diagnostic_t>& diagnostics) -> bool {
    return std::ranges::any_of(diagnostics, [](const auto& diagnostic) {
        return diagnostic.severity == diagnostic_severity_t::error;
    });
}

auto local_position(std::uint64_t row, std::uint64_t divisions) -> local_position_t {
    return {measure_id_t{row / divisions + 1}, row % divisions};
}

auto exact_product_ratio(std::array<std::uint64_t, 3> numerator,
                         std::array<std::uint64_t, 3> denominator) -> std::optional<std::uint64_t> {
    for (auto& top : numerator)
        for (auto& bottom : denominator) {
            const auto common = std::gcd(top, bottom);
            top /= common;
            bottom /= common;
        }
    if (std::ranges::any_of(denominator, [](auto value) { return value != 1; }))
        return std::nullopt;
    std::uint64_t result{1};
    for (const auto value : numerator) {
        if (value != 0 && result > std::numeric_limits<std::uint64_t>::max() / value)
            return std::nullopt;
        result *= value;
    }
    return result;
}

auto parse_timing_fields(const std::vector<field_t>& song_fields,
                         const std::vector<field_t>& chart_fields, stepmania_profile_t profile,
                         chart_document_t& chart, id_source_t& ids, std::size_t& event_total,
                         const stepmania_parse_options_t& options,
                         std::vector<stepmania_parse_diagnostic_t>& diagnostics) -> void {
    auto effective = song_fields;
    if (profile == stepmania_profile_t::ssc) {
        for (const auto key : {"OFFSET", "BPMS", "STOPS", "DELAYS", "WARPS", "SCROLLS", "SPEEDS"}) {
            const auto overrides = fields_for_key(chart_fields, key);
            if (!overrides.empty()) {
                std::erase_if(effective,
                              [&](const auto& field) { return upper_ascii(field.key) == key; });
                for (const auto* field : overrides)
                    effective.push_back(*field);
            }
        }
    }
    const auto profile_name = profile == stepmania_profile_t::sm ? "sm" : "ssc";
    std::set<std::size_t> chart_source_orders;
    for (const auto& field : chart_fields)
        chart_source_orders.insert(field.order);
    const auto provenance = [&](std::string key, std::string spelling, std::size_t order) {
        const auto scoped_profile =
            profile == stepmania_profile_t::ssc && chart_source_orders.contains(order)
                ? "ssc:chart"
                : profile_name;
        return source_provenance_t{format_family_t::stepmania, scoped_profile, std::move(key),
                                   std::move(spelling), order};
    };
    if (const auto* offset = last_field(effective, "OFFSET")) {
        const auto parsed = decimal_rational(offset->value);
        if (!parsed)
            fail(stepmania_parse_diagnostic_code_t::invalid_timing,
                 "OFFSET must be a finite decimal number", offset->order, offset->offset);
        chart.beat_zero_audio_offset_seconds = {-parsed->numerator, parsed->denominator};
    }
    const auto add_event = [&](const field_t& field) {
        if (++event_total > options.max_events)
            fail(stepmania_parse_diagnostic_code_t::event_limit,
                 "StepMania file exceeds the configured event limit", field.order, field.offset);
    };
    const auto read_pairs = [&](std::string_view key, auto&& callback) {
        const auto* field = last_field(effective, key);
        if (!field || trim(field->value).empty())
            return;
        for (const auto item : split(field->value, ',')) {
            const auto parts = split(item, '=');
            if (parts.size() != 2)
                fail(stepmania_parse_diagnostic_code_t::invalid_timing,
                     std::string(key) + " entries must have beat=value form", field->order,
                     field->offset);
            const auto beat = decimal_rational(parts[0]);
            const auto value = decimal_rational(parts[1]);
            if (!beat || !value)
                fail(stepmania_parse_diagnostic_code_t::invalid_timing,
                     std::string(key) + " entry contains an invalid decimal", field->order,
                     field->offset);
            const auto row = position_row(*beat, chart.divisions_per_standard_measure, field->order,
                                          field->offset);
            callback(*beat, *value, row, *field);
            add_event(*field);
        }
    };

    struct bpm_source_t {
        signed_rational_t beat;
        signed_rational_t bpm;
        std::uint64_t row;
        const field_t* field;
    };
    std::vector<bpm_source_t> source_bpms;
    read_pairs("BPMS", [&](auto beat, auto bpm, auto row, const field_t& field) {
        if (bpm.numerator == 0)
            fail(stepmania_parse_diagnostic_code_t::invalid_timing, "BPM cannot be zero",
                 field.order, field.offset);
        source_bpms.push_back({beat, bpm, row, &field});
    });
    std::stable_sort(source_bpms.begin(), source_bpms.end(),
                     [](const auto& left, const auto& right) {
                         const auto left_value = static_cast<long double>(left.beat.numerator) /
                                                 static_cast<long double>(left.beat.denominator);
                         const auto right_value = static_cast<long double>(right.beat.numerator) /
                                                  static_cast<long double>(right.beat.denominator);
                         return left_value < right_value;
                     });
    bool reported_negative_bpm{};
    for (std::size_t index = 0; index < source_bpms.size(); ++index) {
        const auto& entry = source_bpms[index];
        if (entry.bpm.numerator > 0) {
            chart.bpms.push_back(
                {.id = ids.take<object_id_t>(),
                 .position = local_position(entry.row, chart.divisions_per_standard_measure),
                 .beats_per_minute = entry.bpm,
                 .provenance = provenance("BPMS", "", entry.field->order)});
            continue;
        }
        if (options.source_implementation_profile !=
                stepmania_implementation_profile_t::unspecified &&
            options.source_implementation_profile !=
                stepmania_implementation_profile_t::legacy_negative_bpm)
            fail(stepmania_parse_diagnostic_code_t::implementation_profile_mismatch,
                 "Negative BPM warp syntax requires the legacy_negative_bpm source profile",
                 entry.field->order, entry.field->offset);
        auto boundary = index + 1;
        while (boundary < source_bpms.size() && source_bpms[boundary].bpm.numerator <= 0)
            ++boundary;
        if (boundary == source_bpms.size() || source_bpms[boundary].row <= entry.row)
            fail(stepmania_parse_diagnostic_code_t::invalid_timing,
                 "Legacy negative BPM region needs a later positive BPM boundary",
                 entry.field->order, entry.field->offset);
        chart.warps.push_back(
            {.id = ids.take<object_id_t>(),
             .span = {local_position(entry.row, chart.divisions_per_standard_measure),
                      local_position(source_bpms[boundary].row,
                                     chart.divisions_per_standard_measure)},
             .source_encoding = warp_provenance_t::negative_bpm,
             .provenance = provenance("BPMS", "negative BPM region", entry.field->order)});
        if (!reported_negative_bpm) {
            diagnostics.push_back(
                {diagnostic_severity_t::warning,
                 stepmania_parse_diagnostic_code_t::legacy_warp_encoding,
                 "Legacy negative BPM timing was canonicalized to beat-domain warps; additional "
                 "events of this encoding are summarized here",
                 entry.field->order, entry.field->offset});
            reported_negative_bpm = true;
        }
        index = boundary - 1;
    }
    const auto bpm_at_row = [&](std::uint64_t row) -> std::optional<signed_rational_t> {
        std::optional<signed_rational_t> bpm;
        for (const auto& entry : source_bpms) {
            if (entry.row > row)
                break;
            if (entry.bpm.numerator > 0)
                bpm = entry.bpm;
        }
        return bpm;
    };
    bool reported_negative_stop{};
    read_pairs("STOPS", [&](auto, auto duration, auto row, const field_t& field) {
        if (duration.numerator >= 0) {
            chart.pauses.push_back(
                {.id = ids.take<object_id_t>(),
                 .position = local_position(row, chart.divisions_per_standard_measure),
                 .kind = pause_kind_t::stop,
                 .duration = duration,
                 .source_unit = pause_unit_t::seconds,
                 .provenance = provenance("STOPS", "", field.order)});
            return;
        }
        if (options.source_implementation_profile !=
                stepmania_implementation_profile_t::unspecified &&
            options.source_implementation_profile !=
                stepmania_implementation_profile_t::legacy_negative_stop)
            fail(stepmania_parse_diagnostic_code_t::implementation_profile_mismatch,
                 "Negative STOP warp syntax requires the legacy_negative_stop source profile",
                 field.order, field.offset);
        const auto bpm = bpm_at_row(row);
        if (!bpm || bpm->numerator <= 0)
            fail(stepmania_parse_diagnostic_code_t::invalid_timing,
                 "Legacy negative STOP warp needs a positive effective BPM", field.order,
                 field.offset);
        const auto span = exact_product_ratio({static_cast<std::uint64_t>(-duration.numerator),
                                               static_cast<std::uint64_t>(bpm->numerator),
                                               chart.divisions_per_standard_measure},
                                              {duration.denominator, bpm->denominator, 240});
        if (!span || *span == 0 || row > std::numeric_limits<std::uint64_t>::max() - *span)
            fail(stepmania_parse_diagnostic_code_t::position_not_representable,
                 "Legacy negative STOP warp length is not exactly representable", field.order,
                 field.offset);
        chart.warps.push_back(
            {.id = ids.take<object_id_t>(),
             .span = {local_position(row, chart.divisions_per_standard_measure),
                      local_position(row + *span, chart.divisions_per_standard_measure)},
             .source_encoding = warp_provenance_t::negative_stop,
             .provenance = provenance("STOPS", "negative STOP", field.order)});
        if (!reported_negative_stop) {
            diagnostics.push_back(
                {diagnostic_severity_t::warning,
                 stepmania_parse_diagnostic_code_t::legacy_warp_encoding,
                 "Legacy negative STOP timing was canonicalized to beat-domain warps; additional "
                 "events of this encoding are summarized here",
                 field.order, field.offset});
            reported_negative_stop = true;
        }
    });
    read_pairs("DELAYS", [&](auto, auto duration, auto row, const field_t& field) {
        if (duration.numerator < 0)
            fail(stepmania_parse_diagnostic_code_t::invalid_timing,
                 "DELAYS durations cannot be negative", field.order, field.offset);
        chart.pauses.push_back(
            {.id = ids.take<object_id_t>(),
             .position = local_position(row, chart.divisions_per_standard_measure),
             .kind = pause_kind_t::delay,
             .duration = duration,
             .source_unit = pause_unit_t::seconds,
             .provenance = provenance("DELAYS", "", field.order)});
    });
    read_pairs("WARPS", [&](auto, auto length, auto row, const field_t& field) {
        if (options.source_implementation_profile !=
                stepmania_implementation_profile_t::unspecified &&
            options.source_implementation_profile !=
                stepmania_implementation_profile_t::native_warps)
            fail(stepmania_parse_diagnostic_code_t::implementation_profile_mismatch,
                 "WARPS syntax requires the native_warps source profile", field.order,
                 field.offset);
        if (length.numerator <= 0)
            fail(stepmania_parse_diagnostic_code_t::invalid_timing,
                 "WARPS lengths must be positive", field.order, field.offset);
        const auto span =
            position_row(length, chart.divisions_per_standard_measure, field.order, field.offset);
        if (row > std::numeric_limits<std::uint64_t>::max() - span)
            fail(stepmania_parse_diagnostic_code_t::position_not_representable,
                 "Warp endpoint overflows the editor position range", field.order, field.offset);
        chart.warps.push_back(
            {.id = ids.take<object_id_t>(),
             .span = {local_position(row, chart.divisions_per_standard_measure),
                      local_position(row + span, chart.divisions_per_standard_measure)},
             .source_encoding = warp_provenance_t::native,
             .provenance = provenance("WARPS", "", field.order)});
    });
    read_pairs("SCROLLS", [&](auto, auto multiplier, auto row, const field_t& field) {
        chart.scrolls.push_back(
            {.id = ids.take<object_id_t>(),
             .position = local_position(row, chart.divisions_per_standard_measure),
             .multiplier = multiplier,
             .provenance = provenance("SCROLLS", "", field.order)});
    });
    if (const auto* field = last_field(effective, "SPEEDS"); field && !trim(field->value).empty()) {
        for (const auto item : split(field->value, ',')) {
            const auto parts = split(item, '=');
            if (parts.size() != 4)
                fail(stepmania_parse_diagnostic_code_t::invalid_timing,
                     "SPEEDS entries require beat=ratio=duration=unit", field->order,
                     field->offset);
            const auto beat = decimal_rational(parts[0]);
            const auto ratio = decimal_rational(parts[1]);
            const auto duration = decimal_rational(parts[2]);
            std::uint64_t unit{};
            if (!beat || !ratio || !duration || duration->numerator < 0 ||
                !parse_unsigned(parts[3], unit) || unit > 1)
                fail(stepmania_parse_diagnostic_code_t::invalid_timing,
                     "SPEEDS contains an invalid beat, multiplier, duration, or unit", field->order,
                     field->offset);
            const auto row = position_row(*beat, chart.divisions_per_standard_measure, field->order,
                                          field->offset);
            chart.speeds.push_back(
                {.id = ids.take<object_id_t>(),
                 .position = local_position(row, chart.divisions_per_standard_measure),
                 .multiplier = *ratio,
                 .transition_beats = {static_cast<std::uint64_t>(duration->numerator),
                                      duration->denominator},
                 .transition_is_seconds = unit == 1,
                 .provenance = provenance("SPEEDS", "", field->order)});
            add_event(*field);
        }
    }
    (void)diagnostics;
}

} // namespace

auto stepmania_parse_result_t::has_errors() const -> bool {
    return has_error(diagnostics);
}

auto parse_stepmania_bytes(std::span<const std::byte> bytes, stepmania_parse_options_t options)
    -> stepmania_parse_result_t {
    stepmania_parse_result_t result;
    if (bytes.size() > options.max_source_bytes) {
        result.diagnostics.push_back({diagnostic_severity_t::error,
                                      stepmania_parse_diagnostic_code_t::source_limit,
                                      "StepMania source exceeds the configured byte limit", 0, 0});
        return result;
    }
    const auto* data = reinterpret_cast<const char*>(bytes.data());
    auto source = std::string_view(data, bytes.size());
    if (const auto invalid = valid_utf8(source)) {
        result.diagnostics.push_back({diagnostic_severity_t::error,
                                      stepmania_parse_diagnostic_code_t::invalid_encoding,
                                      "StepMania source is not valid UTF-8", 0, *invalid});
        return result;
    }
    if (source.starts_with("\xef\xbb\xbf"))
        source.remove_prefix(3);
    try {
        const auto fields = parse_source(source, options.max_fields);
        std::vector<field_t> song_fields;
        std::vector<chart_source_t> charts;
        if (options.profile == stepmania_profile_t::sm) {
            for (const auto& field : fields) {
                if (upper_ascii(field.key) == "NOTES") {
                    if (charts.size() >= options.max_charts)
                        fail(stepmania_parse_diagnostic_code_t::chart_limit,
                             "StepMania file exceeds the configured chart limit", field.order,
                             field.offset);
                    charts.push_back({{field}});
                } else
                    song_fields.push_back(field);
            }
        } else {
            chart_source_t* active{};
            for (const auto& field : fields) {
                const auto key = upper_ascii(field.key);
                if (key == "NOTEDATA") {
                    if (charts.size() >= options.max_charts)
                        fail(stepmania_parse_diagnostic_code_t::chart_limit,
                             "SSC file exceeds the configured chart limit", field.order,
                             field.offset);
                    charts.push_back({});
                    active = &charts.back();
                    active->fields.push_back(field);
                    continue;
                }
                if (active) {
                    active->fields.push_back(field);
                } else if (key == "NOTES") {
                    if (charts.size() >= options.max_charts)
                        fail(stepmania_parse_diagnostic_code_t::chart_limit,
                             "SSC file exceeds the configured chart limit", field.order,
                             field.offset);
                    charts.push_back({{field}});
                } else {
                    song_fields.push_back(field);
                }
            }
        }
        if (charts.empty())
            fail(stepmania_parse_diagnostic_code_t::invalid_notes,
                 "StepMania source contains no chart note maps", fields.size());
        const auto source_bytes =
            std::make_shared<const std::vector<std::byte>>(bytes.begin(), bytes.end());
        song_document_t song;
        song.id = song_id_t{1};
        song.metadata.format_groups.push_back(
            as_metadata_group(song_fields, options.profile, source_bytes));
        if (const auto* title = last_field(song_fields, "TITLE"))
            song.metadata.title = std::string(trim(title->value));
        if (const auto* artist = last_field(song_fields, "ARTIST"))
            song.metadata.artist = std::string(trim(artist->value));
        id_source_t ids;
        std::size_t total_events{};
        for (std::size_t chart_index = 0; chart_index < charts.size(); ++chart_index) {
            const auto& chart_source = charts[chart_index];
            const auto [step_type, description, difficulty, meter_text, radar] =
                parse_note_source(chart_source, options.profile, chart_index);
            const auto lane_count = note_column_count(step_type);
            if (!lane_count)
                fail(stepmania_parse_diagnostic_code_t::unsupported_step_type,
                     "Unsupported StepMania step type: " + step_type, chart_index,
                     last_field(chart_source.fields, "NOTES")->offset);
            chart_document_t chart;
            chart.id = chart_id_t{chart_index + 1};
            chart.source_family = format_family_t::stepmania;
            chart.divisions_per_standard_measure = default_divisions_per_standard_measure;
            if (chart.divisions_per_standard_measure > options.max_divisions_per_standard_measure)
                fail(stepmania_parse_diagnostic_code_t::division_limit,
                     "Default StepMania editor grid exceeds the configured division limit",
                     chart_index, last_field(chart_source.fields, "NOTES")->offset);
            const auto* chart_name = last_field(chart_source.fields, "CHARTNAME");
            chart.metadata.name = chart_name ? std::string(trim(chart_name->value)) : description;
            if (options.profile == stepmania_profile_t::ssc) {
                if (const auto* credit = last_field(chart_source.fields, "CREDIT"))
                    chart.metadata.author = std::string(trim(credit->value));
            } else if (const auto* credit = last_field(song_fields, "CREDIT")) {
                chart.metadata.author = std::string(trim(credit->value));
            }
            chart.metadata.format_groups.push_back(
                as_metadata_group(chart_source.fields, options.profile, source_bytes));
            if (options.profile == stepmania_profile_t::sm) {
                const auto* source_notes = last_field(chart_source.fields, "NOTES");
                const auto source_order = source_notes ? source_notes->order : chart_index;
                auto& fields_group = chart.metadata.format_groups.back();
                fields_group.ordered_fields.emplace_back("STEPSTYPE", step_type);
                fields_group.ordered_fields.emplace_back("DESCRIPTION", description);
                fields_group.ordered_fields.emplace_back("DIFFICULTY", difficulty);
                fields_group.ordered_fields.emplace_back("METER", meter_text);
                fields_group.ordered_fields.emplace_back("RADARVALUES", radar);
                fields_group.ordered_field_source_orders.insert(
                    fields_group.ordered_field_source_orders.end(), 5, source_order);
            }
            if (const auto parsed_rating = decimal_rational(meter_text);
                parsed_rating && parsed_rating->numerator >= 0)
                chart.metadata.rating = *parsed_rating;
            parse_timing_fields(song_fields, chart_source.fields, options.profile, chart, ids,
                                total_events, options, result.diagnostics);

            const auto* note_field = last_field(chart_source.fields, "NOTES");
            std::string_view note_map = note_field->value;
            if (options.profile == stepmania_profile_t::sm) {
                const auto last_colon = note_field->value.rfind(':');
                if (last_colon == std::string::npos)
                    fail(stepmania_parse_diagnostic_code_t::invalid_notes,
                         "SM #NOTES is missing its note map", note_field->order,
                         note_field->offset);
                note_map = std::string_view(note_field->value).substr(last_colon + 1);
            }
            const auto row_maps =
                parse_row_map(note_map, *lane_count, chart.divisions_per_standard_measure,
                              options.max_measures, options.max_events, note_field->order);
            auto layout = make_layout(step_type, *lane_count, ids);
            chart.lane_layouts.push_back({.id = ids.take<object_id_t>(),
                                          .position = {measure_id_t{1}, 0},
                                          .layout = layout});
            std::vector<measure_chunk_t> measures;
            measures.reserve(row_maps.size());
            for (std::size_t measure = 0; measure < row_maps.size(); ++measure)
                measures.push_back(
                    {.id = measure_id_t{measure + 1}, .definition = {{1, 1}, meter_t{4, 4}}});
            if (measures.empty())
                measures.push_back({.id = measure_id_t{1}, .definition = {{1, 1}, meter_t{4, 4}}});
            struct pending_hold_t {
                note_kind_t kind;
                local_position_t position;
            };
            std::map<std::uint32_t, pending_hold_t> open_holds;
            for (std::size_t measure_index = 0; measure_index < row_maps.size(); ++measure_index) {
                for (const auto& [row, symbols] : row_maps[measure_index]) {
                    for (std::uint32_t lane_index = 0; lane_index < *lane_count; ++lane_index) {
                        const auto symbol = symbols[lane_index];
                        const auto position =
                            local_position(static_cast<std::uint64_t>(measure_index) *
                                                   chart.divisions_per_standard_measure +
                                               row,
                                           chart.divisions_per_standard_measure);
                        if (symbol == '0')
                            continue;
                        if (++total_events > options.max_events)
                            fail(stepmania_parse_diagnostic_code_t::event_limit,
                                 "StepMania file exceeds the configured event limit",
                                 note_field->order, note_field->offset);
                        if (symbol == '2' || symbol == '4') {
                            if (open_holds.contains(lane_index))
                                fail(stepmania_parse_diagnostic_code_t::invalid_hold,
                                     "A hold head appears before the previous hold tail",
                                     note_field->order, note_field->offset);
                            open_holds.emplace(lane_index,
                                               pending_hold_t{symbol == '4' ? note_kind_t::roll
                                                                            : note_kind_t::hold,
                                                              position});
                            continue;
                        }
                        if (symbol == '3') {
                            const auto found = open_holds.find(lane_index);
                            if (found == open_holds.end())
                                fail(stepmania_parse_diagnostic_code_t::invalid_hold,
                                     "Hold tail has no matching head in its lane",
                                     note_field->order, note_field->offset);
                            measures[measure_index].notes.push_back(
                                {.id = ids.take<object_id_t>(),
                                 .position = found->second.position,
                                 .lane = layout.lanes[lane_index].id,
                                 .kind = found->second.kind,
                                 .end = position,
                                 .provenance = {format_family_t::stepmania,
                                                options.profile == stepmania_profile_t::sm ? "sm"
                                                                                           : "ssc",
                                                "NOTES", "hold", note_field->order}});
                            open_holds.erase(found);
                            continue;
                        }
                        auto kind = note_kind_t::tap;
                        if (symbol == 'M')
                            kind = note_kind_t::mine;
                        else if (symbol == 'L')
                            kind = note_kind_t::lift;
                        else if (symbol == 'F')
                            kind = note_kind_t::fake;
                        measures[measure_index].notes.push_back(
                            {.id = ids.take<object_id_t>(),
                             .position = position,
                             .lane = layout.lanes[lane_index].id,
                             .kind = kind,
                             .provenance = {format_family_t::stepmania,
                                            options.profile == stepmania_profile_t::sm ? "sm"
                                                                                       : "ssc",
                                            "NOTES", std::string(1, symbol), note_field->order}});
                    }
                }
            }
            if (!open_holds.empty())
                fail(stepmania_parse_diagnostic_code_t::invalid_hold,
                     "StepMania chart contains a hold head with no tail", note_field->order,
                     note_field->offset);
            std::uint64_t max_measure{static_cast<std::uint64_t>(measures.size())};
            const auto extend_for = [&](local_position_t position) {
                max_measure = std::max(max_measure, position.measure.value);
            };
            for (const auto& event : chart.bpms)
                extend_for(event.position);
            for (const auto& event : chart.pauses)
                extend_for(event.position);
            for (const auto& event : chart.scrolls)
                extend_for(event.position);
            for (const auto& event : chart.speeds)
                extend_for(event.position);
            for (const auto& event : chart.warps) {
                extend_for(event.span.begin);
                extend_for(event.span.end);
            }
            if (max_measure > options.max_measures)
                fail(stepmania_parse_diagnostic_code_t::measure_limit,
                     "StepMania timing exceeds the configured measure limit", chart_index,
                     note_field->offset);
            while (measures.size() < max_measure)
                measures.push_back({.id = measure_id_t{measures.size() + 1},
                                    .definition = {{1, 1}, meter_t{4, 4}}});
            chart.measures = std::move(measures);
            (void)difficulty;
            (void)radar;
            song.charts.push_back(std::move(chart));
        }
        for (const auto key : {"TITLE", "ARTIST"}) {
            const auto found = fields_for_key(song_fields, key);
            if (found.size() > 1)
                for (std::size_t i = 0; i + 1 < found.size(); ++i)
                    result.diagnostics.push_back(
                        {diagnostic_severity_t::warning,
                         stepmania_parse_diagnostic_code_t::duplicate_field,
                         std::string("Duplicate ") + key +
                             " retained in source metadata; last value wins",
                         found[i]->order, found[i]->offset});
        }
        if (has_error(result.diagnostics))
            return result;
        result.song = std::move(song);
    } catch (const failure_t& failure) {
        result.diagnostics.push_back({diagnostic_severity_t::error, failure.code, failure.message,
                                      failure.order, failure.offset});
    } catch (const std::bad_alloc&) {
        result.diagnostics.push_back(
            {diagnostic_severity_t::error, stepmania_parse_diagnostic_code_t::source_limit,
             "Insufficient memory within configured parser limits", 0, std::nullopt});
    }
    return result;
}

namespace {

auto scaled_ratio(std::array<std::uint64_t, 3> numerator, std::array<std::uint64_t, 3> denominator)
    -> std::optional<signed_rational_t> {
    for (auto& top : numerator)
        for (auto& bottom : denominator) {
            const auto common = std::gcd(top, bottom);
            top /= common;
            bottom /= common;
        }
    std::uint64_t top{1};
    std::uint64_t bottom{1};
    for (const auto factor : numerator) {
        if (factor != 0 && top > std::numeric_limits<std::uint64_t>::max() / factor)
            return std::nullopt;
        top *= factor;
    }
    for (const auto factor : denominator) {
        if (factor != 0 && bottom > std::numeric_limits<std::uint64_t>::max() / factor)
            return std::nullopt;
        bottom *= factor;
    }
    if (bottom == 0 || top > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        return std::nullopt;
    return signed_rational_t{static_cast<std::int64_t>(top), bottom};
}

auto rational_decimal(signed_rational_t value) -> std::optional<std::string> {
    if (value.denominator == 0)
        return std::nullopt;
    const auto magnitude = value.numerator < 0
                               ? static_cast<std::uint64_t>(-(value.numerator + 1)) + 1
                               : static_cast<std::uint64_t>(value.numerator);
    std::uint64_t scale{1};
    for (std::uint32_t digits = 0; digits <= 18; ++digits) {
        if (const auto scaled = multiply_divide_exact(magnitude, scale, value.denominator)) {
            std::string text;
            if (digits == 0) {
                text = std::to_string(*scaled);
            } else {
                const auto whole = *scaled / scale;
                auto fraction = std::to_string(*scaled % scale);
                if (fraction.size() < digits)
                    fraction.insert(0, digits - fraction.size(), '0');
                while (!fraction.empty() && fraction.back() == '0')
                    fraction.pop_back();
                text = std::to_string(whole);
                if (!fraction.empty())
                    text += "." + fraction;
            }
            if (value.numerator < 0 && magnitude != 0)
                text.insert(text.begin(), '-');
            return text;
        }
        if (digits != 18) {
            if (scale > std::numeric_limits<std::uint64_t>::max() / 10)
                break;
            scale *= 10;
        }
    }
    return std::nullopt;
}

auto beat_at_row(std::uint64_t row, std::uint64_t divisions) -> std::optional<signed_rational_t> {
    const auto common = std::gcd(divisions, std::uint64_t{4});
    const auto denominator = divisions / common;
    const auto multiplier = std::uint64_t{4} / common;
    if (multiplier != 0 &&
        row > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) / multiplier)
        return std::nullopt;
    auto numerator = row * multiplier;
    const auto reduce = std::gcd(numerator, denominator);
    numerator /= reduce;
    return signed_rational_t{static_cast<std::int64_t>(numerator), denominator / reduce};
}

auto absolute_row_for(const chart_document_t& chart, local_position_t position)
    -> std::optional<std::uint64_t> {
    if (position.row >= chart.divisions_per_standard_measure)
        return std::nullopt;
    const auto found = std::ranges::find(chart.measures, position.measure, &measure_chunk_t::id);
    if (found == chart.measures.end())
        return std::nullopt;
    const auto index = static_cast<std::uint64_t>(std::distance(chart.measures.begin(), found));
    if (index > (std::numeric_limits<std::uint64_t>::max() - position.row) /
                    chart.divisions_per_standard_measure)
        return std::nullopt;
    return index * chart.divisions_per_standard_measure + position.row;
}

auto valid_tag_key(std::string_view key) -> bool {
    if (key.empty() || key.size() > 64 ||
        !((key.front() >= 'A' && key.front() <= 'Z') || (key.front() >= 'a' && key.front() <= 'z')))
        return false;
    return std::ranges::all_of(key, [](char character) {
        return (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') ||
               (character >= '0' && character <= '9') || character == '_';
    });
}

auto valid_tag_value(std::string_view value) -> bool {
    return value.find(';') == std::string_view::npos && value.find('\0') == std::string_view::npos;
}

auto emit_field(std::string& output, std::string_view key, std::string_view value,
                std::vector<stepmania_write_diagnostic_t>& diagnostics,
                std::size_t max_output_bytes) -> void {
    if (!valid_tag_key(key) || !valid_tag_value(value)) {
        diagnostics.push_back(
            {diagnostic_severity_t::error, stepmania_write_diagnostic_code_t::invalid_metadata,
             "StepMania field contains an unsafe tag key or value delimiter", std::string(key)});
        return;
    }
    constexpr std::size_t terminator_size{3};
    if (output.size() > max_output_bytes || key.size() > max_output_bytes - output.size() ||
        value.size() > max_output_bytes - output.size() - key.size() ||
        terminator_size > max_output_bytes - output.size() - key.size() - value.size()) {
        diagnostics.push_back(
            {diagnostic_severity_t::error, stepmania_write_diagnostic_code_t::output_limit,
             "StepMania output exceeds the configured byte limit", std::string(key)});
        return;
    }
    output.push_back('#');
    output.append(key);
    output.push_back(':');
    output.append(value);
    output.append(";\n");
}

auto family_group(const std::vector<format_metadata_t>& groups) -> const format_metadata_t* {
    const auto found =
        std::ranges::find(groups, format_family_t::stepmania, &format_metadata_t::family);
    return found == groups.end() ? nullptr : &*found;
}

auto group_value(const format_metadata_t* group, std::string_view key) -> std::string {
    if (!group)
        return {};
    std::string result;
    for (const auto& [name, value] : group->ordered_fields)
        if (upper_ascii(name) == key)
            result = value;
    return std::string(trim(result));
}

auto group_has_field(const format_metadata_t* group, std::string_view key) -> bool {
    return group && std::ranges::any_of(group->ordered_fields, [&](const auto& field) {
               return upper_ascii(field.first) == key;
           });
}

auto append_unknown_fields(std::string& output, const format_metadata_t* group,
                           const std::set<std::string>& modeled, bool preserve,
                           std::vector<stepmania_write_diagnostic_t>& diagnostics,
                           std::size_t max_output_bytes) -> void {
    if (!preserve || !group)
        return;
    std::vector<std::size_t> order(group->ordered_fields.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](auto left, auto right) {
        const auto source_order = [&](auto index) {
            return index < group->ordered_field_source_orders.size()
                       ? group->ordered_field_source_orders[index]
                       : index;
        };
        return source_order(left) < source_order(right);
    });
    for (const auto index : order) {
        const auto& [key, value] = group->ordered_fields[index];
        if (modeled.contains(upper_ascii(key)))
            continue;
        emit_field(output, key, value, diagnostics, max_output_bytes);
    }
}

auto make_timing_fields(const chart_document_t& chart, const stepmania_write_options_t& options,
                        std::vector<stepmania_write_diagnostic_t>& diagnostics)
    -> std::vector<std::pair<std::string, std::string>> {
    using event_t = std::tuple<std::uint64_t, std::size_t, std::string>;
    const auto warp_encoding = [&] {
        switch (options.target_implementation_profile) {
        case stepmania_implementation_profile_t::native_warps:
            return warp_encoding_t::native;
        case stepmania_implementation_profile_t::legacy_negative_bpm:
            return warp_encoding_t::negative_bpm;
        case stepmania_implementation_profile_t::legacy_negative_stop:
            return warp_encoding_t::negative_stop;
        case stepmania_implementation_profile_t::unspecified:
            return options.warp_encoding;
        }
        return options.warp_encoding;
    }();
    bool reported_legacy_conversion{};
    const auto report = [&](stepmania_write_diagnostic_code_t code, std::string message,
                            std::string field,
                            diagnostic_severity_t severity = diagnostic_severity_t::error) {
        if (code == stepmania_write_diagnostic_code_t::legacy_warp_conversion) {
            if (reported_legacy_conversion)
                return;
            reported_legacy_conversion = true;
        }
        diagnostics.push_back({severity, code, std::move(message), std::move(field)});
    };
    if (options.profile == stepmania_profile_t::sm) {
        if (!chart.scrolls.empty())
            report(stepmania_write_diagnostic_code_t::unsupported_feature,
                   "This SM writer profile cannot represent SCROLLS", "SCROLLS");
        if (!chart.speeds.empty())
            report(stepmania_write_diagnostic_code_t::unsupported_feature,
                   "This SM writer profile cannot represent SPEEDS", "SPEEDS");
        if (!chart.warps.empty() && warp_encoding == warp_encoding_t::native)
            report(stepmania_write_diagnostic_code_t::unsupported_feature,
                   "This SM writer profile requires an explicit legacy strategy for warps",
                   "WARPS");
    }
    const auto beat_text = [&](std::uint64_t row,
                               std::string_view field) -> std::optional<std::string> {
        const auto beat = beat_at_row(row, chart.divisions_per_standard_measure);
        const auto text = beat ? rational_decimal(*beat) : std::nullopt;
        if (!text)
            report(stepmania_write_diagnostic_code_t::unrepresentable_position,
                   "Timing beat cannot be written as an exact StepMania decimal",
                   std::string(field));
        return text;
    };
    const auto value_text = [&](signed_rational_t value,
                                std::string_view field) -> std::optional<std::string> {
        const auto text = rational_decimal(value);
        if (!text)
            report(stepmania_write_diagnostic_code_t::unrepresentable_position,
                   "Timing value cannot be written as an exact StepMania decimal",
                   std::string(field));
        return text;
    };
    const auto position_for = [&](local_position_t position,
                                  std::string_view field) -> std::optional<std::uint64_t> {
        const auto row = absolute_row_for(chart, position);
        if (!row)
            report(stepmania_write_diagnostic_code_t::invalid_document,
                   "Timing event position does not reference a chart measure", std::string(field));
        return row;
    };
    std::vector<event_t> bpm_events;
    for (const auto& event : chart.bpms) {
        const auto row = position_for(event.position, "BPMS");
        const auto beat = row ? beat_text(*row, "BPMS") : std::nullopt;
        const auto bpm = value_text(event.beats_per_minute, "BPMS");
        if (row && beat && bpm)
            bpm_events.emplace_back(*row, event.provenance.source_order, *beat + "=" + *bpm);
    }
    std::stable_sort(bpm_events.begin(), bpm_events.end());

    std::vector<std::pair<std::uint64_t, std::uint64_t>> warps;
    for (const auto& event : chart.warps) {
        const auto begin = position_for(event.span.begin, "WARPS");
        const auto end = position_for(event.span.end, "WARPS");
        if (!begin || !end || *end <= *begin) {
            report(stepmania_write_diagnostic_code_t::invalid_document,
                   "Warp must have a positive span within the chart measure table", "WARPS");
            continue;
        }
        warps.emplace_back(*begin, *end);
    }
    std::sort(warps.begin(), warps.end());
    if (warp_encoding == warp_encoding_t::reject && !warps.empty())
        report(stepmania_write_diagnostic_code_t::unsupported_feature,
               "Selected warp policy rejects StepMania warps", "WARPS");
    if (warp_encoding == warp_encoding_t::negative_bpm ||
        warp_encoding == warp_encoding_t::negative_stop) {
        for (std::size_t i = 1; i < warps.size(); ++i) {
            if (warps[i].first <= warps[i - 1].second)
                report(stepmania_write_diagnostic_code_t::unsupported_feature,
                       "Overlapping or adjacent warps cannot be encoded exactly by the selected "
                       "legacy strategy",
                       "WARPS");
        }
        for (const auto& bpm : chart.bpms) {
            const auto row = position_for(bpm.position, "BPMS");
            if (row && std::ranges::any_of(warps, [&](const auto& warp) {
                    return *row > warp.first && *row < warp.second;
                }))
                report(stepmania_write_diagnostic_code_t::unsupported_feature,
                       "A BPM change inside a warp prevents exact legacy encoding", "BPMS");
        }
        for (const auto& pause : chart.pauses) {
            const auto row =
                position_for(pause.position, pause.kind == pause_kind_t::stop ? "STOPS" : "DELAYS");
            if (row && std::ranges::any_of(warps, [&](const auto& warp) {
                    return *row >= warp.first && *row < warp.second;
                }))
                report(stepmania_write_diagnostic_code_t::unsupported_feature,
                       "A pause inside a warp cannot be encoded by this legacy warp strategy",
                       pause.kind == pause_kind_t::stop ? "STOPS" : "DELAYS");
        }
    }
    if (warp_encoding == warp_encoding_t::negative_bpm && !warps.empty()) {
        const auto original = bpm_events;
        const auto current_bpm = [&](std::uint64_t row) -> std::optional<std::string> {
            std::optional<std::string> value;
            for (const auto& event : original) {
                if (std::get<0>(event) > row)
                    break;
                const auto equals = std::get<2>(event).find('=');
                if (equals != std::string::npos)
                    value = std::get<2>(event).substr(equals + 1);
            }
            return value;
        };
        for (const auto& [begin, end] : warps) {
            const auto bpm = current_bpm(begin);
            const auto start_text = beat_text(begin, "BPMS");
            const auto end_text = beat_text(end, "BPMS");
            if (!bpm || !start_text || !end_text) {
                report(stepmania_write_diagnostic_code_t::unsupported_feature,
                       "Legacy negative BPM encoding requires a positive BPM at the warp start",
                       "BPMS");
                continue;
            }
            const auto bpm_value = decimal_rational(*bpm);
            const auto negative =
                bpm_value ? rational_decimal({-bpm_value->numerator, bpm_value->denominator})
                          : std::nullopt;
            if (!negative) {
                report(stepmania_write_diagnostic_code_t::unrepresentable_position,
                       "Warp start BPM cannot be negated exactly", "BPMS");
                continue;
            }
            std::erase_if(bpm_events, [&](const auto& event) {
                return std::get<0>(event) >= begin && std::get<0>(event) < end;
            });
            bpm_events.emplace_back(begin, 0, *start_text + "=" + *negative);
            if (std::ranges::none_of(bpm_events,
                                     [&](const auto& event) { return std::get<0>(event) == end; }))
                bpm_events.emplace_back(end, 1, *end_text + "=" + *bpm);
            report(stepmania_write_diagnostic_code_t::legacy_warp_conversion,
                   "Warp emitted through legacy negative BPM encoding", "WARPS",
                   diagnostic_severity_t::warning);
        }
        std::stable_sort(bpm_events.begin(), bpm_events.end());
    }
    std::vector<event_t> stop_events;
    std::vector<event_t> delay_events;
    for (const auto& event : chart.pauses) {
        if (event.source_unit != pause_unit_t::seconds) {
            report(stepmania_write_diagnostic_code_t::unsupported_feature,
                   "StepMania export requires pause durations in seconds", "STOPS/DELAYS");
            continue;
        }
        const auto row =
            position_for(event.position, event.kind == pause_kind_t::stop ? "STOPS" : "DELAYS");
        const auto beat =
            row ? beat_text(*row, event.kind == pause_kind_t::stop ? "STOPS" : "DELAYS")
                : std::nullopt;
        const auto duration =
            value_text(event.duration, event.kind == pause_kind_t::stop ? "STOPS" : "DELAYS");
        if (!row || !beat || !duration)
            continue;
        (event.kind == pause_kind_t::stop ? stop_events : delay_events)
            .emplace_back(*row, event.provenance.source_order, *beat + "=" + *duration);
    }
    if (warp_encoding == warp_encoding_t::negative_stop) {
        for (const auto& [begin, end] : warps) {
            std::optional<std::string> bpm_text;
            std::uint64_t bpm_row{};
            for (const auto& event : chart.bpms) {
                const auto row = absolute_row_for(chart, event.position);
                if (!row || *row > begin)
                    continue;
                if (!bpm_text || *row >= bpm_row) {
                    bpm_row = *row;
                    bpm_text = value_text(event.beats_per_minute, "BPMS");
                }
            }
            const auto start_text = beat_text(begin, "STOPS");
            const auto duration = bpm_text ? ([&]() -> std::optional<std::string> {
                const auto bpm = decimal_rational(*bpm_text);
                if (!bpm || bpm->numerator <= 0)
                    return std::nullopt;
                auto seconds = scaled_ratio({end - begin, 240, bpm->denominator},
                                            {chart.divisions_per_standard_measure,
                                             static_cast<std::uint64_t>(bpm->numerator), 1});
                if (!seconds)
                    return std::nullopt;
                seconds->numerator = -seconds->numerator;
                return rational_decimal(*seconds);
            })()
                                           : std::nullopt;
            if (!start_text || !duration) {
                report(stepmania_write_diagnostic_code_t::unsupported_feature,
                       "Warp cannot be converted to an exact negative STOP duration", "STOPS");
                continue;
            }
            stop_events.emplace_back(begin, 0, *start_text + "=" + *duration);
            report(stepmania_write_diagnostic_code_t::legacy_warp_conversion,
                   "Warp emitted through legacy negative STOP encoding", "WARPS",
                   diagnostic_severity_t::warning);
        }
    }
    const auto serialize_events = [&](std::vector<event_t>& events, std::string key,
                                      std::vector<std::pair<std::string, std::string>>& output) {
        if (events.empty())
            return;
        std::stable_sort(events.begin(), events.end());
        std::string value;
        for (const auto& event : events) {
            if (!value.empty())
                value.push_back(',');
            value += std::get<2>(event);
        }
        output.emplace_back(std::move(key), std::move(value));
    };
    std::vector<std::pair<std::string, std::string>> result;
    if (chart.beat_zero_audio_offset_seconds.numerator ==
        std::numeric_limits<std::int64_t>::min()) {
        report(stepmania_write_diagnostic_code_t::unrepresentable_position,
               "Beat-zero offset cannot be negated in the supported rational range", "OFFSET");
    } else if (const auto offset = value_text({-chart.beat_zero_audio_offset_seconds.numerator,
                                               chart.beat_zero_audio_offset_seconds.denominator},
                                              "OFFSET")) {
        result.emplace_back("OFFSET", *offset);
    }
    serialize_events(bpm_events, "BPMS", result);
    serialize_events(stop_events, "STOPS", result);
    serialize_events(delay_events, "DELAYS", result);
    if (warp_encoding == warp_encoding_t::native && !warps.empty()) {
        std::vector<event_t> warp_events;
        for (const auto& [begin, end] : warps) {
            const auto start = beat_text(begin, "WARPS");
            const auto length = beat_at_row(end - begin, chart.divisions_per_standard_measure);
            const auto duration = length ? rational_decimal(*length) : std::nullopt;
            if (start && duration)
                warp_events.emplace_back(begin, 0, *start + "=" + *duration);
        }
        serialize_events(warp_events, "WARPS", result);
    }
    std::vector<event_t> scroll_events;
    for (const auto& event : chart.scrolls) {
        const auto row = position_for(event.position, "SCROLLS");
        const auto beat = row ? beat_text(*row, "SCROLLS") : std::nullopt;
        const auto value = value_text(event.multiplier, "SCROLLS");
        if (row && beat && value)
            scroll_events.emplace_back(*row, event.provenance.source_order, *beat + "=" + *value);
    }
    serialize_events(scroll_events, "SCROLLS", result);
    std::vector<event_t> speed_events;
    for (const auto& event : chart.speeds) {
        const auto row = position_for(event.position, "SPEEDS");
        const auto beat = row ? beat_text(*row, "SPEEDS") : std::nullopt;
        const auto multiplier = value_text(event.multiplier, "SPEEDS");
        if (event.transition_beats.numerator >
            static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            report(stepmania_write_diagnostic_code_t::unrepresentable_position,
                   "SPEEDS duration exceeds the supported signed rational range", "SPEEDS");
            continue;
        }
        const auto duration =
            rational_decimal({static_cast<std::int64_t>(event.transition_beats.numerator),
                              event.transition_beats.denominator});
        if (!duration)
            report(stepmania_write_diagnostic_code_t::unrepresentable_position,
                   "SPEEDS duration cannot be written as an exact decimal", "SPEEDS");
        if (row && beat && multiplier && duration)
            speed_events.emplace_back(*row, event.provenance.source_order,
                                      *beat + "=" + *multiplier + "=" + *duration + "=" +
                                          (event.transition_is_seconds ? "1" : "0"));
    }
    serialize_events(speed_events, "SPEEDS", result);
    return result;
}

auto note_map_text(const chart_document_t& chart,
                   std::vector<stepmania_write_diagnostic_t>& diagnostics,
                   std::size_t max_rows_per_measure, std::size_t max_output_bytes)
    -> std::optional<std::string> {
    const auto report = [&](std::string message) {
        diagnostics.push_back({diagnostic_severity_t::error,
                               stepmania_write_diagnostic_code_t::invalid_note_layout,
                               std::move(message), "NOTES"});
    };
    if (chart.lane_layouts.empty()) {
        report("Chart has no StepMania lane layout");
        return std::nullopt;
    }
    const auto& layout = chart.lane_layouts.front().layout;
    if (layout.lanes.empty() || layout.lanes.size() > 10) {
        report("StepMania note maps require between one and ten lanes");
        return std::nullopt;
    }
    if (std::ranges::any_of(chart.lane_layouts, [&](const auto& segment) {
            return segment.layout.lanes.size() != layout.lanes.size() ||
                   std::ranges::any_of(segment.layout.lanes, [&](const auto& lane) {
                       return std::ranges::find(layout.lanes, lane.id, &lane_definition_t::id) ==
                              layout.lanes.end();
                   });
        })) {
        report("Changing lane layouts are not representable by one StepMania chart");
        return std::nullopt;
    }
    for (const auto& measure : chart.measures) {
        if (measure.definition.length != rational_t{1, 1}) {
            diagnostics.push_back(
                {diagnostic_severity_t::error,
                 stepmania_write_diagnostic_code_t::unsupported_feature,
                 "Nonstandard measure lengths are not representable in StepMania note maps",
                 "NOTES"});
            return std::nullopt;
        }
    }
    const auto divisions = chart.divisions_per_standard_measure;
    if (divisions == 0) {
        report("Chart has zero editor divisions per measure");
        return std::nullopt;
    }
    if (chart.measures.size() > 65'536 || chart.measures.size() > max_output_bytes) {
        diagnostics.push_back({diagnostic_severity_t::error,
                               stepmania_write_diagnostic_code_t::output_limit,
                               "Chart measure count exceeds the StepMania writer cap", "NOTES"});
        return std::nullopt;
    }
    struct cell_event_t {
        std::uint64_t row{};
        std::uint32_t lane{};
        char symbol{};
    };
    std::vector<std::vector<cell_event_t>> by_measure(
        std::max<std::size_t>(1, chart.measures.size()));
    const auto lane_index = [&](lane_id_t lane) -> std::optional<std::uint32_t> {
        const auto found = std::ranges::find(layout.lanes, lane, &lane_definition_t::id);
        if (found == layout.lanes.end())
            return std::nullopt;
        return static_cast<std::uint32_t>(std::distance(layout.lanes.begin(), found));
    };
    const auto put = [&](local_position_t position, lane_id_t lane, char symbol) -> bool {
        const auto absolute = absolute_row_for(chart, position);
        const auto column = lane_index(lane);
        if (!absolute || !column)
            return false;
        const auto measure = static_cast<std::size_t>(*absolute / divisions);
        if (measure >= 65'536 || measure >= max_output_bytes)
            return false;
        if (by_measure.size() <= measure)
            by_measure.resize(measure + 1);
        by_measure[measure].push_back({*absolute % divisions, *column, symbol});
        return true;
    };
    for (const auto& measure : chart.measures)
        for (const auto& note : measure.notes) {
            char head{};
            switch (note.kind) {
            case note_kind_t::tap:
                head = '1';
                break;
            case note_kind_t::hold:
                head = '2';
                break;
            case note_kind_t::roll:
                head = '4';
                break;
            case note_kind_t::mine:
                head = 'M';
                break;
            case note_kind_t::lift:
                head = 'L';
                break;
            case note_kind_t::fake:
                head = 'F';
                break;
            case note_kind_t::invisible:
                report("Invisible notes have no faithful StepMania note-map encoding");
                return std::nullopt;
            default:
                report("Unknown note kind has no StepMania note-map encoding");
                return std::nullopt;
            }
            const bool is_hold = note.kind == note_kind_t::hold || note.kind == note_kind_t::roll;
            if (is_hold != note.end.has_value()) {
                report("Only hold and roll notes may carry a StepMania tail position");
                return std::nullopt;
            }
            if (!put(note.position, note.lane, head)) {
                report("Note does not reference a StepMania chart measure or lane");
                return std::nullopt;
            }
            if (note.end && !put(*note.end, note.lane, '3')) {
                report("Hold tail does not reference a StepMania chart measure or lane");
                return std::nullopt;
            }
            if (note.sound)
                diagnostics.push_back(
                    {diagnostic_severity_t::warning,
                     stepmania_write_diagnostic_code_t::unsupported_feature,
                     "StepMania note sound bindings are not emitted by this codec slice", "NOTES"});
            if (!note.semantic_layers.empty() || note.label)
                diagnostics.push_back({diagnostic_severity_t::warning,
                                       stepmania_write_diagnostic_code_t::unsupported_feature,
                                       "Note labels and semantic layers are editor metadata and "
                                       "are not emitted to StepMania",
                                       "NOTES"});
        }
    std::string output;
    for (std::size_t measure = 0; measure < by_measure.size(); ++measure) {
        if (measure != 0)
            output += ",\n";
        std::uint64_t rows{4};
        if (rows > max_rows_per_measure) {
            report("Configured row cap is smaller than the minimum StepMania measure");
            return std::nullopt;
        }
        for (const auto& event : by_measure[measure]) {
            const auto denominator = divisions / std::gcd(divisions, event.row);
            const auto combined = checked_lcm(rows, denominator);
            if (!combined || *combined > max_rows_per_measure) {
                report("Note rows require more than the StepMania writer row limit");
                return std::nullopt;
            }
            rows = *combined;
        }
        std::vector<std::string> cells(static_cast<std::size_t>(rows),
                                       std::string(layout.lanes.size(), '0'));
        for (const auto& event : by_measure[measure]) {
            const auto row = multiply_divide_exact(event.row, rows, divisions);
            if (!row || *row >= rows) {
                report("Note row cannot be represented on the emitted StepMania measure grid");
                return std::nullopt;
            }
            auto& cell = cells[*row][event.lane];
            if (cell != '0') {
                report("Two note events collide in one StepMania lane cell");
                return std::nullopt;
            }
            cell = event.symbol;
        }
        for (const auto& row : cells) {
            if (output.size() > max_output_bytes ||
                row.size() + 1 > max_output_bytes - output.size()) {
                diagnostics.push_back(
                    {diagnostic_severity_t::error, stepmania_write_diagnostic_code_t::output_limit,
                     "StepMania note map exceeds the configured output byte limit", "NOTES"});
                return std::nullopt;
            }
            output += row + "\n";
        }
    }
    return output;
}

} // namespace

auto stepmania_write_result_t::has_errors() const -> bool {
    return std::ranges::any_of(diagnostics, [](const auto& diagnostic) {
        return diagnostic.severity == diagnostic_severity_t::error;
    });
}

auto write_stepmania(const song_document_t& song, stepmania_write_options_t options)
    -> stepmania_write_result_t {
    stepmania_write_result_t result;
    const auto report = [&](stepmania_write_diagnostic_code_t code, std::string message,
                            std::string field,
                            diagnostic_severity_t severity = diagnostic_severity_t::error) {
        result.diagnostics.push_back({severity, code, std::move(message), std::move(field)});
    };
    std::vector<const chart_document_t*> charts;
    for (const auto& chart : song.charts)
        if (!options.chart || chart.id == *options.chart)
            charts.push_back(&chart);
    if (charts.empty()) {
        report(stepmania_write_diagnostic_code_t::missing_chart,
               "No chart matches the requested StepMania output", "NOTES");
        return result;
    }
    if (charts.size() > options.max_charts) {
        report(stepmania_write_diagnostic_code_t::output_limit,
               "StepMania output exceeds the configured chart limit", "NOTES");
        return result;
    }
    if (options.max_output_bytes == 0 || options.max_rows_per_measure == 0) {
        report(stepmania_write_diagnostic_code_t::output_limit,
               "StepMania writer limits must be positive", "NOTES");
        return result;
    }
    std::string output;
    const auto emit = [&](std::string_view key, std::string_view value) {
        emit_field(output, key, value, result.diagnostics, options.max_output_bytes);
    };
    if (!song.metadata.title.empty())
        emit("TITLE", song.metadata.title);
    if (!song.metadata.artist.empty())
        emit("ARTIST", song.metadata.artist);
    const auto* song_group = family_group(song.metadata.format_groups);
    std::set<std::string> modeled_song{
        "TITLE",     "ARTIST",     "OFFSET",     "BPMS",     "STOPS",       "DELAYS",
        "WARPS",     "SCROLLS",    "SPEEDS",     "NOTEDATA", "STEPSTYPE",   "DESCRIPTION",
        "CHARTNAME", "CHARTSTYLE", "DIFFICULTY", "METER",    "RADARVALUES", "NOTES"};
    if (options.profile == stepmania_profile_t::sm) {
        modeled_song.insert("CREDIT");
        const auto& author = charts.front()->metadata.author;
        if (std::ranges::any_of(
                charts, [&](const auto* chart) { return chart->metadata.author != author; })) {
            report(stepmania_write_diagnostic_code_t::conflicting_song_metadata,
                   "SM stores CREDIT at song scope; selected charts have different authors",
                   "CREDIT");
        } else if (!author.empty()) {
            emit("CREDIT", author);
        }
    }
    append_unknown_fields(output, song_group, modeled_song, options.preserve_unknown_fields,
                          result.diagnostics, options.max_output_bytes);
    std::vector<std::vector<std::pair<std::string, std::string>>> timing_by_chart;
    timing_by_chart.reserve(charts.size());
    for (const auto* chart : charts)
        timing_by_chart.push_back(make_timing_fields(*chart, options, result.diagnostics));
    std::set<std::string> global_timing_keys;
    if (options.profile == stepmania_profile_t::sm) {
        for (std::size_t index = 1; index < timing_by_chart.size(); ++index) {
            if (timing_by_chart.front() != timing_by_chart[index])
                report(stepmania_write_diagnostic_code_t::conflicting_song_timing,
                       "SM stores timing at song scope; selected charts have different timing maps",
                       "BPMS/STOPS/DELAYS/WARPS/SCROLLS/SPEEDS");
        }
        for (const auto& [key, value] : timing_by_chart.front())
            emit(key, value);
    } else {
        constexpr std::array<std::string_view, 7> timing_keys{
            "OFFSET", "BPMS", "STOPS", "DELAYS", "WARPS", "SCROLLS", "SPEEDS"};
        for (const auto key : timing_keys) {
            if (!group_has_field(song_group, key))
                continue;
            bool chart_override{};
            for (const auto* chart : charts)
                chart_override = chart_override ||
                                 group_has_field(family_group(chart->metadata.format_groups), key);
            if (chart_override)
                continue;
            const auto first = std::ranges::find(timing_by_chart.front(), key,
                                                 &std::pair<std::string, std::string>::first);
            if (first == timing_by_chart.front().end())
                continue;
            const bool shared = std::ranges::all_of(timing_by_chart, [&](const auto& timing) {
                const auto found =
                    std::ranges::find(timing, key, &std::pair<std::string, std::string>::first);
                return found != timing.end() && found->second == first->second;
            });
            if (shared) {
                global_timing_keys.insert(std::string(key));
                emit(first->first, first->second);
            }
        }
    }
    const auto emit_chart = [&](const chart_document_t& chart, std::size_t chart_index) {
        const auto* group = family_group(chart.metadata.format_groups);
        auto step_type = group_value(group, "STEPSTYPE");
        if (step_type.empty() && !chart.lane_layouts.empty()) {
            constexpr std::string_view prefix{"stepmania:"};
            const auto& profile_id = chart.lane_layouts.front().layout.profile_id;
            if (profile_id.starts_with(prefix))
                step_type = profile_id.substr(prefix.size());
        }
        if (step_type.empty() && !chart.lane_layouts.empty()) {
            const auto fallback =
                fallback_step_type_for_count(chart.lane_layouts.front().layout.lanes.size());
            if (!fallback) {
                report(stepmania_write_diagnostic_code_t::invalid_note_layout,
                       "No truthful StepMania step type is known for this lane count", "STEPSTYPE");
                return;
            }
            step_type = *fallback;
        }
        if (step_type.empty())
            step_type = "dance-single";
        const auto declared_columns = note_column_count(step_type);
        if (!declared_columns || chart.lane_layouts.empty() ||
            chart.lane_layouts.front().layout.lanes.size() != *declared_columns) {
            report(stepmania_write_diagnostic_code_t::invalid_note_layout,
                   "Step type and lane layout do not have the same known column count",
                   "STEPSTYPE");
            return;
        }
        const auto retained_description = group_value(group, "DESCRIPTION");
        const auto description =
            options.profile == stepmania_profile_t::sm
                ? (!chart.metadata.name.empty() ? chart.metadata.name : retained_description)
                : retained_description;
        const auto difficulty =
            group_value(group, "DIFFICULTY").empty() ? "Edit" : group_value(group, "DIFFICULTY");
        const auto meter =
            chart.metadata.rating
                ? rational_decimal(*chart.metadata.rating).value_or("1")
                : (group_value(group, "METER").empty() ? "1" : group_value(group, "METER"));
        const auto radar = group_value(group, "RADARVALUES").empty()
                               ? "0,0,0,0,0"
                               : group_value(group, "RADARVALUES");
        const auto note_text = note_map_text(
            chart, result.diagnostics, options.max_rows_per_measure, options.max_output_bytes);
        if (!note_text)
            return;
        const std::set<std::string> modeled_chart{
            "NOTEDATA", "STEPSTYPE",   "DESCRIPTION", "CHARTNAME", "CHARTSTYLE", "DIFFICULTY",
            "METER",    "RADARVALUES", "CREDIT",      "NOTES",     "OFFSET",     "BPMS",
            "STOPS",    "DELAYS",      "WARPS",       "SCROLLS",   "SPEEDS"};
        if (options.profile == stepmania_profile_t::ssc) {
            emit("NOTEDATA", "");
            emit("STEPSTYPE", step_type);
            if (!description.empty())
                emit("DESCRIPTION", description);
            emit("DIFFICULTY", difficulty);
            emit("METER", meter);
            emit("RADARVALUES", radar);
            const auto chart_name = !chart.metadata.name.empty() ? chart.metadata.name
                                                                 : group_value(group, "CHARTNAME");
            const auto chart_style = group_value(group, "CHARTSTYLE");
            if (!chart_name.empty())
                emit("CHARTNAME", chart_name);
            if (!chart_style.empty())
                emit("CHARTSTYLE", chart_style);
            if (!chart.metadata.author.empty())
                emit("CREDIT", chart.metadata.author);
            append_unknown_fields(output, group, modeled_chart, options.preserve_unknown_fields,
                                  result.diagnostics, options.max_output_bytes);
            for (const auto& [key, value] : timing_by_chart[chart_index])
                if (!global_timing_keys.contains(key))
                    emit(key, value);
            emit("NOTES", *note_text);
        } else {
            const auto note_header = step_type + ":\n" + description + ":\n" + difficulty + ":\n" +
                                     meter + ":\n" + radar + ":\n";
            emit("NOTES", note_header + *note_text);
        }
        if (!chart.semantic_layers.empty() || !chart.bookmarks.empty() || !chart.sections.empty())
            report(stepmania_write_diagnostic_code_t::unsupported_feature,
                   "Semantic layers, bookmarks, or sections are editor metadata and are not "
                   "emitted to StepMania",
                   "editor metadata", diagnostic_severity_t::warning);
        if (std::ranges::any_of(chart.measures,
                                [](const auto& measure) { return !measure.bgm_events.empty(); }))
            report(stepmania_write_diagnostic_code_t::unsupported_feature,
                   "BGM lane events are not emitted by this StepMania codec slice", "NOTES",
                   diagnostic_severity_t::warning);
    };
    for (std::size_t index = 0; index < charts.size(); ++index)
        emit_chart(*charts[index], index);
    if (!result.has_errors())
        result.text = std::move(output);
    return result;
}

} // namespace wormedit
