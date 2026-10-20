#include "sys/project_view.h"
#include "sys/output_group_export.h"
#include "core/text_util.h"
#include "sys/metadata_commands.h"
#include <wormedit/core/rational.h>

#include <wormedit/core/timing.h>
#include <wormedit/core/chart_editor.h>
#include <wormedit/core/lane_profiles.h>
#include <wormedit/formats/conversion.h>

#include <algorithm>
#include <iomanip>
#include <functional>
#include <locale>
#include <sstream>

namespace wormedit::sys {
namespace {
constexpr std::size_t maximum_project_display_text_bytes = 4096;

auto display_text(std::string_view text) -> std::string {
    if (text.size() <= maximum_project_display_text_bytes) return std::string{text};
    return std::string{text_util::truncate_utf8(text, maximum_project_display_text_bytes - 3)} + "...";
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
        if (names.size() > maximum_project_display_text_bytes) continue;
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
    for (std::size_t family = 0; family < row.output_incompatibility_reasons.size(); ++family) {
        const auto compatibility = output_family_compatibility(
            chart, static_cast<format_family_t>(family));
        if (compatibility.compatible)
            row.compatible_output_families |= std::uint32_t{1} << family;
        else
            row.output_incompatibility_reasons[family] = compatibility.reason;
    }
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
    const auto count = document.charts.size();
    project_chart_table_view_t table{.total_charts = document.charts.size()};
    table.rows.reserve(count);
    for (std::size_t index = 0; index < count; ++index)
        table.rows.push_back(project_chart_row_view_t::from(document, document.charts[index]));
    return table;
}

auto project_chart_table_view_t::from(project_chart_table_cache_t& cache,
                                    const song_document_t& document,
                                    std::uint64_t session_generation)
    -> const project_chart_table_view_t& {
    const project_chart_table_cache_key_t key{.document = document_stamp_t::from(document),
                                             .session_generation = session_generation};
    using builder_t = auto (*)(const song_document_t&) -> project_chart_table_view_t;
    return cache.get_or_build(key, std::bind_front(
        static_cast<builder_t>(&project_chart_table_view_t::from), std::cref(document)));
}
namespace {
void publish_row_error(project_chart_runtime_t& runtime, const wormedit::ui::project_chart_action_t& action,
    const std::string& message) {
    const auto update = [&](std::uint64_t id) {
        if (message.empty()) runtime.row_errors.erase(id);
        else runtime.row_errors[id] = message;
    };
    if (action.chart_id) update(action.chart_id.value);
    if (action.selected_chart_ids.size() <= maximum_batch_chart_metadata_charts)
        for (size_t index = 0; index < action.selected_chart_ids.size(); ++index) update(action.selected_chart_ids[index].value);
}
} // namespace

auto project_chart_command_t::from(const ui::project_chart_action_t& action,
                                  std::optional<chart_id_t> active_chart)
    -> result_t<project_chart_command_t, std::string> {
    project_chart_command_t command{.expected_revision = action.document_revision,
        .chart = action.chart_id, .target_chart = action.target_chart_id,
        .insert_after = action.insert_after};
    switch (action.kind) {
    case WORM_UI_PROJECT_CHART_NEW: command.kind = project_chart_command_kind_t::create; break;
    case WORM_UI_PROJECT_CHART_DUPLICATE: command.kind = project_chart_command_kind_t::duplicate; break;
    case WORM_UI_PROJECT_CHART_DELETE: command.kind = project_chart_command_kind_t::erase; break;
    case WORM_UI_PROJECT_CHART_UP: command.kind = project_chart_command_kind_t::move_up; break;
    case WORM_UI_PROJECT_CHART_DOWN: command.kind = project_chart_command_kind_t::move_down; break;
    case WORM_UI_PROJECT_CHART_OPEN: command.kind = project_chart_command_kind_t::open; break;
    case WORM_UI_PROJECT_CHART_BATCH_METADATA: command.kind = project_chart_command_kind_t::batch_metadata; break;
    case WORM_UI_PROJECT_CHART_REORDER: command.kind = project_chart_command_kind_t::reorder; break;
    default: return std::unexpected("Project chart action is invalid.");
    }
    if (command.kind == project_chart_command_kind_t::create && active_chart)
        command.chart = *active_chart;
    if (command.kind != project_chart_command_kind_t::batch_metadata) return command;
    if (action.selected_chart_ids.empty() ||
        action.selected_chart_ids.size() > maximum_batch_chart_metadata_charts)
        return std::unexpected("Batch metadata selection or rating is invalid.");
    command.metadata.charts = action.selected_chart_ids;
    if (action.edit_author) command.metadata.author = action.author;
    command.metadata.edit_rating = action.edit_rating;
    if (action.edit_rating && !action.unset_rating) {
        command.metadata.rating = parse_metadata_rating(action.rating_numerator, action.rating_denominator);
        if (!command.metadata.rating)
            return std::unexpected("Batch metadata selection or rating is invalid.");
    }
    return command;
}

auto project_chart_runtime_t::preview_job(task_scheduler_t& scheduler) -> output_preview_job_t& {
    if (!preview_job_ || (!preview_job_->is_busy() && !preview_job_->is_bound_to(scheduler)))
        preview_job_.emplace(scheduler, 1, "Preview failed.",
                             "scheduler outstanding-task capacity exceeded", "Preview failed.",
                             async_job_destruction_policy_t::release);
    return *preview_job_;
}

void project_chart_runtime_t::cancel_preview() {
    preview_progress_.reset();
    preview_cancelling_ = false;
    if (preview_job_) {
        preview_job_->cancel();
        preview_job_.reset();
    }
}

void project_chart_runtime_t::request_cancel_preview() {
    if (!preview_job_ || !preview_job_->is_busy()) return;
    preview_cancelling_ = true;
    preview_job_->cancel(async_job_cancel_policy_t::retain_completion);
}

auto project_chart_runtime_t::preview_view() const -> ui::file_job_view_t {
    return {.active = preview_job_ && preview_job_->is_busy(), .cancelling = preview_cancelling_,
            .completed = preview_progress_ ? preview_progress_->completed.load() : 0,
            .total = preview_progress_ ? preview_progress_->total.load() : 0,
            .label = "Converting and checking preview artifacts"};
}

namespace {
void poll_output_group_preview(project_chart_runtime_t& runtime, const song_document_t& doc) {
    if (runtime.output_group_song != doc.id || runtime.output_group_revision != doc.revision) {
        runtime.cancel_preview();
        runtime.output_group_status.assign(doc.output_groups.size(), "Preview not run");
        runtime.output_group_paths.clear();
        runtime.output_group_paths.resize(doc.output_groups.size());
        runtime.output_group_plan_paths.clear();
        runtime.output_group_plan_paths.resize(doc.output_groups.size());
        runtime.output_group_previews.clear();
        runtime.output_group_previews.resize(doc.output_groups.size());
        runtime.output_group_song = doc.id;
        runtime.output_group_revision = doc.revision;
    }
    if (!runtime.preview_job())
        return;
    const auto result = runtime.preview_job()->poll();
    if (!result) return;
    const auto index =
        result->has_value() ? result->value().identity.index : result->error().identity.index;
    if (runtime.is_preview_cancelling()) {
        runtime.output_group_status[index] = "Conversion preview cancelled.";
    } else if (!result->has_value()) {
        runtime.output_group_status[index] = result->error().cause.message;
    } else {
        runtime.output_group_previews[index] = std::move(*result->value().payload);
        runtime.output_group_plan_paths[index].clear();
        const auto& preview = *runtime.output_group_previews[index];
        const auto warnings = std::ranges::count(
            preview.converted.diagnostics, diagnostic_severity_t::warning,
            &conversion_diagnostic_t::severity);
        const auto errors = std::ranges::count(
            preview.converted.diagnostics, diagnostic_severity_t::error,
            &conversion_diagnostic_t::severity);
        runtime.output_group_status[index] = "Preview: " +
            std::to_string(preview.files.size()) + " file(s), " + std::to_string(warnings) +
            " conversion warning(s), " + std::to_string(errors) + " conversion error(s).";
    }
}

auto ui_plan_action(output_group_export_file_action_t action) -> WormUiOutputPlanAction {
    switch (action) {
    case output_group_export_file_action_t::new_file: return WORM_UI_OUTPUT_PLAN_NEW;
    case output_group_export_file_action_t::replace: return WORM_UI_OUTPUT_PLAN_REPLACE;
    case output_group_export_file_action_t::reuse: return WORM_UI_OUTPUT_PLAN_REUSE;
    case output_group_export_file_action_t::copy: return WORM_UI_OUTPUT_PLAN_COPY;
    }
    return WORM_UI_OUTPUT_PLAN_NEW;
}

void build_output_preview_view(project_chart_runtime_t& runtime, std::size_t index,
                               ui::project_output_group_view_t& view) {
    constexpr std::size_t maximum_plan_rows = 512;
    if (!runtime.output_group_previews[index]) {
        view.plan_rows.clear();
        view.diagnostics.clear();
        view.plan_rows_truncated = false;
        view.diagnostics_truncated = false;
        return;
    }
    const auto& preview = *runtime.output_group_previews[index];
    const auto file_count = std::min(preview.files.size(), maximum_plan_rows);
    auto& paths = runtime.output_group_plan_paths[index];
    if (paths.size() != file_count) {
        paths.resize(file_count);
        for (std::size_t row = 0; row < file_count; ++row)
            paths[row] = preview.files[row].path.generic_string();
    }
    view.plan_rows.resize(file_count);
    for (std::size_t row = 0; row < file_count; ++row)
        view.plan_rows[row] = {.path = paths[row], .action = ui_plan_action(preview.files[row].action),
                              .bytes = preview.files[row].bytes};
    const auto diagnostic_count = std::min(preview.converted.diagnostics.size(), maximum_plan_rows);
    view.diagnostics.resize(diagnostic_count);
    for (std::size_t row = 0; row < diagnostic_count; ++row)
        view.diagnostics[row] = {.message = preview.converted.diagnostics[row].message,
                                .severity = preview.converted.diagnostics[row].severity};
    view.plan_rows_truncated = file_count != preview.files.size();
    view.diagnostics_truncated = diagnostic_count != preview.converted.diagnostics.size();
}

void build_output_group_view(project_chart_runtime_t& runtime, const song_document_t& doc,
                             ui::setup_view_t& view) {
    poll_output_group_preview(runtime, doc);
    if (!view.visible) return;
    view.output_groups.resize(doc.output_groups.size());
    for (std::size_t index = 0; index < doc.output_groups.size(); ++index) {
        const auto& group = doc.output_groups[index];
        auto& projected = view.output_groups[index];
        if (runtime.output_group_paths[index].empty())
            runtime.output_group_paths[index] = group.relative_output_path.generic_string();
        projected.document_index = index;
        projected.name = group.name;
        projected.family = group.family;
        projected.path = runtime.output_group_paths[index];
        projected.destination_kind = group.destination_kind;
        projected.chart_ids.assign(group.charts.begin(), group.charts.end());
        projected.preview_status = runtime.output_group_status[index];
        projected.existing_files = group.existing_files;
        projected.stepmania = group.stepmania;
        build_output_preview_view(runtime, index, projected);
    }
}

auto run_output_preview(const output_group_export_request_t& request, std::stop_token stop)
    -> output_preview_job_t::work_outcome_t {
    if (stop.stop_requested())
        return std::unexpected(
            error_t<failure_code_t>{failure_code_t::cancelled, "Preview cancelled."});
    auto prepared = output_group_export_preparation_t::from(request, stop);
    if (!prepared)
        return std::unexpected(
            error_t<failure_code_t>{failure_code_t::invalid_request,
                                    "Cannot export: " + prepared.error()});
    return std::move(*prepared);
}
auto start_output_preview(project_chart_runtime_t& runtime, const song_document_t& doc,
    size_t index, task_scheduler_t& scheduler,
    const std::filesystem::path& asset_base) -> project_chart_ui_result_t {
    poll_output_group_preview(runtime, doc);
    if (runtime.preview_job() && runtime.preview_job()->is_busy())
        return {.status = "Wait for the current conversion preview."};
    auto progress = std::make_shared<job_progress_t>();
    const auto started = runtime.preview_job(scheduler).request(
        [&doc, index, &progress, &asset_base] {
            return output_group_export_request_t{
                .document = doc,
                .plan = output_group_export_plan_t::from(doc, doc.output_groups[index]),
                .asset_base = asset_base,
                .progress = progress};
        },
        {.document = {.project = doc.id, .document_revision = doc.revision}, .index = index},
        run_output_preview);
    if (!started)
        return {.status = started.error().message};
    runtime.begin_preview(std::move(progress));
    runtime.output_group_previews[index].reset();
    runtime.output_group_status[index] = "Preview running…";
    return {.status = "Conversion preview started.", .severity = notification_severity_t::info};
}
} // namespace

auto apply_project_output_group_ui_action(project_chart_runtime_t& runtime,
    document_history_t& history, const wormedit::ui::project_output_group_action_t& action,
    task_scheduler_t& scheduler,
    const std::filesystem::path& asset_base) -> project_chart_ui_result_t {
    const document_stamp_t stamp{.project = song_id_t{action.project_id},
                                 .revision = action.document_revision};
    if (!stamp.is_current(history.document) || action.session_generation != runtime.session_generation)
        return {.status = stale_action_message("Output group action")};
    const bool create = action.kind == WORM_UI_PROJECT_OUTPUT_GROUP_CREATE;
    if (!create && action.group_index >= history.document.output_groups.size())
        return {.status = "Output group is no longer available."};
    if (action.kind == WORM_UI_PROJECT_OUTPUT_GROUP_PREVIEW)
        return start_output_preview(runtime, history.document, action.group_index, scheduler, asset_base);
    document_command_result_t result;
    if (action.kind == WORM_UI_PROJECT_OUTPUT_GROUP_DELETE)
        result = apply_delete_output_group_command(history, action.group_index);
    else {
        if (action.kind != WORM_UI_PROJECT_OUTPUT_GROUP_CREATE && action.kind != WORM_UI_PROJECT_OUTPUT_GROUP_UPDATE)
            return {.status = "Output group action is invalid."};
        output_group_t group{.name = action.name,
                             .family = action.family,
                             .destination_kind = action.destination_kind,
                             .relative_output_path = action.path,
                             .existing_files =
                                 action.existing_files,
                             .stepmania = action.stepmania};
        group.charts = action.chart_ids;
        result = apply_set_output_group_command(
            history, create ? std::nullopt : std::optional{action.group_index}, std::move(group));
    }
    if (!result) return {.status = result.diagnostics.empty() ? "Output group was not changed." : result.diagnostics.front().message};
    return {.status = "Output groups updated.", .applied = true, .severity = notification_severity_t::success};
}

} // namespace wormedit::sys

void wormedit::ui::setup_view_t::refill(sys::project_chart_runtime_t& runtime,
                                      const song_document_t& document) {
    using namespace wormedit::sys;
    build_output_group_view(runtime, document, *this);
    if (!visible) return;
    if (!runtime.cache.has_value() || runtime.cache.key()->document.revision != document.revision)
        std::erase_if(runtime.row_errors, [&](const auto& error) {
            return std::ranges::none_of(document.charts, [&](const auto& chart) {
                return chart.id.value == error.first;
            });
        });
    const auto& table = project_chart_table_view_t::from(runtime.cache, document, runtime.session_generation);
    charts.resize(table.rows.size());
    for (std::size_t index = 0; index < table.rows.size(); ++index) {
        const auto& row = table.rows[index];
        const auto error = runtime.row_errors.find(row.chart.value);
        auto& projected = charts[index];
        projected = {.chart_id = row.chart, .name = row.name, .author = row.author,
            .rating = row.rating, .mode = row.mode, .key_count = row.key_count,
            .output_groups = row.output_groups, .compatible_output_families = row.compatible_output_families,
            .note_count = row.note_count, .length = row.length,
            .diagnostic = error == runtime.row_errors.end() ? row.diagnostic : error->second};
        for (std::size_t family = 0; family < row.output_incompatibility_reasons.size(); ++family)
            projected.output_incompatibility_reasons[family] = row.output_incompatibility_reasons[family];
    }
    song_id = document.id;
    document_revision = document.revision;
    session_generation = runtime.session_generation;
    output_suggestions.clear();
    if (document.output_groups.empty()) {
        const auto* selected = document.find_chart(selected_chart_id);
        if (!selected && !document.charts.empty()) selected = &document.charts.front();
        if (selected)
            for (const auto& suggestion : suggested_output_groups(*selected))
                output_suggestions.push_back({.name = suggestion.label, .family = suggestion.family,
                    .path = suggestion.relative_output_path, .stepmania = suggestion.stepmania});
    }
}

namespace wormedit::sys {

auto apply_project_chart_ui_action(project_chart_runtime_t& runtime,
    document_history_t& history, const wormedit::ui::project_chart_action_t& action,
    std::optional<chart_id_t> active_chart) -> project_chart_ui_result_t {
    if (action.session_generation != runtime.session_generation)
        return {.status = stale_action_message("Project chart action")};
    const auto command = project_chart_command_t::from(action, active_chart);
    if (!command) {
        if (action.kind == WORM_UI_PROJECT_CHART_BATCH_METADATA)
            publish_row_error(runtime, action, command.error());
        return {.status = command.error()};
    }
    auto result = apply_project_chart_command(history, *command);
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
