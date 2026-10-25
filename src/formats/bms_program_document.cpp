#include "bms_program_import_internal.h"
#include "bms_writer_internal.h"
#include "../core/id_ceiling.h"
#include <wormedit/core/bms_program_edit.h>
#include <wormedit/core/metadata_inheritance.h>
#include <wormedit/formats/native_project.h>

namespace wormedit {
namespace {
void import_error(bms_parse_outcome_t& result, std::string message) {
    result.diagnostics.push_back({.severity = diagnostic_severity_t::error,
        .code = bms_parse_diagnostic_code_t::unsupported_channel, .message = std::move(message)});
}

auto has_common_metadata(const bms_program_t& program, bms_metadata_field_t field) -> bool {
    for (const auto child : program.find(program.root())->children) {
        const auto* chart = std::get_if<bms_chart_content_t>(&program.find(child)->value);
        if (!chart) continue;
        for (const auto& item : chart->items)
            if (const auto* value = std::get_if<bms_metadata_declaration_t>(&item.value); value && value->field == field)
                return true;
    }
    return false;
}
void add_export_metadata(bms_prepared_program_t& prepared, bms_chart_content_t content,
    bool before, std::uint64_t& next) {
    if (content.items.empty()) return;
    const auto& children = prepared.program.find(prepared.program.root())->children;
    const auto after = before ? std::optional<branch_id_t>{} : std::optional{children.back()};
    auto updated = prepared.program.with_node(prepared.program.root(), after,
        {.id = branch_id_t{next++}, .value = std::move(content)});
    if (!updated) bms_writer_detail::fail(bms_write_diagnostic_code_t::resource_limit, updated.error().message, "$.program");
    prepared.program = std::move(*updated);
}
auto export_program(const song_document_t& song, const bms_program_document_t& owner) -> bms_prepared_program_t {
    auto prepared = owner.prepared().with_catalog(song);
    const auto* chart = owner.read_chart();
    const auto validation = validate_metadata_overrides(prepared.settings.overrides, format_family_t::bms);
    if (!validation.empty()) bms_writer_detail::fail(bms_write_diagnostic_code_t::unsupported_feature,
        validation.front().message, "$.program.settings.overrides");
    const auto first = id_ceiling_t<object_id_t>::from(prepared).next(16);
    if (!first || !chart) bms_writer_detail::fail(bms_write_diagnostic_code_t::resource_limit,
        "Export metadata identities are exhausted.", "$.program");
    auto next = first->value;
    bms_chart_content_t defaults, overrides;
    const auto metadata = resolved_metadata_t::from(song, *chart);
    for (const auto& field : inherited_metadata_fields) {
        const auto* definition = bms_metadata_definition_t::from(field.bms_key);
        if (!definition) continue;
        const auto& replacement = prepared.settings.overrides.value(field.field);
        if (replacement) overrides.items.push_back({.id = object_id_t{next++},
            .value = bms_metadata_declaration_t{.field = definition->field, .value = *replacement}});
        else if (!metadata.value(field.field).empty() && !has_common_metadata(prepared.program, definition->field))
            defaults.items.push_back({.id = object_id_t{next++}, .value = bms_metadata_declaration_t{
                .field = definition->field, .value = metadata.value(field.field)}});
    }
    if (prepared.settings.rating) overrides.items.push_back({.id = object_id_t{next++},
        .value = bms_metadata_declaration_t{.field = bms_metadata_field_t::playlevel,
            .value = bms_writer_detail::exact_decimal(*prepared.settings.rating, "$.program.settings.rating")}});
    add_export_metadata(prepared, std::move(defaults), true, next);
    add_export_metadata(prepared, std::move(overrides), false, next);
    return prepared;
}
auto write_migrated_legacy_bms(const song_document_t& song, chart_id_t id, bms_write_options_t options)
    -> bms_write_outcome_t {
    auto migrated = prepare_native_bms_migration(song, id);
    if (migrated.value && migrated.ok()) return write_bms(*migrated.value, std::move(options));
    bms_write_outcome_t result;
    for (const auto& diagnostic : migrated.diagnostics)
        result.diagnostics.push_back({.severity = diagnostic.severity,
            .code = diagnostic.code == native_project_diagnostic_code_t::resource_limit
                ? bms_write_diagnostic_code_t::resource_limit : bms_write_diagnostic_code_t::invalid_document,
            .message = diagnostic.message, .location = diagnostic.location});
    if (result.diagnostics.empty()) result.diagnostics.push_back({.severity = diagnostic_severity_t::error,
        .code = bms_write_diagnostic_code_t::invalid_document, .message = "Legacy BMS cannot migrate losslessly.", .location = "$.charts"});
    return result;
}

}

auto parse_bms_bytes(std::span<const std::byte> bytes, bms_parse_options_t options) -> bms_parse_outcome_t {
    auto witness = parse_bms_legacy_witness(bytes, options);
    auto imported = prepare_bms_program(bytes, options);
    bms_parse_outcome_t result{.value = {.encoding = imported.value.encoding,
        .lane_profile_detection = std::move(witness.value.lane_profile_detection)},
        .diagnostics = std::move(imported.diagnostics)};
    if (!imported.value.prepared || !imported.ok() || !witness.value.song || !witness.ok()) {
        if (result.ok()) import_error(result, "BMS source requires recovery before it can become a scoped program; original bytes are retained.");
        return result;
    }
    auto& prepared = *imported.value.prepared;
    prepared.source = result.value.encoding;
    const auto& legacy_chart = witness.value.song->charts.front();
    auto owner = bms_program_document_t::from(legacy_chart.id, std::move(prepared),
        {.selections = legacy_chart.active_branch_selection});
    if (!owner.value || !owner.ok()) {
        for (const auto& diagnostic : owner.diagnostics) import_error(result, diagnostic.message);
        return result;
    }
    const auto* view = (*owner.value)->read_chart();
    if (!view || view->divisions_per_standard_measure > options.max_chart_resolution) {
        import_error(result, "All scoped outcomes require a grid above the configured BMS import limit.");
        return result;
    }
    const auto& program = (*owner.value)->prepared();
    const auto common = bms_program_projection_t::for_authoring(program, *program.program.target_chart(program.program.root()));
    song_document_t song{.id = witness.value.song->id,
        .audio_assets = program.audio_assets, .visual_assets = program.visual_assets, .resources = program.resources,
        .prepared_bms_programs = {*owner.value}, .chart_order = {legacy_chart.id}};
    if (common.value) {
        song.metadata.title = common.value->song().metadata.title;
        song.metadata.artist = common.value->song().metadata.artist;
    }
    song.metadata.format_group_for(format_family_t::bms).encoding = result.value.encoding;
    song.metadata.format_group_for(format_family_t::bms).encoding.original_bytes.reset();
    // The native boundary validates every bounded outcome while retaining
    // diagnosed pre-existing missing bindings and rejecting unpersistable input.
    const auto persisted = write_native_project(song);
    for (const auto& diagnostic : persisted.diagnostics)
        if (diagnostic.severity == diagnostic_severity_t::error) import_error(result, diagnostic.message);
    if (result.ok()) result.value.song = std::move(song);
    return result;
}

auto write_bms(const song_document_t& song, bms_write_options_t options) -> bms_write_outcome_t {
    if (!song.export_cuts.empty())
        return {.diagnostics = {{.severity = diagnostic_severity_t::error,
            .code = bms_write_diagnostic_code_t::unsupported_feature,
            .message = "Export cuts require convert_document so chart transformation and generated audio are planned together.", .location = "export_cuts"}}};

    const auto charts = song.read_charts();
    const auto id = options.chart.value_or(charts.size() == 1 ? charts.front()->id : chart_id_t{});
    const auto* owner = song.find_bms_program(id);
    if (!owner) {
        const auto* chart = song.find_chart(id);
        if (!chart || chart->source_family != format_family_t::bms)
            return write_bms_legacy_witness(song, std::move(options));
        return write_migrated_legacy_bms(song, id, std::move(options));
    }
    try {
        auto prepared = export_program(song, *owner);
        if (options.lane_profile_id) {
            if (!prepared.lane_profile) {
                auto detected = detect_bms_lane_profile({.explicit_profile_id = options.lane_profile_id});
                if (!detected.selected_profile) bms_writer_detail::fail(bms_write_diagnostic_code_t::missing_profile,
                    "The requested export lane profile is unknown.", "$.options.lane_profile_id");
                prepared.lane_profile = std::move(detected.selected_profile);
            } else if (*options.lane_profile_id != prepared.lane_profile->layout.profile_id)
                bms_writer_detail::fail(bms_write_diagnostic_code_t::missing_profile,
                    "Scoped export requires the difficulty's authored lane profile.", "$.options.lane_profile_id");
        }
        options.chart.reset();
        options.lane_profile_id.reset();
        return write_bms_program(prepared, {.format = std::move(options), .evaluation = owner->evaluation(),
            .loss_policy = loss_policy_t::warn, .source = prepared.source});
    } catch (const bms_writer_detail::write_failure_t& failure) {
        return {.diagnostics = {{.severity = diagnostic_severity_t::error,
            .code = failure.code, .message = failure.message, .location = failure.path}}};
    }
}
} // namespace wormedit
