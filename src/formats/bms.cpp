#include "bms_timing_directive.h"
#include "bms_channel.h"
#include "bms_program_import_internal.h"
#include <wormedit/core/catalogs.h>
#include "text_codec_result.h"
#include <wormedit/core/bms36.h>
#include "../core/checked_math.h"
#include "../core/text_util.h"
#include <wormedit/core/bms_profile_detection.h>
#include <wormedit/core/numeric_text.h>
#include <wormedit/formats/bms.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>

#if defined(WORMEDIT_HAS_ICONV)
#include <iconv.h>
#elif defined(_WIN32)
#include "win32_codepage.h"
#endif

namespace wormedit {
namespace {

struct source_line_t {
    std::string text;
    bms_source_span_t span;
    bms_line_ending_t ending{bms_line_ending_t::none};
    std::size_t order{};
};

struct parsed_line_t {
    std::string token;
    std::string raw_value;
    std::string semantic_value;
    bool has_separator{};
    char separator{};
};

enum class frame_kind_t {
    random,
    if_group,
    switch_block,
};

struct open_frame_t {
    frame_kind_t kind{frame_kind_t::random};
    std::vector<std::size_t> parent_path;
    std::vector<std::size_t> path;
    std::optional<std::vector<std::size_t>> active_branch_path;
    bool terminal_branch_seen{};
};

auto equal_ascii_case_insensitive(std::string_view left, std::string_view right) -> bool {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        const auto a = static_cast<unsigned char>(left[index]);
        const auto b = static_cast<unsigned char>(right[index]);
        if (text_util::ascii_upper(static_cast<char>(a)) != text_util::ascii_upper(static_cast<char>(b))) {
            return false;
        }
    }
    return true;
}

auto trim_ascii(std::string_view value) -> std::string_view {
    return text_util::trim(value, " \t");
}

using text_util::path_from_utf8;

using text_util::invalid_utf8_offset;





auto decode_diagnostic(bms_parse_outcome_t& result, bms_parse_diagnostic_code_t code,
                       std::string message, std::size_t byte_offset, std::size_t byte_end) -> void {
    result.diagnostics.push_back({
        .severity = diagnostic_severity_t::error,
        .code = code,
        .message = std::move(message),
        .location = {1, byte_offset + 1, bms_source_span_t{byte_offset, byte_end}},
    });
}

auto add_line_diagnostic(bms_parse_outcome_t& result, diagnostic_severity_t severity,
                         bms_parse_diagnostic_code_t code, std::string message,
                         const source_line_t& line, std::size_t column = 1) -> void {
    result.diagnostics.push_back({
        .severity = severity,
        .code = code,
        .message = std::move(message),
        .location = {line.order + 1, column, line.span},
    });
}

auto split_lines(std::span<const std::byte> bytes, std::size_t start, text_encoding_t encoding,
                 const bms_parse_options_t& options, bms_parse_outcome_t& result)
    -> std::optional<std::vector<source_line_t>> {
    const auto* data = reinterpret_cast<const char*>(bytes.data());
    std::vector<source_line_t> lines;
    auto cursor = start;
    while (cursor < bytes.size()) {
        if (lines.size() >= options.max_lines) {
            result.diagnostics.push_back({
                .severity = diagnostic_severity_t::error,
                .code = bms_parse_diagnostic_code_t::malformed_line,
                .message = "BMS source exceeds the configured physical line limit",
                .location = {lines.size() + 1, 1, bms_source_span_t{cursor, bytes.size()}},
            });
            return std::nullopt;
        }
        const auto begin = cursor;
        while (cursor < bytes.size() && data[cursor] != '\n' && data[cursor] != '\r') {
            ++cursor;
        }
        const auto content_end = cursor;
        if (content_end - begin > options.max_line_bytes) {
            result.diagnostics.push_back({
                .severity = diagnostic_severity_t::error,
                .code = bms_parse_diagnostic_code_t::malformed_line,
                .message = "BMS source line exceeds the configured byte limit",
                .location = {lines.size() + 1, options.max_line_bytes + 1,
                             bms_source_span_t{begin, cursor}},
            });
            return std::nullopt;
        }
        auto ending = bms_line_ending_t::none;
        if (cursor < bytes.size()) {
            if (data[cursor] == '\r' && cursor + 1 < bytes.size() && data[cursor + 1] == '\n') {
                cursor += 2;
                ending = bms_line_ending_t::crlf;
            } else if (data[cursor] == '\r') {
                ++cursor;
                ending = bms_line_ending_t::cr;
            } else {
                ++cursor;
                ending = bms_line_ending_t::lf;
            }
        }

        const std::string_view raw_line(data + begin, content_end - begin);
        auto decoded = decode_bms_text(raw_line, encoding);
        if (has_unsupported_codec_error(decoded)) {
            add_line_diagnostic(result, diagnostic_severity_t::error,
                                bms_parse_diagnostic_code_t::unsupported_encoding,
                                "requested " + std::string(text_encoding_label(encoding)) +
                                    " conversion is unavailable in this build",
                                source_line_t{.span = {begin, cursor}, .order = lines.size()});
            return std::nullopt;
        }
        if (!decoded.has_value()) {
            const auto error = begin + decoded.error().error_offset;
            decode_diagnostic(result, bms_parse_diagnostic_code_t::invalid_encoding,
                              "input contains a malformed " + std::string(text_encoding_label(encoding)) +
                                  " byte sequence",
                              error, std::min(error + 1, cursor));
            result.diagnostics.back().location.line = lines.size() + 1;
            result.diagnostics.back().location.column = decoded.error().error_offset + 1;
            return std::nullopt;
        }
        lines.push_back({
            .text = std::move(*decoded),
            .span = {begin, cursor},
            .ending = ending,
            .order = lines.size(),
        });
    }
    return lines;
}

auto parse_line(std::string_view text) -> parsed_line_t {
    if (text.empty() || text.front() != '#') {
        return {};
    }
    text.remove_prefix(1);
    const auto separator = text.find_first_of(" \t:");
    if (separator == std::string_view::npos) {
        return {.token = std::string(text)};
    }
    auto raw_value = text.substr(separator + 1);
    const auto semantic = trim_ascii(raw_value);
    return {
        .token = std::string(text.substr(0, separator)),
        .raw_value = std::string(raw_value),
        .semantic_value = std::string(semantic),
        .has_separator = true,
        .separator = text[separator],
    };
}

template <typename id_t> auto new_id(id_allocator_t& allocator) -> id_t {
    return allocate_id<id_t>(allocator).value_or(id_t{});
}

auto make_node(const source_line_t& line, const parsed_line_t& parsed, bms_control_kind_t kind,
               id_allocator_t& allocator, std::optional<std::int64_t> value = std::nullopt)
    -> bms_control_node_t {
    return {
        .id = new_id<branch_id_t>(allocator),
        .kind = kind,
        .value = value,
        .original_text = line.text,
        .command_token = parsed.token,
        .raw_value = parsed.raw_value,
        .source_span = line.span,
        .source_order = line.order,
        .line_ending = line.ending,
    };
}

auto node_at(bms_control_node_t& root, const std::vector<std::size_t>& path)
    -> bms_control_node_t& {
    auto* node = &root;
    for (const auto index : path) {
        node = &node->children.at(index);
    }
    return *node;
}

auto append_node(bms_control_node_t& root, const std::vector<std::size_t>& parent_path,
                 bms_control_node_t node) -> std::vector<std::size_t> {
    auto& parent = node_at(root, parent_path);
    const auto index = parent.children.size();
    parent.children.push_back(std::move(node));
    auto path = parent_path;
    path.push_back(index);
    return path;
}

auto close_node(bms_control_node_t& root, const std::vector<std::size_t>& path,
                const source_line_t& line) -> void {
    auto& node = node_at(root, path);
    node.close_source_span = line.span;
    node.close_text = line.text;
    node.close_source_order = line.order;
    node.close_line_ending = line.ending;
}

auto parse_integer(std::string_view text) -> std::optional<std::int64_t> {
    std::int64_t value{};
    if (text.empty()) {
        return std::nullopt;
    }
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

auto is_base36_digit(char byte) -> bool {
    return bms36_value(static_cast<unsigned char>(byte)) >= 0;
}

auto is_bms_metadata_command(std::string_view token) -> bool {
    const auto* definition = bms_metadata_definition_t::from(token);
    return definition && definition->field != bms_metadata_field_t::octave_fp;
}

auto is_safe_bms_header_token(std::string_view token) -> bool {
    if (token.empty() || !((token.front() >= 'A' && token.front() <= 'Z') ||
                           (token.front() >= 'a' && token.front() <= 'z'))) {
        return false;
    }
    return std::ranges::all_of(token, [](char character) {
        return (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') ||
               (character >= '0' && character <= '9') || character == '_';
    });
}

auto parse_measure_command(std::string_view token)
    -> std::optional<std::pair<std::size_t, std::string>> {
    if (token.size() != 5 ||
        !std::ranges::all_of(token.substr(0, 3),
                             [](unsigned char c) { return c >= '0' && c <= '9'; }) ||
        !is_base36_digit(token[3]) || !is_base36_digit(token[4])) {
        return std::nullopt;
    }
    std::size_t measure{};
    const auto converted = std::from_chars(token.data(), token.data() + 3, measure);
    if (converted.ec != std::errc{}) {
        return std::nullopt;
    }
    return std::pair{measure, text_util::ascii_upper(token.substr(3, 2))};
}

struct root_channel_line_t {
    const bms_control_node_t* node{};
    std::size_t measure{};
    std::string channel;
    std::string payload;
    std::size_t pair_count{};
    bool materialize{};
};

auto source_provenance(std::string_view field, std::string_view spelling, std::size_t order,
                       std::string_view profile) -> source_provenance_t;

struct long_note_endpoint_t {
    const root_channel_line_t* line{};
    local_position_t position;
    std::size_t slot{};
    std::string object_id;
};

struct long_note_pairing_context_t {
    chart_document_t& chart;
    const std::set<std::uint64_t>& incomplete_lanes;
    const std::set<std::size_t>& inexact_measures;
    const std::optional<lane_input_profile_t>& selected_profile;
    id_allocator_t& ids;
    std::function<const lane_definition_t*(std::string_view)> lane_for_channel;
    std::function<std::optional<resource_id_t>(const bms_control_node_t&, std::string_view)>
        resource_for_object;
    std::function<void(const bms_control_node_t&, diagnostic_severity_t,
                       bms_parse_diagnostic_code_t, std::string)> diagnose;
};

void emit_long_note_pair(long_note_pairing_context_t& context,
                         const long_note_endpoint_t& start, const long_note_endpoint_t& end) {
    const auto crosses_inexact_measure = std::ranges::any_of(
        context.inexact_measures, [&](const auto measure) {
            return measure >= start.line->measure && measure <= end.line->measure;
        });
    if (crosses_inexact_measure) {
        context.diagnose(*start.line->node, diagnostic_severity_t::warning,
                         bms_parse_diagnostic_code_t::unpaired_long_note,
                         "long-note endpoints cross inexact fallback measure geometry and remain "
                         "raw");
        return;
    }
    if (std::tie(end.line->measure, end.position.row) <=
        std::tie(start.line->measure, start.position.row)) {
        context.diagnose(*end.line->node, diagnostic_severity_t::warning,
                         bms_parse_diagnostic_code_t::unpaired_long_note,
                         "long-note endpoints share a position and remain raw because a "
                         "positive-length hold cannot be represented");
        return;
    }
    const auto* lane = context.lane_for_channel(start.line->channel);
    if (lane == nullptr) {
        return;
    }
    const auto sound = context.resource_for_object(*start.line->node, start.object_id);
    const auto end_line = end.line->node->source_order + 1;
    const auto source_field = std::string("long-note start ") + start.line->channel + " slot " +
                              std::to_string(start.slot) + " object " + start.object_id +
                              " paired with " + end.line->channel + " slot " +
                              std::to_string(end.slot) + " object " + end.object_id +
                              " on source line " + std::to_string(end_line);
    const auto source_spelling =
        start.line->node->original_text + "\n" + end.line->node->original_text;
    context.chart.measures[start.line->measure].notes.push_back({
        .id = new_id<object_id_t>(context.ids),
        .position = start.position,
        .lane = lane->id,
        .kind = note_kind_t::hold,
        .end = end.position,
        .sound = sound,
        .provenance = source_provenance(source_field, source_spelling,
                                        start.line->node->source_order,
                                        context.selected_profile->layout.profile_id),
    });
}

void pair_long_note_lane(long_note_pairing_context_t& context,
                         std::uint64_t lane_value,
                         std::vector<long_note_endpoint_t>& endpoints) {
    if (context.incomplete_lanes.contains(lane_value)) {
        return;
    }
    std::ranges::stable_sort(endpoints, [](const auto& left, const auto& right) {
        return std::tie(left.line->measure, left.position.row, left.line->node->source_order,
                        left.slot) < std::tie(right.line->measure, right.position.row,
                                              right.line->node->source_order, right.slot);
    });
    for (std::size_t index = 0; index + 1 < endpoints.size(); index += 2) {
        emit_long_note_pair(context, endpoints[index], endpoints[index + 1]);
    }
    if (endpoints.size() % 2 != 0) {
        context.diagnose(*endpoints.back().line->node, diagnostic_severity_t::warning,
                         bms_parse_diagnostic_code_t::unpaired_long_note,
                         "long-note endpoint has no matching partner; its source payload remains "
                         "raw");
    }
}

void pair_long_note_endpoints(
    long_note_pairing_context_t& context,
    std::map<std::uint64_t, std::vector<long_note_endpoint_t>>& endpoints_by_lane) {
    for (auto& [lane_value, endpoints] : endpoints_by_lane) {
        pair_long_note_lane(context, lane_value, endpoints);
    }
}

auto is_playable_channel(std::string_view channel) -> bool {
    if (channel.size() != 2) {
        return false;
    }
    switch (channel.front()) {
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
        return true;
    case 'D':
    case 'E':
        return channel[1] >= '1' && channel[1] <= '9';
    default:
        return false;
    }
}

auto is_materialized_note_family(std::string_view channel) -> bool {
    return channel.size() == 2 &&
           (channel.front() == '1' || channel.front() == '2' || channel.front() == '3' ||
            channel.front() == '4' || channel.front() == '5' || channel.front() == '6' ||
            channel.front() == 'D' || channel.front() == 'E');
}

auto is_long_note_channel(std::string_view channel) -> bool {
    return channel.size() == 2 && (channel.front() == '5' || channel.front() == '6');
}

using checked_math::checked_lcm;

auto resolution_for_slots(std::uint64_t current_resolution, const measure_definition_t& measure,
                          std::size_t pair_count) -> std::optional<std::uint64_t> {
    if (pair_count == 0 || measure.length.numerator == 0 || measure.length.denominator == 0) {
        return std::nullopt;
    }
    const auto slots = static_cast<std::uint64_t>(pair_count);
    // R * (p / q) / slots must be integral. Since p/q is reduced, the
    // remaining denominator is q * (slots / gcd(p, slots)).
    const auto reduced_slots = slots / std::gcd(measure.length.numerator, slots);
    if (reduced_slots > std::numeric_limits<std::uint64_t>::max() / measure.length.denominator) {
        return std::nullopt;
    }
    const auto required_multiple = reduced_slots * measure.length.denominator;
    return checked_lcm(current_resolution, required_multiple);
}

// Parses the optional signed decimal exponent after 'e'/'E' (as in "1.00001E+07").
auto parse_decimal_exponent(std::string_view text) -> std::optional<int> {
    const bool negative = !text.empty() && text.front() == '-';
    if (!text.empty() && (text.front() == '-' || text.front() == '+')) text.remove_prefix(1);
    int magnitude{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), magnitude);
    if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
        magnitude > 38) {
        return std::nullopt;
    }
    return negative ? -magnitude : magnitude;
}

// Scales one side of an exact value by 10^count; nullopt on overflow.
auto scale_by_power_of_ten(std::uint64_t value, int count) -> std::optional<std::uint64_t> {
    for (int index = 0; index < count; ++index) {
        if (value > std::numeric_limits<std::uint64_t>::max() / 10) return std::nullopt;
        value *= 10;
    }
    return value;
}

// BMS BPM and STOP declarations are decimals or fractions, optionally with a
// decimal exponent; players read them as doubles, so charts use "1.00001E+07".
auto parse_positive_rational(std::string_view text) -> std::optional<signed_rational_t> {
    if (text.find('/') == std::string_view::npos) {
        const auto parsed = parse_exact_decimal(text, 38);
        return parsed && parsed->numerator > 0 ? parsed : std::nullopt;
    }
    std::optional<int> exponent{0};
    if (const auto marker = text.find_first_of("eE"); marker != std::string_view::npos) {
        exponent = parse_decimal_exponent(text.substr(marker + 1));
        text = text.substr(0, marker);
    }
    const auto parsed = parse_measure_length(text);
    if (!exponent || !parsed.has_value()) return std::nullopt;
    auto numerator = std::optional{parsed->numerator};
    auto denominator = std::optional{parsed->denominator};
    if (*exponent >= 0)
        numerator = scale_by_power_of_ten(*numerator, *exponent);
    else
        denominator = scale_by_power_of_ten(*denominator, -*exponent);
    if (!numerator || !denominator ||
        *numerator > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return std::nullopt;
    }
    const auto divisor = std::gcd(*numerator, *denominator);
    return signed_rational_t{static_cast<std::int64_t>(*numerator / divisor),
                             *denominator / divisor};
}

auto source_provenance(std::string_view field, std::string_view spelling, std::size_t order,
                       std::string_view profile = "bms-byte-parser") -> source_provenance_t {
    return {
        .family = format_family_t::bms,
        .profile = std::string(profile),
        .source_field = std::string(field),
        .source_spelling = std::string(spelling),
        .source_order = order,
    };
}

auto node_source_line(const bms_control_node_t& node) -> source_line_t {
    return {
        .text = node.original_text,
        .span = node.source_span,
        .ending = node.line_ending,
        .order = node.source_order,
    };
}

auto bms_bga_kind_for_channel(std::string_view channel) -> std::optional<visual_event_kind_t> {
    const auto* entry = bms_bga_channel_t::from(channel);
    return entry ? std::optional{entry->kind} : std::nullopt;
}

struct bms_channel_materializer_t {
    chart_document_t& chart;
    song_document_t& song;
    bms_parse_outcome_t& result;
    id_allocator_t& ids;
    std::map<std::string, resource_id_t, std::less<>>& resource_ids;
    const std::map<std::string, signed_rational_t, std::less<>>& extended_bpms;
    const std::map<std::string, signed_rational_t, std::less<>>& stop_definitions;
    const std::map<std::string, signed_rational_t, std::less<>>& scroll_definitions;
    const std::optional<lane_input_profile_t>& selected_profile;
    std::map<std::uint64_t, std::vector<long_note_endpoint_t>>& long_note_endpoints;
    // BGM lines already placed per measure; the k-th line of a measure uses
    // the chart-wide BGM column k, as iBMSC and BMSE present them.
    std::map<std::size_t, std::size_t> bgm_columns_used;

    static constexpr std::size_t maximum_bgm_lines_per_measure = 128;

    void diagnose(const bms_control_node_t& node, diagnostic_severity_t severity,
                  bms_parse_diagnostic_code_t code, std::string message) {
        add_line_diagnostic(result, severity, code, std::move(message), node_source_line(node));
    }

    // Column for the next BGM line of this measure, created on first use.
    auto next_bgm_column(const root_channel_line_t& channel_line, const bms_control_node_t& node)
        -> object_id_t {
        const auto column = bgm_columns_used[channel_line.measure]++;
        if (column == maximum_bgm_lines_per_measure) {
            diagnose(node, diagnostic_severity_t::warning, bms_parse_diagnostic_code_t::malformed_line,
                     "measure has more than 128 BGM lines; the extra lines are kept but exceed "
                     "the BMSE limit");
        }
        if (column >= chart.bgm_source_lanes.size()) {
            const auto lane_id = new_id<object_id_t>(ids);
            chart.bgm_source_lanes.push_back({
                .id = lane_id,
                .name = "BGM " + std::to_string(column + 1),
                .source_order = node.source_order,
            });
            return lane_id;
        }
        return chart.bgm_source_lanes[column].id;
    }

    auto lane_for_channel(std::string_view channel) const -> const lane_definition_t* {
        if (!selected_profile.has_value()) {
            return nullptr;
        }
        const auto found = std::ranges::find_if(
            selected_profile->layout.lanes, [channel](const lane_definition_t& lane) {
                return (lane.bms_channels.visible.has_value() &&
                        *lane.bms_channels.visible == channel) ||
                       (lane.bms_channels.invisible.has_value() &&
                        *lane.bms_channels.invisible == channel) ||
                       (lane.bms_channels.long_note.has_value() &&
                        *lane.bms_channels.long_note == channel) ||
                       (lane.bms_channels.mine.has_value() &&
                        *lane.bms_channels.mine == channel);
            });
        return found == selected_profile->layout.lanes.end() ? nullptr : &*found;
    }

    auto position_for_slot(const root_channel_line_t& channel_line, std::size_t slot) const
        -> std::optional<local_position_t> {
        const auto& measure = chart.measures[channel_line.measure];
        const auto capacity =
            validate_measure(chart.divisions_per_standard_measure, measure.definition);
        if (!capacity.row_capacity.has_value() || channel_line.pair_count == 0 ||
            *capacity.row_capacity % channel_line.pair_count != 0) {
            return std::nullopt;
        }
        const auto rows_per_slot = *capacity.row_capacity / channel_line.pair_count;
        return local_position_t{.measure = measure.id,
                                .row = static_cast<std::uint64_t>(slot) * rows_per_slot};
    }

    auto resource_for_object(const bms_control_node_t& node, std::string_view object_id)
        -> std::optional<resource_id_t> {
        const auto key = std::string("WAV") + std::string(object_id);
        const auto found = resource_ids.find(key);
        if (found != resource_ids.end()) {
            return found->second;
        }
        diagnose(node, diagnostic_severity_t::warning,
                 bms_parse_diagnostic_code_t::unresolved_reference,
                 "channel object references an undeclared " + key +
                     "; semantic event remains unbound");
        return std::nullopt;
    }

    auto bga_resource_for_object(const bms_control_node_t& node, std::string_view object_id)
        -> resource_id_t {
        const auto key = std::string("BMP") + std::string(object_id);
        if (const auto found = resource_ids.find(key); found != resource_ids.end()) {
            return found->second;
        }
        const auto asset_id = new_id<asset_id_t>(ids);
        const auto resource_id = new_id<resource_id_t>(ids);
        const auto normalized_id = text_util::ascii_upper(std::string(object_id));
        const auto missing_name = "Missing BMS BMP " + normalized_id;
        song.visual_assets.push_back({.id = asset_id, .kind = asset_kind_t::image, .source_path = {}});
        song.resources.push_back({
            .id = resource_id,
            .name = missing_name,
            .asset = asset_id,
            .roles = {resource_role_t::bga},
            .target_identities = {{format_family_t::bms, normalized_id, "BMP"}},
            .missing = true,
            .provenance = source_provenance(key + " (missing declaration)", node.original_text,
                                            node.source_order),
        });
        resource_ids.emplace(key, resource_id);
        diagnose(node, diagnostic_severity_t::warning,
                 bms_parse_diagnostic_code_t::unresolved_reference,
                 "BGA channel references undeclared " + key +
                     "; a missing visual resource placeholder was created");
        return resource_id;
    }

    void add_bgm_event(const root_channel_line_t& channel_line,
                       const bms_control_node_t& node, std::size_t slot,
                       const std::string& object_id, local_position_t position,
                       object_id_t source_lane) {
        // `00` is positional padding, not an event; its slot index is kept by
        // the neighbouring events' structural slots.
        if (object_id == "00") {
            return;
        }
        const auto sound = resource_for_object(node, object_id);
        chart.measures[channel_line.measure].bgm_events.push_back({
            .id = new_id<object_id_t>(ids),
            .position = position,
            .source_lane = source_lane,
            .sound = sound,
            .structural_slot = slot,
            .provenance = source_provenance("01 slot " + std::to_string(slot) + " object " +
                                                object_id,
                                            node.original_text, node.source_order),
        });
    }

    void add_bga_event(std::string_view channel, const bms_control_node_t& node,
                       std::size_t slot, const std::string& object_id,
                       local_position_t position) {
        if (object_id == "00") {
            return;
        }
        const auto kind = bms_bga_kind_for_channel(channel).value_or(visual_event_kind_t::background);
        chart.bga_events.push_back({
            .id = new_id<object_id_t>(ids),
            .position = position,
            .resource = bga_resource_for_object(node, object_id),
            .kind = kind,
            .provenance = source_provenance(std::string(channel) + " slot " +
                                                std::to_string(slot) + " object " + object_id,
                                            node.original_text, node.source_order),
        });
    }

    void add_direct_bpm(const bms_control_node_t& node, std::size_t slot,
                        const std::string& object_id, local_position_t position) {
        if (object_id == "00") {
            return;
        }
        unsigned int bpm_value{};
        const auto parsed = std::from_chars(object_id.data(), object_id.data() + object_id.size(),
                                             bpm_value, 16);
        if (parsed.ec != std::errc{} || parsed.ptr != object_id.data() + object_id.size() ||
            bpm_value == 0) {
            diagnose(node, diagnostic_severity_t::warning,
                     bms_parse_diagnostic_code_t::invalid_number,
                     "direct BPM object is not a positive hexadecimal byte; raw slot is retained");
            return;
        }
        chart.bpms.push_back({
            .id = new_id<object_id_t>(ids),
            .position = position,
            .beats_per_minute = {static_cast<std::int64_t>(bpm_value), 1},
            .provenance = source_provenance("03 slot " + std::to_string(slot) + " value " +
                                                object_id,
                                            node.original_text, node.source_order),
        });
    }

    void add_extended_bpm(const bms_control_node_t& node, std::size_t slot,
                          const std::string& object_id, local_position_t position) {
        if (object_id == "00") {
            return;
        }
        const auto found = extended_bpms.find(object_id);
        if (found == extended_bpms.end()) {
            diagnose(node, diagnostic_severity_t::warning,
                     bms_parse_diagnostic_code_t::unresolved_reference,
                     "extended BPM object references undeclared BPM" + object_id +
                         "; source slot remains raw");
            return;
        }
        chart.bpms.push_back({
            .id = new_id<object_id_t>(ids),
            .position = position,
            .beats_per_minute = found->second,
            .provenance = source_provenance("08 slot " + std::to_string(slot) + " BPM" +
                                                object_id,
                                            node.original_text, node.source_order),
        });
    }

    void add_stop_event(const bms_control_node_t& node, std::size_t slot,
                        const std::string& object_id, local_position_t position) {
        if (object_id == "00") {
            return;
        }
        const auto found = stop_definitions.find(object_id);
        if (found == stop_definitions.end()) {
            diagnose(node, diagnostic_severity_t::warning,
                     bms_parse_diagnostic_code_t::unresolved_reference,
                     "STOP object references undeclared STOP" + object_id +
                         "; source slot remains raw");
            return;
        }
        chart.pauses.push_back({
            .id = new_id<object_id_t>(ids),
            .position = position,
            .kind = pause_kind_t::stop,
            .duration = found->second,
            .source_unit = pause_unit_t::bms_stop_ticks,
            .provenance = source_provenance("09 slot " + std::to_string(slot) + " STOP" +
                                                object_id,
                                            node.original_text, node.source_order),
        });
    }

    void add_scroll_event(const bms_control_node_t& node, std::size_t slot,
        const std::string& object_id, local_position_t position) {
        if (object_id == "00") return;
        const auto found = scroll_definitions.find(object_id);
        if (found == scroll_definitions.end()) {
            diagnose(node, diagnostic_severity_t::warning, bms_parse_diagnostic_code_t::unresolved_reference,
                     "SCROLL object references an undeclared value; source slot remains raw");
            return;
        }
        chart.scrolls.push_back({.id = new_id<object_id_t>(ids), .position = position,
            .multiplier = found->second, .provenance = source_provenance(
                "SC slot " + std::to_string(slot) + " object " + object_id,
                node.original_text, node.source_order)});
    }

    void add_note_slot(const root_channel_line_t& channel_line, const bms_control_node_t& node,
                       std::string_view channel, std::size_t slot,
                       const std::string& object_id, local_position_t position) {
        if (!is_materialized_note_family(channel)) {
            return;
        }
        const auto* lane = lane_for_channel(channel);
        if (lane == nullptr) {
            diagnose(node, diagnostic_severity_t::warning,
                     bms_parse_diagnostic_code_t::unsupported_channel,
                     "selected profile has no semantic lane for this channel; source payload "
                     "remains raw");
            return;
        }
        if (object_id == "00") {
            return;
        }
        if (is_long_note_channel(channel)) {
            long_note_endpoints[lane->id.value].push_back(
                {.line = &channel_line, .position = position, .slot = slot, .object_id = object_id});
            return;
        }
        auto kind = note_kind_t::tap;
        const auto visible = lane->bms_channels.visible.has_value() &&
                             *lane->bms_channels.visible == channel;
        const auto invisible = lane->bms_channels.invisible.has_value() &&
                               *lane->bms_channels.invisible == channel;
        const auto mine = lane->bms_channels.mine.has_value() &&
                          *lane->bms_channels.mine == channel;
        if (invisible) {
            kind = note_kind_t::invisible;
        } else if (mine) {
            kind = note_kind_t::mine;
        } else if (!visible) {
            diagnose(node, diagnostic_severity_t::warning,
                     bms_parse_diagnostic_code_t::unsupported_channel,
                     "selected profile does not identify the note-family meaning of " +
                         std::string(channel) + "; source payload remains raw");
            return;
        }
        std::optional<resource_id_t> sound;
        if (kind != note_kind_t::mine) {
            sound = resource_for_object(node, object_id);
        }
        chart.measures[channel_line.measure].notes.push_back({
            .id = new_id<object_id_t>(ids),
            .position = position,
            .lane = lane->id,
            .kind = kind,
            .sound = sound,
            .provenance = source_provenance(
                std::string(channel) + " slot " + std::to_string(slot) + " object " + object_id,
                node.original_text, node.source_order, selected_profile->layout.profile_id),
        });
    }

    void materialize(const root_channel_line_t& channel_line) {
        const auto channel = std::string_view(channel_line.channel);
        const auto& node = *channel_line.node;
        auto bgm_source_lane = std::optional<object_id_t>{};
        if (channel == "01") {
            bgm_source_lane = next_bgm_column(channel_line, node);
        }
        for (std::size_t slot = 0; slot < channel_line.pair_count; ++slot) {
            const auto object_id =
                text_util::ascii_upper(std::string_view(channel_line.payload).substr(slot * 2, 2));
            const auto position = position_for_slot(channel_line, slot);
            if (!position.has_value()) {
                diagnose(node, diagnostic_severity_t::warning,
                         bms_parse_diagnostic_code_t::invalid_measure,
                         "channel slot cannot be represented exactly at the selected chart "
                         "resolution; payload remains raw");
                break;
            }
            if (channel == "01") {
                add_bgm_event(channel_line, node, slot, object_id, *position, *bgm_source_lane);
            } else if (channel == "04" || channel == "06" || channel == "07" ||
                       channel == "0A") {
                add_bga_event(channel, node, slot, object_id, *position);
            } else if (channel == "03") {
                add_direct_bpm(node, slot, object_id, *position);
            } else if (channel == "08") {
                add_extended_bpm(node, slot, object_id, *position);
            } else if (channel == "09") {
                add_stop_event(node, slot, object_id, *position);
            } else if (channel == "SC") {
                add_scroll_event(node, slot, object_id, *position);
            } else {
                add_note_slot(channel_line, node, channel, slot, object_id, *position);
            }
        }
    }

    auto pairing_context(const std::set<std::uint64_t>& incomplete_lanes,
                        const std::set<std::size_t>& inexact_measures)
        -> long_note_pairing_context_t {
        return long_note_pairing_context_t{
            chart,
            incomplete_lanes,
            inexact_measures,
            selected_profile,
            ids,
            [this](std::string_view channel) { return lane_for_channel(channel); },
            [this](const bms_control_node_t& node, std::string_view object_id) {
                return resource_for_object(node, object_id);
            },
            [this](const bms_control_node_t& node, diagnostic_severity_t severity,
                   bms_parse_diagnostic_code_t code, std::string message) {
                diagnose(node, severity, code, std::move(message));
            },
        };
    }
};


auto push_raw_statement(bms_control_node_t& root, const std::vector<std::size_t>& parent_path,
                        const source_line_t& line, const parsed_line_t& parsed,
                        id_allocator_t& allocator) -> void {
    (void)append_node(root, parent_path,
                      make_node(line, parsed, bms_control_kind_t::raw_statement, allocator));
}

auto is_control_token(std::string_view token) -> bool {
    constexpr std::array<std::string_view, 13> tokens{
        "RANDOM",    "SETRANDOM", "IF",  "ELSEIF", "ELSE",  "ENDIF",     "SWITCH",
        "SETSWITCH", "CASE",      "DEF", "SKIP",   "ENDSW", "ENDRANDOM",
    };
    return std::ranges::any_of(tokens, [&](std::string_view candidate) {
        return equal_ascii_case_insensitive(token, candidate);
    });
}

struct active_branch_walk_context_t {
    const std::map<std::uint64_t, std::int64_t>& selected_values;
    std::set<std::size_t>& active_orders;
    std::set<std::uint64_t>& visited_selections;
};

void visit_active_branch(const bms_control_node_t& node,
                        std::optional<std::int64_t> random_value, bool inside_active_branch,
                        bool* switch_stopped, active_branch_walk_context_t& context);

auto selected_if_branch(const bms_control_node_t& group, std::int64_t random_value)
    -> const bms_control_node_t* {
    const bms_control_node_t* fallback{};
    for (const auto& child : group.children) {
        if ((child.kind == bms_control_kind_t::if_branch ||
             child.kind == bms_control_kind_t::else_if_branch) &&
            child.value == random_value) {
            return &child;
        }
        if (child.kind == bms_control_kind_t::else_branch) {
            fallback = &child;
        }
    }
    return fallback;
}

void visit_random_branch(const bms_control_node_t& node,
                         std::optional<std::int64_t> random_value, bool* switch_stopped,
                         active_branch_walk_context_t& context) {
    const auto selected = context.selected_values.find(node.id.value);
    if (selected == context.selected_values.end() || !node.value.has_value() ||
        selected->second < 1 || selected->second > *node.value) {
        return;
    }
    context.visited_selections.insert(node.id.value);
    for (const auto& child : node.children) {
        visit_active_branch(child, selected->second, true, switch_stopped, context);
    }
    (void)random_value;
}

void visit_if_group(const bms_control_node_t& node,
                    std::optional<std::int64_t> random_value, bool* switch_stopped,
                    active_branch_walk_context_t& context) {
    if (!random_value.has_value()) {
        return;
    }
    if (const auto* selected = selected_if_branch(node, *random_value); selected != nullptr) {
        visit_active_branch(*selected, random_value, true, switch_stopped, context);
    }
}

auto selected_switch_branch(const bms_control_node_t& node, std::int64_t selected_value)
    -> const bms_control_node_t* {
    const bms_control_node_t* fallback{};
    for (const auto& child : node.children) {
        if (child.kind == bms_control_kind_t::case_branch && child.value == selected_value) {
            return &child;
        }
        if (child.kind == bms_control_kind_t::default_branch) {
            fallback = &child;
        }
    }
    return fallback;
}

void visit_switch_branch(const bms_control_node_t& node,
                         std::optional<std::int64_t> random_value, bool* switch_stopped,
                         active_branch_walk_context_t& context) {
    const auto choice = context.selected_values.find(node.id.value);
    if (choice == context.selected_values.end()) {
        return;
    }
    context.visited_selections.insert(node.id.value);
    const auto* selected = selected_switch_branch(node, choice->second);
    if (selected == nullptr) {
        return;
    }
    auto stopped = false;
    auto entered = false;
    for (const auto& child : node.children) {
        if (child.kind == bms_control_kind_t::case_branch ||
            child.kind == bms_control_kind_t::default_branch) {
            entered = entered || &child == selected;
        }
        if (entered && !stopped) {
            visit_active_branch(child, random_value, true, &stopped, context);
        }
    }
    (void)switch_stopped;
}

void visit_active_branch(const bms_control_node_t& node,
                        std::optional<std::int64_t> random_value, bool inside_active_branch,
                        bool* switch_stopped, active_branch_walk_context_t& context) {
    switch (node.kind) {
    case bms_control_kind_t::raw_statement:
        if (inside_active_branch) {
            context.active_orders.insert(node.source_order);
        }
        return;
    case bms_control_kind_t::skip:
        if (switch_stopped != nullptr) {
            *switch_stopped = true;
        }
        return;
    case bms_control_kind_t::random:
        visit_random_branch(node, random_value, switch_stopped, context);
        return;
    case bms_control_kind_t::if_group:
        visit_if_group(node, random_value, switch_stopped, context);
        return;
    case bms_control_kind_t::switch_block:
        visit_switch_branch(node, random_value, switch_stopped, context);
        return;
    default:
        const auto branch_scope = inside_active_branch || is_branch_body(node.kind);
        for (const auto& child : node.children) {
            visit_active_branch(child, random_value, branch_scope, switch_stopped, context);
        }
        return;
    }
}

struct branch_projection_context_t {
    const std::map<std::size_t, std::int64_t>& branch_choices;
    std::map<std::size_t, bool>& visited_choices;
    std::vector<const bms_control_node_t*>& projected_statements;
    std::vector<branch_selection_t>& active_selections;
    bms_parse_outcome_t& result;
};

void project_control_node(const bms_control_node_t& node,
                          std::optional<std::int64_t> random_value, bool inside_control,
                          std::optional<std::int64_t>& pending_random,
                          std::optional<std::int64_t>& pending_switch, bool* switch_stopped,
                          branch_projection_context_t& context);

void add_projection_warning(branch_projection_context_t& context,
                            const bms_control_node_t& node, std::string message) {
    add_line_diagnostic(context.result, diagnostic_severity_t::warning,
                        bms_parse_diagnostic_code_t::invalid_branch_selection,
                        std::move(message), node_source_line(node));
}

auto projection_if_branch(const bms_control_node_t& group, std::int64_t random_value)
    -> const bms_control_node_t* {
    const bms_control_node_t* fallback{};
    for (const auto& child : group.children) {
        if ((child.kind == bms_control_kind_t::if_branch ||
             child.kind == bms_control_kind_t::else_if_branch) &&
            child.value == random_value) {
            return &child;
        }
        if (child.kind == bms_control_kind_t::else_branch) {
            fallback = &child;
        }
    }
    return fallback;
}

auto projection_switch_branch(const bms_control_node_t& block, std::int64_t value)
    -> const bms_control_node_t* {
    const bms_control_node_t* fallback{};
    for (const auto& child : block.children) {
        if (child.kind == bms_control_kind_t::case_branch && child.value == value) {
            return &child;
        }
        if (child.kind == bms_control_kind_t::default_branch) {
            fallback = &child;
        }
    }
    return fallback;
}

void project_random_node(const bms_control_node_t& node,
                         std::optional<std::int64_t> random_value,
                         std::optional<std::int64_t>& pending_random,
                         std::optional<std::int64_t>& pending_switch, bool* switch_stopped,
                         branch_projection_context_t& context) {
    auto selected = std::optional<std::int64_t>{};
    if (const auto choice = context.branch_choices.find(node.source_order);
        choice != context.branch_choices.end()) {
        selected = choice->second;
        context.visited_choices[node.source_order] = true;
    } else if (pending_random) {
        selected = pending_random;
    }
    pending_random.reset();
    if (selected && (!node.value || *selected < 1 || *selected > *node.value)) {
        add_projection_warning(
            context, node,
            "fixed RANDOM value is outside the block's declared range; its branches remain unprojected");
        selected.reset();
    }
    if (!selected) {
        return;
    }
    context.active_selections.push_back({node.id, *selected});
    for (const auto& child : node.children) {
        project_control_node(child, selected, true, pending_random, pending_switch, switch_stopped,
                             context);
    }
    (void)random_value;
}

void project_switch_node(const bms_control_node_t& node,
                         std::optional<std::int64_t> random_value,
                         std::optional<std::int64_t>& pending_random,
                         std::optional<std::int64_t>& pending_switch, bool* switch_stopped,
                         branch_projection_context_t& context) {
    auto selected_value = std::optional<std::int64_t>{};
    if (const auto choice = context.branch_choices.find(node.source_order);
        choice != context.branch_choices.end()) {
        selected_value = choice->second;
        context.visited_choices[node.source_order] = true;
    } else if (pending_switch) {
        selected_value = pending_switch;
    }
    pending_switch.reset();
    auto* selected = selected_value ? projection_switch_branch(node, *selected_value) : nullptr;
    if (selected_value && selected == nullptr) {
        add_projection_warning(
            context, node,
            "fixed SWITCH value matches no CASE and has no DEFAULT; its branches remain unprojected");
        selected_value.reset();
    } else if (selected_value) {
        context.active_selections.push_back({node.id, *selected_value});
    }
    auto stopped = false;
    auto entered = false;
    for (const auto& child : node.children) {
        if (child.kind == bms_control_kind_t::case_branch ||
            child.kind == bms_control_kind_t::default_branch) {
            entered = entered || &child == selected;
        }
        if (entered && !stopped) {
            project_control_node(child, random_value, true, pending_random, pending_switch, &stopped,
                                 context);
        }
    }
    (void)switch_stopped;
}

void project_control_node(const bms_control_node_t& node,
                          std::optional<std::int64_t> random_value, bool inside_control,
                          std::optional<std::int64_t>& pending_random,
                          std::optional<std::int64_t>& pending_switch, bool* switch_stopped,
                          branch_projection_context_t& context) {
    switch (node.kind) {
    case bms_control_kind_t::raw_statement:
        if (inside_control) {
            context.projected_statements.push_back(&node);
        }
        return;
    case bms_control_kind_t::set_random:
        pending_random = node.value;
        return;
    case bms_control_kind_t::set_switch:
        pending_switch = node.value;
        return;
    case bms_control_kind_t::skip:
        if (switch_stopped != nullptr) {
            *switch_stopped = true;
        }
        return;
    case bms_control_kind_t::random:
        project_random_node(node, random_value, pending_random, pending_switch, switch_stopped,
                            context);
        return;
    case bms_control_kind_t::if_group:
        if (random_value) {
            if (const auto* selected = projection_if_branch(node, *random_value);
                selected != nullptr) {
                project_control_node(*selected, random_value, true, pending_random, pending_switch,
                                     switch_stopped, context);
            }
        }
        return;
    case bms_control_kind_t::switch_block:
        project_switch_node(node, random_value, pending_random, pending_switch, switch_stopped,
                            context);
        return;
    default:
        for (const auto& child : node.children) {
            project_control_node(child, random_value,
                                 inside_control || node.kind != bms_control_kind_t::root,
                                 pending_random, pending_switch, switch_stopped, context);
        }
        return;
    }
}

using choice_node_map_t = std::map<std::size_t, const bms_control_node_t*>;
using branch_choice_map_t = std::map<std::size_t, std::int64_t>;

auto index_choice_nodes(const bms_control_node_t& root) -> choice_node_map_t {
    choice_node_map_t choices;
    const auto visit = [&](const auto& self, const bms_control_node_t& node) -> void {
        if (node.kind == bms_control_kind_t::random || node.kind == bms_control_kind_t::switch_block)
            choices.emplace(node.source_order, &node);
        for (const auto& child : node.children) self(self, child);
    };
    visit(visit, root);
    return choices;
}

void add_choice_diagnostic(bms_parse_outcome_t& result, const bms_control_node_t& node,
                           std::string message) {
    add_line_diagnostic(result, diagnostic_severity_t::warning,
                        bms_parse_diagnostic_code_t::invalid_branch_selection,
                        std::move(message), node_source_line(node));
}

auto is_branch_choice_valid(const bms_control_node_t& node, std::int64_t value) -> bool {
    if (node.kind == bms_control_kind_t::random)
        return node.value && value >= 1 && value <= *node.value;
    const auto has_case = std::ranges::any_of(node.children, [&](const auto& child) {
        return child.kind == bms_control_kind_t::case_branch && child.value == value;
    });
    const auto has_default = std::ranges::any_of(node.children, [](const auto& child) {
        return child.kind == bms_control_kind_t::default_branch;
    });
    return has_case || has_default;
}

auto collect_branch_choices(const bms_parse_options_t& options,
                            const choice_node_map_t& choice_nodes,
                            bms_parse_outcome_t& result) -> branch_choice_map_t {
    branch_choice_map_t choices;
    const auto count = std::min(options.branch_choices.size(), options.max_lines);
    if (options.branch_choices.size() > options.max_lines)
        result.diagnostics.push_back({.severity = diagnostic_severity_t::warning,
            .code = bms_parse_diagnostic_code_t::invalid_branch_selection,
            .message = "branch choice count exceeds the configured physical line limit; excess choices are ignored",
            .location = {.line = 1, .column = 1}});
    for (std::size_t index = 0; index < count; ++index) {
        const auto& choice = options.branch_choices[index];
        if (!choices.emplace(choice.control_source_order, choice.selected_value).second) {
            const auto node = choice_nodes.find(choice.control_source_order);
            if (node != choice_nodes.end())
                add_choice_diagnostic(result, *node->second,
                    "duplicate choice for a RANDOM/SWITCH block; the first choice is used");
            else
                result.diagnostics.push_back({.severity = diagnostic_severity_t::warning,
                    .code = bms_parse_diagnostic_code_t::invalid_branch_selection,
                    .message = "duplicate choice for an unknown control source order; the first choice is used",
                    .location = {.line = choice.control_source_order + 1, .column = 1}});
        }
    }
    return choices;
}

void validate_branch_choices(branch_choice_map_t& choices,
                             const choice_node_map_t& choice_nodes,
                             bms_parse_outcome_t& result) {
    for (auto choice = choices.begin(); choice != choices.end();) {
        const auto found = choice_nodes.find(choice->first);
        if (found == choice_nodes.end()) {
            result.diagnostics.push_back({.severity = diagnostic_severity_t::warning,
                .code = bms_parse_diagnostic_code_t::invalid_branch_selection,
                .message = "branch choice does not identify a RANDOM or SWITCH opening line",
                .location = {.line = choice->first + 1, .column = 1}});
            choice = choices.erase(choice);
            continue;
        }
        const auto& node = *found->second;
        if (!is_branch_choice_valid(node, choice->second)) {
            add_choice_diagnostic(result, node, node.kind == bms_control_kind_t::random
                ? "RANDOM choice is outside the block's declared range; its branches remain unprojected"
                : "SWITCH choice matches no CASE and has no DEFAULT; its branches remain unprojected");
            choice = choices.erase(choice);
            continue;
        }
        ++choice;
    }
}

auto collect_active_statements(const bms_control_node_t& root,
                               const bms_parse_options_t& options,
                               chart_document_t& chart, bms_parse_outcome_t& result)
    -> std::vector<const bms_control_node_t*> {
    std::vector<const bms_control_node_t*> statements;
    for (const auto& node : root.children)
        if (node.kind == bms_control_kind_t::raw_statement) statements.push_back(&node);
    const auto choice_nodes = index_choice_nodes(root);
    auto choices = collect_branch_choices(options, choice_nodes, result);
    validate_branch_choices(choices, choice_nodes, result);
    std::vector<const bms_control_node_t*> projected;
    std::map<std::size_t, bool> visited;
    auto projection = branch_projection_context_t{choices, visited, projected,
                                                  chart.active_branch_selection, result};
    auto pending_random = std::optional<std::int64_t>{};
    auto pending_switch = std::optional<std::int64_t>{};
    project_control_node(root, std::nullopt, false, pending_random, pending_switch, nullptr,
                         projection);
    for (const auto& [order, value] : choices) {
        (void)value;
        if (!visited.contains(order)) {
            if (const auto node = choice_nodes.find(order); node != choice_nodes.end())
                add_choice_diagnostic(result, *node->second,
                    "choice targets a control block inside an unselected branch and is ignored");
        }
    }
    std::ranges::sort(projected, {}, [](const auto* node) { return node->source_order; });
    statements.insert(statements.end(), projected.begin(), projected.end());
    std::ranges::stable_sort(statements, {}, [](const auto* node) { return node->source_order; });
    return statements;
}

auto prepare_bms_source_lines(std::span<const std::byte> bytes, const bms_parse_options_t& options,
                              bms_parse_outcome_t& result)
    -> std::optional<std::vector<source_line_t>> {
    result.value.encoding.user_override = options.encoding_override;
    if (bytes.size() > options.max_source_bytes) {
        result.diagnostics.push_back({
            .severity = diagnostic_severity_t::error,
            .code = bms_parse_diagnostic_code_t::malformed_line,
            .message = "BMS source exceeds the configured byte limit",
            .location = {1, 1, bms_source_span_t{0, bytes.size()}},
        });
        return std::nullopt;
    }
    if (options.max_chart_resolution < default_divisions_per_standard_measure) {
        result.diagnostics.push_back({
            .severity = diagnostic_severity_t::error,
            .code = bms_parse_diagnostic_code_t::invalid_measure,
            .message = "configured chart-resolution limit must be at least the default 960 rows",
            .location = {.line = 1, .column = 1},
        });
        return std::nullopt;
    }
    if (!result.value.encoding.original_bytes)
        result.value.encoding.original_bytes =
            std::make_shared<const std::vector<std::byte>>(bytes.begin(), bytes.end());
    result.value.encoding.unchanged_since_import = true;

    const auto* data = reinterpret_cast<const char*>(bytes.data());
    const auto has_bom = bytes.size() >= 3 && static_cast<unsigned char>(data[0]) == 0xefU &&
                         static_cast<unsigned char>(data[1]) == 0xbbU &&
                         static_cast<unsigned char>(data[2]) == 0xbfU;
    const auto content_start = has_bom ? 3 : 0;
    auto encoding = text_encoding_t::unknown;
    if (has_bom) {
        if (options.encoding_override && *options.encoding_override != text_encoding_t::utf8) {
            decode_diagnostic(result, bms_parse_diagnostic_code_t::invalid_encoding,
                              "UTF-8 BOM conflicts with the requested " +
                                  std::string(text_encoding_label(*options.encoding_override)) +
                                  " override",
                              0, 3);
            return std::nullopt;
        }
        encoding = text_encoding_t::utf8;
    } else if (options.encoding_override) {
        encoding = *options.encoding_override;
        if (encoding == text_encoding_t::unknown) {
            decode_diagnostic(result, bms_parse_diagnostic_code_t::unsupported_encoding,
                              "unknown cannot be used as an encoding override", 0, bytes.size());
            return std::nullopt;
        }
    } else {
        const auto all_ascii = std::ranges::all_of(
            bytes, [](std::byte byte) { return std::to_integer<unsigned char>(byte) <= 0x7fU; });
        if (!all_ascii) {
            result.diagnostics.push_back({
                .severity = diagnostic_severity_t::error,
                .code = bms_parse_diagnostic_code_t::invalid_encoding,
                .message = "non-ASCII BMS input without a UTF-8 BOM is ambiguous; provide an "
                           "explicit encoding override",
                .location = {1, 1, bms_source_span_t{0, bytes.size()}},
            });
            return std::nullopt;
        }
        encoding = text_encoding_t::ascii;
    }
    result.value.encoding.detected = encoding;
    return split_lines(bytes, content_start, encoding, options, result);
}

} // namespace

auto decode_bms_text(std::string_view input, text_encoding_t encoding) -> text_codec_result_t {
    if (encoding == text_encoding_t::ascii) {
        for (std::size_t index = 0; index < input.size(); ++index) {
            if (static_cast<unsigned char>(input[index]) > 0x7fU) {
                return std::unexpected(text_codec_error_t{.error_offset = index});
            }
        }
        return std::string(input);
    }
    if (encoding == text_encoding_t::utf8) {
        if (const auto error = invalid_utf8_offset(input); error.has_value()) {
            return std::unexpected(text_codec_error_t{.error_offset = *error});
        }
        return std::string(input);
    }

    const auto source_name = text_encoding_iconv_name(encoding);
    if (!source_name.has_value()) {
        return std::unexpected(text_codec_error_t{.unsupported = true});
    }
#if defined(WORMEDIT_HAS_ICONV)
    const auto converter = iconv_open("UTF-8", std::string(*source_name).c_str());
    if (converter == reinterpret_cast<iconv_t>(-1)) {
        return std::unexpected(text_codec_error_t{.unsupported = true});
    }

    std::string output(std::max<std::size_t>(input.size() * 4U + 16U, 32U), '\0');
    auto* source = const_cast<char*>(input.data());
    auto source_remaining = input.size();
    std::size_t written{};
    while (true) {
        auto* destination = output.data() + written;
        auto destination_remaining = output.size() - written;
        errno = 0;
        const auto status =
            iconv(converter, &source, &source_remaining, &destination, &destination_remaining);
        written = output.size() - destination_remaining;
        if (status != static_cast<std::size_t>(-1)) {
            break;
        }
        if (errno != E2BIG) {
            const auto error_offset = input.size() - source_remaining;
            iconv_close(converter);
            return std::unexpected(text_codec_error_t{.error_offset = error_offset});
        }
        if (output.size() > std::numeric_limits<std::size_t>::max() / 2U) {
            iconv_close(converter);
            return std::unexpected(text_codec_error_t{.error_offset = input.size()});
        }
        output.resize(output.size() * 2U);
    }

    while (true) {
        auto* destination = output.data() + written;
        auto destination_remaining = output.size() - written;
        errno = 0;
        const auto status =
            iconv(converter, nullptr, nullptr, &destination, &destination_remaining);
        written = output.size() - destination_remaining;
        if (status != static_cast<std::size_t>(-1)) {
            break;
        }
        if (errno != E2BIG || output.size() > std::numeric_limits<std::size_t>::max() / 2U) {
            iconv_close(converter);
            return std::unexpected(text_codec_error_t{.error_offset = input.size()});
        }
        output.resize(output.size() * 2U);
    }
    iconv_close(converter);
    output.resize(written);
    return output;
#elif defined(_WIN32)
    const auto code_page = win32_codepage::code_page_for(encoding);
    if (!code_page.has_value()) {
        return std::unexpected(text_codec_error_t{.unsupported = true});
    }
    auto converted = win32_codepage::to_utf8(*code_page, input);
    return converted;
#else
    return std::unexpected(text_codec_error_t{.unsupported = true});
#endif
}

auto is_bms_text_encoding_supported(text_encoding_t encoding) -> bool {
    if (encoding == text_encoding_t::ascii || encoding == text_encoding_t::utf8) {
        return true;
    }
    const auto source_name = text_encoding_iconv_name(encoding);
    if (!source_name.has_value()) {
        return false;
    }
#if defined(WORMEDIT_HAS_ICONV)
    const auto converter = iconv_open("UTF-8", std::string(*source_name).c_str());
    if (converter == reinterpret_cast<iconv_t>(-1)) {
        return false;
    }
    iconv_close(converter);
    return true;
#elif defined(_WIN32)
    return win32_codepage::code_page_for(encoding).has_value();
#else
    return false;
#endif
}

auto is_resource_header(std::string_view token, std::string_view prefix) -> bool {
    return token.size() == 5 && token.starts_with(prefix) && is_base36_digit(token[3]) &&
           is_base36_digit(token[4]);
}

auto retain_non_control_header(const source_line_t& line, const parsed_line_t& parsed,
                               std::string_view token, bms_control_node_t& root,
                               std::vector<std::size_t>& current_path, id_allocator_t& ids) -> bool {
    if (line.text.empty() || line.text.front() != '#' || token == "TITLE" || token == "ARTIST" ||
        is_resource_header(token, "WAV") || is_resource_header(token, "BMP")) {
        push_raw_statement(root, current_path, line, parsed, ids);
        return true;
    }
    return false;
}

template <typename EnsureMeasure>
auto retain_measure_command(const source_line_t& line, const parsed_line_t& parsed,
                            std::string_view token, bms_control_node_t& root,
                            std::vector<std::size_t>& current_path, id_allocator_t& ids,
                            bms_parse_outcome_t& result, EnsureMeasure&& ensure_measure) -> bool {
    const auto command = parse_measure_command(token);
    if (!command.has_value()) {
        return false;
    }
    if (parsed.separator != ':') {
        push_raw_statement(root, current_path, line, parsed, ids);
        add_line_diagnostic(result, diagnostic_severity_t::warning,
                            bms_parse_diagnostic_code_t::malformed_line,
                            "BMS measure/channel command is missing its colon delimiter", line);
        return true;
    }
    const auto& [measure_number, channel] = *command;
    push_raw_statement(root, current_path, line, parsed, ids);
    (void)ensure_measure(measure_number);
    if (channel == "02") {
        return true;
    }
    const auto payload = trim_ascii(parsed.raw_value);
    const auto valid_payload = !payload.empty() && payload.size() % 2 == 0 &&
                               std::ranges::all_of(payload, [](char character) {
                                   return is_base36_digit(character);
                               });
    if (!valid_payload) {
        add_line_diagnostic(result, diagnostic_severity_t::error,
                            bms_parse_diagnostic_code_t::malformed_line,
                            payload.size() % 2 != 0
                                ? "BMS object payload has an odd number of base-36 digits"
                                : "BMS object payload contains a non-base-36 digit or is empty",
                            line);
    }
    return true;
}

struct control_tree_build_context_t {
    bms_control_node_t& root;
    std::vector<std::size_t>& current_path;
    std::vector<open_frame_t>& frames;
    const bms_parse_options_t& options;
    bms_parse_outcome_t& result;
    id_allocator_t& ids;
};

void retain_malformed_control(control_tree_build_context_t& context,
                               const source_line_t& line, const parsed_line_t& parsed,
                               std::string message) {
    push_raw_statement(context.root, context.current_path, line, parsed, context.ids);
    add_line_diagnostic(context.result, diagnostic_severity_t::warning,
                        bms_parse_diagnostic_code_t::malformed_control, std::move(message), line);
}

auto is_control_nesting_limit_reached(control_tree_build_context_t& context,
                                   const source_line_t& line, const parsed_line_t& parsed) -> bool {
    if (context.frames.size() < context.options.max_nesting_depth) {
        return false;
    }
    retain_malformed_control(
        context, line, parsed,
        "control nesting exceeds the configured depth limit; directive is retained raw");
    return true;
}

auto parse_control_value(control_tree_build_context_t& context, const source_line_t& line,
                         const parsed_line_t& parsed) -> std::optional<std::int64_t> {
    const auto value = parse_integer(parsed.semantic_value);
    if (!value.has_value()) {
        add_line_diagnostic(context.result, diagnostic_severity_t::warning,
                            bms_parse_diagnostic_code_t::invalid_number,
                            "control directive has a missing or invalid integer value; raw "
                            "spelling is retained",
                            line);
    }
    return value;
}

void add_simple_control(control_tree_build_context_t& context, const source_line_t& line,
                        const parsed_line_t& parsed, bms_control_kind_t kind,
                        std::optional<std::int64_t> value) {
    (void)append_node(context.root, context.current_path,
                      make_node(line, parsed, kind, context.ids, value));
}

auto open_control_block(control_tree_build_context_t& context, const source_line_t& line,
                        const parsed_line_t& parsed, std::string_view token) -> bool {
    if (token == "RANDOM" || token == "SETRANDOM" || token == "SETSWITCH") {
        const auto value = parse_control_value(context, line, parsed);
        const auto kind = token == "RANDOM"      ? bms_control_kind_t::random
                          : token == "SETRANDOM" ? bms_control_kind_t::set_random
                                                 : bms_control_kind_t::set_switch;
        if (token != "RANDOM") {
            add_simple_control(context, line, parsed, kind, value);
            return true;
        }
        if (is_control_nesting_limit_reached(context, line, parsed)) {
            return true;
        }
        const auto path = append_node(context.root, context.current_path,
                                      make_node(line, parsed, kind, context.ids, value));
        context.frames.push_back(
            {.kind = frame_kind_t::random, .parent_path = context.current_path, .path = path});
        context.current_path = path;
        return true;
    }
    if (token == "IF") {
        if (is_control_nesting_limit_reached(context, line, parsed)) {
            return true;
        }
        const auto value = parse_control_value(context, line, parsed);
        auto group_node = bms_control_node_t{
            .id = new_id<branch_id_t>(context.ids),
            .kind = bms_control_kind_t::if_group,
        };
        const auto group = append_node(context.root, context.current_path, std::move(group_node));
        const auto branch = append_node(
            context.root, group,
            make_node(line, parsed, bms_control_kind_t::if_branch, context.ids, value));
        context.frames.push_back({.kind = frame_kind_t::if_group,
                                  .parent_path = context.current_path,
                                  .path = group,
                                  .active_branch_path = branch});
        context.current_path = branch;
        return true;
    }
    if (token == "SWITCH") {
        if (is_control_nesting_limit_reached(context, line, parsed)) {
            return true;
        }
        const auto value = parse_control_value(context, line, parsed);
        const auto path = append_node(
            context.root, context.current_path,
            make_node(line, parsed, bms_control_kind_t::switch_block, context.ids, value));
        context.frames.push_back(
            {.kind = frame_kind_t::switch_block, .parent_path = context.current_path, .path = path});
        context.current_path = path;
        return true;
    }
    return false;
}

auto continue_if_branch(control_tree_build_context_t& context, const source_line_t& line,
                        const parsed_line_t& parsed, std::string_view token) -> bool {
    if (token != "ELSEIF" && token != "ELIF" && token != "ELSE") {
        return false;
    }
    if (context.frames.empty() || context.frames.back().kind != frame_kind_t::if_group ||
        !context.frames.back().active_branch_path.has_value()) {
        retain_malformed_control(context, line, parsed,
                                 "orphan conditional branch directive retained as raw text");
        return true;
    }
    auto& frame = context.frames.back();
    const auto is_else_if = token == "ELSEIF" || token == "ELIF";
    if (frame.terminal_branch_seen) {
        retain_malformed_control(context, line, parsed,
                                 is_else_if
                                     ? "conditional branch after #ELSE is retained as raw text"
                                     : "repeated #ELSE is retained as raw text");
        return true;
    }
    const auto kind = is_else_if ? bms_control_kind_t::else_if_branch
                                 : bms_control_kind_t::else_branch;
    const auto value = is_else_if ? parse_control_value(context, line, parsed) : std::nullopt;
    const auto branch = append_node(context.root, frame.path,
                                    make_node(line, parsed, kind, context.ids, value));
    frame.active_branch_path = branch;
    frame.terminal_branch_seen = !is_else_if;
    context.current_path = branch;
    return true;
}

auto continue_switch_branch(control_tree_build_context_t& context, const source_line_t& line,
                            const parsed_line_t& parsed, std::string_view token) -> bool {
    if (token != "CASE" && token != "DEF") {
        return false;
    }
    if (context.frames.empty() || context.frames.back().kind != frame_kind_t::switch_block) {
        retain_malformed_control(context, line, parsed,
                                 "orphan switch branch directive retained as raw text");
        return true;
    }
    auto& frame = context.frames.back();
    const auto is_default = token == "DEF";
    if (frame.terminal_branch_seen) {
        retain_malformed_control(context, line, parsed,
                                 is_default ? "repeated #DEF is retained as raw text"
                                            : "#CASE after #DEF is retained as raw text");
        return true;
    }
    const auto value = token == "CASE" ? parse_control_value(context, line, parsed)
                                       : std::nullopt;
    const auto kind = token == "CASE" ? bms_control_kind_t::case_branch
                                       : bms_control_kind_t::default_branch;
    const auto branch = append_node(
        context.root, frame.path, make_node(line, parsed, kind, context.ids, value));
    frame.active_branch_path = branch;
    frame.terminal_branch_seen = is_default;
    context.current_path = branch;
    return true;
}

auto close_control_block(control_tree_build_context_t& context, const source_line_t& line,
                         const parsed_line_t& parsed, std::string_view token) -> bool {
    const auto expected = token == "ENDIF"    ? frame_kind_t::if_group
                         : token == "ENDSW"   ? frame_kind_t::switch_block
                                                : frame_kind_t::random;
    const auto message = token == "ENDIF"    ? "orphan #ENDIF retained as raw text"
                         : token == "ENDSW"   ? "orphan #ENDSW retained as raw text"
                                                : "orphan #ENDRANDOM retained as raw text";
    if (token != "ENDIF" && token != "ENDSW" && token != "ENDRANDOM") {
        return false;
    }
    if (context.frames.empty() || context.frames.back().kind != expected) {
        retain_malformed_control(context, line, parsed, message);
        return true;
    }
    auto frame = std::move(context.frames.back());
    context.frames.pop_back();
    close_node(context.root, frame.path, line);
    context.current_path = std::move(frame.parent_path);
    return true;
}

auto process_skip_directive(control_tree_build_context_t& context, const source_line_t& line,
                            const parsed_line_t& parsed, std::string_view token) -> bool {
    if (token != "SKIP") {
        return false;
    }
    const auto active_case = std::ranges::find_if(context.frames.rbegin(), context.frames.rend(),
                                                  [](const auto& frame) {
                                                      return frame.kind == frame_kind_t::switch_block;
                                                  });
    if (active_case != context.frames.rend() && active_case->active_branch_path.has_value()) {
        add_simple_control(context, line, parsed, bms_control_kind_t::skip, std::nullopt);
    } else {
        retain_malformed_control(
            context, line, parsed,
            "#SKIP outside an active switch case is retained as raw text");
    }
    return true;
}

void report_unclosed_control_frames(bms_control_node_t& root,
                                   const std::vector<open_frame_t>& frames,
                                   bms_parse_outcome_t& result) {
    for (const auto& frame : frames) {
        const auto& node = node_at(root, frame.path);
        result.diagnostics.push_back({.severity = diagnostic_severity_t::warning,
                                      .code = bms_parse_diagnostic_code_t::malformed_control,
                                      .message = "control block reaches end of file without its "
                                                 "closing directive",
                                      .location = {node.source_order + 1, 1, node.source_span}});
    }
}

template <typename EnsureMeasure>
void ensure_initial_measure(chart_document_t& chart, EnsureMeasure&& ensure_measure) {
    ensure_initial_measure(chart, ensure_measure);
}

struct active_statement_scan_context_t {
    song_document_t& song;
    chart_document_t& chart;
    bms_parse_outcome_t& result;
    id_allocator_t& ids;
    const bms_parse_options_t& options;
    std::map<std::string, resource_id_t, std::less<>>& resource_ids;
    std::map<std::size_t, std::size_t>& last_measure_length_order;
    std::vector<std::pair<std::string, std::string>>& ordered_metadata_fields;
    std::vector<std::size_t>& ordered_metadata_source_orders;
    std::map<std::string, signed_rational_t, std::less<>>& extended_bpms;
    std::map<std::string, signed_rational_t, std::less<>>& stop_definitions;
    std::map<std::string, signed_rational_t, std::less<>>& scroll_definitions;
    std::optional<std::pair<const bms_control_node_t*, signed_rational_t>>& initial_bpm;
    std::vector<root_channel_line_t>& channel_lines;
    std::vector<std::string>& used_playable_channels;
    std::optional<unsigned int>& player_header;
    bool& octave_fp_header;
    std::function<measure_chunk_t&(std::size_t)> ensure_measure;
    std::function<void(const bms_control_node_t&, diagnostic_severity_t,
                       bms_parse_diagnostic_code_t, std::string)> diagnose;
    std::function<void(const source_line_t&, std::string_view, std::string_view,
                       const std::string&)> add_resource;
};

struct bms_resource_parse_context_t {
    song_document_t& song;
    bms_parse_outcome_t& result;
    id_allocator_t& ids;
    std::map<std::string, resource_id_t, std::less<>>& resource_ids;

    void add(const source_line_t& line, std::string_view command, std::string_view identifier,
             const std::string& name) {
        const auto normalized_id = text_util::ascii_upper(std::string(identifier));
        if (name.empty()) {
            add_line_diagnostic(result, diagnostic_severity_t::warning,
                                bms_parse_diagnostic_code_t::malformed_line,
                                "resource declaration has an empty path", line);
            return;
        }
        const auto target_key = std::string(command) + normalized_id;
        if (resource_ids.contains(target_key)) {
            add_line_diagnostic(
                result, diagnostic_severity_t::warning,
                bms_parse_diagnostic_code_t::duplicate_resource,
                "duplicate BMS resource identifier; first declaration remains the semantic "
                "binding",
                line);
            return;
        }
        const auto asset_id = new_id<asset_id_t>(ids);
        const auto resource_id = new_id<resource_id_t>(ids);
        const auto source_path = path_from_utf8(name);
        if (equal_ascii_case_insensitive(command, "WAV")) {
            song.audio_assets.push_back({.id = asset_id, .source_path = source_path});
            song.resources.push_back({
                .id = resource_id,
                .name = name,
                .asset = asset_id,
                .roles = {resource_role_t::sample, resource_role_t::bgm},
                .target_identities = {{format_family_t::bms, normalized_id, "WAV"}},
                .provenance = {.family = format_family_t::bms,
                               .profile = "bms-byte-parser",
                               .source_field = std::string(command) + normalized_id,
                               .source_spelling = line.text,
                               .source_order = line.order},
            });
        } else {
            song.visual_assets.push_back({.id = asset_id,
                                          .kind = asset_kind_t::image,
                                          .source_path = source_path});
            song.resources.push_back({
                .id = resource_id,
                .name = name,
                .asset = asset_id,
                .roles = {resource_role_t::bga},
                .target_identities = {{format_family_t::bms, normalized_id, "BMP"}},
                .provenance = {.family = format_family_t::bms,
                               .profile = "bms-byte-parser",
                               .source_field = std::string(command) + normalized_id,
                               .source_spelling = line.text,
                               .source_order = line.order},
            });
        }
        resource_ids.emplace(target_key, resource_id);
    }
};

auto collect_metadata_statement(active_statement_scan_context_t& context,
                                const bms_control_node_t& node, std::string_view token,
                                std::string_view value) -> bool {
    if (token == "OCT/FP") {
        context.octave_fp_header = true;
        return true;
    }
    if (!is_bms_metadata_command(token)) {
        return false;
    }
    if (token == "TITLE") {
        context.song.metadata.title = std::string(value);
    } else if (token == "ARTIST") {
        context.song.metadata.artist = std::string(value);
    } else if (token == "PLAYER") {
        const auto parsed_player = parse_integer(value);
        if (parsed_player.has_value() && *parsed_player >= 0 &&
            static_cast<std::uint64_t>(*parsed_player) <=
                std::numeric_limits<unsigned int>::max()) {
            context.player_header = static_cast<unsigned int>(*parsed_player);
        }
    } else if (token == "OFFSET") {
        const auto parsed_offset = parse_exact_decimal(value);
        if (parsed_offset) {
            context.chart.beat_zero_audio_offset_seconds = *parsed_offset;
        } else {
            context.diagnose(node, diagnostic_severity_t::warning,
                             bms_parse_diagnostic_code_t::invalid_number,
                             "OFFSET is not an exact signed decimal; raw text is retained");
        }
    }
    context.ordered_metadata_fields.emplace_back(token, node.raw_value);
    context.ordered_metadata_source_orders.push_back(node.source_order);
    return true;
}

auto collect_resource_statement(active_statement_scan_context_t& context,
                                const bms_control_node_t& node, std::string_view token,
                                std::string_view value) -> bool {
    if (is_resource_header(token, "WAV")) {
        context.add_resource(node_source_line(node), "WAV", token.substr(3), std::string(value));
        return true;
    }
    if (is_resource_header(token, "BMP")) {
        context.add_resource(node_source_line(node), "BMP", token.substr(3), std::string(value));
        return true;
    }
    return false;
}

__extension__ using wide_t = unsigned __int128;

// The fraction with the smallest denominator in the closed interval [low, high] (both positive,
// low <= high), by continued fractions.
auto simplest_between(wide_t low_n, wide_t low_d, wide_t high_n, wide_t high_d)
    -> std::pair<wide_t, wide_t> {
    const auto whole = low_n / low_d;
    if (whole * low_d == low_n) return {whole, 1};
    if (whole < high_n / high_d) return {whole + 1, 1};
    const auto [tail_n, tail_d] = simplest_between(high_d, high_n - whole * high_d, low_d,
                                                   low_n - whole * low_d);
    return {whole * tail_n + tail_d, tail_n};
}

// Players print measure lengths as rounded doubles ("0.104166666666667" for 5/48). A decimal that
// long cannot be a deliberate exact length, so read it as the simplest fraction that prints the
// same; short decimals stay exact.
auto recover_printed_fraction(std::string_view text, rational_t exact) -> rational_t {
    constexpr std::uint64_t exact_denominator_limit = 1'000'000;
    const auto dot = text.find('.');
    if (dot == std::string_view::npos || exact.denominator <= exact_denominator_limit) return exact;
    const auto digits = text.size() - dot - 1;
    if (digits > 30) return exact;
    wide_t scale = 1;
    for (std::size_t digit = 0; digit < digits; ++digit) scale *= 10;
    // exact = numerator / denominator; the printed digit count bounds the error by half a unit.
    const wide_t denominator = exact.denominator;
    const wide_t numerator = exact.numerator;
    if (2 * numerator * scale <= denominator) return exact;
    const auto low_n = (2 * numerator * scale - denominator);
    const auto high_n = (2 * numerator * scale + denominator);
    const auto common = 2 * denominator * scale;
    const auto [n, d] = simplest_between(low_n, common, high_n, common);
    if (d >= denominator || n > std::numeric_limits<std::uint64_t>::max() ||
        d > std::numeric_limits<std::uint64_t>::max()) return exact;
    return {static_cast<std::uint64_t>(n), static_cast<std::uint64_t>(d)};
}

auto parse_bms_measure_length(std::string_view value) -> std::optional<rational_t> {
    if (value.find_first_of("eE") == std::string_view::npos) {
        const auto parsed = parse_measure_length(value);
        if (!parsed) return std::nullopt;
        return recover_printed_fraction(value, *parsed);
    }
    const auto parsed = parse_positive_rational(value);
    if (!parsed) return std::nullopt;
    return rational_t{static_cast<std::uint64_t>(parsed->numerator), parsed->denominator};
}

auto collect_measure_length_statement(active_statement_scan_context_t& context,
                                      const bms_control_node_t& node,
                                      std::string_view token, std::string_view value,
                                      const std::optional<std::pair<std::size_t, std::string>>& command)
    -> bool {
    if (!command.has_value() || command->second != "02" || token.size() != 5 ||
        node.original_text.size() < 7 || node.original_text[0] != '#' ||
        node.original_text[6] != ':') {
        return false;
    }
    const auto measure = command->first;
    const auto parsed_length = parse_bms_measure_length(value);
    if (parsed_length.has_value()) {
        if (context.last_measure_length_order.contains(measure)) {
            context.diagnose(node, diagnostic_severity_t::warning,
                             bms_parse_diagnostic_code_t::invalid_measure,
                             "repeated measure-length declaration; later value is applied");
        }
        auto& measure_chunk = context.ensure_measure(measure);
        measure_chunk.definition.length = *parsed_length;
        measure_chunk.definition.meter.reset();
        context.last_measure_length_order[measure] = node.source_order;
    } else {
        context.diagnose(node, diagnostic_severity_t::error,
                         bms_parse_diagnostic_code_t::invalid_measure,
                         "invalid exact BMS measure length");
    }
    return true;
}

auto is_bpm_definition(std::string_view token) -> bool {
    const auto* directive = bms_timing_directive_t::from(token);
    return token == "BPM" || (directive && directive->name == "BPM");
}

auto collect_bpm_statement(active_statement_scan_context_t& context,
                           const bms_control_node_t& node, std::string_view token,
                           std::string_view value) -> bool {
    if (!is_bpm_definition(token)) {
        return false;
    }
    const auto parsed_value = parse_positive_rational(value);
    if (!parsed_value.has_value()) {
        context.diagnose(node, diagnostic_severity_t::warning,
                         bms_parse_diagnostic_code_t::invalid_number,
                         "BPM declaration is not a positive exact number; raw text is retained");
        return true;
    }
    if (token == "BPM") {
        if (context.initial_bpm.has_value()) {
            context.diagnose(node, diagnostic_severity_t::warning,
                             bms_parse_diagnostic_code_t::invalid_number,
                             "duplicate direct BPM declaration; later valid value is applied");
        }
        context.initial_bpm = std::pair{&node, *parsed_value};
    } else {
        const auto identifier = std::string(token.substr(3, 2));
        if (context.extended_bpms.contains(identifier)) {
            context.diagnose(node, diagnostic_severity_t::warning,
                             bms_parse_diagnostic_code_t::invalid_number,
                             "duplicate extended BPM identifier; later valid value is applied");
        }
        context.extended_bpms.insert_or_assign(identifier, *parsed_value);
    }
    return true;
}

auto is_stop_definition(std::string_view token) -> bool {
    const auto* directive = bms_timing_directive_t::from(token);
    return directive && directive->name == "STOP";
}

auto collect_stop_statement(active_statement_scan_context_t& context,
                            const bms_control_node_t& node, std::string_view token,
                            std::string_view value) -> bool {
    if (!is_stop_definition(token)) {
        return false;
    }
    const auto parsed_value = parse_positive_rational(value);
    if (!parsed_value.has_value()) {
        context.diagnose(node, diagnostic_severity_t::warning,
                         bms_parse_diagnostic_code_t::invalid_number,
                         "STOP declaration is not a positive exact number; raw text is retained");
        return true;
    }
    const auto identifier = std::string(token.substr(4, 2));
    if (context.stop_definitions.contains(identifier)) {
        context.diagnose(node, diagnostic_severity_t::warning,
                         bms_parse_diagnostic_code_t::invalid_number,
                         "duplicate STOP identifier; later valid value is applied");
    }
    context.stop_definitions.insert_or_assign(identifier, *parsed_value);
    return true;
}

auto collect_scroll_statement(active_statement_scan_context_t& context,
    const bms_control_node_t& node, std::string_view token, std::string_view value) -> bool {
    const auto* directive = bms_timing_directive_t::from(token);
    if (!directive || directive->name != "SCROLL") return false;
    const auto parsed = parse_exact_decimal(value, 38);
    if (!parsed) {
        context.diagnose(node, diagnostic_severity_t::warning, bms_parse_diagnostic_code_t::invalid_number,
                         "SCROLL declaration is not an exact decimal; raw text is retained");
        return true;
    }
    context.scroll_definitions.insert_or_assign(std::string(token.substr(6)), *parsed);
    return true;
}

auto retain_custom_header(active_statement_scan_context_t& context,
                          const bms_control_node_t& node, std::string_view token,
                          const std::optional<std::pair<std::size_t, std::string>>& command) -> bool {
    if (command.has_value() || !is_safe_bms_header_token(token)) {
        return false;
    }
    context.ordered_metadata_fields.emplace_back(token, node.raw_value);
    context.ordered_metadata_source_orders.push_back(node.source_order);
    return true;
}

void collect_channel_statement(active_statement_scan_context_t& context,
                               const bms_control_node_t& node, std::string_view token,
                               std::string_view value,
                               const std::optional<std::pair<std::size_t, std::string>>& command) {
    if (!command.has_value() || node.original_text.size() < 7 || node.original_text[0] != '#' ||
        node.original_text[6] != ':' || command->second == "02") {
        return;
    }
    if (value.empty() || value.size() % 2 != 0 ||
        !std::ranges::all_of(value, [](char character) { return is_base36_digit(character); })) {
        return;
    }
    const auto& [measure_number, channel] = *command;
    context.channel_lines.push_back({.node = &node,
                                     .measure = measure_number,
                                     .channel = channel,
                                     .payload = std::string(value),
                                     .pair_count = value.size() / 2});
    if (is_playable_channel(channel)) {
        context.used_playable_channels.push_back(channel);
    }
    (void)token;
}

void collect_active_statement_data(active_statement_scan_context_t& context,
                                   const std::vector<const bms_control_node_t*>& statements) {
    for (const auto* node : statements) {
        const auto token = text_util::ascii_upper(std::string(node->command_token));
        const auto value = trim_ascii(node->raw_value);
        if (collect_metadata_statement(context, *node, token, value) ||
            collect_resource_statement(context, *node, token, value)) {
            continue;
        }
        const auto command = parse_measure_command(token);
        if (collect_measure_length_statement(context, *node, token, value, command) ||
            collect_bpm_statement(context, *node, token, value) ||
            collect_stop_statement(context, *node, token, value) ||
            collect_scroll_statement(context, *node, token, value) ||
            retain_custom_header(context, *node, token, command)) {
            continue;
        }
        collect_channel_statement(context, *node, token, value, command);
    }
    std::ranges::stable_sort(context.channel_lines, {},
                             [](const auto& line) { return line.node->source_order; });
}

void finalize_bms_document(song_document_t& song, chart_document_t& chart,
                           const std::map<std::size_t, std::size_t>& measure_length_orders,
                           std::vector<std::pair<std::string, std::string>> ordered_metadata_fields,
                           std::vector<std::size_t> ordered_metadata_source_orders,
                           encoding_provenance_t encoding) {
    std::ranges::stable_sort(chart.bpms, {},
                             [](const auto& event) { return event.provenance.source_order; });
    chart.bms_bpm_projections.reserve(chart.bpms.size());
    for (const auto& event : chart.bpms) {
        chart.bms_bpm_projections.push_back({
            .event = event.id,
            .source_order = event.provenance.source_order,
            .imported_position = event.position,
            .imported_beats_per_minute = event.beats_per_minute,
        });
    }
    chart.bms_pause_projections.reserve(chart.pauses.size());
    for (const auto& event : chart.pauses) {
        chart.bms_pause_projections.push_back({
            .event = event.id,
            .source_order = event.provenance.source_order,
            .imported_position = event.position,
            .imported_kind = event.kind,
            .imported_duration = event.duration,
            .imported_source_unit = event.source_unit,
        });
    }
    chart.metadata.name = song.metadata.title;
    chart.metadata.author = song.metadata.artist;
    chart.metadata.format_groups.push_back({.family = format_family_t::bms,
                                            .encoding = encoding});
    chart.bms_measure_length_projections.reserve(chart.measures.size());
    for (std::size_t index = 0; index < chart.measures.size(); ++index) {
        const auto source = measure_length_orders.find(index);
        chart.bms_measure_length_projections.push_back({
            .measure_index = index,
            .source_order = source == measure_length_orders.end()
                                ? std::numeric_limits<std::size_t>::max()
                                : source->second,
            .imported_length = chart.measures[index].definition.length,
        });
    }
    song.metadata.format_groups.push_back({
        .family = format_family_t::bms,
        .ordered_fields = std::move(ordered_metadata_fields),
        .ordered_field_source_orders = std::move(ordered_metadata_source_orders),
        .encoding = encoding,
    });
}

auto source_for_measure_length(
    std::size_t measure_index, const std::map<std::size_t, std::size_t>& source_orders,
    const std::vector<const bms_control_node_t*>& statements) -> const bms_control_node_t* {
    const auto source_order = source_orders.find(measure_index);
    if (source_order == source_orders.end()) {
        return nullptr;
    }
    const auto found = std::ranges::find_if(statements, [&](const auto* node) {
        return node->source_order == source_order->second;
    });
    return found == statements.end() ? nullptr : *found;
}

void fallback_measure_length(chart_document_t& chart, std::size_t measure_index,
                             std::string message,
                             std::set<std::size_t>& inexact_measure_geometry,
                             const std::map<std::size_t, std::size_t>& source_orders,
                             const std::vector<const bms_control_node_t*>& statements,
                             bms_parse_outcome_t& result) {
    inexact_measure_geometry.insert(measure_index);
    chart.measures[measure_index].definition = {{1, 1}, meter_t{4, 4}};
    if (const auto* node = source_for_measure_length(measure_index, source_orders, statements);
        node != nullptr) {
        add_line_diagnostic(result, diagnostic_severity_t::warning,
                            bms_parse_diagnostic_code_t::invalid_measure, std::move(message),
                            node_source_line(*node));
        return;
    }
    result.diagnostics.push_back({.severity = diagnostic_severity_t::warning,
                                  .code = bms_parse_diagnostic_code_t::invalid_measure,
                                  .message = std::move(message),
                                  .location = {.line = 1, .column = 1}});
}

void validate_measure_geometry(chart_document_t& chart, const bms_parse_options_t& options,
                               const std::map<std::size_t, std::size_t>& source_orders,
                               const std::vector<const bms_control_node_t*>& statements,
                               std::set<std::size_t>& inexact_measure_geometry,
                               bms_parse_outcome_t& result) {
    for (std::size_t index = 0; index < chart.measures.size(); ++index) {
        const auto required = checked_lcm(chart.divisions_per_standard_measure,
                                          chart.measures[index].definition.length.denominator);
        if (!required.has_value() || *required > options.max_chart_resolution) {
            fallback_measure_length(
                chart, index,
                "measure-length denominator exceeds the configured exact chart-resolution limit; "
                "the measure remains raw and uses its default effective length",
                inexact_measure_geometry, source_orders, statements, result);
            continue;
        }
        chart.divisions_per_standard_measure = *required;
    }
    for (std::size_t index = 0; index < chart.measures.size(); ++index) {
        const auto capacity = validate_measure(chart.divisions_per_standard_measure,
                                               chart.measures[index].definition);
        if (!capacity.row_capacity.has_value()) {
            fallback_measure_length(
                chart, index,
                "measure length cannot be represented safely at the selected chart resolution; "
                "the measure remains raw and uses its default effective length",
                inexact_measure_geometry, source_orders, statements, result);
        }
    }
}

void report_unavailable_lane_profile(const bms_parse_options_t& options,
                                     const std::vector<root_channel_line_t>& channel_lines,
                                     bms_parse_outcome_t& result) {
    std::string message = "selected BMS lane profile is unknown or does not map every recognized "
                          "playable channel; channel payloads remain raw";
    if (!result.value.lane_profile_detection->evidence.empty()) {
        message += ": ";
        message += result.value.lane_profile_detection->evidence.front().message;
    }
    const auto first_playable = std::ranges::find_if(
        channel_lines, [](const auto& line) { return is_playable_channel(line.channel); });
    if (first_playable != channel_lines.end()) {
        add_line_diagnostic(result, diagnostic_severity_t::warning,
                            bms_parse_diagnostic_code_t::unsupported_channel, std::move(message),
                            node_source_line(*first_playable->node));
        return;
    }
    result.diagnostics.push_back({.severity = diagnostic_severity_t::warning,
                                  .code = bms_parse_diagnostic_code_t::unsupported_channel,
                                  .message = std::move(message),
                                  .location = {.line = 1, .column = 1}});
    (void)options;
}

auto select_lane_profile(chart_document_t& chart, bms_parse_outcome_t& result,
                         const bms_parse_options_t& options, id_allocator_t& ids,
                         std::optional<unsigned int> player_header, bool octave_fp_header,
                         const std::vector<std::string>& used_playable_channels,
                         const std::vector<root_channel_line_t>& channel_lines)
    -> std::optional<lane_input_profile_t> {
    if (used_playable_channels.empty() && !options.lane_profile_id.has_value() && !options.authored_lane_profile) {
        return std::nullopt;
    }
    result.value.lane_profile_detection = detect_bms_lane_profile({
        .file_extension = options.file_extension,
        .player_header = player_header,
        .octave_fp_header = octave_fp_header,
        .used_channels = used_playable_channels,
        .explicit_profile_id = options.lane_profile_id,
    });
    auto selected = options.authored_lane_profile ? options.authored_lane_profile : result.value.lane_profile_detection->selected_profile;
    if (options.lane_profile_id.has_value() && !selected.has_value()) {
        report_unavailable_lane_profile(options, channel_lines, result);
    }
    if (selected.has_value()) {
        chart.input_profile = selected->input;
        chart.lane_layouts.push_back({.id = new_id<object_id_t>(ids),
                                      .position = {chart.measures.front().id, 0},
                                      .layout = selected->layout});
    }
    return selected;
}

auto is_builtin_materialized_channel(std::string_view channel) -> bool {
    return channel == "01" || channel == "03" || channel == "08" || channel == "09" ||
           channel == "04" || channel == "06" || channel == "07" || channel == "0A" || channel == "SC";
}

template <typename Diagnose>
void prepare_channel_line(chart_document_t& chart, const bms_parse_options_t& options,
                          const std::optional<lane_input_profile_t>& selected_profile,
                          root_channel_line_t& channel_line,
                          std::vector<std::string>& unmaterialized_long_note_channels,
                          Diagnose&& diagnose) {
    const auto channel = std::string_view(channel_line.channel);
    if (is_builtin_materialized_channel(channel)) {
        channel_line.materialize = true;
    } else if (is_playable_channel(channel)) {
        if (!selected_profile.has_value()) {
            diagnose(*channel_line.node, diagnostic_severity_t::warning,
                     bms_parse_diagnostic_code_t::unsupported_channel,
                     options.lane_profile_id.has_value()
                         ? "playable-family payload is incompatible with the selected profile "
                           "and remains raw"
                         : "playable-family payload remains raw until a lane profile is selected");
            return;
        }
        channel_line.materialize = true;
    } else {
        return;
    }

    const auto required = resolution_for_slots(chart.divisions_per_standard_measure,
                                               chart.measures[channel_line.measure].definition,
                                               channel_line.pair_count);
    if (!required.has_value() || *required > options.max_chart_resolution) {
        channel_line.materialize = false;
        if (is_long_note_channel(channel)) {
            unmaterialized_long_note_channels.push_back(channel_line.channel);
        }
        diagnose(*channel_line.node, diagnostic_severity_t::warning,
                 bms_parse_diagnostic_code_t::invalid_measure,
                 "exact channel subdivisions exceed the configured chart-resolution limit; "
                 "payload remains raw");
        return;
    }
    chart.divisions_per_standard_measure = *required;
}

template <typename Diagnose>
void prepare_channel_materialization(
    chart_document_t& chart, const bms_parse_options_t& options,
    const std::optional<lane_input_profile_t>& selected_profile,
    std::vector<root_channel_line_t>& channel_lines,
    std::vector<std::string>& unmaterialized_long_note_channels, Diagnose&& diagnose) {
    for (auto& channel_line : channel_lines) {
        prepare_channel_line(chart, options, selected_profile, channel_line,
                             unmaterialized_long_note_channels, diagnose);
    }
}

auto ensure_bms_source_measure(chart_document_t& chart, std::size_t index, id_allocator_t& ids)
    -> measure_chunk_t& {
    while (chart.measures.size() <= index)
        chart.measures.push_back({.id = new_id<measure_id_t>(ids), .definition = {{1, 1}, meter_t{4, 4}}});
    return chart.measures[index];
}

auto parse_bms_source(std::span<const std::byte> bytes, const bms_parse_options_t& options,
                      id_allocator_t& ids)
    -> bms_parse_outcome_t {
    bms_parse_outcome_t result{};
    auto lines = prepare_bms_source_lines(bytes, options, result);
    if (!lines.has_value()) {
        return result;
    }

    song_document_t song;
    song.id = new_id<song_id_t>(ids);
    chart_document_t chart;
    chart.id = new_id<chart_id_t>(ids);
    chart.source_family = format_family_t::bms;
    chart.bms_control_tree = bms_control_node_t{
        .id = new_id<branch_id_t>(ids),
        .kind = bms_control_kind_t::root,
        .source_span = {0, bytes.size()},
    };

    auto& root = *chart.bms_control_tree;
    std::vector<std::size_t> current_path;
    std::vector<open_frame_t> frames;

    auto ensure_measure = [&](std::size_t index) -> measure_chunk_t& {
        return ensure_bms_source_measure(chart, index, ids);
    };

    auto control_context = control_tree_build_context_t{root, current_path, frames, options, result,
                                                        ids};

    for (const auto& line : *lines) {
        const auto parsed = parse_line(line.text);
        const auto token = text_util::ascii_upper(std::string(parsed.token));
        if (retain_non_control_header(line, parsed, token, root, current_path, ids)) {
            continue;
        }
        if (retain_measure_command(line, parsed, token, root, current_path, ids, result,
                                   ensure_measure)) {
            continue;
        }

        if (open_control_block(control_context, line, parsed, token)) {
            continue;
        }

        if (continue_if_branch(control_context, line, parsed, token) ||
            continue_switch_branch(control_context, line, parsed, token) ||
            close_control_block(control_context, line, parsed, token) ||
            process_skip_directive(control_context, line, parsed, token)) {
            continue;
        }

        // All unrecognized headers and channel payloads stay byte-addressable in the tree.
        push_raw_statement(root, current_path, line, parsed, ids);
    }

    report_unclosed_control_frames(root, frames, result);

    if (chart.measures.empty()) {
        (void)ensure_measure(0);
    }

    result.value.encoding.original_bytes = std::make_shared<const std::vector<std::byte>>(bytes.begin(), bytes.end());
    result.value.encoding.unchanged_since_import = true;
    song.charts.push_back(std::move(chart));
    result.value.song = std::move(song);
    return result;
}

auto parse_bms_legacy_witness(std::span<const std::byte> bytes, bms_parse_options_t options)
    -> bms_parse_outcome_t {
    id_allocator_t ids;
    auto result = parse_bms_source(bytes, options, ids);
    if (!result.value.song) return result;
    auto song = std::move(*result.value.song);
    result.value.song.reset();
    auto chart = std::move(song.charts.front());
    song.charts.clear();
    auto& root = *chart.bms_control_tree;
    std::map<std::string, resource_id_t, std::less<>> resource_ids;
    std::map<std::size_t, std::size_t> last_measure_length_order;
    std::vector<std::pair<std::string, std::string>> ordered_metadata_fields;
    std::vector<std::size_t> ordered_metadata_source_orders;
    auto ensure_measure = [&](std::size_t index) -> measure_chunk_t& {
        return ensure_bms_source_measure(chart, index, ids);
    };
    auto resource_context = bms_resource_parse_context_t{song, result, ids, resource_ids};
    auto add_resource = [&](const source_line_t& line, std::string_view command,
                            std::string_view identifier, const std::string& name) {
        resource_context.add(line, command, identifier, name);
    };

    auto line_diagnostic = [&](const bms_control_node_t& node, diagnostic_severity_t severity,
                               bms_parse_diagnostic_code_t code, std::string message) {
        add_line_diagnostic(result, severity, code, std::move(message), node_source_line(node));
    };
    auto active_statements = collect_active_statements(root, options, chart, result);

    std::map<std::string, signed_rational_t, std::less<>> extended_bpms;
    std::map<std::string, signed_rational_t, std::less<>> stop_definitions;
    std::map<std::string, signed_rational_t, std::less<>> scroll_definitions;
    std::optional<std::pair<const bms_control_node_t*, signed_rational_t>> initial_bpm;
    std::vector<root_channel_line_t> channel_lines;
    std::vector<std::string> used_playable_channels;
    std::optional<unsigned int> player_header;
    bool octave_fp_header = false;

    auto statement_context = active_statement_scan_context_t{
        song, chart, result, ids, options, resource_ids, last_measure_length_order,
        ordered_metadata_fields, ordered_metadata_source_orders, extended_bpms, stop_definitions, scroll_definitions,
        initial_bpm, channel_lines, used_playable_channels, player_header, octave_fp_header,
        ensure_measure, line_diagnostic, add_resource};
    collect_active_statement_data(statement_context, active_statements);

    chart.metadata.name = song.metadata.title;
    chart.metadata.author = song.metadata.artist;

    auto inexact_measure_geometry = std::set<std::size_t>{};
    validate_measure_geometry(chart, options, last_measure_length_order, active_statements,
                              inexact_measure_geometry, result);

    auto selected_profile = select_lane_profile(
        chart, result, options, ids, player_header, octave_fp_header, used_playable_channels,
        channel_lines);

    auto unmaterialized_long_note_channels = std::vector<std::string>{};
    prepare_channel_materialization(chart, options, selected_profile, channel_lines,
                                   unmaterialized_long_note_channels, line_diagnostic);

    auto long_note_endpoints =
        std::map<std::uint64_t, std::vector<long_note_endpoint_t>>{};
    auto channel_materializer = bms_channel_materializer_t{.chart = chart, .song = song, .result = result,
        .ids = ids, .resource_ids = resource_ids, .extended_bpms = extended_bpms,
        .stop_definitions = stop_definitions, .scroll_definitions = scroll_definitions, .selected_profile = selected_profile,
        .long_note_endpoints = long_note_endpoints};
    auto incomplete_long_note_lanes = std::set<std::uint64_t>{};
    for (const auto& channel : unmaterialized_long_note_channels) {
        if (const auto* lane = channel_materializer.lane_for_channel(channel); lane != nullptr) {
            incomplete_long_note_lanes.insert(lane->id.value);
        }
    }

    if (initial_bpm.has_value()) {
        chart.bpms.push_back({
            .id = new_id<object_id_t>(ids),
            .position = {chart.measures.front().id, 0},
            .beats_per_minute = initial_bpm->second,
            .provenance = source_provenance("BPM", initial_bpm->first->original_text,
                                            initial_bpm->first->source_order),
        });
    }

    for (const auto& channel_line : channel_lines) {
        if (channel_line.materialize) {
            channel_materializer.materialize(channel_line);
        }
    }

    auto long_note_context = channel_materializer.pairing_context(
        incomplete_long_note_lanes, inexact_measure_geometry);
    pair_long_note_endpoints(long_note_context, long_note_endpoints);

    finalize_bms_document(song, chart, last_measure_length_order,
                         std::move(ordered_metadata_fields),
                         std::move(ordered_metadata_source_orders), result.value.encoding);
    song.charts.push_back(std::move(chart));
    result.value.song = std::move(song);
    return result;
}

} // namespace wormedit

namespace wormedit {
auto bms_source_statement_t::from(const bms_control_node_t& node) -> bms_source_statement_t {
    return from(std::string_view{node.original_text});
}
auto bms_source_statement_t::from(std::string_view line) -> bms_source_statement_t {
    const auto parsed = parse_line(line);
    const auto token = text_util::ascii_upper(parsed.token);
    return {.token = token, .value = parsed.semantic_value, .separator = parsed.separator,
            .measure_channel = parse_measure_command(token)};
}
auto bms_source_statement_t::positive_number() const -> std::optional<signed_rational_t> {
    return parse_positive_rational(value);
}
auto bms_source_statement_t::measure_length() const -> std::optional<rational_t> {
    return parse_bms_measure_length(value);
}
auto bms_source_statement_t::is_control() const -> bool { return is_control_token(token); }
auto bms_source_statement_t::is_playable() const -> bool {
    return measure_channel && is_playable_channel(measure_channel->second);
}
auto bms_source_statement_t::is_builtin_channel() const -> bool {
    return measure_channel && is_builtin_materialized_channel(measure_channel->second);
}
auto bms_source_statement_t::bga_kind() const -> std::optional<visual_event_kind_t> {
    return measure_channel ? bms_bga_kind_for_channel(measure_channel->second) : std::nullopt;
}
} // namespace wormedit
