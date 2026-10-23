#pragma once
#include "timing_links.h"
#include "common.h"
#include "catalog.h"
#include <wormedit/core/vocabulary.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <utility>

namespace wormedit {
struct song_document_t;
struct program_preferences_t;
enum class diagnostic_severity_t;
namespace sys {
struct chart_import_session_t;
class project_chart_runtime_t;
}
} // namespace wormedit


namespace wormedit::ui {
struct recovery_candidate_view_t {
    std::string path_label;
    std::string summary;
    std::string_view disabled_reason;
    std::vector<wormedit::native_project_recovery_action_t> actions;
    std::uint64_t scan_generation{};
    std::size_t row_index{}, candidate_index{};

    [[nodiscard]] auto can(wormedit::native_project_recovery_action_t action) const -> bool {
        return std::ranges::find(actions, action) != actions.end();
    }
    auto operator==(const recovery_candidate_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

typedef enum WormUiWorkspaceActionKind {
    WORM_UI_WORKSPACE_ACTION_NONE = 0,
    WORM_UI_WORKSPACE_ACTION_NEW_PROJECT,
    WORM_UI_WORKSPACE_ACTION_OPEN_FILE,
    WORM_UI_WORKSPACE_ACTION_CANCEL_BMS_ENCODING,
    WORM_UI_WORKSPACE_ACTION_CLOSE_PROJECT,
    WORM_UI_WORKSPACE_ACTION_SAVE_PROJECT,
    WORM_UI_WORKSPACE_ACTION_SWITCH_TAB,
    WORM_UI_WORKSPACE_ACTION_QUIT,
    WORM_UI_WORKSPACE_ACTION_TRANSPORT_PLAY_PAUSE,
    WORM_UI_WORKSPACE_ACTION_TRANSPORT_STOP,
    WORM_UI_WORKSPACE_ACTION_TRANSPORT_TOGGLE_METRONOME,
    WORM_UI_WORKSPACE_ACTION_TRANSPORT_SEEK,
    WORM_UI_WORKSPACE_ACTION_TRANSPORT_SET_RATE,
    WORM_UI_WORKSPACE_ACTION_TRANSPORT_SET_KEEP_PITCH,
    WORM_UI_WORKSPACE_ACTION_SELECT_AUDIO_ASSET,
    WORM_UI_WORKSPACE_ACTION_SELECT_SOUND_RESOURCE,
    WORM_UI_WORKSPACE_ACTION_SOUND_USAGE_ACTIVATE,
    WORM_UI_WORKSPACE_ACTION_BGA_USAGE_ACTIVATE,
    WORM_UI_WORKSPACE_ACTION_SELECT_VISUAL_ASSET,
    WORM_UI_WORKSPACE_ACTION_TOGGLE_PLAYER_PREVIEW,
    WORM_UI_WORKSPACE_ACTION_SELECT_PLAYER_PREVIEW_SKIN,
    WORM_UI_WORKSPACE_ACTION_NAVIGATE_PREVIOUS_MARKER,
    WORM_UI_WORKSPACE_ACTION_NAVIGATE_NEXT_MARKER,
    WORM_UI_WORKSPACE_ACTION_RECOVERY_INSPECT,
    WORM_UI_WORKSPACE_ACTION_RECOVERY_RECOVER,
    WORM_UI_WORKSPACE_ACTION_RECOVERY_SAVE_AS,
    WORM_UI_WORKSPACE_ACTION_RECOVERY_DISCARD,
    WORM_UI_WORKSPACE_ACTION_RECOVERY_QUARANTINE,
    WORM_UI_WORKSPACE_ACTION_CANCEL_AUDIO_PROJECT,
    WORM_UI_WORKSPACE_ACTION_COMPARE_SELECT_CHART,
    WORM_UI_WORKSPACE_ACTION_COMPARE_SCROLL,
    WORM_UI_WORKSPACE_ACTION_COMPARE_DIAGNOSTIC_JUMP,
    /* The native picker returned a chart file for the chart import dialog (project_path). */
    WORM_UI_WORKSPACE_ACTION_IMPORT_CHART_FILE,
} WormUiWorkspaceActionKind;

typedef enum WormUiChartImportActionKind {
    WORM_UI_CHART_IMPORT_NONE = 0,
    WORM_UI_CHART_IMPORT_BEGIN,    /* Open the dialog. */
    WORM_UI_CHART_IMPORT_LOAD,     /* Read the chart file named by project_path. */
    WORM_UI_CHART_IMPORT_TOGGLE,   /* Toggle one difficulty (chart_id). */
    WORM_UI_CHART_IMPORT_CONFIRM,  /* Import the ticked difficulties (document_revision stamp). */
    WORM_UI_CHART_IMPORT_CANCEL,
} WormUiChartImportActionKind;

namespace wormedit::ui {
struct chart_import_action_t {
    WormUiChartImportActionKind kind{};
    wormedit::chart_id_t chart_id;
    std::uint64_t document_revision{};
    auto operator==(const chart_import_action_t&) const -> bool = default;
};
} // namespace wormedit::ui

namespace wormedit::ui {
struct chart_import_row_view_t {
    wormedit::chart_id_t chart_id;
    std::string_view label;
    bool selected{};
    auto operator==(const chart_import_row_view_t&) const -> bool = default;
};
} // namespace wormedit::ui


namespace wormedit::ui {
struct chart_import_view_t {
    bool visible{};
    std::string_view path, error, summary;
    std::vector<chart_import_row_view_t> rows;
    std::vector<std::string_view> notes;
    bool can_import{};
    std::uint64_t document_revision{};

    void reset() {
        auto retained_rows = std::move(rows);
        auto retained_notes = std::move(notes);
        *this = {};
        rows = std::move(retained_rows);
        notes = std::move(retained_notes);
        rows.clear();
        notes.clear();
    }
    void refill(const wormedit::sys::chart_import_session_t&, std::uint64_t revision);
    [[nodiscard]] static auto from(const wormedit::sys::chart_import_session_t& session,
                                  std::uint64_t revision) -> chart_import_view_t;
    auto operator==(const chart_import_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

typedef enum WormUiWorkspaceTab {
    WORM_UI_WORKSPACE_TAB_SETUP = 0,
    WORM_UI_WORKSPACE_TAB_METADATA,
    WORM_UI_WORKSPACE_TAB_TIMING,
    WORM_UI_WORKSPACE_TAB_COMPOSE,
    WORM_UI_WORKSPACE_TAB_STORYBOARD,
    WORM_UI_WORKSPACE_TAB_AUDIO_SLICES,
    WORM_UI_WORKSPACE_TAB_COMPARE_VALIDATE,
} WormUiWorkspaceTab;

namespace wormedit::ui {
struct workspace_tab_item_view_t {
    std::string_view label;
    bool enabled{};
    wormedit::workspace_tab_t id{};
    auto operator==(const workspace_tab_item_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

typedef enum WormUiStepmaniaFileProfile {
    WORM_UI_STEPMANIA_FILE_SM = 0,
    WORM_UI_STEPMANIA_FILE_SSC,
} WormUiStepmaniaFileProfile;

typedef enum WormUiStepmaniaTimingSyntax {
    WORM_UI_STEPMANIA_TIMING_NATIVE_WARPS = 0,
    WORM_UI_STEPMANIA_TIMING_LEGACY_NEGATIVE_BPM,
    WORM_UI_STEPMANIA_TIMING_LEGACY_NEGATIVE_STOP,
} WormUiStepmaniaTimingSyntax;

typedef enum WormUiStepmaniaImplementation {
    WORM_UI_STEPMANIA_IMPLEMENTATION_SYNTAX_ONLY = 0,
    WORM_UI_STEPMANIA_IMPLEMENTATION_STEP_MANIA_5_1,
    WORM_UI_STEPMANIA_IMPLEMENTATION_ETTERNA_0_75_1,
} WormUiStepmaniaImplementation;


typedef enum WormUiExistingFilesChoice {
    WORM_UI_EXISTING_FILES_ASK = 0,
    WORM_UI_EXISTING_FILES_REPLACE,
    WORM_UI_EXISTING_FILES_KEEP,
    WORM_UI_EXISTING_FILES_SAVED = 255,
} WormUiExistingFilesChoice;

typedef enum WormUiOutputDestinationKind {
    WORM_UI_OUTPUT_DESTINATION_DIRECTORY = 0,
    WORM_UI_OUTPUT_DESTINATION_FILE,
} WormUiOutputDestinationKind;

typedef enum WormUiOutputPlanAction {
    WORM_UI_OUTPUT_PLAN_NEW = 0,
    WORM_UI_OUTPUT_PLAN_REPLACE,
    WORM_UI_OUTPUT_PLAN_REUSE,
    WORM_UI_OUTPUT_PLAN_COPY,
} WormUiOutputPlanAction;

namespace wormedit::ui {
struct output_plan_row_view_t {
    std::string_view path;
    WormUiOutputPlanAction action{};
    std::uint64_t bytes{};
    auto operator==(const output_plan_row_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

namespace wormedit::ui {
struct output_diagnostic_row_view_t {
    std::string_view message;
    wormedit::diagnostic_severity_t severity{};
    auto operator==(const output_diagnostic_row_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

namespace wormedit::ui {
struct project_output_suggestion_view_t {
    std::string_view name;
    wormedit::format_family_t family{};
    std::string_view path;
    std::optional<wormedit::stepmania_export_policy_t> stepmania;
    auto operator==(const project_output_suggestion_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

namespace wormedit::ui {
struct project_chart_row_view_t {
    wormedit::chart_id_t chart_id;
    std::string_view name, author, rating, mode;
    std::uint32_t key_count{};
    std::string_view output_groups;
    std::uint32_t compatible_output_families{};
    std::array<std::string_view, 5> output_incompatibility_reasons;
    std::uint64_t note_count{};
    std::string_view length, diagnostic;
    auto operator==(const project_chart_row_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

typedef enum WormUiProjectChartActionKind {
    WORM_UI_PROJECT_CHART_NONE = 0,
    WORM_UI_PROJECT_CHART_NEW,
    WORM_UI_PROJECT_CHART_DUPLICATE,
    WORM_UI_PROJECT_CHART_DELETE,
    WORM_UI_PROJECT_CHART_UP,
    WORM_UI_PROJECT_CHART_DOWN,
    WORM_UI_PROJECT_CHART_OPEN,
    WORM_UI_PROJECT_CHART_BATCH_METADATA,
    WORM_UI_PROJECT_CHART_REORDER,
} WormUiProjectChartActionKind;

namespace wormedit::ui {
struct project_chart_action_t {
    timing_link_selection_t timing_sources;
    WormUiProjectChartActionKind kind{};
    std::uint64_t document_revision{}, session_generation{};
    wormedit::chart_id_t chart_id;
    std::vector<wormedit::chart_id_t> selected_chart_ids;
    bool edit_author{};
    std::string author;
    bool edit_rating{}, unset_rating{};
    std::string rating_numerator, rating_denominator;
    wormedit::chart_id_t target_chart_id;
    bool insert_after{};
    auto operator==(const project_chart_action_t&) const -> bool = default;
};
} // namespace wormedit::ui

namespace wormedit::ui {
struct project_output_group_view_t {
    std::size_t document_index{};
    std::string_view name;
    wormedit::format_family_t family{};
    std::string_view path;
    wormedit::output_destination_kind_t destination_kind{};
    std::vector<wormedit::chart_id_t> chart_ids;
    std::string_view preview_status;
    wormedit::existing_files_policy_t existing_files{};
    std::optional<wormedit::stepmania_export_policy_t> stepmania;
    std::vector<output_plan_row_view_t> plan_rows;
    bool plan_rows_truncated{};
    std::vector<output_diagnostic_row_view_t> diagnostics;
    bool diagnostics_truncated{};
    void reset() {
        auto charts = std::move(chart_ids);
        auto plans = std::move(plan_rows);
        auto messages = std::move(diagnostics);
        *this = {};
        chart_ids = std::move(charts);
        plan_rows = std::move(plans);
        diagnostics = std::move(messages);
        chart_ids.clear();
        plan_rows.clear();
        diagnostics.clear();
    }
    auto operator==(const project_output_group_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

typedef enum WormUiProjectOutputGroupActionKind {
    WORM_UI_PROJECT_OUTPUT_GROUP_NONE = 0,
    WORM_UI_PROJECT_OUTPUT_GROUP_CREATE,
    WORM_UI_PROJECT_OUTPUT_GROUP_UPDATE,
    WORM_UI_PROJECT_OUTPUT_GROUP_DELETE,
    WORM_UI_PROJECT_OUTPUT_GROUP_PREVIEW,
} WormUiProjectOutputGroupActionKind;

namespace wormedit::ui {
struct project_output_group_action_t {
    WormUiProjectOutputGroupActionKind kind{};
    wormedit::song_id_t project_id;
    std::uint64_t document_revision{}, session_generation{};
    std::size_t group_index{};
    std::string name, path;
    wormedit::format_family_t family{};
    wormedit::output_destination_kind_t destination_kind{};
    wormedit::existing_files_policy_t existing_files{};
    std::optional<wormedit::stepmania_export_policy_t> stepmania;
    std::vector<wormedit::chart_id_t> chart_ids;
    auto operator==(const project_output_group_action_t&) const -> bool = default;
};
} // namespace wormedit::ui

namespace wormedit::ui {
struct project_settings_view_t {
    bool snap_overridden{}, audio_overridden{}, visual_overridden{};
    std::uint32_t default_snap{}, program_default_snap{};
    std::string_view audio_directory, visual_directory;
    void refill(const wormedit::song_document_t&, const wormedit::program_preferences_t&);
};
struct project_settings_action_t {
    bool requested{};
    wormedit::song_id_t project;
    std::uint64_t revision{}, session_generation{};
    wormedit::project_setting_field_t field{};
    bool use_program_default{};
    std::uint32_t integer{};
    std::string text;
};
struct setup_view_t {
    std::vector<timing_link_category_view_t> timing_categories;
    kind_catalog_view_t<wormedit::format_family_t> output_families;
    bool visible{};
    wormedit::song_id_t song_id;
    std::uint64_t document_revision{}, session_generation{};
    std::vector<project_chart_row_view_t> charts;
    std::vector<project_output_group_view_t> output_groups;
    std::vector<project_output_suggestion_view_t> output_suggestions;
    wormedit::chart_id_t selected_chart_id;
    std::size_t chart_count{}, audio_asset_count{}, visual_asset_count{};
    project_settings_view_t settings;

    void reset() {
        auto retained_timing_categories = std::move(timing_categories);
        auto families = std::move(output_families);
        auto retained_charts = std::move(charts);
        auto groups = std::move(output_groups);
        auto suggestions = std::move(output_suggestions);
        for (auto& group : groups) group.reset();
        *this = {};
        timing_categories = std::move(retained_timing_categories);
        output_families = std::move(families);
        charts = std::move(retained_charts);
        output_groups = std::move(groups);
        output_suggestions = std::move(suggestions);
        charts.clear();
        output_suggestions.clear();
    }
    void refill(wormedit::sys::project_chart_runtime_t&, const wormedit::song_document_t&);

};
} // namespace wormedit::ui
