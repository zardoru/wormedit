#include "bms_writer_internal.h"
#include "bms_program_import_internal.h"
#include "bms_timing_directive.h"
#include "bms_channel.h"
#include "text_codec_result.h"
#include <tuple>
#include "../core/checked_math.h"
#include "../core/id_ceiling.h"
#include "../core/text_util.h"
#include "../core/bms_native_content_internal.h"
#include <wormedit/core/rational.h>

#include <map>
#include <set>

namespace wormedit {
namespace {
using bms_writer_detail::fail;
using bms_writer_detail::write_failure_t;
using bms_writer_detail::is_base36_digit;
using bms_writer_detail::base36_id;
using bms_writer_detail::maximum_bms_channel_slots;
using bms_writer_detail::maximum_bms_line_bytes;
using bms_writer_detail::has_line_break_or_nul;
using bms_writer_detail::has_edge_whitespace;
using bms_writer_detail::is_lr2_command;
using bms_writer_detail::encode_bms_text;
using bms_writer_detail::bms_source_t;
using bms_writer_detail::write_retained_bms_source;
using bms_writer_detail::exact_decimal;
using bms_writer_detail::exact_signed_decimal;
using bms_writer_detail::channel_line_t;
using bms_writer_detail::encode_bms_channel_line;
struct program_emission_t {
    const bms_prepared_program_t& prepared;
    const bms_program_write_options_t& options;
    std::string output{"\xef\xbb\xbf"};
    std::size_t lines{}, items{}, content_items{}, text_bytes{};
    std::map<measure_id_t, std::size_t> measures;
    std::map<resource_id_t, const resource_entry_t*> resources;
    std::map<asset_id_t, std::pair<bms_resource_namespace_t, const std::filesystem::path*>> assets;
    std::map<lane_id_t, const lane_definition_t*> lanes;
    std::set<object_id_t> bgm_columns;
    std::map<std::pair<branch_id_t, std::optional<branch_id_t>>, std::size_t> skips;
    std::set<std::pair<measure_id_t, object_id_t>> emitted_bgm;
    std::set<branch_id_t> active_charts, active_scopes, selected_controls;
    std::optional<channel_line_t> pending;
    std::optional<object_id_t> pending_column;
    std::set<rational_t, bool(*)(rational_t, rational_t)> positions{
        [](rational_t a, rational_t b) { return std::tie(a.numerator, a.denominator) < std::tie(b.numerator, b.denominator); }};
    std::size_t maximum_used_measure{};
    bool flatten{};
    bool source_proof{};
    std::vector<bms_write_diagnostic_t>* diagnostics{};
    void warn_loss(std::string message, std::string path);
    std::array<std::set<std::string>, 2> resource_slots;
    std::array<std::set<std::string>, 3> timing_slots;
    std::map<resource_id_t, std::pair<bms_resource_namespace_t, std::string>> native_resources;

    void account_input(std::size_t size);
    void index_inputs();
    void index_catalogs();
    void index_lanes();
    void index_content();
    void account_text(const bms_text_t& value);
    void select_projection();
    void append(std::string_view bytes);
    auto byte_limit() const -> std::size_t;
    void line(std::string_view value);
    void flush();
    void statement(std::string token, std::string value = {}, bool control = false);
    void text(const bms_text_t& value);
    void emit_node(const bms_program_node_t& node, std::size_t depth);
    void emit_children(const bms_program_node_t& node, std::size_t depth);
    void skip_at(branch_id_t scope, std::optional<branch_id_t> after);
    void open(const bms_root_scope_t&) {}
    void open(const bms_bare_scope_t&) {}
    void open(const bms_random_scope_t& value) { statement("RANDOM", std::to_string(value.range), true); }
    void open(const bms_if_scope_t& value) { statement("IF", std::to_string(value.value), true); }
    void open(const bms_else_if_scope_t& value) { statement("ELSEIF", std::to_string(value.value), true); }
    void open(const bms_else_scope_t&) { statement("ELSE", {}, true); }
    void open(const bms_switch_scope_t& value) { statement("SWITCH", std::to_string(value.value), true); }
    void open(const bms_case_scope_t& value) { statement(value.value ? "CASE" : "DEF", value.value ? std::to_string(*value.value) : "", true); }
    void open(const bms_set_random_t& value) { statement("SETRANDOM", std::to_string(value.value), true); }
    void open(const bms_set_switch_t& value) { statement("SETSWITCH", std::to_string(value.value), true); }
    void open(const bms_chart_content_t&) {}
    void open(const bms_text_t&) {}
    void emit_content(const bms_chart_content_t& content);
    void emit(const bms_note_content_t& value);
    void emit(const bms_hold_endpoint_t& value);
    void emit(const bms_bgm_content_t& value);
    void emit(const bms_bga_content_t& value);
    void emit(const bms_timing_definition_t& value);
    void emit(const bms_timing_reference_t& value);
    void emit(const bms_direct_bpm_t& value);
    void emit(const bms_measure_length_t& value);
    void emit(const bms_resource_declaration_t& value);
    void emit(const bms_metadata_declaration_t& value);
    void emit(const bms_native_content_t& value);
    void emit(const bms_text_t& value) { text(value); }
    void native(const note_t& value, const bms_native_content_t& content);
    void native(const bgm_event_t& value, const bms_native_content_t& content);
    void native(const bga_event_t& value, const bms_native_content_t& content);
    void native(const bpm_event_t& value, const bms_native_content_t& content);
    void native(const pause_event_t& value, const bms_native_content_t& content);
    void native(const scroll_event_t& value, const bms_native_content_t& content);
    template<class Value> void native(const Value&, const bms_native_content_t&) {
        fail(bms_write_diagnostic_code_t::unsupported_feature,
            "Native-only scoped content requires a compatible output format.", "$.program.native_content");
    }
    void index_native_resources();
    template<class Value> void reserve_identifiers(const Value& value);
    auto native_identifier(const bms_native_content_t& content, bms_resource_namespace_t name_space) const -> std::string;
    auto allocate_identifier(std::set<std::string>& used) -> std::string;
    void native_timing(bms_program_position_t position, bms_timing_definition_kind_t kind, signed_rational_t value);
    auto merge_bgm_padding(std::size_t measure, object_id_t column, rational_t fraction,
        const std::string& object, std::optional<std::uint64_t> grid) -> bool;
    void retain_bgm_grid(std::optional<std::uint64_t> grid);
    auto channel(lane_id_t lane, note_kind_t kind) const -> std::string;
    auto measure_index(measure_id_t id) -> std::size_t;
    void prepare_line(std::size_t measure, std::string channel, std::optional<object_id_t> column, rational_t fraction);
    void slot(bms_program_position_t position, std::string channel, std::string object,
        std::optional<object_id_t> column = {}, std::optional<std::size_t> structural_slot = {});
    auto build() -> std::string;
    auto number(signed_rational_t value, bool positive = false) const -> std::string;
    auto ratio(rational_t value) const -> std::string;
};

auto identifier(std::string_view value, bool zero = false) -> std::string {
    if (value.size() != 2 || !is_base36_digit(value[0]) || !is_base36_digit(value[1]) || (!zero && value == "00"))
        fail(bms_write_diagnostic_code_t::unresolved_resource, "BMS objects require a canonical two-digit identifier.", "$.program");
    return std::string(value);
}

template<class Value> void program_emission_t::reserve_identifiers(const Value& value) {
    if constexpr (std::same_as<Value, bms_resource_declaration_t>)
        resource_slots[static_cast<std::size_t>(value.name_space)].insert(value.identifier);
    else if constexpr (requires { value.wav; }) {
        if constexpr (std::same_as<decltype(value.wav), std::string>) resource_slots[0].insert(value.wav);
        else if (value.wav) resource_slots[0].insert(*value.wav);
    } else if constexpr (std::same_as<Value, bms_bga_content_t>) resource_slots[1].insert(value.bmp);
    else if constexpr (std::same_as<Value, bms_timing_definition_t>) {
        if (value.identifier) timing_slots[static_cast<std::size_t>(value.kind)].insert(*value.identifier);
    } else if constexpr (std::same_as<Value, bms_timing_reference_t>)
        timing_slots[static_cast<std::size_t>(value.kind)].insert(value.identifier);
    else if constexpr (std::same_as<Value, bms_native_content_t>) {
        if (value.resource_reference()) resource_slots[static_cast<std::size_t>(value.resource_reference()->name_space)]
            .insert(value.resource_reference()->identifier);
    }
}

auto program_emission_t::allocate_identifier(std::set<std::string>& used) -> std::string {
    for (std::size_t index = 1; index < 1296; ++index) {
        auto name = base36_id(index);
        if (used.insert(name).second) return name;
    }
    fail(bms_write_diagnostic_code_t::resource_limit, "No free BMS identifier remains for native content.", "$.program");
}

void program_emission_t::index_native_resources() {
    std::set<resource_id_t> needed;
    for (const auto& node : prepared.program.nodes()) {
        const auto* chart = std::get_if<bms_chart_content_t>(&node.value);
        if (!chart) continue;
        for (const auto& item : chart->items) {
            std::visit([&](const auto& value) { reserve_identifiers(value); }, item.value);
            const auto* native = std::get_if<bms_native_content_t>(&item.value);
            if (!native || native->resource_reference() || (flatten && !active_charts.contains(node.id))) continue;
            if (const auto id = event_resource(native->payload()); id && *id) needed.insert(*id);
        }
    }
    for (const auto id : needed) {
        const auto resource = resources.find(id);
        if (resource == resources.end()) fail(bms_write_diagnostic_code_t::unresolved_resource,
            "Native content references an unknown catalog resource.", "$.program.native_content");
        const auto asset = assets.find(resource->second->asset);
        if (asset == assets.end()) fail(bms_write_diagnostic_code_t::unresolved_resource,
            "Native content references an unknown asset.", "$.program.native_content");
        const auto name_space = asset->second.first;
        native_resources.emplace(id, std::pair{name_space, allocate_identifier(resource_slots[static_cast<std::size_t>(name_space)])});
    }
}

auto program_emission_t::native_identifier(const bms_native_content_t& content,
    bms_resource_namespace_t name_space) const -> std::string {
    if (content.resource_reference()) {
        if (content.resource_reference()->name_space == name_space) return identifier(content.resource_reference()->identifier);
    } else if (const auto resource = event_resource(content.payload()); resource && *resource) {
        const auto found = native_resources.find(*resource);
        if (found != native_resources.end() && found->second.first == name_space) return found->second.second;
    } else return "00";
    fail(bms_write_diagnostic_code_t::unresolved_resource, "Native content has an incompatible resource binding.", "$.program.native_content");
}

void program_emission_t::emit(const bms_native_content_t& value) {
    std::visit([&](const auto& event) { native(event, value); }, value.payload());
}

void program_emission_t::warn_loss(std::string message, std::string path) {
    if (options.loss_policy == loss_policy_t::reject)
        fail(bms_write_diagnostic_code_t::lossy_transform, std::move(message), std::move(path));
    if (!diagnostics) return;
    const auto duplicate = std::ranges::any_of(*diagnostics, [&](const auto& entry) {
        return entry.code == bms_write_diagnostic_code_t::lossy_transform && entry.message == message;
    });
    if (!duplicate) diagnostics->push_back({diagnostic_severity_t::warning,
        bms_write_diagnostic_code_t::lossy_transform, std::move(message), std::move(path)});
}

void program_emission_t::native(const note_t& value, const bms_native_content_t& content) {
    if (value.sound_channel_event) fail(bms_write_diagnostic_code_t::unsupported_feature,
        "Native stem points require a compatible output format.", "$.program.native_content");
    const auto wav = native_identifier(content, bms_resource_namespace_t::wav);
    const auto attributes = bms_note_attributes_t::from(value);
    if (value.end) {
        emit(bms_hold_endpoint_t{.position = *content.position(), .lane = value.lane, .wav = wav, .attributes = attributes});
        emit(bms_hold_endpoint_t{.position = *content.end_position(), .lane = value.lane, .wav = wav});
    } else emit(bms_note_content_t{.position = *content.position(), .lane = value.lane, .kind = value.kind,
        .wav = wav == "00" ? std::nullopt : std::optional{wav}, .attributes = attributes});
}
void program_emission_t::native(const bgm_event_t& value, const bms_native_content_t& content) {
    if (value.sound_channel_event)
        fail(bms_write_diagnostic_code_t::unsupported_feature, "Native BGM stem points require a compatible output format.", "$.program.native_content");
    if (value.end) warn_loss("BMS background sounds retain their heads and omit native span tails.", "$.program.BGM.end");
    if (!bgm_columns.contains(value.source_lane)) fail(bms_write_diagnostic_code_t::invalid_document,
        "Native BGM content has no shared column.", "$.program.native_content");
    slot(*content.position(), "01", native_identifier(content, bms_resource_namespace_t::wav), value.source_lane);
}
void program_emission_t::native(const bga_event_t& value, const bms_native_content_t& content) {
    emit(bms_bga_content_t{.position = *content.position(), .kind = value.kind,
        .bmp = native_identifier(content, bms_resource_namespace_t::bmp)});
}
void program_emission_t::native_timing(bms_program_position_t position, bms_timing_definition_kind_t kind,
    signed_rational_t value) {
    const auto name = allocate_identifier(timing_slots[static_cast<std::size_t>(kind)]);
    emit(bms_timing_definition_t{.kind = kind, .identifier = name, .value = value});
    emit(bms_timing_reference_t{.position = position, .kind = kind, .identifier = name});
}
void program_emission_t::native(const bpm_event_t& value, const bms_native_content_t& content) {
    native_timing(*content.position(), bms_timing_definition_kind_t::bpm, value.beats_per_minute);
}
void program_emission_t::native(const pause_event_t& value, const bms_native_content_t& content) {
    if (value.kind != pause_kind_t::stop || value.source_unit != pause_unit_t::bms_stop_ticks)
        fail(bms_write_diagnostic_code_t::unsupported_feature, "BMS pauses require STOP ticks.", "$.program.native_content");
    native_timing(*content.position(), bms_timing_definition_kind_t::stop, value.duration);
}
void program_emission_t::native(const scroll_event_t& value, const bms_native_content_t& content) {
    native_timing(*content.position(), bms_timing_definition_kind_t::scroll, value.multiplier);
}

auto timing_directive(bms_timing_definition_kind_t kind) -> const bms_timing_directive_t& {
    const auto found = std::ranges::find(bms_timing_directives, kind, &bms_timing_directive_t::kind);
    if (found == bms_timing_directives.end()) fail(bms_write_diagnostic_code_t::unsupported_feature, "Unknown timing kind.", "$.program");
    return *found;
}

void program_emission_t::account_input(std::size_t size) {
    if (!checked_math::account_size(items, size, options.maximum_input_objects))
        fail(bms_write_diagnostic_code_t::resource_limit, "Scoped serialization input limit exceeded.", "$.program");
}

void program_emission_t::index_inputs() {
    if (prepared.program.nodes().size() > options.program_limits.maximum_nodes || prepared.measures.empty() || prepared.measures.size() > 1000)
        fail(bms_write_diagnostic_code_t::resource_limit, "Scoped node/measure limits exceeded.", "$.program");
    account_input(prepared.resources.size());
    account_input(prepared.audio_assets.size());
    account_input(prepared.visual_assets.size());
    for (const auto id : prepared.measures)
        if (!id || !measures.emplace(id, measures.size()).second)
            fail(bms_write_diagnostic_code_t::invalid_geometry, "Shared measure identities are ambiguous.", "$.program.measures");
    index_catalogs();
    account_input(prepared.bgm_columns.size());
    for (const auto& column : prepared.bgm_columns)
        if (!column.id || !bgm_columns.insert(column.id).second)
            fail(bms_write_diagnostic_code_t::invalid_document, "BGM column identities are ambiguous.", "$.bgm_columns");
    index_lanes();
    index_content();
}

void program_emission_t::index_content() {
    for (const auto& node : prepared.program.nodes()) {
        if (const auto* content = std::get_if<bms_chart_content_t>(&node.value)) {
            account_input(content->items.size());
            if (!checked_math::account_size(content_items, content->items.size(), options.program_limits.maximum_content_items))
                fail(bms_write_diagnostic_code_t::resource_limit, "Scoped content item limit exceeded.", "$.program");
            for (const auto& item : content->items)
                if (const auto* text = std::get_if<bms_text_t>(&item.value)) account_text(*text);
        }
        if (const auto* text = std::get_if<bms_text_t>(&node.value))
            account_text(*text);
        if (const auto* arm = std::get_if<bms_case_scope_t>(&node.value)) {
            account_input(arm->skips.size());
            if (!checked_math::account_size(content_items, arm->skips.size(), options.program_limits.maximum_content_items))
                fail(bms_write_diagnostic_code_t::resource_limit, "Scoped control behavior limit exceeded.", "$.program");
            for (const auto& skip : arm->skips) ++skips[{skip.scope, skip.after_child}];
        }
    }
}

void program_emission_t::account_text(const bms_text_t& value) {
    if (!checked_math::account_size(text_bytes, value.bytes.size(), options.program_limits.maximum_text_bytes))
        fail(bms_write_diagnostic_code_t::resource_limit, "Scoped Text byte limit exceeded.", "$.program.Text");
}

void program_emission_t::index_catalogs() {
    for (const auto& entry : prepared.resources)
        if (!entry.id || !resources.emplace(entry.id, &entry).second)
            fail(bms_write_diagnostic_code_t::invalid_document, "Shared resource identities are ambiguous.", "$.resources");
    for (const auto& asset : prepared.audio_assets)
        if (!asset.id || !assets.emplace(asset.id, std::pair{bms_resource_namespace_t::wav, &asset.source_path}).second)
            fail(bms_write_diagnostic_code_t::invalid_document, "Shared asset identities are ambiguous.", "$.audio_assets");
    for (const auto& asset : prepared.visual_assets)
        if (!asset.id || !assets.emplace(asset.id, std::pair{bms_resource_namespace_t::bmp, &asset.source_path}).second)
            fail(bms_write_diagnostic_code_t::invalid_document, "Shared asset identities are ambiguous.", "$.visual_assets");
}

void program_emission_t::index_lanes() {
    if (!prepared.lane_profile) return;
    const auto& layout = prepared.lane_profile->layout;
    account_input(layout.lanes.size());
    std::set<std::string> used;
    for (const auto& lane : layout.lanes) {
        const auto channels = lane.bms_channels.normalized();
        if (!channels || *channels != lane.bms_channels || !lane.id || !lanes.emplace(lane.id, &lane).second)
            fail(bms_write_diagnostic_code_t::missing_profile, "Lane channels or identities are invalid.", "$.lane_profile");
        for (const auto* channel : {&channels->visible, &channels->invisible, &channels->long_note, &channels->mine})
            if (*channel && !used.insert(**channel).second)
                fail(bms_write_diagnostic_code_t::missing_profile, "Lane channels overlap.", "$.lane_profile");
    }
}

void program_emission_t::select_projection() {
    flatten = options.format.branch_policy == bms_branch_policy_t::flatten_active;
    if (!flatten) return;
    const auto projection = bms_program_projection_t::from(prepared, options.evaluation);
    if (!projection.value) fail(bms_write_diagnostic_code_t::unsupported_branch_policy,
        "Flattening requires an accepted evaluation environment.", "$.evaluation");
    for (const auto id : projection.value->active_charts()) {
        active_charts.insert(id);
        active_scopes.insert(*prepared.program.parent(id));
    }
    for (const auto& choice : projection.value->selections()) selected_controls.insert(choice.branch);
}

auto program_emission_t::byte_limit() const -> std::size_t {
    if (!source_proof) return options.maximum_output_bytes;
    // Verification is not an output artifact. Legacy single-byte text may grow
    // threefold in UTF-8; exact fraction signatures also need formatting slack.
    return checked_math::checked_multiply(std::max<std::size_t>(16, options.maximum_output_bytes), 4).value_or(SIZE_MAX);
}

void program_emission_t::append(std::string_view bytes) {
    if (output.size() > byte_limit() || bytes.size() > byte_limit() - output.size())
        fail(bms_write_diagnostic_code_t::resource_limit, "Scoped BMS output byte limit exceeded.", "$.output");
    output += bytes;
}
void program_emission_t::line(std::string_view value) {
    if (++lines > options.maximum_lines || (!source_proof && value.size() > maximum_bms_line_bytes))
        fail(bms_write_diagnostic_code_t::resource_limit, "Scoped BMS output line limit exceeded.", "$.output");
    if (output.size() > 3 && output.back() != '\n' && output.back() != '\r') append("\n");
    append(value);
    append("\n");
}
void program_emission_t::flush() {
    if (!pending) return;
    line(encode_bms_channel_line(*pending, options.format));
    pending.reset();
    pending_column.reset();
    positions.clear();
}
void program_emission_t::statement(std::string token, std::string value, bool control) {
    flush();
    if (options.format.profile == bms_export_profile_t::lr2_compatible && !control && !is_lr2_command(token))
        fail(bms_write_diagnostic_code_t::lossy_transform, "LR2 cannot represent #" + token, "$.program");
    if (has_line_break_or_nul(value) || has_edge_whitespace(value) || !text_util::is_valid_utf8(value))
        fail(bms_write_diagnostic_code_t::unrepresentable_text, "BMS values require valid single-line text without edge whitespace.", "$.program");
    line("#" + token + (value.empty() ? "" : " " + value));
}

void program_emission_t::text(const bms_text_t& value) {
    flush();
    auto decoded = validate_bms_program_text(value, source_proof ? options.maximum_output_bytes : maximum_bms_line_bytes);
    if (!decoded) {
        const auto code = decoded.error().code == failure_code_t::invalid_request ? bms_write_diagnostic_code_t::invalid_document :
            decoded.error().code == failure_code_t::capacity_exceeded ? bms_write_diagnostic_code_t::resource_limit :
            bms_write_diagnostic_code_t::unrepresentable_text;
        fail(code, decoded.error().message, "$.program.Text");
    }
    std::size_t start{};
    while (start < decoded->size()) {
        auto end = decoded->find_first_of("\r\n", start);
        if (end == std::string::npos) end = decoded->size();
        const auto raw = std::string_view(*decoded).substr(start, end - start);
        const auto statement = bms_source_statement_t::from(raw);
        const auto& token = statement.token;
        if (options.format.profile == bms_export_profile_t::lr2_compatible && !is_lr2_command(token))
            fail(bms_write_diagnostic_code_t::lossy_transform, "LR2 cannot represent this Text command.", "$.program.Text");
        if (++lines > options.maximum_lines)
            fail(bms_write_diagnostic_code_t::resource_limit, "Scoped Text line limit exceeded.", "$.program.Text");
        start = end;
        if (start < decoded->size() && (*decoded)[start++] == '\r' && start < decoded->size() && (*decoded)[start] == '\n') ++start;
    }
    if (!decoded->empty() && output.size() > 3 && output.back() != '\r' && output.back() != '\n') append("\n");
    append(*decoded);
}

void program_emission_t::skip_at(branch_id_t scope, std::optional<branch_id_t> after) {
    if (flatten) return;
    if (const auto found = skips.find({scope, after}); found != skips.end())
        for (std::size_t index = 0; index < found->second; ++index) statement("SKIP");
}

void program_emission_t::emit_children(const bms_program_node_t& node, std::size_t depth) {
    skip_at(node.id, {});
    for (std::size_t index = 0; index < node.children.size(); ++index) {
        const auto& child = *prepared.program.find(node.children[index]);
        emit_node(child, depth + 1);
        if (!flatten && child.is_if_arm() && (index + 1 == node.children.size() || !prepared.program.find(node.children[index + 1])->is_if_continuation())) statement("ENDIF", {}, true);
        skip_at(node.id, child.id);
    }
}
void program_emission_t::emit_node(const bms_program_node_t& node, std::size_t depth) {
    if (depth > options.program_limits.maximum_depth)
        fail(bms_write_diagnostic_code_t::resource_limit, "Scoped serialization nesting limit exceeded.", "$.program");
    if (!flatten && options.format.profile == bms_export_profile_t::lr2_compatible &&
        (std::holds_alternative<bms_switch_scope_t>(node.value) || std::holds_alternative<bms_set_switch_t>(node.value) || std::holds_alternative<bms_case_scope_t>(node.value)))
        fail(bms_write_diagnostic_code_t::lossy_transform, "LR2 cannot represent switch control flow.", "$.program");
    const auto selector = std::holds_alternative<bms_random_scope_t>(node.value) || std::holds_alternative<bms_switch_scope_t>(node.value);
    if (flatten && selector && !selected_controls.contains(node.id))
        fail(bms_write_diagnostic_code_t::unsupported_branch_policy, "A reachable selector has no resolved choice.", "$.evaluation");
    if (flatten && node.is_scope() && node.id != prepared.program.root() && !selector && !active_scopes.contains(node.id)) return;
    if (const auto* content = std::get_if<bms_chart_content_t>(&node.value)) {
        if (!flatten || active_charts.contains(node.id)) emit_content(*content);
        return;
    }
    if (const auto* opaque = std::get_if<bms_text_t>(&node.value)) { text(*opaque); return; }
    if (!flatten) std::visit([&](const auto& value) { open(value); }, node.value);
    emit_children(node, depth);
    if (!flatten && std::holds_alternative<bms_random_scope_t>(node.value)) statement("ENDRANDOM", {}, true);
    if (!flatten && std::holds_alternative<bms_switch_scope_t>(node.value)) statement("ENDSW", {}, true);
}
void program_emission_t::emit_content(const bms_chart_content_t& content) {
    for (const auto& item : content.items) std::visit([&](const auto& value) { emit(value); }, item.value);
    // Chart boundaries must not collapse independently authored channel lines.
    flush();
}

auto program_emission_t::measure_index(measure_id_t id) -> std::size_t {
    const auto found = measures.find(id);
    if (found == measures.end()) fail(bms_write_diagnostic_code_t::invalid_geometry, "Content references an unknown shared measure.", "$.program.position");
    maximum_used_measure = std::max(maximum_used_measure, found->second);
    return found->second;
}
namespace {
auto bgm_source_grid(rational_t fraction, std::optional<std::size_t> slot) -> std::optional<std::uint64_t> {
    if (!slot) return {};
    if (!fraction.numerator) {
        if (*slot) fail(bms_write_diagnostic_code_t::invalid_geometry, "BGM zero position requires slot zero.", "$.program.BGM");
        return {};
    }
    const auto grid = checked_math::checked_multiply(*slot, fraction.denominator);
    if (!grid || !*grid || *grid % fraction.numerator)
        fail(bms_write_diagnostic_code_t::invalid_geometry, "BGM structural slot disagrees with its exact position.", "$.program.BGM");
    return *grid / fraction.numerator;
}
}
void program_emission_t::retain_bgm_grid(std::optional<std::uint64_t> grid) {
    if (!grid) return;
    if (pending->minimum_slots != 1 && pending->minimum_slots != *grid)
        fail(bms_write_diagnostic_code_t::invalid_geometry, "BGM slots disagree on their source grid.", "$.program.BGM");
    pending->minimum_slots = *grid;
}
auto program_emission_t::merge_bgm_padding(std::size_t measure, object_id_t column, rational_t fraction,
    const std::string& object, std::optional<std::uint64_t> grid) -> bool {
    if (!pending || pending->measure != measure || pending_column != column) return false;
    const auto same = std::ranges::find_if(pending->notes, [&](const auto& note) {
        return note.numerator == fraction.numerator && note.denominator == fraction.denominator;
    });
    if (same == pending->notes.end() || (same->object_id != "00" && object != "00")) return false;
    retain_bgm_grid(grid);
    if (object != "00") same->object_id = object;
    return true;
}

void program_emission_t::slot(bms_program_position_t position, std::string channel, std::string object,
    std::optional<object_id_t> column, std::optional<std::size_t> structural_slot) {
    if (options.format.profile == bms_export_profile_t::lr2_compatible && !is_lr2_command("000" + channel))
        fail(bms_write_diagnostic_code_t::lossy_transform, "LR2 cannot represent this channel.", "$.program");
    const auto measure = measure_index(position.measure);
    if (!position.fraction.denominator || position.fraction.numerator >= position.fraction.denominator)
        fail(bms_write_diagnostic_code_t::invalid_geometry, "BMS position is outside its shared measure.", "$.program.position");
    const auto fraction = reduce_fraction(position.fraction);
    const auto grid = bgm_source_grid(fraction, structural_slot);
    if (column && merge_bgm_padding(measure, *column, fraction, object, grid)) return;
    prepare_line(measure, std::move(channel), column, fraction);
    retain_bgm_grid(grid);
    pending->notes.push_back({fraction.numerator, fraction.denominator, std::move(object), "$.program"});
    positions.insert(fraction);
}

void program_emission_t::prepare_line(std::size_t measure, std::string channel,
    std::optional<object_id_t> column, rational_t fraction) {
    if (pending && (pending->measure != measure || pending->channel != channel || pending_column != column || positions.contains(fraction))) flush();
    if (pending && !column) {
        const auto grid = checked_math::checked_lcm(pending->minimum_slots, fraction.denominator);
        if (!grid || *grid > std::min(options.format.max_slots_per_line, maximum_bms_channel_slots)) flush();
    }
    if (!pending) {
        if (column && !emitted_bgm.emplace(prepared.measures[measure], *column).second)
            fail(bms_write_diagnostic_code_t::invalid_document, "A BGM line is fragmented across content boundaries.", "$.program.BGM");
        pending = channel_line_t{.measure = measure, .channel = std::move(channel)};
        pending_column = column;
    }
    if (!column) {
        const auto grid = checked_math::checked_lcm(pending->minimum_slots, fraction.denominator);
        if (!grid) fail(bms_write_diagnostic_code_t::resource_limit, "Channel subdivision overflows.", "$.program");
        pending->minimum_slots = *grid;
    }
}

auto program_emission_t::channel(lane_id_t id, note_kind_t kind) const -> std::string {
    const auto found = lanes.find(id);
    if (found == lanes.end()) fail(bms_write_diagnostic_code_t::missing_profile, "Note references an unknown lane.", "$.program.notes");
    const auto& map = found->second->bms_channels;
    std::optional<std::string> value;
    switch (kind) {
    case note_kind_t::tap: value = map.visible; break;
    case note_kind_t::hold: value = map.long_note; break;
    case note_kind_t::invisible: value = map.invisible; break;
    case note_kind_t::mine: value = map.mine; break;
    case note_kind_t::roll: case note_kind_t::lift: case note_kind_t::fake: break;
    }
    if (!value) fail(bms_write_diagnostic_code_t::unsupported_feature, "Lane has no channel for this note kind.", "$.program.notes");
    return identifier(*value);
}
void program_emission_t::emit(const bms_note_content_t& value) {
    if (value.attributes.osu_hitsample) warn_loss("BMS omits osu hitsample annotations.", "$.program.notes.attributes");
    if (value.kind == note_kind_t::hold) fail(bms_write_diagnostic_code_t::invalid_document, "Holds require separate authored endpoints.", "$.program.notes");
    std::string object;
    if (value.kind == note_kind_t::mine) {
        if (value.wav || !value.mine_value || !*value.mine_value || *value.mine_value >= 36 * 36)
            fail(bms_write_diagnostic_code_t::unrepresentable_number, "Mine value is not a nonzero base-36 pair.", "$.program.notes");
        object = base36_id(*value.mine_value);
    } else {
        if (!value.wav || value.mine_value) fail(bms_write_diagnostic_code_t::unresolved_resource, "A note requires its authored WAV identifier.", "$.program.notes");
        object = identifier(*value.wav);
    }
    slot(value.position, channel(value.lane, value.kind), std::move(object));
}
void program_emission_t::emit(const bms_hold_endpoint_t& value) {
    if (value.attributes.osu_hitsample) warn_loss("BMS omits osu hitsample annotations.", "$.program.notes.attributes");
    slot(value.position, channel(value.lane, note_kind_t::hold), identifier(value.wav));
}
void program_emission_t::emit(const bms_bgm_content_t& value) {
    if (!value.column || !bgm_columns.contains(value.column)) fail(bms_write_diagnostic_code_t::invalid_document, "BGM requires a line identity.", "$.program.BGM");
    slot(value.position, "01", value.wav ? identifier(*value.wav) : "00", value.column, value.structural_slot);
}
void program_emission_t::emit(const bms_bga_content_t& value) {
    const auto* channel = bms_bga_channel_t::from(value.kind);
    if (!channel) fail(bms_write_diagnostic_code_t::unsupported_feature, "BGA kind has no supported BMS channel.", "$.program.BGA");
    slot(value.position, std::string(channel->channel), identifier(value.bmp));
}
void program_emission_t::emit(const bms_timing_definition_t& value) {
    const auto& directive = timing_directive(value.kind);
    if (!value.identifier && value.kind != bms_timing_definition_kind_t::bpm)
        fail(bms_write_diagnostic_code_t::invalid_document, "Only initial BPM can omit a definition identifier.", "$.program.timing");
    statement(std::string(directive.name) + (value.identifier ? identifier(*value.identifier, true) : ""),
        number(value.value, value.kind != bms_timing_definition_kind_t::scroll));
}
void program_emission_t::emit(const bms_timing_reference_t& value) {
    slot(value.position, std::string(timing_directive(value.kind).channel), identifier(value.identifier));
}
void program_emission_t::emit(const bms_direct_bpm_t& value) {
    if (value.value.denominator != 1 || value.value.numerator < 1 || value.value.numerator > 255)
        fail(bms_write_diagnostic_code_t::unrepresentable_number, "Direct BPM requires a positive hexadecimal byte.", "$.program.timing");
    constexpr std::string_view digits = "0123456789ABCDEF";
    std::string object(2, '0');
    object[0] = digits[value.value.numerator / 16];
    object[1] = digits[value.value.numerator % 16];
    slot(value.position, "03", std::move(object));
}
void program_emission_t::emit(const bms_measure_length_t& value) {
    const auto index = measure_index(value.measure);
    const auto prefix = index < 10 ? "00" : index < 100 ? "0" : "";
    flush();
    line("#" + std::string(prefix) + std::to_string(index) + "02:" + ratio(value.length));
}
void program_emission_t::emit(const bms_resource_declaration_t& value) {
    const auto resource = resources.find(value.resource);
    if (resource == resources.end()) fail(bms_write_diagnostic_code_t::unresolved_resource, "Declaration references a missing resource.", "$.program.resources");
    const auto asset = assets.find(resource->second->asset);
    if (asset == assets.end() || asset->second.first != value.name_space || asset->second.second->empty())
        fail(bms_write_diagnostic_code_t::unresolved_resource, "Declaration references a missing or wrong-kind asset path.", "$.program.resources");
    statement((value.name_space == bms_resource_namespace_t::wav ? "WAV" : "BMP") + identifier(value.identifier, true), text_util::path_utf8(*asset->second.second));
}
void program_emission_t::emit(const bms_metadata_declaration_t& value) {
    const auto definition = std::ranges::find(bms_metadata_definitions, value.field, &bms_metadata_definition_t::field);
    if (definition == bms_metadata_definitions.end()) fail(bms_write_diagnostic_code_t::unsupported_feature, "Unknown metadata field.", "$.program.metadata");
    if (value.field == bms_metadata_field_t::offset || value.field == bms_metadata_field_t::player) {
        const auto* number = std::get_if<signed_rational_t>(&value.value);
        if (!number) fail(bms_write_diagnostic_code_t::invalid_document, "Numeric metadata requires an exact number.", "$.program.metadata");
        if (value.field == bms_metadata_field_t::player && (number->denominator != 1 || number->numerator < 0 || static_cast<std::uint64_t>(number->numerator) > std::numeric_limits<unsigned int>::max()))
            fail(bms_write_diagnostic_code_t::unrepresentable_number, "PLAYER requires a supported unsigned integer.", "$.program.metadata");
        statement(std::string(definition->command), this->number(*number));
    } else {
        const auto* text = std::get_if<std::string>(&value.value);
        if (!text) fail(bms_write_diagnostic_code_t::invalid_document, "Text metadata requires a string.", "$.program.metadata");
        statement(std::string(definition->command), *text);
    }
}

auto program_emission_t::number(signed_rational_t value, bool positive) const -> std::string {
    if (!value.denominator || (positive && value.numerator <= 0))
        fail(bms_write_diagnostic_code_t::unrepresentable_number, "Invalid exact BMS numeric value.", "$.program");
    if (source_proof) return std::to_string(value.numerator) + "/" + std::to_string(value.denominator);
    return positive ? exact_decimal(value, "$.program") : exact_signed_decimal(value, "$.program");
}
auto program_emission_t::ratio(rational_t value) const -> std::string {
    if (!value.numerator || !value.denominator)
        fail(bms_write_diagnostic_code_t::unrepresentable_number, "Invalid positive BMS ratio.", "$.program");
    if (source_proof) return std::to_string(value.numerator) + "/" + std::to_string(value.denominator);
    return exact_decimal(value.numerator, value.denominator, "$.program.measure_length");
}

auto program_emission_t::build() -> std::string {
    if (options.format.chart || options.format.lane_profile_id)
        fail(bms_write_diagnostic_code_t::invalid_document, "Prepared export uses its own difficulty and lane profile.", "$.options");
    if (options.format.profile == bms_export_profile_t::lr2_compatible && options.format.output_encoding && *options.format.output_encoding != text_encoding_t::cp932)
        fail(bms_write_diagnostic_code_t::unsupported_encoding, "LR2 requires CP932 output.", "$.output_encoding");
    index_inputs();
    select_projection();
    index_native_resources();
    for (const auto& [resource, binding] : native_resources)
        emit(bms_resource_declaration_t{.name_space = binding.first, .identifier = binding.second, .resource = resource});
    emit_node(*prepared.program.find(prepared.program.root()), 0);
    flush();
    // Zero direct-BPM padding preserves an otherwise unreferenced trailing
    // measure without inventing a semantic declaration or a BGM column.
    if (maximum_used_measure + 1 < prepared.measures.size()) {
        channel_line_t extent{.measure = prepared.measures.size() - 1, .channel = "03"};
        line(encode_bms_channel_line(extent, options.format));
    }
    if (output.size() > byte_limit()) fail(bms_write_diagnostic_code_t::resource_limit, "Scoped BMS output byte limit exceeded.", "$.output");
    return std::move(output);
}

void validate_output_bounds(std::string_view bytes, const bms_program_write_options_t& options) {
    if (bytes.size() > options.maximum_output_bytes)
        fail(bms_write_diagnostic_code_t::resource_limit, "Encoded BMS output byte limit exceeded.", "$.output");
    if (bytes.starts_with("\xef\xbb\xbf")) bytes.remove_prefix(3);
    std::size_t start{}, lines{};
    while (start < bytes.size()) {
        auto end = bytes.find_first_of("\r\n", start);
        if (end == std::string_view::npos) end = bytes.size();
        if (++lines > options.maximum_lines || end - start > maximum_bms_line_bytes)
            fail(bms_write_diagnostic_code_t::resource_limit, "Encoded BMS output line limit exceeded.", "$.output");
        start = end;
        if (start < bytes.size() && bytes[start++] == '\r' && start < bytes.size() && bytes[start] == '\n') ++start;
    }
}

auto canonical_source_program(bms_prepared_program_t prepared) -> std::optional<bms_prepared_program_t> {
    prepared.settings = {};
    std::unordered_map<std::uint64_t, std::uint64_t> ids;
    std::uint64_t next{1};
    auto reserve = [&](object_id_t id) { return ids.emplace(id.value, next++).second; };
    if (!id_declarations_t<object_id_t>::visit(prepared, reserve)) return {};
    auto remapped = prepared.with_remapped_ids(ids);
    if (!remapped) return {};
    return std::move(*remapped);
}
auto source_semantics_match(const bms_prepared_program_t& current, const bms_prepared_program_t& imported) -> bool {
    const auto left = canonical_source_program(current);
    const auto right = canonical_source_program(imported);
    if (!left || !right || left->program != right->program || left->measures != right->measures ||
        left->lane_profile != right->lane_profile || left->bgm_columns != right->bgm_columns ||
        left->semantic_layers != right->semantic_layers || left->sound_layer_defaults != right->sound_layer_defaults ||
        left->resources.size() != right->resources.size() || left->audio_assets.size() != right->audio_assets.size() ||
        left->visual_assets.size() != right->visual_assets.size()) return false;
    for (std::size_t index = 0; index < left->resources.size(); ++index)
        if (left->resources[index].asset != right->resources[index].asset ||
            left->resources[index].name != right->resources[index].name) return false;
    for (std::size_t index = 0; index < left->audio_assets.size(); ++index)
        if (left->audio_assets[index].source_path != right->audio_assets[index].source_path) return false;
    for (std::size_t index = 0; index < left->visual_assets.size(); ++index)
        if (left->visual_assets[index].source_path != right->visual_assets[index].source_path ||
            left->visual_assets[index].kind != right->visual_assets[index].kind) return false;
    return true;
}
template<class Asset>
void apply_resolution_evidence(std::vector<Asset>& assets, const std::vector<bms_asset_resolution_t>& records) {
    for (auto& asset : assets) {
        std::optional<std::filesystem::path> resolved;
        bool ambiguous{};
        for (const auto& record : records) {
            if (record.authored != asset.source_path) continue;
            if (resolved && *resolved != record.resolved) ambiguous = true;
            resolved = record.resolved;
        }
        if (resolved && !ambiguous) asset.source_path = *resolved;
    }
}

auto verified_source(const bms_prepared_program_t& prepared, const bms_program_write_options_t& options,
    std::optional<std::string_view> normalized) -> std::optional<bms_source_t> {
    if (!options.can_verify_source()) return {};
    const auto& source = *options.source;
    if (source.original_bytes->size() > options.maximum_output_bytes) return {};
    bms_parse_options_t parse_options{.encoding_override = source.user_override.value_or(source.detected),
        .max_source_bytes = options.maximum_output_bytes, .max_lines = options.maximum_lines};
    parse_options.authored_lane_profile = prepared.lane_profile;
    auto imported = prepare_bms_program(*source.original_bytes, parse_options, options.program_limits);
    if (!imported.value.prepared || !imported.ok()) return {};
    apply_resolution_evidence(imported.value.prepared->audio_assets, prepared.resolved_asset_paths);
    apply_resolution_evidence(imported.value.prepared->visual_assets, prepared.resolved_asset_paths);
    if (!normalized) {
        if (source_semantics_match(prepared, *imported.value.prepared))
            return bms_source_t{source.original_bytes, *parse_options.encoding_override};
        return {};
    }
    try {
        program_emission_t original{.prepared = *imported.value.prepared, .options = options, .source_proof = true};
        if (original.build() == *normalized) return bms_source_t{source.original_bytes, *parse_options.encoding_override};
    } catch (const write_failure_t&) { return {}; }
    return {};
}
} // namespace

auto write_bms_program(const bms_prepared_program_t& prepared, bms_program_write_options_t options) -> bms_write_outcome_t {
    bms_write_outcome_t result;
    try {
        const auto has_control = prepared.program.has_control_flow();
        if (has_control && options.format.branch_policy == bms_branch_policy_t::flatten_active) {
            if (options.loss_policy == loss_policy_t::reject) fail(bms_write_diagnostic_code_t::lossy_transform, "Flattening discards control flow and inactive content.", "$.program");
            result.diagnostics.push_back({options.loss_policy == loss_policy_t::allow ? diagnostic_severity_t::info : diagnostic_severity_t::warning,
                bms_write_diagnostic_code_t::lossy_transform, "Flattening discards control flow and inactive content.", "$.program"});
        }
        // This signature uses exact fractions rather than output decimals. It
        // verifies unchanged backing even for a recovered nonterminating ratio;
        // signature bytes are never returned as an export artifact.
        if (options.can_verify_source() && options.source->original_bytes->size() <= options.maximum_output_bytes) {
            program_emission_t proof{.prepared = prepared, .options = options, .source_proof = true, .diagnostics = &result.diagnostics};
            try {
                const auto signature = proof.build();
                if (const auto source = verified_source(prepared, options, signature))
                    result.value = write_retained_bms_source(*source, options.format);
            } catch (const write_failure_t& failure) {
                // Fragmented BGM source lines can be valid even when they cannot
                // be normalized into a single native column without changing voice.
                if (failure.code != bms_write_diagnostic_code_t::duplicate_event) throw;
                const auto source = verified_source(prepared, options, std::nullopt);
                if (!source) throw;
                result.value = write_retained_bms_source(*source, options.format);
            }
        }
        if (!result.value) {
            program_emission_t emission{.prepared = prepared, .options = options, .diagnostics = &result.diagnostics};
            result.value = encode_bms_text(emission.build(), options.format.output_encoding.value_or(
                options.format.profile == bms_export_profile_t::beatoraja ? text_encoding_t::utf8 : text_encoding_t::cp932));
        }
        validate_output_bounds(*result.value, options);
    } catch (const write_failure_t& failure) {
        result.value.reset();
        result.diagnostics.push_back({diagnostic_severity_t::error, failure.code, failure.message, failure.path});
    }
    return result;
}
} // namespace wormedit

namespace wormedit {
namespace {
auto has_modeled_text_statement(std::string_view raw) -> bool {
    const auto statement = bms_source_statement_t::from(raw);
    const auto& token = statement.token;
    const auto resource = token.size() == 5 && (token.starts_with("WAV") || token.starts_with("BMP"));
    return token == "BPM" || (statement.measure_channel && statement.measure_channel->second == "02") ||
        statement.is_control() || statement.is_builtin_channel() || statement.is_playable() || resource ||
        bms_metadata_definition_t::from(token) || bms_timing_directive_t::from(token);
}
} // namespace
auto validate_bms_program_text(const bms_text_t& text, std::size_t maximum_line_bytes) -> result_t<std::string, error_t<failure_code_t>> {
    if (text.bytes.size() > bms_program_limits_t{}.maximum_text_bytes)
        return std::unexpected(error_t{failure_code_t::capacity_exceeded, "Text exceeds the byte limit."});
    auto decoded = decode_bms_text(text.bytes, text.encoding);
    if (!decoded || !text_util::is_valid_utf8(*decoded) || decoded->find('\0') != std::string::npos)
        return std::unexpected(error_t{failure_code_t::invalid_data, "Scoped Text cannot be decoded losslessly."});
    std::size_t start{};
    while (start < decoded->size()) {
        auto end = decoded->find_first_of("\r\n", start);
        if (end == std::string::npos) end = decoded->size();
        const auto raw = std::string_view(*decoded).substr(start, end - start);
        if (has_modeled_text_statement(raw))
            return std::unexpected(error_t{failure_code_t::invalid_request, "Modeled/control commands require semantic nodes, not Text."});
        if (raw.size() > maximum_line_bytes)
            return std::unexpected(error_t{failure_code_t::capacity_exceeded, "Scoped Text line limit exceeded."});
        start = end;
        if (start < decoded->size() && (*decoded)[start++] == '\r' && start < decoded->size() && (*decoded)[start] == '\n') ++start;
    }
    return std::move(*decoded);
}
} // namespace wormedit
