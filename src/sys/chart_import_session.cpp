#include "sys/chart_import_session.h"
#include "sys/metadata_commands.h"

#include "sys/project_open.h"

#include <algorithm>

namespace wormedit::sys {
namespace {

constexpr std::size_t listed_missing_files = 8;

auto family_name(format_family_t family) -> const char* {
    switch (family) {
    case format_family_t::bms: return "BMS";
    case format_family_t::bmson: return "BMSON";
    case format_family_t::stepmania: return "StepMania";
    case format_family_t::osu_mania: return "osu!mania";
    default: return "WormEdit";
    }
}

auto is_selected(const chart_import_session_t& session, chart_id_t chart) -> bool {
    return std::ranges::find(session.selected, chart) != session.selected.end();
}

void rebuild_view_storage(chart_import_session_t& session) {
    session.row_labels.clear();
    session.row_views.clear();
    session.note_lines.clear();
    session.summary.clear();
    const auto difficulties = session.source ? list_chart_import_difficulties(*session.source)
                                             : std::vector<chart_import_difficulty_t>{};
    for (const auto& item : difficulties)
        session.row_labels.push_back((item.name.empty() ? std::string{"Untitled"} : item.name) + " · " +
            family_name(item.family) + " · " + std::to_string(item.notes) + " notes");
    for (std::size_t index = 0; index < difficulties.size(); ++index)
        session.row_views.push_back({.chart_id = difficulties[index].chart.value,
            .label = session.row_labels[index].c_str(),
            .selected = is_selected(session, difficulties[index].chart)});
    if (session.selected.empty() || !session.proposal.diagnostics.empty()) return;
    const auto& proposal = session.proposal;
    session.summary = std::to_string(proposal.count(chart_import_resource_action_t::reuse)) + " sounds reused · " +
                      std::to_string(proposal.count(chart_import_resource_action_t::add)) + " added · " +
                      std::to_string(proposal.count(chart_import_resource_action_t::missing)) + " missing";
    std::size_t listed{};
    for (const auto& item : proposal.resources) {
        if (item.action != chart_import_resource_action_t::missing) continue;
        if (listed++ < listed_missing_files)
            session.note_lines.push_back("Missing file: " + (item.path.empty() ? item.name : item.path.filename().string()));
    }
    if (listed > listed_missing_files)
        session.note_lines.push_back("… and " + std::to_string(listed - listed_missing_files) + " more missing files");
    for (const auto& difference : proposal.timing_differences) session.note_lines.push_back(difference.message);
    if (session.note_lines.size() > WORM_UI_CHART_IMPORT_NOTE_CAPACITY)
        session.note_lines.resize(WORM_UI_CHART_IMPORT_NOTE_CAPACITY);
}

void replan(chart_import_session_t& session, const song_document_t& target,
            const std::filesystem::path& target_base) {
    session.proposal = {};
    if (session.source && !session.selected.empty())
        session.proposal = plan_chart_import(target, *session.source,
                                             {session.source_base, target_base}, session.selected);
    rebuild_view_storage(session);
}

} // namespace

void chart_import_begin(chart_import_session_t& session) {
    session = {};
    session.open = true;
}

void chart_import_cancel(chart_import_session_t& session) { session = {}; }

void chart_import_load(chart_import_session_t& session, const std::filesystem::path& path,
                       const song_document_t& target, const std::filesystem::path& target_base) {
    session.path_text = path.string();
    session.source.reset();
    session.selected.clear();
    session.error.clear();
    auto opened = open_project_file(path);
    if (opened.needs_encoding_choice) {
        session.error = "This chart needs a text encoding choice. Open it as a project once to pick one.";
    } else if (!opened.is_success()) {
        session.error = opened.error.empty() ? std::string{"The chart file could not be read."} : opened.error;
    } else if (opened.document->charts.empty()) {
        session.error = "The file contains no charts.";
    } else {
        session.source = std::move(opened.document);
        session.source_base = opened.asset_base;
        if (session.source->charts.size() == 1) session.selected.push_back(session.source->charts.front().id);
    }
    replan(session, target, target_base);
}

void chart_import_toggle(chart_import_session_t& session, chart_id_t chart,
                         const song_document_t& target, const std::filesystem::path& target_base) {
    if (!session.source ||
        session.source->find_chart(chart) == nullptr)
        return;
    const auto found = std::ranges::find(session.selected, chart);
    if (found == session.selected.end()) session.selected.push_back(chart);
    else session.selected.erase(found);
    replan(session, target, target_base);
}

auto chart_import_confirm(chart_import_session_t& session, document_history_t& history,
                          std::uint64_t expected_revision, const std::filesystem::path& target_base)
    -> chart_import_result_t {
    if (!session.source) return {.command = {.revision = history.document.revision, .diagnostics = {{
        .severity = diagnostic_severity_t::error, .code = diagnostic_code_t::invalid_id,
        .message = "Read a chart file before importing."}}}, .payload = {}};
    auto rejected = metadata_command_detail::stale_rejection(
        history, expected_revision, "Chart import");
    auto result = rejected
        ? chart_import_result_t{.command = std::move(*rejected), .payload = {}}
        : apply_chart_import(history, *session.source,
                             {session.source_base, target_base}, session.selected);
    if (result.command.applied) chart_import_cancel(session);
    else {
        session.error = result.command.diagnostics.empty() ? std::string{"The import was rejected."}
                                                           : result.command.diagnostics.front().message;
        rebuild_view_storage(session);
    }
    return result;
}

auto chart_import_view(const chart_import_session_t& session, std::uint64_t document_revision)
    -> WormUiChartImportView {
    WormUiChartImportView view{};
    view.visible = session.open;
    view.path = session.path_text.c_str();
    view.error = session.error.c_str();
    view.rows = session.row_views.data();
    view.row_count = session.row_views.size();
    view.summary = session.summary.c_str();
    for (const auto& line : session.note_lines)
        if (view.note_count < WORM_UI_CHART_IMPORT_NOTE_CAPACITY) view.notes[view.note_count++] = line.c_str();
    view.can_import = session.source && !session.selected.empty() && session.proposal.diagnostics.empty();
    view.document_revision = document_revision;
    return view;
}

} // namespace wormedit::sys
