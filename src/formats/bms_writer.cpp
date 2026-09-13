#include <wormedit/formats/bms_writer.h>

#include <wormedit/core/bms_profile_detection.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <limits>
#include <map>
#include <numeric>
#include <ranges>
#include <set>
#include <string_view>
#include <tuple>
#include <utility>

#if defined(WORMEDIT_HAS_ICONV)
#include <iconv.h>
#endif

namespace wormedit {
namespace {

struct write_failure_t {
    bms_write_diagnostic_code_t code;
    std::string message;
    std::string path;
};

struct text_encode_result_t {
    std::optional<std::string> bytes;
    std::size_t error_offset{};
    bool unsupported{};
};

[[noreturn]] auto fail(bms_write_diagnostic_code_t code, std::string message, std::string path)
    -> void {
    throw write_failure_t{code, std::move(message), std::move(path)};
}

auto warn(std::vector<bms_write_diagnostic_t>& diagnostics, bms_write_diagnostic_code_t code,
          std::string message, std::string path) -> void {
    diagnostics.push_back(
        {diagnostic_severity_t::warning, code, std::move(message), std::move(path)});
}

auto encoding_name(text_encoding_t encoding) -> std::string_view {
    switch (encoding) {
    case text_encoding_t::unknown:
        return "unknown";
    case text_encoding_t::ascii:
        return "ASCII";
    case text_encoding_t::utf8:
        return "UTF-8";
    case text_encoding_t::cp932:
        return "CP932";
    case text_encoding_t::euc_kr:
        return "EUC-KR";
    case text_encoding_t::cp949:
        return "CP949";
    case text_encoding_t::windows_1252:
        return "Windows-1252";
    }
    return "unknown";
}

auto iconv_name(text_encoding_t encoding) -> std::optional<std::string_view> {
    switch (encoding) {
    case text_encoding_t::cp932:
        return "CP932";
    case text_encoding_t::euc_kr:
        return "EUC-KR";
    case text_encoding_t::cp949:
        return "CP949";
    case text_encoding_t::windows_1252:
        return "WINDOWS-1252";
    case text_encoding_t::unknown:
    case text_encoding_t::ascii:
    case text_encoding_t::utf8:
        return std::nullopt;
    }
    return std::nullopt;
}

auto encode_normalized_bms(std::string_view utf8_with_bom, text_encoding_t encoding)
    -> text_encode_result_t {
    constexpr std::string_view utf8_bom = "\xef\xbb\xbf";
    if (!utf8_with_bom.starts_with(utf8_bom)) {
        return {.error_offset = 0};
    }
    if (encoding == text_encoding_t::utf8) {
        return {.bytes = std::string(utf8_with_bom)};
    }
    const auto utf8 = utf8_with_bom.substr(utf8_bom.size());
    if (encoding == text_encoding_t::ascii) {
        for (std::size_t index = 0; index < utf8.size(); ++index) {
            if (static_cast<unsigned char>(utf8[index]) > 0x7fU) {
                return {.error_offset = index};
            }
        }
        return {.bytes = std::string(utf8)};
    }

    const auto destination_name = iconv_name(encoding);
    if (!destination_name.has_value()) {
        return {.unsupported = true};
    }
#if defined(WORMEDIT_HAS_ICONV)
    const auto converter = iconv_open(std::string(*destination_name).c_str(), "UTF-8");
    if (converter == reinterpret_cast<iconv_t>(-1)) {
        return {.unsupported = true};
    }
    const auto initial_capacity = utf8.size() > (std::numeric_limits<std::size_t>::max() - 32U) / 2U
                                      ? std::numeric_limits<std::size_t>::max()
                                      : std::max<std::size_t>(utf8.size() * 2U + 32U, 32U);
    if (initial_capacity == std::numeric_limits<std::size_t>::max()) {
        iconv_close(converter);
        return {.error_offset = utf8.size()};
    }
    std::string output(initial_capacity, '\0');
    auto* source = const_cast<char*>(utf8.data());
    auto source_remaining = utf8.size();
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
        if (errno != E2BIG || output.size() > std::numeric_limits<std::size_t>::max() / 2U) {
            const auto error_offset = utf8.size() - source_remaining;
            iconv_close(converter);
            return {.error_offset = error_offset};
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
            return {.error_offset = utf8.size()};
        }
        output.resize(output.size() * 2U);
    }
    iconv_close(converter);
    output.resize(written);
    return {.bytes = std::move(output)};
#else
    return {.unsupported = true};
#endif
}

auto base36_digit(char value) -> bool {
    return (value >= '0' && value <= '9') || (value >= 'A' && value <= 'Z');
}

auto base36_id(std::size_t value) -> std::string {
    constexpr std::string_view digits = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    std::string result(2, '0');
    result[0] = digits[(value / 36U) % 36U];
    result[1] = digits[value % 36U];
    return result;
}

auto bms_identity(const resource_entry_t& resource, std::string_view name_space)
    -> std::optional<std::string> {
    const auto identity =
        std::ranges::find_if(resource.target_identities, [&](const auto& candidate) {
            return candidate.family == format_family_t::bms &&
                   candidate.namespace_name == name_space;
        });
    if (identity == resource.target_identities.end() || identity->identifier.size() != 2) {
        return std::nullopt;
    }
    std::string result = identity->identifier;
    for (auto& character : result) {
        if (character >= 'a' && character <= 'z') {
            character = static_cast<char>(character - 'a' + 'A');
        }
    }
    if (!base36_digit(result[0]) || !base36_digit(result[1]) || result == "00") {
        return std::nullopt;
    }
    return result;
}

auto has_line_break_or_nul(std::string_view value) -> bool {
    return std::ranges::any_of(value, [](char character) {
        return character == '\r' || character == '\n' || character == '\0';
    });
}

auto has_edge_whitespace(std::string_view value) -> bool {
    return !value.empty() && (std::isspace(static_cast<unsigned char>(value.front())) != 0 ||
                              std::isspace(static_cast<unsigned char>(value.back())) != 0);
}

auto safe_metadata_token(std::string_view token) -> bool {
    if (token.empty() || !((token.front() >= 'A' && token.front() <= 'Z') ||
                           (token.front() >= 'a' && token.front() <= 'z'))) {
        return false;
    }
    return std::ranges::all_of(token, [](char character) {
        return (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') ||
               (character >= '0' && character <= '9') || character == '_';
    });
}

auto ascii_upper(std::string value) -> std::string {
    for (auto& character : value) {
        if (character >= 'a' && character <= 'z') {
            character = static_cast<char>(character - 'a' + 'A');
        }
    }
    return value;
}

auto trim_ascii_whitespace(std::string_view value) -> std::string_view {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
        value.remove_prefix(1);
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.remove_suffix(1);
    }
    return value;
}

auto generated_or_channel_token(std::string_view token) -> bool {
    const auto upper = ascii_upper(std::string(token));
    if (upper == "PLAYER" || upper == "BPM" || upper == "STOP" || upper == "WAV" ||
        upper == "BMP" || upper == "RANDOM" || upper == "SETRANDOM" || upper == "SWITCH" ||
        upper == "SETSWITCH" || upper == "IF" || upper == "ELSEIF" || upper == "ELSE" ||
        upper == "ENDIF" || upper == "ENDSW" || upper == "CASE" || upper == "SKIP" ||
        upper == "DEF") {
        return true;
    }
    const auto is_base36 = [](char value) { return base36_digit(value); };
    if ((upper.size() == 5 && upper.starts_with("WAV") && is_base36(upper[3]) &&
         is_base36(upper[4])) ||
        (upper.size() == 5 && upper.starts_with("BMP") && is_base36(upper[3]) &&
         is_base36(upper[4])) ||
        (upper.size() == 5 && upper.starts_with("BPM") && is_base36(upper[3]) &&
         is_base36(upper[4])) ||
        (upper.size() == 6 && upper.starts_with("STOP") && is_base36(upper[4]) &&
         is_base36(upper[5]))) {
        return true;
    }
    return upper.size() == 5 && upper[0] >= '0' && upper[0] <= '9' && upper[1] >= '0' &&
           upper[1] <= '9' && upper[2] >= '0' && upper[2] <= '9' && is_base36(upper[3]) &&
           is_base36(upper[4]);
}

auto valid_utf8(std::string_view text) -> bool {
    std::size_t index{};
    while (index < text.size()) {
        const auto first = static_cast<unsigned char>(text[index]);
        if (first <= 0x7fU) {
            ++index;
            continue;
        }
        std::size_t width{};
        std::uint32_t codepoint{};
        if (first >= 0xc2U && first <= 0xdfU) {
            width = 2;
            codepoint = first & 0x1fU;
        } else if (first >= 0xe0U && first <= 0xefU) {
            width = 3;
            codepoint = first & 0x0fU;
        } else if (first >= 0xf0U && first <= 0xf4U) {
            width = 4;
            codepoint = first & 0x07U;
        } else {
            return false;
        }
        if (width > text.size() - index)
            return false;
        for (std::size_t offset = 1; offset < width; ++offset) {
            const auto next = static_cast<unsigned char>(text[index + offset]);
            if ((next & 0xc0U) != 0x80U)
                return false;
            codepoint = (codepoint << 6U) | (next & 0x3fU);
        }
        if ((width == 3 && codepoint < 0x800U) || (width == 4 && codepoint < 0x10000U) ||
            (codepoint >= 0xd800U && codepoint <= 0xdfffU) || codepoint > 0x10ffffU) {
            return false;
        }
        index += width;
    }
    return true;
}

auto retained_source_bytes(const song_document_t& song, const chart_document_t& chart)
    -> std::shared_ptr<const std::vector<std::byte>> {
    const auto find = [](const std::vector<format_metadata_t>& groups)
        -> std::shared_ptr<const std::vector<std::byte>> {
        const auto group = std::ranges::find_if(groups, [](const auto& candidate) {
            return candidate.family == format_family_t::bms &&
                   candidate.encoding.unchanged_since_import &&
                   candidate.encoding.original_bytes != nullptr;
        });
        return group == groups.end() ? nullptr : group->encoding.original_bytes;
    };
    if (auto bytes = find(chart.metadata.format_groups); bytes)
        return bytes;
    return find(song.metadata.format_groups);
}

auto exact_decimal(std::uint64_t numerator, std::uint64_t denominator, std::string path)
    -> std::string {
    if (numerator == 0 || denominator == 0) {
        fail(bms_write_diagnostic_code_t::unrepresentable_number,
             "BMS numeric values must be positive exact rationals", std::move(path));
    }
    const auto divisor = std::gcd(numerator, denominator);
    numerator /= divisor;
    denominator /= divisor;
    auto remainder_denominator = denominator;
    while (remainder_denominator % 2 == 0)
        remainder_denominator /= 2;
    while (remainder_denominator % 5 == 0)
        remainder_denominator /= 5;
    if (remainder_denominator != 1) {
        fail(bms_write_diagnostic_code_t::unrepresentable_number,
             "BMS text cannot represent this rational as an exact terminating decimal",
             std::move(path));
    }

    auto output = std::to_string(numerator / denominator);
    auto remainder = numerator % denominator;
    if (remainder == 0)
        return output;
    output.push_back('.');
    // Compute (remainder * 10) / denominator without overflowing uint64_t.
    // Each decimal digit needs only ten bounded modular additions.
    while (remainder != 0) {
        unsigned digit{};
        std::uint64_t next_remainder{};
        for (unsigned iteration = 0; iteration < 10; ++iteration) {
            if (next_remainder >= denominator - remainder) {
                next_remainder -= denominator - remainder;
                ++digit;
            } else {
                next_remainder += remainder;
            }
        }
        output.push_back(static_cast<char>('0' + digit));
        remainder = next_remainder;
    }
    return output;
}

auto exact_decimal(signed_rational_t value, std::string path) -> std::string {
    if (value.numerator <= 0) {
        fail(bms_write_diagnostic_code_t::unrepresentable_number,
             "BMS timing values must be positive exact rationals", std::move(path));
    }
    return exact_decimal(static_cast<std::uint64_t>(value.numerator), value.denominator,
                         std::move(path));
}

auto checked_lcm(std::uint64_t left, std::uint64_t right) -> std::optional<std::uint64_t> {
    if (left == 0 || right == 0)
        return std::nullopt;
    const auto reduced = left / std::gcd(left, right);
    if (right > std::numeric_limits<std::uint64_t>::max() / reduced)
        return std::nullopt;
    return reduced * right;
}

struct note_slot_t {
    std::uint64_t numerator{};
    std::uint64_t denominator{1};
    std::string object_id;
    std::string path;
};

struct channel_line_t {
    std::size_t measure{};
    std::string channel;
    std::vector<note_slot_t> notes;
};

struct source_measure_line_t {
    std::size_t source_order{};
    std::size_t measure_index{};
    std::string text;
};

struct branch_tree_analysis_t {
    bool has_branches{};
    bool has_branch_timing{};
    bool has_unmodeled_root_statement{};
    std::set<std::size_t> branch_source_orders;
    std::set<std::size_t> root_source_orders;
    std::set<std::size_t> retained_timing_source_orders;
    std::set<std::size_t> branch_measure_indices;
    std::set<std::pair<std::string, std::string>> branch_resource_ids;
    std::map<std::size_t, std::pair<std::string, std::string>> metadata_fields;
    std::map<std::size_t, std::pair<std::string, std::string>> branch_metadata_fields;
    std::map<std::size_t, std::pair<std::string, std::string>> branch_resource_fields;
    std::map<std::size_t, std::string> branch_resource_source_lines;
    std::vector<source_measure_line_t> root_measure_ratio_lines;
    std::vector<std::pair<std::size_t, std::string>> root_timing_lines;
    std::vector<std::pair<std::size_t, std::string>> preserved_lines;
    std::optional<std::size_t> branch_source_begin;
    std::optional<std::size_t> branch_source_end;
};

auto replace_retained_header_value(std::string_view line, std::string_view expected_token,
                                   std::string_view expected_value, std::string_view replacement)
    -> std::optional<std::string> {
    if (line.empty() || line.front() != '#') {
        return std::nullopt;
    }
    const auto separator = line.find_first_of(" \t:", 1);
    if (separator == std::string_view::npos ||
        ascii_upper(std::string(line.substr(1, separator - 1))) != expected_token ||
        line.substr(separator + 1) != expected_value || has_line_break_or_nul(replacement) ||
        !valid_utf8(replacement)) {
        return std::nullopt;
    }
    std::string result(line.substr(0, separator + 1));
    result.append(replacement);
    return result;
}

auto is_branch_boundary(bms_control_kind_t kind) -> bool {
    return kind == bms_control_kind_t::random || kind == bms_control_kind_t::switch_block ||
           kind == bms_control_kind_t::if_group || kind == bms_control_kind_t::if_branch ||
           kind == bms_control_kind_t::else_if_branch || kind == bms_control_kind_t::else_branch ||
           kind == bms_control_kind_t::case_branch || kind == bms_control_kind_t::default_branch;
}

auto is_measure_length_command(std::string_view token) -> std::optional<std::size_t> {
    if (token.size() != 5 || token.substr(3) != "02" ||
        !std::ranges::all_of(token.substr(0, 3),
                             [](char character) { return character >= '0' && character <= '9'; })) {
        return std::nullopt;
    }
    return static_cast<std::size_t>((token[0] - '0') * 100 + (token[1] - '0') * 10 +
                                    (token[2] - '0'));
}

auto is_timing_command(std::string_view token) -> bool {
    if (token == "BPM" ||
        (token.size() == 5 && token.starts_with("BPM") && base36_digit(token[3]) &&
         base36_digit(token[4])) ||
        (token.size() == 6 && token.starts_with("STOP") && base36_digit(token[4]) &&
         base36_digit(token[5]))) {
        return true;
    }
    return token.size() == 5 && token[0] >= '0' && token[0] <= '9' && token[1] >= '0' &&
           token[1] <= '9' && token[2] >= '0' && token[2] <= '9' &&
           (token.substr(3) == "03" || token.substr(3) == "08" || token.substr(3) == "09");
}

auto analyze_branch_tree(const bms_control_node_t& root) -> branch_tree_analysis_t {
    branch_tree_analysis_t result;
    const auto visit = [&](const auto& self, const bms_control_node_t& node,
                           bool inside_branch) -> void {
        if (node.kind == bms_control_kind_t::random || node.kind == bms_control_kind_t::if_group ||
            node.kind == bms_control_kind_t::if_branch ||
            node.kind == bms_control_kind_t::else_if_branch ||
            node.kind == bms_control_kind_t::else_branch ||
            node.kind == bms_control_kind_t::switch_block ||
            node.kind == bms_control_kind_t::case_branch ||
            node.kind == bms_control_kind_t::default_branch) {
            result.has_branches = true;
        }
        const bool node_is_branch_scoped = inside_branch;
        const bool is_synthetic_group =
            node.kind == bms_control_kind_t::root || node.kind == bms_control_kind_t::if_group;
        const bool is_control_statement = node.kind != bms_control_kind_t::root &&
                                          node.kind != bms_control_kind_t::if_group &&
                                          node.kind != bms_control_kind_t::raw_statement;
        if (!is_synthetic_group && node.source_order < std::numeric_limits<std::size_t>::max()) {
            if (node_is_branch_scoped) {
                result.branch_source_orders.insert(node.source_order);
            }
            if (is_control_statement && !node.original_text.empty()) {
                result.preserved_lines.emplace_back(node.source_order, node.original_text);
            } else if (node.kind == bms_control_kind_t::raw_statement &&
                       !node.original_text.empty()) {
                if (node_is_branch_scoped) {
                    result.branch_source_orders.insert(node.source_order);
                    const auto token = ascii_upper(node.command_token);
                    if (safe_metadata_token(token) &&
                        (!generated_or_channel_token(token) || token == "PLAYER")) {
                        result.metadata_fields.emplace(node.source_order,
                                                       std::pair{token, node.raw_value});
                        result.branch_metadata_fields.emplace(node.source_order,
                                                              std::pair{token, node.raw_value});
                    }
                    if (const auto measure = is_measure_length_command(token);
                        measure.has_value()) {
                        result.branch_measure_indices.insert(*measure);
                    }
                    if (is_timing_command(token)) {
                        result.has_branch_timing = true;
                        result.retained_timing_source_orders.insert(node.source_order);
                    }
                    if ((token.starts_with("WAV") || token.starts_with("BMP")) &&
                        token.size() == 5 && base36_digit(token[3]) && base36_digit(token[4])) {
                        result.branch_resource_ids.emplace(token.substr(0, 3), token.substr(3, 2));
                        result.branch_resource_fields.emplace(node.source_order,
                                                              std::pair{token, node.raw_value});
                        result.branch_resource_source_lines.emplace(node.source_order,
                                                                    node.original_text);
                    }
                    result.preserved_lines.emplace_back(node.source_order, node.original_text);
                } else {
                    const auto token = ascii_upper(node.command_token);
                    if (safe_metadata_token(token)) {
                        result.metadata_fields.emplace(node.source_order,
                                                       std::pair{token, node.raw_value});
                    }
                    if (const auto measure = is_measure_length_command(token);
                        measure.has_value()) {
                        result.root_measure_ratio_lines.push_back(
                            {node.source_order, *measure, node.original_text});
                    }
                    if (is_timing_command(token)) {
                        result.root_timing_lines.emplace_back(node.source_order,
                                                              node.original_text);
                    } else if (safe_metadata_token(token) || generated_or_channel_token(token) ||
                               is_measure_length_command(token).has_value()) {
                        result.root_source_orders.insert(node.source_order);
                    }
                    if (!safe_metadata_token(token) && !generated_or_channel_token(token)) {
                        result.has_unmodeled_root_statement = true;
                    }
                }
            }
        }
        if (node.close_text.has_value() && node.close_source_order.has_value()) {
            result.preserved_lines.emplace_back(*node.close_source_order, *node.close_text);
        }

        const bool descendants_are_branch_scoped =
            node_is_branch_scoped || is_branch_boundary(node.kind);
        for (const auto& child : node.children) {
            self(self, child, descendants_are_branch_scoped);
        }
    };
    visit(visit, root, false);
    if (!result.preserved_lines.empty()) {
        const auto [first, last] = std::minmax_element(
            result.preserved_lines.begin(), result.preserved_lines.end(),
            [](const auto& left, const auto& right) { return left.first < right.first; });
        result.branch_source_begin = first->first;
        result.branch_source_end = last->first;
    }
    if (result.has_branches && result.has_branch_timing) {
        result.preserved_lines.insert(result.preserved_lines.end(),
                                      result.root_timing_lines.begin(),
                                      result.root_timing_lines.end());
        for (const auto& [source_order, line] : result.root_timing_lines) {
            (void)line;
            result.retained_timing_source_orders.insert(source_order);
        }
    } else {
        result.retained_timing_source_orders.clear();
    }
    std::ranges::stable_sort(result.preserved_lines, {},
                             [](const auto& line) { return line.first; });
    return result;
}

auto has_bms_source_provenance(const source_provenance_t& provenance) -> bool {
    return provenance.family == format_family_t::bms && !provenance.source_spelling.empty();
}

auto fraction_at(const measure_index_t& index, local_position_t position, std::string path)
    -> std::pair<std::uint64_t, std::uint64_t> {
    const auto entry =
        std::ranges::find(index.entries, position.measure, &measure_index_entry_t::id);
    if (entry == index.entries.end() || position.row >= entry->row_capacity) {
        fail(bms_write_diagnostic_code_t::invalid_geometry,
             "event position is outside the chart's measure geometry", std::move(path));
    }
    const auto divisor = std::gcd(position.row, entry->row_capacity);
    return {position.row / divisor, entry->row_capacity / divisor};
}

auto layout_for_export(const chart_document_t& chart, const bms_write_options_t& options,
                       std::vector<bms_write_diagnostic_t>& diagnostics) -> lane_layout_t {
    if (options.lane_profile_id.has_value()) {
        const auto detection = detect_bms_lane_profile({
            .explicit_profile_id = options.lane_profile_id,
        });
        if (!detection.selected_profile.has_value()) {
            fail(bms_write_diagnostic_code_t::missing_profile,
                 "requested BMS lane profile is unknown", "$.charts[].lane_layouts");
        }
        return detection.selected_profile->layout;
    }
    if (chart.lane_layouts.empty()) {
        fail(bms_write_diagnostic_code_t::missing_profile,
             "normalized BMS export requires an authored lane map or an explicit profile",
             "$.charts[].lane_layouts");
    }
    if (chart.lane_layouts.size() != 1 || chart.measures.empty() ||
        chart.lane_layouts.front().position.measure != chart.measures.front().id ||
        chart.lane_layouts.front().position.row != 0) {
        fail(bms_write_diagnostic_code_t::unsupported_feature,
             "BMS lane-layout changes are not supported by this writer slice",
             "$.charts[].lane_layouts");
    }
    (void)diagnostics;
    return chart.lane_layouts.front().layout;
}

auto normalized_write(const song_document_t& song, const chart_document_t& chart,
                      const bms_write_options_t& options,
                      std::vector<bms_write_diagnostic_t>& diagnostics) -> std::string {
    auto export_scope = song;
    export_scope.charts.clear();
    export_scope.charts.push_back(chart);
    export_scope.output_groups.clear();
    const auto validation = validate_document(export_scope);
    const auto validation_error = std::ranges::find_if(
        validation, [](const auto& item) { return item.severity == diagnostic_severity_t::error; });
    if (validation_error != validation.end()) {
        fail(bms_write_diagnostic_code_t::invalid_document,
             "document validation failed before BMS export: " + validation_error->message, "$");
    }
    if (chart.measures.size() > 1000) {
        fail(bms_write_diagnostic_code_t::resource_limit,
             "BMS measure indices are limited to 000 through 999", "$.charts[].measures");
    }

    const auto retained_branch_tree = chart.bms_control_tree.has_value()
                                          ? analyze_branch_tree(*chart.bms_control_tree)
                                          : branch_tree_analysis_t{};
    if (retained_branch_tree.has_branches &&
        options.branch_policy == bms_branch_policy_t::flatten_active &&
        chart.active_branch_selection.empty()) {
        fail(bms_write_diagnostic_code_t::unsupported_branch_policy,
             "active-projection flattening requires explicit or fixed choices for the retained "
             "branch tree",
             "$.charts[].active_branch_selection");
    }
    if (retained_branch_tree.has_branches &&
        options.branch_policy == bms_branch_policy_t::flatten_active) {
        warn(diagnostics, bms_write_diagnostic_code_t::lossy_transform,
             "the explicitly selected active BMS projection is flattened and inactive branches "
             "are omitted",
             "$.charts[].bms_control_tree");
    }
    const auto branch_tree = options.branch_policy == bms_branch_policy_t::flatten_active
                                 ? branch_tree_analysis_t{}
                                 : retained_branch_tree;
    if (!chart.active_branch_selection.empty() && !retained_branch_tree.has_branches) {
        fail(bms_write_diagnostic_code_t::branch_provenance,
             "active branch selections have no retained source tree to preserve",
             "$.charts[].active_branch_selection");
    }
    for (const auto measure_index : branch_tree.branch_measure_indices) {
        if (measure_index >= chart.measures.size()) {
            continue;
        }
        const auto projection =
            std::ranges::find(chart.bms_measure_length_projections, measure_index,
                              &bms_measure_length_projection_t::measure_index);
        if (projection == chart.bms_measure_length_projections.end() ||
            chart.measures[measure_index].definition.length != projection->imported_length) {
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "a branch-controlled measure length is edited or lacks an import baseline; it "
                 "cannot be written without branch-body editing",
                 "$.charts[].measures[" + std::to_string(measure_index) + "].definition.length");
        }
    }
    if (branch_tree.has_branches) {
        warn(diagnostics, bms_write_diagnostic_code_t::lossy_transform,
             "branch events, ratios, and timing remain source-backed; direct edits to those "
             "branch-projected objects are not applied by this writer slice",
             "$.charts[].bms_control_tree");
    }
    if (branch_tree.has_unmodeled_root_statement) {
        warn(diagnostics, bms_write_diagnostic_code_t::lossy_transform,
             "unmodeled root-level raw source lines were omitted from normalized BMS output",
             "$.charts[].bms_control_tree");
    }
    const auto is_branch_provenance = [&](const source_provenance_t& provenance) {
        return has_bms_source_provenance(provenance) &&
               branch_tree.branch_source_orders.contains(provenance.source_order);
    };
    const auto is_retained_timing_provenance = [&](const source_provenance_t& provenance) {
        return has_bms_source_provenance(provenance) && branch_tree.has_branches &&
               branch_tree.retained_timing_source_orders.contains(provenance.source_order);
    };
    const auto is_branch_metadata_field = [&](const format_metadata_t& group,
                                              std::size_t field_index) {
        if (group.family != format_family_t::bms ||
            field_index >= group.ordered_field_source_orders.size()) {
            return false;
        }
        return branch_tree.branch_source_orders.contains(
            group.ordered_field_source_orders[field_index]);
    };

    for (const auto& bpm : chart.bpms) {
        if (!is_retained_timing_provenance(bpm.provenance)) {
            continue;
        }
        const auto projection =
            std::ranges::find(chart.bms_bpm_projections, bpm.id, &bms_bpm_projection_t::event);
        if (projection == chart.bms_bpm_projections.end() ||
            projection->source_order != bpm.provenance.source_order ||
            projection->imported_position != bpm.position ||
            projection->imported_beats_per_minute != bpm.beats_per_minute) {
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "source-backed BMS BPM timing was edited or lacks its import baseline; direct "
                 "branch timing editing is not implemented",
                 "$.charts[].bpms");
        }
    }
    for (const auto& pause : chart.pauses) {
        if (!is_retained_timing_provenance(pause.provenance)) {
            continue;
        }
        const auto projection = std::ranges::find(chart.bms_pause_projections, pause.id,
                                                  &bms_pause_projection_t::event);
        if (projection == chart.bms_pause_projections.end() ||
            projection->source_order != pause.provenance.source_order ||
            projection->imported_position != pause.position ||
            projection->imported_kind != pause.kind ||
            projection->imported_duration != pause.duration ||
            projection->imported_source_unit != pause.source_unit) {
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "source-backed BMS pause timing was edited or lacks its import baseline; direct "
                 "branch timing editing is not implemented",
                 "$.charts[].pauses");
        }
    }

    const auto has_unsupported_root_timing = [&](const auto& events) {
        return std::ranges::any_of(events, [&](const auto& event) {
            return !is_branch_provenance(event.provenance) &&
                   !is_retained_timing_provenance(event.provenance);
        });
    };
    if (has_unsupported_root_timing(chart.pauses) || has_unsupported_root_timing(chart.warps) ||
        has_unsupported_root_timing(chart.scrolls) || has_unsupported_root_timing(chart.speeds) ||
        has_unsupported_root_timing(chart.kiai) || !chart.timing_anchors.empty() ||
        !chart.sections.empty() || !chart.sound_channels.empty()) {
        fail(bms_write_diagnostic_code_t::unsupported_feature,
             "chart contains timing or media features outside the normalized writer slice",
             "$.charts[]");
    }
    if (!chart.bookmarks.empty()) {
        warn(diagnostics, bms_write_diagnostic_code_t::lossy_transform,
             "BMS has no supported bookmark representation; bookmarks are omitted",
             "$.charts[].bookmarks");
    }
    if (!chart.semantic_layers.empty() ||
        std::ranges::any_of(chart.measures, [](const auto& measure) {
            return std::ranges::any_of(
                measure.notes, [](const auto& note) { return !note.semantic_layers.empty(); });
        })) {
        warn(diagnostics, bms_write_diagnostic_code_t::lossy_transform,
             "semantic note layers are omitted while their underlying note types are retained",
             "$.charts[].semantic_layers");
    }
    if (chart.metadata.rating.has_value()) {
        warn(diagnostics, bms_write_diagnostic_code_t::lossy_transform,
             "chart rating is omitted because the current BMS reader does not materialize it",
             "$.charts[].metadata.rating");
    }

    const auto title = song.metadata.title.empty() ? chart.metadata.name : song.metadata.title;
    const auto artist = song.metadata.artist.empty() ? chart.metadata.author : song.metadata.artist;
    if (has_line_break_or_nul(title) || has_line_break_or_nul(artist) ||
        has_edge_whitespace(title) || has_edge_whitespace(artist) || !valid_utf8(title) ||
        !valid_utf8(artist)) {
        fail(bms_write_diagnostic_code_t::invalid_document,
             "BMS title and artist must be valid UTF-8 single-line values without edge whitespace",
             "$.metadata");
    }

    const auto layout = layout_for_export(chart, options, diagnostics);
    std::map<std::uint64_t, std::string> lane_channels;
    std::map<std::uint64_t, std::string> long_note_channels;
    std::set<std::string> used_channels;
    for (const auto& lane : layout.lanes) {
        if (lane.bms_channels.visible.has_value()) {
            auto channel = *lane.bms_channels.visible;
            for (auto& character : channel) {
                if (character >= 'a' && character <= 'z') {
                    character = static_cast<char>(character - 'a' + 'A');
                }
            }
            if (channel.size() != 2 || !base36_digit(channel[0]) || !base36_digit(channel[1]) ||
                !used_channels.insert(channel).second) {
                fail(bms_write_diagnostic_code_t::invalid_document,
                     "lane profile has an invalid or duplicate visible BMS channel",
                     "$.charts[].lane_layouts");
            }
            lane_channels.emplace(lane.id.value, std::move(channel));
        }
        if (lane.bms_channels.long_note.has_value()) {
            auto channel = *lane.bms_channels.long_note;
            for (auto& character : channel) {
                if (character >= 'a' && character <= 'z') {
                    character = static_cast<char>(character - 'a' + 'A');
                }
            }
            if (channel.size() != 2 || !base36_digit(channel[0]) || !base36_digit(channel[1]) ||
                (channel[0] != '5' && channel[0] != '6') || !used_channels.insert(channel).second) {
                fail(bms_write_diagnostic_code_t::invalid_document,
                     "lane profile has an invalid or duplicate long-note BMS channel",
                     "$.charts[].lane_layouts");
            }
            long_note_channels.emplace(lane.id.value, std::move(channel));
        }
    }

    std::map<std::uint64_t, std::string> audio_ids;
    std::map<std::uint64_t, std::string> visual_ids;
    std::map<std::string, std::size_t> next_ids{{"WAV", 1}, {"BMP", 1}};
    std::map<std::string, std::set<std::string>> assigned_ids;
    std::set<std::uint64_t> branch_resource_ids;
    std::map<std::size_t, std::string> branch_resource_edits;
    struct resource_declaration_t {
        std::string name_space;
        const resource_entry_t* resource{};
        bool branch_scoped{};
    };
    std::vector<resource_declaration_t> declarations;
    declarations.reserve(song.resources.size());
    for (const auto& resource : song.resources) {
        const auto audio = std::ranges::find(song.audio_assets, resource.asset, &audio_asset_t::id);
        const auto visual =
            std::ranges::find(song.visual_assets, resource.asset, &visual_asset_t::id);
        const bool is_audio = audio != song.audio_assets.end();
        const bool is_visual = visual != song.visual_assets.end();
        if (is_audio == is_visual || resource.missing) {
            fail(bms_write_diagnostic_code_t::unresolved_resource,
                 "resource must refer to exactly one available audio or visual asset",
                 "$.resources[]");
        }
        const auto name_space = is_audio ? std::string("WAV") : std::string("BMP");
        if (resource.name.empty() || has_line_break_or_nul(resource.name) ||
            has_edge_whitespace(resource.name) || !valid_utf8(resource.name)) {
            fail(bms_write_diagnostic_code_t::unresolved_resource,
                 "resource path must be a valid UTF-8 single-line value without edge whitespace",
                 "$.resources[].name");
        }
        const bool branch_scoped = is_branch_provenance(resource.provenance);
        auto identifier = bms_identity(resource, name_space);
        if (branch_scoped) {
            const auto retained =
                branch_tree.branch_resource_fields.find(resource.provenance.source_order);
            const auto retained_line =
                branch_tree.branch_resource_source_lines.find(resource.provenance.source_order);
            const auto expected_token =
                identifier.has_value() ? name_space + *identifier : std::string{};
            if (!identifier.has_value() || retained == branch_tree.branch_resource_fields.end() ||
                retained_line == branch_tree.branch_resource_source_lines.end() ||
                retained->second.first != expected_token ||
                ascii_upper(resource.provenance.source_field) != expected_token ||
                resource.provenance.source_spelling != retained_line->second ||
                !branch_tree.branch_resource_ids.contains({name_space, *identifier}) ||
                assigned_ids[name_space].contains(*identifier)) {
                fail(bms_write_diagnostic_code_t::branch_provenance,
                     "branch resources must retain one matching source statement, namespace, and "
                     "identifier",
                     "$.resources[].target_identities");
            }
            if (resource.name != trim_ascii_whitespace(retained->second.second)) {
                branch_resource_edits.emplace(resource.provenance.source_order, resource.name);
            }
        } else if (!identifier.has_value() || assigned_ids[name_space].contains(*identifier) ||
                   branch_tree.branch_resource_ids.contains({name_space, *identifier})) {
            const auto old = identifier;
            auto& next = next_ids[name_space];
            while (next < 1296 &&
                   (assigned_ids[name_space].contains(base36_id(next)) ||
                    branch_tree.branch_resource_ids.contains({name_space, base36_id(next)}))) {
                ++next;
            }
            if (next >= 1296) {
                fail(bms_write_diagnostic_code_t::resource_limit,
                     "BMS resource namespace exceeds 1295 nonzero two-digit identifiers",
                     "$.resources");
            }
            identifier = base36_id(next++);
            if (old.has_value()) {
                warn(diagnostics, bms_write_diagnostic_code_t::lossy_transform,
                     "colliding BMS resource identifier was reassigned deterministically",
                     "$.resources[].target_identities");
            }
        }
        assigned_ids[name_space].insert(*identifier);
        if (is_audio)
            audio_ids.emplace(resource.id.value, *identifier);
        if (is_visual)
            visual_ids.emplace(resource.id.value, *identifier);
        if (branch_scoped)
            branch_resource_ids.insert(resource.id.value);
        declarations.push_back({name_space, &resource, branch_scoped});
    }

    const auto measure_index_result = build_measure_index(chart);
    if (!std::holds_alternative<measure_index_t>(measure_index_result)) {
        fail(bms_write_diagnostic_code_t::invalid_geometry,
             "chart measure geometry cannot be represented safely", "$.charts[].measures");
    }
    const auto& index = std::get<measure_index_t>(measure_index_result);

    std::map<std::pair<std::size_t, std::string>, channel_line_t> channel_lines;
    std::map<std::pair<std::size_t, std::uint64_t>, channel_line_t> bgm_lines;
    std::map<std::pair<std::size_t, std::string>, std::size_t> bga_line_order;
    struct hold_endpoint_t {
        std::uint64_t row{};
        std::uint64_t note_id{};
        bool is_end{};
        std::string path;
    };
    std::map<std::uint64_t, std::vector<hold_endpoint_t>> hold_endpoints;
    for (std::size_t measure = 0; measure < chart.measures.size(); ++measure) {
        for (const auto& note : chart.measures[measure].notes) {
            const auto path = "$.charts[].measures[" + std::to_string(measure) + "].notes[]";
            if (is_branch_provenance(note.provenance))
                continue;
            if ((note.kind != note_kind_t::tap && note.kind != note_kind_t::hold) ||
                (note.kind == note_kind_t::tap && note.end.has_value())) {
                fail(bms_write_diagnostic_code_t::unsupported_feature,
                     "normalized BMS writer supports playable taps and holds only", path);
            }
            if (note.branch.has_value()) {
                fail(bms_write_diagnostic_code_t::unsupported_feature,
                     "branch-local notes require an explicit branch export policy", path);
            }
            const auto& selected_channels =
                note.kind == note_kind_t::hold ? long_note_channels : lane_channels;
            const auto channel = selected_channels.find(note.lane.value);
            if (channel == selected_channels.end()) {
                fail(
                    bms_write_diagnostic_code_t::missing_profile,
                    note.kind == note_kind_t::hold
                        ? "hold lane has no authored long-note BMS channel in the selected lane map"
                        : "note lane has no visible BMS channel in the selected lane map",
                    path);
            }
            if (!note.sound.has_value()) {
                fail(bms_write_diagnostic_code_t::unresolved_resource,
                     "BMS playable taps require a WAV resource binding", path + ".sound");
            }
            if (branch_resource_ids.contains(note.sound->value)) {
                fail(bms_write_diagnostic_code_t::branch_provenance,
                     "a root note refers to a resource declared only inside a branch",
                     path + ".sound");
            }
            const auto wav = audio_ids.find(note.sound->value);
            if (wav == audio_ids.end()) {
                fail(bms_write_diagnostic_code_t::unresolved_resource,
                     "tap refers to a resource that is not exported as WAV", path + ".sound");
            }
            auto [numerator, denominator] = fraction_at(index, note.position, path + ".position");
            auto key = std::pair{measure, channel->second};
            auto [line, inserted] = channel_lines.try_emplace(key, channel_line_t{
                                                                       .measure = measure,
                                                                       .channel = channel->second,
                                                                   });
            (void)inserted;
            line->second.notes.push_back({numerator, denominator, wav->second, path});

            const auto absolute_start = absolute_row(index, note.position);
            if (!absolute_start.has_value()) {
                fail(bms_write_diagnostic_code_t::invalid_geometry,
                     "hold endpoint is outside the chart's measure geometry", path);
            }
            if (note.kind == note_kind_t::hold) {
                if (!note.end.has_value()) {
                    fail(bms_write_diagnostic_code_t::invalid_document,
                         "hold note has no tail position", path + ".end");
                }
                const auto [end_numerator, end_denominator] =
                    fraction_at(index, *note.end, path + ".end");
                const auto end_measure =
                    std::ranges::find(chart.measures, note.end->measure, &measure_chunk_t::id);
                const auto end_measure_index =
                    static_cast<std::size_t>(std::distance(chart.measures.begin(), end_measure));
                auto [end_line, end_inserted] = channel_lines.try_emplace(
                    std::pair{end_measure_index, channel->second},
                    channel_line_t{.measure = end_measure_index, .channel = channel->second});
                (void)end_inserted;
                end_line->second.notes.push_back(
                    {end_numerator, end_denominator, wav->second, path + ".end"});
                const auto absolute_end = absolute_row(index, *note.end);
                if (!absolute_end.has_value()) {
                    fail(bms_write_diagnostic_code_t::invalid_geometry,
                         "hold tail is outside the chart's measure geometry", path + ".end");
                }
                hold_endpoints[note.lane.value].push_back(
                    {*absolute_start, note.id.value, false, path});
                hold_endpoints[note.lane.value].push_back(
                    {*absolute_end, note.id.value, true, path + ".end"});
            }
        }

        for (const auto& event : chart.measures[measure].bgm_events) {
            const auto path = "$.charts[].measures[" + std::to_string(measure) + "].bgm_events[]";
            if (is_branch_provenance(event.provenance))
                continue;
            if (event.branch.has_value()) {
                fail(bms_write_diagnostic_code_t::unsupported_feature,
                     "branch-local BGM events require an explicit branch export policy", path);
            }
            const auto source_lane = std::ranges::find(chart.bgm_source_lanes, event.source_lane,
                                                       &bgm_source_lane_t::id);
            if (source_lane == chart.bgm_source_lanes.end()) {
                fail(bms_write_diagnostic_code_t::invalid_document,
                     "BGM event refers to an unknown source lane", path + ".source_lane");
            }
            std::string object_id = "00";
            if (event.sound.has_value()) {
                if (branch_resource_ids.contains(event.sound->value)) {
                    fail(bms_write_diagnostic_code_t::branch_provenance,
                         "a root BGM event refers to a resource declared only inside a branch",
                         path + ".sound");
                }
                const auto wav = audio_ids.find(event.sound->value);
                if (wav == audio_ids.end()) {
                    fail(bms_write_diagnostic_code_t::unresolved_resource,
                         "BGM event refers to a resource that is not exported as WAV",
                         path + ".sound");
                }
                object_id = wav->second;
            }
            const auto [numerator, denominator] =
                fraction_at(index, event.position, path + ".position");
            const auto key = std::pair{measure, event.source_lane.value};
            auto [line, inserted] =
                bgm_lines.try_emplace(key, channel_line_t{.measure = measure, .channel = "01"});
            (void)inserted;
            line->second.notes.push_back({numerator, denominator, std::move(object_id), path});
        }
    }

    for (auto& [lane, endpoints] : hold_endpoints) {
        (void)lane;
        std::ranges::sort(endpoints, {}, &hold_endpoint_t::row);
        for (std::size_t endpoint = 0; endpoint < endpoints.size(); endpoint += 2) {
            if (endpoint + 1 >= endpoints.size() || endpoints[endpoint].is_end ||
                !endpoints[endpoint + 1].is_end ||
                endpoints[endpoint].note_id != endpoints[endpoint + 1].note_id) {
                fail(bms_write_diagnostic_code_t::unsupported_feature,
                     "overlapping holds on one lane cannot be paired safely by BMS long-note "
                     "channels",
                     endpoint < endpoints.size() ? endpoints[endpoint].path
                                                 : "$.charts[].measures[].notes[]");
            }
        }
    }

    for (const auto& source_lane : chart.bgm_source_lanes) {
        if (branch_tree.branch_source_orders.contains(source_lane.source_order))
            continue;
        if (std::ranges::none_of(bgm_lines, [&](const auto& entry) {
                return entry.first.second == source_lane.id.value;
            })) {
            warn(diagnostics, bms_write_diagnostic_code_t::lossy_transform,
                 "empty editor BGM source lanes have no BMS representation and are omitted",
                 "$.charts[].bgm_source_lanes");
        }
    }

    std::map<std::pair<std::size_t, std::uint64_t>, std::size_t> layer_counts;
    auto sorted_bga_events = chart.bga_events;
    std::ranges::stable_sort(sorted_bga_events, [](const auto& left, const auto& right) {
        return std::tie(left.position.measure.value, left.position.row,
                        left.provenance.source_order,
                        left.id.value) < std::tie(right.position.measure.value, right.position.row,
                                                  right.provenance.source_order, right.id.value);
    });
    for (const auto& event : sorted_bga_events) {
        if (is_branch_provenance(event.provenance))
            continue;
        const std::string path = "$.charts[].bga_events[]";
        std::string channel;
        switch (event.kind) {
        case visual_event_kind_t::background:
            channel = "04";
            break;
        case visual_event_kind_t::poor:
            channel = "06";
            break;
        case visual_event_kind_t::layer: {
            const auto absolute = absolute_row(index, event.position);
            if (!absolute.has_value()) {
                fail(bms_write_diagnostic_code_t::invalid_geometry,
                     "BGA event is outside the chart's measure geometry", path + ".position");
            }
            auto& count = layer_counts[{event.position.measure.value, *absolute}];
            if (count >= 2) {
                fail(bms_write_diagnostic_code_t::duplicate_event,
                     "more than two layer events share a position; BMS channels 07 and 0A cannot "
                     "represent the collision",
                     path);
            }
            channel = count++ == 0 ? "07" : "0A";
            break;
        }
        case visual_event_kind_t::video:
        case visual_event_kind_t::storyboard:
            fail(bms_write_diagnostic_code_t::unsupported_feature,
                 "BMS BGA channels cannot represent video or storyboard events", path);
        }
        const auto bmp = visual_ids.find(event.resource.value);
        if (branch_resource_ids.contains(event.resource.value)) {
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "a root BGA event refers to a resource declared only inside a branch",
                 path + ".resource");
        }
        if (bmp == visual_ids.end()) {
            fail(bms_write_diagnostic_code_t::unresolved_resource,
                 "BGA event refers to a resource that is not exported as BMP", path + ".resource");
        }
        const auto [numerator, denominator] =
            fraction_at(index, event.position, path + ".position");
        const auto key = std::pair{
            static_cast<std::size_t>(std::distance(
                chart.measures.begin(),
                std::ranges::find(chart.measures, event.position.measure, &measure_chunk_t::id))),
            channel};
        auto [line, inserted] = channel_lines.try_emplace(
            key, channel_line_t{.measure = key.first, .channel = channel});
        (void)inserted;
        line->second.notes.push_back({numerator, denominator, bmp->second, path});
        auto [order, order_inserted] =
            bga_line_order.try_emplace(key, event.provenance.source_order);
        if (!order_inserted)
            order->second = std::min(order->second, event.provenance.source_order);
    }

    const auto player_value =
        layout.profile_id.starts_with("pms-") ? std::string("3")
        : (layout.split_stage ||
           std::ranges::any_of(layout.lanes,
                               [](const auto& lane) { return lane.stage == stage_group_t::right; }))
            ? std::string("2")
            : std::string("1");
    std::vector<const format_metadata_t*> bms_metadata_groups;
    const auto append_bms_groups = [&](const auto& groups) {
        for (const auto& group : groups) {
            if (group.family == format_family_t::bms && !group.ordered_fields.empty()) {
                bms_metadata_groups.push_back(&group);
            }
        }
    };
    append_bms_groups(song.metadata.format_groups);
    append_bms_groups(chart.metadata.format_groups);
    bool has_branch_player_field = false;
    struct effective_metadata_source_t {
        std::size_t source_order{};
        bool branch_scoped{};
    };
    std::optional<effective_metadata_source_t> last_title_source;
    std::optional<effective_metadata_source_t> last_artist_source;
    std::map<std::size_t, std::string> branch_metadata_edits;
    std::set<std::size_t> metadata_source_orders;
    for (const auto* group : bms_metadata_groups) {
        std::optional<std::size_t> previous_source_order;
        for (std::size_t index = 0; index < group->ordered_fields.size(); ++index) {
            const auto& [token, value] = group->ordered_fields[index];
            if (!safe_metadata_token(token)) {
                fail(bms_write_diagnostic_code_t::invalid_document,
                     "retained BMS metadata contains an unsafe command token",
                     "$.metadata.format_groups[].ordered_fields[]");
            }
            if (index >= group->ordered_field_source_orders.size()) {
                continue;
            }
            const auto source_order = group->ordered_field_source_orders[index];
            const auto source_field = retained_branch_tree.metadata_fields.find(source_order);
            if ((previous_source_order.has_value() && source_order <= *previous_source_order) ||
                !metadata_source_orders.insert(source_order).second ||
                source_field == retained_branch_tree.metadata_fields.end() ||
                ascii_upper(token) != source_field->second.first) {
                fail(bms_write_diagnostic_code_t::branch_provenance,
                     "retained BMS metadata source order or command token does not identify one "
                     "unique source statement",
                     "$.metadata.format_groups[].ordered_fields[]");
            }
            previous_source_order = source_order;
            const bool branch_scoped = is_branch_metadata_field(*group, index);
            const auto normalized_token = ascii_upper(token);
            if (branch_scoped) {
                const auto source_field = branch_tree.branch_metadata_fields.find(source_order);
                if (source_field == branch_tree.branch_metadata_fields.end() ||
                    normalized_token != source_field->second.first) {
                    fail(bms_write_diagnostic_code_t::branch_provenance,
                         "branch metadata source identity does not match its retained source "
                         "statement",
                         "$.metadata.format_groups[].ordered_fields[]");
                }
                if (has_line_break_or_nul(value) || !valid_utf8(value)) {
                    fail(bms_write_diagnostic_code_t::branch_provenance,
                         "edited branch metadata must be valid UTF-8 single-line text",
                         "$.metadata.format_groups[].ordered_fields[]");
                }
                if (value != source_field->second.second) {
                    branch_metadata_edits.insert_or_assign(source_order, value);
                }
            }
            if (normalized_token == "PLAYER") {
                has_branch_player_field = has_branch_player_field || branch_scoped;
            } else if (normalized_token == "TITLE" &&
                       (!last_title_source || source_order >= last_title_source->source_order)) {
                last_title_source = {source_order, branch_scoped};
            } else if (normalized_token == "ARTIST" &&
                       (!last_artist_source || source_order >= last_artist_source->source_order)) {
                last_artist_source = {source_order, branch_scoped};
            }
        }
    }

    const auto route_projected_branch_metadata =
        [&](std::string_view token, const std::optional<effective_metadata_source_t>& source,
            std::string_view projected_value) {
            if (!source.has_value() || !source->branch_scoped) {
                return false;
            }
            const auto retained = branch_tree.branch_metadata_fields.find(source->source_order);
            if (retained == branch_tree.branch_metadata_fields.end() ||
                retained->second.first != token) {
                fail(bms_write_diagnostic_code_t::branch_provenance,
                     "projected branch metadata has no unique retained source statement",
                     "$.metadata");
            }
            const auto original_value = trim_ascii_whitespace(retained->second.second);
            if (projected_value != original_value) {
                const auto edit = branch_metadata_edits.find(source->source_order);
                if (edit != branch_metadata_edits.end() &&
                    trim_ascii_whitespace(edit->second) != projected_value) {
                    fail(bms_write_diagnostic_code_t::branch_provenance,
                         "normalized and source-field edits disagree for the same branch metadata "
                         "statement",
                         "$.metadata");
                }
                if (edit == branch_metadata_edits.end()) {
                    branch_metadata_edits.emplace(source->source_order,
                                                  std::string(projected_value));
                }
            }
            return true;
        };
    const bool title_is_branch_projection =
        route_projected_branch_metadata("TITLE", last_title_source, title);
    const bool artist_is_branch_projection =
        route_projected_branch_metadata("ARTIST", last_artist_source, artist);

    const auto has_player_field = std::ranges::any_of(bms_metadata_groups, [&](const auto* group) {
        for (std::size_t index = 0; index < group->ordered_fields.size(); ++index) {
            if (!is_branch_metadata_field(*group, index) &&
                ascii_upper(group->ordered_fields[index].first) == "PLAYER") {
                return true;
            }
        }
        return false;
    });
    std::vector<std::string> output;
    if (!has_player_field && !has_branch_player_field)
        output.emplace_back("#PLAYER " + player_value);
    bool player_emitted = false;
    bool title_emitted = false;
    bool artist_emitted = false;
    for (std::size_t group_index = 0; group_index < bms_metadata_groups.size(); ++group_index) {
        const auto& fields = bms_metadata_groups[group_index]->ordered_fields;
        for (std::size_t field_index = 0; field_index < fields.size(); ++field_index) {
            if (is_branch_metadata_field(*bms_metadata_groups[group_index], field_index)) {
                continue;
            }
            const auto& [token, retained_value] = fields[field_index];
            const auto upper_token = ascii_upper(token);
            if (!safe_metadata_token(token) ||
                (upper_token != "PLAYER" && generated_or_channel_token(token))) {
                fail(bms_write_diagnostic_code_t::invalid_document,
                     "retained BMS metadata contains an unsafe or writer-owned command "
                     "token",
                     "$.metadata.format_groups[" + std::to_string(group_index) +
                         "].ordered_fields[" + std::to_string(field_index) + "]");
            }
            std::string value = retained_value;
            if (upper_token == "PLAYER") {
                if (player_emitted) {
                    warn(diagnostics, bms_write_diagnostic_code_t::lossy_transform,
                         "duplicate retained PLAYER metadata was collapsed to one canonical value",
                         "$.metadata.format_groups[" + std::to_string(group_index) +
                             "].ordered_fields[" + std::to_string(field_index) + "]");
                    continue;
                }
                value = " " + player_value;
                player_emitted = true;
            } else if (upper_token == "TITLE") {
                if (title_emitted) {
                    warn(diagnostics, bms_write_diagnostic_code_t::lossy_transform,
                         "duplicate retained TITLE metadata was collapsed to its effective value",
                         "$.metadata.format_groups[" + std::to_string(group_index) +
                             "].ordered_fields[" + std::to_string(field_index) + "]");
                    continue;
                }
                value = title_is_branch_projection ? retained_value : title;
                title_emitted = true;
            } else if (upper_token == "ARTIST") {
                if (artist_emitted) {
                    warn(diagnostics, bms_write_diagnostic_code_t::lossy_transform,
                         "duplicate retained ARTIST metadata was collapsed to its effective value",
                         "$.metadata.format_groups[" + std::to_string(group_index) +
                             "].ordered_fields[" + std::to_string(field_index) + "]");
                    continue;
                }
                value = artist_is_branch_projection ? retained_value : artist;
                artist_emitted = true;
            }
            if (has_line_break_or_nul(value) || !valid_utf8(value)) {
                fail(bms_write_diagnostic_code_t::invalid_document,
                     "retained BMS metadata values must be valid UTF-8 single-line text",
                     "$.metadata.format_groups[" + std::to_string(group_index) +
                         "].ordered_fields[" + std::to_string(field_index) + "]");
            }
            std::string line = "#" + token;
            if (value.empty() || (value.front() != ' ' && value.front() != '\t')) {
                line.push_back(' ');
            }
            line += value;
            output.push_back(std::move(line));
        }
    }
    if (!title_emitted && !title.empty() && !title_is_branch_projection)
        output.emplace_back("#TITLE " + title);
    if (!artist_emitted && !artist.empty() && !artist_is_branch_projection)
        output.emplace_back("#ARTIST " + artist);

    std::vector<const bpm_event_t*> root_bpms;
    for (const auto& bpm : chart.bpms) {
        if (!is_branch_provenance(bpm.provenance) &&
            !is_retained_timing_provenance(bpm.provenance)) {
            root_bpms.push_back(&bpm);
        }
    }
    if (root_bpms.size() > 1 ||
        (!root_bpms.empty() &&
         absolute_row(index, root_bpms.front()->position) != std::optional<std::uint64_t>{0})) {
        fail(bms_write_diagnostic_code_t::unsupported_feature,
             "this writer slice can export only one direct BPM at chart position zero",
             "$.charts[].bpms");
    }
    if (!root_bpms.empty()) {
        output.emplace_back(
            "#BPM " + exact_decimal(root_bpms.front()->beats_per_minute, "$.charts[].bpms[0].bpm"));
    }

    for (const auto& declaration : declarations) {
        if (declaration.branch_scoped)
            continue;
        const auto& identifiers = declaration.name_space == "WAV" ? audio_ids : visual_ids;
        const auto identity = identifiers.find(declaration.resource->id.value);
        if (identity == identifiers.end())
            continue;
        output.emplace_back("#" + declaration.name_space + identity->second + " " +
                            declaration.resource->name);
    }

    bool warned_meter = false;
    for (std::size_t measure = 0; measure < chart.measures.size(); ++measure) {
        const auto& definition = chart.measures[measure].definition;
        if (branch_tree.branch_measure_indices.contains(measure)) {
            continue;
        }
        if (definition.meter.has_value() && !warned_meter) {
            warn(diagnostics, bms_write_diagnostic_code_t::lossy_transform,
                 "BMS measure ratios preserve duration but not authored beat grouping",
                 "$.charts[].measures[].definition.meter");
            warned_meter = true;
        }
        if (definition.length != rational_t{1, 1}) {
            output.emplace_back("#" +
                                (measure < 10    ? std::string("00")
                                 : measure < 100 ? std::string("0")
                                                 : std::string()) +
                                std::to_string(measure) + "02:" +
                                exact_decimal(definition.length.numerator,
                                              definition.length.denominator,
                                              "$.charts[].measures[].definition.length"));
        }
    }
    for (const auto& line : branch_tree.root_measure_ratio_lines) {
        if (!branch_tree.branch_measure_indices.contains(line.measure_index)) {
            continue;
        }
        if (has_line_break_or_nul(line.text) || !valid_utf8(line.text)) {
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "retained BMS measure-ratio statements must be valid UTF-8 single lines",
                 "$.charts[].bms_control_tree");
        }
        output.push_back(line.text);
    }

    auto encode_channel_line = [&](const channel_line_t& line) -> std::string {
        std::uint64_t slot_count = 1;
        for (const auto& note : line.notes) {
            const auto slots = checked_lcm(slot_count, note.denominator);
            if (!slots.has_value() || *slots > options.max_slots_per_line || *slots > 500'000) {
                fail(bms_write_diagnostic_code_t::resource_limit,
                     "exact note positions require too many BMS subdivision slots",
                     "$.charts[].measures[].notes");
            }
            slot_count = *slots;
        }
        if (slot_count == 0 || slot_count > options.max_slots_per_line || slot_count > 500'000) {
            fail(bms_write_diagnostic_code_t::resource_limit,
                 "BMS channel line exceeds the configured subdivision limit",
                 "$.charts[].measures[].notes");
        }
        std::vector<std::string> cells(static_cast<std::size_t>(slot_count), "00");
        std::vector<bool> occupied(static_cast<std::size_t>(slot_count), false);
        for (const auto& note : line.notes) {
            const auto multiplier = slot_count / note.denominator;
            if (note.numerator > std::numeric_limits<std::uint64_t>::max() / multiplier) {
                fail(bms_write_diagnostic_code_t::unrepresentable_number,
                     "note subdivision index overflows the BMS channel grid", note.path);
            }
            const auto slot = note.numerator * multiplier;
            if (slot >= slot_count || occupied[static_cast<std::size_t>(slot)]) {
                fail(bms_write_diagnostic_code_t::duplicate_event,
                     "two events occupy the same BMS channel slot", note.path);
            }
            occupied[static_cast<std::size_t>(slot)] = true;
            cells[static_cast<std::size_t>(slot)] = note.object_id;
        }
        std::string payload;
        payload.reserve(cells.size() * 2);
        for (const auto& cell : cells)
            payload += cell;
        const auto prefix = line.measure < 10    ? std::string("00")
                            : line.measure < 100 ? std::string("0")
                                                 : std::string();
        return "#" + prefix + std::to_string(line.measure) + line.channel + ":" +
               std::move(payload);
    };

    std::vector<const channel_line_t*> ordered_channel_lines;
    ordered_channel_lines.reserve(channel_lines.size());
    for (const auto& [key, line] : channel_lines) {
        (void)key;
        ordered_channel_lines.push_back(&line);
    }
    std::ranges::stable_sort(ordered_channel_lines, [&](const auto* left, const auto* right) {
        const auto left_key = std::pair{left->measure, left->channel};
        const auto right_key = std::pair{right->measure, right->channel};
        const auto left_order = bga_line_order.contains(left_key) ? bga_line_order.at(left_key) : 0;
        const auto right_order =
            bga_line_order.contains(right_key) ? bga_line_order.at(right_key) : 0;
        return std::tie(left->measure, left_order, left->channel) <
               std::tie(right->measure, right_order, right->channel);
    });
    for (const auto* line : ordered_channel_lines) {
        output.push_back(encode_channel_line(*line));
    }

    std::vector<std::pair<std::pair<std::size_t, std::uint64_t>, const channel_line_t*>>
        ordered_bgm_lines;
    ordered_bgm_lines.reserve(bgm_lines.size());
    for (const auto& [key, line] : bgm_lines) {
        ordered_bgm_lines.emplace_back(key, &line);
    }
    std::ranges::stable_sort(ordered_bgm_lines, [&](const auto& left, const auto& right) {
        const auto left_lane = std::ranges::find(chart.bgm_source_lanes, left.first.second,
                                                 [](const auto& lane) { return lane.id.value; });
        const auto right_lane = std::ranges::find(chart.bgm_source_lanes, right.first.second,
                                                  [](const auto& lane) { return lane.id.value; });
        const auto left_order = left_lane == chart.bgm_source_lanes.end()
                                    ? std::numeric_limits<std::size_t>::max()
                                    : left_lane->source_order;
        const auto right_order = right_lane == chart.bgm_source_lanes.end()
                                     ? std::numeric_limits<std::size_t>::max()
                                     : right_lane->source_order;
        return std::tie(left.first.first, left_order, left.first.second) <
               std::tie(right.first.first, right_order, right.first.second);
    });
    for (const auto& [key, line] : ordered_bgm_lines) {
        (void)key;
        output.push_back(encode_channel_line(*line));
    }

    std::vector<std::string> preserved_tree_lines;
    preserved_tree_lines.reserve(branch_tree.preserved_lines.size());
    std::set<std::size_t> emitted_branch_metadata_edits;
    std::set<std::size_t> emitted_branch_resource_edits;
    for (const auto& [source_order, line] : branch_tree.preserved_lines) {
        if (has_line_break_or_nul(line) || !valid_utf8(line)) {
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "retained BMS control-tree statements must be valid UTF-8 single lines",
                 "$.charts[].bms_control_tree");
        }
        const auto metadata_edit = branch_metadata_edits.find(source_order);
        const auto resource_edit = branch_resource_edits.find(source_order);
        if (metadata_edit == branch_metadata_edits.end() &&
            resource_edit == branch_resource_edits.end()) {
            preserved_tree_lines.push_back(line);
            continue;
        }
        if (metadata_edit != branch_metadata_edits.end() &&
            resource_edit != branch_resource_edits.end()) {
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "one retained branch statement is ambiguously claimed by multiple edits",
                 "$.charts[].bms_control_tree");
        }
        if (metadata_edit != branch_metadata_edits.end()) {
            const auto source_field = branch_tree.branch_metadata_fields.find(source_order);
            if (source_field == branch_tree.branch_metadata_fields.end()) {
                fail(bms_write_diagnostic_code_t::branch_provenance,
                     "edited branch metadata has no retained source identity",
                     "$.charts[].bms_control_tree");
            }
            const auto replaced =
                replace_retained_header_value(line, source_field->second.first,
                                              source_field->second.second, metadata_edit->second);
            if (!replaced.has_value() ||
                !emitted_branch_metadata_edits.insert(source_order).second) {
                fail(bms_write_diagnostic_code_t::branch_provenance,
                     "edited branch metadata does not identify one unambiguous retained source "
                     "statement",
                     "$.charts[].bms_control_tree");
            }
            preserved_tree_lines.push_back(*replaced);
            continue;
        }
        const auto source_field = branch_tree.branch_resource_fields.find(source_order);
        if (source_field == branch_tree.branch_resource_fields.end()) {
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "edited branch resource has no retained source identity",
                 "$.charts[].bms_control_tree");
        }
        const auto replaced = replace_retained_header_value(
            line, source_field->second.first, source_field->second.second, resource_edit->second);
        if (!replaced.has_value() || !emitted_branch_resource_edits.insert(source_order).second) {
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "edited branch resource does not identify one unambiguous retained source "
                 "statement",
                 "$.charts[].bms_control_tree");
        }
        preserved_tree_lines.push_back(*replaced);
    }
    if (emitted_branch_metadata_edits.size() != branch_metadata_edits.size()) {
        fail(bms_write_diagnostic_code_t::branch_provenance,
             "one or more edited branch metadata statements are absent from the retained tree",
             "$.charts[].bms_control_tree");
    }
    if (emitted_branch_resource_edits.size() != branch_resource_edits.size()) {
        fail(bms_write_diagnostic_code_t::branch_provenance,
             "one or more edited branch resource statements are absent from the retained tree",
             "$.charts[].bms_control_tree");
    }
    if (branch_tree.has_branches && !preserved_tree_lines.empty()) {
        if (!branch_tree.branch_source_begin.has_value() ||
            !branch_tree.branch_source_end.has_value()) {
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "retained BMS branch tree has no source-order span",
                 "$.charts[].bms_control_tree");
        }
        bool root_before_tree = true;
        bool root_after_tree = !branch_tree.root_source_orders.empty();
        for (const auto source_order : branch_tree.root_source_orders) {
            root_before_tree = root_before_tree && source_order < *branch_tree.branch_source_begin;
            root_after_tree = root_after_tree && source_order > *branch_tree.branch_source_end;
        }
        if (!branch_tree.root_source_orders.empty() && !root_before_tree && !root_after_tree) {
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "root semantic statements occur on both sides of retained branch source; this "
                 "writer cannot interleave normalized lines without changing override order",
                 "$.charts[].bms_control_tree");
        }
        if (root_after_tree) {
            output.insert(output.begin(), preserved_tree_lines.begin(), preserved_tree_lines.end());
        } else {
            output.insert(output.end(), preserved_tree_lines.begin(), preserved_tree_lines.end());
        }
    }

    std::string bytes = "\xef\xbb\xbf";
    for (const auto& line : output) {
        bytes += line;
        bytes.push_back('\n');
    }
    return bytes;
}

} // namespace

auto bms_write_result_t::has_errors() const -> bool {
    return std::ranges::any_of(diagnostics, [](const auto& diagnostic) {
        return diagnostic.severity == diagnostic_severity_t::error;
    });
}

auto write_bms(const song_document_t& song, bms_write_options_t options) -> bms_write_result_t {
    bms_write_result_t result;
    try {
        const chart_document_t* chart{};
        if (options.chart.has_value()) {
            const auto found =
                std::ranges::find(song.charts, *options.chart, &chart_document_t::id);
            if (found == song.charts.end()) {
                fail(bms_write_diagnostic_code_t::missing_chart,
                     "requested chart ID does not exist", "$.charts");
            }
            chart = &*found;
        } else if (song.charts.size() == 1) {
            chart = &song.charts.front();
        } else {
            fail(bms_write_diagnostic_code_t::missing_chart,
                 "select one chart explicitly when the song does not contain exactly one chart",
                 "$.charts");
        }

        if (!options.output_encoding.has_value() && options.preserve_unchanged_source &&
            song.charts.size() == 1 &&
            options.branch_policy == bms_branch_policy_t::preserve_tree) {
            if (const auto source = retained_source_bytes(song, *chart); source) {
                result.bytes =
                    std::string(reinterpret_cast<const char*>(source->data()), source->size());
                return result;
            }
        }
        auto normalized = normalized_write(song, *chart, options, result.diagnostics);
        if (!options.output_encoding.has_value()) {
            result.bytes = std::move(normalized);
            return result;
        }
        auto encoded = encode_normalized_bms(normalized, *options.output_encoding);
        if (encoded.unsupported) {
            fail(bms_write_diagnostic_code_t::unsupported_encoding,
                 "requested " + std::string(encoding_name(*options.output_encoding)) +
                     " output encoding is unavailable",
                 "$.output_encoding");
        }
        if (!encoded.bytes.has_value()) {
            fail(bms_write_diagnostic_code_t::unrepresentable_text,
                 "normalized BMS text cannot be represented as " +
                     std::string(encoding_name(*options.output_encoding)) + " near UTF-8 byte " +
                     std::to_string(encoded.error_offset),
                 "$.output_encoding");
        }
        result.bytes = std::move(*encoded.bytes);
    } catch (const write_failure_t& failure) {
        result.diagnostics.push_back(
            {diagnostic_severity_t::error, failure.code, failure.message, failure.path});
    }
    return result;
}

} // namespace wormedit
