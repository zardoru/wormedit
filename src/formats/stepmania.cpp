#include <wormedit/core/rational.h>
#include "../core/timing_arithmetic.h"
#include "../core/checked_math.h"
#include "../core/text_util.h"
#include "id_source.h"
#include <wormedit/formats/stepmania.h>

#include <wormedit/core/measure.h>
#include <wormedit/core/numeric_text.h>
#include <wormedit/core/sv_recipe.h>
#include <wormedit/core/lane_profiles.h>

#include <algorithm>
#include <cctype>
#include <array>
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

constexpr std::size_t sm_source_order_stride = 6;

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

using format_detail::id_source_t;

[[noreturn]] auto fail(stepmania_parse_diagnostic_code_t code, std::string message,
                       std::size_t order, std::optional<std::size_t> offset = {}) -> void {
    throw failure_t{code, std::move(message), order, offset};
}

auto trim(std::string_view text) -> std::string_view {
    return text_util::trim(text, " \t\r\n");
}

auto valid_utf8(std::string_view text) -> std::optional<std::size_t> {
    return text_util::invalid_utf8_offset(text);
}

auto parse_unsigned(std::string_view text, std::uint64_t& value) -> bool {
    return wormedit::parse_integer(trim(text), value);
}

auto decimal_rational(std::string_view text) -> std::optional<signed_rational_t> {
    const auto parsed = parse_exact_decimal(trim(text));
    // Offset and legacy timing conversions negate values; keep the signed range symmetric.
    return parsed && parsed->numerator != std::numeric_limits<std::int64_t>::min()
        ? parsed : std::nullopt;
}

auto checked_lcm(std::uint64_t left, std::uint64_t right) -> std::optional<std::uint64_t> {
    // Preserve StepMania's existing zero-input result; other domains reject zero.
    if (left == 0 || right == 0)
        return std::uint64_t{0};
    return checked_math::checked_lcm(left, right);
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
        if (text_util::ascii_upper(std::string(field.key)) == key)
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
    return stepmania_key_count(trim(step_type));
}

auto fallback_step_type_for_count(std::size_t count) -> std::optional<std::string> {
    const auto step_type = default_stepmania_step_type(count);
    if (step_type.empty()) return std::nullopt;
    return std::string(step_type);
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

struct note_cell_text_t {
    char symbol{};
    std::string_view index_digits;
};

struct normalized_row_t {
    std::string symbols;
    // Digit text of each cell's `[n]` suffix; empty when no cell in the row has one.
    std::vector<std::string> suffixes;
};

struct note_row_t {
    std::uint64_t position{};
    std::size_t line{};
    std::string symbols;
    std::vector<std::string> suffixes;
};

auto read_cell_index(std::string_view source, std::size_t& at, std::size_t source_order)
    -> std::string_view {
    const auto close = source.find(']', at);
    if (close == std::string_view::npos)
        fail(stepmania_parse_diagnostic_code_t::invalid_notes,
             "Note row has an unterminated keysound index", source_order);
    const auto digits = source.substr(at + 1, close - at - 1);
    if (digits.empty() || !std::ranges::all_of(digits, [](char digit) {
            return digit >= '0' && digit <= '9';
        }))
        fail(stepmania_parse_diagnostic_code_t::invalid_notes,
             "Note row keysound index must be decimal digits", source_order);
    at = close + 1;
    return digits;
}

auto split_note_cells(std::string_view source, std::size_t source_order)
    -> std::vector<note_cell_text_t> {
    std::vector<note_cell_text_t> cells;
    cells.reserve(source.size());
    std::size_t at{};
    while (at < source.size()) {
        note_cell_text_t cell{.symbol = source[at++]};
        if (at < source.size() && source[at] == '[')
            cell.index_digits = read_cell_index(source, at, source_order);
        cells.push_back(cell);
    }
    return cells;
}

auto normalize_note_row(std::string_view source, std::uint32_t lanes, std::size_t source_order)
    -> normalized_row_t {
    const auto cells = split_note_cells(source, source_order);
    if (cells.size() != lanes)
        fail(stepmania_parse_diagnostic_code_t::invalid_notes,
             "Note row column count does not match the declared step type", source_order);
    normalized_row_t result;
    result.symbols.reserve(lanes);
    for (const auto& cell : cells) {
        const auto symbol = text_util::ascii_upper(cell.symbol);
        if (symbol != '0' && symbol != '1' && symbol != '2' && symbol != '3' && symbol != '4' &&
            symbol != 'M' && symbol != 'L' && symbol != 'F' && symbol != 'K')
            fail(stepmania_parse_diagnostic_code_t::invalid_notes,
                 "Note map contains an unsupported StepMania note symbol", source_order);
        result.symbols.push_back(symbol);
    }
    if (std::ranges::any_of(cells, [](const auto& cell) { return !cell.index_digits.empty(); }))
        for (const auto& cell : cells)
            result.suffixes.emplace_back(cell.index_digits);
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
    -> std::vector<note_row_t> {
    const auto rows = note_rows_in_measure(raw_measure, lanes);
    std::vector<note_row_t> result;
    result.reserve(rows.size());
    for (std::size_t index = 0; index < rows.size(); ++index) {
        auto row = normalize_note_row(rows[index], lanes, source_order);
        const auto position = note_row_position(index, rows.size(), divisions, source_order);
        result.push_back({position, index + 1, std::move(row.symbols), std::move(row.suffixes)});
        event_count += static_cast<std::size_t>(std::ranges::count_if(
            result.back().symbols, [](char symbol) { return symbol != '0'; }));
        if (event_count > max_events)
            fail(stepmania_parse_diagnostic_code_t::event_limit,
                 "StepMania note map exceeds the configured event limit", source_order);
    }
    return result;
}

auto parse_row_map(std::string_view source, std::uint32_t lanes, std::uint64_t divisions,
                   std::size_t max_measures, std::size_t max_events, std::size_t source_order)
    -> std::vector<std::vector<note_row_t>> {
    const auto measures = split(source, ',');
    if (measures.size() > max_measures)
        fail(stepmania_parse_diagnostic_code_t::measure_limit,
             "StepMania note map exceeds the configured measure limit", source_order);
    std::vector<std::vector<note_row_t>> output;
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
    const auto folded_step_type = text_util::ascii_lowercase(step_type);
    const bool cardinal_dance = folded_step_type == "dance-single" ||
                                folded_step_type == "dance-double" ||
                                folded_step_type == "dance-couple";
    static constexpr std::array<lane_glyph_t, 4> dance_arrows{
        lane_glyph_t::arrow_left, lane_glyph_t::arrow_down,
        lane_glyph_t::arrow_up, lane_glyph_t::arrow_right};
    const auto named_specs = stepmania_lane_specs(folded_step_type);
    const bool named = named_specs.size() == count;
    for (std::uint32_t i = 0; i < count; ++i) {
        layout.lanes.push_back(
            {.id = ids.take<lane_id_t>(),
             .name = named ? std::string(named_specs[i].name)
                           : std::string(i < dance_names.size() ? dance_names[i] : "Column") +
                                 (i < dance_names.size() ? "" : " " + std::to_string(i + 1)),
             .role = lane_role_t::playable,
             .stage = count > 4 && i >= count / 2 ? stage_group_t::right : stage_group_t::left,
             .display_order = i,
             .glyph = cardinal_dance ? dance_arrows[i % dance_arrows.size()]
                       : named       ? named_specs[i].glyph
                                     : lane_glyph_t::bar});
    }
    layout.split_stage = count == 8 || count == 10;
    if (layout.split_stage)
        layout.split_after_column = count / 2;
    return layout;
}

auto stepmania_relative_path(std::string_view value) -> std::optional<std::filesystem::path> {
    if (value.empty() || value.find('\0') != std::string_view::npos)
        return std::nullopt;
    std::string normalized(value);
    std::ranges::replace(normalized, '\\', '/');
    if (normalized.front() == '/' ||
        (normalized.size() >= 2 &&
         ((normalized[0] >= 'A' && normalized[0] <= 'Z') ||
          (normalized[0] >= 'a' && normalized[0] <= 'z')) && normalized[1] == ':'))
        return std::nullopt;
    auto path = text_util::path_from_utf8(normalized);
    if (path.is_absolute() || path.has_root_name() || path.has_root_directory() ||
        path.filename().empty() ||
        std::ranges::any_of(path, [](const auto& component) { return component == ".."; }))
        return std::nullopt;
    return path.lexically_normal();
}

auto add_audio_resource(song_document_t& song, id_source_t& ids,
                        const stepmania_parse_options_t& options, const std::string& name,
                        const std::filesystem::path& path, resource_role_t role,
                        std::string source_field, std::size_t source_order) -> resource_id_t {
    std::error_code error;
    const bool missing = !options.asset_base_directory ||
                         !std::filesystem::exists(*options.asset_base_directory / path, error) ||
                         error;
    const auto asset = ids.take<asset_id_t>();
    const auto resource = ids.take<resource_id_t>();
    song.audio_assets.push_back({.id = asset, .source_path = path});
    song.resources.push_back({.id = resource,
                              .name = name,
                              .asset = asset,
                              .roles = {role},
                              .target_identities = {{format_family_t::stepmania, name,
                                                     "audio-path"}},
                              .missing = missing,
                              .provenance = {format_family_t::stepmania,
                                             options.profile == stepmania_profile_t::sm
                                                 ? "sm" : "ssc",
                                             std::move(source_field), name, source_order}});
    return resource;
}

void add_stepmania_music(const std::vector<field_t>& fields,
                         const stepmania_parse_options_t& options,
                         song_document_t& song, id_source_t& ids) {
    const auto* music = last_field(fields, "MUSIC");
    if (!music || trim(music->value).empty())
        return;
    const auto name = std::string(trim(music->value));
    const auto path = stepmania_relative_path(name);
    if (!path)
        return;
    add_audio_resource(song, ids, options, name, *path, resource_role_t::music, "MUSIC",
                       music->order);
}

// The song-level #KEYSOUNDS list. Entries keep their list position so a note's
// `[n]` index addresses them directly; an unusable entry stays as a placeholder.
struct keysound_list_t {
    std::vector<std::string> names;
    std::vector<std::optional<std::filesystem::path>> paths;
    std::size_t order{};
};

void append_keysound_warning(std::vector<stepmania_parse_diagnostic_t>& diagnostics,
                             const field_t& field, std::string message) {
    diagnostics.push_back({diagnostic_severity_t::warning,
                           stepmania_parse_diagnostic_code_t::malformed_tag,
                           std::move(message), {field.order, field.offset}});
}

auto read_keysound_list(const std::vector<field_t>& song_fields, stepmania_profile_t profile,
                        std::vector<stepmania_parse_diagnostic_t>& diagnostics)
    -> keysound_list_t {
    keysound_list_t list;
    const auto* field = last_field(song_fields, "KEYSOUNDS");
    if (!field || trim(field->value).empty())
        return list;
    list.order = field->order;
    std::string_view value = field->value;
    if (profile == stepmania_profile_t::ssc && value.starts_with("\\#"))
        value.remove_prefix(1);
    for (const auto entry : split(value, ',')) {
        const auto position = list.names.size();
        list.names.emplace_back(entry);
        list.paths.push_back(stepmania_relative_path(entry));
        if (entry.empty())
            append_keysound_warning(diagnostics, *field,
                "Empty #KEYSOUNDS entry " + std::to_string(position) + " is kept as a placeholder");
        else if (!list.paths.back())
            append_keysound_warning(diagnostics, *field,
                "#KEYSOUNDS entry " + std::to_string(position) +
                " is not a safe relative path and binds no sound");
    }
    return list;
}

struct keysound_context_t {
    keysound_list_t list;
    song_document_t& song;
    id_source_t& ids;
    const stepmania_parse_options_t& options;
    std::map<std::size_t, resource_id_t> resolved;
    std::map<std::string, resource_id_t> by_path;
};

struct note_cell_sound_t {
    std::optional<resource_id_t> resource;
    std::string suffix;
};

auto has_cell_sound_slot(char symbol) -> bool {
    return symbol != '0' && symbol != '3';
}

auto parse_keysound_index(const keysound_list_t& list, const std::string& digits)
    -> std::optional<std::size_t> {
    std::uint64_t index{};
    if (!parse_unsigned(digits, index) || index >= list.names.size())
        return std::nullopt;
    return static_cast<std::size_t>(index);
}

void register_keysound(keysound_context_t& context, std::size_t index) {
    const auto& path = context.list.paths[index];
    if (!path || context.resolved.contains(index))
        return;
    const auto key = path->generic_string();
    auto found = context.by_path.find(key);
    if (found == context.by_path.end())
        found = context.by_path.emplace(key, add_audio_resource(
            context.song, context.ids, context.options, context.list.names[index], *path,
            resource_role_t::sample, "KEYSOUNDS", context.list.order)).first;
    context.resolved.emplace(index, found->second);
}

// Registers the referenced files in list order so resource order follows #KEYSOUNDS.
void register_referenced_keysounds(keysound_context_t& context,
                                   const std::vector<std::vector<note_row_t>>& measures) {
    std::set<std::size_t> referenced;
    for (const auto& rows : measures)
        for (const auto& row : rows)
            for (std::size_t lane = 0; lane < row.suffixes.size(); ++lane)
                if (has_cell_sound_slot(row.symbols[lane]))
                    if (const auto index = parse_keysound_index(context.list, row.suffixes[lane]))
                        referenced.insert(*index);
    for (const auto index : referenced)
        register_keysound(context, index);
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
        group.ordered_field_source_orders.push_back(
            profile == stepmania_profile_t::sm
                ? field.order * sm_source_order_stride : field.order);
    }
    return group;
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
            std::erase_if(effective, [&](const auto& field) { return text_util::ascii_upper(std::string(field.key)) == key; });
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
            return to_long_double(left_beat) <
                   to_long_double(right_beat);
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
                    .location={field->order, field->offset}});
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
                    .location={field.order, field.offset}});
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

struct stepmania_parse_state_t {
    const stepmania_parse_options_t& options;
    id_source_t& ids;
    std::size_t& total_events;
    keysound_context_t& keysounds;
    std::vector<stepmania_parse_diagnostic_t>& diagnostics;
    const stepmania_profile_t& profile;
    const std::shared_ptr<const std::vector<std::byte>>& source_bytes;
};
void parse_timing_fields(stepmania_parse_state_t& context, const std::vector<field_t>& song_fields,
                         const std::vector<field_t>& chart_fields, chart_document_t& chart) {
    timing_parser_t{song_fields, chart_fields,         context.profile, chart,
                    context.ids, context.total_events, context.options, context.diagnostics}
        .parse_all();
}

struct pending_hold_t {
    note_kind_t kind;
    local_position_t position;
    std::size_t measure_index;
    note_cell_sound_t sound;
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

void append_hold_tail(stepmania_parse_state_t& context, chart_document_t& chart,
                      const lane_layout_t& layout, std::uint32_t lane_index,
                      local_position_t position,
                      std::map<std::uint32_t, pending_hold_t>& open_holds, const field_t& field) {
    const auto found = open_holds.find(lane_index);
    if (found == open_holds.end())
        fail(stepmania_parse_diagnostic_code_t::invalid_hold,
             "Hold tail has no matching head in its lane", field.order, field.offset);
    chart.measures[found->second.measure_index].notes.push_back(
        {.id = context.ids.take<object_id_t>(),
         .position = found->second.position,
         .lane = layout.lanes[lane_index].id,
         .kind = found->second.kind,
         .end = position,
         .sound = found->second.sound.resource,
         .provenance =
             note_provenance(context.profile, "hold" + found->second.sound.suffix, field)});
    open_holds.erase(found);
}

void append_note_symbol(stepmania_parse_state_t& context, chart_document_t& chart,
                        const lane_layout_t& layout, std::size_t measure_index,
                        std::uint32_t lane_index, char symbol, local_position_t position,
                        std::map<std::uint32_t, pending_hold_t>& open_holds, const field_t& field,
                        note_cell_sound_t sound) {
    if (symbol == '0') return;
    if (symbol == '2' || symbol == '4') {
        if (open_holds.contains(lane_index))
            fail(stepmania_parse_diagnostic_code_t::invalid_hold,
                 "A hold head appears before the previous hold tail", field.order, field.offset);
        open_holds.emplace(lane_index, pending_hold_t{
            symbol == '4' ? note_kind_t::roll : note_kind_t::hold, position, measure_index,
            std::move(sound)});
        return;
    }
    if (symbol == '3') {
        append_hold_tail(context, chart, layout, lane_index, position, open_holds, field);
        return;
    }
    chart.measures[measure_index].notes.push_back(
        {.id = context.ids.take<object_id_t>(),
         .position = position,
         .lane = layout.lanes[lane_index].id,
         .kind = note_kind_for_symbol(symbol),
         .sound = sound.resource,
         .provenance =
             note_provenance(context.profile, std::string(1, symbol) + sound.suffix, field)});
}

// Resolves a cell's `[n]` suffix. An index outside #KEYSOUNDS keeps the note
// without a sound and warns once, naming the measure and row.
auto cell_sound(const keysound_context_t& context, const note_row_t& row, std::uint32_t lane,
                std::size_t measure, const field_t& field,
                std::vector<stepmania_parse_diagnostic_t>& diagnostics) -> note_cell_sound_t {
    if (row.suffixes.empty() || row.suffixes[lane].empty() || !has_cell_sound_slot(row.symbols[lane]))
        return {};
    note_cell_sound_t result{.suffix = "[" + row.suffixes[lane] + "]"};
    const auto index = parse_keysound_index(context.list, row.suffixes[lane]);
    if (!index) {
        append_keysound_warning(diagnostics, field,
            "Keysound index " + row.suffixes[lane] + " in measure " +
            std::to_string(measure + 1) + " row " + std::to_string(row.line) +
            " is outside #KEYSOUNDS; the note keeps no sound");
        return result;
    }
    if (const auto found = context.resolved.find(*index); found != context.resolved.end())
        result.resource = found->second;
    return result;
}

// Returns the BGM source lane for the slot-th autokeysound of a row, creating
// lanes left to right on first use and reusing them on later rows.
auto autokeysound_lane(chart_document_t& chart, std::size_t slot, id_source_t& ids,
                       std::size_t source_order) -> object_id_t {
    while (chart.bgm_source_lanes.size() <= slot)
        chart.bgm_source_lanes.push_back({
            .id = ids.take<object_id_t>(),
            .name = "BGM " + std::to_string(chart.bgm_source_lanes.size() + 1),
            .source_order = source_order});
    return chart.bgm_source_lanes[slot].id;
}

struct autokeysound_cell_t {
    std::size_t measure{};
    std::size_t line{};
    std::uint32_t column{};
    std::size_t slot{};
    local_position_t position;
    note_cell_sound_t sound;
};

// A `K` cell plays its keysound without being judged, so it becomes a BGM event.
// One that would play nothing imports as nothing.
void append_autokeysound(stepmania_parse_state_t& context, chart_document_t& chart,
                         const autokeysound_cell_t& cell, const field_t& field) {
    if (!cell.sound.resource) {
        if (cell.sound.suffix.empty())
            append_keysound_warning(context.diagnostics, field,
                                    "Autokeysound in measure " + std::to_string(cell.measure + 1) +
                                        " row " + std::to_string(cell.line) +
                                        " has no keysound index and plays nothing");
        return;
    }
    const auto lane = autokeysound_lane(chart, cell.slot, context.ids, field.order);
    chart.measures[cell.measure].bgm_events.push_back(
        {.id = context.ids.take<object_id_t>(),
         .position = cell.position,
         .source_lane = lane,
         .sound = cell.sound.resource,
         .structural_slot = cell.slot,
         .provenance = note_provenance(
             context.profile, "K" + cell.sound.suffix + " column " + std::to_string(cell.column),
             field)});
}

void append_note_map_rows(stepmania_parse_state_t& context, chart_document_t& chart,
                          const std::vector<std::vector<note_row_t>>& rows,
                          std::uint32_t lane_count, const lane_layout_t& layout,
                          const field_t& note_field) {
    std::map<std::uint32_t, pending_hold_t> open_holds;
    for (std::size_t measure = 0; measure < rows.size(); ++measure) {
        for (const auto& row : rows[measure]) {
            const auto absolute = static_cast<std::uint64_t>(measure) *
                                  chart.divisions_per_standard_measure + row.position;
            const auto position = local_position(absolute, chart.divisions_per_standard_measure);
            std::size_t autokeysounds_in_row{};
            for (std::uint32_t lane = 0; lane < lane_count; ++lane) {
                if (row.symbols[lane] == '0') continue;
                if (++context.total_events > context.options.max_events)
                    fail(stepmania_parse_diagnostic_code_t::event_limit,
                         "StepMania file exceeds the configured event limit",
                         note_field.order, note_field.offset);
                auto sound = cell_sound(context.keysounds, row, lane, measure, note_field,
                                        context.diagnostics);
                if (row.symbols[lane] == 'K') {
                    const bool plays = sound.resource.has_value();
                    append_autokeysound(
                        context, chart,
                        {measure, row.line, lane, autokeysounds_in_row, position, std::move(sound)},
                        note_field);
                    if (plays) ++autokeysounds_in_row;
                    continue;
                }
                append_note_symbol(context, chart, layout, measure, lane, row.symbols[lane],
                                   position, open_holds, note_field, std::move(sound));
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

auto parse_one_chart(stepmania_parse_state_t& context, const chart_source_t& source,
                     const std::vector<field_t>& song_fields, std::size_t chart_index)
    -> chart_document_t {
    const auto [step_type, description, difficulty, meter_text, radar] =
        parse_note_source(source, context.profile, chart_index);
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
    if (chart.divisions_per_standard_measure > context.options.max_divisions_per_standard_measure)
        fail(stepmania_parse_diagnostic_code_t::division_limit,
             "Default StepMania editor grid exceeds the configured division limit", chart_index,
             note_field->offset);
    const auto* chart_name = last_field(source.fields, "CHARTNAME");
    chart.metadata.name = chart_name ? std::string(trim(chart_name->value)) : description;
    const auto* credit = last_field(
        context.profile == stepmania_profile_t::ssc ? source.fields : song_fields, "CREDIT");
    if (credit) chart.metadata.author = std::string(trim(credit->value));
    chart.metadata.format_groups.push_back(
        as_metadata_group(source.fields, context.profile, context.source_bytes));
    if (context.profile == stepmania_profile_t::sm) {
        const auto order = (note_field ? note_field->order : chart_index) *
                           sm_source_order_stride;
        auto& group = chart.metadata.format_groups.back();
        group.ordered_fields.emplace_back("STEPSTYPE", step_type);
        group.ordered_fields.emplace_back("DESCRIPTION", description);
        group.ordered_fields.emplace_back("DIFFICULTY", difficulty);
        group.ordered_fields.emplace_back("METER", meter_text);
        group.ordered_fields.emplace_back("RADARVALUES", radar);
        for (std::size_t subfield = 1; subfield <= 5; ++subfield)
            group.ordered_field_source_orders.push_back(order + subfield);
    }
    if (const auto rating = decimal_rational(meter_text); rating && rating->numerator >= 0)
        chart.metadata.rating = *rating;
    parse_timing_fields(context, song_fields, source.fields, chart);

    std::string_view note_map = note_field->value;
    if (context.profile == stepmania_profile_t::sm) {
        const auto colon = note_field->value.rfind(':');
        if (colon == std::string::npos)
            fail(stepmania_parse_diagnostic_code_t::invalid_notes,
                 "SM #NOTES is missing its note map", note_field->order, note_field->offset);
        note_map = std::string_view(note_field->value).substr(colon + 1);
    }
    const auto rows =
        parse_row_map(note_map, *lane_count, chart.divisions_per_standard_measure,
                      context.options.max_measures, context.options.max_events, note_field->order);
    auto layout = make_layout(step_type, *lane_count, context.ids);
    if (const auto input = make_default_input_profile_for(layout)) chart.input_profile = *input;
    chart.lane_layouts.push_back({.id = context.ids.take<object_id_t>(),
                                  .position = {measure_id_t{1}, 0},
                                  .layout = layout});
    auto measures = make_note_measures(rows.size());
    chart.measures = measures;
    register_referenced_keysounds(context.keysounds, rows);
    append_note_map_rows(context, chart, rows, *lane_count, layout, *note_field);
    measures = std::move(chart.measures);
    const auto max_measure = maximum_timing_measure(chart, measures.size());
    extend_note_measures(measures, max_measure, context.options.max_measures, chart_index,
                         *note_field);
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
        if (text_util::ascii_upper(std::string(field.key)) == "NOTES")
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
        const auto key = text_util::ascii_upper(std::string(field.key));
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
                {found[index]->order, found[index]->offset}});
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
    add_stepmania_music(song_fields, options, song, ids);
    std::size_t total_events{};
    keysound_context_t keysounds{read_keysound_list(song_fields, options.profile, diagnostics),
                                 song, ids, options, {}, {}};
    stepmania_parse_state_t context{
        options, ids, total_events, keysounds, diagnostics, options.profile, source_bytes};
    for (std::size_t index = 0; index < charts.size(); ++index)
        song.charts.push_back(parse_one_chart(context, charts[index], song_fields, index));
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
            .location={0, 0}});
        return std::nullopt;
    }
    const auto* data = reinterpret_cast<const char*>(bytes.data());
    auto source = std::string_view(data, bytes.size());
    if (const auto invalid = valid_utf8(source)) {
        diagnostics.push_back({.severity=diagnostic_severity_t::error,
            .code=stepmania_parse_diagnostic_code_t::invalid_encoding,
            .message="StepMania source is not valid UTF-8", .location={0, *invalid}});
        return std::nullopt;
    }
    if (source.starts_with("\xef\xbb\xbf")) source.remove_prefix(3);
    return source;
}

} // namespace

auto parse_stepmania_bytes(std::span<const std::byte> bytes, stepmania_parse_options_t options)
    -> stepmania_parse_outcome_t {
    stepmania_parse_outcome_t result{};
    const auto source = validate_input_bytes(bytes, options, result.diagnostics);
    if (!source) return result;
    try {
        auto song = parse_stepmania_document(bytes, *source, options, result.diagnostics);
        if (result.ok()) result.value = std::move(song);
    } catch (const failure_t& failure) {
        result.diagnostics.push_back({.severity=diagnostic_severity_t::error,
            .code=failure.code, .message=failure.message,
            .location={failure.order, failure.offset}});
    } catch (const std::bad_alloc&) {
        result.diagnostics.push_back({.severity=diagnostic_severity_t::error,
            .code=stepmania_parse_diagnostic_code_t::source_limit,
            .message="Insufficient memory within configured parser limits",
            .location={0, std::nullopt}});
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
    return timing_arithmetic::rows_to_beat_fraction(row, divisions);
}

auto absolute_row_for(const chart_document_t& chart, local_position_t position)
    -> std::optional<std::uint64_t> {
    if (position.row >= chart.divisions_per_standard_measure)
        return std::nullopt;
    const auto found = chart.find_measure(position.measure);
    if (found == nullptr)
        return std::nullopt;
    const auto index = static_cast<std::uint64_t>(found - chart.measures.data());
    if (index > (std::numeric_limits<std::uint64_t>::max() - position.row) /
                    chart.divisions_per_standard_measure)
        return std::nullopt;
    return index * chart.divisions_per_standard_measure + position.row;
}

auto is_valid_tag_key(std::string_view key) -> bool {
    if (key.empty() || key.size() > 64 ||
        !((key.front() >= 'A' && key.front() <= 'Z') || (key.front() >= 'a' && key.front() <= 'z')))
        return false;
    return std::ranges::all_of(key, [](char character) {
        return (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') ||
               (character >= '0' && character <= '9') || character == '_';
    });
}

auto is_valid_tag_value(std::string_view value) -> bool {
    return value.find(';') == std::string_view::npos && value.find('\0') == std::string_view::npos;
}

auto emit_field(std::string& output, std::string_view key, std::string_view value,
                std::vector<stepmania_write_diagnostic_t>& diagnostics,
                std::size_t max_output_bytes) -> void {
    if (!is_valid_tag_key(key) || !is_valid_tag_value(value)) {
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
        if (text_util::ascii_upper(std::string(name)) == key)
            result = value;
    return std::string(trim(result));
}

auto has_group_field(const format_metadata_t* group, std::string_view key) -> bool {
    return group && std::ranges::any_of(group->ordered_fields, [&](const auto& field) {
               return text_util::ascii_upper(std::string(field.first)) == key;
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
        if (modeled.contains(text_util::ascii_upper(std::string(key))))
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

// The song-level #KEYSOUNDS list as written: `names` keeps list positions (an
// empty name is a placeholder), `bound` marks entries some resource maps to.
struct keysound_table_t {
    std::vector<std::string> names;
    std::vector<bool> bound;
    std::map<resource_id_t, std::size_t> indices;

    static auto spelled_digits(std::string_view spelling) -> std::optional<std::string> {
        const auto open = spelling.find('[');
        const auto close = spelling.find(']', open == std::string_view::npos ? 0 : open);
        if (open == std::string_view::npos || close == std::string_view::npos) return std::nullopt;
        return std::string(spelling.substr(open + 1, close - open - 1));
    }
    static auto digits_value(const std::string& digits) -> std::optional<std::uint64_t> {
        std::uint64_t value{};
        const auto end = digits.data() + digits.size();
        const auto parsed = std::from_chars(digits.data(), end, value);
        if (parsed.ec != std::errc{} || parsed.ptr != end) return std::nullopt;
        return value;
    }
    // `[n]` text for a sounded cell. An unchanged index keeps its original
    // spelling; a sound-less cell keeps an original index that bound nothing.
    auto suffix(const std::optional<resource_id_t>& sound, std::string_view spelling) const
        -> std::string {
        const auto digits = spelled_digits(spelling);
        if (sound) {
            const auto found = indices.find(*sound);
            if (found == indices.end()) return {};
            if (digits && digits_value(*digits) == found->second) return "[" + *digits + "]";
            return "[" + std::to_string(found->second) + "]";
        }
        if (!digits) return {};
        const auto index = digits_value(*digits);
        if (index && *index < names.size() && bound[*index]) return {};
        return "[" + *digits + "]";
    }
};

struct note_map_builder_t {
    struct cell_event_t {
        std::uint64_t row{}; std::uint32_t lane{}; char symbol{}; std::string suffix{};
    };
    const chart_document_t& chart;
    const lane_layout_t& layout;
    std::vector<stepmania_write_diagnostic_t>& diagnostics;
    std::size_t max_rows;
    std::size_t max_bytes;
    std::uint64_t divisions;
    std::vector<std::vector<cell_event_t>> by_measure;
    const keysound_table_t& keysounds;
    std::set<std::tuple<std::size_t, std::uint64_t, std::uint32_t>> occupied;
    std::size_t dropped_autokeysounds{};

    void report(stepmania_write_diagnostic_code_t code, std::string message) {
        diagnostics.push_back({diagnostic_severity_t::error, code, std::move(message), "NOTES"});
    }
    auto lane_index(lane_id_t lane) const -> std::optional<std::uint32_t> {
        const auto found = layout.find(lane);
        if (found == nullptr) return std::nullopt;
        return static_cast<std::uint32_t>(found - layout.lanes.data());
    }
    auto put_cell(local_position_t position, std::uint32_t column, char symbol,
                  std::string suffix) -> bool {
        const auto absolute = absolute_row_for(chart, position);
        if (!absolute) return false;
        const auto measure = static_cast<std::size_t>(*absolute / divisions);
        if (measure >= 65'536 || measure >= max_bytes) return false;
        if (by_measure.size() <= measure) by_measure.resize(measure + 1);
        by_measure[measure].push_back({*absolute % divisions, column, symbol, std::move(suffix)});
        occupied.emplace(measure, *absolute % divisions, column);
        return true;
    }
    auto put(local_position_t position, lane_id_t lane, char symbol, std::string suffix = {})
        -> bool {
        const auto column = lane_index(lane);
        return column && put_cell(position, *column, symbol, std::move(suffix));
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
        if (!put(note.position, note.lane, *head,
                 keysounds.suffix(note.sound, note.provenance.source_spelling))) {
            report(stepmania_write_diagnostic_code_t::invalid_note_layout,
                   "Note does not reference a StepMania chart measure or lane");
            return false;
        }
        if (note.end && !put(*note.end, note.lane, '3')) {
            report(stepmania_write_diagnostic_code_t::invalid_note_layout,
                   "Hold tail does not reference a StepMania chart measure or lane");
            return false;
        }
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
    static auto with_suffixes(const std::string& symbols, std::size_t line,
                              const std::map<std::pair<std::size_t, std::uint32_t>,
                                             std::string>& suffixes) -> std::string {
        if (suffixes.empty()) return symbols;
        std::string text;
        for (std::uint32_t lane = 0; lane < symbols.size(); ++lane) {
            text += symbols[lane];
            if (const auto found = suffixes.find({line, lane}); found != suffixes.end())
                text += found->second;
        }
        return text;
    }
    // StepMania's column for an autokeysound: the one it was imported from when
    // that cell is free, else the first free column of the row.
    auto autokeysound_column(std::size_t measure, std::uint64_t row, std::string_view spelling)
        -> std::optional<std::uint32_t> {
        const auto lanes = static_cast<std::uint32_t>(layout.lanes.size());
        const auto free_at = [&](std::uint32_t column) {
            return !occupied.contains({measure, row, column});
        };
        const auto marker = spelling.find("column ");
        if (marker != std::string_view::npos) {
            const auto digits = std::string(spelling.substr(marker + 7));
            if (const auto value = keysound_table_t::digits_value(digits);
                value && *value < lanes && free_at(static_cast<std::uint32_t>(*value)))
                return static_cast<std::uint32_t>(*value);
        }
        for (std::uint32_t column = 0; column < lanes; ++column)
            if (free_at(column)) return column;
        return std::nullopt;
    }
    void place_autokeysound(const bgm_event_t& event) {
        const auto suffix = keysounds.suffix(event.sound, event.provenance.source_spelling);
        if (!event.sound || suffix.empty()) return;
        const auto absolute = absolute_row_for(chart, event.position);
        const auto measure = absolute ? static_cast<std::size_t>(*absolute / divisions) : 0;
        const auto column = absolute ? autokeysound_column(measure, *absolute % divisions,
                                                          event.provenance.source_spelling)
                                     : std::nullopt;
        if (!column || !put_cell(event.position, *column, 'K', suffix)) ++dropped_autokeysounds;
    }
    void place_autokeysounds() {
        for (const auto& measure : chart.measures)
            for (const auto& event : measure.bgm_events) place_autokeysound(event);
        if (dropped_autokeysounds != 0)
            diagnostics.push_back({diagnostic_severity_t::warning,
                stepmania_write_diagnostic_code_t::unsupported_feature,
                std::to_string(dropped_autokeysounds) +
                    " BGM event(s) were not written because no StepMania column was free at "
                    "their row", "NOTES"});
    }
    auto render_measure(std::size_t measure, std::string& output) -> bool {
        const auto rows = required_rows(by_measure[measure]);
        if (!rows) return false;
        std::vector<std::string> cells(static_cast<std::size_t>(*rows),
                                       std::string(layout.lanes.size(), '0'));
        std::map<std::pair<std::size_t, std::uint32_t>, std::string> suffixes;
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
            if (!event.suffix.empty())
                suffixes.emplace(std::pair{static_cast<std::size_t>(*row), event.lane},
                                 event.suffix);
        }
        for (std::size_t line = 0; line < cells.size(); ++line) {
            const auto row = with_suffixes(cells[line], line, suffixes);
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
        place_autokeysounds();
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
                   std::size_t max_rows_per_measure, std::size_t max_output_bytes,
                   const keysound_table_t& keysounds)
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
                return layout.find(lane.id) ==
                       nullptr;
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
    note_map_builder_t builder{.chart = chart, .layout = layout, .diagnostics = diagnostics,
        .max_rows = max_rows_per_measure, .max_bytes = max_output_bytes, .divisions = divisions,
        .by_measure = std::vector<std::vector<note_map_builder_t::cell_event_t>>(
            std::max<std::size_t>(1, chart.measures.size())),
        .keysounds = keysounds};
    return builder.build();
}

} // namespace

auto stepmania_write_options_t::from(stepmania_export_policy_t policy)
    -> stepmania_export_policy_outcome_t {
    error_t<failure_code_t> result{};
    if (policy.timing_syntax == stepmania_timing_syntax_t::unspecified) {
        result.code = failure_code_t::invalid_request;
        result.message = "Select a StepMania timing syntax explicitly";
        return std::unexpected(std::move(result));
    }
    if (policy.implementation_profile !=
            stepmania_implementation_profile_t::unspecified &&
        policy.file_profile != stepmania_profile_t::ssc) {
        result.code =
            failure_code_t::unsupported;
        result.message = "Named StepMania implementation targets currently require SSC output";
        return std::unexpected(std::move(result));
    }
    if (policy.implementation_profile !=
            stepmania_implementation_profile_t::unspecified &&
        policy.timing_syntax != stepmania_timing_syntax_t::native_warps) {
        result.code =
            failure_code_t::conflict;
        result.message =
            "The selected implementation target is audited only with native WARPS";
        return std::unexpected(std::move(result));
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
    return options;
}

class stepmania_writer_t {
    const song_document_t& song_;
    stepmania_write_options_t options_;
    stepmania_write_outcome_t result_{};
    std::string output_;
    std::vector<const chart_document_t*> charts_;
    std::vector<chart_document_t> baked_charts_;
    std::vector<std::vector<std::pair<std::string, std::string>>> timing_by_chart_;
    std::set<std::string> global_timing_keys_;
    const format_metadata_t* song_group_{};
    keysound_table_t keysounds_;

    void report(stepmania_write_diagnostic_code_t code, std::string message,
                std::string field, diagnostic_severity_t severity = diagnostic_severity_t::error) {
        result_.diagnostics.push_back({severity, code, std::move(message), std::move(field)});
    }
    auto resolve_options() -> bool {
        if (options_.target_implementation_profile !=
        stepmania_implementation_profile_t::unspecified) {
        const auto resolved = stepmania_write_options_t::from(
            {.file_profile = options_.profile,
             .timing_syntax = options_.target_timing_syntax ==
                                      stepmania_timing_syntax_t::unspecified
                                  ? stepmania_timing_syntax_t::native_warps
                                  : options_.target_timing_syntax,
             .implementation_profile = options_.target_implementation_profile});
        if (!resolved) {
            report(stepmania_write_diagnostic_code_t::unsupported_feature,
                   resolved.error().message,
                   resolved.error().code ==
                           failure_code_t::unsupported
                       ? "profile"
                       : "WARPS");
            return false;
        }
        options_.target_timing_syntax = resolved->target_timing_syntax;
        options_.warp_encoding = resolved->warp_encoding;
    }
        return true;
    }
  public:
    stepmania_writer_t(const song_document_t& source_song,
                       stepmania_write_options_t write_options)
        : song_(source_song), options_(std::move(write_options)) {}

  private:

    auto select_and_bake_charts() -> bool {
    for (const auto& chart : song_.charts)
        if (!options_.chart || chart.id == *options_.chart)
            charts_.push_back(&chart);
    if (charts_.empty()) {
        report(stepmania_write_diagnostic_code_t::missing_chart,
               "No chart matches the requested StepMania output", "NOTES");
        return false;
    }
    if (charts_.size() > options_.max_charts) {
        report(stepmania_write_diagnostic_code_t::output_limit,
               "StepMania output exceeds the configured chart limit", "NOTES");
        return false;
    }
    if (options_.max_output_bytes == 0 || options_.max_rows_per_measure == 0) {
        report(stepmania_write_diagnostic_code_t::output_limit,
               "StepMania writer limits must be positive", "NOTES");
        return false;
    }
    baked_charts_.reserve(charts_.size());
    for (const auto* chart : charts_) {
        auto baked = bake_sv_recipes_for_export(*chart);
        if (!baked.value || !baked.diagnostics.empty()) {
            report(stepmania_write_diagnostic_code_t::unrepresentable_position,
                   "SV recipe export bake failed: " + baked.diagnostics.front().message,
                   "SCROLLS/SPEEDS");
            return false;
        }
        baked_charts_.push_back(std::move(*baked.value));
    }
    charts_.clear();
    charts_.reserve(baked_charts_.size());
    for (const auto& chart : baked_charts_) {
        charts_.push_back(&chart);
    }
        return true;
    }
    void emit(std::string_view key, std::string_view value) {
        emit_field(output_, key, value, result_.diagnostics, options_.max_output_bytes);
    }
    auto music_path() -> std::optional<std::string> {
        const resource_entry_t* music{};
        for (const auto& resource : song_.resources) {
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
        const auto asset = song_.find_audio_asset(music->asset);
        if (asset == nullptr || asset->source_path.empty()) {
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
        const auto authored = std::ranges::find_if(
            music->target_identities, [](const auto& identity) {
                return identity.family == format_family_t::stepmania &&
                       identity.namespace_name == "audio-path";
            });
        auto normalized_authored_path = authored == music->target_identities.end()
                                            ? std::string{}
                                            : authored->identifier;
        std::ranges::replace(normalized_authored_path, '\\', '/');
        if (authored != music->target_identities.end() &&
            music->name == authored->identifier &&
            path.generic_string() == normalized_authored_path)
            return authored->identifier;
        return path.generic_string();
    }
    auto referenced_sounds() const -> std::vector<resource_id_t> {
        std::vector<resource_id_t> ordered;
        std::set<resource_id_t> seen;
        const auto note = [&](const std::optional<resource_id_t>& sound) {
            if (sound && seen.insert(*sound).second) ordered.push_back(*sound);
        };
        for (const auto* chart : charts_)
            for (const auto& measure : chart->measures) {
                for (const auto& item : measure.notes) note(item.sound);
                for (const auto& item : measure.bgm_events) note(item.sound);
            }
        return ordered;
    }
    // The name a resource has in #KEYSOUNDS, or nothing when its file cannot be
    // written as a relative path that survives the comma-separated list.
    auto keysound_name(const resource_entry_t& resource) const -> std::optional<std::string> {
        const auto asset = song_.find_audio_asset(resource.asset);
        if (asset == nullptr) return std::nullopt;
        const auto authored = std::ranges::find_if(
            resource.target_identities, [](const auto& identity) {
                return identity.family == format_family_t::stepmania &&
                       identity.namespace_name == "audio-path";
            });
        auto name = asset->source_path.generic_string();
        if (authored != resource.target_identities.end() && resource.name == authored->identifier) {
            auto normalized = authored->identifier;
            std::ranges::replace(normalized, '\\', '/');
            if (normalized == name) name = authored->identifier;
        }
        if (name.empty() || name.find_first_of(",;\r\n") != std::string::npos ||
            asset->source_path.lexically_normal() != asset->source_path ||
            !stepmania_relative_path(name))
            return std::nullopt;
        return name;
    }
    auto original_keysound_names() const -> std::vector<std::string> {
        std::optional<std::string> value;
        if (song_group_)
            for (const auto& [key, text] : song_group_->ordered_fields)
                if (text_util::ascii_upper(std::string(key)) == "KEYSOUNDS") value = text;
        if (!value || trim(*value).empty()) return {};
        std::string_view text = *value;
        if (text.starts_with("\\#")) text.remove_prefix(1);
        std::vector<std::string> names;
        for (const auto name : split(text, ',')) names.emplace_back(name);
        return names;
    }
    void bind_keysound(const resource_entry_t& resource) {
        const auto name = keysound_name(resource);
        if (!name) {
            report(stepmania_write_diagnostic_code_t::unsupported_feature,
                   "Sound " + resource.name + " cannot be written as a StepMania keysound path",
                   "KEYSOUNDS", diagnostic_severity_t::warning);
            return;
        }
        auto found = std::ranges::find(keysounds_.names, *name);
        if (found == keysounds_.names.end()) {
            keysounds_.names.push_back(*name);
            keysounds_.bound.push_back(false);
            found = std::prev(keysounds_.names.end());
        }
        const auto index = static_cast<std::size_t>(std::distance(keysounds_.names.begin(), found));
        keysounds_.bound[index] = true;
        keysounds_.indices.emplace(resource.id, index);
    }
    void build_keysound_table() {
        keysounds_.names = original_keysound_names();
        keysounds_.bound.assign(keysounds_.names.size(), false);
        for (const auto id : referenced_sounds()) {
            const auto found = std::ranges::find(song_.resources, id, &resource_entry_t::id);
            if (found != song_.resources.end()) bind_keysound(*found);
        }
    }
    void emit_keysounds() {
        build_keysound_table();
        if (keysounds_.names.empty()) return;
        std::string value;
        for (std::size_t index = 0; index < keysounds_.names.size(); ++index)
            value += (index == 0 ? "" : ",") + keysounds_.names[index];
        if (options_.profile == stepmania_profile_t::ssc && keysounds_.names.front().starts_with('#'))
            value.insert(0, "\\");
        emit("KEYSOUNDS", value);
    }
    void emit_song_metadata() {
    if (options_.profile == stepmania_profile_t::ssc)
        emit("VERSION", "0.83");
    if (!song_.metadata.title.empty())
        emit("TITLE", song_.metadata.title);
    if (!song_.metadata.artist.empty())
        emit("ARTIST", song_.metadata.artist);
    song_group_ = family_group(song_.metadata.format_groups);
    const auto modeled_music = music_path();
    if (modeled_music) emit("MUSIC", *modeled_music);
    emit_keysounds();
    std::set<std::string> modeled_song{
        "TITLE",     "ARTIST",     "OFFSET",     "BPMS",     "STOPS",       "DELAYS",
        "WARPS",     "SCROLLS",    "SPEEDS",     "NOTEDATA", "STEPSTYPE",   "DESCRIPTION",
        "CHARTNAME", "CHARTSTYLE", "DIFFICULTY", "METER",    "RADARVALUES", "NOTES",
        "KEYSOUNDS"};
    const auto source_music = group_value(song_group_, "MUSIC");
    if (modeled_music || stepmania_relative_path(source_music)) modeled_song.insert("MUSIC");
    if (options_.profile == stepmania_profile_t::sm) {
        modeled_song.insert("CREDIT");
        const auto& author = charts_.front()->metadata.author;
        if (std::ranges::any_of(
                charts_, [&](const auto* chart) { return chart->metadata.author != author; })) {
            report(stepmania_write_diagnostic_code_t::conflicting_song_metadata,
                   "SM stores CREDIT at song scope; selected charts have different authors",
                   "CREDIT");
        } else if (!author.empty()) {
            emit("CREDIT", author);
        }
    } else {
        modeled_song.insert("VERSION");
    }
    append_unknown_fields(output_, song_group_, modeled_song, options_.preserve_unknown_fields,
                          result_.diagnostics, options_.max_output_bytes);
    }
    void prepare_timing() {
        timing_by_chart_.reserve(charts_.size());
        for (const auto* chart : charts_)
            timing_by_chart_.push_back(make_timing_fields(*chart, options_, result_.diagnostics));
        if (options_.profile == stepmania_profile_t::sm) emit_shared_sm_timing();
        else emit_shared_ssc_timing();
    }
    void emit_shared_sm_timing() {
        for (std::size_t index = 1; index < timing_by_chart_.size(); ++index) {
            if (timing_by_chart_.front() != timing_by_chart_[index])
                report(stepmania_write_diagnostic_code_t::conflicting_song_timing,
                       "SM stores timing at song scope; selected charts have different timing maps",
                       "BPMS/STOPS/DELAYS/WARPS/SCROLLS/SPEEDS");
        }
        for (const auto& [key, value] : timing_by_chart_.front())
            emit(key, value);
    }
    void emit_shared_ssc_timing() {
        constexpr std::array<std::string_view, 7> timing_keys{
            "OFFSET", "BPMS", "STOPS", "DELAYS", "WARPS", "SCROLLS", "SPEEDS"};
        for (const auto key : timing_keys) {
            if (!has_group_field(song_group_, key))
                continue;
            bool chart_override{};
            for (const auto* chart : charts_)
                chart_override = chart_override ||
                                 has_group_field(family_group(chart->metadata.format_groups), key);
            if (chart_override)
                continue;
            const auto first = std::ranges::find(timing_by_chart_.front(), key,
                                                 &std::pair<std::string, std::string>::first);
            if (first == timing_by_chart_.front().end())
                continue;
            const bool shared = std::ranges::all_of(timing_by_chart_, [&](const auto& timing) {
                const auto found =
                    std::ranges::find(timing, key, &std::pair<std::string, std::string>::first);
                return found != timing.end() && found->second == first->second;
            });
            if (shared) {
                global_timing_keys_.insert(std::string(key));
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
        append_unknown_fields(output_, group, modeled_chart, options_.preserve_unknown_fields,
                              result_.diagnostics, options_.max_output_bytes);
        for (const auto& [key, value] : timing_by_chart_[chart_index])
            if (!global_timing_keys_.contains(key)) emit(key, value);
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
            options_.profile == stepmania_profile_t::sm
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
            chart, result_.diagnostics, options_.max_rows_per_measure, options_.max_output_bytes,
            keysounds_);
        if (!note_text)
            return false;
        if (options_.profile == stepmania_profile_t::ssc) {
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
        return true;
    }
  public:
    auto run() -> stepmania_write_outcome_t {
        if (!resolve_options() || !select_and_bake_charts()) return std::move(result_);
        emit_song_metadata();
        prepare_timing();
        for (std::size_t index = 0; index < charts_.size(); ++index)
            emit_chart(*charts_[index], index);
        if (result_.ok()) result_.value = std::move(output_);
        return std::move(result_);
    }
};

auto write_stepmania(const song_document_t& song, stepmania_write_options_t options)
    -> stepmania_write_outcome_t {
    return stepmania_writer_t{song, std::move(options)}.run();
}

} // namespace wormedit
