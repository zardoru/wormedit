#include "sys/metadata/metadata_commands.h"
#include "sys/commands/document_edit_feedback.h"
#include "sys/metadata/metadata_actions.h"
#include "sys/app/action_router.h"
#include "sys/app/application_edit_inputs.h"
#include "wormedit/core/numeric_text.h"

#include "ui/frontend.h"

#include <wormedit/core/audio_offset_editor.h>
#include <wormedit/core/channel_map_editor.h>
#include <wormedit/core/command.h>
#include <wormedit/core/lane_migration.h>
#include <wormedit/core/metadata_editor.h>
#include <wormedit/core/resource_file_copy.h>
#include <wormedit/core/workspace.h>
#include <wormedit/core/timing_links.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace wormedit::sys {
namespace {

struct metadata_edit_feedback_t {
    notification_severity_t severity;
    std::string message;
};

auto diagnostic_feedback(const diagnostic_t& diagnostic) -> metadata_edit_feedback_t {
    const auto severity = diagnostic.severity;
    return {to_notification_severity(severity, notification_severity_t::info), diagnostic.message};
}

auto publish_metadata_edit(document_command_result_t result,
                           workspace_controller_t& workspace,
                           document_history_t& history) -> metadata_edit_feedback_t {
    if (!result.applied) {
        if (!result.diagnostics.empty()) return diagnostic_feedback(result.diagnostics.front());
        return {notification_severity_t::info, "No metadata changes."};
    }
    auto feedback = rebind_document_edit(
        workspace, history,
        {.success = "Metadata updated.",
         .rebind_failure = "Metadata changed, but workspace rebinding failed.",
         .unknown_severity = notification_severity_t::info,
         .source = notification_source_t::edit});
    return {feedback.severity, std::string{feedback.message_text()}};
}

auto dispatch_song_edit(const wormedit::ui::metadata_edit_request_t& request, document_history_t& history,
                        workspace_controller_t& workspace) -> metadata_edit_feedback_t {
    if (!request.title || !request.artist ||
        request.search_tags.size() > wormedit::default_metadata_edit_limits.maximum_search_tags) {
        return {notification_severity_t::warning, "Metadata edit payload is incomplete or exceeds the tag-count cap."};
    }
    std::vector<std::string> tags;
    tags.reserve(request.search_tags.size());
    for (std::size_t index = 0; index < request.search_tags.size(); ++index) {
        if (!request.search_tags[index])
            return {notification_severity_t::warning, "Metadata edit contains an empty tag pointer."};
        tags.emplace_back(*request.search_tags[index]);
    }
    return publish_metadata_edit(
        apply_metadata_edit(history, request.document_revision,
                            set_song_metadata_action_t{*request.title, *request.artist,
                                                       std::move(tags)}),
        workspace, history);
}

auto dispatch_chart_edit(const wormedit::ui::metadata_edit_request_t& request, document_history_t& history,
                         workspace_controller_t& workspace) -> metadata_edit_feedback_t {
    if (!request.chart_id || !request.chart_name ||
        !request.chart_author ||
        (request.has_chart_rating &&
         (!request.chart_rating_numerator_text ||
          !request.chart_rating_denominator_text))) {
        return {notification_severity_t::warning, "Chart metadata edit payload is incomplete."};
    }
    const auto rating = wormedit::chart_rating_edit_t::from(
        request.chart_id, !request.has_chart_rating,
        request.chart_rating_numerator_text ? *request.chart_rating_numerator_text
                                                    : "",
        request.chart_rating_denominator_text
            ? *request.chart_rating_denominator_text
            : "");
    if (!rating)
        return {notification_severity_t::warning, "Rating must use a signed 64-bit numerator and positive 64-bit denominator."};
    return publish_metadata_edit(
        apply_metadata_edit(
            history, request.document_revision,
            set_chart_metadata_action_t{request.chart_id,
                                        *request.chart_name, *request.chart_author,
                                        rating->rating}),
        workspace, history);
}

auto dispatch_batch_chart_edit(const wormedit::ui::metadata_edit_request_t& request, document_history_t& history,
                               workspace_controller_t& workspace) -> metadata_edit_feedback_t {
    const auto count = request.batch_chart_ids.size();
    if (count == 0 || count > wormedit::maximum_batch_chart_metadata_charts ||
        (request.batch_edit_author && !request.chart_author) ||
        (request.batch_edit_rating && request.has_chart_rating &&
         (!request.chart_rating_numerator_text ||
          !request.chart_rating_denominator_text)))
        return {notification_severity_t::warning, "Batch metadata edit payload is incomplete."};
    batch_chart_metadata_action_t batch;
    batch.charts = request.batch_chart_ids;
    if (request.batch_edit_author) batch.author = *request.chart_author;
    batch.edit_rating = request.batch_edit_rating;
    if (batch.edit_rating) {
        const auto rating = wormedit::chart_rating_edit_t::from(
            batch.charts.front(), !request.has_chart_rating,
            request.chart_rating_numerator_text
                ? *request.chart_rating_numerator_text
                : "",
            request.chart_rating_denominator_text
                ? *request.chart_rating_denominator_text
                : "");
        if (!rating)
            return {notification_severity_t::warning, "Rating must use a signed 64-bit numerator and positive 64-bit denominator."};
        batch.rating = rating->rating;
    }
    auto feedback = publish_metadata_edit(
        apply_batch_chart_metadata_edit(history, request.document_revision, batch),
        workspace, history);
    if (feedback.severity == notification_severity_t::success)
        feedback.message = "Metadata updated for " + std::to_string(count) + " difficulties.";
    return feedback;
}

auto dispatch_audio_offset_edit(const wormedit::ui::metadata_edit_request_t& request,
                                document_history_t& history,
                                workspace_controller_t& workspace,
                                bool timing_locked) -> metadata_edit_feedback_t {
    if (!request.chart_id || !request.audio_offset_numerator_text ||
        !request.audio_offset_denominator_text)
        return {notification_severity_t::warning, "Audio offset edit payload is incomplete."};
    std::int64_t numerator{};
    std::uint64_t denominator{};
    if (!parse_integer(*request.audio_offset_numerator_text, numerator) ||
        !parse_integer(*request.audio_offset_denominator_text, denominator) || denominator == 0) {
        return {notification_severity_t::warning, "Audio offset must use a signed 64-bit numerator and positive 64-bit denominator."};
    }
    return publish_metadata_edit(
        apply_audio_offset_edit(history, request.document_revision,
                               request.chart_id, {numerator, denominator},
                               timing_locked),
        workspace, history);
}

auto dispatch_format_edit(const wormedit::ui::metadata_edit_request_t& request, document_history_t& history,
                          workspace_controller_t& workspace) -> metadata_edit_feedback_t {
    if (request.format_family < format_family_t::native_project ||
        request.format_family > format_family_t::quaver)
        return {notification_severity_t::warning, "Format metadata edit refers to an unknown format family."};
    const format_metadata_target_t target{
        request.format_chart_scoped
            ? std::optional<chart_id_t>{request.chart_id}
            : std::nullopt,
        request.format_family};
    metadata_edit_action_t edit;
    if (request.kind == WORM_UI_METADATA_EDIT_REPLACE_FORMAT_FIELD) {
        if (!request.format_name || !request.format_value)
            return {notification_severity_t::warning, "Format metadata replacement payload is incomplete."};
        edit = edit_format_metadata_field_action_t{
            .target = target,
            .index = request.format_index,
            .name = *request.format_name,
            .value = *request.format_value,
            .mode = wormedit::format_metadata_field_mode_t::replace};
    } else if (request.kind == WORM_UI_METADATA_EDIT_ERASE_FORMAT_FIELD) {
        edit = erase_format_metadata_field_action_t{target, request.format_index};
    } else {
        if (!request.format_name || !request.format_value)
            return {notification_severity_t::warning, "Format metadata insertion payload is incomplete."};
        edit = edit_format_metadata_field_action_t{
            .target = target,
            .index = request.format_index,
            .name = *request.format_name,
            .value = *request.format_value,
            .mode = wormedit::format_metadata_field_mode_t::insert};
    }
    return publish_metadata_edit(
        apply_metadata_edit(history, request.document_revision, edit), workspace, history);
}

auto redivision_rejection_message(const diagnostic_t& diagnostic) -> std::string {
    auto message = "Resolution change rejected: " + diagnostic.message;
    if (diagnostic.object) message += " (object " + std::to_string(diagnostic.object->value) + ")";
    return message;
}

auto dispatch_redivision_edit(const wormedit::ui::metadata_edit_request_t& request, document_history_t& history,
                              workspace_controller_t& workspace) -> metadata_edit_feedback_t {
    if (!request.chart_id || request.redivision_resolution == 0)
        return {notification_severity_t::warning, "Resolution change payload is incomplete."};
    auto redivision = apply_redivision_command(history, request.document_revision,
                                               request.chart_id,
                                               request.redivision_resolution);
    if (!redivision.command.applied && !redivision.command.diagnostics.empty())
        return {notification_severity_t::error,
                redivision_rejection_message(redivision.command.diagnostics.front())};
    const auto moved = redivision.payload.moves.size();
    auto feedback = publish_metadata_edit(std::move(redivision.command), workspace, history);
    if (feedback.severity == notification_severity_t::success)
        feedback.message = "Redivided " + std::to_string(moved) + " positions to " +
                           std::to_string(request.redivision_resolution) +
                           " divisions per measure.";
    return feedback;
}

auto optional_channel_text(const std::optional<std::string>& text) -> std::optional<std::string> {
    if (!text || text->empty()) return std::nullopt;
    return *text;
}

// Lane ids and four texts per lane; an empty text means the channel is unset.
auto channel_map_replacements(const wormedit::ui::metadata_edit_request_t& request)
    -> std::optional<std::vector<channel_map_replacement_t>> {
    const auto count = request.channel_map_lanes.size();
    if (count > wormedit::ui::metadata_channel_map_lane_limit)
        return std::nullopt;
    std::vector<channel_map_replacement_t> replacements;
    replacements.reserve(count);
    for (const auto& lane : request.channel_map_lanes) {
        if (!lane.visible || !lane.invisible || !lane.long_note || !lane.mine) return std::nullopt;
        replacements.push_back({.lane = lane.lane,
            .channels = {optional_channel_text(lane.visible), optional_channel_text(lane.invisible),
                         optional_channel_text(lane.long_note), optional_channel_text(lane.mine)}});
    }
    return replacements;
}

auto dispatch_channel_map_preview(const wormedit::ui::metadata_edit_request_t& request, const document_history_t& history,
                                  metadata_drop_state_t& metadata_drop) -> metadata_edit_feedback_t {
    const auto replacements = channel_map_replacements(request);
    if (!request.chart_id || !replacements)
        return {notification_severity_t::warning, "Channel-map preview payload is incomplete."};
    auto preview = channel_map_proposal_t::propose(
        history.document, request.document_revision, request.chart_id,
        request.channel_map_layout_segment_id, *replacements);
    const auto view = channel_map_preview_view_t::from(preview);
    auto feedback = metadata_edit_feedback_t{notification_severity_t::info,
        view.diagnostic ? view.diagnostic->message : std::string{}};
    if (!view.proposal) {
        feedback.severity = notification_severity_t::error;
    } else if (view.report->conflict_count != 0) {
        feedback = {notification_severity_t::warning, "Channel-map preview has " +
                    std::to_string(view.report->conflict_count) + " conflicts; Apply is disabled."};
    } else {
        feedback.message = "Channel-map preview: " + std::to_string(view.report->affected_note_count) +
                           " notes change channel.";
    }
    metadata_drop.channel_map_preview = std::move(preview);
    return feedback;
}

auto dispatch_channel_map_apply(document_history_t& history, workspace_controller_t& workspace,
                                metadata_drop_state_t& metadata_drop) -> metadata_edit_feedback_t {
    if (!metadata_drop.channel_map_preview)
        return {notification_severity_t::warning, "Preview the channel map before applying it."};
    const auto& preview = *metadata_drop.channel_map_preview;
    const auto view = channel_map_preview_view_t::from(preview);
    if (!view.proposal || view.report->conflict_count != 0)
        return {notification_severity_t::warning, "The channel-map preview is not applicable."};
    auto result = apply_proposal(history, *view.proposal);
    metadata_drop.clear_channel_map_preview();
    if (!result) return {notification_severity_t::error, result.payload.message};
    auto feedback = publish_metadata_edit(std::move(result.command), workspace, history);
    if (feedback.severity == notification_severity_t::success)
        feedback.message = "Channel maps updated.";
    return feedback;
}

// Previews the chosen family profile as a sealed proposal. The index must name a family choice:
// Custom and stale indices never reach the core.
auto dispatch_lane_profile_preview(const wormedit::ui::metadata_edit_request_t& request, const document_history_t& history,
                                   metadata_drop_state_t& metadata_drop) -> metadata_edit_feedback_t {
    const auto* chart = history.read_chart(request.chart_id);
    if (!chart) return {notification_severity_t::warning, "Choose a chart before previewing its key count."};
    const auto targets = lane_profile_family_targets(*chart);
    if (request.lane_profile_index >= targets.size())
        return {notification_severity_t::warning,
                "Choose a built-in key-count profile of this chart's family to preview."};
    auto proposal = lane_migration_proposal_t::propose(history.document, request.document_revision,
                                           chart->id, targets[request.lane_profile_index]);
    if (!proposal.value) {
        metadata_drop.clear_lane_profile_preview();
        return {notification_severity_t::error, proposal.diagnostics.empty() ? std::string{} : proposal.diagnostics.front().message};
    }
    const auto& report = proposal.value->payload.report;
    const auto message = "Key-count preview: " + std::to_string(report.mapped_note_count) + " notes map, " +
                         std::to_string(report.dropped_notes.size()) + " dropped, " +
                         std::to_string(report.collision_count) + " collisions.";
    metadata_drop.lane_profile_preview = std::move(proposal);
    return {notification_severity_t::info, message};
}

// Apply consumes the sealed preview. The button press is the confirmation for any dropped notes.
auto dispatch_lane_profile_apply(document_history_t& history, workspace_controller_t& workspace,
                                 metadata_drop_state_t& metadata_drop) -> metadata_edit_feedback_t {
    if (!metadata_drop.lane_profile_preview)
        return {notification_severity_t::warning, "Preview the key-count change before applying it."};
    const auto proposal = std::move(*metadata_drop.lane_profile_preview);
    metadata_drop.clear_lane_profile_preview();
    if (!proposal.value)
        return {notification_severity_t::error, "Lane migration preview data was changed after proposal."};
    auto result = apply_proposal(history, *proposal.value, true);
    if (!result) return {notification_severity_t::error, result.payload.message};
    auto feedback = publish_metadata_edit(std::move(result.command), workspace, history);
    if (feedback.severity == notification_severity_t::success)
        feedback.message = "Key-count profile changed.";
    return feedback;
}

auto lane_profile_family_targets_impl(const chart_document_t& chart) -> std::vector<lane_input_profile_t> {
    std::vector<lane_input_profile_t> targets;
    if (chart.lane_layouts.empty()) return targets;
    const auto& current_id = chart.lane_layouts.front().layout.profile_id;
    const auto catalog = built_in_bms_lane_profiles();
    const auto current = std::ranges::find_if(catalog, [&](const bms_lane_profile_descriptor_t& item) {
        return item.profile.layout.profile_id == current_id;
    });
    if (current != catalog.end()) {
        for (const auto& item : catalog)
            if (item.family == current->family) targets.push_back(item.profile);
        return targets;
    }
    // osu!mania charts: the 1-18 key factory is the family's supported counts.
    for (std::uint32_t keys = 1; keys <= 18; ++keys) {
        auto profile = make_osu_mania_profile(keys);
        if (profile) targets.push_back(*profile);
    }
    return targets;
}

// What one metadata edit runs against. The row's handler takes only the request and this.
struct metadata_edit_context_t {
    document_history_t& history;
    workspace_controller_t& workspace;
    metadata_drop_state_t& drop;
    bool timing_locked;
};

struct metadata_edit_row_t {
    metadata_edit_feedback_t (*run)(const wormedit::ui::metadata_edit_request_t&, const metadata_edit_context_t&){};
    bool clears_output_drop{};
};

struct metadata_edit_entry_t {
    WormUiMetadataEditKind kind;
    metadata_edit_row_t row;
};

constexpr auto metadata_edit_kind_count = static_cast<std::size_t>(WORM_UI_METADATA_EDIT_APPLY_LANE_PROFILE) + 1;

// One row per edit kind. A kind listed twice or not at all fails the build, so a new
// WormUiMetadataEditKind cannot be added without deciding what runs for it.
template <std::size_t N>
consteval auto make_metadata_edit_table(const std::array<metadata_edit_entry_t, N>& entries)
    -> std::array<metadata_edit_row_t, metadata_edit_kind_count> {
    std::array<metadata_edit_row_t, metadata_edit_kind_count> table{};
    for (const auto& entry : entries) {
        auto& row = table[static_cast<std::size_t>(entry.kind)];
        if (row.run) throw "metadata edit kind listed twice";
        row = entry.row;
    }
    for (std::size_t kind = 1; kind < table.size(); ++kind)
        if (!table[kind].run) throw "metadata edit kind has no row";
    return table;
}

auto edit_output_path(const wormedit::ui::metadata_edit_request_t& request, const metadata_edit_context_t& context)
    -> metadata_edit_feedback_t {
    if (!request.output_path ||
        request.output_path->size() > wormedit::default_metadata_edit_limits.maximum_output_path_bytes)
        return {notification_severity_t::warning, "Output path edit payload is incomplete or too long."};
    return publish_metadata_edit(
        apply_metadata_edit(context.history, request.document_revision,
                            set_output_group_path_action_t{request.output_group_index, *request.output_path}),
        context.workspace, context.history);
}

constexpr auto metadata_edit_table = make_metadata_edit_table(std::array{
    metadata_edit_entry_t{WORM_UI_METADATA_EDIT_SONG, {[](const auto& request, const auto& context) {
        return dispatch_song_edit(request, context.history, context.workspace);
    }}},
    metadata_edit_entry_t{WORM_UI_METADATA_EDIT_CHART, {[](const auto& request, const auto& context) {
        return dispatch_chart_edit(request, context.history, context.workspace);
    }}},
    metadata_edit_entry_t{WORM_UI_METADATA_EDIT_INHERITANCE, {[](const auto& request, const auto& context) {
        return publish_metadata_edit(apply_metadata_edit(context.history, request.document_revision,
            set_chart_metadata_overrides_action_t{.chart = request.chart_id, .overrides = request.overrides}),
            context.workspace, context.history);
    }}},
    metadata_edit_entry_t{WORM_UI_METADATA_EDIT_BATCH_CHART, {[](const auto& request, const auto& context) {
        return dispatch_batch_chart_edit(request, context.history, context.workspace);
    }}},
    metadata_edit_entry_t{WORM_UI_METADATA_EDIT_AUDIO_OFFSET, {[](const auto& request, const auto& context) {
        return dispatch_audio_offset_edit(request, context.history, context.workspace, context.timing_locked);
    }}},
    metadata_edit_entry_t{WORM_UI_METADATA_EDIT_OUTPUT_PATH, {edit_output_path, true}},
    metadata_edit_entry_t{WORM_UI_METADATA_EDIT_REDIVIDE_CHART, {[](const auto& request, const auto& context) {
        return dispatch_redivision_edit(request, context.history, context.workspace);
    }}},
    metadata_edit_entry_t{WORM_UI_METADATA_EDIT_PREVIEW_CHANNEL_MAP, {[](const auto& request, const auto& context) {
        return dispatch_channel_map_preview(request, context.history, context.drop);
    }}},
    metadata_edit_entry_t{WORM_UI_METADATA_EDIT_APPLY_CHANNEL_MAP, {[](const auto&, const auto& context) {
        return dispatch_channel_map_apply(context.history, context.workspace, context.drop);
    }}},
    metadata_edit_entry_t{WORM_UI_METADATA_EDIT_PREVIEW_LANE_PROFILE, {[](const auto& request, const auto& context) {
        return dispatch_lane_profile_preview(request, context.history, context.drop);
    }}},
    metadata_edit_entry_t{WORM_UI_METADATA_EDIT_APPLY_LANE_PROFILE, {[](const auto&, const auto& context) {
        return dispatch_lane_profile_apply(context.history, context.workspace, context.drop);
    }}},
    metadata_edit_entry_t{WORM_UI_METADATA_EDIT_REPLACE_FORMAT_FIELD, {[](const auto& request, const auto& context) {
        return dispatch_format_edit(request, context.history, context.workspace);
    }}},
    metadata_edit_entry_t{WORM_UI_METADATA_EDIT_ERASE_FORMAT_FIELD, {[](const auto& request, const auto& context) {
        return dispatch_format_edit(request, context.history, context.workspace);
    }}},
    metadata_edit_entry_t{WORM_UI_METADATA_EDIT_INSERT_FORMAT_FIELD, {[](const auto& request, const auto& context) {
        return dispatch_format_edit(request, context.history, context.workspace);
    }}},
});

auto dispatch_metadata_edit(const wormedit::ui::metadata_edit_request_t& request,
                            const metadata_edit_context_t& context) -> metadata_edit_feedback_t {
    const auto index = static_cast<std::size_t>(request.kind);
    if (index == 0 || index >= metadata_edit_table.size())
        return {notification_severity_t::warning, "Invalid metadata edit action."};
    const auto& row = metadata_edit_table[index];
    auto feedback = row.run(request, context);
    if (row.clears_output_drop) context.drop.clear_output();
    return feedback;
}

} // namespace

auto lane_profile_family_targets(const chart_document_t& chart) -> std::vector<lane_input_profile_t> {
    return lane_profile_family_targets_impl(chart);
}

namespace {
auto dispatch_timing_link(const ui::timing_link_action_t& action, const metadata_action_inputs_t& environment,
    bool timing_locked, const notification_sink_t& notifications) -> std::string {
    auto& history = environment.document.history;
    document_command_result_t result;
    if (action.context.session_generation != environment.session_generation ||
        environment.document.workspace.selected_chart != action.context.chart)
        result = document_command_transaction_t::rejection(history.document.revision,
            diagnostic_code_t::invalid_binding, stale_action_message("Timing link"));
    else if (timing_locked)
        result = document_command_transaction_t::rejection(history.document.revision,
            diagnostic_code_t::unsupported_feature, "Timing links are unavailable while timing is locked.");
    else result = apply_timing_link_edit(history,
        {.document = {.project = action.context.project, .revision = action.context.revision}, .chart = action.context.chart},
        action.request);
    auto feedback = publish_metadata_edit(std::move(result), environment.document.workspace, history);
    if (feedback.severity == notification_severity_t::success) feedback.message = "Timing link updated.";
    notifications.post(feedback.severity, notification_source_t::edit, feedback.message);
    return std::move(feedback.message);
}
}
namespace {
// What a metadata route runs against: the frame's inputs plus the flags the caller decided.
struct metadata_route_environment_t {
    const metadata_action_inputs_t& inputs;
    bool timing_locked;
    bool file_jobs_busy;
    notification_sink_t notifications;
    std::optional<std::string>& status;  // text of the handled action, read by the caller
};

using metadata_router_t = action_router_t<wormedit::ui::workspace_action_t, metadata_route_environment_t>;

auto route_timing_link(const wormedit::ui::workspace_action_t& action, const metadata_route_environment_t& environment)
    -> route_status_t {
    if (!action.timing_link.requested) return route_status_t::declined;
    environment.status = dispatch_timing_link(action.timing_link, environment.inputs, environment.timing_locked,
                                              environment.notifications);
    return route_status_t::handled;
}

auto route_metadata_edit(const wormedit::ui::workspace_action_t& action, const metadata_route_environment_t& environment)
    -> route_status_t {
    if (action.metadata.kind == WORM_UI_METADATA_EDIT_NONE) return route_status_t::declined;
    const auto& inputs = environment.inputs;
    auto feedback = dispatch_metadata_edit(action.metadata, {.history = inputs.document.history,
        .workspace = inputs.document.workspace, .drop = inputs.import.drop, .timing_locked = environment.timing_locked});
    environment.notifications.post(feedback.severity, notification_source_t::edit, feedback.message);
    environment.status = std::move(feedback.message);
    return route_status_t::handled;
}

auto route_import_copy(const wormedit::ui::workspace_action_t& action, const metadata_route_environment_t& environment)
    -> route_status_t {
    if (!action.metadata.import_copy_project && !action.metadata.import_copy_export) return route_status_t::declined;
    const auto& inputs = environment.inputs;
    environment.status = inputs.import.runtime.start_import_copy(
        action, inputs.document.history, inputs.document.workspace, inputs.asset_base, inputs.import.scheduler,
        environment.file_jobs_busy);
    if (inputs.import.runtime.import_pending()) inputs.import.drop.clear_music();
    return route_status_t::handled;
}

auto route_copy_preview(const wormedit::ui::workspace_action_t& action, const metadata_route_environment_t& environment)
    -> route_status_t {
    if (!action.metadata.inspect_copy) return route_status_t::declined;
    const auto& inputs = environment.inputs;
    environment.status = inputs.import.runtime.start_copy_preview(
        action, inputs.document.history, inputs.document.workspace, inputs.asset_base);
    return route_status_t::handled;
}

// The metadata domain's routes, in the order it wants them tried (same priority: declaration order).
auto metadata_routes() -> std::vector<metadata_router_t::route_t> {
    return {
        {"metadata.timing_link", route_priority_t::domain, {}, route_timing_link},
        {"metadata.edit", route_priority_t::domain, {}, route_metadata_edit},
        {"metadata.import_copy", route_priority_t::domain, {}, route_import_copy},
        {"metadata.copy_preview", route_priority_t::domain, {}, route_copy_preview},
    };
}

auto metadata_router() -> const metadata_router_t& {
    static const auto router = metadata_router_t::from(metadata_routes());
    return router;
}

}  // namespace

auto metadata_route_names() -> std::vector<std::string_view> {
    auto names = std::vector<std::string_view>{};
    for (const auto& route : metadata_router().routes()) names.push_back(route.name);
    return names;
}

auto dispatch_metadata_action(const wormedit::ui::workspace_action_t& action,
                              const metadata_action_inputs_t& environment, bool timing_locked,
                              bool file_jobs_busy, bool post_notifications)
    -> std::optional<std::string> {
    std::optional<std::string> status;
    const metadata_route_environment_t route_environment{
        .inputs = environment,
        .timing_locked = timing_locked,
        .file_jobs_busy = file_jobs_busy,
        .notifications = post_notifications ? environment.document.notifications : notification_sink_t::muted(),
        .status = status};
    // Session and shortcut gating belongs to the frame router; this domain is dispatched after both.
    static_cast<void>(metadata_router().dispatch(action, route_environment,
        {.session_loaded = true, .shortcut_handled = false}));
    return status;
}

} // namespace wormedit::sys
