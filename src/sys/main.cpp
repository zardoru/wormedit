#include "wormedit/audio.h"
#include "wormedit/window.h"

#include "sys/platform_recovery.h"
#include "ui/frontend.h"
#include "wormedit/config.h"

#include <wormedit/core/autosave_loop.h>
#include <wormedit/core/command.h>
#include <wormedit/core/editor_scene.h>
#include <wormedit/core/interaction.h>
#include <wormedit/core/lane_profiles.h>
#include <wormedit/core/metadata_editor.h>
#include <wormedit/core/recovery_controller.h>
#include <wormedit/core/startup_session.h>
#include <wormedit/core/timing.h>
#include <wormedit/core/timing_lane_controller.h>
#include <wormedit/core/viewport_input.h>
#include <wormedit/core/workspace.h>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <limits>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace {

constexpr std::array<const char*, WORM_UI_WORKSPACE_TAB_COUNT> tab_labels{
    "Setup", "Metadata", "Timing", "Compose", "Storyboard", "Audio / Slices", "Compare / Validate",
};

auto new_session_project_id() -> wormedit::song_id_t {
    try {
        std::random_device source;
        const auto value =
            (static_cast<std::uint64_t>(source()) << 32U) | static_cast<std::uint64_t>(source());
        if (value != 0)
            return wormedit::song_id_t{value};
    } catch (...) {
    }
    const auto wall =
        static_cast<std::uint64_t>(std::chrono::system_clock::now().time_since_epoch().count());
    const auto monotonic =
        static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto value = wall ^ (monotonic + 0x9e3779b97f4a7c15ULL + (wall << 6U) + (wall >> 2U));
    return wormedit::song_id_t{value == 0 ? 1 : value};
}

auto initial_document(wormedit::song_id_t project_id) -> wormedit::song_document_t {
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
        .id = project_id,
        .revision = 1,
        .metadata = {.title = "Untitled Project", .artist = "Unknown Artist"},
        .charts = {std::move(chart)},
    };
}

struct flattened_workspace_t {
    WormUiWorkspaceView view{};
    std::array<std::string, WORM_UI_WORKSPACE_SUMMARY_CAPACITY> summary_storage;
    std::array<std::string, WORM_UI_RECOVERY_ROW_CAPACITY> recovery_path_storage;
    std::array<std::string, WORM_UI_RECOVERY_ROW_CAPACITY> recovery_summary_storage;
    std::array<std::string, WORM_UI_RECOVERY_ROW_CAPACITY> recovery_disabled_storage;
    std::array<std::string, WORM_UI_RECOVERY_DIAGNOSTIC_CAPACITY> recovery_diagnostic_storage;
    std::vector<const char*> metadata_search_tag_storage;
    std::vector<std::size_t> metadata_search_tag_length_storage;
    std::array<WormUiMetadataFormatFieldView, WORM_UI_METADATA_FORMAT_FIELD_CAPACITY>
        metadata_format_field_storage{};
    std::array<WormUiMetadataFormatTargetView, WORM_UI_METADATA_FORMAT_TARGET_CAPACITY>
        metadata_format_target_storage{};
    std::string timing_status_storage;
    std::vector<WormUiTimingSpeedView> timing_speed_storage;
    std::vector<WormUiTimingMarkerView> timing_marker_storage;
    std::string compose_status_storage;
    std::vector<WormUiComposeBandView> compose_band_storage;
    std::vector<WormUiComposeLaneView> compose_lane_storage;
    std::vector<WormUiComposeGridLineView> compose_grid_storage;
    std::vector<WormUiComposeDifferenceView> compose_difference_storage;
    std::vector<WormUiComposeHoldView> compose_hold_storage;
    std::vector<WormUiComposeNoteView> compose_note_storage;
    std::optional<WormUiComposeCursorView> compose_cursor_storage;
    std::array<WormUiComposeBranchRowView, WORM_UI_COMPOSE_BRANCH_ROW_CAPACITY>
        compose_branch_row_storage{};
    std::array<std::string, WORM_UI_COMPOSE_BRANCH_ROW_CAPACITY> compose_branch_text_storage;
    std::array<std::string, WORM_UI_COMPOSE_BRANCH_DIAGNOSTIC_CAPACITY>
        compose_branch_diagnostic_storage;
    std::array<WormUiComposeLaneKeyBinding, WORM_UI_COMPOSE_LANE_KEY_BINDING_CAPACITY>
        compose_lane_key_storage{};
    std::optional<wormedit::editor_interaction_view_model_t> compose_interaction_view_storage;
};

struct compose_scene_cache_t {
    wormedit::chart_id_t chart;
    std::uint64_t revision{};
    bool attempted{};
    std::optional<wormedit::editor_field_scene_index_t> index;
    std::string status;
};

struct timing_speed_cache_entry_t {
    std::uint64_t object_id{};
    long double marker_row{};
    long double affected_begin_row{};
    long double affected_end_row{};
    double multiplier{};
    double transition_duration{};
    bool transition_is_seconds{};
    bool transition_clipped{};
};

struct timing_scene_cache_t {
    wormedit::chart_id_t chart;
    std::uint64_t revision{};
    bool attempted{};
    bool truncated{};
    std::uint64_t total_rows{};
    std::vector<timing_speed_cache_entry_t> speeds;
    std::string status;
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
        result.recovery_path_storage[index] = row.path_label;
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
            .path_label = result.recovery_path_storage[index].c_str(),
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
                auto& metadata = result.view.metadata;
                metadata.visible = true;
                metadata.document_revision = source.document_revision;
                metadata.title = tab.title.c_str();
                metadata.artist = tab.artist.c_str();
                metadata.title_bytes = tab.title.size();
                metadata.artist_bytes = tab.artist.size();
                metadata.search_tag_count = tab.search_tags.size();
                metadata.song_fields_editable =
                    tab.title.size() <= WORM_UI_METADATA_TITLE_BYTES &&
                    tab.artist.size() <= WORM_UI_METADATA_TITLE_BYTES &&
                    tab.search_tags.size() <= WORM_UI_METADATA_SEARCH_TAG_CAPACITY;
                metadata.search_tags_editable =
                    tab.search_tags.size() <= WORM_UI_METADATA_SEARCH_TAG_CAPACITY;
                if (metadata.search_tags_editable) {
                    result.metadata_search_tag_storage.reserve(tab.search_tags.size());
                    result.metadata_search_tag_length_storage.reserve(tab.search_tags.size());
                    for (const auto& tag : tab.search_tags) {
                        result.metadata_search_tag_storage.push_back(tag.c_str());
                        result.metadata_search_tag_length_storage.push_back(tag.size());
                        metadata.search_tags_editable =
                            metadata.search_tags_editable &&
                            tag.size() <= WORM_UI_METADATA_SEARCH_TAG_BYTES;
                    }
                }
                metadata.search_tags = metadata.search_tags_editable
                                           ? result.metadata_search_tag_storage.data()
                                           : nullptr;
                metadata.search_tag_bytes = metadata.search_tags_editable
                                                ? result.metadata_search_tag_length_storage.data()
                                                : nullptr;
                metadata.song_fields_editable =
                    metadata.song_fields_editable && metadata.search_tags_editable;
                metadata.has_chart = tab.selected_difficulty.has_value();
                if (tab.selected_difficulty) {
                    metadata.chart_id = tab.selected_difficulty->id.value;
                    metadata.chart_name = tab.selected_difficulty->difficulty_name.c_str();
                    metadata.chart_author = tab.chart_author.c_str();
                    metadata.chart_name_bytes = tab.selected_difficulty->difficulty_name.size();
                    metadata.chart_author_bytes = tab.chart_author.size();
                    metadata.chart_fields_editable =
                        tab.selected_difficulty->difficulty_name.size() <=
                            WORM_UI_METADATA_CHART_TEXT_BYTES &&
                        tab.chart_author.size() <= WORM_UI_METADATA_CHART_TEXT_BYTES;
                    metadata.has_chart_rating = tab.selected_difficulty->rating.has_value();
                    if (tab.selected_difficulty->rating) {
                        metadata.chart_rating_numerator =
                            tab.selected_difficulty->rating->numerator;
                        metadata.chart_rating_denominator =
                            tab.selected_difficulty->rating->denominator;
                    }
                }
                metadata.format_fields_truncated =
                    tab.format_fields.size() > WORM_UI_METADATA_FORMAT_FIELD_CAPACITY;
                metadata.format_field_total_count = tab.format_fields.size();
                metadata.format_field_count =
                    std::min(tab.format_fields.size(),
                             static_cast<std::size_t>(WORM_UI_METADATA_FORMAT_FIELD_CAPACITY));
                for (std::size_t index = 0; index < metadata.format_field_count; ++index) {
                    const auto& field = tab.format_fields[index];
                    result.metadata_format_field_storage[index] = {
                        .chart_scoped = field.chart_scoped,
                        .family = static_cast<std::uint8_t>(field.family),
                        .first_group_for_family = field.first_group_for_family,
                        .group_index = field.group_index,
                        .field_index = field.field_index,
                        .group_field_count = field.group_field_count,
                        .name = field.name.c_str(),
                        .value = field.value.c_str(),
                        .name_bytes = field.name.size(),
                        .value_bytes = field.value.size(),
                        .has_source_order = field.source_order.has_value(),
                        .source_order = field.source_order.value_or(0),
                    };
                }
                metadata.format_fields = result.metadata_format_field_storage.data();
                constexpr std::array format_families{
                    wormedit::format_family_t::native_project, wormedit::format_family_t::bms,
                    wormedit::format_family_t::stepmania,      wormedit::format_family_t::osu_mania,
                    wormedit::format_family_t::bmson,          wormedit::format_family_t::midi,
                    wormedit::format_family_t::quaver};
                const auto append_format_targets = [&](const auto& groups, bool chart_scoped) {
                    std::array<const wormedit::format_metadata_t*, format_families.size()>
                        first_groups{};
                    std::array<std::size_t, format_families.size()> first_group_indices{};
                    for (std::size_t group_index = 0; group_index < groups.size(); ++group_index) {
                        const auto family_index =
                            static_cast<std::size_t>(groups[group_index].family);
                        if (family_index < first_groups.size() &&
                            first_groups[family_index] == nullptr) {
                            first_groups[family_index] = &groups[group_index];
                            first_group_indices[family_index] = group_index;
                        }
                    }
                    for (const auto family : format_families) {
                        const auto family_index = static_cast<std::size_t>(family);
                        const auto* found = first_groups[family_index];
                        const auto index = metadata.format_target_count++;
                        result.metadata_format_target_storage[index] = {
                            .chart_scoped = chart_scoped,
                            .family = static_cast<std::uint8_t>(family),
                            .group_exists = found != nullptr,
                            .group_index = found == nullptr ? 0 : first_group_indices[family_index],
                            .field_count = found == nullptr ? 0 : found->ordered_fields.size(),
                        };
                    }
                };
                append_format_targets(document.metadata.format_groups, false);
                if (tab.selected_difficulty) {
                    const auto selected =
                        std::ranges::find(document.charts, tab.selected_difficulty->id,
                                          &wormedit::chart_document_t::id);
                    if (selected != document.charts.end())
                        append_format_targets(selected->metadata.format_groups, true);
                }
                metadata.format_targets = result.metadata_format_target_storage.data();
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
                result.view.timing.visible = true;
                result.view.timing.measure_count = tab.measure_count;
                result.view.timing.bpm_count = tab.bpm_count;
                result.view.timing.pause_count = tab.pause_count;
                result.view.timing.warp_count = tab.warp_count;
                result.view.timing.bookmark_count = tab.bookmark_count;
                result.view.timing.section_count = tab.section_count;
                result.view.timing.anchor_count = tab.anchor_count;
                append("Measures: " + std::to_string(tab.measure_count));
                append("BPM events: " + std::to_string(tab.bpm_count));
                append("Stops / delays: " + std::to_string(tab.pause_count));
                append("Warps: " + std::to_string(tab.warp_count));
                append("Anchors: " + std::to_string(tab.anchor_count));
                append("Bookmarks / sections: " +
                       std::to_string(tab.bookmark_count + tab.section_count));
            } else if constexpr (std::is_same_v<tab_t, wormedit::workspace_compose_view_t>) {
                result.view.compose.visible = true;
                const auto& outline = tab.bms_branch_outline;
                auto& compose = result.view.compose;
                compose.branch_outline_available = outline.available;
                compose.branch_rows_truncated = outline.rows_truncated;
                compose.branch_display_text_truncated = outline.display_text_truncated;
                compose.branch_diagnostics_truncated = outline.diagnostics_truncated;
                compose.branch_row_count =
                    std::min(outline.rows.size(),
                             static_cast<std::size_t>(WORM_UI_COMPOSE_BRANCH_ROW_CAPACITY));
                compose.branch_rows_truncated =
                    compose.branch_rows_truncated || compose.branch_row_count < outline.rows.size();
                for (std::size_t row_index = 0; row_index < compose.branch_row_count; ++row_index) {
                    const auto& row = outline.rows[row_index];
                    result.compose_branch_text_storage[row_index] = row.display_text;
                    result.compose_branch_row_storage[row_index] = {
                        .display_text = result.compose_branch_text_storage[row_index].c_str(),
                        .depth = row.depth,
                        .active = row.active,
                        .selected = row.selected,
                        .display_text_truncated = row.display_text_truncated,
                        .has_source_order = row.source_order.has_value(),
                        .source_order = row.source_order.value_or(0),
                    };
                }
                compose.branch_rows = result.compose_branch_row_storage.data();
                compose.branch_diagnostic_count =
                    std::min(outline.diagnostics.size(),
                             static_cast<std::size_t>(WORM_UI_COMPOSE_BRANCH_DIAGNOSTIC_CAPACITY));
                compose.branch_diagnostics_truncated =
                    compose.branch_diagnostics_truncated ||
                    compose.branch_diagnostic_count < outline.diagnostics.size();
                for (std::size_t diagnostic_index = 0;
                     diagnostic_index < compose.branch_diagnostic_count; ++diagnostic_index) {
                    auto& diagnostic = result.compose_branch_diagnostic_storage[diagnostic_index];
                    diagnostic = outline.diagnostics[diagnostic_index].message;
                    compose.branch_diagnostics[diagnostic_index] = diagnostic.c_str();
                }
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

auto compose_surface_height(int window_height, const WormUiWorkspaceView& view) -> double {
    float recovery_height = 0.0f;
    if (view.recovery_panel_visible) {
        recovery_height = 40.0f + (view.recovery_notice && view.recovery_notice[0] ? 22.0f : 0.0f) +
                          static_cast<float>(view.recovery_candidate_count) * 48.0f +
                          static_cast<float>(view.recovery_diagnostic_count) * 18.0f;
        for (std::size_t row = 0; row < view.recovery_candidate_count; ++row) {
            const auto& candidate = view.recovery_candidates[row];
            if ((candidate.action_mask & WORM_UI_RECOVERY_ACTION_SAVE_AS) != 0) {
                recovery_height += 25.0f;
                if (candidate.path_label == nullptr || candidate.path_label[0] == '\0')
                    recovery_height += 18.0f;
            }
            if (candidate.disabled_reason && candidate.disabled_reason[0])
                recovery_height += 20.0f;
        }
    }
    const auto content_height =
        window_height > 300 ? static_cast<float>(window_height) - 165.0f - recovery_height : 80.0f;
    const auto& compose = view.compose;
    const auto rows_shown = std::min<std::size_t>(compose.branch_row_count, 4);
    const auto diagnostics_shown = std::min<std::size_t>(compose.branch_diagnostic_count, 2);
    const bool branch_status_shown =
        compose.branch_rows_truncated || compose.branch_display_text_truncated ||
        compose.branch_diagnostics_truncated || compose.branch_row_count > rows_shown ||
        compose.branch_diagnostic_count > diagnostics_shown;
    const bool branch_visible = compose.branch_outline_available || compose.branch_row_count != 0 ||
                                compose.branch_diagnostic_count != 0 || branch_status_shown;
    const auto branch_height = branch_visible
                                   ? 23.0f + static_cast<float>(rows_shown + diagnostics_shown +
                                                                (branch_status_shown ? 1 : 0)) *
                                                 18.0f
                                   : 0.0f;
    return std::max(64.0, static_cast<double>(content_height - 38.0f - 18.0f - branch_height));
}

auto rational_as_long_double(wormedit::signed_rational_t value) -> long double {
    if (value.denominator == 0) {
        return 0.0L;
    }
    return static_cast<long double>(value.numerator) / static_cast<long double>(value.denominator);
}

auto rational_as_double(wormedit::rational_t value) -> double {
    if (value.denominator == 0) {
        return 0.0;
    }
    return static_cast<double>(value.numerator) / static_cast<double>(value.denominator);
}

auto timing_position_at_row(const wormedit::measure_index_t& measures, std::uint64_t row)
    -> std::optional<wormedit::local_position_t> {
    const auto found = std::ranges::find_if(measures.entries, [row](const auto& entry) {
        return row >= entry.absolute_begin_row &&
               row - entry.absolute_begin_row < entry.row_capacity;
    });
    if (found == measures.entries.end()) {
        return std::nullopt;
    }
    return wormedit::local_position_t{.measure = found->id, .row = row - found->absolute_begin_row};
}

auto timing_stage_grid_step(const wormedit::chart_document_t& chart) -> std::uint64_t {
    return chart.divisions_per_standard_measure % 4 == 0 ? chart.divisions_per_standard_measure / 4
                                                         : chart.divisions_per_standard_measure;
}

auto timing_lane_kind_from_ui(WormUiTimingLaneKind lane)
    -> std::optional<wormedit::timing_lane_kind_t> {
    using kind_t = wormedit::timing_lane_kind_t;
    switch (lane) {
    case WORM_UI_TIMING_LANE_BPM:
        return kind_t::bpm;
    case WORM_UI_TIMING_LANE_SCROLL:
        return kind_t::scroll;
    case WORM_UI_TIMING_LANE_SPEED:
        return kind_t::speed;
    case WORM_UI_TIMING_LANE_STOP:
        return kind_t::stop;
    case WORM_UI_TIMING_LANE_DELAY:
        return kind_t::delay;
    case WORM_UI_TIMING_LANE_WARP:
        return kind_t::warp;
    }
    return std::nullopt;
}

auto timing_lane_kind_to_ui(wormedit::timing_lane_kind_t lane) -> WormUiTimingLaneKind {
    using kind_t = wormedit::timing_lane_kind_t;
    switch (lane) {
    case kind_t::bpm:
        return WORM_UI_TIMING_LANE_BPM;
    case kind_t::scroll:
        return WORM_UI_TIMING_LANE_SCROLL;
    case kind_t::speed:
        return WORM_UI_TIMING_LANE_SPEED;
    case kind_t::stop:
        return WORM_UI_TIMING_LANE_STOP;
    case kind_t::delay:
        return WORM_UI_TIMING_LANE_DELAY;
    case kind_t::warp:
        return WORM_UI_TIMING_LANE_WARP;
    case kind_t::kiai:
    case kind_t::anchor:
        return WORM_UI_TIMING_LANE_BPM;
    }
    return WORM_UI_TIMING_LANE_BPM;
}

auto begin_timing_stage(const WormUiWorkspaceAction& action,
                        const wormedit::document_history_t& history,
                        const wormedit::workspace_controller_t& workspace)
    -> wormedit::timing_lane_pseudonote_preview_t {
    using namespace wormedit;
    timing_lane_pseudonote_preview_t rejected{};
    if (workspace.active_tab != workspace_tab_t::timing || !workspace.selected_chart ||
        action.timing_chart_id == 0 || workspace.selected_chart->value != action.timing_chart_id ||
        history.document.revision != action.timing_document_revision ||
        !std::isfinite(action.timing_target_y_fraction) || action.timing_target_y_fraction < 0.0 ||
        action.timing_target_y_fraction >= 1.0) {
        return rejected;
    }
    const auto requested_kind = timing_lane_kind_from_ui(action.timing_lane);
    if (!requested_kind) {
        return rejected;
    }
    const auto found = std::ranges::find(history.document.charts,
                                         chart_id_t{action.timing_chart_id}, &chart_document_t::id);
    if (found == history.document.charts.end()) {
        return rejected;
    }
    const auto measures_result = build_measure_index(*found);
    const auto* measures = std::get_if<measure_index_t>(&measures_result);
    if (!measures || measures->total_rows == 0) {
        return rejected;
    }
    const auto step = timing_stage_grid_step(*found);
    const auto raw_row_value =
        std::min(std::floor(static_cast<long double>(action.timing_target_y_fraction) *
                            measures->total_rows),
                 static_cast<long double>(measures->total_rows - 1));
    const auto raw_row = static_cast<std::uint64_t>(raw_row_value);
    const auto remainder = raw_row % step;
    auto snapped_row = raw_row - remainder;
    if (remainder >= step - remainder &&
        snapped_row <= std::numeric_limits<std::uint64_t>::max() - step) {
        snapped_row += step;
    }
    const auto last_valid_row = measures->total_rows - 1;
    if (snapped_row > last_valid_row) {
        snapped_row = last_valid_row - last_valid_row % step;
    }
    const auto position = timing_position_at_row(*measures, snapped_row);
    if (!position) {
        return rejected;
    }

    const auto kind = *requested_kind;
    timing_lane_value_t initial{timing_bpm_value_t{}};
    switch (kind) {
    case timing_lane_kind_t::bpm:
        initial = timing_bpm_value_t{{120, 1}};
        break;
    case timing_lane_kind_t::scroll:
        initial = timing_scroll_value_t{{1, 1}};
        break;
    case timing_lane_kind_t::speed:
        initial = timing_speed_value_t{{1, 1}, {0, 1}, false};
        break;
    case timing_lane_kind_t::stop:
    case timing_lane_kind_t::delay:
        initial = timing_pause_value_t{{1, 1}, pause_unit_t::seconds};
        break;
    case timing_lane_kind_t::warp: {
        if (step > std::numeric_limits<std::uint64_t>::max() - snapped_row ||
            snapped_row + step >= measures->total_rows) {
            return rejected;
        }
        const auto end = timing_position_at_row(*measures, snapped_row + step);
        if (!end) {
            return rejected;
        }
        initial = timing_warp_value_t{*end, warp_provenance_t::native};
        break;
    }
    case timing_lane_kind_t::kiai:
    case timing_lane_kind_t::anchor:
        return rejected;
    }
    return begin_timing_lane_pseudonote(
        history, chart_id_t{action.timing_chart_id}, kind, *position, std::move(initial),
        timing_lane_initial_value_source_t::explicit_default, false);
}

auto parse_timing_signed(const char* text) -> std::optional<std::int64_t> {
    if (!text || text[0] == '\0') {
        return std::nullopt;
    }
    const auto* end = text + std::strlen(text);
    std::int64_t value{};
    const auto parsed = std::from_chars(text, end, value);
    return parsed.ec == std::errc{} && parsed.ptr == end ? std::optional{value} : std::nullopt;
}

auto parse_timing_unsigned(const char* text) -> std::optional<std::uint64_t> {
    if (!text || text[0] == '\0') {
        return std::nullopt;
    }
    const auto* end = text + std::strlen(text);
    std::uint64_t value{};
    const auto parsed = std::from_chars(text, end, value);
    return parsed.ec == std::errc{} && parsed.ptr == end ? std::optional{value} : std::nullopt;
}

auto timing_ratio_in_bounds(std::int64_t numerator, std::uint64_t denominator, long double minimum,
                            long double maximum) -> bool {
    if (denominator == 0) {
        return false;
    }
    const auto value = static_cast<long double>(numerator) / denominator;
    return value >= minimum && value <= maximum;
}

auto timing_unsigned_ratio_in_bounds(std::uint64_t numerator, std::uint64_t denominator,
                                     long double maximum) -> bool {
    if (denominator == 0) {
        return false;
    }
    return static_cast<long double>(numerator) / denominator <= maximum;
}

auto timing_stage_value(const wormedit::timing_lane_pseudonote_preview_t& preview,
                        const wormedit::chart_document_t& chart,
                        const WormUiWorkspaceAction& action)
    -> std::optional<wormedit::timing_lane_value_t> {
    using namespace wormedit;
    const auto primary_numerator = parse_timing_signed(action.timing_primary_numerator_text);
    const auto primary_denominator = parse_timing_unsigned(action.timing_primary_denominator_text);
    const auto secondary_numerator = parse_timing_signed(action.timing_secondary_numerator_text);
    const auto secondary_denominator =
        parse_timing_unsigned(action.timing_secondary_denominator_text);
    switch (preview.kind) {
    case timing_lane_kind_t::bpm: {
        if (!primary_numerator || !primary_denominator ||
            !timing_ratio_in_bounds(*primary_numerator, *primary_denominator, 1.0L, 1000.0L))
            return std::nullopt;
        return timing_lane_value_t{timing_bpm_value_t{{*primary_numerator, *primary_denominator}}};
    }
    case timing_lane_kind_t::scroll: {
        if (!primary_numerator || !primary_denominator ||
            !timing_ratio_in_bounds(*primary_numerator, *primary_denominator, -1000.0L, 1000.0L))
            return std::nullopt;
        return timing_lane_value_t{
            timing_scroll_value_t{{*primary_numerator, *primary_denominator}}};
    }
    case timing_lane_kind_t::speed: {
        if (!primary_numerator || !primary_denominator || !secondary_numerator ||
            *secondary_numerator < 0 || !secondary_denominator ||
            !timing_ratio_in_bounds(*primary_numerator, *primary_denominator, 0.0L, 1000.0L) ||
            !timing_unsigned_ratio_in_bounds(static_cast<std::uint64_t>(*secondary_numerator),
                                             *secondary_denominator, 10000.0L)) {
            return std::nullopt;
        }
        return timing_lane_value_t{timing_speed_value_t{
            {*primary_numerator, *primary_denominator},
            {static_cast<std::uint64_t>(*secondary_numerator), *secondary_denominator},
            action.timing_transition_is_seconds}};
    }
    case timing_lane_kind_t::stop:
    case timing_lane_kind_t::delay: {
        if (!primary_numerator || !primary_denominator || *primary_numerator < 0 ||
            !timing_unsigned_ratio_in_bounds(static_cast<std::uint64_t>(*primary_numerator),
                                             *primary_denominator, 86400.0L)) {
            return std::nullopt;
        }
        return timing_lane_value_t{timing_pause_value_t{{*primary_numerator, *primary_denominator},
                                                        pause_unit_t::seconds}};
    }
    case timing_lane_kind_t::warp: {
        if (!secondary_numerator || *secondary_numerator < 1 || !secondary_denominator ||
            *secondary_denominator != 1 || *secondary_numerator > 1000000) {
            return std::nullopt;
        }
        const auto units = static_cast<std::uint64_t>(*secondary_numerator);
        const auto measures = build_measure_index(chart);
        const auto* index = std::get_if<measure_index_t>(&measures);
        const auto start = index ? absolute_row(*index, preview.position) : std::nullopt;
        const auto step = timing_stage_grid_step(chart);
        if (!index || !start ||
            units > (std::numeric_limits<std::uint64_t>::max() - *start) / step) {
            return std::nullopt;
        }
        const auto end_row = *start + units * step;
        if (end_row >= index->total_rows) {
            return std::nullopt;
        }
        const auto end = timing_position_at_row(*index, end_row);
        return end ? std::optional<timing_lane_value_t>{timing_warp_value_t{
                         *end, warp_provenance_t::native}}
                   : std::nullopt;
    }
    case timing_lane_kind_t::kiai:
    case timing_lane_kind_t::anchor:
        return std::nullopt;
    }
    return std::nullopt;
}

void populate_timing_view(flattened_workspace_t& flattened,
                          const wormedit::document_history_t& history,
                          const wormedit::workspace_controller_t& workspace,
                          timing_scene_cache_t& cache, int window_width, int window_height,
                          const wormedit::timing_lane_pseudonote_preview_t* stage) {
    auto& view = flattened.view.timing;
    if (!view.visible) {
        return;
    }
    view.viewport_width_pixels = std::max(160.0, static_cast<double>(window_width) - 48.0);
    view.viewport_height_pixels = compose_surface_height(window_height, flattened.view);
    if (!workspace.selected_chart) {
        flattened.timing_status_storage = "Choose a difficulty to display its timing lanes.";
        view.status_text = flattened.timing_status_storage.c_str();
        return;
    }
    const auto chart_it = std::ranges::find(history.document.charts, *workspace.selected_chart,
                                            &wormedit::chart_document_t::id);
    if (chart_it == history.document.charts.end()) {
        flattened.timing_status_storage = "The selected difficulty is no longer available.";
        view.status_text = flattened.timing_status_storage.c_str();
        return;
    }
    const auto& chart = *chart_it;
    view.chart_id = chart.id.value;
    view.document_revision = history.document.revision;
    view.speed_total_count = chart.speeds.size();
    if (!cache.attempted || cache.chart != chart.id ||
        cache.revision != history.document.revision) {
        cache = {.chart = chart.id, .revision = history.document.revision, .attempted = true};
        const auto timing = wormedit::build_timing_index(chart);
        if (!timing.index) {
            cache.status = timing.diagnostics.empty() ? "The timing index could not be built."
                                                      : timing.diagnostics.front().message;
        } else {
            const auto scroll_speed = wormedit::build_scroll_speed_index(chart, *timing.index);
            if (!scroll_speed.index) {
                cache.status = scroll_speed.diagnostics.empty()
                                   ? "The SPEEDS evaluator could not be built."
                                   : scroll_speed.diagnostics.front().message;
            } else {
                cache.total_rows = scroll_speed.index->total_rows;
                const auto spans = wormedit::scroll_speed_transition_spans(
                    *scroll_speed.index, *timing.index,
                    {.maximum_spans = WORM_UI_TIMING_SPEED_CAPACITY});
                if (!spans) {
                    cache.status = spans.diagnostics.empty()
                                       ? "The SPEEDS transition spans could not be built."
                                       : spans.diagnostics.front().message;
                }
                cache.truncated = spans.truncated;
                cache.speeds.reserve(spans.spans.size());
                for (std::size_t index = 0; index < spans.spans.size(); ++index) {
                    const auto& speed = scroll_speed.index->speed_segments[index];
                    const auto& span = spans.spans[index];
                    cache.speeds.push_back({
                        .object_id = speed.source_event.value,
                        .marker_row = static_cast<long double>(speed.begin_row),
                        .affected_begin_row = rational_as_long_double(span.begin_row),
                        .affected_end_row = rational_as_long_double(span.end_row),
                        .multiplier =
                            static_cast<double>(rational_as_long_double(speed.target_multiplier)),
                        .transition_duration = rational_as_double(speed.transition_duration),
                        .transition_is_seconds = speed.transition_is_seconds,
                        .transition_clipped = span.clipped,
                    });
                }
            }
        }
    }
    if (!cache.status.empty()) {
        flattened.timing_status_storage = cache.status;
        view.status_text = flattened.timing_status_storage.c_str();
        return;
    }
    if (cache.total_rows == 0) {
        flattened.timing_status_storage = "This difficulty has no timing rows to display.";
        view.status_text = flattened.timing_status_storage.c_str();
        return;
    }
    view.total_rows = cache.total_rows;

    const auto row_to_pixels = [&](long double row) {
        const auto bounded = std::clamp(row, 0.0L, static_cast<long double>(cache.total_rows));
        return static_cast<double>(bounded / static_cast<long double>(cache.total_rows) *
                                   view.viewport_height_pixels);
    };
    flattened.timing_speed_storage.reserve(cache.speeds.size());
    for (const auto& speed : cache.speeds) {
        flattened.timing_speed_storage.push_back({
            .object_id = speed.object_id,
            .marker_y_pixels = row_to_pixels(speed.marker_row),
            .affected_begin_y_pixels = row_to_pixels(speed.affected_begin_row),
            .affected_end_y_pixels = row_to_pixels(speed.affected_end_row),
            .multiplier = speed.multiplier,
            .transition_duration = speed.transition_duration,
            .transition_is_seconds = speed.transition_is_seconds,
            .transition_clipped = speed.transition_clipped,
        });
    }
    flattened.timing_marker_storage.clear();
    const auto measure_result = wormedit::build_measure_index(chart);
    const auto* measures = std::get_if<wormedit::measure_index_t>(&measure_result);
    std::size_t marker_reserve = 0;
    for (const auto count :
         {chart.bpms.size(), chart.scrolls.size(), chart.pauses.size(), chart.warps.size()}) {
        marker_reserve +=
            std::min<std::size_t>(count, WORM_UI_TIMING_MARKER_CAPACITY - marker_reserve);
    }
    flattened.timing_marker_storage.reserve(marker_reserve);
    if (measures) {
        const auto append_marker = [&](wormedit::local_position_t position, std::uint64_t id,
                                       WormUiTimingLaneKind lane) {
            if (flattened.timing_marker_storage.size() >= WORM_UI_TIMING_MARKER_CAPACITY) {
                view.truncated = true;
                return;
            }
            const auto row = wormedit::absolute_row(*measures, position);
            if (!row) {
                return;
            }
            flattened.timing_marker_storage.push_back({
                .object_id = id,
                .marker_y_pixels = row_to_pixels(static_cast<long double>(*row)),
                .lane = lane,
            });
        };
        for (const auto& event : chart.bpms)
            append_marker(event.position, event.id.value, WORM_UI_TIMING_LANE_BPM);
        for (const auto& event : chart.scrolls)
            append_marker(event.position, event.id.value, WORM_UI_TIMING_LANE_SCROLL);
        for (const auto& event : chart.pauses)
            append_marker(event.position, event.id.value,
                          event.kind == wormedit::pause_kind_t::stop ? WORM_UI_TIMING_LANE_STOP
                                                                     : WORM_UI_TIMING_LANE_DELAY);
        for (const auto& event : chart.warps)
            append_marker(event.span.begin, event.id.value, WORM_UI_TIMING_LANE_WARP);
    }
    view.scene_available = true;
    view.truncated = view.truncated || cache.truncated;
    view.speeds = flattened.timing_speed_storage.data();
    view.speed_count = flattened.timing_speed_storage.size();
    view.markers = flattened.timing_marker_storage.data();
    view.marker_count = flattened.timing_marker_storage.size();
    if (stage && stage->status == wormedit::timing_lane_pseudonote_status_t::editing &&
        stage->chart == chart.id && stage->document_revision == history.document.revision) {
        view.stage_active = true;
        view.stage_valid = stage->valid;
        view.stage_lane = timing_lane_kind_to_ui(stage->kind);
        const auto stage_row =
            measures ? wormedit::absolute_row(*measures, stage->position) : std::nullopt;
        view.stage_marker_y_pixels =
            stage_row ? row_to_pixels(static_cast<long double>(*stage_row)) : 0.0;
        view.stage_transition_is_seconds = false;
        std::visit(
            [&](const auto& value) {
                using value_t = std::decay_t<decltype(value)>;
                const auto set_primary = [&](std::int64_t numerator, std::uint64_t denominator) {
                    (void)std::snprintf(view.stage_primary_numerator_text,
                                        sizeof(view.stage_primary_numerator_text), "%lld",
                                        static_cast<long long>(numerator));
                    (void)std::snprintf(view.stage_primary_denominator_text,
                                        sizeof(view.stage_primary_denominator_text), "%llu",
                                        static_cast<unsigned long long>(denominator));
                };
                const auto set_secondary = [&](std::uint64_t numerator, std::uint64_t denominator) {
                    (void)std::snprintf(view.stage_secondary_numerator_text,
                                        sizeof(view.stage_secondary_numerator_text), "%llu",
                                        static_cast<unsigned long long>(numerator));
                    (void)std::snprintf(view.stage_secondary_denominator_text,
                                        sizeof(view.stage_secondary_denominator_text), "%llu",
                                        static_cast<unsigned long long>(denominator));
                };
                if constexpr (std::is_same_v<value_t, wormedit::timing_bpm_value_t>) {
                    view.stage_value =
                        static_cast<double>(rational_as_long_double(value.beats_per_minute));
                    set_primary(value.beats_per_minute.numerator,
                                value.beats_per_minute.denominator);
                } else if constexpr (std::is_same_v<value_t, wormedit::timing_scroll_value_t>) {
                    view.stage_value =
                        static_cast<double>(rational_as_long_double(value.multiplier));
                    set_primary(value.multiplier.numerator, value.multiplier.denominator);
                } else if constexpr (std::is_same_v<value_t, wormedit::timing_speed_value_t>) {
                    view.stage_value =
                        static_cast<double>(rational_as_long_double(value.multiplier));
                    view.stage_secondary_value = rational_as_double(value.transition_beats);
                    view.stage_transition_is_seconds = value.transition_is_seconds;
                    set_primary(value.multiplier.numerator, value.multiplier.denominator);
                    set_secondary(value.transition_beats.numerator,
                                  value.transition_beats.denominator);
                } else if constexpr (std::is_same_v<value_t, wormedit::timing_pause_value_t>) {
                    view.stage_value = static_cast<double>(rational_as_long_double(value.duration));
                    set_primary(value.duration.numerator, value.duration.denominator);
                } else if constexpr (std::is_same_v<value_t, wormedit::timing_warp_value_t>) {
                    const auto end_row =
                        measures ? wormedit::absolute_row(*measures, value.end) : std::nullopt;
                    const auto start_row = stage_row;
                    const auto step = timing_stage_grid_step(chart);
                    const auto length_steps =
                        end_row && start_row && *end_row > *start_row
                            ? static_cast<long double>(*end_row - *start_row) /
                                  static_cast<long double>(step)
                            : 1.0L;
                    view.stage_secondary_value = static_cast<double>(length_steps);
                    (void)std::snprintf(view.stage_secondary_numerator_text,
                                        sizeof(view.stage_secondary_numerator_text), "%.0Lf",
                                        length_steps);
                    (void)std::snprintf(view.stage_secondary_denominator_text,
                                        sizeof(view.stage_secondary_denominator_text), "1");
                }
            },
            stage->value);
        if (!stage->diagnostics.empty()) {
            flattened.timing_status_storage = stage->diagnostics.front().message;
            view.stage_status_text = flattened.timing_status_storage.c_str();
        }
    }
}

auto compose_lane_is_editable(const wormedit::lane_definition_t& lane) -> bool {
    using wormedit::lane_role_t;
    return lane.role != lane_role_t::autoplay && lane.role != lane_role_t::invisible &&
           lane.role != lane_role_t::mine;
}

auto compose_physical_scancode(wormedit::physical_key_t key) -> std::uint32_t {
    using wormedit::physical_key_t;
    switch (key) {
    case physical_key_t::digit_1:
        return SDL_SCANCODE_1;
    case physical_key_t::digit_2:
        return SDL_SCANCODE_2;
    case physical_key_t::digit_3:
        return SDL_SCANCODE_3;
    case physical_key_t::digit_4:
        return SDL_SCANCODE_4;
    case physical_key_t::digit_5:
        return SDL_SCANCODE_5;
    case physical_key_t::digit_6:
        return SDL_SCANCODE_6;
    case physical_key_t::digit_7:
        return SDL_SCANCODE_7;
    case physical_key_t::digit_8:
        return SDL_SCANCODE_8;
    case physical_key_t::digit_9:
        return SDL_SCANCODE_9;
    case physical_key_t::digit_0:
        return SDL_SCANCODE_0;
    case physical_key_t::key_q:
        return SDL_SCANCODE_Q;
    case physical_key_t::key_w:
        return SDL_SCANCODE_W;
    case physical_key_t::key_e:
        return SDL_SCANCODE_E;
    case physical_key_t::key_r:
        return SDL_SCANCODE_R;
    case physical_key_t::key_t:
        return SDL_SCANCODE_T;
    case physical_key_t::key_y:
        return SDL_SCANCODE_Y;
    case physical_key_t::key_u:
        return SDL_SCANCODE_U;
    case physical_key_t::key_i:
        return SDL_SCANCODE_I;
    case physical_key_t::key_o:
        return SDL_SCANCODE_O;
    case physical_key_t::key_p:
        return SDL_SCANCODE_P;
    case physical_key_t::key_a:
        return SDL_SCANCODE_A;
    case physical_key_t::key_s:
        return SDL_SCANCODE_S;
    case physical_key_t::key_d:
        return SDL_SCANCODE_D;
    case physical_key_t::key_f:
        return SDL_SCANCODE_F;
    case physical_key_t::key_g:
        return SDL_SCANCODE_G;
    case physical_key_t::key_h:
        return SDL_SCANCODE_H;
    case physical_key_t::key_j:
        return SDL_SCANCODE_J;
    case physical_key_t::key_k:
        return SDL_SCANCODE_K;
    case physical_key_t::key_l:
        return SDL_SCANCODE_L;
    case physical_key_t::key_z:
        return SDL_SCANCODE_Z;
    case physical_key_t::key_x:
        return SDL_SCANCODE_X;
    case physical_key_t::key_c:
        return SDL_SCANCODE_C;
    case physical_key_t::key_v:
        return SDL_SCANCODE_V;
    case physical_key_t::key_b:
        return SDL_SCANCODE_B;
    case physical_key_t::key_n:
        return SDL_SCANCODE_N;
    case physical_key_t::key_m:
        return SDL_SCANCODE_M;
    case physical_key_t::unknown:
        return 0;
    }
    return 0;
}

void populate_compose_view(flattened_workspace_t& flattened,
                           const wormedit::document_history_t& history,
                           const wormedit::workspace_controller_t& workspace,
                           compose_scene_cache_t& cache, int window_width, int window_height,
                           std::optional<wormedit::editor_interaction_state_t>& retained_state) {
    auto& view = flattened.view.compose;
    if (!view.visible)
        return;
    view.viewport_width_pixels = std::max(160.0, static_cast<double>(window_width) - 48.0);
    view.viewport_height_pixels = compose_surface_height(window_height, flattened.view);
    view.pixels_per_standard_measure = retained_state
                                           ? retained_state->pixels_per_standard_measure
                                           : wormedit::default_pixels_per_standard_measure;
    view.status_text = nullptr;
    if (!workspace.selected_chart) {
        flattened.compose_status_storage = "Choose a difficulty to display its chart field.";
        view.status_text = flattened.compose_status_storage.c_str();
        return;
    }
    const auto chart_it = std::ranges::find(history.document.charts, *workspace.selected_chart,
                                            &wormedit::chart_document_t::id);
    if (chart_it == history.document.charts.end()) {
        flattened.compose_status_storage = "The selected difficulty is no longer available.";
        view.status_text = flattened.compose_status_storage.c_str();
        return;
    }
    const auto& chart = *chart_it;
    view.chart_id = chart.id.value;
    view.document_revision = history.document.revision;
    if (!cache.attempted || cache.chart != chart.id ||
        cache.revision != history.document.revision) {
        cache.index.reset();
        cache.status.clear();
        cache.chart = chart.id;
        cache.revision = history.document.revision;
        cache.attempted = true;
        const auto built =
            wormedit::build_editor_field_scene_index(chart, history.document.revision);
        if (std::holds_alternative<wormedit::editor_field_scene_index_t>(built)) {
            cache.index = std::get<wormedit::editor_field_scene_index_t>(built);
        } else {
            const auto& diagnostics = std::get<wormedit::diagnostic_list_t>(built);
            cache.status = diagnostics.empty() ? "The field layout could not be indexed."
                                               : diagnostics.front().message;
        }
    }
    if (!cache.index) {
        flattened.compose_status_storage = cache.status;
        view.status_text = flattened.compose_status_storage.c_str();
        return;
    }
    if (chart.measures.empty()) {
        flattened.compose_status_storage = "This difficulty has no measures to display.";
        view.status_text = flattened.compose_status_storage.c_str();
        return;
    }

    const auto measure_index = wormedit::build_measure_index(chart);
    if (!std::holds_alternative<wormedit::measure_index_t>(measure_index)) {
        const auto& diagnostics = std::get<wormedit::diagnostic_list_t>(measure_index);
        flattened.compose_status_storage = diagnostics.empty()
                                               ? "The chart measure geometry is invalid."
                                               : diagnostics.front().message;
        view.status_text = flattened.compose_status_storage.c_str();
        return;
    }
    constexpr std::array<std::uint64_t, 4> snap_denominators{4, 8, 16, 32};
    if (!retained_state || retained_state->chart != chart.id) {
        std::optional<wormedit::local_position_t> initial_cursor;
        std::optional<wormedit::lane_id_t> initial_lane;
        std::uint64_t earliest_layout_row = std::numeric_limits<std::uint64_t>::max();
        for (const auto& segment : chart.lane_layouts) {
            const auto row = wormedit::absolute_row(
                std::get<wormedit::measure_index_t>(measure_index), segment.position);
            if (!row || *row >= earliest_layout_row)
                continue;
            const auto lane = std::ranges::find_if(segment.layout.lanes, compose_lane_is_editable);
            if (lane == segment.layout.lanes.end())
                continue;
            earliest_layout_row = *row;
            initial_cursor = segment.position;
            initial_lane = lane->id;
        }
        if (!initial_cursor || !initial_lane) {
            flattened.compose_status_storage = "This difficulty has no editable lanes to display.";
            view.status_text = flattened.compose_status_storage.c_str();
            return;
        }
        const auto initial =
            wormedit::make_editor_interaction(history, chart.id, *initial_lane, *initial_cursor,
                                              snap_denominators, 4, view.viewport_height_pixels);
        if (!initial.state || !initial.view) {
            flattened.compose_status_storage = initial.diagnostics.empty()
                                                   ? "The chart field view could not be created."
                                                   : initial.diagnostics.front().message;
            view.status_text = flattened.compose_status_storage.c_str();
            return;
        }
        retained_state = *initial.state;
    }

    retained_state->viewport_height_pixels = view.viewport_height_pixels;
    auto interaction = wormedit::make_editor_interaction(
        history, chart.id, retained_state->active_lane, retained_state->cursor,
        retained_state->supported_snap_denominators, retained_state->active_snap_denominator,
        view.viewport_height_pixels);
    if (!interaction.view) {
        // Undo, recovery, or an upstream chart edit may invalidate a retained cursor.
        retained_state.reset();
        flattened.compose_status_storage = "The retained chart cursor is no longer valid.";
        view.status_text = flattened.compose_status_storage.c_str();
        return;
    }
    auto interaction_view = *interaction.view;
    interaction_view.pixels_per_standard_measure = retained_state->pixels_per_standard_measure;
    interaction_view.viewport_scroll_pixels = retained_state->viewport_scroll_pixels;
    interaction_view.pending_hold = retained_state->pending_hold;
    interaction_view.pending_note_duration_gesture = retained_state->pending_note_duration_gesture;
    interaction_view.pending_note_paint_gesture =
        retained_state->pending_note_paint_gesture
            ? std::optional{wormedit::note_paint_gesture_preview_t{
                  .document_revision =
                      retained_state->pending_note_paint_gesture->document_revision,
                  .targets = retained_state->pending_note_paint_gesture->targets,
              }}
            : std::nullopt;
    interaction_view.pending_editor_area_selection =
        retained_state->pending_editor_area_selection
            ? std::optional{wormedit::editor_area_selection_preview_t{
                  .document_revision =
                      retained_state->pending_editor_area_selection->document_revision,
                  .left_pixels =
                      std::min(retained_state->pending_editor_area_selection->anchor_x_pixels,
                               retained_state->pending_editor_area_selection->current_x_pixels),
                  .top_pixels =
                      std::min(retained_state->pending_editor_area_selection->anchor_y_pixels,
                               retained_state->pending_editor_area_selection->current_y_pixels),
                  .right_pixels =
                      std::max(retained_state->pending_editor_area_selection->anchor_x_pixels,
                               retained_state->pending_editor_area_selection->current_x_pixels),
                  .bottom_pixels =
                      std::max(retained_state->pending_editor_area_selection->anchor_y_pixels,
                               retained_state->pending_editor_area_selection->current_y_pixels),
                  .notes = retained_state->pending_editor_area_selection->notes,
                  .objects = retained_state->pending_editor_area_selection->objects,
              }}
            : std::nullopt;
    interaction_view.selected_notes = retained_state->selected_notes;
    interaction_view.selected_objects = retained_state->selected_objects;
    const auto content_height = static_cast<long double>(cache.index->measures.total_rows) *
                                interaction_view.pixels_per_standard_measure /
                                chart.divisions_per_standard_measure;
    const auto maximum_scroll =
        std::max(0.0L, content_height - static_cast<long double>(view.viewport_height_pixels));
    retained_state->viewport_scroll_pixels = std::clamp(retained_state->viewport_scroll_pixels, 0.0,
                                                        static_cast<double>(maximum_scroll));
    interaction_view.viewport_scroll_pixels = retained_state->viewport_scroll_pixels;
    view.pixels_per_standard_measure = interaction_view.pixels_per_standard_measure;
    const auto scalar_selection_mode = [](wormedit::editor_area_selection_mode_t mode) {
        switch (mode) {
        case wormedit::editor_area_selection_mode_t::replace:
            return WORM_UI_COMPOSE_SELECTION_REPLACE;
        case wormedit::editor_area_selection_mode_t::add:
            return WORM_UI_COMPOSE_SELECTION_ADD;
        case wormedit::editor_area_selection_mode_t::toggle:
            return WORM_UI_COMPOSE_SELECTION_TOGGLE;
        }
        return WORM_UI_COMPOSE_SELECTION_REPLACE;
    };
    if (interaction_view.pending_editor_area_selection) {
        const auto& preview = *interaction_view.pending_editor_area_selection;
        view.area_selection_preview_visible = true;
        view.area_selection_preview_mode =
            scalar_selection_mode(retained_state->pending_editor_area_selection->mode);
        view.area_selection_preview_bounds = {
            std::clamp(preview.left_pixels, 0.0, view.viewport_width_pixels),
            std::clamp(preview.top_pixels, 0.0, view.viewport_height_pixels),
            std::clamp(preview.right_pixels, 0.0, view.viewport_width_pixels),
            std::clamp(preview.bottom_pixels, 0.0, view.viewport_height_pixels),
        };
    }
    if (retained_state->pending_space_range_selection) {
        const auto& pending = *retained_state->pending_space_range_selection;
        const auto& measures = std::get<wormedit::measure_index_t>(measure_index);
        const auto anchor_row = wormedit::absolute_row(measures, pending.anchor);
        const auto cursor_row = wormedit::absolute_row(measures, retained_state->cursor);
        if (anchor_row && cursor_row) {
            const auto pixels_per_row = interaction_view.pixels_per_standard_measure /
                                        static_cast<double>(chart.divisions_per_standard_measure);
            const auto first_row = std::min(*anchor_row, *cursor_row);
            const auto last_row = std::max(*anchor_row, *cursor_row);
            const auto top = static_cast<double>(first_row) * pixels_per_row -
                             interaction_view.viewport_scroll_pixels;
            const auto bottom = static_cast<double>(last_row + 1) * pixels_per_row -
                                interaction_view.viewport_scroll_pixels;
            view.space_range_preview_visible = true;
            view.space_range_preview_mode = scalar_selection_mode(pending.mode);
            view.space_range_preview_bounds = {
                0.0,
                std::clamp(top, 0.0, view.viewport_height_pixels),
                view.viewport_width_pixels,
                std::clamp(bottom, 0.0, view.viewport_height_pixels),
            };
        }
    }
    flattened.compose_interaction_view_storage = interaction_view;
    std::size_t lane_key_binding_count{};
    bool lane_key_bindings_truncated{};
    for (const auto& binding : chart.input_profile.bindings) {
        const auto scancode = compose_physical_scancode(binding.key);
        if (scancode == 0) {
            continue;
        }
        if (lane_key_binding_count >= flattened.compose_lane_key_storage.size()) {
            lane_key_bindings_truncated = true;
            break;
        }
        flattened.compose_lane_key_storage[lane_key_binding_count++] = {
            .scancode = scancode,
            .lane_id = binding.lane.value,
        };
    }
    view.lane_key_bindings = flattened.compose_lane_key_storage.data();
    view.lane_key_binding_count = lane_key_binding_count;

    const auto composed = wormedit::make_editor_field_scene(
        chart, *cache.index, interaction_view,
        {.field_left_pixels = 0.0,
         .field_width_pixels = view.viewport_width_pixels,
         .split_stage_gutter_pixels = 14.0,
         .maximum_grid_lines = WORM_UI_COMPOSE_GRID_CAPACITY,
         .maximum_scene_primitives = WORM_UI_COMPOSE_SCENE_PRIMITIVE_CAPACITY});
    if (!composed.scene || composed.has_errors()) {
        flattened.compose_status_storage = composed.diagnostics.empty()
                                               ? "The chart field could not be composed."
                                               : composed.diagnostics.front().message;
        view.status_text = flattened.compose_status_storage.c_str();
        return;
    }
    const auto& scene = *composed.scene;
    view.scene_available = true;
    const auto convert_rect = [](const wormedit::editor_field_rect_t& rect) {
        return WormUiComposeRect{rect.left, rect.top, rect.right, rect.bottom};
    };
    const auto& bands = scene.layout_bands;
    const auto band_count =
        std::min(bands.size(), static_cast<std::size_t>(WORM_UI_COMPOSE_BAND_CAPACITY));
    flattened.compose_band_storage.reserve(band_count);
    for (std::size_t index = 0; index < band_count; ++index) {
        const auto& band = bands[index];
        flattened.compose_band_storage.push_back(
            {.bounds = {0.0, band.top_pixels, view.viewport_width_pixels, band.bottom_pixels},
             .split_stage = band.split_stage});
    }
    const auto& lanes = scene.lanes;
    const auto lane_count =
        std::min(lanes.size(), static_cast<std::size_t>(WORM_UI_COMPOSE_LANE_CAPACITY));
    flattened.compose_lane_storage.reserve(lane_count);
    for (std::size_t index = 0; index < lane_count; ++index) {
        const auto& lane = lanes[index];
        flattened.compose_lane_storage.push_back({.bounds = convert_rect(lane.bounds),
                                                  .lane_id = lane.lane.value,
                                                  .display_order = lane.display_order,
                                                  .role = static_cast<std::uint8_t>(lane.role),
                                                  .stage = static_cast<std::uint8_t>(lane.stage),
                                                  .glyph = static_cast<std::uint8_t>(lane.glyph)});
    }
    const auto& lines = scene.grid_lines;
    const auto line_count =
        std::min(lines.size(), static_cast<std::size_t>(WORM_UI_COMPOSE_GRID_CAPACITY));
    flattened.compose_grid_storage.reserve(line_count);
    for (std::size_t index = 0; index < line_count; ++index) {
        const auto& line = lines[index];
        flattened.compose_grid_storage.push_back(
            {.y_pixels = line.y_pixels,
             .kind = static_cast<std::uint8_t>(line.kind),
             .emphasis = static_cast<std::uint8_t>(line.emphasis)});
    }
    const auto& differences = scene.comparison_differences;
    const auto difference_count =
        std::min(differences.size(), static_cast<std::size_t>(WORM_UI_COMPOSE_DIFFERENCE_CAPACITY));
    flattened.compose_difference_storage.reserve(difference_count);
    for (std::size_t index = 0; index < difference_count; ++index) {
        flattened.compose_difference_storage.push_back(
            {.bounds = convert_rect(differences[index].bounds)});
    }
    const auto& holds = scene.hold_bodies;
    const auto hold_count =
        std::min(holds.size(), static_cast<std::size_t>(WORM_UI_COMPOSE_HOLD_CAPACITY));
    flattened.compose_hold_storage.reserve(hold_count);
    for (std::size_t index = 0; index < hold_count; ++index) {
        const auto& hold = holds[index];
        flattened.compose_hold_storage.push_back({.bounds = convert_rect(hold.bounds),
                                                  .note_id = hold.note.value,
                                                  .lane_id = hold.lane.value,
                                                  .selected = hold.selected});
    }
    const auto& endpoints = scene.note_endpoints;
    const auto endpoint_count =
        std::min(endpoints.size(), static_cast<std::size_t>(WORM_UI_COMPOSE_NOTE_CAPACITY));
    flattened.compose_note_storage.reserve(endpoint_count);
    for (std::size_t index = 0; index < endpoint_count; ++index) {
        const auto& endpoint = endpoints[index];
        flattened.compose_note_storage.push_back({.bounds = convert_rect(endpoint.bounds),
                                                  .note_id = endpoint.note.value,
                                                  .lane_id = endpoint.lane.value,
                                                  .kind = static_cast<std::uint8_t>(endpoint.kind),
                                                  .tail = endpoint.tail,
                                                  .selected = endpoint.selected});
    }
    if (scene.cursor) {
        flattened.compose_cursor_storage = WormUiComposeCursorView{
            .bounds = convert_rect(scene.cursor->bounds),
            .lane_id = scene.cursor->lane.value,
        };
    }
    view.truncated = lane_key_bindings_truncated || band_count != bands.size() ||
                     lane_count != lanes.size() || line_count != lines.size() ||
                     difference_count != differences.size() || hold_count != holds.size() ||
                     endpoint_count != endpoints.size();
    view.bands = flattened.compose_band_storage.data();
    view.band_count = flattened.compose_band_storage.size();
    view.lanes = flattened.compose_lane_storage.data();
    view.lane_count = flattened.compose_lane_storage.size();
    view.grid_lines = flattened.compose_grid_storage.data();
    view.grid_line_count = flattened.compose_grid_storage.size();
    view.differences = flattened.compose_difference_storage.data();
    view.difference_count = flattened.compose_difference_storage.size();
    view.hold_bodies = flattened.compose_hold_storage.data();
    view.hold_body_count = flattened.compose_hold_storage.size();
    view.note_endpoints = flattened.compose_note_storage.data();
    view.note_endpoint_count = flattened.compose_note_storage.size();
    view.cursor = flattened.compose_cursor_storage ? &*flattened.compose_cursor_storage : nullptr;
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

auto autosave_status_text(const wormedit::native_project_autosave_result_t& result) -> std::string {
    using status_t = wormedit::native_project_autosave_status_t;
    if (result.status == status_t::published)
        return "Autosaved revision " + std::to_string(result.revision) + ".";
    if (result.status != status_t::failed)
        return {};
    if (result.diagnostics.empty())
        return "Autosave failed; the previous recovery backup was retained.";
    const auto& diagnostic = result.diagnostics.front();
    auto message = "Autosave failed: " + diagnostic.message;
    if (!diagnostic.path.empty())
        message += " (" + diagnostic.path.generic_string() + ")";
    return message;
}

auto dispatch_recovery_ui_action(
    const WormUiWorkspaceAction& action, wormedit::native_project_recovery_controller_t& controller,
    wormedit::document_history_t& history, wormedit::workspace_controller_t& workspace,
    std::optional<wormedit::native_project_document_version_t> scanned_primary_version,
    wormedit::native_project_autosave_loop_t& autosave_loop) -> std::string {
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
        if (!history.undo.empty() || !history.redo.empty()) {
            return "The current workspace changed after recovery discovery; save or restart "
                   "before replacing it with a recovery candidate.";
        }
        request = recover_native_project_recovery_action_t{
            .target = target,
            .expected_session =
                {
                    .is_set = true,
                    .version =
                        scanned_primary_version.has_value()
                            ? std::optional<
                                  native_project_document_version_t>{native_project_document_version_t{
                                  .project_id = history.document.id,
                                  .revision = history.document.revision}}
                            : std::nullopt,
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
            const auto& recovery_view = controller.view();
            std::optional<native_project_document_version_t> recovered_save_baseline;
            auto recovered_sequence = std::uint64_t{};
            if (action.recovery_row_index < recovery_view.rows.size()) {
                const auto& row = recovery_view.rows[action.recovery_row_index];
                if (row.last_explicit_save_revision) {
                    recovered_save_baseline = native_project_document_version_t{
                        .project_id = result.document->id,
                        .revision = *row.last_explicit_save_revision};
                }
                if (row.recovery_session_sequence)
                    recovered_sequence = *row.recovery_session_sequence;
            }
            if (!autosave_loop.reset_session(native_project_autosave_loop_t::clock_t::now(),
                                             recovered_save_baseline, recovered_sequence)) {
                return "Wait for the current autosave to finish before recovering a session.";
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

auto dispatch_metadata_ui_action(const WormUiWorkspaceAction& action,
                                 wormedit::document_history_t& history,
                                 wormedit::workspace_controller_t& workspace) -> std::string {
    using namespace wormedit;
    const auto failed = [](const document_command_result_t& result) {
        if (!result.diagnostics.empty())
            return result.diagnostics.front().message;
        return std::string{"Metadata edit was not applied."};
    };
    const auto publish = [&](document_command_result_t result) {
        if (!result.applied)
            return result.diagnostics.empty() ? std::string{"No metadata changes."}
                                              : failed(result);
        const auto rebound = rebind_workspace(workspace, history);
        if (!rebound) {
            return rebound.diagnostics.empty()
                       ? std::string{"Metadata changed, but workspace rebinding failed."}
                       : rebound.diagnostics.front().message;
        }
        return std::string{"Metadata updated."};
    };

    switch (action.metadata_edit) {
    case WORM_UI_METADATA_EDIT_SONG: {
        if (!action.metadata_title || !action.metadata_artist ||
            action.metadata_search_tag_count > WORM_UI_METADATA_SEARCH_TAG_CAPACITY ||
            (action.metadata_search_tag_count != 0 && !action.metadata_search_tags)) {
            return "Metadata edit payload is incomplete or exceeds the tag-count cap.";
        }
        std::vector<std::string> tags;
        tags.reserve(action.metadata_search_tag_count);
        for (std::size_t index = 0; index < action.metadata_search_tag_count; ++index) {
            if (!action.metadata_search_tags[index])
                return "Metadata edit contains an empty tag pointer.";
            tags.emplace_back(action.metadata_search_tags[index]);
        }
        return publish(apply_metadata_edit(history, action.metadata_document_revision,
                                           set_song_metadata_action_t{action.metadata_title,
                                                                      action.metadata_artist,
                                                                      std::move(tags)}));
    }
    case WORM_UI_METADATA_EDIT_CHART: {
        if (action.metadata_chart_id == 0 || !action.metadata_chart_name ||
            !action.metadata_chart_author ||
            (action.metadata_has_chart_rating &&
             (!action.metadata_chart_rating_numerator_text ||
              !action.metadata_chart_rating_denominator_text))) {
            return "Chart metadata edit payload is incomplete.";
        }
        const auto rating = make_chart_rating_metadata_edit(
            chart_id_t{action.metadata_chart_id}, !action.metadata_has_chart_rating,
            action.metadata_chart_rating_numerator_text
                ? action.metadata_chart_rating_numerator_text
                : "",
            action.metadata_chart_rating_denominator_text
                ? action.metadata_chart_rating_denominator_text
                : "");
        if (!rating) {
            return "Rating must use a signed 64-bit numerator and positive 64-bit denominator.";
        }
        return publish(apply_metadata_edit(
            history, action.metadata_document_revision,
            set_chart_metadata_action_t{chart_id_t{action.metadata_chart_id},
                                        action.metadata_chart_name, action.metadata_chart_author,
                                        rating->rating}));
    }
    case WORM_UI_METADATA_EDIT_REPLACE_FORMAT_FIELD:
    case WORM_UI_METADATA_EDIT_ERASE_FORMAT_FIELD:
    case WORM_UI_METADATA_EDIT_INSERT_FORMAT_FIELD: {
        if (action.metadata_format_family > static_cast<std::uint8_t>(format_family_t::quaver)) {
            return "Format metadata edit refers to an unknown format family.";
        }
        const format_metadata_target_t target{
            action.metadata_format_chart_scoped
                ? std::optional<chart_id_t>{chart_id_t{action.metadata_chart_id}}
                : std::nullopt,
            static_cast<format_family_t>(action.metadata_format_family)};
        metadata_edit_action_t edit;
        if (action.metadata_edit == WORM_UI_METADATA_EDIT_REPLACE_FORMAT_FIELD) {
            if (!action.metadata_format_name || !action.metadata_format_value)
                return "Format metadata replacement payload is incomplete.";
            edit = replace_format_metadata_field_action_t{target, action.metadata_format_index,
                                                          action.metadata_format_name,
                                                          action.metadata_format_value};
        } else if (action.metadata_edit == WORM_UI_METADATA_EDIT_ERASE_FORMAT_FIELD) {
            edit = erase_format_metadata_field_action_t{target, action.metadata_format_index};
        } else {
            if (!action.metadata_format_name || !action.metadata_format_value)
                return "Format metadata insertion payload is incomplete.";
            edit = insert_format_metadata_field_action_t{target, action.metadata_format_index,
                                                         action.metadata_format_name,
                                                         action.metadata_format_value};
        }
        return publish(apply_metadata_edit(history, action.metadata_document_revision, edit));
    }
    case WORM_UI_METADATA_EDIT_NONE:
        break;
    }
    return "Invalid metadata edit action.";
}

auto compose_result_status(const wormedit::editor_interaction_result_t& result,
                           std::string_view accepted) -> std::string {
    if (result.accepted) {
        return std::string(accepted);
    }
    return result.diagnostics.empty() ? "Compose action was rejected."
                                      : result.diagnostics.front().message;
}

auto compose_view_for_state(const wormedit::document_history_t& history,
                            const wormedit::editor_interaction_state_t& state)
    -> std::optional<wormedit::editor_interaction_view_model_t> {
    const auto setup = wormedit::make_editor_interaction(
        history, state.chart, state.active_lane, state.cursor, state.supported_snap_denominators,
        state.active_snap_denominator, state.viewport_height_pixels);
    if (!setup.view) {
        return std::nullopt;
    }
    auto view = *setup.view;
    view.pixels_per_standard_measure = state.pixels_per_standard_measure;
    view.viewport_scroll_pixels = state.viewport_scroll_pixels;
    view.pending_hold = state.pending_hold;
    view.selected_notes = state.selected_notes;
    view.selected_objects = state.selected_objects;
    return view;
}

auto dispatch_compose_input_action(const WormUiWorkspaceAction& ui_action,
                                   wormedit::document_history_t& history,
                                   wormedit::workspace_controller_t& workspace,
                                   wormedit::editor_interaction_state_t& state,
                                   wormedit::viewport_input_controller_t& viewport_input)
    -> std::string {
    using namespace wormedit;
    if (workspace.active_tab != workspace_tab_t::compose || !workspace.selected_chart ||
        workspace.selected_chart->value != ui_action.compose_input_chart_id ||
        state.chart.value != ui_action.compose_input_chart_id ||
        history.document.revision != ui_action.compose_input_document_revision ||
        !std::isfinite(ui_action.compose_input_viewport_width_pixels) ||
        ui_action.compose_input_viewport_width_pixels <= 0.0) {
        return "Compose input ignored because the chart or document revision changed.";
    }
    if (ui_action.compose_input_count > WORM_UI_COMPOSE_INPUT_ACTION_CAPACITY) {
        (void)dispatch_editor_action(history, state, cancel_editor_gesture_action_t{});
        return "Compose input batch exceeded its capacity; the active gesture was canceled.";
    }
    if (ui_action.compose_viewport_input_count > WORM_UI_COMPOSE_VIEWPORT_INPUT_CAPACITY) {
        (void)dispatch_editor_action(history, state, cancel_editor_gesture_action_t{});
        return "Compose viewport input exceeded its capacity; the active gesture was canceled.";
    }

    auto status = std::string{};
    const auto selection_mode = [](WormUiComposeSelectionMode mode)
        -> std::optional<wormedit::editor_area_selection_mode_t> {
        switch (mode) {
        case WORM_UI_COMPOSE_SELECTION_REPLACE:
            return wormedit::editor_area_selection_mode_t::replace;
        case WORM_UI_COMPOSE_SELECTION_ADD:
            return wormedit::editor_area_selection_mode_t::add;
        case WORM_UI_COMPOSE_SELECTION_TOGGLE:
            return wormedit::editor_area_selection_mode_t::toggle;
        }
        return std::nullopt;
    };
    for (std::size_t index = 0; index < ui_action.compose_input_count; ++index) {
        if (history.document.revision != ui_action.compose_input_document_revision ||
            workspace.active_tab != workspace_tab_t::compose || !workspace.selected_chart ||
            workspace.selected_chart->value != ui_action.compose_input_chart_id ||
            state.chart.value != ui_action.compose_input_chart_id) {
            return "Remaining Compose input was discarded after the document revision changed.";
        }

        const auto& input = ui_action.compose_inputs[index];
        editor_interaction_result_t result;
        std::string_view accepted = "Compose interaction applied.";
        switch (input.kind) {
        case WORM_UI_COMPOSE_INPUT_MOVE_CURSOR_BACKWARD:
            result = dispatch_editor_action(
                history, state, move_edit_cursor_action_t{interaction_direction_t::backward});
            accepted = "Edit cursor moved backward.";
            break;
        case WORM_UI_COMPOSE_INPUT_MOVE_CURSOR_FORWARD:
            result = dispatch_editor_action(
                history, state, move_edit_cursor_action_t{interaction_direction_t::forward});
            accepted = "Edit cursor moved forward.";
            break;
        case WORM_UI_COMPOSE_INPUT_DELETE_NOTES:
            result = dispatch_editor_action(history, state, delete_editor_notes_action_t{});
            accepted = result.document_changed ? "Note deleted." : "No note at the cursor.";
            break;
        case WORM_UI_COMPOSE_INPUT_LANE_KEY_PRESS:
            if (input.lane_id == 0) {
                return "Lane-key input did not identify a chart lane.";
            }
            if (!state.selected_notes.empty()) {
                result = dispatch_editor_action(history, state,
                                                assign_selected_notes_to_lane_action_t{
                                                    lane_id_t{input.lane_id}, input.successive});
                accepted = input.successive ? "Next selected note assigned to lane."
                                            : "Selected notes assigned to lane.";
            } else {
                result = dispatch_editor_action(
                    history, state, begin_lane_key_hold_gesture_action_t{lane_id_t{input.lane_id}});
                accepted = "Lane hold started; move the edit cursor and release the key to finish.";
            }
            break;
        case WORM_UI_COMPOSE_INPUT_LANE_KEY_RELEASE:
            if (state.pending_hold.has_value() &&
                state.pending_hold->gesture_source == hold_gesture_source_t::keyboard_lane) {
                result = dispatch_editor_action(history, state,
                                                release_lane_key_hold_gesture_action_t{});
                accepted = result.document_changed ? "Lane hold committed." : "Lane hold released.";
            } else {
                result.accepted = true;
                accepted = "Lane key released.";
            }
            break;
        case WORM_UI_COMPOSE_INPUT_ALT_POINTER_BEGIN: {
            const auto chart =
                std::ranges::find(history.document.charts, state.chart, &chart_document_t::id);
            const auto current_view = compose_view_for_state(history, state);
            if (chart == history.document.charts.end() || !current_view) {
                return "The selected chart field is no longer available.";
            }
            const auto hit_index = build_editor_hit_test_index(*chart, history.document.revision);
            if (!std::holds_alternative<editor_hit_test_index_t>(hit_index)) {
                return "The chart field could not be indexed for hold input.";
            }
            const auto target = hit_test_editor(
                *chart, std::get<editor_hit_test_index_t>(hit_index), *current_view,
                {.x_pixels = input.x_pixels,
                 .y_pixels = input.y_pixels,
                 .field_left_pixels = 0.0,
                 .field_width_pixels = ui_action.compose_input_viewport_width_pixels});
            if (!target) {
                return "Alt/Option-drag must begin over an editable lane.";
            }
            if (target->note && target->note_kind &&
                (*target->note_kind == note_kind_t::tap ||
                 *target->note_kind == note_kind_t::hold)) {
                state.active_lane = target->lane;
                state.cursor = target->position;
                result = dispatch_editor_action(
                    history, state, begin_note_duration_gesture_action_t{*target->note});
                accepted = "Note resize started; drag to change duration and release to finish.";
            } else if (target->note) {
                return "Alt/Option-drag resizes tap and hold notes; this note kind is not "
                       "resizable.";
            } else {
                result = dispatch_editor_action(history, state,
                                                begin_alt_pointer_hold_gesture_action_t{
                                                    .lane = target->lane,
                                                    .position = target->position,
                                                    .alt_or_option_down = true,
                                                });
                accepted = "Pointer hold started; release to finish.";
            }
            break;
        }
        case WORM_UI_COMPOSE_INPUT_ALT_POINTER_UPDATE:
        case WORM_UI_COMPOSE_INPUT_ALT_POINTER_RELEASE: {
            const auto chart =
                std::ranges::find(history.document.charts, state.chart, &chart_document_t::id);
            const auto current_view = compose_view_for_state(history, state);
            if (chart == history.document.charts.end() || !current_view) {
                (void)dispatch_editor_action(history, state, cancel_editor_gesture_action_t{});
                return "The selected chart field is no longer available.";
            }
            const auto y = std::clamp(input.y_pixels, 0.0,
                                      std::max(0.0, current_view->viewport_height_pixels - 0.001));
            const auto position = editor_position_at_y_pixels(*chart, *current_view, y);
            if (!position) {
                (void)dispatch_editor_action(history, state, cancel_editor_gesture_action_t{});
                return "Pointer hold input fell outside chart geometry and was canceled.";
            }
            if (state.pending_note_duration_gesture) {
                if (input.kind == WORM_UI_COMPOSE_INPUT_ALT_POINTER_UPDATE) {
                    result = dispatch_editor_action(
                        history, state, update_note_duration_gesture_action_t{*position});
                    accepted = "Note resize preview updated.";
                } else {
                    result = dispatch_editor_action(
                        history, state, release_note_duration_gesture_action_t{*position});
                    accepted = result.document_changed ? "Note duration committed."
                                                       : "Note resize released.";
                }
            } else if (input.kind == WORM_UI_COMPOSE_INPUT_ALT_POINTER_UPDATE) {
                result = dispatch_editor_action(history, state,
                                                update_alt_pointer_hold_gesture_action_t{
                                                    .position = *position,
                                                    .alt_or_option_down = true,
                                                    .pointer_captured = true,
                                                });
                accepted = "Pointer hold preview updated.";
            } else {
                result = dispatch_editor_action(history, state,
                                                release_alt_pointer_hold_gesture_action_t{
                                                    .position = *position,
                                                    .alt_or_option_down = true,
                                                    .pointer_captured = true,
                                                });
                accepted =
                    result.document_changed ? "Pointer hold committed." : "Pointer hold released.";
            }
            break;
        }
        case WORM_UI_COMPOSE_INPUT_AREA_SELECTION_BEGIN:
            if (!selection_mode(input.selection_mode).has_value()) {
                return "Compose area selection used an unknown selection mode.";
            }
            result = dispatch_editor_action(
                history, state,
                begin_editor_area_selection_action_t{
                    .x_pixels = input.x_pixels,
                    .y_pixels = input.y_pixels,
                    .field_left_pixels = 0.0,
                    .field_width_pixels = ui_action.compose_input_viewport_width_pixels,
                    .mode = *selection_mode(input.selection_mode),
                });
            accepted = "Area selection started.";
            break;
        case WORM_UI_COMPOSE_INPUT_AREA_SELECTION_UPDATE:
            result = dispatch_editor_action(
                history, state,
                update_editor_area_selection_action_t{input.x_pixels, input.y_pixels, true});
            accepted = "Area selection preview updated.";
            break;
        case WORM_UI_COMPOSE_INPUT_AREA_SELECTION_RELEASE:
            result = dispatch_editor_action(
                history, state,
                commit_editor_area_selection_action_t{input.x_pixels, input.y_pixels, true});
            accepted = "Area selection committed.";
            break;
        case WORM_UI_COMPOSE_INPUT_SPACE_RANGE_SELECTION:
            if (!selection_mode(input.selection_mode).has_value()) {
                return "Compose Space selection used an unknown selection mode.";
            }
            result = dispatch_editor_action(history, state,
                                            space_range_selection_action_t{
                                                .mode = *selection_mode(input.selection_mode),
                                            });
            accepted = state.pending_space_range_selection
                           ? "Space range selection started; move the cursor and press Space again."
                           : "Space range selection committed.";
            break;
        case WORM_UI_COMPOSE_INPUT_POINTER_CLICK: {
            const auto chart =
                std::ranges::find(history.document.charts, state.chart, &chart_document_t::id);
            auto current_view = compose_view_for_state(history, state);
            if (chart == history.document.charts.end() || !current_view) {
                return "The selected chart field is no longer available.";
            }
            const auto hit_index = build_editor_hit_test_index(*chart, history.document.revision);
            if (!std::holds_alternative<editor_hit_test_index_t>(hit_index)) {
                return "The chart field could not be indexed for pointer input.";
            }
            const auto target = hit_test_editor(
                *chart, std::get<editor_hit_test_index_t>(hit_index), *current_view,
                {.x_pixels = input.x_pixels,
                 .y_pixels = input.y_pixels,
                 .field_left_pixels = 0.0,
                 .field_width_pixels = ui_action.compose_input_viewport_width_pixels});
            if (!target) {
                return "Click inside an editable lane to place or select a note.";
            }
            state.active_lane = target->lane;
            state.cursor = target->position;
            if (target->note && target->note_kind != note_kind_t::tap) {
                result = dispatch_editor_action(history, state,
                                                select_editor_notes_action_t{{*target->note}});
                accepted = "Note selected.";
            } else {
                result =
                    dispatch_editor_action(history, state, place_tap_action_t{target->position});
                accepted =
                    result.document_changed ? "Tap note toggled." : "Tap note was not changed.";
            }
            break;
        }
        case WORM_UI_COMPOSE_INPUT_UNDO:
        case WORM_UI_COMPOSE_INPUT_REDO: {
            (void)dispatch_editor_action(history, state, cancel_editor_gesture_action_t{});
            (void)dispatch_editor_action(history, state, select_editor_notes_action_t{});
            const bool changed = input.kind == WORM_UI_COMPOSE_INPUT_UNDO
                                     ? undo_document_command(history)
                                     : redo_document_command(history);
            status = changed ? (input.kind == WORM_UI_COMPOSE_INPUT_UNDO ? "Undo applied."
                                                                         : "Redo applied.")
                             : (input.kind == WORM_UI_COMPOSE_INPUT_UNDO ? "Nothing to undo."
                                                                         : "Nothing to redo.");
            if (changed) {
                const auto rebound = rebind_workspace(workspace, history);
                if (!rebound) {
                    return rebound.diagnostics.empty()
                               ? "History changed, but workspace rebinding failed."
                               : rebound.diagnostics.front().message;
                }
                return status;
            }
            continue;
        }
        case WORM_UI_COMPOSE_INPUT_CANCEL_GESTURE:
            result = dispatch_editor_action(history, state, cancel_editor_gesture_action_t{});
            accepted = "Compose gesture canceled.";
            break;
        case WORM_UI_COMPOSE_INPUT_NONE:
        default:
            continue;
        }
        status = compose_result_status(result, accepted);
        if (result.accepted && result.document_changed) {
            const auto rebound = rebind_workspace(workspace, history);
            if (!rebound) {
                return rebound.diagnostics.empty()
                           ? "Compose edit applied, but workspace rebinding failed."
                           : rebound.diagnostics.front().message;
            }
        }
    }

    for (std::size_t index = 0; index < ui_action.compose_viewport_input_count; ++index) {
        if (history.document.revision != ui_action.compose_input_document_revision ||
            workspace.active_tab != workspace_tab_t::compose || !workspace.selected_chart ||
            workspace.selected_chart->value != ui_action.compose_input_chart_id ||
            state.chart.value != ui_action.compose_input_chart_id) {
            return "Remaining Compose viewport input was discarded after the chart or document "
                   "changed.";
        }
        const auto& input = ui_action.compose_viewport_inputs[index];
        const auto actions = viewport_input.handle_wheel({
            .pane = viewport_pane_t::chart,
            .kind = input.kind == WORM_UI_COMPOSE_VIEWPORT_INPUT_COARSE_WHEEL
                        ? viewport_wheel_kind_t::coarse_steps
                        : viewport_wheel_kind_t::smooth_pixels,
            .delta_x = input.delta_x,
            .delta_y = input.delta_y,
            .zoom_modifier = input.zoom_modifier,
            .cursor_x_pixels = input.cursor_x_pixels,
            .cursor_y_pixels = input.cursor_y_pixels,
        });
        for (std::size_t action_index = 0; action_index < actions.count; ++action_index) {
            const auto& viewport_action = actions[action_index];
            if (const auto* scroll = std::get_if<viewport_scroll_action_t>(&viewport_action)) {
                if (scroll->pane != viewport_pane_t::chart || scroll->delta_y_pixels == 0.0) {
                    continue;
                }
                const auto result = dispatch_editor_action(
                    history, state, scroll_editor_view_action_t{scroll->delta_y_pixels});
                status = compose_result_status(result, "Compose field scrolled.");
            } else if (const auto* zoom = std::get_if<viewport_zoom_action_t>(&viewport_action)) {
                if (zoom->pane != viewport_pane_t::chart) {
                    continue;
                }
                const auto result = dispatch_editor_action(
                    history, state,
                    adjust_editor_zoom_action_t{.factor = zoom->factor,
                                                .anchor_screen_y_pixels = zoom->cursor_y_pixels});
                status = compose_result_status(result, "Compose field zoomed at the pointer.");
            }
        }
    }
    return status.empty() ? "Compose input handled." : status;
}

auto dispatch_compose_view_action(const WormUiWorkspaceAction& action,
                                  wormedit::document_history_t& history,
                                  wormedit::workspace_controller_t& workspace,
                                  wormedit::editor_interaction_state_t& state,
                                  double viewport_height) -> std::string {
    using namespace wormedit;
    if (workspace.active_tab != workspace_tab_t::compose || !workspace.selected_chart ||
        workspace.selected_chart->value != action.compose_input_chart_id ||
        state.chart.value != action.compose_input_chart_id ||
        history.document.revision != action.compose_input_document_revision) {
        return "Compose view action ignored because the chart or document revision changed.";
    }
    editor_interaction_result_t result;
    std::string status;
    switch (action.compose_action) {
    case WORM_UI_COMPOSE_ACTION_ZOOM_IN:
        result = dispatch_editor_action(
            history, state,
            adjust_editor_zoom_action_t{.factor = 1.25, .anchor_screen_y_pixels = std::nullopt});
        status = "Compose field zoomed in.";
        break;
    case WORM_UI_COMPOSE_ACTION_ZOOM_OUT:
        result = dispatch_editor_action(
            history, state,
            adjust_editor_zoom_action_t{.factor = 1.0 / 1.25,
                                        .anchor_screen_y_pixels = std::nullopt});
        status = "Compose field zoomed out.";
        break;
    case WORM_UI_COMPOSE_ACTION_SCROLL_UP:
    case WORM_UI_COMPOSE_ACTION_SCROLL_DOWN: {
        const auto direction =
            action.compose_action == WORM_UI_COMPOSE_ACTION_SCROLL_UP ? -1.0 : 1.0;
        result = dispatch_editor_action(
            history, state,
            scroll_editor_view_action_t{direction * std::max(64.0, viewport_height * 0.75)});
        status = direction < 0.0 ? "Compose field scrolled up." : "Compose field scrolled down.";
        break;
    }
    case WORM_UI_COMPOSE_ACTION_RESET_VIEW: {
        const auto factor = default_pixels_per_standard_measure / state.pixels_per_standard_measure;
        result = dispatch_editor_action(
            history, state,
            adjust_editor_zoom_action_t{.factor = factor, .anchor_screen_y_pixels = std::nullopt});
        if (result) {
            result = dispatch_editor_action(
                history, state, scroll_editor_view_action_t{-state.viewport_scroll_pixels});
        }
        status = "Compose field view reset.";
        break;
    }
    case WORM_UI_COMPOSE_ACTION_NONE:
        return {};
    }
    return compose_result_status(result, status);
}

auto editor_interaction_has_pending_gesture(const wormedit::editor_interaction_state_t& state)
    -> bool {
    return state.pending_hold.has_value() || state.pending_note_duration_gesture.has_value() ||
           state.pending_note_paint_gesture.has_value() ||
           state.pending_editor_area_selection.has_value() ||
           state.pending_space_range_selection.has_value();
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

    if (!SDL_Init(0)) {
        std::fprintf(stderr, "SDL init failed\n");
        return 1;
    }
    const auto platform_recovery = wormedit::prepare_platform_recovery_directory();
    if (platform_recovery) {
        startup_request.recovery_directories.push_back(platform_recovery.path);
    }
    auto startup = wormedit::prepare_native_project_startup(
        initial_document(new_session_project_id()), std::move(startup_request));

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
    const auto initial_autosave_sequence = wormedit::native_project_recovery_session_sequence(
        startup.recovery_scan, history.document.id);
    wormedit::task_scheduler_t autosave_scheduler(1, 1);
    wormedit::native_project_autosave_loop_t autosave_loop(
        wormedit::native_project_autosave_loop_t::clock_t::now(), autosave_scheduler,
        startup.primary_version, initial_autosave_sequence);
    std::string status;
    if (startup.loaded_primary) {
        status = "Opened native project: " + requested_primary_path.generic_string();
    } else if (!requested_primary_path.empty()) {
        status = "Primary project was not loaded; review the completed recovery scan.";
    } else if (platform_recovery) {
        status = "New unsaved workspace ready; platform recovery scan complete.";
    } else {
        status = "New unsaved workspace ready; recovery discovery unavailable.";
    }
    if (!platform_recovery && !platform_recovery.error.empty()) {
        status += " " + platform_recovery.error;
    }
    wormedit::native_project_recovery_controller_t recovery_controller;
    if (startup.recovery_scan_configured)
        (void)recovery_controller.replace_scan(std::move(startup.recovery_scan));
    const auto recovery_scan_configured = startup.recovery_scan_configured;
    const auto scanned_primary_version = startup.primary_version;

    timing_scene_cache_t timing_scene_cache;
    std::optional<wormedit::timing_lane_pseudonote_preview_t> timing_stage;
    compose_scene_cache_t compose_scene_cache;
    std::optional<wormedit::editor_interaction_state_t> compose_interaction_state;
    wormedit::viewport_input_controller_t compose_viewport_input;
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

        const auto autosave_target = wormedit::make_native_project_autosave_target(
            history.document.id, startup.loaded_primary, requested_primary_path,
            platform_recovery.path);
        if (const auto autosave_result = autosave_loop.tick(
                history.document, wormedit::is_document_dirty(history),
                wormedit::native_project_autosave_loop_t::clock_t::now(), autosave_target)) {
            const auto autosave_text = autosave_status_text(*autosave_result);
            if (!autosave_text.empty())
                status = autosave_text;
        }

        if (compose_interaction_state) {
            const auto& state = *compose_interaction_state;
            const bool selected_chart_changed =
                !workspace.selected_chart || *workspace.selected_chart != state.chart;
            const bool pending_is_stale =
                (state.pending_hold &&
                 state.pending_hold->document_revision != history.document.revision) ||
                (state.pending_note_duration_gesture &&
                 state.pending_note_duration_gesture->document_revision !=
                     history.document.revision) ||
                (state.pending_note_paint_gesture &&
                 state.pending_note_paint_gesture->document_revision !=
                     history.document.revision) ||
                (state.pending_editor_area_selection &&
                 state.pending_editor_area_selection->document_revision !=
                     history.document.revision) ||
                (state.pending_space_range_selection &&
                 state.pending_space_range_selection->document_revision !=
                     history.document.revision);
            const bool compose_inactive =
                workspace.active_tab != wormedit::workspace_tab_t::compose;
            if (editor_interaction_has_pending_gesture(state) &&
                (selected_chart_changed || pending_is_stale || compose_inactive)) {
                (void)wormedit::dispatch_editor_action(history, *compose_interaction_state,
                                                       wormedit::cancel_editor_gesture_action_t{});
            }
            if (selected_chart_changed) {
                compose_interaction_state.reset();
            }
        }

        const auto model = wormedit::make_workspace_view(workspace, history.document);
        if (timing_stage &&
            (timing_stage->document_revision != history.document.revision ||
             workspace.active_tab != wormedit::workspace_tab_t::timing ||
             !workspace.selected_chart || *workspace.selected_chart != timing_stage->chart)) {
            wormedit::cancel_timing_lane_pseudonote(*timing_stage);
            timing_stage.reset();
        }
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
            for (std::size_t row = 0; row < flattened.view.recovery_candidate_count; ++row) {
                flattened.view.recovery_candidates[row].path_label =
                    flattened.recovery_path_storage[row].c_str();
                flattened.view.recovery_candidates[row].summary =
                    flattened.recovery_summary_storage[row].c_str();
                flattened.view.recovery_candidates[row].disabled_reason =
                    flattened.recovery_disabled_storage[row].empty()
                        ? nullptr
                        : flattened.recovery_disabled_storage[row].c_str();
            }
            for (std::size_t diagnostic = 0; diagnostic < flattened.view.recovery_diagnostic_count;
                 ++diagnostic) {
                flattened.view.recovery_diagnostics[diagnostic] =
                    flattened.recovery_diagnostic_storage[diagnostic].c_str();
            }
            flattened.view.compose.branch_rows = flattened.compose_branch_row_storage.data();
            for (std::size_t row = 0; row < flattened.view.compose.branch_row_count; ++row) {
                flattened.compose_branch_row_storage[row].display_text =
                    flattened.compose_branch_text_storage[row].c_str();
            }
            for (std::size_t diagnostic = 0;
                 diagnostic < flattened.view.compose.branch_diagnostic_count; ++diagnostic) {
                flattened.view.compose.branch_diagnostics[diagnostic] =
                    flattened.compose_branch_diagnostic_storage[diagnostic].c_str();
            }
            if (flattened.view.metadata.visible) {
                flattened.view.metadata.search_tags =
                    flattened.view.metadata.search_tags_editable
                        ? flattened.metadata_search_tag_storage.data()
                        : nullptr;
                flattened.view.metadata.search_tag_bytes =
                    flattened.view.metadata.search_tags_editable
                        ? flattened.metadata_search_tag_length_storage.data()
                        : nullptr;
                flattened.view.metadata.format_fields =
                    flattened.metadata_format_field_storage.data();
                flattened.view.metadata.format_targets =
                    flattened.metadata_format_target_storage.data();
            }
            int compose_window_width = 0;
            int compose_window_height = 0;
            worm_window_get_size(&window, &compose_window_width, &compose_window_height);
            populate_timing_view(flattened, history, workspace, timing_scene_cache,
                                 compose_window_width, compose_window_height,
                                 timing_stage ? &*timing_stage : nullptr);
            populate_compose_view(flattened, history, workspace, compose_scene_cache,
                                  compose_window_width, compose_window_height,
                                  compose_interaction_state);
            const auto action = worm_ui_frontend_compose(&flattened.view);
            if (action.kind != WORM_UI_WORKSPACE_ACTION_QUIT &&
                (action.compose_input_count != 0 || action.compose_viewport_input_count != 0) &&
                compose_interaction_state) {
                status = dispatch_compose_input_action(
                    action, history, workspace, *compose_interaction_state, compose_viewport_input);
            }
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
            } else if (action.metadata_edit != WORM_UI_METADATA_EDIT_NONE) {
                status = dispatch_metadata_ui_action(action, history, workspace);
            } else if (action.timing_stage_action != WORM_UI_TIMING_STAGE_NONE) {
                using status_t = wormedit::timing_lane_pseudonote_status_t;
                if (action.timing_stage_action == WORM_UI_TIMING_STAGE_BEGIN) {
                    if (!timing_stage) {
                        auto candidate = begin_timing_stage(action, history, workspace);
                        if (candidate.status == status_t::editing) {
                            timing_stage = std::move(candidate);
                            status = "Timing event staged; confirm to insert it.";
                        } else if (!candidate.diagnostics.empty()) {
                            status = candidate.diagnostics.front().message;
                        } else {
                            status = "Timing event could not be staged at that row.";
                        }
                    }
                } else if (action.timing_stage_action == WORM_UI_TIMING_STAGE_CANCEL) {
                    if (timing_stage) {
                        wormedit::cancel_timing_lane_pseudonote(*timing_stage);
                        timing_stage.reset();
                        status = "Timing event staging cancelled.";
                    }
                } else if (timing_stage) {
                    const bool stage_matches =
                        workspace.active_tab == wormedit::workspace_tab_t::timing &&
                        workspace.selected_chart &&
                        workspace.selected_chart->value == action.timing_chart_id &&
                        timing_stage->chart.value == action.timing_chart_id &&
                        timing_stage->document_revision == action.timing_document_revision &&
                        history.document.revision == action.timing_document_revision;
                    if (!stage_matches) {
                        wormedit::cancel_timing_lane_pseudonote(*timing_stage);
                        timing_stage.reset();
                        status =
                            "Timing event staging expired because its chart or revision changed.";
                    } else {
                        const auto chart =
                            std::ranges::find(history.document.charts, timing_stage->chart,
                                              &wormedit::chart_document_t::id);
                        const auto value = chart == history.document.charts.end()
                                               ? std::optional<wormedit::timing_lane_value_t>{}
                                               : timing_stage_value(*timing_stage, *chart, action);
                        if (!value) {
                            timing_stage->valid = false;
                            timing_stage->diagnostics = {{
                                .severity = wormedit::diagnostic_severity_t::error,
                                .code = wormedit::diagnostic_code_t::invalid_rational,
                                .message = "Enter a bounded exact rational for this timing value.",
                            }};
                            status = "Timing value is outside its supported bounds.";
                        } else {
                            const bool valid = wormedit::update_timing_lane_pseudonote(
                                history, *timing_stage, *value, false);
                            if (action.timing_stage_action == WORM_UI_TIMING_STAGE_CONFIRM &&
                                valid) {
                                const auto result = wormedit::confirm_timing_lane_pseudonote(
                                    history, *timing_stage, false);
                                status = result ? "Timing event inserted."
                                                : (result.diagnostics.empty()
                                                       ? "Timing event insertion was rejected."
                                                       : result.diagnostics.front().message);
                                timing_stage.reset();
                            } else if (action.timing_stage_action == WORM_UI_TIMING_STAGE_CONFIRM) {
                                status = timing_stage->diagnostics.empty()
                                             ? "Timing event values are invalid."
                                             : timing_stage->diagnostics.front().message;
                            }
                        }
                    }
                }
            } else if (action.compose_action != WORM_UI_COMPOSE_ACTION_NONE) {
                if (compose_interaction_state) {
                    status = dispatch_compose_view_action(
                        action, history, workspace, *compose_interaction_state,
                        flattened.view.compose.viewport_height_pixels);
                }
            } else if (action.kind >= WORM_UI_WORKSPACE_ACTION_RECOVERY_INSPECT &&
                       action.kind <= WORM_UI_WORKSPACE_ACTION_RECOVERY_QUARANTINE) {
                if (autosave_loop.busy()) {
                    status = "Wait for the current autosave to finish before recovering a session.";
                } else {
                    status =
                        dispatch_recovery_ui_action(action, recovery_controller, history, workspace,
                                                    scanned_primary_version, autosave_loop);
                }
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
