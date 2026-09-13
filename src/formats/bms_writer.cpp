#include <wormedit/formats/bms_writer.h>

#include <wormedit/core/bms_profile_detection.h>

#include <algorithm>
#include <array>
#include <cctype>
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

struct write_failure_t {
    bms_write_diagnostic_code_t code;
    std::string message;
    std::string path;
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

    if (chart.bms_control_tree.has_value()) {
        const auto& root = *chart.bms_control_tree;
        const auto has_branch = [&](const auto& self, const bms_control_node_t& node) -> bool {
            if (node.kind == bms_control_kind_t::random ||
                node.kind == bms_control_kind_t::set_random ||
                node.kind == bms_control_kind_t::if_group ||
                node.kind == bms_control_kind_t::if_branch ||
                node.kind == bms_control_kind_t::else_if_branch ||
                node.kind == bms_control_kind_t::else_branch ||
                node.kind == bms_control_kind_t::switch_block ||
                node.kind == bms_control_kind_t::set_switch ||
                node.kind == bms_control_kind_t::case_branch ||
                node.kind == bms_control_kind_t::default_branch ||
                node.kind == bms_control_kind_t::skip) {
                return true;
            }
            return std::ranges::any_of(node.children,
                                       [&](const auto& child) { return self(self, child); });
        };
        if (has_branch(has_branch, root) || !chart.active_branch_selection.empty()) {
            fail(bms_write_diagnostic_code_t::unsupported_feature,
                 "BMS branch trees require an explicit branch-preserving or flattening policy",
                 "$.charts[].bms_control_tree");
        }
        const auto has_unmodeled_raw = [&](const auto& self,
                                           const bms_control_node_t& node) -> bool {
            if (node.kind == bms_control_kind_t::raw_statement && !node.original_text.empty() &&
                !safe_metadata_token(node.command_token) &&
                !generated_or_channel_token(node.command_token)) {
                return true;
            }
            return std::ranges::any_of(node.children,
                                       [&](const auto& child) { return self(self, child); });
        };
        if (has_unmodeled_raw(has_unmodeled_raw, root)) {
            warn(diagnostics, bms_write_diagnostic_code_t::lossy_transform,
                 "unmodeled raw source lines were omitted from normalized BMS output",
                 "$.charts[].bms_control_tree");
        }
    }

    if (!chart.pauses.empty() || !chart.warps.empty() || !chart.scrolls.empty() ||
        !chart.speeds.empty() || !chart.kiai.empty() || !chart.timing_anchors.empty() ||
        !chart.sections.empty() || !chart.sound_channels.empty()) {
        fail(bms_write_diagnostic_code_t::unsupported_feature,
             "chart contains timing or media features outside the normalized writer slice",
             "$.charts[]");
    }
    if (std::ranges::any_of(chart.measures,
                            [](const auto& measure) { return !measure.bgm_events.empty(); }) ||
        !chart.bga_events.empty()) {
        fail(bms_write_diagnostic_code_t::unsupported_feature,
             "BGM and BGA event export is not implemented in this writer slice", "$.charts[]");
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
    std::set<std::string> used_channels;
    for (const auto& lane : layout.lanes) {
        if (!lane.bms_channels.visible.has_value())
            continue;
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

    std::map<std::uint64_t, std::string> audio_ids;
    std::map<std::uint64_t, std::string> visual_ids;
    std::map<std::string, std::size_t> next_ids{{"WAV", 1}, {"BMP", 1}};
    std::map<std::string, std::set<std::string>> assigned_ids;
    std::vector<std::pair<std::string, const resource_entry_t*>> declarations;
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
        auto identifier = bms_identity(resource, name_space);
        if (!identifier.has_value() || assigned_ids[name_space].contains(*identifier)) {
            const auto old = identifier;
            auto& next = next_ids[name_space];
            while (next < 1296 && assigned_ids[name_space].contains(base36_id(next))) {
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
        declarations.emplace_back(name_space, &resource);
    }

    const auto measure_index_result = build_measure_index(chart);
    if (!std::holds_alternative<measure_index_t>(measure_index_result)) {
        fail(bms_write_diagnostic_code_t::invalid_geometry,
             "chart measure geometry cannot be represented safely", "$.charts[].measures");
    }
    const auto& index = std::get<measure_index_t>(measure_index_result);

    std::map<std::pair<std::size_t, std::string>, channel_line_t> channel_lines;
    for (std::size_t measure = 0; measure < chart.measures.size(); ++measure) {
        for (const auto& note : chart.measures[measure].notes) {
            const auto path = "$.charts[].measures[" + std::to_string(measure) + "].notes[]";
            if (note.kind != note_kind_t::tap || note.end.has_value()) {
                fail(bms_write_diagnostic_code_t::unsupported_feature,
                     "normalized BMS writer currently supports playable tap notes only", path);
            }
            if (note.branch.has_value()) {
                fail(bms_write_diagnostic_code_t::unsupported_feature,
                     "branch-local notes require an explicit branch export policy", path);
            }
            const auto channel = lane_channels.find(note.lane.value);
            if (channel == lane_channels.end()) {
                fail(bms_write_diagnostic_code_t::missing_profile,
                     "note lane has no visible BMS channel in the selected lane map", path);
            }
            if (!note.sound.has_value()) {
                fail(bms_write_diagnostic_code_t::unresolved_resource,
                     "BMS playable taps require a WAV resource binding", path + ".sound");
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
        }
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
    const auto has_player_field = std::ranges::any_of(bms_metadata_groups, [](const auto* group) {
        return std::ranges::any_of(group->ordered_fields, [](const auto& field) {
            return ascii_upper(field.first) == "PLAYER";
        });
    });
    std::vector<std::string> output;
    if (!has_player_field)
        output.emplace_back("#PLAYER " + player_value);
    bool player_emitted = false;
    bool title_emitted = false;
    bool artist_emitted = false;
    for (std::size_t group_index = 0; group_index < bms_metadata_groups.size(); ++group_index) {
        const auto& fields = bms_metadata_groups[group_index]->ordered_fields;
        for (std::size_t field_index = 0; field_index < fields.size(); ++field_index) {
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
                value = title;
                title_emitted = true;
            } else if (upper_token == "ARTIST") {
                value = artist;
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
    if (!title_emitted && !title.empty())
        output.emplace_back("#TITLE " + title);
    if (!artist_emitted && !artist.empty())
        output.emplace_back("#ARTIST " + artist);

    if (!chart.bpms.empty()) {
        if (chart.bpms.size() != 1 ||
            absolute_row(index, chart.bpms.front().position) != std::optional<std::uint64_t>{0}) {
            fail(bms_write_diagnostic_code_t::unsupported_feature,
                 "this writer slice can export only one direct BPM at chart position zero",
                 "$.charts[].bpms");
        }
        output.emplace_back(
            "#BPM " + exact_decimal(chart.bpms.front().beats_per_minute, "$.charts[].bpms[0].bpm"));
    }

    for (const auto& [name_space, resource] : declarations) {
        const auto& identifiers = name_space == "WAV" ? audio_ids : visual_ids;
        const auto identity = identifiers.find(resource->id.value);
        if (identity == identifiers.end())
            continue;
        output.emplace_back("#" + name_space + identity->second + " " + resource->name);
    }

    bool warned_meter = false;
    for (std::size_t measure = 0; measure < chart.measures.size(); ++measure) {
        const auto& definition = chart.measures[measure].definition;
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

    for (auto& [key, line] : channel_lines) {
        (void)key;
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
        for (const auto& note : line.notes) {
            const auto multiplier = slot_count / note.denominator;
            if (note.numerator > std::numeric_limits<std::uint64_t>::max() / multiplier) {
                fail(bms_write_diagnostic_code_t::unrepresentable_number,
                     "note subdivision index overflows the BMS channel grid", note.path);
            }
            const auto slot = note.numerator * multiplier;
            if (slot >= slot_count || cells[static_cast<std::size_t>(slot)] != "00") {
                fail(bms_write_diagnostic_code_t::duplicate_event,
                     "two tap notes occupy the same BMS lane slot", note.path);
            }
            cells[static_cast<std::size_t>(slot)] = note.object_id;
        }
        std::string payload;
        payload.reserve(cells.size() * 2);
        for (const auto& cell : cells)
            payload += cell;
        const auto prefix = line.measure < 10    ? std::string("00")
                            : line.measure < 100 ? std::string("0")
                                                 : std::string();
        output.emplace_back("#" + prefix + std::to_string(line.measure) + line.channel + ":" +
                            std::move(payload));
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

        if (options.preserve_unchanged_source && song.charts.size() == 1) {
            if (const auto source = retained_source_bytes(song, *chart); source) {
                result.bytes =
                    std::string(reinterpret_cast<const char*>(source->data()), source->size());
                return result;
            }
        }
        result.bytes = normalized_write(song, *chart, options, result.diagnostics);
    } catch (const write_failure_t& failure) {
        result.diagnostics.push_back(
            {diagnostic_severity_t::error, failure.code, failure.message, failure.path});
    }
    return result;
}

} // namespace wormedit
