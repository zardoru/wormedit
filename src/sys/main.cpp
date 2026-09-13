#include "wormedit/audio.h"
#include "wormedit/window.h"

#include "ui/frontend.h"
#include "wormedit/config.h"

#include <wormedit/core/command.h>
#include <wormedit/core/lane_profiles.h>
#include <wormedit/core/recovery_controller.h>
#include <wormedit/core/startup_session.h>
#include <wormedit/core/workspace.h>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {

constexpr std::array<const char*, WORM_UI_WORKSPACE_TAB_COUNT> tab_labels{
    "Setup", "Metadata", "Timing", "Compose", "Storyboard", "Audio / Slices", "Compare / Validate",
};

auto initial_document() -> wormedit::song_document_t {
    auto profile = wormedit::make_bms_7k_profile();
    wormedit::chart_document_t chart{
        .id = wormedit::chart_id_t{2},
        .metadata = {.name = "New Difficulty", .author = ""},
        .source_family = wormedit::format_family_t::native_project,
        .measures = {{.id = wormedit::measure_id_t{3}}},
        .lane_layouts = {{.id = wormedit::object_id_t{4},
                          .position = {wormedit::measure_id_t{3}, 0},
                          .layout = std::move(profile.layout)}},
        .input_profile = std::move(profile.input),
        .bpms = {{.id = wormedit::object_id_t{5},
                  .position = {wormedit::measure_id_t{3}, 0},
                  .beats_per_minute = {120, 1}}},
    };
    return {
        .id = wormedit::song_id_t{1},
        .revision = 1,
        .metadata = {.title = "Untitled Project", .artist = "Unknown Artist"},
        .charts = {std::move(chart)},
    };
}

struct flattened_workspace_t {
    WormUiWorkspaceView view{};
    std::array<std::string, WORM_UI_WORKSPACE_SUMMARY_CAPACITY> summary_storage;
    std::array<std::string, WORM_UI_RECOVERY_ROW_CAPACITY> recovery_summary_storage;
    std::array<std::string, WORM_UI_RECOVERY_ROW_CAPACITY> recovery_disabled_storage;
    std::array<std::string, WORM_UI_RECOVERY_DIAGNOSTIC_CAPACITY> recovery_diagnostic_storage;
};

auto recovery_status_name(wormedit::native_project_recovery_candidate_status_t status)
    -> std::string_view {
    using status_t = wormedit::native_project_recovery_candidate_status_t;
    switch (status) {
    case status_t::missing:
        return "missing";
    case status_t::valid:
        return "valid";
    case status_t::invalid:
        return "invalid";
    case status_t::unreadable:
        return "unreadable";
    case status_t::resource_limited:
        return "resource limited";
    }
    return "unknown";
}

auto recovery_comparison_name(wormedit::native_project_recovery_comparison_t comparison)
    -> std::string_view {
    using comparison_t = wormedit::native_project_recovery_comparison_t;
    switch (comparison) {
    case comparison_t::not_comparable:
        return "not compared";
    case comparison_t::no_valid_primary:
        return "no valid primary";
    case comparison_t::different_project:
        return "different project";
    case comparison_t::older_revision:
        return "older";
    case comparison_t::equal_revision:
        return "same revision";
    case comparison_t::newer_revision:
        return "newer";
    }
    return "unknown comparison";
}

auto format_family_name(wormedit::format_family_t family) -> std::string_view {
    using family_t = wormedit::format_family_t;
    switch (family) {
    case family_t::native_project:
        return "Native";
    case family_t::bms:
        return "BMS";
    case family_t::stepmania:
        return "StepMania";
    case family_t::osu_mania:
        return "osu!mania";
    case family_t::bmson:
        return "BMSON";
    case family_t::midi:
        return "MIDI";
    case family_t::quaver:
        return "Quaver";
    }
    return "Unknown";
}

auto metadata_value_summary(std::string_view value) -> std::string {
    constexpr std::size_t readable_value_limit = 96;
    if (value.size() <= readable_value_limit)
        return std::string(value);
    return "<" + std::to_string(value.size()) + " UTF-8 bytes>";
}

auto recovery_action_mask(const std::vector<wormedit::native_project_recovery_action_t>& actions)
    -> size_t {
    using action_t = wormedit::native_project_recovery_action_t;
    size_t mask{};
    for (const auto action : actions) {
        switch (action) {
        case action_t::inspect_read_only:
            mask |= WORM_UI_RECOVERY_ACTION_INSPECT;
            break;
        case action_t::recover_into_session:
            mask |= WORM_UI_RECOVERY_ACTION_RECOVER;
            break;
        case action_t::save_as_new_project:
            mask |= WORM_UI_RECOVERY_ACTION_SAVE_AS;
            break;
        case action_t::discard:
            mask |= WORM_UI_RECOVERY_ACTION_DISCARD;
            break;
        case action_t::quarantine:
            mask |= WORM_UI_RECOVERY_ACTION_QUARANTINE;
            break;
        }
    }
    return mask;
}

auto flatten_workspace(const wormedit::workspace_view_t& source,
                       const wormedit::song_document_t& document, const std::string& status,
                       const wormedit::native_project_recovery_controller_view_t& recovery,
                       bool recovery_scan_configured) -> flattened_workspace_t {
    flattened_workspace_t result;
    result.view.project_title = document.metadata.title.c_str();
    result.view.artist = document.metadata.artist.c_str();
    result.view.selected_difficulty = source.selected_difficulty.has_value()
                                          ? source.selected_difficulty->difficulty_name.c_str()
                                          : "No difficulty";
    result.view.status_text = status.c_str();
    result.view.git_hash = WORMEDIT_GIT_HASH;
    result.view.active_tab = static_cast<WormUiWorkspaceTab>(source.active_tab);
    result.view.history_available = source.history.available;
    result.view.dirty = source.history.dirty;
    result.view.undo_depth = source.history.undo_depth;
    result.view.redo_depth = source.history.redo_depth;
    result.view.recovery_panel_visible = true;
    result.view.recovery_heading = "Project recovery";
    result.view.recovery_rows_truncated = recovery.rows_truncated;
    if (recovery_scan_configured) {
        result.view.recovery_notice = recovery.banner_visible
                                          ? "Review candidates before choosing an action."
                                          : "No recovery candidates currently require action.";
    } else {
        result.view.recovery_notice =
            "Unavailable: this starter workspace has no opened project path or configured "
            "recovery directory.";
    }
    const auto recovery_row_count =
        std::min(recovery.rows.size(), static_cast<std::size_t>(WORM_UI_RECOVERY_ROW_CAPACITY));
    result.view.recovery_candidate_count = recovery_row_count;
    result.view.recovery_rows_truncated =
        result.view.recovery_rows_truncated || recovery_row_count < recovery.rows.size();
    for (std::size_t index = 0; index < recovery_row_count; ++index) {
        const auto& row = recovery.rows[index];
        auto& summary = result.recovery_summary_storage[index];
        summary = std::string(recovery_status_name(row.status)) + " · " +
                  std::string(recovery_comparison_name(row.comparison));
        if (row.version) {
            summary += " · revision " + std::to_string(row.version->revision);
        }
        if (row.recovery_session_sequence) {
            summary += " · session " + std::to_string(*row.recovery_session_sequence);
        }
        if (recovery.preferred_recovery_row == index) {
            summary += " · recommended recovery";
        }
        auto& disabled_reason = result.recovery_disabled_storage[index];
        const auto action_mask = recovery_action_mask(row.available_actions);
        constexpr auto all_recovery_actions =
            WORM_UI_RECOVERY_ACTION_INSPECT | WORM_UI_RECOVERY_ACTION_RECOVER |
            WORM_UI_RECOVERY_ACTION_SAVE_AS | WORM_UI_RECOVERY_ACTION_DISCARD |
            WORM_UI_RECOVERY_ACTION_QUARANTINE;
        if (row.available_actions.empty()) {
            disabled_reason = "No safe action is available for this candidate.";
        } else if (action_mask != all_recovery_actions) {
            disabled_reason =
                "Unavailable actions are disabled by the recovery controller's safety checks.";
        }
        result.view.recovery_candidates[index] = {
            .path_label = row.path_label.c_str(),
            .summary = summary.c_str(),
            .disabled_reason = disabled_reason.empty() ? nullptr : disabled_reason.c_str(),
            .action_mask = action_mask,
            .scan_generation = row.target.scan_generation,
            .row_index = row.target.row_index,
            .candidate_index = row.target.candidate_index,
        };
    }
    const auto recovery_diagnostic_count =
        std::min(recovery.diagnostics.size(),
                 static_cast<std::size_t>(WORM_UI_RECOVERY_DIAGNOSTIC_CAPACITY));
    result.view.recovery_diagnostic_count = recovery_diagnostic_count;
    for (std::size_t index = 0; index < recovery_diagnostic_count; ++index) {
        auto& diagnostic = result.recovery_diagnostic_storage[index];
        diagnostic = recovery.diagnostics[index].message;
        if (!recovery.diagnostics[index].path.empty()) {
            diagnostic += " (" + recovery.diagnostics[index].path.generic_string() + ")";
        }
        result.view.recovery_diagnostics[index] = diagnostic.c_str();
    }
    for (std::size_t tab = 0; tab < tab_labels.size(); ++tab) {
        result.view.tabs[tab] = {
            .label = tab_labels[tab],
            .enabled = wormedit::workspace_tab_enabled(static_cast<wormedit::workspace_tab_t>(tab)),
        };
    }
    result.view.tab_heading = tab_labels[static_cast<std::size_t>(source.active_tab)];

    const auto append = [&result](std::string line) {
        if (result.view.summary_line_count >= result.summary_storage.size()) {
            return;
        }
        const auto index = result.view.summary_line_count++;
        result.summary_storage[index] = std::move(line);
    };
    std::visit(
        [&](const auto& tab) {
            using tab_t = std::remove_cvref_t<decltype(tab)>;
            if constexpr (std::is_same_v<tab_t, wormedit::workspace_setup_view_t>) {
                result.view.project_title = tab.title.c_str();
                result.view.artist = tab.artist.c_str();
                append("Charts: " + std::to_string(tab.charts.size()));
                append("Audio assets: " + std::to_string(tab.audio_asset_count));
                append("Visual assets: " + std::to_string(tab.visual_asset_count));
                append("Use the tabs above to edit one shared song workspace.");
            } else if constexpr (std::is_same_v<tab_t, wormedit::workspace_metadata_view_t>) {
                result.view.project_title = tab.title.c_str();
                result.view.artist = tab.artist.c_str();
                append("Song title: " + tab.title);
                append("Artist: " + tab.artist);
                append("Search tags: " + std::to_string(tab.search_tags.size()));
                append("Chart author: " +
                       (tab.chart_author.empty() ? std::string("(unset)") : tab.chart_author));
                append("Format-specific fields: " + std::to_string(tab.format_fields.size()));
                for (const auto& field : tab.format_fields) {
                    append(std::string(field.chart_scoped ? "Difficulty · " : "Song · ") +
                           std::string(format_family_name(field.family)) + " · " + field.name +
                           ": " + metadata_value_summary(field.value));
                }
            } else if constexpr (std::is_same_v<tab_t, wormedit::workspace_timing_view_t>) {
                append("Measures: " + std::to_string(tab.measure_count));
                append("BPM events: " + std::to_string(tab.bpm_count));
                append("Stops / delays: " + std::to_string(tab.pause_count));
                append("Warps: " + std::to_string(tab.warp_count));
                append("Anchors: " + std::to_string(tab.anchor_count));
                append("Bookmarks / sections: " +
                       std::to_string(tab.bookmark_count + tab.section_count));
            } else if constexpr (std::is_same_v<tab_t, wormedit::workspace_compose_view_t>) {
                append("Measures: " + std::to_string(tab.measure_count));
                append("Notes: " + std::to_string(tab.note_count));
                append("Layout segments: " + std::to_string(tab.layout_segment_count));
                append("Visible lane definitions: " + std::to_string(tab.lane_count));
                append("Semantic layers: " + std::to_string(tab.layer_count));
            } else if constexpr (std::is_same_v<tab_t, wormedit::workspace_storyboard_view_t>) {
                append(tab.placeholder);
                append("Storyboard documents: " + std::to_string(tab.storyboard_count));
            } else if constexpr (std::is_same_v<tab_t, wormedit::workspace_audio_slices_view_t>) {
                append("Audio assets: " + std::to_string(tab.audio_assets.size()));
                append("Clips: " + std::to_string(tab.clip_count));
                append("Selected chart sound channels: " + std::to_string(tab.sound_channel_count));
                append(std::string("Transport: ") +
                       (tab.transport.playing ? "playing" : "stopped"));
            } else if constexpr (std::is_same_v<tab_t,
                                                wormedit::workspace_compare_validate_view_t>) {
                append("Compared difficulties: " + std::to_string(tab.comparison_charts.size()));
                append("Validation errors: " + std::to_string(tab.error_count));
                append("Validation warnings: " + std::to_string(tab.warning_count));
                for (const auto& diagnostic : tab.diagnostics) {
                    append(diagnostic.message);
                }
            }
        },
        source.tab_view);
    return result;
}

auto first_recovery_diagnostic(
    const wormedit::native_project_recovery_controller_dispatch_result_t& result) -> std::string {
    if (!result.diagnostics.empty()) {
        const auto& diagnostic = result.diagnostics.front();
        return diagnostic.path.empty()
                   ? diagnostic.message
                   : diagnostic.message + " (" + diagnostic.path.generic_string() + ")";
    }
    if (result.action_result && !result.action_result->diagnostics.empty()) {
        const auto& diagnostic = result.action_result->diagnostics.front();
        return diagnostic.path.empty()
                   ? diagnostic.message
                   : diagnostic.message + " (" + diagnostic.path.generic_string() + ")";
    }
    return {};
}

auto dispatch_recovery_ui_action(
    const WormUiWorkspaceAction& action, wormedit::native_project_recovery_controller_t& controller,
    wormedit::document_history_t& history, wormedit::workspace_controller_t& workspace,
    std::optional<wormedit::native_project_document_version_t> scanned_primary_version)
    -> std::string {
    using namespace wormedit;
    const native_project_recovery_action_target_t target{
        .scan_generation = action.recovery_scan_generation,
        .row_index = action.recovery_row_index,
        .candidate_index = action.recovery_candidate_index,
    };
    native_project_recovery_controller_action_t request =
        inspect_native_project_recovery_action_t{.target = target};
    switch (action.kind) {
    case WORM_UI_WORKSPACE_ACTION_RECOVERY_INSPECT:
        request = inspect_native_project_recovery_action_t{.target = target};
        break;
    case WORM_UI_WORKSPACE_ACTION_RECOVERY_RECOVER:
        request = recover_native_project_recovery_action_t{
            .target = target,
            .expected_session =
                {
                    .is_set = true,
                    .version = scanned_primary_version,
                },
        };
        break;
    case WORM_UI_WORKSPACE_ACTION_RECOVERY_SAVE_AS: {
        const auto end = std::find(std::begin(action.recovery_save_as_destination),
                                   std::end(action.recovery_save_as_destination), '\0');
        const std::string destination(action.recovery_save_as_destination, end);
        if (destination.empty()) {
            return "Choose a new project path before using Save as.";
        }
        request = save_as_native_project_recovery_action_t{
            .target = target, .destination = std::filesystem::path{destination}};
        break;
    }
    case WORM_UI_WORKSPACE_ACTION_RECOVERY_DISCARD:
        request = discard_native_project_recovery_action_t{.target = target};
        break;
    case WORM_UI_WORKSPACE_ACTION_RECOVERY_QUARANTINE:
        request = quarantine_native_project_recovery_action_t{.target = target};
        break;
    default:
        return "Invalid recovery action.";
    }

    const auto dispatched = controller.dispatch(request);
    const auto diagnostic = first_recovery_diagnostic(dispatched);
    if (!dispatched.accepted) {
        return diagnostic.empty() ? "Recovery action was rejected." : diagnostic;
    }
    if (!dispatched.action_result) {
        return diagnostic.empty() ? "Recovery action did not produce a result." : diagnostic;
    }
    const auto& result = *dispatched.action_result;
    if (result.has_errors() || !result.applied) {
        return diagnostic.empty() ? "Recovery action did not complete." : diagnostic;
    }

    switch (action.kind) {
    case WORM_UI_WORKSPACE_ACTION_RECOVERY_INSPECT:
        if (result.document) {
            return "Inspected recovery candidate read-only: " + result.document->metadata.title +
                   " (revision " + std::to_string(result.document->revision) + ").";
        }
        return "Recovery candidate inspected read-only.";
    case WORM_UI_WORKSPACE_ACTION_RECOVERY_RECOVER:
        if (!result.document) {
            return "Recovery action completed without a replacement document.";
        }
        {
            auto replacement_history = make_document_history(*result.document, 128, false);
            auto replacement_binding = make_workspace_controller(replacement_history);
            if (!replacement_binding) {
                return "Recovered data validated, but the workspace could not be rebound; "
                       "the current workspace was kept.";
            }
            history = std::move(replacement_history);
            workspace = std::move(*replacement_binding.workspace);
        }
        // A recovered document is no longer the primary baseline captured by
        // this scan. Invalidate every remaining row token before returning to
        // the event loop so another click cannot replace the recovered session.
        (void)controller.replace_scan({});
        return "Recovery loaded into the current session. Review it and save explicitly.";
    case WORM_UI_WORKSPACE_ACTION_RECOVERY_SAVE_AS:
        return "Recovery candidate saved as a new project; the current workspace was unchanged.";
    case WORM_UI_WORKSPACE_ACTION_RECOVERY_DISCARD:
        return "Recovery artifact moved to its recoverable discarded location.";
    case WORM_UI_WORKSPACE_ACTION_RECOVERY_QUARANTINE:
        return result.quarantined_path
                   ? "Recovery artifact quarantined at " + result.quarantined_path->generic_string()
                   : "Recovery artifact quarantined.";
    default:
        return "Invalid recovery action.";
    }
}

} // namespace

int main(int argc, char** argv) {
    constexpr int maximum_startup_paths = 30;
    if (argc > maximum_startup_paths + 1) {
        std::fprintf(stderr, "usage: wormedit [project.wormedit [recovery-artifact ...]]\n");
        return 2;
    }
    wormedit::native_project_startup_request_t startup_request;
    if (argc > 1)
        startup_request.primary_path = std::filesystem::path{argv[1]};
    for (int index = 2; index < argc; ++index)
        startup_request.recovery_entries.emplace_back(argv[index]);
    const auto requested_primary_path = startup_request.primary_path;
    auto startup =
        wormedit::prepare_native_project_startup(initial_document(), std::move(startup_request));

    if (!SDL_Init(0)) {
        std::fprintf(stderr, "SDL init failed\n");
        return 1;
    }

    WormAudioSystem audio{};
    if (!worm_audio_init(&audio)) {
        std::fprintf(stderr, "Audio subsystem init failed\n");
        SDL_Quit();
        return 1;
    }

    WormWindow window{};
    const WormWindowDesc window_desc{"wormedit", 1280, 720};
    if (!worm_window_create(&window, &window_desc)) {
        worm_audio_shutdown(&audio);
        SDL_Quit();
        return 1;
    }

    if (!worm_ui_frontend_init(window.handle)) {
        worm_window_destroy(&window);
        worm_audio_shutdown(&audio);
        SDL_Quit();
        return 1;
    }

    auto history =
        wormedit::make_document_history(std::move(startup.document), 128, startup.loaded_primary);
    auto binding = wormedit::make_workspace_controller(history);
    if (!binding) {
        std::fprintf(stderr, "Initial workspace binding failed\n");
        worm_ui_frontend_shutdown();
        worm_window_destroy(&window);
        worm_audio_shutdown(&audio);
        SDL_Quit();
        return 1;
    }
    auto workspace = std::move(*binding.workspace);
    std::string status;
    if (startup.loaded_primary) {
        status = "Opened native project: " + requested_primary_path.generic_string();
    } else if (startup.recovery_scan_configured) {
        status = "Primary project was not loaded; review the completed recovery scan.";
    } else {
        status = "New unsaved workspace ready";
    }
    wormedit::native_project_recovery_controller_t recovery_controller;
    if (startup.recovery_scan_configured)
        (void)recovery_controller.replace_scan(std::move(startup.recovery_scan));
    const auto recovery_scan_configured = startup.recovery_scan_configured;
    const auto scanned_primary_version = startup.primary_version;

    bool running = true;
    while (running) {
        SDL_Event event;
        worm_ui_frontend_begin_input();
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                running = false;
            }
            worm_ui_frontend_handle_event(&event);
        }
        worm_ui_frontend_end_input();

        const auto model = wormedit::make_workspace_view(workspace, history.document);
        if (!model) {
            status = model.diagnostics.empty() ? "Workspace view unavailable"
                                               : model.diagnostics.front().message;
        } else {
            auto flattened =
                flatten_workspace(*model.view, history.document, status, recovery_controller.view(),
                                  recovery_scan_configured);
            for (std::size_t line = 0; line < flattened.view.summary_line_count; ++line) {
                flattened.view.summary_lines[line] = flattened.summary_storage[line].c_str();
            }
            const auto action = worm_ui_frontend_compose(&flattened.view);
            if (action.kind == WORM_UI_WORKSPACE_ACTION_QUIT) {
                running = false;
            } else if (action.kind == WORM_UI_WORKSPACE_ACTION_SWITCH_TAB) {
                const auto applied = wormedit::apply_workspace_action(
                    workspace, history.document, history.document.revision,
                    wormedit::workspace_switch_tab_action_t{
                        static_cast<wormedit::workspace_tab_t>(action.tab)});
                status = applied
                             ? "Workspace tab changed"
                             : (applied.diagnostics.empty() ? "Workspace action rejected"
                                                            : applied.diagnostics.front().message);
            } else if (action.kind >= WORM_UI_WORKSPACE_ACTION_RECOVERY_INSPECT &&
                       action.kind <= WORM_UI_WORKSPACE_ACTION_RECOVERY_QUARANTINE) {
                status = dispatch_recovery_ui_action(action, recovery_controller, history,
                                                     workspace, scanned_primary_version);
            }
        }

        int width = 0;
        int height = 0;
        worm_window_get_size(&window, &width, &height);
        worm_ui_frontend_render(width, height);
        worm_window_swap(&window);
        SDL_Delay(1);
    }

    worm_ui_frontend_shutdown();
    worm_window_destroy(&window);
    worm_audio_shutdown(&audio);
    SDL_Quit();
    return 0;
}
