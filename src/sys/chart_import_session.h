#pragma once

#include "ui/frontend.h"

#include <wormedit/core/chart_import.h>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace wormedit::sys {

// The Project tab's "Import chart…" dialog state: the chart file read into a temporary song,
// which difficulties are ticked, and the plan the core computed for them. Reading goes through
// open_project_file, so every importer the editor opens is available. The session never changes
// the project; chart_import_confirm commits one undoable command.
struct chart_import_session_t {
    bool open{};
    std::string path_text;
    std::optional<song_document_t> source;
    std::filesystem::path source_base;
    std::vector<chart_id_t> selected;
    chart_import_proposal_t proposal;
    std::string error;

    // Borrowed by the frontend view; rebuilt after every change.
    std::vector<wormedit::ui::chart_import_row_view_t> row_views;
    std::vector<std::string> row_labels;
    std::string summary;
    std::vector<std::string> note_lines;
};

void chart_import_begin(chart_import_session_t& session);
void chart_import_cancel(chart_import_session_t& session);

// Reads the chart file and plans the ticked difficulties against the project.
void chart_import_load(chart_import_session_t& session, const std::filesystem::path& path,
                       const song_document_t& target, const std::filesystem::path& target_base);

void chart_import_toggle(chart_import_session_t& session, chart_id_t chart,
                         const song_document_t& target, const std::filesystem::path& target_base);

// Imports the ticked difficulties; the session closes only when the import applied.
auto chart_import_confirm(chart_import_session_t& session, document_history_t& history,
                          std::uint64_t expected_revision, const std::filesystem::path& target_base)
    -> chart_import_result_t;


} // namespace wormedit::sys
