#include "sys/project_view.h"
#include "core/text_util.h"
#include "sys/metadata_commands.h"
#include <wormedit/core/rational.h>

#include <wormedit/core/timing.h>
#include <wormedit/core/chart_editor.h>
#include <wormedit/formats/conversion.h>

#include <algorithm>
#include <iomanip>
#include <locale>
#include <sstream>

namespace wormedit::sys {
namespace {
auto display_text(std::string_view text) -> std::string {
    constexpr std::size_t capacity = 4096;
    if (text.size() <= capacity) return std::string{text};
    return std::string{text_util::truncate_utf8(text, capacity - 3)} + "...";
}

auto decimal_value(signed_rational_t value, unsigned precision) -> std::string {
    if (value.denominator == 0) return "Invalid";
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::fixed << std::setprecision(precision) << to_long_double(value);
    return stream.str();
}

void build_project_chart_mode_view(const chart_document_t& chart, project_chart_row_view_t& row) {
    if (chart.lane_layouts.empty()) { row.mode = "No lane layout"; return; }
    const auto& layout = chart.lane_layouts.front().layout;
    row.mode = display_text(layout.display_name.empty() ? layout.profile_id : layout.display_name);
    row.key_count = static_cast<std::uint32_t>(std::ranges::count_if(layout.lanes, [](const auto& lane) {
        return lane.role == lane_role_t::playable || lane.role == lane_role_t::foot_pedal;
    }));
}

void build_project_chart_groups_view(const song_document_t& document, project_chart_row_view_t& row) {
    std::string names;
    for (const auto& group : document.output_groups) {
        if (std::ranges::find(group.charts, row.chart) == group.charts.end()) continue;
        ++row.output_group_count;
        if (names.size() > 4096) continue;
        if (!names.empty()) names += ", ";
        names += display_text(group.name);
    }
    row.output_groups = names.empty() ? "Unassigned" : display_text(names);
}

void build_project_chart_length_view(const chart_document_t& chart, project_chart_row_view_t& row) {
    const auto built = timing_index_t::from(chart, {.allow_bounded_clock = true});
    if (!built.value) {
        row.length = "Unavailable";
        row.diagnostic = built.diagnostics.empty() ? "Chart timing is unavailable."
            : display_text(built.diagnostics.front().message);
        return;
    }
    const auto begin = built.value->query_seconds(0, timing_boundary_side_t::before_delays);
    const auto end =
        built.value->query_seconds(built.value->total_rows, timing_boundary_side_t::after_stops);
    if (!begin || !end) { row.length = "Unavailable"; row.diagnostic = "Chart length query failed."; return; }
    const auto seconds = to_long_double(end->value) - to_long_double(begin->value);
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::fixed << std::setprecision(2) << seconds << " s";
    row.length = stream.str();
}

} // namespace

auto project_chart_row_view_t::from(const song_document_t& document, const chart_document_t& chart)
    -> project_chart_row_view_t {
    project_chart_row_view_t row{.chart = chart.id, .name = display_text(chart.metadata.name),
        .author = display_text(chart.metadata.author),
        .rating = chart.metadata.rating ? decimal_value(*chart.metadata.rating, 2) : "Unset"};
    build_project_chart_mode_view(chart, row);
    build_project_chart_groups_view(document, row);
    for (const auto& measure : chart.measures) row.note_count += measure.notes.size();
    build_project_chart_length_view(chart, row);
    return row;
}

namespace {
auto reorder_project_chart(document_history_t& history, const project_chart_command_t& action)
    -> project_chart_command_result_t {
    const auto& charts = history.document.charts;
    const auto* source = history.document.find_chart(action.chart);
    const auto* target = history.document.find_chart(action.target_chart);
    if (!source || !target)
        return {.command = {.revision = history.document.revision, .diagnostics = {{
            .severity = diagnostic_severity_t::error, .code = diagnostic_code_t::invalid_id,
            .message = "Chart reorder is stale or its source or destination no longer exists."}}}};
    const auto source_index = static_cast<std::size_t>(source - charts.data());
    auto destination =
        static_cast<std::size_t>(target - charts.data()) + (action.insert_after ? 1 : 0);
    if (destination > source_index) --destination;
    if (destination == source_index) return {.command = {.revision = history.document.revision}};
    return {.command = apply_reorder_chart_command(history, action.chart, destination)};
}
} // namespace

auto apply_project_chart_command(document_history_t& history,
    const project_chart_command_t& action) -> project_chart_command_result_t {
    if (action.kind == project_chart_command_kind_t::batch_metadata)
        return {.command = apply_batch_chart_metadata_edit(history, action.expected_revision, action.metadata)};
    const auto label = action.kind == project_chart_command_kind_t::reorder
        ? "Chart reorder" : "Project chart action";
    if (auto rejected = metadata_command_detail::stale_rejection(history, action.expected_revision, label))
        return {.command = std::move(*rejected)};
    if (action.kind == project_chart_command_kind_t::reorder)
        return reorder_project_chart(history, action);
    if (action.kind == project_chart_command_kind_t::create) {
        auto created = history.document.charts.empty()
                           ? apply_new_chart_command(history)
                           : apply_add_chart_command(history, action.chart);
        return {.command = std::move(created.command), .open_chart = created.payload.chart};
    }
    if (action.kind == project_chart_command_kind_t::duplicate) {
        auto copy = apply_duplicate_chart_command(history, action.chart);
        return {.command = std::move(copy.command)};
    }
    if (action.kind == project_chart_command_kind_t::erase)
        return {.command = apply_delete_chart_command(history, action.chart)};

    const auto* chart = history.document.find_chart(action.chart);
    if (chart == nullptr)
        return {.command = {.revision = history.document.revision, .diagnostics = {{
            .severity = diagnostic_severity_t::error, .code = diagnostic_code_t::invalid_id,
            .message = "Project chart action is stale or its chart no longer exists."}}}};
    if (action.kind == project_chart_command_kind_t::open)
        return {.command = {.revision = history.document.revision}, .open_chart = action.chart};
    const auto index = static_cast<std::size_t>(chart - history.document.charts.data());
    const bool up = action.kind == project_chart_command_kind_t::move_up;
    if ((up && index == 0) || (!up && index + 1 == history.document.charts.size()))
        return {.command = {.revision = history.document.revision}};
    return {.command =
                apply_reorder_chart_command(history, action.chart, up ? index - 1 : index + 1)};
}

auto project_chart_table_view_t::from(const song_document_t& document) -> project_chart_table_view_t {
    const auto count = std::min(document.charts.size(), maximum_project_chart_rows);
    project_chart_table_view_t table{.total_charts = document.charts.size(),
        .rows_truncated = document.charts.size() > count};
    table.rows.reserve(count);
    for (std::size_t index = 0; index < count; ++index)
        table.rows.push_back(project_chart_row_view_t::from(document, document.charts[index]));
    return table;
}

auto project_chart_table_view_t::from(project_chart_table_cache_t& cache,
                                    const song_document_t& document,
                                    std::uint64_t session_generation)
    -> const project_chart_table_view_t& {
    if (!cache.populated || cache.song != document.id || cache.session_generation != session_generation ||
        cache.revision != document.revision) {
        cache.table = project_chart_table_view_t::from(document);
        cache.song = document.id;
        cache.session_generation = session_generation;
        cache.revision = document.revision;
        cache.populated = true;
    }
    return cache.table;
}
namespace {
auto ui_command_kind(WormUiProjectChartActionKind kind) -> std::optional<project_chart_command_kind_t> {
    switch (kind) {
    case WORM_UI_PROJECT_CHART_NEW: return project_chart_command_kind_t::create;
    case WORM_UI_PROJECT_CHART_DUPLICATE: return project_chart_command_kind_t::duplicate;
    case WORM_UI_PROJECT_CHART_DELETE: return project_chart_command_kind_t::erase;
    case WORM_UI_PROJECT_CHART_UP: return project_chart_command_kind_t::move_up;
    case WORM_UI_PROJECT_CHART_DOWN: return project_chart_command_kind_t::move_down;
    case WORM_UI_PROJECT_CHART_OPEN: return project_chart_command_kind_t::open;
    case WORM_UI_PROJECT_CHART_BATCH_METADATA: return project_chart_command_kind_t::batch_metadata;
    case WORM_UI_PROJECT_CHART_REORDER: return project_chart_command_kind_t::reorder;
    default: return std::nullopt;
    }
}

auto ui_batch_metadata(const WormUiProjectChartAction& action) -> std::optional<batch_chart_metadata_action_t> {
    if (action.selected_count == 0 || action.selected_count > WORM_UI_PROJECT_CHART_CAPACITY ||
        (action.edit_author && !action.author)) return std::nullopt;
    batch_chart_metadata_action_t metadata;
    for (size_t index = 0; index < action.selected_count; ++index)
        metadata.charts.push_back(chart_id_t{action.selected_chart_ids[index]});
    if (action.edit_author) metadata.author = action.author;
    metadata.edit_rating = action.edit_rating;
    if (action.edit_rating && !action.unset_rating) {
        if (!action.rating_numerator || !action.rating_denominator) return std::nullopt;
        metadata.rating = parse_metadata_rating(action.rating_numerator, action.rating_denominator);
        if (!metadata.rating) return std::nullopt;
    }
    return metadata;
}

void publish_row_error(project_chart_runtime_t& runtime, const WormUiProjectChartAction& action,
    const std::string& message) {
    const auto update = [&](std::uint64_t id) {
        if (message.empty()) runtime.row_errors.erase(id);
        else runtime.row_errors[id] = message;
    };
    if (action.chart_id) update(action.chart_id);
    if (action.selected_count <= WORM_UI_PROJECT_CHART_CAPACITY)
        for (size_t index = 0; index < action.selected_count; ++index) update(action.selected_chart_ids[index]);
}
} // namespace

auto project_chart_runtime_t::preview_job(task_scheduler_t& scheduler) -> output_preview_job_t& {
    if (!preview_job_ || (!preview_job_->is_busy() && !preview_job_->is_bound_to(scheduler)))
        preview_job_.emplace(scheduler, 1, "Preview failed.",
                             "scheduler outstanding-task capacity exceeded", "Preview failed.",
                             async_job_destruction_policy_t::release);
    return *preview_job_;
}

void project_chart_runtime_t::cancel_preview() {
    if (preview_job_) {
        preview_job_->cancel();
        preview_job_.reset();
    }
}

namespace {
void poll_output_group_preview(project_chart_runtime_t& runtime, const song_document_t& doc) {
    if (runtime.output_group_song != doc.id || runtime.output_group_revision != doc.revision) {
        runtime.cancel_preview();
        runtime.output_group_status.assign(doc.output_groups.size(), "Preview not run");
        runtime.output_group_song = doc.id;
        runtime.output_group_revision = doc.revision;
    }
    if (!runtime.preview_job())
        return;
    const auto result = runtime.preview_job()->poll();
    if (!result) return;
    const auto index =
        result->has_value() ? result->value().identity.index : result->error().identity.index;
    runtime.output_group_status[index] =
        result->has_value() ? *result->value().payload : result->error().cause.message;
}
void build_output_group_view(project_chart_runtime_t& runtime, const song_document_t& doc,
                             WormUiSetupView& view) {
    poll_output_group_preview(runtime, doc);
    if (!view.visible) return;
    const auto count = std::min(doc.output_groups.size(), maximum_project_chart_rows);
    runtime.output_group_charts.resize(count);
    runtime.output_group_paths.resize(count);
    runtime.output_groups.clear();
    runtime.output_groups.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const auto& group = doc.output_groups[i];
        auto& charts = runtime.output_group_charts[i];
        charts.clear();
        for (const auto id : group.charts) charts.push_back(id.value);
        runtime.output_group_paths[i] = group.relative_output_path.generic_string();
        runtime.output_groups.push_back({i, group.name.c_str(), static_cast<uint8_t>(group.family),
            runtime.output_group_paths[i].c_str(), charts.data(), charts.size(), runtime.output_group_status[i].c_str()});
    }
    view.output_groups = runtime.output_groups.data();
    view.output_group_count = count;
    view.output_groups_truncated = count != doc.output_groups.size();
}
auto output_preview(const song_document_t& doc, const output_group_t& group) -> std::string {
    if (group.charts.empty()) return "Add at least one chart before exporting.";
    conversion_request_t request;
    request.target = group.family;
    if (group.family != format_family_t::native_project) request.charts = group.charts;
    request.loss_policy = loss_policy_t::warn;
    if (group.family == format_family_t::stepmania)
        request.stepmania = stepmania_export_policy_t{.file_profile =
            group.relative_output_path.extension() == ".sm" ? stepmania_profile_t::sm : stepmania_profile_t::ssc};
    const auto result = convert_document(doc, request);
    const auto error = std::ranges::find(result.diagnostics, diagnostic_severity_t::error,
        &conversion_diagnostic_t::severity);
    if (error != result.diagnostics.end()) return "Cannot convert: " + error->message;
    const auto losses = std::ranges::count(result.diagnostics, true, &conversion_diagnostic_t::loss);
    return "Preview: " + std::to_string(result.outputs.size()) + " file(s), " +
        std::to_string(losses) + " conversion warning(s).";
}
auto run_output_preview(const output_preview_request_t& request, std::stop_token stop)
    -> output_preview_job_t::work_outcome_t {
    if (stop.stop_requested())
        return std::unexpected(
            error_t<task_error_code_t>{task_error_code_t::cancelled, "Preview cancelled."});
    return output_preview(request.document, request.group);
}
auto start_output_preview(project_chart_runtime_t& runtime, const song_document_t& doc,
    size_t index, task_scheduler_t& scheduler) -> project_chart_ui_result_t {
    poll_output_group_preview(runtime, doc);
    if (runtime.preview_job() && runtime.preview_job()->is_busy())
        return {.status = "Wait for the current conversion preview."};
    const auto started = runtime.preview_job(scheduler).request(
        [&doc, index] {
            return output_preview_request_t{.document = doc, .group = doc.output_groups[index]};
        },
        {.document = {.project = doc.id, .document_revision = doc.revision}, .index = index},
        run_output_preview);
    if (!started)
        return {.status = started.error().message};
    runtime.output_group_status[index] = "Preview running…";
    return {.status = "Conversion preview started.", .severity = notification_severity_t::info};
}
} // namespace

auto apply_project_output_group_ui_action(project_chart_runtime_t& runtime,
    document_history_t& history, const WormUiProjectOutputGroupAction& action,
    task_scheduler_t& scheduler) -> project_chart_ui_result_t {
    const document_stamp_t stamp{.project = song_id_t{action.project_id},
                                 .revision = action.document_revision};
    if (!stamp.is_current(history.document) || action.session_generation != runtime.session_generation)
        return {.status = stale_action_message("Output group action")};
    const bool create = action.kind == WORM_UI_PROJECT_OUTPUT_GROUP_CREATE;
    if (!create && action.group_index >= history.document.output_groups.size())
        return {.status = "Output group is no longer available."};
    if (action.kind == WORM_UI_PROJECT_OUTPUT_GROUP_PREVIEW)
        return start_output_preview(runtime, history.document, action.group_index, scheduler);
    document_command_result_t result;
    if (action.kind == WORM_UI_PROJECT_OUTPUT_GROUP_DELETE)
        result = apply_delete_output_group_command(history, action.group_index);
    else {
        if (action.kind != WORM_UI_PROJECT_OUTPUT_GROUP_CREATE && action.kind != WORM_UI_PROJECT_OUTPUT_GROUP_UPDATE)
            return {.status = "Output group action is invalid."};
        if (!action.name || !action.path || action.chart_count > WORM_UI_PROJECT_CHART_CAPACITY)
            return {.status = "Output group fields or membership are invalid."};
        output_group_t group{.name = action.name, .family = static_cast<format_family_t>(action.family),
            .relative_output_path = action.path};
        for (size_t i = 0; i < action.chart_count; ++i) group.charts.push_back(chart_id_t{action.chart_ids[i]});
        result = apply_set_output_group_command(
            history, create ? std::nullopt : std::optional{action.group_index}, std::move(group));
    }
    if (!result) return {.status = result.diagnostics.empty() ? "Output group was not changed." : result.diagnostics.front().message};
    return {.status = "Output groups updated.", .applied = true, .severity = notification_severity_t::success};
}

void build_project_chart_view(project_chart_runtime_t& runtime, const song_document_t& document,
                              WormUiSetupView& view) {
    build_output_group_view(runtime, document, view);
    if (!view.visible) return;
    if (!runtime.cache.populated || runtime.cache.revision != document.revision)
        std::erase_if(runtime.row_errors, [&](const auto& error) {
            return std::ranges::none_of(document.charts, [&](const auto& chart) {
                return chart.id.value == error.first;
            });
        });
    const auto& table =
        project_chart_table_view_t::from(runtime.cache, document, runtime.session_generation);
    runtime.rows.clear();
    runtime.rows.reserve(table.rows.size());
    for (const auto& row : table.rows) {
        const auto error = runtime.row_errors.find(row.chart.value);
        runtime.rows.push_back({row.chart.value, row.name.c_str(), row.author.c_str(), row.rating.c_str(),
            row.mode.c_str(), row.key_count, row.output_groups.c_str(), row.note_count, row.length.c_str(),
            error == runtime.row_errors.end() ? row.diagnostic.c_str() : error->second.c_str()});
    }
    view.song_id = document.id.value;
    view.document_revision = document.revision;
    view.session_generation = runtime.session_generation;
    view.charts = runtime.rows.data();
    view.chart_row_count = runtime.rows.size();
    view.charts_truncated = table.rows_truncated;
}

auto apply_project_chart_ui_action(project_chart_runtime_t& runtime,
    document_history_t& history, const WormUiProjectChartAction& action,
    std::optional<chart_id_t> active_chart) -> project_chart_ui_result_t {
    if (action.session_generation != runtime.session_generation)
        return {.status = stale_action_message("Project chart action")};
    const auto kind = ui_command_kind(action.kind);
    if (!kind) return {.status = "Project chart action is invalid."};
    project_chart_command_t command{.kind = *kind, .expected_revision = action.document_revision,
        .chart = chart_id_t{action.chart_id}, .target_chart = chart_id_t{action.target_chart_id},
        .insert_after = action.insert_after};
    if (*kind == project_chart_command_kind_t::create && active_chart) command.chart = *active_chart;
    if (*kind == project_chart_command_kind_t::batch_metadata) {
        const auto metadata = ui_batch_metadata(action);
        if (!metadata) {
            const std::string error = "Batch metadata selection or rating is invalid.";
            publish_row_error(runtime, action, error);
            return {.status = error};
        }
        command.metadata = *metadata;
    }
    auto result = apply_project_chart_command(history, command);
    const auto error = result.command.diagnostics.empty() ? std::string{} : result.command.diagnostics.front().message;
    publish_row_error(runtime, action, error);
    if (!error.empty()) {
        const auto severity = result.command.diagnostics.front().severity;
        return {.status = error,
                .severity = to_notification_severity(severity, notification_severity_t::info)};
    }
    return {.status = result.command.applied ? "Project chart changes applied." : "Project chart action completed.",
        .open_chart = result.open_chart, .applied = result.command.applied,
        .severity = result.command.applied ? notification_severity_t::success : notification_severity_t::info};
}

} // namespace wormedit::sys
