#include "bms_program_import_internal.h"
#include "bms_timing_directive.h"
#include "../core/text_util.h"
#include <wormedit/core/bms36.h>
#include <wormedit/core/numeric_text.h>
#include <wormedit/core/rational.h>
#include <algorithm>
#include <charconv>
#include <map>

namespace wormedit {
namespace {
struct import_scope_t {
    std::size_t node;
    std::optional<std::size_t> case_node;
};
struct program_import_t {
    const chart_document_t& source_chart;
    const bms_parse_options_t& options;
    const bms_program_limits_t& limits;
    bms_program_import_outcome_t& result;
    id_allocator_t& ids;
    std::vector<bms_program_node_t> nodes;
    std::unordered_map<std::uint64_t, std::size_t> node_indices;
    std::optional<lane_input_profile_t> profile;
    std::vector<bgm_source_lane_t> columns;
    std::vector<resource_entry_t> resources;
    std::vector<audio_asset_t> audio;
    std::vector<visual_asset_t> visual;
    std::map<std::pair<bms_resource_namespace_t, std::string>, asset_id_t> assets;
    std::map<std::size_t, std::size_t> bgm_counts;
    std::size_t item_count{}, text_bytes{};

    void diagnose(const bms_control_node_t& source, bms_parse_diagnostic_code_t code,
        std::string message) {
        result.diagnostics.push_back({.severity = diagnostic_severity_t::error, .code = code,
            .message = std::move(message), .location = {source.source_order + 1, 1, source.source_span}});
    }
    auto provenance(const bms_control_node_t& source) const -> source_provenance_t {
        return {.family = format_family_t::bms, .profile = "bms-scoped-import",
            // Original bytes live once in the import backing. In particular, do
            // not copy a megabyte channel line into every slot's provenance.
            .source_field = source.command_token,
            .source_order = source.source_order};
    }
    auto append_node(import_scope_t parent, branch_id_t id, bms_program_node_value_t value,
        const bms_control_node_t& source) -> std::optional<std::size_t> {
        if (nodes.size() >= limits.maximum_nodes || !id) {
            diagnose(source, bms_parse_diagnostic_code_t::malformed_line, "Scoped program node capacity or identity space is exhausted.");
            return std::nullopt;
        }
        const auto index = nodes.size();
        nodes[parent.node].children.push_back(id);
        node_indices.emplace(id.value, index);
        nodes.push_back({.id = id, .value = std::move(value)});
        return index;
    }
    auto ensure_chart(import_scope_t parent, const bms_control_node_t& source) -> std::optional<std::size_t> {
        const auto& children = nodes[parent.node].children;
        if (!children.empty()) {
            const auto last = nodes.begin() + node_indices.at(children.back().value);
            if (std::holds_alternative<bms_chart_content_t>(last->value))
                return static_cast<std::size_t>(last - nodes.begin());
        }
        return append_node(parent, allocate_id<branch_id_t>(ids).value_or(branch_id_t{}), bms_chart_content_t{}, source);
    }
    auto append_content(import_scope_t parent, const bms_control_node_t& source, bms_content_value_t value) -> bool {
        if (item_count >= limits.maximum_content_items) {
            diagnose(source, bms_parse_diagnostic_code_t::malformed_line, "Scoped program content capacity is exhausted.");
            return false;
        }
        const auto id = allocate_id<object_id_t>(ids);
        const auto chart = ensure_chart(parent, source);
        if (!chart || !id) return false;
        std::get<bms_chart_content_t>(nodes[*chart].value).items.push_back(
            {.id = *id, .value = std::move(value), .provenance = provenance(source)});
        ++item_count;
        return true;
    }
    auto raw_bytes(const bms_control_node_t& source) const -> std::string {
        const auto& bytes = *result.value.encoding.original_bytes;
        const auto span = source.source_span;
        if (span.byte_begin == span.byte_end) return {};
        return {reinterpret_cast<const char*>(bytes.data() + span.byte_begin), span.byte_end - span.byte_begin};
    }
    auto append_text(import_scope_t parent, const bms_control_node_t& source) -> bool {
        auto bytes = raw_bytes(source);
        if (bytes.size() > limits.maximum_text_bytes - text_bytes) {
            diagnose(source, bms_parse_diagnostic_code_t::malformed_line, "Scoped program Text capacity is exhausted.");
            return false;
        }
        text_bytes += bytes.size();
        const auto& children = nodes[parent.node].children;
        if (!children.empty()) {
            const auto last = nodes.begin() + node_indices.at(children.back().value);
            if (auto* text = std::get_if<bms_text_t>(&last->value)) {
                text->bytes += bytes;
                return true;
            }
        }
        return append_node(parent, source.id,
            bms_text_t{std::move(bytes), result.value.encoding.detected}, source).has_value();
    }
    auto choose_profile(const bms_control_node_t& root) -> bool;
    void collect_profile_evidence(const bms_control_node_t& source, bms_profile_detection_request_t& request) const;
    auto import_metadata(import_scope_t parent, const bms_control_node_t& source,
        const bms_source_statement_t& statement, bms_metadata_field_t field) -> bool;
    auto import_resource(import_scope_t parent, const bms_control_node_t& source,
        const bms_source_statement_t& statement, bms_resource_namespace_t name_space) -> bool;
    auto import_timing(import_scope_t parent, const bms_control_node_t& source,
        const bms_source_statement_t& statement, bms_timing_definition_kind_t kind) -> bool;
    auto import_statement(import_scope_t parent, const bms_control_node_t& source) -> bool;
    auto import_channel(import_scope_t parent, const bms_control_node_t& source,
        const bms_source_statement_t& statement) -> bool;
    auto import_slot(import_scope_t parent, const bms_control_node_t& source,
        const bms_source_statement_t& statement, std::size_t slot, std::optional<object_id_t> column) -> bool;
    auto import_direct_bpm(import_scope_t parent, const bms_control_node_t& source,
        bms_program_position_t position, const std::string& object) -> bool;
    auto import_note(import_scope_t parent, const bms_control_node_t& source,
        const bms_source_statement_t& statement, bms_program_position_t position, std::string object) -> bool;
    auto import_scope(import_scope_t parent, const bms_control_node_t& source,
        std::optional<branch_id_t> previous = std::nullopt) -> bool;
    auto import_children(import_scope_t parent, const bms_control_node_t& source) -> bool;
    auto import_child(import_scope_t parent, const bms_control_node_t& child) -> bool;
    auto import_group(import_scope_t parent, const bms_control_node_t& group) -> bool;
    auto import_skip(import_scope_t parent, const bms_control_node_t& source) -> bool;
    auto scope_value(const bms_control_node_t& source, std::optional<branch_id_t> previous)
        -> std::optional<bms_program_node_value_t>;
};

void program_import_t::collect_profile_evidence(const bms_control_node_t& source,
    bms_profile_detection_request_t& request) const {
    if (source.kind == bms_control_kind_t::raw_statement) {
        const auto statement = bms_source_statement_t::from(source);
        if (statement.is_playable()) request.used_channels.push_back(statement.measure_channel->second);
        if (statement.token == "PLAYER") {
            unsigned int player{};
            if (parse_integer(statement.value, player)) request.player_header = player;
        }
        if (statement.token == "OCT/FP") request.octave_fp_header = true;
    }
    for (const auto& child : source.children) collect_profile_evidence(child, request);
}
auto program_import_t::choose_profile(const bms_control_node_t& root) -> bool {
    bms_profile_detection_request_t request{.file_extension = options.file_extension,
        .explicit_profile_id = options.lane_profile_id};
    collect_profile_evidence(root, request);
    if (request.used_channels.empty() && !request.explicit_profile_id) return true;
    auto detected = detect_bms_lane_profile(request);
    profile = std::move(detected.selected_profile);
    if (profile) return true;
    diagnose(root, bms_parse_diagnostic_code_t::unsupported_channel,
        "All scoped playable channels require one compatible explicit or unambiguous lane profile.");
    return false;
}
auto program_import_t::import_metadata(import_scope_t parent, const bms_control_node_t& source,
    const bms_source_statement_t& statement, bms_metadata_field_t field) -> bool {
    bms_metadata_declaration_t value{.field = field, .value = statement.value};
    if (field == bms_metadata_field_t::offset) {
        const auto number = parse_exact_decimal(statement.value, 38);
        if (!number) {
            diagnose(source, bms_parse_diagnostic_code_t::invalid_number, "OFFSET must be an exact signed decimal.");
            return false;
        }
        value.value = *number;
    }
    if (field == bms_metadata_field_t::player) {
        unsigned int player{};
        if (!parse_integer(statement.value, player)) {
            diagnose(source, bms_parse_diagnostic_code_t::invalid_number, "PLAYER must be a supported unsigned integer.");
            return false;
        }
        value.value = signed_rational_t{static_cast<std::int64_t>(player), 1};
    }
    return append_content(parent, source, std::move(value));
}
auto program_import_t::import_resource(import_scope_t parent, const bms_control_node_t& source,
    const bms_source_statement_t& statement, bms_resource_namespace_t name_space) -> bool {
    if (statement.value.empty() || bms36_value(statement.token[3]) < 0 || bms36_value(statement.token[4]) < 0) {
        diagnose(source, bms_parse_diagnostic_code_t::malformed_line, "A scoped resource declaration requires a valid identifier and path.");
        return false;
    }
    const auto key = std::pair{name_space, statement.value};
    auto found = assets.find(key);
    if (found == assets.end()) {
        const auto id = allocate_id<asset_id_t>(ids);
        if (!id) return false;
        found = assets.emplace(key, *id).first;
        const auto path = text_util::path_from_utf8(statement.value);
        if (name_space == bms_resource_namespace_t::wav) audio.push_back({.id = *id, .source_path = path});
        else visual.push_back({.id = *id, .kind = asset_kind_t::image, .source_path = path});
    }
    const auto resource = allocate_id<resource_id_t>(ids);
    if (!resource) return false;
    resources.push_back({.id = *resource, .name = statement.value, .asset = found->second,
        .roles = name_space == bms_resource_namespace_t::wav
            ? std::vector{resource_role_t::sample, resource_role_t::bgm} : std::vector{resource_role_t::bga}});
    return append_content(parent, source, bms_resource_declaration_t{name_space, statement.token.substr(3), *resource});
}
auto program_import_t::import_timing(import_scope_t parent, const bms_control_node_t& source,
    const bms_source_statement_t& statement, bms_timing_definition_kind_t kind) -> bool {
    const auto value = kind == bms_timing_definition_kind_t::scroll
        ? parse_exact_decimal(statement.value, 38) : statement.positive_number();
    if (!value) {
        diagnose(source, bms_parse_diagnostic_code_t::invalid_number, "Scoped timing declaration has an invalid exact value.");
        return false;
    }
    const auto* directive = bms_timing_directive_t::from(statement.token);
    const auto identifier = directive ? std::optional{statement.token.substr(directive->name.size())} : std::nullopt;
    return append_content(parent, source, bms_timing_definition_t{kind, identifier, *value});
}
auto program_import_t::import_statement(import_scope_t parent, const bms_control_node_t& source) -> bool {
    const auto statement = bms_source_statement_t::from(source);
    if (const auto* metadata = bms_metadata_definition_t::from(statement.token))
        return import_metadata(parent, source, statement, metadata->field);
    if (statement.token.size() == 5 && (statement.token.starts_with("WAV") || statement.token.starts_with("BMP")))
        return import_resource(parent, source, statement,
            statement.token.starts_with("WAV") ? bms_resource_namespace_t::wav : bms_resource_namespace_t::bmp);
    if (statement.token == "BPM") return import_timing(parent, source, statement, bms_timing_definition_kind_t::bpm);
    if (const auto* timing = bms_timing_directive_t::from(statement.token)) {
        return import_timing(parent, source, statement, timing->kind);
    }
    for (const auto& timing : bms_timing_directives) {
        if (statement.token.size() == timing.name.size() + 2 && statement.token.starts_with(timing.name)) {
            diagnose(source, bms_parse_diagnostic_code_t::invalid_number, "Scoped timing identifier is malformed.");
            return false;
        }
    }
    if (statement.measure_channel) return import_channel(parent, source, statement);
    if (statement.is_control()) {
        diagnose(source, bms_parse_diagnostic_code_t::malformed_control, "Malformed control retained in original source; prepared publication refused.");
        return false;
    }
    return append_text(parent, source);
}
auto program_import_t::import_note(import_scope_t parent, const bms_control_node_t& source,
    const bms_source_statement_t& statement, bms_program_position_t position, std::string object) -> bool {
    const auto& channel = statement.measure_channel->second;
    const auto lane = std::ranges::find_if(profile->layout.lanes, [&](const auto& candidate) {
        const auto& map = candidate.bms_channels;
        return map.visible == channel || map.invisible == channel || map.long_note == channel || map.mine == channel;
    });
    if (lane == profile->layout.lanes.end()) {
        diagnose(source, bms_parse_diagnostic_code_t::unsupported_channel, "Scoped note channel cannot be mapped to the shared lane profile.");
        return false;
    }
    if (lane->bms_channels.long_note == channel)
        return append_content(parent, source, bms_hold_endpoint_t{position, lane->id, std::move(object)});
    const auto kind = lane->bms_channels.mine == channel ? note_kind_t::mine
        : lane->bms_channels.invisible == channel ? note_kind_t::invisible : note_kind_t::tap;
    const auto mine = kind == note_kind_t::mine
        ? std::optional{static_cast<unsigned int>(bms36_value(object[0]) * 36 + bms36_value(object[1]))} : std::nullopt;
    return append_content(parent, source, bms_note_content_t{position, lane->id, kind,
        kind == note_kind_t::mine ? std::nullopt : std::optional{std::move(object)}, mine});
}
auto program_import_t::import_direct_bpm(import_scope_t parent, const bms_control_node_t& source,
    bms_program_position_t position, const std::string& object) -> bool {
    unsigned int value{};
    const auto decoded = std::from_chars(object.data(), object.data() + object.size(), value, 16);
    if (decoded.ec != std::errc{} || decoded.ptr != object.data() + object.size() || !value) {
        diagnose(source, bms_parse_diagnostic_code_t::invalid_number, "Direct BPM must be a positive hexadecimal byte.");
        return false;
    }
    return append_content(parent, source, bms_direct_bpm_t{position, {value, 1}});
}
auto program_import_t::import_slot(import_scope_t parent, const bms_control_node_t& source,
    const bms_source_statement_t& statement, std::size_t slot, std::optional<object_id_t> column) -> bool {
    const auto& [measure, channel] = *statement.measure_channel;
    auto object = text_util::ascii_upper(std::string_view(statement.value).substr(slot * 2, 2));
    const bms_program_position_t position{source_chart.measures[measure].id,
        reduce_fraction(rational_t{slot, statement.value.size() / 2})};
    if (column) return append_content(parent, source, bms_bgm_content_t{position, *column,
        object == "00" ? std::nullopt : std::optional{std::move(object)}, slot});
    if (object == "00") return true;
    if (channel == "03") return import_direct_bpm(parent, source, position, object);
    if (const auto* timing = bms_timing_directive_t::for_channel(channel))
        return append_content(parent, source, bms_timing_reference_t{position, timing->kind, std::move(object)});
    if (const auto kind = statement.bga_kind())
        return append_content(parent, source, bms_bga_content_t{position, *kind, std::move(object)});
    return import_note(parent, source, statement, position, std::move(object));
}
auto program_import_t::import_channel(import_scope_t parent, const bms_control_node_t& source,
    const bms_source_statement_t& statement) -> bool {
    const auto& [measure, channel] = *statement.measure_channel;
    if (channel == "02") {
        const auto length = statement.measure_length();
        if (!length) {
            diagnose(source, bms_parse_diagnostic_code_t::invalid_measure, "Scoped measure length is not a positive exact ratio.");
            return false;
        }
        return append_content(parent, source, bms_measure_length_t{source_chart.measures[measure].id, *length});
    }
    if (!statement.is_builtin_channel() && !statement.is_playable()) return append_text(parent, source);
    std::optional<object_id_t> column;
    if (channel == "01") {
        const auto index = bgm_counts[measure]++;
        if (index == columns.size()) columns.push_back({.id = allocate_id<object_id_t>(ids).value_or(object_id_t{}),
            .name = "BGM " + std::to_string(index + 1), .source_order = index});
        column = columns[index].id;
    }
    if (!ensure_chart(parent, source)) return false;
    for (std::size_t slot = 0; slot < statement.value.size() / 2; ++slot)
        if (!import_slot(parent, source, statement, slot, column)) return false;
    return true;
}
auto program_import_t::scope_value(const bms_control_node_t& source, std::optional<branch_id_t> previous)
    -> std::optional<bms_program_node_value_t> {
    switch (source.kind) {
    case bms_control_kind_t::root: return bms_root_scope_t{};
    case bms_control_kind_t::random:
        if (source.value) return bms_random_scope_t{*source.value};
        break;
    case bms_control_kind_t::if_branch:
        if (source.value) return bms_if_scope_t{*source.value};
        break;
    case bms_control_kind_t::else_if_branch:
        if (source.value && previous) return bms_else_if_scope_t{*previous, *source.value};
        break;
    case bms_control_kind_t::else_branch:
        if (previous) return bms_else_scope_t{*previous};
        break;
    case bms_control_kind_t::switch_block:
        if (source.value) return bms_switch_scope_t{*source.value};
        break;
    case bms_control_kind_t::case_branch: return bms_case_scope_t{.value = source.value};
    case bms_control_kind_t::default_branch: return bms_case_scope_t{};
    case bms_control_kind_t::set_random:
        if (source.value) return bms_set_random_t{*source.value};
        break;
    case bms_control_kind_t::set_switch:
        if (source.value) return bms_set_switch_t{*source.value};
        break;
    case bms_control_kind_t::raw_statement:
    case bms_control_kind_t::if_group:
    case bms_control_kind_t::skip: break;
    }
    diagnose(source, bms_parse_diagnostic_code_t::malformed_control, "Control has missing settings or unsupported source structure.");
    return std::nullopt;
}
auto program_import_t::import_scope(import_scope_t parent, const bms_control_node_t& source,
    std::optional<branch_id_t> previous) -> bool {
    const auto value = scope_value(source, previous);
    if (!value) return false;
    const auto index = append_node(parent, source.id, *value, source);
    if (!index) return false;
    auto scope = import_scope_t{*index, parent.case_node};
    if (std::holds_alternative<bms_case_scope_t>(*value)) scope.case_node = *index;
    return import_children(scope, source);
}
auto program_import_t::import_group(import_scope_t parent, const bms_control_node_t& group) -> bool {
    std::optional<branch_id_t> previous;
    for (const auto& arm : group.children) {
        if (!import_scope(parent, arm, previous)) return false;
        previous = arm.id;
    }
    return true;
}
auto program_import_t::import_skip(import_scope_t parent, const bms_control_node_t& source) -> bool {
    if (!parent.case_node) {
        diagnose(source, bms_parse_diagnostic_code_t::malformed_control, "SKIP requires a containing case.");
        return false;
    }
    const auto& children = nodes[parent.node].children;
    const auto after = children.empty() ? std::nullopt : std::optional{children.back()};
    std::get<bms_case_scope_t>(nodes[*parent.case_node].value).skips.push_back(
        {nodes[parent.node].id, after, provenance(source)});
    // A subsequent semantic run must not cross this execution boundary.
    return append_node(parent, allocate_id<branch_id_t>(ids).value_or(branch_id_t{}), bms_chart_content_t{}, source).has_value();
}
auto program_import_t::import_child(import_scope_t parent, const bms_control_node_t& child) -> bool {
    if (child.kind == bms_control_kind_t::raw_statement) return import_statement(parent, child);
    if (child.kind == bms_control_kind_t::if_group) return import_group(parent, child);
    if (child.kind == bms_control_kind_t::skip) return import_skip(parent, child);
    if (child.kind == bms_control_kind_t::set_random || child.kind == bms_control_kind_t::set_switch) {
        const auto value = scope_value(child, std::nullopt);
        return value && append_node(parent, child.id, *value, child).has_value();
    }
    return import_scope(parent, child);
}
auto program_import_t::import_children(import_scope_t parent, const bms_control_node_t& source) -> bool {
    for (const auto& child : source.children)
        if (!import_child(parent, child)) return false;
    const auto has_chart = std::ranges::any_of(nodes[parent.node].children, [&](auto id) {
        const auto found = nodes.begin() + node_indices.at(id.value);
        return std::holds_alternative<bms_chart_content_t>(found->value);
    });
    return has_chart || ensure_chart(parent, source).has_value();
}
} // namespace

auto prepare_bms_program(std::span<const std::byte> bytes, bms_parse_options_t options,
    bms_program_limits_t limits) -> bms_program_import_outcome_t {
    id_allocator_t ids;
    auto source = parse_bms_source(bytes, options, ids);
    bms_program_import_outcome_t result;
    result.value.encoding = std::move(source.value.encoding);
    result.diagnostics = std::move(source.diagnostics);
    if (!result.value.encoding.original_bytes && bytes.size() <= options.max_source_bytes)
        result.value.encoding.original_bytes = std::make_shared<const std::vector<std::byte>>(bytes.begin(), bytes.end());
    if (!source.value.song || !result.ok()) return result;
    // Unclosed/malformed control frames cannot be made into a valid typed graph
    // without repair; keep the complete read-only source instead (T-287).
    if (std::ranges::any_of(result.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == bms_parse_diagnostic_code_t::malformed_control ||
               diagnostic.code == bms_parse_diagnostic_code_t::invalid_number;
    })) return result;
    const auto& chart = source.value.song->charts.front();
    const auto& root = *chart.bms_control_tree;
    program_import_t builder{.source_chart = chart, .options = options, .limits = limits, .result = result, .ids = ids};
    if (!builder.choose_profile(root)) return result;
    builder.nodes.push_back({.id = root.id, .value = bms_root_scope_t{}});
    builder.node_indices.emplace(root.id.value, 0);
    if (!builder.import_children({0, std::nullopt}, root)) return result;
    auto program = bms_program_t::from(std::move(builder.nodes), limits);
    if (!program) {
        builder.diagnose(root, bms_parse_diagnostic_code_t::malformed_control, program.error().message);
        return result;
    }
    std::vector<measure_id_t> measures;
    for (const auto& measure : chart.measures) measures.push_back(measure.id);
    result.value.prepared = bms_prepared_program_t{.program = std::move(*program), .measures = std::move(measures),
        .lane_profile = std::move(builder.profile), .bgm_columns = std::move(builder.columns),
        .resources = std::move(builder.resources), .audio_assets = std::move(builder.audio), .visual_assets = std::move(builder.visual)};
    return result;
}
} // namespace wormedit
