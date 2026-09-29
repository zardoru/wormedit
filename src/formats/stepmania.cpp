#include <wormedit/formats/stepmania.h>

#include <wormedit/core/measure.h>
#include <wormedit/core/sv_recipe.h>

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

void skip_source_space_and_comments(std::string_view source, std::size_t& at) {
    while (at < source.size()) {
        if (std::isspace(static_cast<unsigned char>(source[at]))) {
            ++at;
        } else if (source.substr(at, 2) == "//") {
            const auto end = source.find('\n', at + 2);
            at = end == std::string_view::npos ? source.size() : end + 1;
        } else {
            return;
        }
    }
}

auto parse_source(std::string_view source, std::size_t max_fields) -> std::vector<field_t> {
    std::vector<field_t> fields;
    std::size_t at{};
    while (at < source.size()) {
        skip_source_space_and_comments(source, at);
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

auto note_rows_in_measure(std::string_view source, std::uint32_t lanes)
    -> std::vector<std::string_view> {
    std::vector<std::string_view> rows;
    std::size_t begin{};
    while (begin <= source.size()) {
        const auto end = source.find_first_of("\r\n", begin);
        const auto row = trim(source.substr(begin,
            end == std::string_view::npos ? source.size() - begin : end - begin));
        if (!row.empty()) rows.push_back(row);
        if (end == std::string_view::npos) break;
        begin = end + 1;
    }
    if (rows.empty()) rows.push_back(std::string_view{"0000000000"}.substr(0, lanes));
    return rows;
}

auto normalize_note_row(std::string_view source, std::uint32_t lanes, std::size_t source_order)
    -> std::string {
    if (source.size() != lanes)
        fail(stepmania_parse_diagnostic_code_t::invalid_notes,
             "Note row column count does not match the declared step type", source_order);
    std::string result;
    result.reserve(lanes);
    for (const auto character : source) {
        const auto symbol = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
        if (symbol != '0' && symbol != '1' && symbol != '2' && symbol != '3' && symbol != '4' &&
            symbol != 'M' && symbol != 'L' && symbol != 'F')
            fail(stepmania_parse_diagnostic_code_t::invalid_notes,
                 "Note map contains an unsupported StepMania note symbol", source_order);
        result.push_back(symbol);
    }
    return result;
}

auto note_row_position(std::size_t row_index, std::size_t row_count, std::uint64_t divisions,
                       std::size_t source_order) -> std::uint64_t {
    const auto local = multiply_divide_exact(static_cast<std::uint64_t>(row_index), divisions,
                                              static_cast<std::uint64_t>(row_count));
    if (!local || *local >= divisions)
        fail(stepmania_parse_diagnostic_code_t::position_not_representable,
             "Note row spacing cannot be represented on the selected editor grid", source_order);
    return *local;
}

auto parse_measure_rows(std::string_view raw_measure, std::uint32_t lanes,
                        std::uint64_t divisions, std::size_t max_events,
                        std::size_t& event_count, std::size_t source_order)
    -> std::vector<std::pair<std::uint64_t, std::string>> {
    const auto rows = note_rows_in_measure(raw_measure, lanes);
    std::vector<std::pair<std::uint64_t, std::string>> result;
    result.reserve(rows.size());
    for (std::size_t index = 0; index < rows.size(); ++index) {
        auto row = normalize_note_row(rows[index], lanes, source_order);
        const auto position = note_row_position(index, rows.size(), divisions, source_order);
        result.emplace_back(position, std::move(row));
        event_count += static_cast<std::size_t>(std::ranges::count_if(
            result.back().second, [](char symbol) { return symbol != '0'; }));
        if (event_count > max_events)
            fail(stepmania_parse_diagnostic_code_t::event_limit,
                 "StepMania note map exceeds the configured event limit", source_order);
    }
    return result;
}

auto parse_row_map(std::string_view source, std::uint32_t lanes, std::uint64_t divisions,
                   std::size_t max_measures, std::size_t max_events, std::size_t source_order)
    -> std::vector<std::vector<std::pair<std::uint64_t, std::string>>> {
    const auto measures = split(source, ',');
    if (measures.size() > max_measures)
        fail(stepmania_parse_diagnostic_code_t::measure_limit,
             "StepMania note map exceeds the configured measure limit", source_order);
    std::vector<std::vector<std::pair<std::uint64_t, std::string>>> output;
    output.reserve(measures.size());
    std::size_t event_count{};
    for (const auto raw : measures)
        output.push_back(parse_measure_rows(raw, lanes, divisions, max_events, event_count,
                                            source_order));
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

struct timing_parser_t {
    const std::vector<field_t>& song_fields;
    const std::vector<field_t>& chart_fields;
    stepmania_profile_t profile;
    chart_document_t& chart;
    id_source_t& ids;
    std::size_t& event_total;
    const stepmania_parse_options_t& options;
    std::vector<stepmania_parse_diagnostic_t>& diagnostics;
    std::vector<field_t> effective;
    std::set<std::size_t> chart_source_orders;
    bool reported_negative_bpm{};
    bool reported_negative_stop{};

    timing_parser_t(const std::vector<field_t>& song, const std::vector<field_t>& chart_source,
                    stepmania_profile_t source_profile, chart_document_t& target,
                    id_source_t& id_source, std::size_t& total,
                    const stepmania_parse_options_t& parse_options,
                    std::vector<stepmania_parse_diagnostic_t>& output)
        : song_fields(song), chart_fields(chart_source), profile(source_profile), chart(target),
          ids(id_source), event_total(total), options(parse_options), diagnostics(output),
          effective(song) {
        if (profile != stepmania_profile_t::ssc) return;
        for (const auto key : {"OFFSET", "BPMS", "STOPS", "DELAYS", "WARPS", "SCROLLS", "SPEEDS"}) {
            const auto overrides = fields_for_key(chart_fields, key);
            if (overrides.empty()) continue;
            std::erase_if(effective, [&](const auto& field) { return upper_ascii(field.key) == key; });
            for (const auto* field : overrides) effective.push_back(*field);
        }
        for (const auto& field : chart_fields) chart_source_orders.insert(field.order);
    }

    auto provenance(std::string key, std::string spelling, std::size_t order)
        -> source_provenance_t {
        const auto source_profile = profile == stepmania_profile_t::ssc &&
                                    chart_source_orders.contains(order)
                                        ? "ssc:chart"
                                        : profile == stepmania_profile_t::sm ? "sm" : "ssc";
        return {format_family_t::stepmania, source_profile, std::move(key),
                std::move(spelling), order};
    }

    void count_event(const field_t& field) {
        if (++event_total > options.max_events)
            fail(stepmania_parse_diagnostic_code_t::event_limit,
                 "StepMania file exceeds the configured event limit", field.order, field.offset);
    }

    template <typename Callback>
    void read_pairs(std::string_view key, Callback&& callback) {
        const auto* field = last_field(effective, key);
        if (!field || trim(field->value).empty()) return;
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
            const auto row = position_row(*beat, chart.divisions_per_standard_measure,
                                          field->order, field->offset);
            callback(*beat, *value, row, *field);
            count_event(*field);
        }
    }

    void parse_offset() {
        const auto* offset = last_field(effective, "OFFSET");
        if (!offset) return;
        const auto parsed = decimal_rational(offset->value);
        if (!parsed)
            fail(stepmania_parse_diagnostic_code_t::invalid_timing,
                 "OFFSET must be a finite decimal number", offset->order, offset->offset);
        chart.beat_zero_audio_offset_seconds = {-parsed->numerator, parsed->denominator};
    }

    auto source_bpms() -> std::vector<std::tuple<signed_rational_t, signed_rational_t,
                                                  std::uint64_t, const field_t*>> {
        using entry_t = std::tuple<signed_rational_t, signed_rational_t, std::uint64_t,
                                   const field_t*>;
        std::vector<entry_t> entries;
        read_pairs("BPMS", [&](auto beat, auto bpm, auto row, const field_t& field) {
            if (bpm.numerator == 0)
                fail(stepmania_parse_diagnostic_code_t::invalid_timing, "BPM cannot be zero",
                     field.order, field.offset);
            entries.emplace_back(beat, bpm, row, &field);
        });
        std::stable_sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
            const auto left_beat = std::get<0>(left);
            const auto right_beat = std::get<0>(right);
            return static_cast<long double>(left_beat.numerator) / left_beat.denominator <
                   static_cast<long double>(right_beat.numerator) / right_beat.denominator;
        });
        return entries;
    }

    void parse_bpms() {
        const auto entries = source_bpms();
        for (std::size_t index = 0; index < entries.size(); ++index) {
            const auto& [beat, bpm, row, field] = entries[index];
            if (bpm.numerator > 0) {
                chart.bpms.push_back({.id=ids.take<object_id_t>(),
                    .position=local_position(row, chart.divisions_per_standard_measure),
                    .beats_per_minute=bpm, .provenance=provenance("BPMS", "", field->order)});
                continue;
            }
            validate_legacy_bpm(*field);
            auto boundary = index + 1;
            while (boundary < entries.size() && std::get<1>(entries[boundary]).numerator <= 0)
                ++boundary;
            if (boundary == entries.size() || std::get<2>(entries[boundary]) <= row)
                fail(stepmania_parse_diagnostic_code_t::invalid_timing,
                     "Legacy negative BPM region needs a later positive BPM boundary",
                     field->order, field->offset);
            chart.warps.push_back({.id=ids.take<object_id_t>(),
                .span={local_position(row, chart.divisions_per_standard_measure),
                       local_position(std::get<2>(entries[boundary]), chart.divisions_per_standard_measure)},
                .source_encoding=warp_provenance_t::negative_bpm,
                .provenance=provenance("BPMS", "negative BPM region", field->order)});
            if (!reported_negative_bpm) {
                diagnostics.push_back({.severity=diagnostic_severity_t::warning,
                    .code=stepmania_parse_diagnostic_code_t::legacy_warp_encoding,
                    .message="Legacy negative BPM timing was canonicalized to beat-domain warps; additional events of this encoding are summarized here",
                    .source_order=field->order, .byte_offset=field->offset});
                reported_negative_bpm = true;
            }
            index = boundary - 1;
        }
        source_bpm_rows = entries;
    }

    auto effective_bpm(std::uint64_t row) -> std::optional<signed_rational_t> {
        std::optional<signed_rational_t> result;
        for (const auto& entry : source_bpm_rows) {
            const auto entry_row = std::get<2>(entry);
            if (entry_row > row) break;
            const auto bpm = std::get<1>(entry);
            if (bpm.numerator > 0) result = bpm;
        }
        return result;
    }

    void validate_legacy_bpm(const field_t& field) {
        if (options.source_implementation_profile != stepmania_implementation_profile_t::unspecified ||
            (options.source_timing_syntax != stepmania_timing_syntax_t::unspecified &&
             options.source_timing_syntax != stepmania_timing_syntax_t::legacy_negative_bpm))
            fail(stepmania_parse_diagnostic_code_t::implementation_profile_mismatch,
                 "Negative BPM warp syntax is outside the selected implementation or timing syntax profile",
                 field.order, field.offset);
    }

    void validate_legacy_stop(const field_t& field) {
        if (options.source_implementation_profile != stepmania_implementation_profile_t::unspecified ||
            (options.source_timing_syntax != stepmania_timing_syntax_t::unspecified &&
             options.source_timing_syntax != stepmania_timing_syntax_t::legacy_negative_stop))
            fail(stepmania_parse_diagnostic_code_t::implementation_profile_mismatch,
                 "Negative STOP warp syntax is outside the selected implementation or timing syntax profile",
                 field.order, field.offset);
    }

    void parse_stops() {
        read_pairs("STOPS", [&](auto, auto duration, auto row, const field_t& field) {
            if (duration.numerator >= 0) {
                chart.pauses.push_back({.id=ids.take<object_id_t>(),
                    .position=local_position(row, chart.divisions_per_standard_measure),
                    .kind=pause_kind_t::stop, .duration=duration, .source_unit=pause_unit_t::seconds,
                    .provenance=provenance("STOPS", "", field.order)});
                return;
            }
            validate_legacy_stop(field);
            const auto bpm = effective_bpm(row);
            if (!bpm || bpm->numerator <= 0)
                fail(stepmania_parse_diagnostic_code_t::invalid_timing,
                     "Legacy negative STOP warp needs a positive effective BPM", field.order,
                     field.offset);
            const auto span = exact_product_ratio({static_cast<std::uint64_t>(-duration.numerator),
                static_cast<std::uint64_t>(bpm->numerator), chart.divisions_per_standard_measure},
                {duration.denominator, bpm->denominator, 240});
            if (!span || *span == 0 || row > std::numeric_limits<std::uint64_t>::max() - *span)
                fail(stepmania_parse_diagnostic_code_t::position_not_representable,
                     "Legacy negative STOP warp length is not exactly representable",
                     field.order, field.offset);
            chart.warps.push_back({.id=ids.take<object_id_t>(),
                .span={local_position(row, chart.divisions_per_standard_measure),
                       local_position(row + *span, chart.divisions_per_standard_measure)},
                .source_encoding=warp_provenance_t::negative_stop,
                .provenance=provenance("STOPS", "negative STOP", field.order)});
            if (!reported_negative_stop) {
                diagnostics.push_back({.severity=diagnostic_severity_t::warning,
                    .code=stepmania_parse_diagnostic_code_t::legacy_warp_encoding,
                    .message="Legacy negative STOP timing was canonicalized to beat-domain warps; additional events of this encoding are summarized here",
                    .source_order=field.order, .byte_offset=field.offset});
                reported_negative_stop = true;
            }
        });
    }

    void parse_delays() {
        read_pairs("DELAYS", [&](auto, auto duration, auto row, const field_t& field) {
            if (duration.numerator < 0)
                fail(stepmania_parse_diagnostic_code_t::invalid_timing,
                     "DELAYS durations cannot be negative", field.order, field.offset);
            chart.pauses.push_back({.id=ids.take<object_id_t>(),
                .position=local_position(row, chart.divisions_per_standard_measure),
                .kind=pause_kind_t::delay, .duration=duration, .source_unit=pause_unit_t::seconds,
                .provenance=provenance("DELAYS", "", field.order)});
        });
    }

    void parse_warps() {
        read_pairs("WARPS", [&](auto, auto length, auto row, const field_t& field) {
            if (options.source_timing_syntax != stepmania_timing_syntax_t::unspecified &&
                options.source_timing_syntax != stepmania_timing_syntax_t::native_warps)
                fail(stepmania_parse_diagnostic_code_t::implementation_profile_mismatch,
                     "WARPS syntax requires the native_warps source profile", field.order,
                     field.offset);
            if (length.numerator <= 0)
                fail(stepmania_parse_diagnostic_code_t::invalid_timing,
                     "WARPS lengths must be positive", field.order, field.offset);
            const auto span = position_row(length, chart.divisions_per_standard_measure,
                                           field.order, field.offset);
            if (row > std::numeric_limits<std::uint64_t>::max() - span)
                fail(stepmania_parse_diagnostic_code_t::position_not_representable,
                     "Warp endpoint overflows the editor position range", field.order,
                     field.offset);
            chart.warps.push_back({.id=ids.take<object_id_t>(),
                .span={local_position(row, chart.divisions_per_standard_measure),
                       local_position(row + span, chart.divisions_per_standard_measure)},
                .source_encoding=warp_provenance_t::native,
                .provenance=provenance("WARPS", "", field.order)});
        });
    }

    void parse_scrolls() {
        read_pairs("SCROLLS", [&](auto, auto multiplier, auto row, const field_t& field) {
            chart.scrolls.push_back({.id=ids.take<object_id_t>(),
                .position=local_position(row, chart.divisions_per_standard_measure),
                .multiplier=multiplier, .provenance=provenance("SCROLLS", "", field.order)});
        });
    }

    void parse_speeds() {
        const auto* field = last_field(effective, "SPEEDS");
        if (!field || trim(field->value).empty()) return;
        for (const auto item : split(field->value, ',')) {
            const auto parts = split(item, '=');
            if (parts.size() != 4)
                fail(stepmania_parse_diagnostic_code_t::invalid_timing,
                     "SPEEDS entries require beat=ratio=duration=unit", field->order, field->offset);
            const auto beat = decimal_rational(parts[0]);
            const auto ratio = decimal_rational(parts[1]);
            const auto duration = decimal_rational(parts[2]);
            std::uint64_t unit{};
            if (!beat || !ratio || !duration || duration->numerator < 0 ||
                !parse_unsigned(parts[3], unit) || unit > 1)
                fail(stepmania_parse_diagnostic_code_t::invalid_timing,
                     "SPEEDS contains an invalid beat, multiplier, duration, or unit",
                     field->order, field->offset);
            const auto row = position_row(*beat, chart.divisions_per_standard_measure,
                                          field->order, field->offset);
            chart.speeds.push_back({.id=ids.take<object_id_t>(),
                .position=local_position(row, chart.divisions_per_standard_measure),
                .multiplier=*ratio,
                .transition_beats={static_cast<std::uint64_t>(duration->numerator), duration->denominator},
                .transition_is_seconds=unit == 1,
                .provenance=provenance("SPEEDS", "", field->order)});
            count_event(*field);
        }
    }

    void parse_all() {
        parse_offset();
        parse_bpms();
        parse_stops();
        parse_delays();
        parse_warps();
        parse_scrolls();
        parse_speeds();
    }

    using bpm_entry_t = std::tuple<signed_rational_t, signed_rational_t, std::uint64_t,
                                   const field_t*>;
    std::vector<bpm_entry_t> source_bpm_rows;
};

void parse_timing_fields(const std::vector<field_t>& song_fields,
                         const std::vector<field_t>& chart_fields, stepmania_profile_t profile,
                         chart_document_t& chart, id_source_t& ids, std::size_t& event_total,
                         const stepmania_parse_options_t& options,
                         std::vector<stepmania_parse_diagnostic_t>& diagnostics) {
    timing_parser_t{song_fields, chart_fields, profile, chart, ids, event_total, options,
                    diagnostics}.parse_all();
}

struct pending_hold_t {
    note_kind_t kind;
    local_position_t position;
};

auto note_kind_for_symbol(char symbol) -> note_kind_t {
    if (symbol == 'M') return note_kind_t::mine;
    if (symbol == 'L') return note_kind_t::lift;
    if (symbol == 'F') return note_kind_t::fake;
    return note_kind_t::tap;
}

auto note_provenance(stepmania_profile_t profile, std::string spelling,
                     const field_t& field) -> source_provenance_t {
    return {format_family_t::stepmania,
            profile == stepmania_profile_t::sm ? "sm" : "ssc",
            "NOTES", std::move(spelling), field.order};
}

void append_hold_tail(chart_document_t& chart, const lane_layout_t& layout,
                      std::size_t measure_index, std::uint32_t lane_index,
                      local_position_t position, std::map<std::uint32_t, pending_hold_t>& open_holds,
                      id_source_t& ids, stepmania_profile_t profile, const field_t& field) {
    const auto found = open_holds.find(lane_index);
    if (found == open_holds.end())
        fail(stepmania_parse_diagnostic_code_t::invalid_hold,
             "Hold tail has no matching head in its lane", field.order, field.offset);
    chart.measures[measure_index].notes.push_back({
        .id=ids.take<object_id_t>(), .position=found->second.position,
        .lane=layout.lanes[lane_index].id, .kind=found->second.kind, .end=position,
        .provenance=note_provenance(profile, "hold", field)});
    open_holds.erase(found);
}

void append_note_symbol(chart_document_t& chart, const lane_layout_t& layout,
                        std::size_t measure_index, std::uint32_t lane_index, char symbol,
                        local_position_t position,
                        std::map<std::uint32_t, pending_hold_t>& open_holds,
                        id_source_t& ids, stepmania_profile_t profile, const field_t& field) {
    if (symbol == '0') return;
    if (symbol == '2' || symbol == '4') {
        if (open_holds.contains(lane_index))
            fail(stepmania_parse_diagnostic_code_t::invalid_hold,
                 "A hold head appears before the previous hold tail", field.order, field.offset);
        open_holds.emplace(lane_index, pending_hold_t{
            symbol == '4' ? note_kind_t::roll : note_kind_t::hold, position});
        return;
    }
    if (symbol == '3') {
        append_hold_tail(chart, layout, measure_index, lane_index, position, open_holds,
                         ids, profile, field);
        return;
    }
    chart.measures[measure_index].notes.push_back({
        .id=ids.take<object_id_t>(), .position=position, .lane=layout.lanes[lane_index].id,
        .kind=note_kind_for_symbol(symbol),
        .provenance=note_provenance(profile, std::string(1, symbol), field)});
}

void append_note_map_rows(chart_document_t& chart,
                          const std::vector<std::vector<std::pair<std::uint64_t, std::string>>>& rows,
                          std::uint32_t lane_count, const lane_layout_t& layout,
                          id_source_t& ids, std::size_t& total_events,
                          const stepmania_parse_options_t& options,
                          stepmania_profile_t profile, const field_t& note_field) {
    std::map<std::uint32_t, pending_hold_t> open_holds;
    for (std::size_t measure = 0; measure < rows.size(); ++measure) {
        for (const auto& [row, symbols] : rows[measure]) {
            const auto absolute = static_cast<std::uint64_t>(measure) *
                                  chart.divisions_per_standard_measure + row;
            const auto position = local_position(absolute, chart.divisions_per_standard_measure);
            for (std::uint32_t lane = 0; lane < lane_count; ++lane) {
                if (symbols[lane] == '0') continue;
                if (++total_events > options.max_events)
                    fail(stepmania_parse_diagnostic_code_t::event_limit,
                         "StepMania file exceeds the configured event limit",
                         note_field.order, note_field.offset);
                append_note_symbol(chart, layout, measure, lane, symbols[lane], position,
                                   open_holds, ids, profile, note_field);
            }
        }
    }
    if (!open_holds.empty())
        fail(stepmania_parse_diagnostic_code_t::invalid_hold,
             "StepMania chart contains a hold head with no tail", note_field.order,
             note_field.offset);
}

auto make_note_measures(std::size_t count) -> std::vector<measure_chunk_t> {
    std::vector<measure_chunk_t> measures;
    measures.reserve(count);
    for (std::size_t measure = 0; measure < count; ++measure)
        measures.push_back({.id=measure_id_t{measure + 1},
                            .definition={{1, 1}, meter_t{4, 4}}});
    if (measures.empty())
        measures.push_back({.id=measure_id_t{1}, .definition={{1, 1}, meter_t{4, 4}}});
    return measures;
}

auto maximum_timing_measure(const chart_document_t& chart, std::size_t note_measure_count)
    -> std::uint64_t {
    auto maximum = static_cast<std::uint64_t>(note_measure_count);
    const auto extend = [&](local_position_t position) {
        maximum = std::max(maximum, position.measure.value);
    };
    for (const auto& event : chart.bpms) extend(event.position);
    for (const auto& event : chart.pauses) extend(event.position);
    for (const auto& event : chart.scrolls) extend(event.position);
    for (const auto& event : chart.speeds) extend(event.position);
    for (const auto& event : chart.warps) {
        extend(event.span.begin);
        extend(event.span.end);
    }
    return maximum;
}

void extend_note_measures(std::vector<measure_chunk_t>& measures, std::uint64_t maximum,
                          std::size_t max_measures, std::size_t chart_order,
                          const field_t& note_field) {
    if (maximum > max_measures)
        fail(stepmania_parse_diagnostic_code_t::measure_limit,
             "StepMania timing exceeds the configured measure limit", chart_order,
             note_field.offset);
    while (measures.size() < maximum)
        measures.push_back({.id=measure_id_t{measures.size() + 1},
                            .definition={{1, 1}, meter_t{4, 4}}});
}

auto parse_one_chart(const chart_source_t& source, const std::vector<field_t>& song_fields,
                     stepmania_profile_t profile,
                     const std::shared_ptr<const std::vector<std::byte>>& source_bytes,
                     const stepmania_parse_options_t& options, std::size_t chart_index,
                     id_source_t& ids, std::size_t& total_events,
                     std::vector<stepmania_parse_diagnostic_t>& diagnostics)
    -> chart_document_t {
    const auto [step_type, description, difficulty, meter_text, radar] =
        parse_note_source(source, profile, chart_index);
    const auto lane_count = note_column_count(step_type);
    const auto* note_field = last_field(source.fields, "NOTES");
    if (!lane_count)
        fail(stepmania_parse_diagnostic_code_t::unsupported_step_type,
             "Unsupported StepMania step type: " + step_type, chart_index,
             note_field ? std::optional{note_field->offset} : std::nullopt);

    chart_document_t chart;
    chart.id = chart_id_t{chart_index + 1};
    chart.source_family = format_family_t::stepmania;
    chart.divisions_per_standard_measure = default_divisions_per_standard_measure;
    if (chart.divisions_per_standard_measure > options.max_divisions_per_standard_measure)
        fail(stepmania_parse_diagnostic_code_t::division_limit,
             "Default StepMania editor grid exceeds the configured division limit", chart_index,
             note_field->offset);
    const auto* chart_name = last_field(source.fields, "CHARTNAME");
    chart.metadata.name = chart_name ? std::string(trim(chart_name->value)) : description;
    const auto* credit = last_field(profile == stepmania_profile_t::ssc
                                        ? source.fields : song_fields, "CREDIT");
    if (credit) chart.metadata.author = std::string(trim(credit->value));
    chart.metadata.format_groups.push_back(as_metadata_group(source.fields, profile, source_bytes));
    if (profile == stepmania_profile_t::sm) {
        const auto order = note_field ? note_field->order : chart_index;
        auto& group = chart.metadata.format_groups.back();
        group.ordered_fields.emplace_back("STEPSTYPE", step_type);
        group.ordered_fields.emplace_back("DESCRIPTION", description);
        group.ordered_fields.emplace_back("DIFFICULTY", difficulty);
        group.ordered_fields.emplace_back("METER", meter_text);
        group.ordered_fields.emplace_back("RADARVALUES", radar);
        group.ordered_field_source_orders.insert(group.ordered_field_source_orders.end(), 5, order);
    }
    if (const auto rating = decimal_rational(meter_text); rating && rating->numerator >= 0)
        chart.metadata.rating = *rating;
    parse_timing_fields(song_fields, source.fields, profile, chart, ids, total_events,
                        options, diagnostics);

    std::string_view note_map = note_field->value;
    if (profile == stepmania_profile_t::sm) {
        const auto colon = note_field->value.rfind(':');
        if (colon == std::string::npos)
            fail(stepmania_parse_diagnostic_code_t::invalid_notes,
                 "SM #NOTES is missing its note map", note_field->order, note_field->offset);
        note_map = std::string_view(note_field->value).substr(colon + 1);
    }
    const auto rows = parse_row_map(note_map, *lane_count,
                                    chart.divisions_per_standard_measure,
                                    options.max_measures, options.max_events, note_field->order);
    auto layout = make_layout(step_type, *lane_count, ids);
    chart.lane_layouts.push_back({.id=ids.take<object_id_t>(),
                                  .position={measure_id_t{1}, 0}, .layout=layout});
    auto measures = make_note_measures(rows.size());
    chart.measures = measures;
    append_note_map_rows(chart, rows, *lane_count, layout, ids, total_events,
                         options, profile, *note_field);
    measures = std::move(chart.measures);
    const auto max_measure = maximum_timing_measure(chart, measures.size());
    extend_note_measures(measures, max_measure, options.max_measures, chart_index, *note_field);
    chart.measures = std::move(measures);
    return chart;
}

void validate_stepmania_source_options(const stepmania_parse_options_t& options,
                                      std::size_t field_count) {
    if (options.source_implementation_profile ==
        stepmania_implementation_profile_t::unspecified) return;
    if (options.profile != stepmania_profile_t::ssc)
        fail(stepmania_parse_diagnostic_code_t::implementation_profile_mismatch,
             "Named StepMania implementation profiles currently cover SSC input only",
             field_count);
    if (options.source_timing_syntax != stepmania_timing_syntax_t::unspecified &&
        options.source_timing_syntax != stepmania_timing_syntax_t::native_warps)
        fail(stepmania_parse_diagnostic_code_t::implementation_profile_mismatch,
             "The selected implementation profile is audited only for native WARPS",
             field_count);
}

void append_chart_source(std::vector<chart_source_t>& charts, const field_t& field,
                         std::size_t max_charts, std::string_view limit_message) {
    if (charts.size() >= max_charts)
        fail(stepmania_parse_diagnostic_code_t::chart_limit, std::string(limit_message),
             field.order, field.offset);
    charts.push_back({{field}});
}

auto partition_sm_fields(const std::vector<field_t>& fields, std::size_t max_charts)
    -> std::pair<std::vector<field_t>, std::vector<chart_source_t>> {
    std::vector<field_t> song_fields;
    std::vector<chart_source_t> charts;
    for (const auto& field : fields) {
        if (upper_ascii(field.key) == "NOTES")
            append_chart_source(charts, field, max_charts,
                                "StepMania file exceeds the configured chart limit");
        else
            song_fields.push_back(field);
    }
    return {std::move(song_fields), std::move(charts)};
}

auto partition_ssc_fields(const std::vector<field_t>& fields, std::size_t max_charts)
    -> std::pair<std::vector<field_t>, std::vector<chart_source_t>> {
    std::vector<field_t> song_fields;
    std::vector<chart_source_t> charts;
    chart_source_t* active{};
    for (const auto& field : fields) {
        const auto key = upper_ascii(field.key);
        if (key == "NOTEDATA") {
            if (charts.size() >= max_charts)
                fail(stepmania_parse_diagnostic_code_t::chart_limit,
                     "SSC file exceeds the configured chart limit", field.order, field.offset);
            charts.emplace_back();
            active = &charts.back();
            active->fields.push_back(field);
        } else if (active) {
            active->fields.push_back(field);
        } else if (key == "NOTES") {
            append_chart_source(charts, field, max_charts,
                                "SSC file exceeds the configured chart limit");
        } else {
            song_fields.push_back(field);
        }
    }
    return {std::move(song_fields), std::move(charts)};
}

auto partition_stepmania_fields(const std::vector<field_t>& fields,
                                const stepmania_parse_options_t& options)
    -> std::pair<std::vector<field_t>, std::vector<chart_source_t>> {
    if (options.profile == stepmania_profile_t::sm)
        return partition_sm_fields(fields, options.max_charts);
    return partition_ssc_fields(fields, options.max_charts);
}

void validate_ssc_version(const std::vector<field_t>& song_fields,
                           stepmania_profile_t profile) {
    if (profile != stepmania_profile_t::ssc) return;
    const auto versions = fields_for_key(song_fields, "VERSION");
    if (versions.size() > 1)
        fail(stepmania_parse_diagnostic_code_t::duplicate_field,
             "SSC source contains more than one song-level VERSION field",
             versions[1]->order, versions[1]->offset);
    if (versions.empty()) return;
    const auto version = decimal_rational(versions.front()->value);
    if (!version || *version != signed_rational_t{83, 100})
        fail(stepmania_parse_diagnostic_code_t::unsupported_version,
             "Only the audited StepMania 5.1 and Etterna 0.75.1 SSC version 0.83 profiles are supported",
             versions.front()->order, versions.front()->offset);
}

auto make_stepmania_song(const std::vector<field_t>& song_fields, stepmania_profile_t profile,
                         const std::shared_ptr<const std::vector<std::byte>>& bytes)
    -> song_document_t {
    song_document_t song;
    song.id = song_id_t{1};
    song.metadata.format_groups.push_back(as_metadata_group(song_fields, profile, bytes));
    if (const auto* title = last_field(song_fields, "TITLE"))
        song.metadata.title = std::string(trim(title->value));
    if (const auto* artist = last_field(song_fields, "ARTIST"))
        song.metadata.artist = std::string(trim(artist->value));
    return song;
}

void append_duplicate_song_field_warnings(const std::vector<field_t>& song_fields,
                                          std::vector<stepmania_parse_diagnostic_t>& diagnostics) {
    for (const auto key : {"TITLE", "ARTIST"}) {
        const auto found = fields_for_key(song_fields, key);
        for (std::size_t index = 0; index + 1 < found.size(); ++index)
            diagnostics.push_back({diagnostic_severity_t::warning,
                stepmania_parse_diagnostic_code_t::duplicate_field,
                std::string("Duplicate ") + key +
                    " retained in source metadata; last value wins",
                found[index]->order, found[index]->offset});
    }
}

auto parse_stepmania_document(std::span<const std::byte> bytes, std::string_view source,
                             const stepmania_parse_options_t& options,
                             std::vector<stepmania_parse_diagnostic_t>& diagnostics)
    -> song_document_t {
    const auto fields = parse_source(source, options.max_fields);
    validate_stepmania_source_options(options, fields.size());
    auto [song_fields, charts] = partition_stepmania_fields(fields, options);
    validate_ssc_version(song_fields, options.profile);
    if (charts.empty())
        fail(stepmania_parse_diagnostic_code_t::invalid_notes,
             "StepMania source contains no chart note maps", fields.size());
    const auto source_bytes = std::make_shared<const std::vector<std::byte>>(bytes.begin(), bytes.end());
    auto song = make_stepmania_song(song_fields, options.profile, source_bytes);
    id_source_t ids;
    std::size_t total_events{};
    for (std::size_t index = 0; index < charts.size(); ++index)
        song.charts.push_back(parse_one_chart(charts[index], song_fields, options.profile,
            source_bytes, options, index, ids, total_events, diagnostics));
    append_duplicate_song_field_warnings(song_fields, diagnostics);
    return song;
}

auto validate_input_bytes(std::span<const std::byte> bytes,
                          const stepmania_parse_options_t& options,
                          std::vector<stepmania_parse_diagnostic_t>& diagnostics)
    -> std::optional<std::string_view> {
    if (bytes.size() > options.max_source_bytes) {
        diagnostics.push_back({.severity=diagnostic_severity_t::error,
            .code=stepmania_parse_diagnostic_code_t::source_limit,
            .message="StepMania source exceeds the configured byte limit",
            .source_order=0, .byte_offset=0});
        return std::nullopt;
    }
    const auto* data = reinterpret_cast<const char*>(bytes.data());
    auto source = std::string_view(data, bytes.size());
    if (const auto invalid = valid_utf8(source)) {
        diagnostics.push_back({.severity=diagnostic_severity_t::error,
            .code=stepmania_parse_diagnostic_code_t::invalid_encoding,
            .message="StepMania source is not valid UTF-8", .source_order=0,
            .byte_offset=*invalid});
        return std::nullopt;
    }
    if (source.starts_with("\xef\xbb\xbf")) source.remove_prefix(3);
    return source;
}

} // namespace

auto stepmania_parse_result_t::has_errors() const -> bool {
    return has_error(diagnostics);
}

auto parse_stepmania_bytes(std::span<const std::byte> bytes, stepmania_parse_options_t options)
    -> stepmania_parse_result_t {
    stepmania_parse_result_t result;
    const auto source = validate_input_bytes(bytes, options, result.diagnostics);
    if (!source) return result;
    try {
        auto song = parse_stepmania_document(bytes, *source, options, result.diagnostics);
        if (!has_error(result.diagnostics)) result.song = std::move(song);
    } catch (const failure_t& failure) {
        result.diagnostics.push_back({.severity=diagnostic_severity_t::error,
            .code=failure.code, .message=failure.message, .source_order=failure.order,
            .byte_offset=failure.offset});
    } catch (const std::bad_alloc&) {
        result.diagnostics.push_back({.severity=diagnostic_severity_t::error,
            .code=stepmania_parse_diagnostic_code_t::source_limit,
            .message="Insufficient memory within configured parser limits",
            .source_order=0, .byte_offset=std::nullopt});
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

auto format_scaled_decimal(std::uint64_t scaled, std::uint64_t scale,
                           std::uint32_t digits) -> std::string {
    if (digits == 0) return std::to_string(scaled);
    const auto whole = scaled / scale;
    auto fraction = std::to_string(scaled % scale);
    if (fraction.size() < digits) fraction.insert(0, digits - fraction.size(), '0');
    while (!fraction.empty() && fraction.back() == '0') fraction.pop_back();
    auto result = std::to_string(whole);
    if (!fraction.empty()) result += "." + fraction;
    return result;
}

auto signed_decimal(std::uint64_t magnitude, bool negative, std::uint64_t scale,
                    std::uint32_t digits) -> std::optional<std::string> {
    auto result = format_scaled_decimal(magnitude, scale, digits);
    if (negative && magnitude != 0) result.insert(result.begin(), '-');
    return result;
}

auto rational_decimal(signed_rational_t value) -> std::optional<std::string> {
    if (value.denominator == 0) return std::nullopt;
    const auto magnitude = value.numerator < 0
                               ? static_cast<std::uint64_t>(-(value.numerator + 1)) + 1
                               : static_cast<std::uint64_t>(value.numerator);
    std::uint64_t scale{1};
    for (std::uint32_t digits = 0; digits <= 18; ++digits) {
        if (const auto scaled = multiply_divide_exact(magnitude, scale, value.denominator))
            return signed_decimal(*scaled, value.numerator < 0, scale, digits);
        if (digits < 18 && scale <= std::numeric_limits<std::uint64_t>::max() / 10)
            scale *= 10;
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

struct timing_fields_builder_t {
    using event_t = std::tuple<std::uint64_t, std::size_t, std::string>;
    using field_t_pair = std::pair<std::string, std::string>;
    const chart_document_t& chart;
    const stepmania_write_options_t& options;
    std::vector<stepmania_write_diagnostic_t>& diagnostics;
    warp_encoding_t encoding;
    bool reported_legacy_conversion{};
    std::vector<event_t> bpm_events, stop_events, delay_events;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> warps;
    std::vector<field_t_pair> result;

    timing_fields_builder_t(const chart_document_t& source_chart,
                            const stepmania_write_options_t& write_options,
                            std::vector<stepmania_write_diagnostic_t>& output_diagnostics)
        : chart(source_chart), options(write_options), diagnostics(output_diagnostics),
          encoding(select_warp_encoding(write_options)) {}

    static auto select_warp_encoding(const stepmania_write_options_t& options) -> warp_encoding_t {
        if (options.target_implementation_profile !=
            stepmania_implementation_profile_t::unspecified) return warp_encoding_t::native;
        switch (options.target_timing_syntax) {
        case stepmania_timing_syntax_t::native_warps: return warp_encoding_t::native;
        case stepmania_timing_syntax_t::legacy_negative_bpm: return warp_encoding_t::negative_bpm;
        case stepmania_timing_syntax_t::legacy_negative_stop: return warp_encoding_t::negative_stop;
        case stepmania_timing_syntax_t::unspecified: return options.warp_encoding;
        }
        return options.warp_encoding;
    }

    void report(stepmania_write_diagnostic_code_t code, std::string message, std::string field,
                diagnostic_severity_t severity = diagnostic_severity_t::error) {
        if (code == stepmania_write_diagnostic_code_t::legacy_warp_conversion) {
            if (reported_legacy_conversion) return;
            reported_legacy_conversion = true;
        }
        diagnostics.push_back({severity, code, std::move(message), std::move(field)});
    }
    auto beat_text(std::uint64_t row, std::string_view field) -> std::optional<std::string> {
        const auto beat = beat_at_row(row, chart.divisions_per_standard_measure);
        const auto text = beat ? rational_decimal(*beat) : std::nullopt;
        if (!text) report(stepmania_write_diagnostic_code_t::unrepresentable_position,
            "Timing beat cannot be written as an exact StepMania decimal", std::string(field));
        return text;
    }
    auto value_text(signed_rational_t value, std::string_view field)
        -> std::optional<std::string> {
        const auto text = rational_decimal(value);
        if (!text) report(stepmania_write_diagnostic_code_t::unrepresentable_position,
            "Timing value cannot be written as an exact StepMania decimal", std::string(field));
        return text;
    }
    auto position_for(local_position_t position, std::string_view field)
        -> std::optional<std::uint64_t> {
        const auto row = absolute_row_for(chart, position);
        if (!row) report(stepmania_write_diagnostic_code_t::invalid_document,
            "Timing event position does not reference a chart measure", std::string(field));
        return row;
    }
    void check_profile_support() {
        if (options.profile != stepmania_profile_t::sm) return;
        if (!chart.scrolls.empty()) report(stepmania_write_diagnostic_code_t::unsupported_feature,
            "This SM writer profile cannot represent SCROLLS", "SCROLLS");
        if (!chart.speeds.empty()) report(stepmania_write_diagnostic_code_t::unsupported_feature,
            "This SM writer profile cannot represent SPEEDS", "SPEEDS");
        if (!chart.warps.empty() && encoding == warp_encoding_t::native)
            report(stepmania_write_diagnostic_code_t::unsupported_feature,
                "This SM writer profile requires an explicit legacy strategy for warps", "WARPS");
    }
    void collect_bpms() {
        for (const auto& event : chart.bpms) {
            const auto row = position_for(event.position, "BPMS");
            const auto beat = row ? beat_text(*row, "BPMS") : std::nullopt;
            const auto bpm = value_text(event.beats_per_minute, "BPMS");
            if (row && beat && bpm) bpm_events.emplace_back(*row, event.provenance.source_order,
                                                             *beat + "=" + *bpm);
        }
        std::stable_sort(bpm_events.begin(), bpm_events.end());
    }
    void collect_warps() {
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
        std::ranges::sort(warps);
        if (encoding == warp_encoding_t::reject && !warps.empty())
            report(stepmania_write_diagnostic_code_t::unsupported_feature,
                   "Selected warp policy rejects StepMania warps", "WARPS");
    }
    auto warp_ranges() const -> const std::vector<std::pair<std::uint64_t, std::uint64_t>>& {
        return warps;
    }
    void validate_legacy_warp_conflicts() {
        if (encoding != warp_encoding_t::negative_bpm &&
            encoding != warp_encoding_t::negative_stop) return;
        const auto ranges = warp_ranges();
        for (std::size_t index = 1; index < ranges.size(); ++index)
            if (ranges[index].first <= ranges[index - 1].second)
                report(stepmania_write_diagnostic_code_t::unsupported_feature,
                    "Overlapping or adjacent warps cannot be encoded exactly by the selected legacy strategy",
                    "WARPS");
        validate_no_timing_inside_warps(ranges);
    }
    void validate_no_timing_inside_warps(const std::vector<std::pair<std::uint64_t,std::uint64_t>>& ranges) {
        for (const auto& bpm : chart.bpms) {
            const auto row = position_for(bpm.position, "BPMS");
            if (row && std::ranges::any_of(ranges, [&](const auto& warp) {
                    return *row > warp.first && *row < warp.second;
                })) report(stepmania_write_diagnostic_code_t::unsupported_feature,
                    "A BPM change inside a warp prevents exact legacy encoding", "BPMS");
        }
        for (const auto& pause : chart.pauses) {
            const auto field = pause.kind == pause_kind_t::stop ? "STOPS" : "DELAYS";
            const auto row = position_for(pause.position, field);
            if (row && std::ranges::any_of(ranges, [&](const auto& warp) {
                    return *row >= warp.first && *row < warp.second;
                })) report(stepmania_write_diagnostic_code_t::unsupported_feature,
                    "A pause inside a warp cannot be encoded by this legacy warp strategy", field);
        }
    }
    auto bpm_at_row(std::uint64_t row, const std::vector<event_t>& source)
        -> std::optional<std::string> {
        std::optional<std::string> value;
        for (const auto& event : source) {
            if (std::get<0>(event) > row) break;
            const auto equals = std::get<2>(event).find('=');
            if (equals != std::string::npos) value = std::get<2>(event).substr(equals + 1);
        }
        return value;
    }
    void encode_negative_bpm_warps() {
        if (encoding != warp_encoding_t::negative_bpm || warps.empty()) return;
        const auto original = bpm_events;
        for (const auto& warp : warps) {
            const auto [begin, end] = warp;
            append_negative_bpm_warp(begin, end, original);
        }
        std::stable_sort(bpm_events.begin(), bpm_events.end());
    }
    void append_negative_bpm_warp(std::uint64_t begin, std::uint64_t end,
                                  const std::vector<event_t>& original) {
        const auto bpm = bpm_at_row(begin, original);
        const auto start_text = beat_text(begin, "BPMS");
        const auto end_text = beat_text(end, "BPMS");
        if (!bpm || !start_text || !end_text) {
            report(stepmania_write_diagnostic_code_t::unsupported_feature,
                "Legacy negative BPM encoding requires a positive BPM at the warp start", "BPMS");
            return;
        }
        const auto parsed_bpm = decimal_rational(*bpm);
        const auto negative = parsed_bpm
            ? rational_decimal({-parsed_bpm->numerator, parsed_bpm->denominator}) : std::nullopt;
        if (!negative) {
            report(stepmania_write_diagnostic_code_t::unrepresentable_position,
                   "Warp start BPM cannot be negated exactly", "BPMS");
            return;
        }
        std::erase_if(bpm_events, [&](const auto& event) {
            return std::get<0>(event) >= begin && std::get<0>(event) < end;
        });
        bpm_events.emplace_back(begin, 0, *start_text + "=" + *negative);
        if (std::ranges::none_of(bpm_events, [&](const auto& event) {
                return std::get<0>(event) == end;
            })) bpm_events.emplace_back(end, 1, *end_text + "=" + *bpm);
        report(stepmania_write_diagnostic_code_t::legacy_warp_conversion,
               "Warp emitted through legacy negative BPM encoding", "WARPS",
               diagnostic_severity_t::warning);
    }
    auto negative_stop_duration(std::uint64_t begin, std::uint64_t end)
        -> std::optional<std::string> {
        std::optional<std::string> bpm_text;
        std::uint64_t bpm_row{};
        for (const auto& event : chart.bpms) {
            const auto row = absolute_row_for(chart, event.position);
            if (!row || *row > begin) continue;
            if (!bpm_text || *row >= bpm_row) {
                bpm_row = *row;
                bpm_text = value_text(event.beats_per_minute, "BPMS");
            }
        }
        if (!bpm_text) return std::nullopt;
        const auto bpm = decimal_rational(*bpm_text);
        if (!bpm || bpm->numerator <= 0) return std::nullopt;
        auto seconds = scaled_ratio({end - begin, 240, bpm->denominator},
            {chart.divisions_per_standard_measure, static_cast<std::uint64_t>(bpm->numerator), 1});
        if (!seconds) return std::nullopt;
        seconds->numerator = -seconds->numerator;
        return rational_decimal(*seconds);
    }
    void encode_negative_stop_warps() {
        if (encoding != warp_encoding_t::negative_stop) return;
        for (const auto& warp : warps) {
            const auto [begin, end] = warp;
            const auto start = beat_text(begin, "STOPS");
            const auto duration = negative_stop_duration(begin, end);
            if (!start || !duration) {
                report(stepmania_write_diagnostic_code_t::unsupported_feature,
                    "Warp cannot be converted to an exact negative STOP duration", "STOPS");
                continue;
            }
            stop_events.emplace_back(begin, 0, *start + "=" + *duration);
            report(stepmania_write_diagnostic_code_t::legacy_warp_conversion,
                "Warp emitted through legacy negative STOP encoding", "WARPS",
                diagnostic_severity_t::warning);
        }
    }
    void collect_pauses() {
        for (const auto& event : chart.pauses) {
            if (event.source_unit != pause_unit_t::seconds) {
                report(stepmania_write_diagnostic_code_t::unsupported_feature,
                    "StepMania export requires pause durations in seconds", "STOPS/DELAYS");
                continue;
            }
            const auto field = event.kind == pause_kind_t::stop ? "STOPS" : "DELAYS";
            const auto row = position_for(event.position, field);
            const auto beat = row ? beat_text(*row, field) : std::nullopt;
            const auto duration = value_text(event.duration, field);
            if (row && beat && duration)
                (event.kind == pause_kind_t::stop ? stop_events : delay_events)
                    .emplace_back(*row, event.provenance.source_order, *beat + "=" + *duration);
        }
    }
    void serialize_events(std::vector<event_t>& events, std::string key) {
        if (events.empty()) return;
        std::stable_sort(events.begin(), events.end());
        std::string value;
        for (const auto& event : events) {
            if (!value.empty()) value.push_back(',');
            value += std::get<2>(event);
        }
        result.emplace_back(std::move(key), std::move(value));
    }
    void append_offset() {
        if (chart.beat_zero_audio_offset_seconds.numerator ==
            std::numeric_limits<std::int64_t>::min()) {
            report(stepmania_write_diagnostic_code_t::unrepresentable_position,
                   "Beat-zero offset cannot be negated in the supported rational range", "OFFSET");
            return;
        }
        const auto offset = value_text({-chart.beat_zero_audio_offset_seconds.numerator,
                                        chart.beat_zero_audio_offset_seconds.denominator}, "OFFSET");
        if (offset) result.emplace_back("OFFSET", *offset);
    }
    void append_native_warps() {
        if (encoding != warp_encoding_t::native || warps.empty()) return;
        std::vector<event_t> events;
        for (const auto& warp : warps) {
            const auto [begin, end] = warp;
            const auto start = beat_text(begin, "WARPS");
            const auto length = beat_at_row(end - begin, chart.divisions_per_standard_measure);
            const auto duration = length ? rational_decimal(*length) : std::nullopt;
            if (start && duration) events.emplace_back(begin, 0, *start + "=" + *duration);
        }
        serialize_events(events, "WARPS");
    }
    void collect_scrolls() {
        std::vector<event_t> events;
        for (const auto& event : chart.scrolls) {
            const auto row = position_for(event.position, "SCROLLS");
            const auto beat = row ? beat_text(*row, "SCROLLS") : std::nullopt;
            const auto value = value_text(event.multiplier, "SCROLLS");
            if (row && beat && value)
                events.emplace_back(*row, event.provenance.source_order, *beat + "=" + *value);
        }
        serialize_events(events, "SCROLLS");
    }
    void collect_speeds() {
        std::vector<event_t> events;
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
            const auto duration = rational_decimal({
                static_cast<std::int64_t>(event.transition_beats.numerator),
                event.transition_beats.denominator});
            if (!duration) report(stepmania_write_diagnostic_code_t::unrepresentable_position,
                "SPEEDS duration cannot be written as an exact decimal", "SPEEDS");
            if (row && beat && multiplier && duration)
                events.emplace_back(*row, event.provenance.source_order,
                    *beat + "=" + *multiplier + "=" + *duration + "=" +
                    (event.transition_is_seconds ? "1" : "0"));
        }
        serialize_events(events, "SPEEDS");
    }
    auto build() -> std::vector<field_t_pair> {
        check_profile_support();
        collect_bpms();
        collect_warps();
        validate_legacy_warp_conflicts();
        encode_negative_bpm_warps();
        collect_pauses();
        encode_negative_stop_warps();
        append_offset();
        serialize_events(bpm_events, "BPMS");
        serialize_events(stop_events, "STOPS");
        serialize_events(delay_events, "DELAYS");
        append_native_warps();
        collect_scrolls();
        collect_speeds();
        return std::move(result);
    }
};

auto make_timing_fields(const chart_document_t& chart, const stepmania_write_options_t& options,
                        std::vector<stepmania_write_diagnostic_t>& diagnostics)
    -> std::vector<std::pair<std::string, std::string>> {
    return timing_fields_builder_t{chart, options, diagnostics}.build();
}


struct note_map_builder_t {
    struct cell_event_t { std::uint64_t row{}; std::uint32_t lane{}; char symbol{}; };
    const chart_document_t& chart;
    const lane_layout_t& layout;
    std::vector<stepmania_write_diagnostic_t>& diagnostics;
    std::size_t max_rows;
    std::size_t max_bytes;
    std::uint64_t divisions;
    std::vector<std::vector<cell_event_t>> by_measure;

    void report(stepmania_write_diagnostic_code_t code, std::string message) {
        diagnostics.push_back({diagnostic_severity_t::error, code, std::move(message), "NOTES"});
    }
    auto lane_index(lane_id_t lane) const -> std::optional<std::uint32_t> {
        const auto found = std::ranges::find(layout.lanes, lane, &lane_definition_t::id);
        if (found == layout.lanes.end()) return std::nullopt;
        return static_cast<std::uint32_t>(std::distance(layout.lanes.begin(), found));
    }
    auto put(local_position_t position, lane_id_t lane, char symbol) -> bool {
        const auto absolute = absolute_row_for(chart, position);
        const auto column = lane_index(lane);
        if (!absolute || !column) return false;
        const auto measure = static_cast<std::size_t>(*absolute / divisions);
        if (measure >= 65'536 || measure >= max_bytes) return false;
        if (by_measure.size() <= measure) by_measure.resize(measure + 1);
        by_measure[measure].push_back({*absolute % divisions, *column, symbol});
        return true;
    }
    auto symbol_for(note_kind_t kind) -> std::optional<char> {
        switch (kind) {
        case note_kind_t::tap: return '1';
        case note_kind_t::hold: return '2';
        case note_kind_t::roll: return '4';
        case note_kind_t::mine: return 'M';
        case note_kind_t::lift: return 'L';
        case note_kind_t::fake: return 'F';
        case note_kind_t::invisible:
            report(stepmania_write_diagnostic_code_t::invalid_note_layout,
                   "Invisible notes have no faithful StepMania note-map encoding");
            return std::nullopt;
        default:
            report(stepmania_write_diagnostic_code_t::invalid_note_layout,
                   "Unknown note kind has no StepMania note-map encoding");
            return std::nullopt;
        }
    }
    auto append_note(const note_t& note) -> bool {
        const auto head = symbol_for(note.kind);
        if (!head) return false;
        const auto is_hold = note.kind == note_kind_t::hold || note.kind == note_kind_t::roll;
        if (is_hold != note.end.has_value()) {
            report(stepmania_write_diagnostic_code_t::invalid_note_layout,
                   "Only hold and roll notes may carry a StepMania tail position");
            return false;
        }
        if (!put(note.position, note.lane, *head)) {
            report(stepmania_write_diagnostic_code_t::invalid_note_layout,
                   "Note does not reference a StepMania chart measure or lane");
            return false;
        }
        if (note.end && !put(*note.end, note.lane, '3')) {
            report(stepmania_write_diagnostic_code_t::invalid_note_layout,
                   "Hold tail does not reference a StepMania chart measure or lane");
            return false;
        }
        if (note.sound)
            report(stepmania_write_diagnostic_code_t::unsupported_feature,
                   "StepMania note sound bindings are not emitted by this codec slice");
        if (!note.semantic_layers.empty() || note.label)
            diagnostics.push_back({diagnostic_severity_t::warning,
                stepmania_write_diagnostic_code_t::unsupported_feature,
                "Note labels and semantic layers are editor metadata and are not emitted to StepMania",
                "NOTES"});
        return true;
    }
    auto required_rows(const std::vector<cell_event_t>& events) -> std::optional<std::uint64_t> {
        std::uint64_t rows{4};
        if (rows > max_rows) {
            report(stepmania_write_diagnostic_code_t::output_limit,
                   "Configured row cap is smaller than the minimum StepMania measure");
            return std::nullopt;
        }
        for (const auto& event : events) {
            const auto denominator = divisions / std::gcd(divisions, event.row);
            const auto combined = checked_lcm(rows, denominator);
            if (!combined || *combined > max_rows) {
                report(stepmania_write_diagnostic_code_t::output_limit,
                       "Note rows require more than the StepMania writer row limit");
                return std::nullopt;
            }
            rows = *combined;
        }
        return rows;
    }
    auto render_measure(std::size_t measure, std::string& output) -> bool {
        const auto rows = required_rows(by_measure[measure]);
        if (!rows) return false;
        std::vector<std::string> cells(static_cast<std::size_t>(*rows),
                                       std::string(layout.lanes.size(), '0'));
        for (const auto& event : by_measure[measure]) {
            const auto row = multiply_divide_exact(event.row, *rows, divisions);
            if (!row || *row >= *rows) {
                report(stepmania_write_diagnostic_code_t::invalid_note_layout,
                       "Note row cannot be represented on the emitted StepMania measure grid");
                return false;
            }
            auto& cell = cells[*row][event.lane];
            if (cell != '0') {
                report(stepmania_write_diagnostic_code_t::invalid_note_layout,
                       "Two note events collide in one StepMania lane cell");
                return false;
            }
            cell = event.symbol;
        }
        for (const auto& row : cells) {
            if (output.size() > max_bytes || row.size() + 1 > max_bytes - output.size()) {
                report(stepmania_write_diagnostic_code_t::output_limit,
                       "StepMania note map exceeds the configured output byte limit");
                return false;
            }
            output += row + "\n";
        }
        return true;
    }
    auto build() -> std::optional<std::string> {
        for (const auto& measure : chart.measures)
            for (const auto& note : measure.notes)
                if (!append_note(note)) return std::nullopt;
        std::string output;
        for (std::size_t measure = 0; measure < by_measure.size(); ++measure) {
            if (measure != 0) output += ",\n";
            if (!render_measure(measure, output)) return std::nullopt;
        }
        return output;
    }
};

auto note_map_text(const chart_document_t& chart,
                   std::vector<stepmania_write_diagnostic_t>& diagnostics,
                   std::size_t max_rows_per_measure, std::size_t max_output_bytes)
    -> std::optional<std::string> {
    if (chart.lane_layouts.empty()) {
        diagnostics.push_back({diagnostic_severity_t::error,
            stepmania_write_diagnostic_code_t::invalid_note_layout,
            "Chart has no StepMania lane layout", "NOTES"});
        return std::nullopt;
    }
    const auto& layout = chart.lane_layouts.front().layout;
    if (layout.lanes.empty() || layout.lanes.size() > 10) {
        diagnostics.push_back({diagnostic_severity_t::error,
            stepmania_write_diagnostic_code_t::invalid_note_layout,
            "StepMania note maps require between one and ten lanes", "NOTES"});
        return std::nullopt;
    }
    const auto changing_layout = std::ranges::any_of(chart.lane_layouts, [&](const auto& segment) {
        return segment.layout.lanes.size() != layout.lanes.size() ||
            std::ranges::any_of(segment.layout.lanes, [&](const auto& lane) {
                return std::ranges::find(layout.lanes, lane.id, &lane_definition_t::id) ==
                       layout.lanes.end();
            });
    });
    if (changing_layout) {
        diagnostics.push_back({diagnostic_severity_t::error,
            stepmania_write_diagnostic_code_t::invalid_note_layout,
            "Changing lane layouts are not representable by one StepMania chart", "NOTES"});
        return std::nullopt;
    }
    if (std::ranges::any_of(chart.measures, [](const auto& measure) {
            return measure.definition.length != rational_t{1, 1};
        })) {
        diagnostics.push_back({diagnostic_severity_t::error,
            stepmania_write_diagnostic_code_t::unsupported_feature,
            "Nonstandard measure lengths are not representable in StepMania note maps", "NOTES"});
        return std::nullopt;
    }
    const auto divisions = chart.divisions_per_standard_measure;
    if (divisions == 0 || chart.measures.size() > 65'536 ||
        chart.measures.size() > max_output_bytes) {
        diagnostics.push_back({diagnostic_severity_t::error,
            divisions == 0 ? stepmania_write_diagnostic_code_t::invalid_note_layout
                            : stepmania_write_diagnostic_code_t::output_limit,
            divisions == 0 ? "Chart has zero editor divisions per measure"
                           : "Chart measure count exceeds the StepMania writer cap", "NOTES"});
        return std::nullopt;
    }
    note_map_builder_t builder{chart, layout, diagnostics, max_rows_per_measure,
        max_output_bytes, divisions,
        std::vector<std::vector<note_map_builder_t::cell_event_t>>(
            std::max<std::size_t>(1, chart.measures.size()))};
    return builder.build();
}

} // namespace

auto stepmania_write_result_t::has_errors() const -> bool {
    return std::ranges::any_of(diagnostics, [](const auto& diagnostic) {
        return diagnostic.severity == diagnostic_severity_t::error;
    });
}

auto resolve_stepmania_export_policy(stepmania_export_policy_t policy)
    -> stepmania_export_policy_result_t {
    stepmania_export_policy_result_t result;
    if (policy.timing_syntax == stepmania_timing_syntax_t::unspecified) {
        result.error = stepmania_export_policy_error_t::timing_syntax_required;
        result.message = "Select a StepMania timing syntax explicitly";
        return result;
    }
    if (policy.implementation_profile !=
            stepmania_implementation_profile_t::unspecified &&
        policy.file_profile != stepmania_profile_t::ssc) {
        result.error =
            stepmania_export_policy_error_t::named_implementation_requires_ssc;
        result.message = "Named StepMania implementation targets currently require SSC output";
        return result;
    }
    if (policy.implementation_profile !=
            stepmania_implementation_profile_t::unspecified &&
        policy.timing_syntax != stepmania_timing_syntax_t::native_warps) {
        result.error =
            stepmania_export_policy_error_t::named_implementation_requires_native_warps;
        result.message =
            "The selected implementation target is audited only with native WARPS";
        return result;
    }

    stepmania_write_options_t options;
    options.profile = policy.file_profile;
    options.target_timing_syntax = policy.timing_syntax;
    options.target_implementation_profile = policy.implementation_profile;
    switch (policy.timing_syntax) {
    case stepmania_timing_syntax_t::native_warps:
        options.warp_encoding = warp_encoding_t::native;
        break;
    case stepmania_timing_syntax_t::legacy_negative_bpm:
        options.warp_encoding = warp_encoding_t::negative_bpm;
        break;
    case stepmania_timing_syntax_t::legacy_negative_stop:
        options.warp_encoding = warp_encoding_t::negative_stop;
        break;
    case stepmania_timing_syntax_t::unspecified:
        break;
    }
    result.options = std::move(options);
    return result;
}

struct stepmania_writer_t {
    const song_document_t& song;
    stepmania_write_options_t options;
    stepmania_write_result_t result;
    std::string output;
    std::vector<const chart_document_t*> charts;
    std::vector<chart_document_t> baked_charts;
    std::vector<std::vector<std::pair<std::string, std::string>>> timing_by_chart;
    std::set<std::string> global_timing_keys;
    const format_metadata_t* song_group{};

    void report(stepmania_write_diagnostic_code_t code, std::string message,
                std::string field, diagnostic_severity_t severity = diagnostic_severity_t::error) {
        result.diagnostics.push_back({severity, code, std::move(message), std::move(field)});
    }
    auto resolve_options() -> bool {
        if (options.target_implementation_profile !=
        stepmania_implementation_profile_t::unspecified) {
        const auto resolved = resolve_stepmania_export_policy(
            {.file_profile = options.profile,
             .timing_syntax = options.target_timing_syntax ==
                                      stepmania_timing_syntax_t::unspecified
                                  ? stepmania_timing_syntax_t::native_warps
                                  : options.target_timing_syntax,
             .implementation_profile = options.target_implementation_profile});
        if (!resolved) {
            report(stepmania_write_diagnostic_code_t::unsupported_feature,
                   resolved.message,
                   resolved.error ==
                           stepmania_export_policy_error_t::named_implementation_requires_ssc
                       ? "profile"
                       : "WARPS");
            return false;
        }
        options.target_timing_syntax = resolved.options->target_timing_syntax;
        options.warp_encoding = resolved.options->warp_encoding;
    }
        return true;
    }
    stepmania_writer_t(const song_document_t& source_song,
                       stepmania_write_options_t write_options)
        : song(source_song), options(std::move(write_options)) {}

    auto select_and_bake_charts() -> bool {
    for (const auto& chart : song.charts)
        if (!options.chart || chart.id == *options.chart)
            charts.push_back(&chart);
    if (charts.empty()) {
        report(stepmania_write_diagnostic_code_t::missing_chart,
               "No chart matches the requested StepMania output", "NOTES");
        return false;
    }
    if (charts.size() > options.max_charts) {
        report(stepmania_write_diagnostic_code_t::output_limit,
               "StepMania output exceeds the configured chart limit", "NOTES");
        return false;
    }
    if (options.max_output_bytes == 0 || options.max_rows_per_measure == 0) {
        report(stepmania_write_diagnostic_code_t::output_limit,
               "StepMania writer limits must be positive", "NOTES");
        return false;
    }
    baked_charts.reserve(charts.size());
    for (const auto* chart : charts) {
        auto baked = bake_sv_recipes_for_export(*chart);
        if (!baked) {
            report(stepmania_write_diagnostic_code_t::unrepresentable_position,
                   "SV recipe export bake failed: " + baked.diagnostics.front().message,
                   "SCROLLS/SPEEDS");
            return false;
        }
        baked_charts.push_back(std::move(*baked.chart));
    }
    charts.clear();
    charts.reserve(baked_charts.size());
    for (const auto& chart : baked_charts) {
        charts.push_back(&chart);
    }
        return true;
    }
    void emit(std::string_view key, std::string_view value) {
        emit_field(output, key, value, result.diagnostics, options.max_output_bytes);
    }
    auto music_path() -> std::optional<std::string> {
        const resource_entry_t* music{};
        for (const auto& resource : song.resources) {
            if (std::ranges::find(resource.roles, resource_role_t::music) == resource.roles.end())
                continue;
            if (music) {
                report(stepmania_write_diagnostic_code_t::unsupported_feature,
                       "StepMania MUSIC can represent only one music resource", "MUSIC");
                return std::nullopt;
            }
            music = &resource;
        }
        if (!music) return std::nullopt;
        const auto asset = std::ranges::find(song.audio_assets, music->asset, &audio_asset_t::id);
        if (asset == song.audio_assets.end() || asset->source_path.empty()) {
            report(stepmania_write_diagnostic_code_t::invalid_metadata,
                   "Music resource has no audio asset path", "MUSIC");
            return std::nullopt;
        }
        const auto& path = asset->source_path;
        if (path.empty() || path.is_absolute() || path.has_root_name() ||
            path.filename().empty() || path.lexically_normal() != path ||
            std::ranges::any_of(path, [](const auto& part) { return part == ".."; })) {
            report(stepmania_write_diagnostic_code_t::invalid_metadata,
                   "Music resource has an unsafe audio asset path", "MUSIC");
            return std::nullopt;
        }
        return path.generic_string();
    }
    void emit_song_metadata() {
    if (options.profile == stepmania_profile_t::ssc)
        emit("VERSION", "0.83");
    if (!song.metadata.title.empty())
        emit("TITLE", song.metadata.title);
    if (!song.metadata.artist.empty())
        emit("ARTIST", song.metadata.artist);
    song_group = family_group(song.metadata.format_groups);
    const auto modeled_music = music_path();
    if (modeled_music) emit("MUSIC", *modeled_music);
    std::set<std::string> modeled_song{
        "TITLE",     "ARTIST",     "OFFSET",     "BPMS",     "STOPS",       "DELAYS",
        "WARPS",     "SCROLLS",    "SPEEDS",     "NOTEDATA", "STEPSTYPE",   "DESCRIPTION",
        "CHARTNAME", "CHARTSTYLE", "DIFFICULTY", "METER",    "RADARVALUES", "NOTES"};
    if (modeled_music) modeled_song.insert("MUSIC");
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
    } else {
        modeled_song.insert("VERSION");
    }
    append_unknown_fields(output, song_group, modeled_song, options.preserve_unknown_fields,
                          result.diagnostics, options.max_output_bytes);
    }
    void prepare_timing() {
        timing_by_chart.reserve(charts.size());
        for (const auto* chart : charts)
            timing_by_chart.push_back(make_timing_fields(*chart, options, result.diagnostics));
        if (options.profile == stepmania_profile_t::sm) emit_shared_sm_timing();
        else emit_shared_ssc_timing();
    }
    void emit_shared_sm_timing() {
        for (std::size_t index = 1; index < timing_by_chart.size(); ++index) {
            if (timing_by_chart.front() != timing_by_chart[index])
                report(stepmania_write_diagnostic_code_t::conflicting_song_timing,
                       "SM stores timing at song scope; selected charts have different timing maps",
                       "BPMS/STOPS/DELAYS/WARPS/SCROLLS/SPEEDS");
        }
        for (const auto& [key, value] : timing_by_chart.front())
            emit(key, value);
    }
    void emit_shared_ssc_timing() {
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
    auto chart_step_type(const chart_document_t& chart, const format_metadata_t* group)
        -> std::optional<std::string> {
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
                return std::nullopt;
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
            return std::nullopt;
        }
        return step_type;
    }
    void emit_ssc_chart(const chart_document_t& chart, const format_metadata_t* group,
                        std::size_t chart_index, const std::string& step_type,
                        const std::string& description, const std::string& difficulty,
                        const std::string& meter, const std::string& radar,
                        const std::string& note_text) {
        const std::set<std::string> modeled_chart{
            "NOTEDATA", "STEPSTYPE",   "DESCRIPTION", "CHARTNAME", "CHARTSTYLE", "DIFFICULTY",
            "METER",    "RADARVALUES", "CREDIT",      "NOTES",     "OFFSET",     "BPMS",
            "STOPS",    "DELAYS",      "WARPS",       "SCROLLS",   "SPEEDS"};
        emit("NOTEDATA", "");
        emit("STEPSTYPE", step_type);
        if (!description.empty()) emit("DESCRIPTION", description);
        emit("DIFFICULTY", difficulty);
        emit("METER", meter);
        emit("RADARVALUES", radar);
        const auto chart_name = !chart.metadata.name.empty() ? chart.metadata.name
                                                             : group_value(group, "CHARTNAME");
        const auto chart_style = group_value(group, "CHARTSTYLE");
        if (!chart_name.empty()) emit("CHARTNAME", chart_name);
        if (!chart_style.empty()) emit("CHARTSTYLE", chart_style);
        if (!chart.metadata.author.empty()) emit("CREDIT", chart.metadata.author);
        append_unknown_fields(output, group, modeled_chart, options.preserve_unknown_fields,
                              result.diagnostics, options.max_output_bytes);
        for (const auto& [key, value] : timing_by_chart[chart_index])
            if (!global_timing_keys.contains(key)) emit(key, value);
        emit("NOTES", note_text);
    }
    void emit_sm_chart(const std::string& step_type, const std::string& description,
                       const std::string& difficulty, const std::string& meter,
                       const std::string& radar, const std::string& note_text) {
        const auto note_header = step_type + ":\n" + description + ":\n" + difficulty + ":\n" +
                                 meter + ":\n" + radar + ":\n";
        emit("NOTES", note_header + note_text);
    }
    auto emit_chart(const chart_document_t& chart, std::size_t chart_index) -> bool {
        const auto* group = family_group(chart.metadata.format_groups);
        const auto resolved_step_type = chart_step_type(chart, group);
        if (!resolved_step_type) return false;
        const auto& step_type = *resolved_step_type;
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
            return false;
        if (options.profile == stepmania_profile_t::ssc) {
            emit_ssc_chart(chart, group, chart_index, step_type, description, difficulty, meter,
                           radar, *note_text);
        } else {
            emit_sm_chart(step_type, description, difficulty, meter, radar, *note_text);
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
        return true;
    }
    auto run() -> stepmania_write_result_t {
        if (!resolve_options() || !select_and_bake_charts()) return std::move(result);
        emit_song_metadata();
        prepare_timing();
        for (std::size_t index = 0; index < charts.size(); ++index)
            emit_chart(*charts[index], index);
        if (!result.has_errors()) result.text = std::move(output);
        return std::move(result);
    }
};

auto write_stepmania(const song_document_t& song, stepmania_write_options_t options)
    -> stepmania_write_result_t {
    return stepmania_writer_t{song, std::move(options)}.run();
}

} // namespace wormedit
