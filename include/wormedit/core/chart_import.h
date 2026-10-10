#pragma once

#include <wormedit/core/command.h>

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace wormedit {

// Imports charts from another parsed song (a chart file read through the normal importers)
// into the current song. The source song is only read. Resource files are matched by their
// resolved absolute path, then by identical content; everything else is added or reported
// missing.

struct chart_import_paths_t {
    // Directory the source's relative asset paths are resolved against (the chart file's
    // directory for an imported file).
    std::filesystem::path source_base;
    // The same for the target project.
    std::filesystem::path target_base;
};

struct chart_import_difficulty_t {
    chart_id_t chart;
    std::string name;
    format_family_t family{format_family_t::native_project};
    std::size_t measures{};
    std::size_t notes{};
};

enum class chart_import_resource_action_t {
    reuse,    // An identical file is already in the project.
    add,      // A new catalog entry (and asset) is created.
    missing,  // The file does not exist; the entry is added flagged missing.
};

struct chart_import_resource_t {
    resource_id_t source;
    std::string name;
    chart_import_resource_action_t action{chart_import_resource_action_t::add};
    // The existing project resource that will be reused.
    std::optional<resource_id_t> existing;
    std::filesystem::path path;  // Resolved absolute source file.
};

struct chart_import_timing_difference_t {
    chart_id_t chart;
    std::string message;
};

struct chart_import_proposal_t {
    std::vector<chart_import_difficulty_t> difficulties;
    // Resources the selected charts use, in source catalog order.
    std::vector<chart_import_resource_t> resources;
    std::vector<chart_import_timing_difference_t> timing_differences;
    diagnostic_list_t diagnostics;

    [[nodiscard]] auto count(chart_import_resource_action_t action) const -> std::size_t;
    [[nodiscard]] explicit operator bool() const noexcept { return diagnostics.empty(); }
};

// Every difficulty of the source, for the picker.
[[nodiscard]] auto list_chart_import_difficulties(const song_document_t& source)
    -> std::vector<chart_import_difficulty_t>;

// Plans importing `selected` (source chart IDs; empty plans nothing). Unknown charts are an
// error diagnostic.
[[nodiscard]] auto plan_chart_import(const song_document_t& target, const song_document_t& source,
                                     const chart_import_paths_t& paths,
                                     std::span<const chart_id_t> selected) -> chart_import_proposal_t;

struct chart_import_result_t {
    document_command_result_t command;
    std::vector<chart_id_t> charts;  // The IDs the imported charts received.
    [[nodiscard]] explicit operator bool() const noexcept { return static_cast<bool>(command); }
};

// Adds the selected source charts with fresh identities, reuses or adds the resources they
// reference, and commits one undoable command. The current charts and resources are unchanged
// except for the additions. Parse, validation and revision failures add nothing.
[[nodiscard]] auto apply_chart_import(document_history_t& history, std::uint64_t expected_revision,
                                      const song_document_t& source,
                                      const chart_import_paths_t& paths,
                                      std::span<const chart_id_t> selected) -> chart_import_result_t;

} // namespace wormedit
