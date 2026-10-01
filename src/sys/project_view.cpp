#include "sys/project_view.h"

#include <wormedit/core/timing.h>
#include <wormedit/core/chart_editor.h>

#include <algorithm>
#include <iomanip>
#include <locale>
#include <sstream>

namespace wormedit::sys {
namespace {
auto display_text(std::string_view text) -> std::string {
    constexpr std::size_t capacity = 4096;
    if (text.size() <= capacity) return std::string{text};
    auto end = capacity - 3;
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xc0U) == 0x80U) --end;
    return std::string{text.substr(0, end)} + "...";
}

auto decimal_value(signed_rational_t value, unsigned precision) -> std::string {
    if (value.denominator == 0) return "Invalid";
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::fixed << std::setprecision(precision) <<
        static_cast<long double>(value.numerator) / value.denominator;
    return stream.str();
}

void project_chart_mode(const chart_document_t& chart, project_chart_row_t& row) {
    if (chart.lane_layouts.empty()) { row.mode = "No lane layout"; return; }
    const auto& layout = chart.lane_layouts.front().layout;
    row.mode = display_text(layout.display_name.empty() ? layout.profile_id : layout.display_name);
    row.key_count = static_cast<std::uint32_t>(std::ranges::count_if(layout.lanes, [](const auto& lane) {
        return lane.role == lane_role_t::playable || lane.role == lane_role_t::foot_pedal;
    }));
}

void project_chart_groups(const song_document_t& document, project_chart_row_t& row) {
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

void project_chart_length(const chart_document_t& chart, project_chart_row_t& row) {
    const auto built = build_timing_index(chart);
    if (!built) {
        row.length = "Unavailable";
        row.diagnostic = built.diagnostics.empty() ? "Chart timing is unavailable."
            : display_text(built.diagnostics.front().message);
        return;
    }
    const auto begin = timing_at(*built.index, 0, timing_boundary_side_t::before_delays);
    const auto end = timing_at(*built.index, built.index->total_rows, timing_boundary_side_t::after_stops);
    if (!begin || !end) { row.length = "Unavailable"; row.diagnostic = "Chart length query failed."; return; }
    const auto seconds = static_cast<long double>(end.seconds->numerator) / end.seconds->denominator -
                         static_cast<long double>(begin.seconds->numerator) / begin.seconds->denominator;
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::fixed << std::setprecision(2) << seconds << " s";
    row.length = stream.str();
}

auto project_chart_row(const song_document_t& document, const chart_document_t& chart) -> project_chart_row_t {
    project_chart_row_t row{.chart = chart.id, .name = display_text(chart.metadata.name),
        .author = display_text(chart.metadata.author),
        .rating = chart.metadata.rating ? decimal_value(*chart.metadata.rating, 2) : "Unset"};
    project_chart_mode(chart, row);
    project_chart_groups(document, row);
    for (const auto& measure : chart.measures) row.note_count += measure.notes.size();
    project_chart_length(chart, row);
    return row;
}
} // namespace

auto apply_project_chart_command(document_history_t& history,
    const project_chart_command_t& action) -> project_chart_command_result_t {
    if (action.kind == project_chart_command_kind_t::create) {
        auto created = apply_new_chart_command(history, action.expected_revision);
        return {.command = std::move(created.command), .open_chart = created.chart};
    }
    if (action.kind == project_chart_command_kind_t::duplicate) {
        auto copy = apply_duplicate_chart_command(history, action.expected_revision, action.chart);
        return {.command = std::move(copy.command)};
    }
    if (action.kind == project_chart_command_kind_t::erase)
        return {.command = apply_delete_chart_command(history, action.expected_revision, action.chart)};
    if (action.kind == project_chart_command_kind_t::batch_metadata)
        return {.command = apply_batch_chart_metadata_edit(history, action.expected_revision, action.metadata)};
    const auto chart = std::ranges::find(history.document.charts, action.chart, &chart_document_t::id);
    if (action.expected_revision != history.document.revision || chart == history.document.charts.end())
        return {.command = {.revision = history.document.revision, .diagnostics = {{
            .severity = diagnostic_severity_t::error, .code = diagnostic_code_t::invalid_id,
            .message = "Project chart action is stale or its chart no longer exists."}}}};
    if (action.kind == project_chart_command_kind_t::open)
        return {.command = {.revision = history.document.revision}, .open_chart = action.chart};
    const auto index = static_cast<std::size_t>(chart - history.document.charts.begin());
    const bool up = action.kind == project_chart_command_kind_t::move_up;
    if ((up && index == 0) || (!up && index + 1 == history.document.charts.size()))
        return {.command = {.revision = history.document.revision}};
    return {.command = apply_reorder_chart_command(history, action.expected_revision, action.chart,
                                                   up ? index - 1 : index + 1)};
}

auto build_project_chart_table(const song_document_t& document) -> project_chart_table_t {
    const auto count = std::min(document.charts.size(), maximum_project_chart_rows);
    project_chart_table_t table{.total_charts = document.charts.size(),
        .rows_truncated = document.charts.size() > count};
    table.rows.reserve(count);
    for (std::size_t index = 0; index < count; ++index)
        table.rows.push_back(project_chart_row(document, document.charts[index]));
    return table;
}

auto project_chart_table_for(project_chart_table_cache_t& cache,
    const song_document_t& document, std::uint64_t session_generation) -> const project_chart_table_t& {
    if (!cache.populated || cache.song != document.id || cache.session_generation != session_generation ||
        cache.revision != document.revision) {
        cache.table = build_project_chart_table(document);
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

void project_chart_view(project_chart_runtime_t& runtime, const song_document_t& document,
    WormUiSetupView& view) {
    if (!view.visible) return;
    if (!runtime.cache.populated || runtime.cache.revision != document.revision)
        std::erase_if(runtime.row_errors, [&](const auto& error) {
            return std::ranges::none_of(document.charts, [&](const auto& chart) {
                return chart.id.value == error.first;
            });
        });
    const auto& table = project_chart_table_for(runtime.cache, document, runtime.session_generation);
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
    document_history_t& history, const WormUiProjectChartAction& action) -> project_chart_ui_result_t {
    if (action.session_generation != runtime.session_generation)
        return {.status = "Project chart action belongs to a closed session."};
    const auto kind = ui_command_kind(action.kind);
    if (!kind) return {.status = "Project chart action is invalid."};
    project_chart_command_t command{.kind = *kind, .expected_revision = action.document_revision,
        .chart = chart_id_t{action.chart_id}};
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
    if (!error.empty()) return {.status = error};
    return {.status = result.command.applied ? "Project chart changes applied." : "Project chart action completed.",
        .open_chart = result.open_chart, .applied = result.command.applied};
}

} // namespace wormedit::sys
